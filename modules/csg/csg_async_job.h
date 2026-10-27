/**************************************************************************/
/*  csg_async_job.h                                                       */
/**************************************************************************/

#pragma once

#include "core/object/ref_counted.h"
#include "core/object/worker_thread_pool.h"
#include "core/templates/hash_map.h"
#include "scene/resources/mesh.h"

#include <atomic>

class CSGShape3D;
class StaticBody3D;
class Image;

class CSGAsyncJob : public RefCounted {
	GDCLASS(CSGAsyncJob, RefCounted);

public:
	enum Status {
		STATUS_PENDING,
		STATUS_COMPLETED,
		STATUS_FAILED,
		STATUS_CANCELLED,
	};

private:
	CSGShape3D *root = nullptr;
	WorkerThreadPool::TaskID task_id = WorkerThreadPool::INVALID_TASK_ID;
	std::atomic<bool> cancel_requested{ false };
	bool create_static_body = false;
	bool worker_succeeded = false;
	Status status = STATUS_PENDING;
	String error_message;
	Ref<ArrayMesh> mesh;
	StaticBody3D *static_body = nullptr;

	static void _run_worker(void *p_userdata);
	void _finish();
	void _finish_internal(bool p_emit_signal);
	static void _prepare_tree(CSGShape3D *p_shape, HashMap<ObjectID, Ref<Image>> &r_images);
	static void _clear_prepared_tree(CSGShape3D *p_shape);

protected:
	static void _bind_methods();

public:
	static Ref<CSGAsyncJob> start(CSGShape3D *p_root, bool p_create_static_body);
	static void initialize();
	static void shutdown();

	void cancel();
	Status get_status() const;
	bool is_finished() const;
	String get_error_message() const;
	Ref<ArrayMesh> get_mesh() const;
	StaticBody3D *take_static_body();

	~CSGAsyncJob();
};

VARIANT_ENUM_CAST(CSGAsyncJob::Status);
