/**************************************************************************/
/*  csg_async_job.cpp                                                     */
/**************************************************************************/

#include "csg_async_job.h"

#include "csg_shape.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/thread.h"
#include "core/templates/hash_map.h"
#include "scene/3d/physics/static_body_3d.h"
#include "scene/resources/texture.h"

#ifndef PHYSICS_3D_DISABLED
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/resources/3d/concave_polygon_shape_3d.h"
#endif

// Accessed only on the main thread. Keeps jobs and their orphan trees alive
// even if the caller drops its reference before the worker completes.
static HashMap<ObjectID, Ref<CSGAsyncJob>> active_csg_jobs;
static bool csg_jobs_shutting_down = false;

static bool _csg_tree_is_queued_for_deletion(Node *p_node) {
	if (p_node->is_queued_for_deletion()) {
		return true;
	}
	for (int i = 0; i < p_node->get_child_count(true); i++) {
		if (_csg_tree_is_queued_for_deletion(p_node->get_child(i, true))) {
			return true;
		}
	}
	return false;
}

void CSGAsyncJob::_bind_methods() {
	ClassDB::bind_method(D_METHOD("cancel"), &CSGAsyncJob::cancel);
	ClassDB::bind_method(D_METHOD("get_status"), &CSGAsyncJob::get_status);
	ClassDB::bind_method(D_METHOD("is_finished"), &CSGAsyncJob::is_finished);
	ClassDB::bind_method(D_METHOD("get_error_message"), &CSGAsyncJob::get_error_message);
	ClassDB::bind_method(D_METHOD("get_mesh"), &CSGAsyncJob::get_mesh);
	ClassDB::bind_method(D_METHOD("take_static_body"), &CSGAsyncJob::take_static_body);
	ADD_SIGNAL(MethodInfo("completed"));
	BIND_ENUM_CONSTANT(STATUS_PENDING);
	BIND_ENUM_CONSTANT(STATUS_COMPLETED);
	BIND_ENUM_CONSTANT(STATUS_FAILED);
	BIND_ENUM_CONSTANT(STATUS_CANCELLED);
}

void CSGAsyncJob::_prepare_tree(CSGShape3D *p_shape, HashMap<ObjectID, Ref<Image>> &r_images) {
	p_shape->dirty = true;
	if (CSGHeightMap3D *height_map = Object::cast_to<CSGHeightMap3D>(p_shape)) {
		const Ref<Texture2D> texture = height_map->height_map;
		height_map->async_height_image_prepared = true;
		if (texture.is_valid()) {
			const ObjectID texture_id = texture->get_instance_id();
			if (!r_images.has(texture_id)) {
				r_images.insert(texture_id, texture->get_image());
			}
			height_map->async_height_image = r_images[texture_id];
		}
	} else if (Object::cast_to<CSGMesh3D>(p_shape)) {
		// ArrayMesh and other mesh types can read back through RenderingServer.
		// Prepare their local input brush before the worker starts.
		p_shape->async_prebuilt_brush = p_shape->_build_brush();
	}
	for (int i = 0; i < p_shape->get_child_count(); i++) {
		CSGShape3D *child = Object::cast_to<CSGShape3D>(p_shape->get_child(i));
		if (child) {
			_prepare_tree(child, r_images);
		}
	}
}

void CSGAsyncJob::_clear_prepared_tree(CSGShape3D *p_shape) {
	if (p_shape->async_prebuilt_brush) {
		memdelete(p_shape->async_prebuilt_brush);
		p_shape->async_prebuilt_brush = nullptr;
	}
	if (CSGHeightMap3D *height_map = Object::cast_to<CSGHeightMap3D>(p_shape)) {
		height_map->async_height_image.unref();
		height_map->async_height_image_prepared = false;
	}
	for (int i = 0; i < p_shape->get_child_count(); i++) {
		CSGShape3D *child = Object::cast_to<CSGShape3D>(p_shape->get_child(i));
		if (child) {
			_clear_prepared_tree(child);
		}
	}
}

Ref<CSGAsyncJob> CSGAsyncJob::start(CSGShape3D *p_root, bool p_create_static_body) {
	ERR_FAIL_COND_V_MSG(!Thread::is_main_thread(), Ref<CSGAsyncJob>(), "CSG async jobs must be started on the main thread.");
	ERR_FAIL_COND_V_MSG(csg_jobs_shutting_down, Ref<CSGAsyncJob>(), "CSG async generation is shutting down.");
	ERR_FAIL_NULL_V(p_root, Ref<CSGAsyncJob>());
	ERR_FAIL_COND_V_MSG(p_root->is_inside_tree() || p_root->get_parent(), Ref<CSGAsyncJob>(), "CSG async generation requires an unparented root outside the scene tree.");
	ERR_FAIL_COND_V_MSG(p_root->async_job_owned, Ref<CSGAsyncJob>(), "This CSG tree is already owned by an async job.");
	ERR_FAIL_COND_V_MSG(_csg_tree_is_queued_for_deletion(p_root), Ref<CSGAsyncJob>(), "A node in the CSG tree is queued for deletion.");
	ERR_FAIL_COND_V_MSG(!WorkerThreadPool::get_singleton(), Ref<CSGAsyncJob>(), "WorkerThreadPool is unavailable.");
	ERR_FAIL_COND_V_MSG(WorkerThreadPool::get_singleton()->get_thread_count() == 0, Ref<CSGAsyncJob>(), "CSG async generation requires at least one worker thread.");
#ifndef THREADS_ENABLED
	ERR_FAIL_V_MSG(Ref<CSGAsyncJob>(), "CSG async generation requires thread support.");
#endif
#ifdef PHYSICS_3D_DISABLED
	ERR_FAIL_COND_V_MSG(p_create_static_body, Ref<CSGAsyncJob>(), "Physics 3D is disabled.");
#endif

	Ref<CSGAsyncJob> job;
	job.instantiate();
	job->root = p_root;
	p_root->async_job_owned = true;
	job->create_static_body = p_create_static_body;
	HashMap<ObjectID, Ref<Image>> images;
	_prepare_tree(p_root, images);
	active_csg_jobs.insert(job->get_instance_id(), job);
	job->task_id = WorkerThreadPool::get_singleton()->add_native_task(_run_worker, job.ptr(), false, "CSG async generation");
	if (job->task_id == WorkerThreadPool::INVALID_TASK_ID) {
		active_csg_jobs.erase(job->get_instance_id());
		p_root->async_job_owned = false;
		_clear_prepared_tree(p_root);
		job->root = nullptr; // Submission failed; the caller retains ownership.
		ERR_FAIL_V_MSG(Ref<CSGAsyncJob>(), "Could not schedule CSG async generation.");
	}
	return job;
}

void CSGAsyncJob::_run_worker(void *p_userdata) {
	CSGAsyncJob *job = static_cast<CSGAsyncJob *>(p_userdata);
	if (!job->cancel_requested.load()) {
		CSGBrush *result = job->root->_get_brush();
		job->worker_succeeded = result && !result->faces.is_empty();
	}
	callable_mp(job, &CSGAsyncJob::_finish).call_deferred();
}

void CSGAsyncJob::_finish() {
	_finish_internal(true);
}

void CSGAsyncJob::_finish_internal(bool p_emit_signal) {
	if (status != STATUS_PENDING) {
		return;
	}
	Ref<CSGAsyncJob> keep_alive(this);
	if (task_id != WorkerThreadPool::INVALID_TASK_ID) {
		WorkerThreadPool::get_singleton()->wait_for_task_completion(task_id);
		task_id = WorkerThreadPool::INVALID_TASK_ID;
	}

	if (cancel_requested.load()) {
		status = STATUS_CANCELLED;
	} else if (!worker_succeeded) {
		status = STATUS_FAILED;
		error_message = "CSG generation produced no geometry.";
	} else {
		mesh = root->_build_array_mesh(root->brush);
		if (mesh.is_null() || mesh->get_surface_count() == 0) {
			status = STATUS_FAILED;
			error_message = "Could not build the CSG output mesh.";
		} else {
#ifndef PHYSICS_3D_DISABLED
			if (create_static_body) {
				Ref<ConcavePolygonShape3D> collision;
				collision.instantiate();
				collision->set_faces(root->_get_brush_collision_faces());
				static_body = memnew(StaticBody3D);
				static_body->set_collision_layer(root->get_collision_layer());
				static_body->set_collision_mask(root->get_collision_mask());
				static_body->set_collision_priority(root->get_collision_priority());
				CollisionShape3D *collision_node = memnew(CollisionShape3D);
				collision_node->set_shape(collision);
				static_body->add_child(collision_node);
			}
#endif
			status = STATUS_COMPLETED;
		}
	}

	memdelete(root);
	root = nullptr;
	active_csg_jobs.erase(get_instance_id());
	if (p_emit_signal) {
		emit_signal(SNAME("completed"));
	}
}

void CSGAsyncJob::initialize() {
	csg_jobs_shutting_down = false;
}

void CSGAsyncJob::shutdown() {
	csg_jobs_shutting_down = true;
	while (!active_csg_jobs.is_empty()) {
		Ref<CSGAsyncJob> job = active_csg_jobs.begin()->value;
		job->cancel_requested.store(true);
		job->_finish_internal(false);
	}
}

void CSGAsyncJob::cancel() {
	ERR_FAIL_COND_MSG(!Thread::is_main_thread(), "CSG async job methods must be called on the main thread.");
	if (status == STATUS_PENDING) {
		cancel_requested.store(true);
	}
}

CSGAsyncJob::Status CSGAsyncJob::get_status() const {
	return status;
}

bool CSGAsyncJob::is_finished() const {
	return status != STATUS_PENDING;
}

String CSGAsyncJob::get_error_message() const {
	return error_message;
}

Ref<ArrayMesh> CSGAsyncJob::get_mesh() const {
	return mesh;
}

StaticBody3D *CSGAsyncJob::take_static_body() {
	ERR_FAIL_COND_V_MSG(!Thread::is_main_thread(), nullptr, "CSG async job methods must be called on the main thread.");
	StaticBody3D *body = static_body;
	static_body = nullptr;
	return body;
}

CSGAsyncJob::~CSGAsyncJob() {
#ifndef PHYSICS_3D_DISABLED
	if (static_body) {
		memdelete(static_body);
	}
#endif
}
