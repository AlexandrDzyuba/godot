/**************************************************************************/
/*  csg_shape.cpp                                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "csg_shape.h"

#include "csg_native_bevel.h"

#include "core/config/engine.h"
#include "core/io/image.h"
#include "core/math/geometry_2d.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/main/scene_tree.h"
#include "scene/resources/3d/navigation_mesh_source_geometry_data_3d.h"
#include "scene/resources/navigation_mesh.h"
#include "scene/resources/texture.h"
#include "servers/rendering/rendering_server.h"

#ifdef DEV_ENABLED
#include "core/io/json.h"
#endif // DEV_ENABLED

#ifndef NAVIGATION_3D_DISABLED
#include "servers/navigation_3d/navigation_server_3d.h"
#endif // NAVIGATION_3D_DISABLED

#include <manifold/manifold.h>

#include <cfloat> // FLT_EPSILON

#ifndef NAVIGATION_3D_DISABLED
Callable CSGShape3D::_navmesh_source_geometry_parsing_callback;
RID CSGShape3D::_navmesh_source_geometry_parser;

void CSGShape3D::navmesh_parse_init() {
	ERR_FAIL_NULL(NavigationServer3D::get_singleton());
	if (!_navmesh_source_geometry_parser.is_valid()) {
		_navmesh_source_geometry_parsing_callback = callable_mp_static(&CSGShape3D::navmesh_parse_source_geometry);
		_navmesh_source_geometry_parser = NavigationServer3D::get_singleton()->source_geometry_parser_create();
		NavigationServer3D::get_singleton()->source_geometry_parser_set_callback(_navmesh_source_geometry_parser, _navmesh_source_geometry_parsing_callback);
	}
}

void CSGShape3D::navmesh_parse_source_geometry(const Ref<NavigationMesh> &p_navigation_mesh, Ref<NavigationMeshSourceGeometryData3D> p_source_geometry_data, Node *p_node) {
	CSGShape3D *csgshape3d = Object::cast_to<CSGShape3D>(p_node);

	if (csgshape3d == nullptr) {
		return;
	}

	NavigationMesh::ParsedGeometryType parsed_geometry_type = p_navigation_mesh->get_parsed_geometry_type();

#ifndef PHYSICS_3D_DISABLED
	bool nav_collision = (parsed_geometry_type == NavigationMesh::PARSED_GEOMETRY_STATIC_COLLIDERS && csgshape3d->is_using_collision() && (csgshape3d->get_collision_layer() & p_navigation_mesh->get_collision_mask()));
#else
	bool nav_collision = false;
#endif // PHYSICS_3D_DISABLED
	if (parsed_geometry_type == NavigationMesh::PARSED_GEOMETRY_MESH_INSTANCES || nav_collision || parsed_geometry_type == NavigationMesh::PARSED_GEOMETRY_BOTH) {
		Array meshes = csgshape3d->get_meshes();
		if (!meshes.is_empty()) {
			Ref<Mesh> mesh = meshes[1];
			if (mesh.is_valid()) {
				p_source_geometry_data->add_mesh(mesh, csgshape3d->get_global_transform());
			}
		}
	}
}
#endif // NAVIGATION_3D_DISABLED

#ifndef PHYSICS_3D_DISABLED
void CSGShape3D::set_use_collision(bool p_enable) {
	if (use_collision == p_enable) {
		return;
	}

	use_collision = p_enable;

	if (!is_inside_tree() || !is_root_shape()) {
		return;
	}

	if (use_collision) {
		root_collision_shape.instantiate();
		root_collision_instance = PhysicsServer3D::get_singleton()->body_create();
		PhysicsServer3D::get_singleton()->body_set_mode(root_collision_instance, PhysicsServer3D::BODY_MODE_STATIC);
		PhysicsServer3D::get_singleton()->body_set_state(root_collision_instance, PhysicsServer3D::BODY_STATE_TRANSFORM, get_global_transform());
		PhysicsServer3D::get_singleton()->body_add_shape(root_collision_instance, root_collision_shape->get_rid());
		PhysicsServer3D::get_singleton()->body_set_space(root_collision_instance, get_world_3d()->get_space());
		PhysicsServer3D::get_singleton()->body_attach_object_instance_id(root_collision_instance, get_instance_id());
		set_collision_layer(collision_layer);
		set_collision_mask(collision_mask);
		set_collision_priority(collision_priority);
		_make_dirty(); //force update
	} else {
		PhysicsServer3D::get_singleton()->free_rid(root_collision_instance);
		root_collision_instance = RID();
		root_collision_shape.unref();
	}
	notify_property_list_changed();
	update_gizmos();
}

bool CSGShape3D::is_using_collision() const {
	return use_collision;
}

void CSGShape3D::set_collision_layer(uint32_t p_layer) {
	collision_layer = p_layer;
	if (root_collision_instance.is_valid()) {
		PhysicsServer3D::get_singleton()->body_set_collision_layer(root_collision_instance, p_layer);
	}
}

uint32_t CSGShape3D::get_collision_layer() const {
	return collision_layer;
}

void CSGShape3D::set_collision_mask(uint32_t p_mask) {
	collision_mask = p_mask;
	if (root_collision_instance.is_valid()) {
		PhysicsServer3D::get_singleton()->body_set_collision_mask(root_collision_instance, p_mask);
	}
}

uint32_t CSGShape3D::get_collision_mask() const {
	return collision_mask;
}

void CSGShape3D::set_collision_layer_value(int p_layer_number, bool p_value) {
	ERR_FAIL_COND_MSG(p_layer_number < 1, "Collision layer number must be between 1 and 32 inclusive.");
	ERR_FAIL_COND_MSG(p_layer_number > 32, "Collision layer number must be between 1 and 32 inclusive.");
	uint32_t layer = get_collision_layer();
	if (p_value) {
		layer |= 1 << (p_layer_number - 1);
	} else {
		layer &= ~(1 << (p_layer_number - 1));
	}
	set_collision_layer(layer);
}

bool CSGShape3D::get_collision_layer_value(int p_layer_number) const {
	ERR_FAIL_COND_V_MSG(p_layer_number < 1, false, "Collision layer number must be between 1 and 32 inclusive.");
	ERR_FAIL_COND_V_MSG(p_layer_number > 32, false, "Collision layer number must be between 1 and 32 inclusive.");
	return get_collision_layer() & (1 << (p_layer_number - 1));
}

void CSGShape3D::set_collision_mask_value(int p_layer_number, bool p_value) {
	ERR_FAIL_COND_MSG(p_layer_number < 1, "Collision layer number must be between 1 and 32 inclusive.");
	ERR_FAIL_COND_MSG(p_layer_number > 32, "Collision layer number must be between 1 and 32 inclusive.");
	uint32_t mask = get_collision_mask();
	if (p_value) {
		mask |= 1 << (p_layer_number - 1);
	} else {
		mask &= ~(1 << (p_layer_number - 1));
	}
	set_collision_mask(mask);
}

bool CSGShape3D::get_collision_mask_value(int p_layer_number) const {
	ERR_FAIL_COND_V_MSG(p_layer_number < 1, false, "Collision layer number must be between 1 and 32 inclusive.");
	ERR_FAIL_COND_V_MSG(p_layer_number > 32, false, "Collision layer number must be between 1 and 32 inclusive.");
	return get_collision_mask() & (1 << (p_layer_number - 1));
}

RID CSGShape3D::_get_root_collision_instance() const {
	if (root_collision_instance.is_valid()) {
		return root_collision_instance;
	} else if (parent_shape) {
		return parent_shape->_get_root_collision_instance();
	}

	return RID();
}

void CSGShape3D::set_collision_priority(real_t p_priority) {
	collision_priority = p_priority;
	if (root_collision_instance.is_valid()) {
		PhysicsServer3D::get_singleton()->body_set_collision_priority(root_collision_instance, p_priority);
	}
}

real_t CSGShape3D::get_collision_priority() const {
	return collision_priority;
}

void CSGShape3D::set_autosmooth(bool p_smooth) {
	autosmooth = p_smooth;
	_make_dirty();
	notify_property_list_changed();
}

bool CSGShape3D::is_autosmooth() const {
	return autosmooth;
}

void CSGShape3D::set_smoothing_angle(const float p_angle) {
	smoothing_angle = p_angle;
	_make_dirty();
}

float CSGShape3D::get_smoothing_angle() const {
	return smoothing_angle;
}

#endif // PHYSICS_3D_DISABLED

bool CSGShape3D::is_root_shape() const {
	return !parent_shape;
}

#ifndef DISABLE_DEPRECATED
void CSGShape3D::set_snap(float p_snap) {
	if (snap == p_snap) {
		return;
	}

	snap = p_snap;
	_make_dirty();
}

float CSGShape3D::get_snap() const {
	return snap;
}
#endif // DISABLE_DEPRECATED

void CSGShape3D::_make_dirty(bool p_parent_removing) {
#ifndef PHYSICS_3D_DISABLED
	if ((p_parent_removing || is_root_shape()) && !dirty) {
		callable_mp(this, &CSGShape3D::update_shape).call_deferred(); // Must be deferred; otherwise, is_root_shape() will use the previous parent.
	}
#endif // PHYSICS_3D_DISABLED

	if (!is_root_shape()) {
		parent_shape->_make_dirty();
	}
#ifndef PHYSICS_3D_DISABLED
	else if (!dirty) {
		callable_mp(this, &CSGShape3D::update_shape).call_deferred();
	}
#endif // PHYSICS_3D_DISABLED

	dirty = true;
}

enum ManifoldProperty {
	MANIFOLD_PROPERTY_POSITION_X = 0,
	MANIFOLD_PROPERTY_POSITION_Y,
	MANIFOLD_PROPERTY_POSITION_Z,
	MANIFOLD_PROPERTY_INVERT,
	MANIFOLD_PROPERTY_SMOOTH_GROUP,
	MANIFOLD_PROPERTY_UV_X_0,
	MANIFOLD_PROPERTY_UV_Y_0,
	MANIFOLD_PROPERTY_COLOR_R,
	MANIFOLD_PROPERTY_COLOR_G,
	MANIFOLD_PROPERTY_COLOR_B,
	MANIFOLD_PROPERTY_COLOR_A,
	MANIFOLD_PROPERTY_CUSTOM_0_X,
	MANIFOLD_PROPERTY_CUSTOM_0_Y,
	MANIFOLD_PROPERTY_CUSTOM_0_Z,
	MANIFOLD_PROPERTY_CUSTOM_0_W,
	MANIFOLD_PROPERTY_CUSTOM_1_X,
	MANIFOLD_PROPERTY_CUSTOM_1_Y,
	MANIFOLD_PROPERTY_CUSTOM_1_Z,
	MANIFOLD_PROPERTY_CUSTOM_1_W,
	MANIFOLD_PROPERTY_CUSTOM_2_X,
	MANIFOLD_PROPERTY_CUSTOM_2_Y,
	MANIFOLD_PROPERTY_CUSTOM_2_Z,
	MANIFOLD_PROPERTY_CUSTOM_2_W,
	MANIFOLD_PROPERTY_CUSTOM_3_X,
	MANIFOLD_PROPERTY_CUSTOM_3_Y,
	MANIFOLD_PROPERTY_CUSTOM_3_Z,
	MANIFOLD_PROPERTY_CUSTOM_3_W,
	MANIFOLD_PROPERTY_FACE_ID,
	MANIFOLD_PROPERTY_SOURCE_FACE_ID,
	MANIFOLD_PROPERTY_SURFACE_ID,
	MANIFOLD_PROPERTY_BRUSH_ID,
	MANIFOLD_PROPERTY_LAYER_ID,
	MANIFOLD_PROPERTY_FACE_GENERATION,
	MANIFOLD_PROPERTY_MAX
};

using CSGFaceSourceMap = HashMap<uint64_t, CSGBrush::FaceMetadata>;

static uint64_t _csg_face_source_key(uint32_t p_brush_id, uint32_t p_face_id) {
	return (uint64_t(p_brush_id) << 32) | p_face_id;
}

static void _unpack_manifold(
		const manifold::Manifold &p_manifold,
		const HashMap<int32_t, Ref<Material>> &p_mesh_materials,
		const CSGFaceSourceMap &p_face_sources,
		CSGBrush *r_mesh_merge,
		CSGBrush::FaceGeneration p_fallback_generation = CSGBrush::FACE_ORIGINAL) {
	manifold::MeshGL64 mesh = p_manifold.GetMeshGL64();

	constexpr int32_t order[3] = { 0, 2, 1 };

	for (size_t run_i = 0; run_i < mesh.runIndex.size() - 1; run_i++) {
		uint32_t original_id = -1;
		if (run_i < mesh.runOriginalID.size()) {
			original_id = mesh.runOriginalID[run_i];
		}

		Ref<Material> material;
		if (p_mesh_materials.has(original_id)) {
			material = p_mesh_materials[original_id];
		}
		// Find or reserve a material ID in the brush.
		int32_t material_id = r_mesh_merge->materials.find(material);
		if (material_id == -1) {
			material_id = r_mesh_merge->materials.size();
			r_mesh_merge->materials.push_back(material);
		}

		size_t begin = mesh.runIndex[run_i];
		size_t end = mesh.runIndex[run_i + 1];
		for (size_t vert_i = begin; vert_i < end; vert_i += 3) {
			CSGBrush::Face face;
			face.material = material_id;
			const int32_t first_property_index = mesh.triVerts[vert_i + order[0]];
			const double *first_properties = &mesh.vertProperties[first_property_index * mesh.numProp];
			const uint32_t input_face_id = uint32_t(MAX(Math::round(first_properties[MANIFOLD_PROPERTY_FACE_ID]), 0.0));
			const uint32_t source_face_id = uint32_t(MAX(Math::round(first_properties[MANIFOLD_PROPERTY_SOURCE_FACE_ID]), 0.0));
			const uint32_t brush_id = uint32_t(MAX(Math::round(first_properties[MANIFOLD_PROPERTY_BRUSH_ID]), 0.0));
			CSGFaceSourceMap::ConstIterator source = p_face_sources.find(_csg_face_source_key(brush_id, input_face_id));
			if (source) {
				face.metadata = source->value;
			} else {
				face.metadata.source_face_id = source_face_id;
				face.metadata.surface_id = uint32_t(MAX(Math::round(first_properties[MANIFOLD_PROPERTY_SURFACE_ID]), 0.0));
				face.metadata.brush_id = brush_id;
				face.metadata.layer_id = uint32_t(MAX(Math::round(first_properties[MANIFOLD_PROPERTY_LAYER_ID]), 0.0));
				face.metadata.generation = CSGBrush::FaceGeneration(uint32_t(CLAMP(Math::round(first_properties[MANIFOLD_PROPERTY_FACE_GENERATION]), 0.0, double(CSGBrush::FACE_TOPOLOGY_GENERATED))));
			}
			face.metadata.face_id = r_mesh_merge->faces.size();
			bool mixed_face_sources = false;
			for (int corner = 1; corner < 3; corner++) {
				const int32_t property_index = mesh.triVerts[vert_i + order[corner]];
				const double *properties = &mesh.vertProperties[property_index * mesh.numProp];
				mixed_face_sources |= !Math::is_equal_approx(properties[MANIFOLD_PROPERTY_FACE_ID], first_properties[MANIFOLD_PROPERTY_FACE_ID]);
				mixed_face_sources |= !Math::is_equal_approx(properties[MANIFOLD_PROPERTY_BRUSH_ID], first_properties[MANIFOLD_PROPERTY_BRUSH_ID]);
			}
			if (p_fallback_generation != CSGBrush::FACE_ORIGINAL) {
				face.metadata.generation = p_fallback_generation;
			} else if (mixed_face_sources) {
				face.metadata.generation = CSGBrush::FACE_BEVEL_GENERATED;
			}
			face.smooth = mesh.vertProperties[first_property_index * mesh.numProp + MANIFOLD_PROPERTY_SMOOTH_GROUP] > 0.5f;
			face.invert = mesh.vertProperties[first_property_index * mesh.numProp + MANIFOLD_PROPERTY_INVERT] > 0.5f;

			for (int32_t tri_order_i = 0; tri_order_i < 3; tri_order_i++) {
				int32_t property_i = mesh.triVerts[vert_i + order[tri_order_i]];
				ERR_FAIL_COND_MSG(property_i * mesh.numProp >= mesh.vertProperties.size(), "Invalid index into vertex properties");
				face.vertices[tri_order_i] = Vector3(
						mesh.vertProperties[property_i * mesh.numProp + MANIFOLD_PROPERTY_POSITION_X],
						mesh.vertProperties[property_i * mesh.numProp + MANIFOLD_PROPERTY_POSITION_Y],
						mesh.vertProperties[property_i * mesh.numProp + MANIFOLD_PROPERTY_POSITION_Z]);
				face.uvs[tri_order_i] = Vector2(
						mesh.vertProperties[property_i * mesh.numProp + MANIFOLD_PROPERTY_UV_X_0],
						mesh.vertProperties[property_i * mesh.numProp + MANIFOLD_PROPERTY_UV_Y_0]);
				face.colors[tri_order_i] = Color(
						mesh.vertProperties[property_i * mesh.numProp + MANIFOLD_PROPERTY_COLOR_R],
						mesh.vertProperties[property_i * mesh.numProp + MANIFOLD_PROPERTY_COLOR_G],
						mesh.vertProperties[property_i * mesh.numProp + MANIFOLD_PROPERTY_COLOR_B],
						mesh.vertProperties[property_i * mesh.numProp + MANIFOLD_PROPERTY_COLOR_A]);
				for (int custom_i = 0; custom_i < CSGBrush::CUSTOM_CHANNEL_COUNT; custom_i++) {
					const int property_offset = MANIFOLD_PROPERTY_CUSTOM_0_X + custom_i * 4;
					face.customs[custom_i][tri_order_i] = Vector4(
							mesh.vertProperties[property_i * mesh.numProp + property_offset + 0],
							mesh.vertProperties[property_i * mesh.numProp + property_offset + 1],
							mesh.vertProperties[property_i * mesh.numProp + property_offset + 2],
							mesh.vertProperties[property_i * mesh.numProp + property_offset + 3]);
				}
			}
			r_mesh_merge->faces.push_back(face);
		}
	}

	r_mesh_merge->_regen_face_aabbs();
}

#ifdef DEV_ENABLED
static String _export_meshgl_as_json(const manifold::MeshGL64 &p_mesh) {
	Dictionary mesh_dict;
	mesh_dict["numProp"] = p_mesh.numProp;

	Array vert_properties;
	for (const double &val : p_mesh.vertProperties) {
		vert_properties.append(val);
	}
	mesh_dict["vertProperties"] = vert_properties;

	Array tri_verts;
	for (const uint64_t &val : p_mesh.triVerts) {
		tri_verts.append(val);
	}
	mesh_dict["triVerts"] = tri_verts;

	Array merge_from_vert;
	for (const uint64_t &val : p_mesh.mergeFromVert) {
		merge_from_vert.append(val);
	}
	mesh_dict["mergeFromVert"] = merge_from_vert;

	Array merge_to_vert;
	for (const uint64_t &val : p_mesh.mergeToVert) {
		merge_to_vert.append(val);
	}
	mesh_dict["mergeToVert"] = merge_to_vert;

	Array run_index;
	for (const uint64_t &val : p_mesh.runIndex) {
		run_index.append(val);
	}
	mesh_dict["runIndex"] = run_index;

	Array run_original_id;
	for (const uint32_t &val : p_mesh.runOriginalID) {
		run_original_id.append(val);
	}
	mesh_dict["runOriginalID"] = run_original_id;

	Array run_transform;
	for (const double &val : p_mesh.runTransform) {
		run_transform.append(val);
	}
	mesh_dict["runTransform"] = run_transform;

	Array face_id;
	for (const uint64_t &val : p_mesh.faceID) {
		face_id.append(val);
	}
	mesh_dict["faceID"] = face_id;

	Array halfedge_tangent;
	for (const double &val : p_mesh.halfedgeTangent) {
		halfedge_tangent.append(val);
	}
	mesh_dict["halfedgeTangent"] = halfedge_tangent;

	mesh_dict["tolerance"] = p_mesh.tolerance;

	String json_string = JSON::stringify(mesh_dict);
	return json_string;
}
#endif // DEV_ENABLED

static void _pack_manifold(
		const CSGBrush *const p_mesh_merge,
		manifold::Manifold &r_manifold,
		HashMap<int32_t, Ref<Material>> &p_mesh_materials,
		CSGFaceSourceMap &r_face_sources,
		CSGShape3D *p_csg_shape) {
	ERR_FAIL_NULL_MSG(p_mesh_merge, "p_mesh_merge is null");
	ERR_FAIL_NULL_MSG(p_csg_shape, "p_shape is null");
	HashMap<uint32_t, Vector<CSGBrush::Face>> faces_by_material;
	for (int face_i = 0; face_i < p_mesh_merge->faces.size(); face_i++) {
		const CSGBrush::Face &face = p_mesh_merge->faces[face_i];
		faces_by_material[face.material].push_back(face);
	}

	manifold::MeshGL64 mesh;
	mesh.numProp = MANIFOLD_PROPERTY_MAX;
	mesh.runOriginalID.reserve(faces_by_material.size());
	mesh.runIndex.reserve(faces_by_material.size() + 1);
	mesh.vertProperties.reserve(p_mesh_merge->faces.size() * 3 * MANIFOLD_PROPERTY_MAX);

	// Make a run of triangles for each material.
	for (const KeyValue<uint32_t, Vector<CSGBrush::Face>> &E : faces_by_material) {
		const uint32_t material_id = E.key;
		const Vector<CSGBrush::Face> &faces = E.value;
		mesh.runIndex.push_back(mesh.triVerts.size());

		// Associate the material with an ID.
		uint32_t reserved_id = r_manifold.ReserveIDs(1);
		mesh.runOriginalID.push_back(reserved_id);
		Ref<Material> material;
		if (material_id < p_mesh_merge->materials.size()) {
			material = p_mesh_merge->materials[material_id];
		}

		p_mesh_materials.insert(reserved_id, material);
		for (const CSGBrush::Face &face : faces) {
			r_face_sources.insert(_csg_face_source_key(face.metadata.brush_id, face.metadata.face_id), face.metadata);
			for (int32_t tri_order_i = 0; tri_order_i < 3; tri_order_i++) {
				constexpr int32_t order[3] = { 0, 2, 1 };
				int i = order[tri_order_i];

				mesh.triVerts.push_back(mesh.vertProperties.size() / MANIFOLD_PROPERTY_MAX);

				size_t begin = mesh.vertProperties.size();
				mesh.vertProperties.resize(mesh.vertProperties.size() + MANIFOLD_PROPERTY_MAX);
				// Add the vertex properties.
				// Use CSGBrush constants rather than push_back for clarity.
				double *vert = &mesh.vertProperties[begin];
				vert[MANIFOLD_PROPERTY_POSITION_X] = face.vertices[i].x;
				vert[MANIFOLD_PROPERTY_POSITION_Y] = face.vertices[i].y;
				vert[MANIFOLD_PROPERTY_POSITION_Z] = face.vertices[i].z;
				vert[MANIFOLD_PROPERTY_UV_X_0] = face.uvs[i].x;
				vert[MANIFOLD_PROPERTY_UV_Y_0] = face.uvs[i].y;
				vert[MANIFOLD_PROPERTY_SMOOTH_GROUP] = face.smooth ? 1.0f : 0.0f;
				vert[MANIFOLD_PROPERTY_INVERT] = face.invert ? 1.0f : 0.0f;
				vert[MANIFOLD_PROPERTY_COLOR_R] = face.colors[i].r;
				vert[MANIFOLD_PROPERTY_COLOR_G] = face.colors[i].g;
				vert[MANIFOLD_PROPERTY_COLOR_B] = face.colors[i].b;
				vert[MANIFOLD_PROPERTY_COLOR_A] = face.colors[i].a;
				for (int custom_i = 0; custom_i < CSGBrush::CUSTOM_CHANNEL_COUNT; custom_i++) {
					const int property_offset = MANIFOLD_PROPERTY_CUSTOM_0_X + custom_i * 4;
					vert[property_offset + 0] = face.customs[custom_i][i].x;
					vert[property_offset + 1] = face.customs[custom_i][i].y;
					vert[property_offset + 2] = face.customs[custom_i][i].z;
					vert[property_offset + 3] = face.customs[custom_i][i].w;
				}
				vert[MANIFOLD_PROPERTY_FACE_ID] = face.metadata.face_id;
				vert[MANIFOLD_PROPERTY_SOURCE_FACE_ID] = face.metadata.source_face_id;
				vert[MANIFOLD_PROPERTY_SURFACE_ID] = face.metadata.surface_id;
				vert[MANIFOLD_PROPERTY_BRUSH_ID] = face.metadata.brush_id;
				vert[MANIFOLD_PROPERTY_LAYER_ID] = face.metadata.layer_id;
				vert[MANIFOLD_PROPERTY_FACE_GENERATION] = face.metadata.generation;
			}
		}
	}
	// runIndex needs an explicit end value.
	mesh.runIndex.push_back(mesh.triVerts.size());
	mesh.tolerance = 2 * FLT_EPSILON;
	ERR_FAIL_COND_MSG(mesh.vertProperties.size() % mesh.numProp != 0, "Invalid vertex properties size.");
	mesh.Merge();
#ifdef DEV_ENABLED
	print_verbose(_export_meshgl_as_json(mesh));
#endif // DEV_ENABLED
	r_manifold = manifold::Manifold(mesh);
}

struct ManifoldOperation {
	manifold::Manifold manifold;
	manifold::OpType operation;
	static manifold::OpType convert_csg_op(CSGShape3D::Operation op) {
		switch (op) {
			case CSGShape3D::OPERATION_SUBTRACTION:
				return manifold::OpType::Subtract;
			case CSGShape3D::OPERATION_INTERSECTION:
				return manifold::OpType::Intersect;
			default:
				return manifold::OpType::Add;
		}
	}
	ManifoldOperation() :
			operation(manifold::OpType::Add) {}
	ManifoldOperation(const manifold::Manifold &m, manifold::OpType op) :
			manifold(m), operation(op) {}
};

namespace {

struct CSGTopologyFace {
	int vertices[3];
	Vector3 normal;
	int32_t original_id = -1;
	bool invert = false;
	bool smooth = false;
};

struct CSGTopologyEdge {
	int vertices[2];
	Vector<int> faces;
};

static uint64_t _topology_edge_key(int p_a, int p_b) {
	const uint32_t a = MIN(p_a, p_b);
	const uint32_t b = MAX(p_a, p_b);
	return (uint64_t(a) << 32) | b;
}

static Vector3i _topology_cell(const Vector3 &p_position, real_t p_epsilon) {
	return Vector3i(
			Math::floor(p_position.x / p_epsilon),
			Math::floor(p_position.y / p_epsilon),
			Math::floor(p_position.z / p_epsilon));
}

static int _topology_weld_vertex(const Vector3 &p_position, real_t p_epsilon, HashMap<Vector3i, Vector<int>> &r_cells, Vector<Vector3> &r_vertices) {
	const Vector3i cell = _topology_cell(p_position, p_epsilon);
	const real_t epsilon_squared = p_epsilon * p_epsilon;
	for (int x = -1; x <= 1; x++) {
		for (int y = -1; y <= 1; y++) {
			for (int z = -1; z <= 1; z++) {
				HashMap<Vector3i, Vector<int>>::ConstIterator candidates = r_cells.find(cell + Vector3i(x, y, z));
				if (!candidates) {
					continue;
				}
				for (int vertex : candidates->value) {
					if (r_vertices[vertex].distance_squared_to(p_position) <= epsilon_squared) {
						return vertex;
					}
				}
			}
		}
	}

	const int vertex = r_vertices.size();
	r_vertices.push_back(p_position);
	r_cells[cell].push_back(vertex);
	return vertex;
}

static manifold::Manifold _topology_volume_with_properties(manifold::Manifold p_volume, bool p_invert, bool p_smooth) {
	p_volume = p_volume.SetProperties(MANIFOLD_PROPERTY_MAX - 3, [p_invert, p_smooth](double *p_properties, manifold::vec3, const double *) {
		for (int i = 0; i < MANIFOLD_PROPERTY_MAX - 3; i++) {
			p_properties[i] = 0.0;
		}
		p_properties[MANIFOLD_PROPERTY_INVERT - 3] = p_invert ? 1.0 : 0.0;
		p_properties[MANIFOLD_PROPERTY_SMOOTH_GROUP - 3] = p_smooth ? 1.0 : 0.0;
		p_properties[MANIFOLD_PROPERTY_COLOR_R - 3] = 1.0;
		p_properties[MANIFOLD_PROPERTY_COLOR_G - 3] = 1.0;
		p_properties[MANIFOLD_PROPERTY_COLOR_B - 3] = 1.0;
		p_properties[MANIFOLD_PROPERTY_COLOR_A - 3] = 1.0;
	});
	return p_volume.AsOriginal();
}

static bool _build_edge_volume_topology(const manifold::Manifold &p_source, const Ref<CSGTopologySettings> &p_settings, const HashMap<int32_t, Ref<Material>> &p_source_materials, manifold::Manifold &r_result, HashMap<int32_t, Ref<Material>> &r_result_materials) {
	ERR_FAIL_COND_V(p_settings.is_null(), false);
	const manifold::MeshGL64 mesh = p_source.GetMeshGL64();
	if (mesh.triVerts.empty()) {
		r_result = manifold::Manifold();
		return true;
	}

	const real_t merge_epsilon = p_settings->get_merge_epsilon();
	const real_t edge_width = p_settings->get_edge_width();
	const real_t normal_dot_limit = Math::cos(p_settings->get_angle_threshold());
	HashMap<Vector3i, Vector<int>> vertex_cells;
	Vector<Vector3> vertices;
	Vector<CSGTopologyFace> faces;
	HashMap<uint64_t, CSGTopologyEdge> edges;
	const int face_count = mesh.triVerts.size() / 3;
	faces.resize(face_count);

	size_t run_i = 0;
	for (int face_i = 0; face_i < face_count; face_i++) {
		while (run_i + 1 < mesh.runIndex.size() && size_t(face_i * 3) >= mesh.runIndex[run_i + 1]) {
			run_i++;
		}
		CSGTopologyFace &face = faces.write[face_i];
		if (run_i < mesh.runOriginalID.size()) {
			face.original_id = mesh.runOriginalID[run_i];
		}

		Vector3 face_positions[3];
		for (int corner = 0; corner < 3; corner++) {
			const uint64_t property_vertex = mesh.triVerts[face_i * 3 + corner];
			const size_t property_offset = property_vertex * mesh.numProp;
			ERR_FAIL_COND_V(property_offset + 2 >= mesh.vertProperties.size(), false);
			face_positions[corner] = Vector3(
					mesh.vertProperties[property_offset + MANIFOLD_PROPERTY_POSITION_X],
					mesh.vertProperties[property_offset + MANIFOLD_PROPERTY_POSITION_Y],
					mesh.vertProperties[property_offset + MANIFOLD_PROPERTY_POSITION_Z]);
		}
		face.normal = (face_positions[1] - face_positions[0]).cross(face_positions[2] - face_positions[0]).normalized();
		if (face.normal.is_zero_approx()) {
			continue;
		}
		const size_t first_property_offset = mesh.triVerts[face_i * 3] * mesh.numProp;
		if (mesh.numProp >= MANIFOLD_PROPERTY_MAX) {
			face.invert = mesh.vertProperties[first_property_offset + MANIFOLD_PROPERTY_INVERT] > 0.5;
			face.smooth = mesh.vertProperties[first_property_offset + MANIFOLD_PROPERTY_SMOOTH_GROUP] > 0.5;
		}

		for (int corner = 0; corner < 3; corner++) {
			face.vertices[corner] = _topology_weld_vertex(face_positions[corner], merge_epsilon, vertex_cells, vertices);
		}
		for (int corner = 0; corner < 3; corner++) {
			const int a = face.vertices[corner];
			const int b = face.vertices[(corner + 1) % 3];
			if (a == b) {
				continue;
			}
			const uint64_t key = _topology_edge_key(a, b);
			HashMap<uint64_t, CSGTopologyEdge>::Iterator edge = edges.find(key);
			if (edge) {
				edge->value.faces.push_back(face_i);
			} else {
				CSGTopologyEdge new_edge;
				new_edge.vertices[0] = MIN(a, b);
				new_edge.vertices[1] = MAX(a, b);
				new_edge.faces.push_back(face_i);
				edges.insert(key, new_edge);
			}
		}
	}

	std::vector<manifold::Manifold> edge_volumes;
	edge_volumes.reserve(edges.size());
	for (const KeyValue<uint64_t, CSGTopologyEdge> &entry : edges) {
		const CSGTopologyEdge &edge = entry.value;
		if (edge.faces.size() != 2) {
			continue;
		}

		const CSGTopologyFace &face_a = faces[edge.faces[0]];
		const CSGTopologyFace &face_b = faces[edge.faces[1]];
		const real_t normal_dot = CLAMP(face_a.normal.dot(face_b.normal), -1.0, 1.0);
		if (normal_dot >= 1.0 - CMP_EPSILON || normal_dot > normal_dot_limit) {
			continue;
		}

		const Vector3 start = vertices[edge.vertices[0]];
		const Vector3 end = vertices[edge.vertices[1]];
		const real_t edge_length = start.distance_to(end);
		if (edge_length <= merge_epsilon) {
			continue;
		}

		const Vector3 tangent = (end - start) / edge_length;
		Vector3 axis_y = face_a.normal - tangent * face_a.normal.dot(tangent);
		if (axis_y.is_zero_approx()) {
			axis_y = tangent.cross(Vector3(0, 1, 0));
			if (axis_y.is_zero_approx()) {
				axis_y = tangent.cross(Vector3(1, 0, 0));
			}
		}
		axis_y.normalize();
		const Vector3 axis_z = tangent.cross(axis_y).normalized();
		const Vector3 midpoint = (start + end) * 0.5;
		const manifold::mat3 rotation({
				{ tangent.x, tangent.y, tangent.z },
				{ axis_y.x, axis_y.y, axis_y.z },
				{ axis_z.x, axis_z.y, axis_z.z },
		});
		const manifold::mat3x4 transform(rotation, manifold::vec3(midpoint.x, midpoint.y, midpoint.z));

		manifold::Manifold volume = manifold::Manifold::Cube(manifold::vec3(edge_length + edge_width, edge_width, edge_width), true).Transform(transform);
		volume = _topology_volume_with_properties(std::move(volume), face_a.invert, face_a.smooth);

		Ref<Material> material;
		HashMap<int32_t, Ref<Material>>::ConstIterator source_material = p_source_materials.find(face_a.original_id);
		if (source_material) {
			material = source_material->value;
		}
		r_result_materials.insert(volume.OriginalID(), material);
		edge_volumes.push_back(std::move(volume));
	}

	if (edge_volumes.empty()) {
		r_result = manifold::Manifold();
		return true;
	}

	r_result = manifold::Manifold::BatchBoolean(edge_volumes, manifold::OpType::Add);
	if (r_result.Status() != manifold::Manifold::Error::NoError) {
		r_result_materials.clear();
		return false;
	}
	return true;
}

static bool _process_topology(const manifold::Manifold &p_source, const Ref<CSGTopologySettings> &p_settings, const HashMap<int32_t, Ref<Material>> &p_source_materials, manifold::Manifold &r_result, HashMap<int32_t, Ref<Material>> &r_result_materials) {
	ERR_FAIL_COND_V(p_settings.is_null(), false);
	switch (p_settings->get_mode()) {
		case CSGTopologySettings::TOPOLOGY_NONE:
			return false;
		case CSGTopologySettings::TOPOLOGY_EDGE_VOLUME:
			return _build_edge_volume_topology(p_source, p_settings, p_source_materials, r_result, r_result_materials);
	}
	return false;
}

} // namespace

void CSGShape3D::_process_modifiers(CSGBrush *p_brush) {
	if (!p_brush || modifiers.is_empty()) {
		return;
	}

	Ref<CSGModifierContext> context;
	context.instantiate();
	context->setup(p_brush);
	for (int i = 0; i < modifiers.size(); i++) {
		Ref<CSGModifier> modifier = modifiers[i];
		if (modifier.is_valid()) {
			modifier->process(context);
		}
	}
	p_brush->_regen_face_aabbs();
}

CSGBrush *CSGShape3D::_get_brush() {
	if (!dirty) {
		return brush;
	}
	if (brush) {
		memdelete(brush);
	}
	brush = nullptr;
	CSGBrush *n = _build_brush();
	bool has_colors = n && n->has_colors;
	uint32_t custom_channels = n ? n->custom_channels : 0;
	Mesh::ArrayCustomFormat custom_formats[CSGBrush::CUSTOM_CHANNEL_COUNT] = {
		Mesh::ARRAY_CUSTOM_RGBA_FLOAT,
		Mesh::ARRAY_CUSTOM_RGBA_FLOAT,
		Mesh::ARRAY_CUSTOM_RGBA_FLOAT,
		Mesh::ARRAY_CUSTOM_RGBA_FLOAT,
	};
	if (n) {
		for (int i = 0; i < CSGBrush::CUSTOM_CHANNEL_COUNT; i++) {
			custom_formats[i] = n->custom_formats[i];
		}
	}
	HashMap<int32_t, Ref<Material>> mesh_materials;
	CSGFaceSourceMap face_sources;
	manifold::Manifold root_manifold;
	uint32_t next_brush_id = 1;
	if (n) {
		for (CSGBrush::Face &face : n->faces) {
			next_brush_id = MAX(next_brush_id, face.metadata.brush_id + 1);
		}
	}
	_pack_manifold(n, root_manifold, mesh_materials, face_sources, this);
	manifold::OpType current_op = ManifoldOperation::convert_csg_op(get_operation());
	std::vector<manifold::Manifold> manifolds;
	manifolds.push_back(root_manifold);
	for (int i = 0; i < get_child_count(); i++) {
		CSGShape3D *child = Object::cast_to<CSGShape3D>(get_child(i));
		if (!child || !child->is_visible()) {
			continue;
		}
		CSGBrush *child_brush = child->_get_brush();
		if (!child_brush) {
			continue;
		}
		has_colors |= child_brush->has_colors;
		for (int custom_i = 0; custom_i < CSGBrush::CUSTOM_CHANNEL_COUNT; custom_i++) {
			if (child_brush->custom_channels & (1u << custom_i)) {
				if (custom_channels & (1u << custom_i)) {
					ERR_CONTINUE_MSG(custom_formats[custom_i] != child_brush->custom_formats[custom_i], "CSG custom channel formats must match across Boolean operands.");
				}
				custom_channels |= 1u << custom_i;
				custom_formats[custom_i] = child_brush->custom_formats[custom_i];
			}
		}

		CSGBrush transformed_brush;
		transformed_brush.copy_from(*child_brush, child->get_transform());
		uint32_t child_brush_count = 1;
		for (CSGBrush::Face &face : transformed_brush.faces) {
			child_brush_count = MAX(child_brush_count, face.metadata.brush_id + 1);
			face.metadata.brush_id += next_brush_id;
			if (child->get_operation() == CSGShape3D::OPERATION_SUBTRACTION && face.metadata.generation == CSGBrush::FACE_ORIGINAL) {
				face.metadata.generation = CSGBrush::FACE_BOOLEAN_GENERATED;
			}
		}
		next_brush_id += child_brush_count;
		manifold::Manifold child_manifold;
		_pack_manifold(&transformed_brush, child_manifold, mesh_materials, face_sources, child);
		manifold::OpType child_operation = ManifoldOperation::convert_csg_op(child->get_operation());
		if (child_operation != current_op) {
			manifold::Manifold result = manifold::Manifold::BatchBoolean(manifolds, current_op);
			manifolds.clear();
			manifolds.push_back(result);
			current_op = child_operation;
		}
		manifolds.push_back(child_manifold);
	}
	if (!manifolds.empty()) {
		manifold::Manifold manifold_result = manifold::Manifold::BatchBoolean(manifolds, current_op);
		if (bevel_settings.is_valid() && bevel_settings->is_enabled() && bevel_settings->get_width() > 0.0) {
			if (bevel_settings->is_debug_printing()) {
				print_line(vformat("[CSGBevel] node=%s class=%s children=%d operation=%d", get_path(), get_class(), get_child_count(), get_operation()));
			}
			manifold::Manifold bevel_result;
			if (csg_build_native_bevel(manifold_result, bevel_settings, bevel_result)) {
				manifold_result = std::move(bevel_result);
			} else {
				WARN_PRINT("Native CSG bevel processing failed; the original Boolean result was preserved.");
			}
		}
		if (n) {
			memdelete(n);
		}
		n = memnew(CSGBrush);
		n->has_colors = has_colors;
		n->custom_channels = custom_channels;
		for (int i = 0; i < CSGBrush::CUSTOM_CHANNEL_COUNT; i++) {
			n->custom_formats[i] = custom_formats[i];
		}

		if (topology_settings.is_valid() && topology_settings->get_mode() != CSGTopologySettings::TOPOLOGY_NONE) {
			manifold::Manifold topology_result;
			HashMap<int32_t, Ref<Material>> topology_materials;
			if (_process_topology(manifold_result, topology_settings, mesh_materials, topology_result, topology_materials)) {
				if (!topology_result.IsEmpty()) {
					_unpack_manifold(topology_result, topology_materials, face_sources, n, CSGBrush::FACE_TOPOLOGY_GENERATED);
				}
			} else {
				WARN_PRINT("CSG topology processing failed; the original Boolean result was preserved.");
				_unpack_manifold(manifold_result, mesh_materials, face_sources, n);
			}
		} else {
			_unpack_manifold(manifold_result, mesh_materials, face_sources, n);
		}
	}
	_process_modifiers(n);

	AABB aabb;
	if (n && !n->faces.is_empty()) {
		aabb.position = n->faces[0].vertices[0];
		for (const CSGBrush::Face &face : n->faces) {
			for (int i = 0; i < 3; ++i) {
				aabb.expand_to(face.vertices[i]);
			}
		}
	}
	node_aabb = aabb;
	brush = n;
	dirty = false;
	update_configuration_warnings();
	return brush;
}

int CSGShape3D::mikktGetNumFaces(const SMikkTSpaceContext *pContext) {
	ShapeUpdateSurface &surface = *((ShapeUpdateSurface *)pContext->m_pUserData);

	return surface.vertices.size() / 3;
}

int CSGShape3D::mikktGetNumVerticesOfFace(const SMikkTSpaceContext *pContext, const int iFace) {
	// always 3
	return 3;
}

void CSGShape3D::mikktGetPosition(const SMikkTSpaceContext *pContext, float fvPosOut[], const int iFace, const int iVert) {
	ShapeUpdateSurface &surface = *((ShapeUpdateSurface *)pContext->m_pUserData);

	Vector3 v = surface.verticesw[iFace * 3 + iVert];
	fvPosOut[0] = v.x;
	fvPosOut[1] = v.y;
	fvPosOut[2] = v.z;
}

void CSGShape3D::mikktGetNormal(const SMikkTSpaceContext *pContext, float fvNormOut[], const int iFace, const int iVert) {
	ShapeUpdateSurface &surface = *((ShapeUpdateSurface *)pContext->m_pUserData);

	Vector3 n = surface.normalsw[iFace * 3 + iVert];
	fvNormOut[0] = n.x;
	fvNormOut[1] = n.y;
	fvNormOut[2] = n.z;
}

void CSGShape3D::mikktGetTexCoord(const SMikkTSpaceContext *pContext, float fvTexcOut[], const int iFace, const int iVert) {
	ShapeUpdateSurface &surface = *((ShapeUpdateSurface *)pContext->m_pUserData);

	Vector2 t = surface.uvsw[iFace * 3 + iVert];
	fvTexcOut[0] = t.x;
	fvTexcOut[1] = t.y;
}

void CSGShape3D::mikktSetTSpaceDefault(const SMikkTSpaceContext *pContext, const float fvTangent[], const float fvBiTangent[], const float fMagS, const float fMagT,
		const tbool bIsOrientationPreserving, const int iFace, const int iVert) {
	ShapeUpdateSurface &surface = *((ShapeUpdateSurface *)pContext->m_pUserData);

	int i = iFace * 3 + iVert;
	Vector3 normal = surface.normalsw[i];
	Vector3 tangent = Vector3(fvTangent[0], fvTangent[1], fvTangent[2]);
	Vector3 bitangent = Vector3(-fvBiTangent[0], -fvBiTangent[1], -fvBiTangent[2]); // for some reason these are reversed, something with the coordinate system in Godot
	float d = bitangent.dot(normal.cross(tangent));

	i *= 4;
	surface.tansw[i++] = tangent.x;
	surface.tansw[i++] = tangent.y;
	surface.tansw[i++] = tangent.z;
	surface.tansw[i++] = d < 0 ? -1 : 1;
}

static Variant _csg_pack_custom_array(const Vector<Vector4> &p_values, Mesh::ArrayCustomFormat p_format) {
	if (p_format >= Mesh::ARRAY_CUSTOM_R_FLOAT) {
		const int components = int(p_format) - int(Mesh::ARRAY_CUSTOM_R_FLOAT) + 1;
		PackedFloat32Array result;
		result.resize(p_values.size() * components);
		float *resultw = result.ptrw();
		for (int i = 0; i < p_values.size(); i++) {
			for (int component = 0; component < components; component++) {
				resultw[i * components + component] = p_values[i][component];
			}
		}
		return result;
	}

	const bool half = p_format == Mesh::ARRAY_CUSTOM_RG_HALF || p_format == Mesh::ARRAY_CUSTOM_RGBA_HALF;
	const int components = p_format == Mesh::ARRAY_CUSTOM_RG_HALF ? 2 : 4;
	const int component_size = half ? 2 : 1;
	PackedByteArray result;
	result.resize(p_values.size() * components * component_size);
	uint8_t *resultw = result.ptrw();

	for (int i = 0; i < p_values.size(); i++) {
		for (int component = 0; component < components; component++) {
			const float value = p_values[i][component];
			const int offset = (i * components + component) * component_size;
			if (half) {
				const uint16_t encoded = Math::make_half_float(value);
				resultw[offset + 0] = encoded & 0xff;
				resultw[offset + 1] = encoded >> 8;
			} else if (p_format == Mesh::ARRAY_CUSTOM_RGBA8_SNORM) {
				resultw[offset] = uint8_t(int8_t(CLAMP(Math::round(value * 127.0f), -127.0f, 127.0f)));
			} else {
				resultw[offset] = uint8_t(CLAMP(Math::round(value * 255.0f), 0.0f, 255.0f));
			}
		}
	}
	return result;
}

void CSGShape3D::update_shape() {
	if (!is_root_shape()) {
		return;
	}

	set_base(RID());
	root_mesh.unref(); //byebye root mesh

	CSGBrush *n = _get_brush();
	ERR_FAIL_NULL_MSG(n, "Cannot get CSGBrush.");

	Vector<int> face_count;
	face_count.resize(n->materials.size() + 1);
	face_count.fill(0);

	Vector<ShapeUpdateSurface> surfaces;
	surfaces.resize(face_count.size());

	if (autosmooth) {
		_build_surfaces_smoothed(n, surfaces, face_count);
	} else {
		_build_surfaces_default(n, surfaces, face_count);
	}

	root_mesh.instantiate();
	//create surfaces

	for (int i = 0; i < surfaces.size(); i++) {
		// calculate tangents for this surface
		bool have_tangents = calculate_tangents;
		if (have_tangents) {
			SMikkTSpaceInterface mkif;
			mkif.m_getNormal = mikktGetNormal;
			mkif.m_getNumFaces = mikktGetNumFaces;
			mkif.m_getNumVerticesOfFace = mikktGetNumVerticesOfFace;
			mkif.m_getPosition = mikktGetPosition;
			mkif.m_getTexCoord = mikktGetTexCoord;
			mkif.m_setTSpace = mikktSetTSpaceDefault;
			mkif.m_setTSpaceBasic = nullptr;

			SMikkTSpaceContext msc;
			msc.m_pInterface = &mkif;
			msc.m_pUserData = &surfaces.write[i];
			have_tangents = genTangSpaceDefault(&msc);
		}

		if (surfaces[i].last_added == 0) {
			continue;
		}

		// and convert to surface array
		Array array;
		array.resize(Mesh::ARRAY_MAX);

		array[Mesh::ARRAY_VERTEX] = surfaces[i].vertices;
		array[Mesh::ARRAY_NORMAL] = surfaces[i].normals;
		array[Mesh::ARRAY_TEX_UV] = surfaces[i].uvs;
		if (n->has_colors) {
			array[Mesh::ARRAY_COLOR] = surfaces[i].colors;
		}
		if (have_tangents) {
			array[Mesh::ARRAY_TANGENT] = surfaces[i].tans;
		}

		uint64_t format_flags = 0;
		for (int custom_i = 0; custom_i < CSGBrush::CUSTOM_CHANNEL_COUNT; custom_i++) {
			if (n->custom_channels & (1u << custom_i)) {
				array[Mesh::ARRAY_CUSTOM0 + custom_i] = _csg_pack_custom_array(surfaces[i].customs[custom_i], n->custom_formats[custom_i]);
				format_flags |= uint64_t(n->custom_formats[custom_i]) << (Mesh::ARRAY_FORMAT_CUSTOM0_SHIFT + custom_i * Mesh::ARRAY_FORMAT_CUSTOM_BITS);
			}
		}

		int idx = root_mesh->get_surface_count();
		root_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, array, Array(), Dictionary(), format_flags);
		root_mesh->surface_set_material(idx, surfaces[i].material);
	}

	set_base(root_mesh->get_rid());

	update_gizmos();

#ifndef PHYSICS_3D_DISABLED
	_update_collision_faces();
#endif // PHYSICS_3D_DISABLED
}

void CSGShape3D::_build_surfaces_smoothed(CSGBrush *p_brush, Vector<CSGShape3D::ShapeUpdateSurface> &r_surfaces, Vector<int> &r_face_count) {
	Vector<Vector3> smooth_faces;
	LocalVector<Vector3> smooth_vertex;
	smooth_faces.resize(p_brush->faces.size());
	smooth_vertex.resize(p_brush->faces.size() * 3);

	Vector3 *smooth_faces_ptrw = smooth_faces.ptrw();
	int *face_count_ptrw = r_face_count.ptrw();

	for (int i = 0; i < p_brush->faces.size(); i++) {
		int mat = p_brush->faces[i].material;
		ERR_CONTINUE(mat < -1 || mat >= r_face_count.size());
		int idx = mat == -1 ? r_face_count.size() - 1 : mat;

		Plane p(p_brush->faces[i].vertices[0], p_brush->faces[i].vertices[1], p_brush->faces[i].vertices[2]);

		smooth_faces_ptrw[i] = p.normal;
		// Not sure if resize populates the LocalVector.
		smooth_vertex[i * 3 + 0] = Vector3(p.normal);
		smooth_vertex[i * 3 + 1] = Vector3(p.normal);
		smooth_vertex[i * 3 + 2] = Vector3(p.normal);
		// We could use a AHashMap Vector3, int to store the number of connections of each vertex position and end the loop earlier. But I'm not sure if the performance gains outweigh the cost.
		face_count_ptrw[idx]++;
	}

	const Vector3 *smooth_faces_ptr = smooth_faces.ptr();
	const int smooth_faces_size = smooth_faces.size();

	// We could add a `use_groups` property later to only apply autosmooth on smooth faces or respect smoothing groups in some way.
	if (smoothing_angle > 0.1) {
		float smooth_angle_rad = Math::cos(Math::deg_to_rad(smoothing_angle));
		for (int i = 0; i < smooth_faces_size; i++) {
			for (int k = 0; k < 3; k++) {
				int curr_vert = i * 3 + k;
				// Skip the other vertices of the face as they will never occupy the same position.
				Vector3 vert_a = p_brush->faces[i].vertices[k];
				for (int j = i + 1; j < smooth_faces_size; j++) {
					// Compare the angles of faces instead of vertices.
					if (smooth_faces_ptr[i].dot(smooth_faces_ptr[j]) > smooth_angle_rad) {
						for (int h = 0; h < 3; h++) {
							Vector3 vert_b = p_brush->faces[j].vertices[h];
							if (vert_a == vert_b) {
								int curr_j = j * 3 + h;
								smooth_vertex[curr_vert] += smooth_faces_ptr[j];
								smooth_vertex[curr_j] += smooth_faces_ptr[i];
								// Skip the other 2 vertices as only one vertex of each face can connect with one vertex of other face.
								break;
							}
						}
					}
				}
				smooth_vertex[curr_vert].normalize();
			}
		}
	}

	//create arrays
	for (int i = 0; i < r_surfaces.size(); i++) {
		r_surfaces.write[i].vertices.resize(r_face_count[i] * 3);
		r_surfaces.write[i].normals.resize(r_face_count[i] * 3);
		r_surfaces.write[i].uvs.resize(r_face_count[i] * 3);
		if (p_brush->has_colors) {
			r_surfaces.write[i].colors.resize(r_face_count[i] * 3);
		}
		for (int custom_i = 0; custom_i < CSGBrush::CUSTOM_CHANNEL_COUNT; custom_i++) {
			if (p_brush->custom_channels & (1u << custom_i)) {
				r_surfaces.write[i].customs[custom_i].resize(r_face_count[i] * 3);
			}
		}
		if (calculate_tangents) {
			r_surfaces.write[i].tans.resize(r_face_count[i] * 3 * 4);
		}
		r_surfaces.write[i].last_added = 0;

		if (i != r_surfaces.size() - 1) {
			r_surfaces.write[i].material = p_brush->materials[i];
		}

		r_surfaces.write[i].verticesw = r_surfaces.write[i].vertices.ptrw();
		r_surfaces.write[i].normalsw = r_surfaces.write[i].normals.ptrw();
		r_surfaces.write[i].uvsw = r_surfaces.write[i].uvs.ptrw();
		if (p_brush->has_colors) {
			r_surfaces.write[i].colorsw = r_surfaces.write[i].colors.ptrw();
		}
		for (int custom_i = 0; custom_i < CSGBrush::CUSTOM_CHANNEL_COUNT; custom_i++) {
			if (p_brush->custom_channels & (1u << custom_i)) {
				r_surfaces.write[i].customsw[custom_i] = r_surfaces.write[i].customs[custom_i].ptrw();
			}
		}
		if (calculate_tangents) {
			r_surfaces.write[i].tansw = r_surfaces.write[i].tans.ptrw();
		}
	}

	//fill arrays
	{
		for (int i = 0; i < p_brush->faces.size(); i++) {
			int order[3] = { 0, 1, 2 };

			if (p_brush->faces[i].invert) {
				SWAP(order[1], order[2]);
			}

			int mat = p_brush->faces[i].material;
			ERR_CONTINUE(mat < -1 || mat >= r_face_count.size());
			int idx = mat == -1 ? r_face_count.size() - 1 : mat;

			int last = r_surfaces[idx].last_added;

			int face_pos_i = i * 3;

			for (int j = 0; j < 3; j++) {
				Vector3 v = p_brush->faces[i].vertices[j];

				Vector3 normal = smooth_vertex[face_pos_i + j];

				if (p_brush->faces[i].invert) {
					normal = -normal;
				}

				int k = last + order[j];
				r_surfaces[idx].verticesw[k] = v;
				r_surfaces[idx].uvsw[k] = p_brush->faces[i].uvs[j];
				r_surfaces[idx].normalsw[k] = normal;
				if (p_brush->has_colors) {
					r_surfaces[idx].colorsw[k] = p_brush->faces[i].colors[j];
				}
				for (int custom_i = 0; custom_i < CSGBrush::CUSTOM_CHANNEL_COUNT; custom_i++) {
					if (p_brush->custom_channels & (1u << custom_i)) {
						r_surfaces[idx].customsw[custom_i][k] = p_brush->faces[i].customs[custom_i][j];
					}
				}

				if (calculate_tangents) {
					// zero out our tangents for now
					k *= 4;
					r_surfaces[idx].tansw[k++] = 0.0;
					r_surfaces[idx].tansw[k++] = 0.0;
					r_surfaces[idx].tansw[k++] = 0.0;
					r_surfaces[idx].tansw[k++] = 0.0;
				}
			}

			r_surfaces.write[idx].last_added += 3;
		}
	}
}

void CSGShape3D::_build_surfaces_default(CSGBrush *p_brush, Vector<CSGShape3D::ShapeUpdateSurface> &r_surfaces, Vector<int> &r_face_count) {
	AHashMap<Vector3, Vector3> vec_map;
	vec_map.reserve(p_brush->faces.size() * 3);

	for (int i = 0; i < p_brush->faces.size(); i++) {
		int mat = p_brush->faces[i].material;
		ERR_CONTINUE(mat < -1 || mat >= r_face_count.size());
		int idx = mat == -1 ? r_face_count.size() - 1 : mat;

		if (p_brush->faces[i].smooth) {
			Plane p(p_brush->faces[i].vertices[0], p_brush->faces[i].vertices[1], p_brush->faces[i].vertices[2]);

			for (int j = 0; j < 3; j++) {
				Vector3 v = p_brush->faces[i].vertices[j];
				Vector3 *vec = vec_map.getptr(v);
				if (vec) {
					*vec += p.normal;
				} else {
					vec_map.insert(v, p.normal);
				}
			}
		}

		r_face_count.write[idx]++;
	}

	//create arrays
	for (int i = 0; i < r_surfaces.size(); i++) {
		r_surfaces.write[i].vertices.resize(r_face_count[i] * 3);
		r_surfaces.write[i].normals.resize(r_face_count[i] * 3);
		r_surfaces.write[i].uvs.resize(r_face_count[i] * 3);
		if (p_brush->has_colors) {
			r_surfaces.write[i].colors.resize(r_face_count[i] * 3);
		}
		for (int custom_i = 0; custom_i < CSGBrush::CUSTOM_CHANNEL_COUNT; custom_i++) {
			if (p_brush->custom_channels & (1u << custom_i)) {
				r_surfaces.write[i].customs[custom_i].resize(r_face_count[i] * 3);
			}
		}
		if (calculate_tangents) {
			r_surfaces.write[i].tans.resize(r_face_count[i] * 3 * 4);
		}
		r_surfaces.write[i].last_added = 0;

		if (i != r_surfaces.size() - 1) {
			r_surfaces.write[i].material = p_brush->materials[i];
		}

		r_surfaces.write[i].verticesw = r_surfaces.write[i].vertices.ptrw();
		r_surfaces.write[i].normalsw = r_surfaces.write[i].normals.ptrw();
		r_surfaces.write[i].uvsw = r_surfaces.write[i].uvs.ptrw();
		if (p_brush->has_colors) {
			r_surfaces.write[i].colorsw = r_surfaces.write[i].colors.ptrw();
		}
		for (int custom_i = 0; custom_i < CSGBrush::CUSTOM_CHANNEL_COUNT; custom_i++) {
			if (p_brush->custom_channels & (1u << custom_i)) {
				r_surfaces.write[i].customsw[custom_i] = r_surfaces.write[i].customs[custom_i].ptrw();
			}
		}
		if (calculate_tangents) {
			r_surfaces.write[i].tansw = r_surfaces.write[i].tans.ptrw();
		}
	}

	//fill arrays
	{
		for (int i = 0; i < p_brush->faces.size(); i++) {
			int order[3] = { 0, 1, 2 };

			if (p_brush->faces[i].invert) {
				SWAP(order[1], order[2]);
			}

			int mat = p_brush->faces[i].material;
			ERR_CONTINUE(mat < -1 || mat >= r_face_count.size());
			int idx = mat == -1 ? r_face_count.size() - 1 : mat;

			int last = r_surfaces[idx].last_added;

			Plane p(p_brush->faces[i].vertices[0], p_brush->faces[i].vertices[1], p_brush->faces[i].vertices[2]);

			for (int j = 0; j < 3; j++) {
				Vector3 v = p_brush->faces[i].vertices[j];

				Vector3 normal = p.normal;

				if (p_brush->faces[i].smooth) {
					Vector3 *ptr = vec_map.getptr(v);
					if (ptr) {
						normal = ptr->normalized();
					}
				}

				if (p_brush->faces[i].invert) {
					normal = -normal;
				}

				int k = last + order[j];
				r_surfaces[idx].verticesw[k] = v;
				r_surfaces[idx].uvsw[k] = p_brush->faces[i].uvs[j];
				r_surfaces[idx].normalsw[k] = normal;
				if (p_brush->has_colors) {
					r_surfaces[idx].colorsw[k] = p_brush->faces[i].colors[j];
				}
				for (int custom_i = 0; custom_i < CSGBrush::CUSTOM_CHANNEL_COUNT; custom_i++) {
					if (p_brush->custom_channels & (1u << custom_i)) {
						r_surfaces[idx].customsw[custom_i][k] = p_brush->faces[i].customs[custom_i][j];
					}
				}

				if (calculate_tangents) {
					// zero out our tangents for now
					k *= 4;
					r_surfaces[idx].tansw[k++] = 0.0;
					r_surfaces[idx].tansw[k++] = 0.0;
					r_surfaces[idx].tansw[k++] = 0.0;
					r_surfaces[idx].tansw[k++] = 0.0;
				}
			}

			r_surfaces.write[idx].last_added += 3;
		}
	}
}

Ref<ArrayMesh> CSGShape3D::bake_static_mesh() {
	Ref<ArrayMesh> baked_mesh;
	if (is_root_shape() && root_mesh.is_valid()) {
		baked_mesh = root_mesh;
	}
	return baked_mesh;
}

#ifndef PHYSICS_3D_DISABLED
Vector<Vector3> CSGShape3D::_get_brush_collision_faces() {
	Vector<Vector3> collision_faces;
	CSGBrush *n = _get_brush();
	ERR_FAIL_NULL_V_MSG(n, collision_faces, "Cannot get CSGBrush.");
	collision_faces.resize(n->faces.size() * 3);
	Vector3 *collision_faces_ptrw = collision_faces.ptrw();

	for (int i = 0; i < n->faces.size(); i++) {
		int order[3] = { 0, 1, 2 };

		if (n->faces[i].invert) {
			SWAP(order[1], order[2]);
		}

		collision_faces_ptrw[i * 3 + 0] = n->faces[i].vertices[order[0]];
		collision_faces_ptrw[i * 3 + 1] = n->faces[i].vertices[order[1]];
		collision_faces_ptrw[i * 3 + 2] = n->faces[i].vertices[order[2]];
	}

	return collision_faces;
}

void CSGShape3D::_update_collision_faces() {
	if (use_collision && is_root_shape() && root_collision_shape.is_valid()) {
		root_collision_shape->set_faces(_get_brush_collision_faces());

		if (_is_debug_collision_shape_visible()) {
			_update_debug_collision_shape();
		}
	}
}

Ref<ConcavePolygonShape3D> CSGShape3D::bake_collision_shape() {
	Ref<ConcavePolygonShape3D> baked_collision_shape;
	if (is_root_shape() && root_collision_shape.is_valid()) {
		baked_collision_shape.instantiate();
		baked_collision_shape->set_faces(root_collision_shape->get_faces());
	} else if (is_root_shape()) {
		baked_collision_shape.instantiate();
		baked_collision_shape->set_faces(_get_brush_collision_faces());
	}
	return baked_collision_shape;
}

bool CSGShape3D::_is_debug_collision_shape_visible() {
	return !Engine::get_singleton()->is_editor_hint() && is_inside_tree() && get_tree()->is_debugging_collisions_hint();
}

void CSGShape3D::_update_debug_collision_shape() {
	if (!use_collision || !is_root_shape() || root_collision_shape.is_null() || !_is_debug_collision_shape_visible()) {
		return;
	}

	ERR_FAIL_NULL(RenderingServer::get_singleton());

	if (root_collision_debug_instance.is_null()) {
		root_collision_debug_instance = RS::get_singleton()->instance_create();
	}

	Ref<Mesh> debug_mesh = root_collision_shape->get_debug_mesh();
	RS::get_singleton()->instance_set_scenario(root_collision_debug_instance, get_world_3d()->get_scenario());
	RS::get_singleton()->instance_set_base(root_collision_debug_instance, debug_mesh->get_rid());
	RS::get_singleton()->instance_set_transform(root_collision_debug_instance, get_global_transform());
}

void CSGShape3D::_clear_debug_collision_shape() {
	if (root_collision_debug_instance.is_valid()) {
		RS::get_singleton()->free_rid(root_collision_debug_instance);
		root_collision_debug_instance = RID();
	}
}

void CSGShape3D::_on_transform_changed() {
	if (root_collision_debug_instance.is_valid() && !debug_shape_old_transform.is_equal_approx(get_global_transform())) {
		debug_shape_old_transform = get_global_transform();
		RS::get_singleton()->instance_set_transform(root_collision_debug_instance, debug_shape_old_transform);
	}
}
#endif // PHYSICS_3D_DISABLED

AABB CSGShape3D::get_aabb() const {
	return node_aabb;
}

Vector<Vector3> CSGShape3D::get_brush_faces() {
	ERR_FAIL_COND_V(!is_inside_tree(), Vector<Vector3>());
	CSGBrush *b = _get_brush();
	if (!b) {
		return Vector<Vector3>();
	}

	Vector<Vector3> faces;
	int fc = b->faces.size();
	faces.resize(fc * 3);
	{
		Vector3 *w = faces.ptrw();
		for (int i = 0; i < fc; i++) {
			w[i * 3 + 0] = b->faces[i].vertices[0];
			w[i * 3 + 1] = b->faces[i].vertices[1];
			w[i * 3 + 2] = b->faces[i].vertices[2];
		}
	}

	return faces;
}

Ref<CSGGeometryData> CSGShape3D::get_geometry_data(real_t p_merge_epsilon, real_t p_sharp_angle) {
	CSGBrush *current_brush = _get_brush();
	if (!current_brush) {
		return Ref<CSGGeometryData>();
	}
	Ref<CSGGeometryData> geometry_data;
	geometry_data.instantiate();
	geometry_data->build(*current_brush, p_merge_epsilon, p_sharp_angle);
	return geometry_data;
}

void CSGShape3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_PARENTED: {
			Node *parentn = get_parent();
			if (parentn) {
				parent_shape = Object::cast_to<CSGShape3D>(parentn);
				if (parent_shape) {
					set_base(RID());
					root_mesh.unref();
				}
			}
			if (!brush || parent_shape) {
				// Update this node if uninitialized, or both this node and its new parent if it gets added to another CSG shape
				_make_dirty();
			}
			last_visible = is_visible();
		} break;

		case NOTIFICATION_UNPARENTED: {
			if (!is_root_shape()) {
				// Update this node and its previous parent only if it's currently being removed from another CSG shape
				_make_dirty(true); // Must be forced since is_root_shape() uses the previous parent
			}
			parent_shape = nullptr;
		} break;

		case NOTIFICATION_CHILD_ORDER_CHANGED: {
			_make_dirty();
		} break;

		case NOTIFICATION_VISIBILITY_CHANGED: {
			if (!is_root_shape() && last_visible != is_visible()) {
				// Update this node's parent only if its own visibility has changed, not the visibility of parent nodes
				parent_shape->_make_dirty();
			}
			last_visible = is_visible();
		} break;

		case NOTIFICATION_LOCAL_TRANSFORM_CHANGED: {
			if (!is_root_shape()) {
				// Update this node's parent only if its own transformation has changed, not the transformation of parent nodes
				parent_shape->_make_dirty();
			}
		} break;

#ifndef PHYSICS_3D_DISABLED
		case NOTIFICATION_ENTER_TREE: {
			if (use_collision && is_root_shape()) {
				root_collision_shape.instantiate();
				root_collision_instance = PhysicsServer3D::get_singleton()->body_create();
				PhysicsServer3D::get_singleton()->body_set_mode(root_collision_instance, PhysicsServer3D::BODY_MODE_STATIC);
				PhysicsServer3D::get_singleton()->body_set_state(root_collision_instance, PhysicsServer3D::BODY_STATE_TRANSFORM, get_global_transform());
				PhysicsServer3D::get_singleton()->body_add_shape(root_collision_instance, root_collision_shape->get_rid());
				PhysicsServer3D::get_singleton()->body_set_space(root_collision_instance, get_world_3d()->get_space());
				PhysicsServer3D::get_singleton()->body_attach_object_instance_id(root_collision_instance, get_instance_id());
				set_collision_layer(collision_layer);
				set_collision_mask(collision_mask);
				set_collision_priority(collision_priority);
				debug_shape_old_transform = get_global_transform();
				_make_dirty();
			}
		} break;

		case NOTIFICATION_EXIT_TREE: {
			if (use_collision && is_root_shape() && root_collision_instance.is_valid()) {
				PhysicsServer3D::get_singleton()->free_rid(root_collision_instance);
				root_collision_instance = RID();
				root_collision_shape.unref();
				_clear_debug_collision_shape();
			}
		} break;

		case NOTIFICATION_TRANSFORM_CHANGED: {
			if (use_collision && is_root_shape() && root_collision_instance.is_valid()) {
				PhysicsServer3D::get_singleton()->body_set_state(root_collision_instance, PhysicsServer3D::BODY_STATE_TRANSFORM, get_global_transform());
			}
			_on_transform_changed();
		} break;
#endif // PHYSICS_3D_DISABLED
	}
}

void CSGShape3D::set_operation(Operation p_operation) {
	operation = p_operation;
	_make_dirty();
	update_gizmos();
}

CSGShape3D::Operation CSGShape3D::get_operation() const {
	return operation;
}

void CSGShape3D::set_calculate_tangents(bool p_calculate_tangents) {
	calculate_tangents = p_calculate_tangents;
	_make_dirty();
}

bool CSGShape3D::is_calculating_tangents() const {
	return calculate_tangents;
}

void CSGShape3D::_validate_property(PropertyInfo &p_property) const {
	if (!Engine::get_singleton()->is_editor_hint()) {
		return;
	}

	if (p_property.name == "smoothing_angle") {
		if (!autosmooth || (is_inside_tree() && !is_root_shape())) {
			p_property.usage = PROPERTY_USAGE_NO_EDITOR;
		}
	}

	if (p_property.name == "autosmooth") {
		if (is_inside_tree() && !is_root_shape()) {
			p_property.usage = PROPERTY_USAGE_NO_EDITOR;
		}
	}

	bool is_collision_prefixed = p_property.name.begins_with("collision_");
	if ((is_collision_prefixed || p_property.name.begins_with("use_collision")) && is_inside_tree() && !is_root_shape()) {
		//hide collision if not root
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	} else if (is_collision_prefixed && !bool(get("use_collision"))) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
}

Array CSGShape3D::get_meshes() const {
	if (root_mesh.is_valid()) {
		Array arr;
		arr.resize(2);
		arr[0] = Transform3D();
		arr[1] = root_mesh;
		return arr;
	}

	return Array();
}

PackedStringArray CSGShape3D::get_configuration_warnings() const {
	PackedStringArray warnings = Node::get_configuration_warnings();
	const CSGShape3D *current_shape = this;
	while (current_shape) {
		if (!current_shape->brush || current_shape->brush->faces.is_empty()) {
			warnings.push_back(RTR("The CSGShape3D has an empty shape.\nCSGShape3D empty shapes typically occur because the mesh is not manifold.\nA manifold mesh forms a solid object without gaps, holes, or loose edges.\nEach edge must be a member of exactly two faces."));
			break;
		}
		current_shape = current_shape->parent_shape;
	}
	return warnings;
}

Ref<TriangleMesh> CSGShape3D::generate_triangle_mesh() const {
	if (root_mesh.is_valid()) {
		return root_mesh->generate_triangle_mesh();
	}
	return Ref<TriangleMesh>();
}

void CSGShape3D::_modifier_changed() {
	_make_dirty();
}

void CSGShape3D::set_modifiers(const TypedArray<CSGModifier> &p_modifiers) {
	Callable changed_callable = callable_mp(this, &CSGShape3D::_modifier_changed);
	for (int i = 0; i < modifiers.size(); i++) {
		Ref<CSGModifier> modifier = modifiers[i];
		if (modifier.is_valid() && modifier->is_connected(StringName("changed"), changed_callable)) {
			modifier->disconnect(StringName("changed"), changed_callable);
		}
	}

	modifiers = p_modifiers;

	for (int i = 0; i < modifiers.size(); i++) {
		Ref<CSGModifier> modifier = modifiers[i];
		if (modifier.is_valid() && !modifier->is_connected(StringName("changed"), changed_callable)) {
			modifier->connect(StringName("changed"), changed_callable);
		}
	}
	_make_dirty();
}

TypedArray<CSGModifier> CSGShape3D::get_modifiers() const {
	return modifiers;
}

void CSGShape3D::_bevel_settings_changed() {
	_make_dirty();
}

void CSGShape3D::set_bevel_settings(const Ref<CSGBevelSettings> &p_bevel_settings) {
	if (bevel_settings == p_bevel_settings) {
		return;
	}

	Callable changed_callable = callable_mp(this, &CSGShape3D::_bevel_settings_changed);
	if (bevel_settings.is_valid() && bevel_settings->is_connected(StringName("changed"), changed_callable)) {
		bevel_settings->disconnect(StringName("changed"), changed_callable);
	}

	bevel_settings = p_bevel_settings;
	if (bevel_settings.is_valid()) {
		bevel_settings->connect(StringName("changed"), changed_callable);
	}
	_make_dirty();
}

Ref<CSGBevelSettings> CSGShape3D::get_bevel_settings() const {
	return bevel_settings;
}

void CSGShape3D::_topology_settings_changed() {
	_make_dirty();
}

void CSGShape3D::set_topology_settings(const Ref<CSGTopologySettings> &p_topology_settings) {
	if (topology_settings == p_topology_settings) {
		return;
	}

	Callable changed_callable = callable_mp(this, &CSGShape3D::_topology_settings_changed);
	if (topology_settings.is_valid() && topology_settings->is_connected(StringName("changed"), changed_callable)) {
		topology_settings->disconnect(StringName("changed"), changed_callable);
	}

	topology_settings = p_topology_settings;
	if (topology_settings.is_valid()) {
		topology_settings->connect(StringName("changed"), changed_callable);
	}
	_make_dirty();
}

Ref<CSGTopologySettings> CSGShape3D::get_topology_settings() const {
	return topology_settings;
}

void CSGShape3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_root_shape"), &CSGShape3D::is_root_shape);

	ClassDB::bind_method(D_METHOD("set_operation", "operation"), &CSGShape3D::set_operation);
	ClassDB::bind_method(D_METHOD("get_operation"), &CSGShape3D::get_operation);

#ifndef DISABLE_DEPRECATED
	ClassDB::bind_method(D_METHOD("_update_shape"), &CSGShape3D::update_shape);
	ClassDB::bind_method(D_METHOD("set_snap", "snap"), &CSGShape3D::set_snap);
	ClassDB::bind_method(D_METHOD("get_snap"), &CSGShape3D::get_snap);
#endif // DISABLE_DEPRECATED

#ifndef PHYSICS_3D_DISABLED
	ClassDB::bind_method(D_METHOD("set_use_collision", "operation"), &CSGShape3D::set_use_collision);
	ClassDB::bind_method(D_METHOD("is_using_collision"), &CSGShape3D::is_using_collision);

	ClassDB::bind_method(D_METHOD("set_collision_layer", "layer"), &CSGShape3D::set_collision_layer);
	ClassDB::bind_method(D_METHOD("get_collision_layer"), &CSGShape3D::get_collision_layer);

	ClassDB::bind_method(D_METHOD("set_collision_mask", "mask"), &CSGShape3D::set_collision_mask);
	ClassDB::bind_method(D_METHOD("get_collision_mask"), &CSGShape3D::get_collision_mask);

	ClassDB::bind_method(D_METHOD("set_collision_mask_value", "layer_number", "value"), &CSGShape3D::set_collision_mask_value);
	ClassDB::bind_method(D_METHOD("get_collision_mask_value", "layer_number"), &CSGShape3D::get_collision_mask_value);

	ClassDB::bind_method(D_METHOD("_get_root_collision_instance"), &CSGShape3D::_get_root_collision_instance);

	ClassDB::bind_method(D_METHOD("set_collision_layer_value", "layer_number", "value"), &CSGShape3D::set_collision_layer_value);
	ClassDB::bind_method(D_METHOD("get_collision_layer_value", "layer_number"), &CSGShape3D::get_collision_layer_value);

	ClassDB::bind_method(D_METHOD("set_collision_priority", "priority"), &CSGShape3D::set_collision_priority);
	ClassDB::bind_method(D_METHOD("get_collision_priority"), &CSGShape3D::get_collision_priority);

	ClassDB::bind_method(D_METHOD("bake_collision_shape"), &CSGShape3D::bake_collision_shape);
#endif // PHYSICS_3D_DISABLED

	ClassDB::bind_method(D_METHOD("set_calculate_tangents", "enabled"), &CSGShape3D::set_calculate_tangents);
	ClassDB::bind_method(D_METHOD("is_calculating_tangents"), &CSGShape3D::is_calculating_tangents);

	ClassDB::bind_method(D_METHOD("get_meshes"), &CSGShape3D::get_meshes);
	ClassDB::bind_method(D_METHOD("get_geometry_data", "merge_epsilon", "sharp_angle"), &CSGShape3D::get_geometry_data, DEFVAL(0.00001), DEFVAL(Math::deg_to_rad(30.0)));

	ClassDB::bind_method(D_METHOD("set_modifiers", "modifiers"), &CSGShape3D::set_modifiers);
	ClassDB::bind_method(D_METHOD("get_modifiers"), &CSGShape3D::get_modifiers);
	ClassDB::bind_method(D_METHOD("set_bevel_settings", "bevel_settings"), &CSGShape3D::set_bevel_settings);
	ClassDB::bind_method(D_METHOD("get_bevel_settings"), &CSGShape3D::get_bevel_settings);
	ClassDB::bind_method(D_METHOD("set_topology_settings", "topology_settings"), &CSGShape3D::set_topology_settings);
	ClassDB::bind_method(D_METHOD("get_topology_settings"), &CSGShape3D::get_topology_settings);

	ClassDB::bind_method(D_METHOD("bake_static_mesh"), &CSGShape3D::bake_static_mesh);

	ClassDB::bind_method(D_METHOD("set_autosmooth", "autosmooth"), &CSGShape3D::set_autosmooth);
	ClassDB::bind_method(D_METHOD("is_autosmooth"), &CSGShape3D::is_autosmooth);

	ClassDB::bind_method(D_METHOD("set_smoothing_angle", "smoothing_angle"), &CSGShape3D::set_smoothing_angle);
	ClassDB::bind_method(D_METHOD("get_smoothing_angle"), &CSGShape3D::get_smoothing_angle);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "autosmooth"), "set_autosmooth", "is_autosmooth");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "smoothing_angle", PROPERTY_HINT_RANGE, "0,180,0.1,degrees"), "set_smoothing_angle", "get_smoothing_angle");

	ADD_PROPERTY(PropertyInfo(Variant::INT, "operation", PROPERTY_HINT_ENUM, "Union,Intersection,Subtraction"), "set_operation", "get_operation");
#ifndef DISABLE_DEPRECATED
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "snap", PROPERTY_HINT_RANGE, "0.000001,1,0.000001,suffix:m", PROPERTY_USAGE_NONE), "set_snap", "get_snap");
#endif // DISABLE_DEPRECATED
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "calculate_tangents"), "set_calculate_tangents", "is_calculating_tangents");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "bevel_settings", PROPERTY_HINT_RESOURCE_TYPE, "CSGBevelSettings"), "set_bevel_settings", "get_bevel_settings");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "topology_settings", PROPERTY_HINT_RESOURCE_TYPE, "CSGTopologySettings"), "set_topology_settings", "get_topology_settings");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "modifiers", PROPERTY_HINT_ARRAY_TYPE, MAKE_RESOURCE_TYPE_HINT("CSGModifier")), "set_modifiers", "get_modifiers");

#ifndef PHYSICS_3D_DISABLED
	ADD_GROUP("Collision", "collision_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_collision"), "set_use_collision", "is_using_collision");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_layer", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_collision_layer", "get_collision_layer");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_mask", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_collision_mask", "get_collision_mask");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "collision_priority"), "set_collision_priority", "get_collision_priority");
#endif // PHYSICS_3D_DISABLED

	BIND_ENUM_CONSTANT(OPERATION_UNION);
	BIND_ENUM_CONSTANT(OPERATION_INTERSECTION);
	BIND_ENUM_CONSTANT(OPERATION_SUBTRACTION);
}

CSGShape3D::CSGShape3D() {
	set_notify_local_transform(true);
}

CSGShape3D::~CSGShape3D() {
	if (brush) {
		memdelete(brush);
		brush = nullptr;
	}
}

//////////////////////////////////

CSGBrush *CSGCombiner3D::_build_brush() {
	return memnew(CSGBrush); //does not build anything
}

CSGCombiner3D::CSGCombiner3D() {
}

/////////////////////

CSGBrush *CSGPrimitive3D::_create_brush_from_arrays(const Vector<Vector3> &p_vertices, const Vector<Vector2> &p_uv, const Vector<bool> &p_smooth, const Vector<Ref<Material>> &p_materials, const Vector<int> &p_surface_ids) {
	CSGBrush *new_brush = memnew(CSGBrush);

	Vector<bool> invert;
	invert.resize(p_vertices.size() / 3);
	{
		int ic = invert.size();
		bool *w = invert.ptrw();
		for (int i = 0; i < ic; i++) {
			w[i] = flip_faces;
		}
	}
	new_brush->build_from_faces(p_vertices, p_uv, p_smooth, p_materials, invert, p_surface_ids);

	return new_brush;
}

void CSGPrimitive3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_flip_faces", "flip_faces"), &CSGPrimitive3D::set_flip_faces);
	ClassDB::bind_method(D_METHOD("get_flip_faces"), &CSGPrimitive3D::get_flip_faces);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "flip_faces"), "set_flip_faces", "get_flip_faces");
}

void CSGPrimitive3D::set_flip_faces(bool p_invert) {
	if (flip_faces == p_invert) {
		return;
	}

	flip_faces = p_invert;

	_make_dirty();
}

bool CSGPrimitive3D::get_flip_faces() {
	return flip_faces;
}

CSGPrimitive3D::CSGPrimitive3D() {
	flip_faces = false;
}

/////////////////////

CSGBrush *CSGMesh3D::_build_brush() {
	if (mesh.is_null()) {
		return memnew(CSGBrush);
	}

	Vector<Vector3> vertices;
	Vector<bool> smooth;
	Vector<Ref<Material>> materials;
	Vector<int> surface_ids;
	Vector<Vector2> uvs;
	Ref<Material> base_material = get_material();

	for (int i = 0; i < mesh->get_surface_count(); i++) {
		if (mesh->surface_get_primitive_type(i) != Mesh::PRIMITIVE_TRIANGLES) {
			continue;
		}

		Array arrays = mesh->surface_get_arrays(i);

		if (arrays.is_empty()) {
			_make_dirty();
			ERR_FAIL_COND_V(arrays.is_empty(), memnew(CSGBrush));
		}

		Vector<Vector3> avertices = arrays[Mesh::ARRAY_VERTEX];
		if (avertices.is_empty()) {
			continue;
		}

		const Vector3 *vr = avertices.ptr();

		Vector<Vector3> anormals = arrays[Mesh::ARRAY_NORMAL];
		const Vector3 *nr = nullptr;
		if (anormals.size()) {
			nr = anormals.ptr();
		}

		Vector<Vector2> auvs = arrays[Mesh::ARRAY_TEX_UV];
		const Vector2 *uvr = nullptr;
		if (auvs.size()) {
			uvr = auvs.ptr();
		}

		Ref<Material> mat;
		if (base_material.is_valid()) {
			mat = base_material;
		} else {
			mat = mesh->surface_get_material(i);
		}

		Vector<int> aindices = arrays[Mesh::ARRAY_INDEX];
		if (aindices.size()) {
			int as = vertices.size();
			int is = aindices.size();

			vertices.resize(as + is);
			smooth.resize((as + is) / 3);
			materials.resize((as + is) / 3);
			surface_ids.resize((as + is) / 3);
			uvs.resize(as + is);

			Vector3 *vw = vertices.ptrw();
			bool *sw = smooth.ptrw();
			Vector2 *uvw = uvs.ptrw();
			Ref<Material> *mw = materials.ptrw();
			int *surface_ids_w = surface_ids.ptrw();

			const int *ir = aindices.ptr();

			for (int j = 0; j < is; j += 3) {
				Vector3 vertex[3];
				Vector3 normal[3];
				Vector2 uv[3];

				for (int k = 0; k < 3; k++) {
					int idx = ir[j + k];
					vertex[k] = vr[idx];
					if (nr) {
						normal[k] = nr[idx];
					}
					if (uvr) {
						uv[k] = uvr[idx];
					}
				}

				bool flat = normal[0].is_equal_approx(normal[1]) && normal[0].is_equal_approx(normal[2]);

				vw[as + j + 0] = vertex[0];
				vw[as + j + 1] = vertex[1];
				vw[as + j + 2] = vertex[2];

				uvw[as + j + 0] = uv[0];
				uvw[as + j + 1] = uv[1];
				uvw[as + j + 2] = uv[2];

				sw[(as + j) / 3] = !flat;
				mw[(as + j) / 3] = mat;
				surface_ids_w[(as + j) / 3] = i;
			}
		} else {
			int as = vertices.size();
			int is = avertices.size();

			vertices.resize(as + is);
			smooth.resize((as + is) / 3);
			uvs.resize(as + is);
			materials.resize((as + is) / 3);
			surface_ids.resize((as + is) / 3);

			Vector3 *vw = vertices.ptrw();
			bool *sw = smooth.ptrw();
			Vector2 *uvw = uvs.ptrw();
			Ref<Material> *mw = materials.ptrw();
			int *surface_ids_w = surface_ids.ptrw();

			for (int j = 0; j < is; j += 3) {
				Vector3 vertex[3];
				Vector3 normal[3];
				Vector2 uv[3];

				for (int k = 0; k < 3; k++) {
					vertex[k] = vr[j + k];
					if (nr) {
						normal[k] = nr[j + k];
					}
					if (uvr) {
						uv[k] = uvr[j + k];
					}
				}

				bool flat = normal[0].is_equal_approx(normal[1]) && normal[0].is_equal_approx(normal[2]);

				vw[as + j + 0] = vertex[0];
				vw[as + j + 1] = vertex[1];
				vw[as + j + 2] = vertex[2];

				uvw[as + j + 0] = uv[0];
				uvw[as + j + 1] = uv[1];
				uvw[as + j + 2] = uv[2];

				sw[(as + j) / 3] = !flat;
				mw[(as + j) / 3] = mat;
				surface_ids_w[(as + j) / 3] = i;
			}
		}
	}

	if (vertices.is_empty()) {
		return memnew(CSGBrush);
	}

	return _create_brush_from_arrays(vertices, uvs, smooth, materials, surface_ids);
}

void CSGMesh3D::_mesh_changed() {
	_make_dirty();

	callable_mp((Node3D *)this, &Node3D::update_gizmos).call_deferred();
}

void CSGMesh3D::set_material(const Ref<Material> &p_material) {
	if (material == p_material) {
		return;
	}
	material = p_material;
	_make_dirty();
}

Ref<Material> CSGMesh3D::get_material() const {
	return material;
}

void CSGMesh3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_mesh", "mesh"), &CSGMesh3D::set_mesh);
	ClassDB::bind_method(D_METHOD("get_mesh"), &CSGMesh3D::get_mesh);

	ClassDB::bind_method(D_METHOD("set_material", "material"), &CSGMesh3D::set_material);
	ClassDB::bind_method(D_METHOD("get_material"), &CSGMesh3D::get_material);

	// Hide PrimitiveMeshes that are always non-manifold and therefore can't be used as CSG meshes.
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "mesh", PROPERTY_HINT_RESOURCE_TYPE, "Mesh,-PlaneMesh,-PointMesh,-QuadMesh,-RibbonTrailMesh"), "set_mesh", "get_mesh");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material", PROPERTY_HINT_RESOURCE_TYPE, "BaseMaterial3D,ShaderMaterial"), "set_material", "get_material");
}

void CSGMesh3D::set_mesh(const Ref<Mesh> &p_mesh) {
	if (mesh == p_mesh) {
		return;
	}
	if (mesh.is_valid()) {
		mesh->disconnect_changed(callable_mp(this, &CSGMesh3D::_mesh_changed));
	}
	mesh = p_mesh;

	if (mesh.is_valid()) {
		mesh->connect_changed(callable_mp(this, &CSGMesh3D::_mesh_changed));
	}

	_mesh_changed();
}

Ref<Mesh> CSGMesh3D::get_mesh() {
	return mesh;
}

////////////////////////////////

CSGBrush *CSGSphere3D::_build_brush() {
	// set our bounding box

	CSGBrush *new_brush = memnew(CSGBrush);

	int face_count = rings * radial_segments * 2 - radial_segments * 2;

	bool invert_val = get_flip_faces();
	Ref<Material> base_material = get_material();

	Vector<Vector3> faces;
	Vector<Vector2> uvs;
	Vector<bool> smooth;
	Vector<Ref<Material>> materials;
	Vector<bool> invert;

	faces.resize(face_count * 3);
	uvs.resize(face_count * 3);

	smooth.resize(face_count);
	materials.resize(face_count);
	invert.resize(face_count);

	{
		Vector3 *facesw = faces.ptrw();
		Vector2 *uvsw = uvs.ptrw();
		bool *smoothw = smooth.ptrw();
		Ref<Material> *materialsw = materials.ptrw();
		bool *invertw = invert.ptrw();

		// We want to follow an order that's convenient for UVs.
		// For latitude step we start at the top and move down like in an image.
		const double latitude_step = -Math::PI / rings;
		const double longitude_step = Math::TAU / radial_segments;
		int face = 0;
		for (int i = 0; i < rings; i++) {
			double cos0 = 0;
			double sin0 = 1;
			if (i > 0) {
				double latitude0 = latitude_step * i + Math::TAU / 4;
				cos0 = Math::cos(latitude0);
				sin0 = Math::sin(latitude0);
			}
			double v0 = double(i) / rings;

			double cos1 = 0;
			double sin1 = -1;
			if (i < rings - 1) {
				double latitude1 = latitude_step * (i + 1) + Math::TAU / 4;
				cos1 = Math::cos(latitude1);
				sin1 = Math::sin(latitude1);
			}
			double v1 = double(i + 1) / rings;

			for (int j = 0; j < radial_segments; j++) {
				double longitude0 = longitude_step * j;
				// We give sin to X and cos to Z on purpose.
				// This allows UVs to be CCW on +X so it maps to images well.
				double x0 = Math::sin(longitude0);
				double z0 = Math::cos(longitude0);
				double u0 = double(j) / radial_segments;

				double longitude1 = longitude_step * (j + 1);
				if (j == radial_segments - 1) {
					longitude1 = 0;
				}

				double x1 = Math::sin(longitude1);
				double z1 = Math::cos(longitude1);
				double u1 = double(j + 1) / radial_segments;

				Vector3 v[4] = {
					Vector3(x0 * cos0, sin0, z0 * cos0) * radius,
					Vector3(x1 * cos0, sin0, z1 * cos0) * radius,
					Vector3(x1 * cos1, sin1, z1 * cos1) * radius,
					Vector3(x0 * cos1, sin1, z0 * cos1) * radius,
				};

				Vector2 u[4] = {
					Vector2(u0, v0),
					Vector2(u1, v0),
					Vector2(u1, v1),
					Vector2(u0, v1),
				};

				// Draw the first face, but skip this at the north pole (i == 0).
				if (i > 0) {
					facesw[face * 3 + 0] = v[0];
					facesw[face * 3 + 1] = v[1];
					facesw[face * 3 + 2] = v[2];

					uvsw[face * 3 + 0] = u[0];
					uvsw[face * 3 + 1] = u[1];
					uvsw[face * 3 + 2] = u[2];

					smoothw[face] = smooth_faces;
					invertw[face] = invert_val;
					materialsw[face] = base_material;

					face++;
				}

				// Draw the second face, but skip this at the south pole (i == rings - 1).
				if (i < rings - 1) {
					facesw[face * 3 + 0] = v[2];
					facesw[face * 3 + 1] = v[3];
					facesw[face * 3 + 2] = v[0];

					uvsw[face * 3 + 0] = u[2];
					uvsw[face * 3 + 1] = u[3];
					uvsw[face * 3 + 2] = u[0];

					smoothw[face] = smooth_faces;
					invertw[face] = invert_val;
					materialsw[face] = base_material;

					face++;
				}
			}
		}

		if (face != face_count) {
			ERR_PRINT("Face mismatch bug! fix code");
		}
	}

	new_brush->build_from_faces(faces, uvs, smooth, materials, invert);

	return new_brush;
}

void CSGSphere3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_radius", "radius"), &CSGSphere3D::set_radius);
	ClassDB::bind_method(D_METHOD("get_radius"), &CSGSphere3D::get_radius);

	ClassDB::bind_method(D_METHOD("set_radial_segments", "radial_segments"), &CSGSphere3D::set_radial_segments);
	ClassDB::bind_method(D_METHOD("get_radial_segments"), &CSGSphere3D::get_radial_segments);
	ClassDB::bind_method(D_METHOD("set_rings", "rings"), &CSGSphere3D::set_rings);
	ClassDB::bind_method(D_METHOD("get_rings"), &CSGSphere3D::get_rings);

	ClassDB::bind_method(D_METHOD("set_smooth_faces", "smooth_faces"), &CSGSphere3D::set_smooth_faces);
	ClassDB::bind_method(D_METHOD("get_smooth_faces"), &CSGSphere3D::get_smooth_faces);

	ClassDB::bind_method(D_METHOD("set_material", "material"), &CSGSphere3D::set_material);
	ClassDB::bind_method(D_METHOD("get_material"), &CSGSphere3D::get_material);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius", PROPERTY_HINT_RANGE, "0.001,100.0,0.001,suffix:m"), "set_radius", "get_radius");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "radial_segments", PROPERTY_HINT_RANGE, "1,100,1"), "set_radial_segments", "get_radial_segments");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "rings", PROPERTY_HINT_RANGE, "1,100,1"), "set_rings", "get_rings");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "smooth_faces"), "set_smooth_faces", "get_smooth_faces");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material", PROPERTY_HINT_RESOURCE_TYPE, "BaseMaterial3D,ShaderMaterial"), "set_material", "get_material");
}

void CSGSphere3D::set_radius(const float p_radius) {
	ERR_FAIL_COND(p_radius <= 0);
	radius = p_radius;
	_make_dirty();
	update_gizmos();
}

float CSGSphere3D::get_radius() const {
	return radius;
}

void CSGSphere3D::set_radial_segments(const int p_radial_segments) {
	radial_segments = p_radial_segments > 4 ? p_radial_segments : 4;
	_make_dirty();
	update_gizmos();
}

int CSGSphere3D::get_radial_segments() const {
	return radial_segments;
}

void CSGSphere3D::set_rings(const int p_rings) {
	rings = p_rings > 1 ? p_rings : 1;
	_make_dirty();
	update_gizmos();
}

int CSGSphere3D::get_rings() const {
	return rings;
}

void CSGSphere3D::set_smooth_faces(const bool p_smooth_faces) {
	smooth_faces = p_smooth_faces;
	_make_dirty();
}

bool CSGSphere3D::get_smooth_faces() const {
	return smooth_faces;
}

void CSGSphere3D::set_material(const Ref<Material> &p_material) {
	material = p_material;
	_make_dirty();
}

Ref<Material> CSGSphere3D::get_material() const {
	return material;
}

CSGSphere3D::CSGSphere3D() {
	// defaults
	radius = 0.5;
	radial_segments = 12;
	rings = 6;
	smooth_faces = true;
}

///////////////

CSGBrush *CSGBox3D::_build_brush() {
	// set our bounding box

	CSGBrush *new_brush = memnew(CSGBrush);

	int face_count = 12; //it's a cube..

	bool invert_val = get_flip_faces();
	Ref<Material> base_material = get_material();

	Vector<Vector3> faces;
	Vector<Vector2> uvs;
	Vector<bool> smooth;
	Vector<Ref<Material>> materials;
	Vector<bool> invert;

	faces.resize(face_count * 3);
	uvs.resize(face_count * 3);

	smooth.resize(face_count);
	materials.resize(face_count);
	invert.resize(face_count);

	{
		Vector3 *facesw = faces.ptrw();
		Vector2 *uvsw = uvs.ptrw();
		bool *smoothw = smooth.ptrw();
		Ref<Material> *materialsw = materials.ptrw();
		bool *invertw = invert.ptrw();

		int face = 0;

		Vector3 vertex_mul = size / 2;

		{
			for (int i = 0; i < 6; i++) {
				Vector3 face_points[4];
				float uv_points[8] = { 0, 0, 0, 1, 1, 1, 1, 0 };

				for (int j = 0; j < 4; j++) {
					float v[3];
					v[0] = 1.0;
					v[1] = 1 - 2 * ((j >> 1) & 1);
					v[2] = v[1] * (1 - 2 * (j & 1));

					for (int k = 0; k < 3; k++) {
						if (i < 3) {
							face_points[j][(i + k) % 3] = v[k];
						} else {
							face_points[3 - j][(i + k) % 3] = -v[k];
						}
					}
				}

				Vector2 u[4];
				for (int j = 0; j < 4; j++) {
					u[j] = Vector2(uv_points[j * 2 + 0], uv_points[j * 2 + 1]);
				}

				//face 1
				facesw[face * 3 + 0] = face_points[0] * vertex_mul;
				facesw[face * 3 + 1] = face_points[1] * vertex_mul;
				facesw[face * 3 + 2] = face_points[2] * vertex_mul;

				uvsw[face * 3 + 0] = u[0];
				uvsw[face * 3 + 1] = u[1];
				uvsw[face * 3 + 2] = u[2];

				smoothw[face] = false;
				invertw[face] = invert_val;
				materialsw[face] = base_material;

				face++;
				//face 2
				facesw[face * 3 + 0] = face_points[2] * vertex_mul;
				facesw[face * 3 + 1] = face_points[3] * vertex_mul;
				facesw[face * 3 + 2] = face_points[0] * vertex_mul;

				uvsw[face * 3 + 0] = u[2];
				uvsw[face * 3 + 1] = u[3];
				uvsw[face * 3 + 2] = u[0];

				smoothw[face] = false;
				invertw[face] = invert_val;
				materialsw[face] = base_material;

				face++;
			}
		}

		if (face != face_count) {
			ERR_PRINT("Face mismatch bug! fix code");
		}
	}

	new_brush->build_from_faces(faces, uvs, smooth, materials, invert);

	return new_brush;
}

void CSGBox3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_size", "size"), &CSGBox3D::set_size);
	ClassDB::bind_method(D_METHOD("get_size"), &CSGBox3D::get_size);

	ClassDB::bind_method(D_METHOD("set_material", "material"), &CSGBox3D::set_material);
	ClassDB::bind_method(D_METHOD("get_material"), &CSGBox3D::get_material);

	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "size", PROPERTY_HINT_NONE, "suffix:m"), "set_size", "get_size");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material", PROPERTY_HINT_RESOURCE_TYPE, "BaseMaterial3D,ShaderMaterial"), "set_material", "get_material");
}

void CSGBox3D::set_size(const Vector3 &p_size) {
	size = p_size;
	_make_dirty();
	update_gizmos();
}

Vector3 CSGBox3D::get_size() const {
	return size;
}

#ifndef DISABLE_DEPRECATED
// Kept for compatibility from 3.x to 4.0.
bool CSGBox3D::_set(const StringName &p_name, const Variant &p_value) {
	if (p_name == "width") {
		size.x = p_value;
		_make_dirty();
		update_gizmos();
		return true;
	} else if (p_name == "height") {
		size.y = p_value;
		_make_dirty();
		update_gizmos();
		return true;
	} else if (p_name == "depth") {
		size.z = p_value;
		_make_dirty();
		update_gizmos();
		return true;
	} else {
		return false;
	}
}
#endif

void CSGBox3D::set_material(const Ref<Material> &p_material) {
	material = p_material;
	_make_dirty();
	update_gizmos();
}

Ref<Material> CSGBox3D::get_material() const {
	return material;
}

/////////////////////

namespace {

struct HeightMapContourSegment {
	uint64_t points[2] = {};
	bool used = false;
};

static uint64_t _height_map_contour_point_key(int p_x, int p_z) {
	return (uint64_t(uint32_t(p_x)) << 32) | uint32_t(p_z);
}

static Vector2 _height_map_contour_point_position(uint64_t p_key, const Vector<real_t> &p_sample_x, const Vector<real_t> &p_sample_z) {
	const int doubled_x = int(uint32_t(p_key >> 32));
	const int doubled_z = int(uint32_t(p_key));
	auto coordinate = [](int p_doubled, const Vector<real_t> &p_positions) {
		const int index = p_doubled / 2;
		return (p_doubled & 1) ? (p_positions[index] + p_positions[index + 1]) * 0.5 : p_positions[index];
	};
	return Vector2(coordinate(doubled_x, p_sample_x), coordinate(doubled_z, p_sample_z));
}

static manifold::Polygons _height_map_trace_contours(const Vector<real_t> &p_heights, int p_grid_width, int p_grid_depth, const Vector<real_t> &p_x_positions, const Vector<real_t> &p_z_positions, real_t p_level) {
	const int sample_width = p_grid_width + 2;
	const int sample_depth = p_grid_depth + 2;
	Vector<uint8_t> mask;
	mask.resize(sample_width * sample_depth);
	mask.fill(0);
	for (int z = 0; z < p_grid_depth; z++) {
		for (int x = 0; x < p_grid_width; x++) {
			mask.write[(z + 1) * sample_width + x + 1] = p_heights[z * p_grid_width + x] >= p_level || Math::is_equal_approx(p_heights[z * p_grid_width + x], p_level);
		}
	}

	Vector<real_t> sample_x;
	Vector<real_t> sample_z;
	sample_x.resize(sample_width);
	sample_z.resize(sample_depth);
	for (int x = 0; x < p_grid_width; x++) {
		sample_x.write[x + 1] = (p_x_positions[x] + p_x_positions[x + 1]) * 0.5;
	}
	for (int z = 0; z < p_grid_depth; z++) {
		sample_z.write[z + 1] = (p_z_positions[z] + p_z_positions[z + 1]) * 0.5;
	}
	sample_x.write[0] = p_x_positions[0] * 2.0 - sample_x[1];
	sample_x.write[sample_width - 1] = p_x_positions[p_grid_width] * 2.0 - sample_x[sample_width - 2];
	sample_z.write[0] = p_z_positions[0] * 2.0 - sample_z[1];
	sample_z.write[sample_depth - 1] = p_z_positions[p_grid_depth] * 2.0 - sample_z[sample_depth - 2];

	Vector<HeightMapContourSegment> segments;
	HashMap<uint64_t, Vector<int>> adjacency;
	auto add_segment = [&](uint64_t p_a, uint64_t p_b) {
		const int segment = segments.size();
		HeightMapContourSegment value;
		value.points[0] = p_a;
		value.points[1] = p_b;
		segments.push_back(value);
		adjacency[p_a].push_back(segment);
		adjacency[p_b].push_back(segment);
	};

	for (int z = 0; z < sample_depth - 1; z++) {
		for (int x = 0; x < sample_width - 1; x++) {
			const int square = (mask[z * sample_width + x] ? 1 : 0) |
					(mask[z * sample_width + x + 1] ? 2 : 0) |
					(mask[(z + 1) * sample_width + x + 1] ? 4 : 0) |
					(mask[(z + 1) * sample_width + x] ? 8 : 0);
			const uint64_t bottom = _height_map_contour_point_key(x * 2 + 1, z * 2);
			const uint64_t right = _height_map_contour_point_key(x * 2 + 2, z * 2 + 1);
			const uint64_t top = _height_map_contour_point_key(x * 2 + 1, z * 2 + 2);
			const uint64_t left = _height_map_contour_point_key(x * 2, z * 2 + 1);
			switch (square) {
				case 1:
				case 14:
					add_segment(left, bottom);
					break;
				case 2:
				case 13:
					add_segment(bottom, right);
					break;
				case 3:
				case 12:
					add_segment(left, right);
					break;
				case 4:
				case 11:
					add_segment(right, top);
					break;
				case 5:
					add_segment(left, bottom);
					add_segment(right, top);
					break;
				case 6:
				case 9:
					add_segment(bottom, top);
					break;
				case 7:
				case 8:
					add_segment(left, top);
					break;
				case 10:
					add_segment(bottom, right);
					add_segment(top, left);
					break;
				default:
					break;
			}
		}
	}

	Vector<Vector<Vector2>> contours;
	for (int segment_i = 0; segment_i < segments.size(); segment_i++) {
		if (segments[segment_i].used) {
			continue;
		}
		Vector<uint64_t> contour_keys;
		const uint64_t start = segments[segment_i].points[0];
		uint64_t current = start;
		int current_segment = segment_i;
		bool closed = false;
		while (current_segment >= 0) {
			segments.write[current_segment].used = true;
			contour_keys.push_back(current);
			const HeightMapContourSegment &segment = segments[current_segment];
			const uint64_t next = segment.points[0] == current ? segment.points[1] : segment.points[0];
			if (next == start) {
				closed = true;
				break;
			}
			HashMap<uint64_t, Vector<int>>::ConstIterator connected = adjacency.find(next);
			if (!connected) {
				break;
			}
			int next_segment = -1;
			for (int candidate : connected->value) {
				if (!segments[candidate].used) {
					next_segment = candidate;
					break;
				}
			}
			current = next;
			current_segment = next_segment;
		}
		if (!closed || contour_keys.size() < 3) {
			continue;
		}

		Vector<Vector2> contour;
		for (uint64_t key : contour_keys) {
			contour.push_back(_height_map_contour_point_position(key, sample_x, sample_z));
		}
		Vector<Vector2> simplified;
		for (int point = 0; point < contour.size(); point++) {
			const Vector2 previous = contour[(point + contour.size() - 1) % contour.size()];
			const Vector2 current_point = contour[point];
			const Vector2 next = contour[(point + 1) % contour.size()];
			if (!Math::is_zero_approx((current_point - previous).cross(next - current_point))) {
				simplified.push_back(current_point);
			}
		}
		if (simplified.size() < 3) {
			continue;
		}
		contours.push_back(simplified);
	}

	manifold::Polygons polygons;
	for (int contour_i = 0; contour_i < contours.size(); contour_i++) {
		Vector<Vector2> &contour = contours.write[contour_i];
		int nesting_depth = 0;
		for (int other_i = 0; other_i < contours.size(); other_i++) {
			if (other_i != contour_i && Geometry2D::is_point_in_polygon(contour[0], contours[other_i])) {
				nesting_depth++;
			}
		}

		real_t signed_area = 0.0;
		for (int point = 0; point < contour.size(); point++) {
			const Vector2 &a = contour[point];
			const Vector2 &b = contour[(point + 1) % contour.size()];
			signed_area += a.cross(b);
		}
		// Manifold uses positive-area loops as solid outlines and negative-area
		// loops as holes. Alternating the winding also supports islands inside
		// holes without turning every traced loop into an independent solid.
		const bool should_be_positive = (nesting_depth & 1) == 0;
		if ((signed_area > 0.0) != should_be_positive) {
			contour.reverse();
		}
		manifold::SimplePolygon polygon;
		polygon.reserve(contour.size());
		for (const Vector2 &point : contour) {
			polygon.push_back(manifold::vec2(point.x, point.y));
		}
		polygons.push_back(std::move(polygon));
	}
	return polygons;
}

static HashMap<Vector2, Vector2> _height_map_build_slope_targets(const manifold::Polygons &p_polygons, real_t p_width, const Vector2 &p_half_size) {
	HashMap<Vector2, Vector2> targets;
	const real_t border_epsilon = MAX(MIN(p_half_size.x, p_half_size.y) * real_t(0.000001), real_t(CMP_EPSILON));
	for (const manifold::SimplePolygon &polygon : p_polygons) {
		if (polygon.size() < 3) {
			continue;
		}
		const int point_count = int(polygon.size());
		Vector<Vector2> source_polygon;
		source_polygon.resize(point_count);
		real_t signed_area = 0.0;
		for (int point_i = 0; point_i < point_count; point_i++) {
			const manifold::vec2 &source = polygon[point_i];
			const manifold::vec2 &source_next = polygon[(point_i + 1) % point_count];
			source_polygon.write[point_i] = Vector2(source[0], source[1]);
			signed_area += real_t(source[0] * source_next[1] - source[1] * source_next[0]);
		}
		auto is_valid_offset = [&](real_t p_test_width) {
			const real_t delta = signed_area > 0.0 ? -p_test_width : p_test_width;
			const Vector<Vector<Vector2>> offset = Geometry2D::offset_polygon(source_polygon, delta, Geometry2D::JOIN_MITER);
			return offset.size() == 1 && offset[0].size() >= 3;
		};
		real_t effective_width = p_width;
		if (!is_valid_offset(effective_width)) {
			real_t valid_width = 0.0;
			real_t invalid_width = effective_width;
			for (int iteration = 0; iteration < 12; iteration++) {
				const real_t candidate = (valid_width + invalid_width) * 0.5;
				if (is_valid_offset(candidate)) {
					valid_width = candidate;
				} else {
					invalid_width = candidate;
				}
			}
			effective_width = valid_width * real_t(0.99);
		}
		for (int point_i = 0; point_i < point_count; point_i++) {
			const manifold::vec2 &source_previous = polygon[(point_i + point_count - 1) % point_count];
			const manifold::vec2 &source_current = polygon[point_i];
			const manifold::vec2 &source_next = polygon[(point_i + 1) % point_count];
			const Vector2 previous(source_previous[0], source_previous[1]);
			const Vector2 current(source_current[0], source_current[1]);
			const Vector2 next(source_next[0], source_next[1]);
			const bool on_chunk_border = Math::abs(Math::abs(current.x) - p_half_size.x) <= border_epsilon || Math::abs(Math::abs(current.y) - p_half_size.y) <= border_epsilon;
			if (on_chunk_border) {
				targets.insert(current, current);
				continue;
			}

			const Vector2 incoming = current - previous;
			const Vector2 outgoing = next - current;
			const real_t incoming_length = incoming.length();
			const real_t outgoing_length = outgoing.length();
			if (incoming_length <= CMP_EPSILON || outgoing_length <= CMP_EPSILON) {
				targets.insert(current, current);
				continue;
			}
			const Vector2 incoming_normal(-incoming.y / incoming_length, incoming.x / incoming_length);
			const Vector2 outgoing_normal(-outgoing.y / outgoing_length, outgoing.x / outgoing_length);
			const Vector2 normal_sum = incoming_normal + outgoing_normal;
			if (normal_sum.length_squared() <= CMP_EPSILON * CMP_EPSILON) {
				targets.insert(current, current);
				continue;
			}
			const Vector2 bisector = normal_sum.normalized();
			const real_t denominator = bisector.dot(incoming_normal);
			if (denominator <= CMP_EPSILON) {
				targets.insert(current, current);
				continue;
			}
			const real_t miter_length = MIN(effective_width / denominator, effective_width * real_t(2.0));
			targets.insert(current, current + bisector * miter_length);
		}
	}
	return targets;
}

} // namespace

CSGBrush *CSGHeightMap3D::_build_brush() {
	if (generation_mode == GENERATION_CONTOUR_LAYERS && height_map.is_valid()) {
		CSGBrush *contour_brush = _build_contour_layers_brush();
		if (!contour_brush->faces.is_empty()) {
			return contour_brush;
		}
		memdelete(contour_brush);
		WARN_PRINT_ONCE("CSGHeightMap3D contour generation failed; falling back to the cell grid generator.");
	}
	return _build_cell_grid_brush();
}

Rect2i CSGHeightMap3D::_get_effective_region(int p_image_width, int p_image_height) const {
	const Rect2i image_rect(0, 0, p_image_width, p_image_height);
	return region_enabled ? image_rect.intersection(region_rect) : image_rect;
}

Vector<real_t> CSGHeightMap3D::_build_layer_heights(real_t p_minimum_top, real_t p_maximum_top) const {
	Vector<real_t> layer_heights;
	if (height_steps <= 1) {
		return layer_heights;
	}
	layer_heights.resize(height_steps);
	layer_heights.write[0] = p_minimum_top;
	if (height_steps == 2) {
		layer_heights.write[1] = p_maximum_top;
		return layer_heights;
	}

	real_t total_weight = 0.0;
	for (int layer = 1; layer < height_steps; layer++) {
		Ref<CSGHeightMapLayer> settings;
		if (layer < layer_settings.size()) {
			settings = layer_settings[layer];
		}
		total_weight += settings.is_valid() ? settings->get_height_weight() : real_t(1.0);
	}
	total_weight = MAX(total_weight, real_t(CMP_EPSILON));
	real_t height = p_minimum_top;
	for (int layer = 1; layer < height_steps; layer++) {
		Ref<CSGHeightMapLayer> settings;
		if (layer < layer_settings.size()) {
			settings = layer_settings[layer];
		}
		const real_t weight = settings.is_valid() ? settings->get_height_weight() : real_t(1.0);
		height += (p_maximum_top - p_minimum_top) * weight / total_weight;
		layer_heights.write[layer] = layer == height_steps - 1 ? p_maximum_top : height;
	}
	return layer_heights;
}

real_t CSGHeightMap3D::_get_layer_slope_width(int p_layer) const {
	if (p_layer >= 0 && p_layer < layer_settings.size()) {
		Ref<CSGHeightMapLayer> settings = layer_settings[p_layer];
		if (settings.is_valid() && settings->get_slope_width() >= 0.0) {
			return settings->get_slope_width();
		}
	}
	return slope_width;
}

CSGBrush *CSGHeightMap3D::_build_cell_grid_brush() {
	if (height_map.is_null()) {
		return memnew(CSGBrush);
	}

	Ref<Image> image = height_map->get_image();
	if (image.is_null() || image->is_empty()) {
		return memnew(CSGBrush);
	}

	const int image_width = image->get_width();
	const int image_height = image->get_height();
	if (image_width <= 0 || image_height <= 0) {
		return memnew(CSGBrush);
	}
	const Rect2i effective_region = _get_effective_region(image_width, image_height);
	if (!effective_region.has_area()) {
		return memnew(CSGBrush);
	}
	const int region_width = effective_region.size.x;
	const int region_height = effective_region.size.y;

	const int grid_width = (region_width + sampling_step - 1) / sampling_step;
	const int grid_depth = (region_height + sampling_step - 1) / sampling_step;
	Vector<real_t> x_positions;
	Vector<real_t> z_positions;
	x_positions.resize(grid_width + 1);
	z_positions.resize(grid_depth + 1);
	for (int x = 0; x <= grid_width; x++) {
		const int pixel_x = MIN(x * sampling_step, region_width);
		x_positions.write[x] = -size.x * 0.5 + size.x * real_t(pixel_x) / region_width;
	}
	for (int z = 0; z <= grid_depth; z++) {
		const int pixel_z = MIN(z * sampling_step, region_height);
		z_positions.write[z] = -size.z * 0.5 + size.z * real_t(pixel_z) / region_height;
	}

	auto sample_color = [this](const Color &p_color) -> real_t {
		switch (height_channel) {
			case HEIGHT_CHANNEL_RED:
				return p_color.r;
			case HEIGHT_CHANNEL_GREEN:
				return p_color.g;
			case HEIGHT_CHANNEL_BLUE:
				return p_color.b;
			case HEIGHT_CHANNEL_ALPHA:
				return p_color.a;
			case HEIGHT_CHANNEL_LUMINANCE:
			default:
				return p_color.get_luminance();
		}
	};

	const real_t bottom_y = -size.y * 0.5;
	const real_t minimum_top_y = bottom_y + MIN(base_thickness, size.y);
	const real_t maximum_top_y = size.y * 0.5;
	Vector<real_t> heights;
	Vector<int> cell_layer_ids;
	const Vector<real_t> configured_heights = _build_layer_heights(minimum_top_y, maximum_top_y);
	heights.resize(grid_width * grid_depth);
	cell_layer_ids.resize(grid_width * grid_depth);
	for (int z = 0; z < grid_depth; z++) {
		for (int x = 0; x < grid_width; x++) {
			const int sample_x = effective_region.position.x + MIN(x * sampling_step + sampling_step / 2, region_width - 1);
			const int sample_z = effective_region.position.y + MIN(z * sampling_step + sampling_step / 2, region_height - 1);
			real_t value = CLAMP(sample_color(image->get_pixel(sample_x, sample_z)), real_t(0.0), real_t(1.0));
			if (invert_height) {
				value = 1.0 - value;
			}
			int layer_id = 0;
			if (height_steps > 1) {
				layer_id = CLAMP(int(Math::round(value * (height_steps - 1))), 0, height_steps - 1);
				heights.write[z * grid_width + x] = configured_heights[layer_id];
			} else {
				heights.write[z * grid_width + x] = Math::lerp(minimum_top_y, maximum_top_y, value);
			}
			cell_layer_ids.write[z * grid_width + x] = layer_id;
		}
	}

	Vector<Vector3> vertices;
	Vector<Vector2> uvs;
	Vector<bool> smooth;
	Vector<Ref<Material>> materials;
	Vector<int> surface_ids;
	Vector<uint32_t> source_ids;
	Vector<StringName> semantics;

	const Ref<Material> cliff_material = side_material.is_valid() ? side_material : material;
	const Ref<Material> base_material = bottom_material.is_valid() ? bottom_material : cliff_material;

	auto append_triangle = [&](const Vector3 &p_a, const Vector3 &p_b, const Vector3 &p_c, const Vector2 &p_uv_a, const Vector2 &p_uv_b, const Vector2 &p_uv_c, SurfaceType p_surface, uint32_t p_source_id, const Ref<Material> &p_material, const StringName &p_semantic) {
		vertices.push_back(p_a);
		vertices.push_back(p_b);
		vertices.push_back(p_c);
		uvs.push_back(p_uv_a);
		uvs.push_back(p_uv_b);
		uvs.push_back(p_uv_c);
		smooth.push_back(false);
		materials.push_back(p_material);
		surface_ids.push_back(p_surface);
		source_ids.push_back(p_source_id);
		semantics.push_back(p_semantic);
	};
	auto append_quad = [&](const Vector3 &p_a, const Vector3 &p_b, const Vector3 &p_c, const Vector3 &p_d, const Vector2 &p_uv_a, const Vector2 &p_uv_b, const Vector2 &p_uv_c, const Vector2 &p_uv_d, SurfaceType p_surface, uint32_t p_source_id, const Ref<Material> &p_material, const StringName &p_semantic) {
		append_triangle(p_a, p_b, p_c, p_uv_a, p_uv_b, p_uv_c, p_surface, p_source_id, p_material, p_semantic);
		append_triangle(p_c, p_d, p_a, p_uv_c, p_uv_d, p_uv_a, p_surface, p_source_id, p_material, p_semantic);
	};
	auto height_equal = [](real_t p_a, real_t p_b) {
		return Math::is_equal_approx(p_a, p_b);
	};
	auto height_uv = [&](real_t p_height) {
		return (p_height - bottom_y) / size.y;
	};
	auto get_vertex_levels = [&](int p_grid_x, int p_grid_z, real_t p_low, real_t p_high) {
		Vector<real_t> candidates;
		candidates.push_back(p_low);
		candidates.push_back(p_high);
		for (int offset_z = -1; offset_z <= 0; offset_z++) {
			const int cell_z = p_grid_z + offset_z;
			if (cell_z < 0 || cell_z >= grid_depth) {
				continue;
			}
			for (int offset_x = -1; offset_x <= 0; offset_x++) {
				const int cell_x = p_grid_x + offset_x;
				if (cell_x < 0 || cell_x >= grid_width) {
					continue;
				}
				const real_t candidate = heights[cell_z * grid_width + cell_x];
				if (candidate > p_low && candidate < p_high) {
					candidates.push_back(candidate);
				}
			}
		}
		candidates.sort();
		Vector<real_t> levels;
		for (real_t candidate : candidates) {
			if (levels.is_empty() || !height_equal(levels[levels.size() - 1], candidate)) {
				levels.push_back(candidate);
			}
		}
		return levels;
	};

	// Keep the top grid conforming. Merging coplanar cells here would create
	// T-junctions where a long top edge meets shorter cliff edges, which is not
	// valid input for Manifold.
	for (int z = 0; z < grid_depth; z++) {
		for (int x = 0; x < grid_width; x++) {
			const int cell = z * grid_width + x;
			const real_t cell_height = heights[cell];
			const real_t x0 = x_positions[x];
			const real_t x1 = x_positions[x + 1];
			const real_t z0 = z_positions[z];
			const real_t z1 = z_positions[z + 1];
			const Vector2 uv0(real_t(x * sampling_step) / region_width, real_t(z * sampling_step) / region_height);
			const Vector2 uv1(real_t(MIN((x + 1) * sampling_step, region_width)) / region_width, real_t(MIN((z + 1) * sampling_step, region_height)) / region_height);
			append_quad(
					Vector3(x0, cell_height, z0), Vector3(x1, cell_height, z0), Vector3(x1, cell_height, z1), Vector3(x0, cell_height, z1),
					Vector2(uv0.x, uv0.y), Vector2(uv1.x, uv0.y), Vector2(uv1.x, uv1.y), Vector2(uv0.x, uv1.y),
					SURFACE_TOP, cell, material, SNAME("HEIGHTMAP_TOP"));
		}
	}

	auto append_wall_fan = [&](const Vector3 &p_endpoint_0, const Vector3 &p_endpoint_1, real_t p_u0, real_t p_u1, const Vector<real_t> &p_levels_0, const Vector<real_t> &p_levels_1, bool p_reverse, SurfaceType p_surface, uint32_t p_source_id, const StringName &p_semantic) {
		Vector<Vector3> boundary;
		Vector<Vector2> boundary_uvs;
		boundary.push_back(Vector3(p_endpoint_0.x, p_levels_0[0], p_endpoint_0.z));
		boundary_uvs.push_back(Vector2(p_u0, height_uv(p_levels_0[0])));
		boundary.push_back(Vector3(p_endpoint_1.x, p_levels_1[0], p_endpoint_1.z));
		boundary_uvs.push_back(Vector2(p_u1, height_uv(p_levels_1[0])));
		for (int level = 1; level < p_levels_1.size(); level++) {
			boundary.push_back(Vector3(p_endpoint_1.x, p_levels_1[level], p_endpoint_1.z));
			boundary_uvs.push_back(Vector2(p_u1, height_uv(p_levels_1[level])));
		}
		boundary.push_back(Vector3(p_endpoint_0.x, p_levels_0[p_levels_0.size() - 1], p_endpoint_0.z));
		boundary_uvs.push_back(Vector2(p_u0, height_uv(p_levels_0[p_levels_0.size() - 1])));
		for (int level = p_levels_0.size() - 2; level > 0; level--) {
			boundary.push_back(Vector3(p_endpoint_0.x, p_levels_0[level], p_endpoint_0.z));
			boundary_uvs.push_back(Vector2(p_u0, height_uv(p_levels_0[level])));
		}
		if (p_reverse) {
			boundary.reverse();
			boundary_uvs.reverse();
		}
		if (boundary.size() == 4) {
			append_quad(boundary[0], boundary[1], boundary[2], boundary[3], boundary_uvs[0], boundary_uvs[1], boundary_uvs[2], boundary_uvs[3], p_surface, p_source_id, cliff_material, p_semantic);
			return;
		}
		const real_t center_height = (p_levels_0[0] + p_levels_0[p_levels_0.size() - 1]) * 0.5;
		const Vector3 center((p_endpoint_0.x + p_endpoint_1.x) * 0.5, center_height, (p_endpoint_0.z + p_endpoint_1.z) * 0.5);
		const Vector2 center_uv((p_u0 + p_u1) * 0.5, height_uv(center_height));
		for (int boundary_vertex = 0; boundary_vertex < boundary.size(); boundary_vertex++) {
			const int next_vertex = (boundary_vertex + 1) % boundary.size();
			append_triangle(center, boundary[boundary_vertex], boundary[next_vertex], center_uv, boundary_uvs[boundary_vertex], boundary_uvs[next_vertex], p_surface, p_source_id, cliff_material, p_semantic);
		}
	};
	auto append_x_wall = [&](int p_boundary, int p_z0, int p_z1, real_t p_low, real_t p_high, bool p_outward_positive, SurfaceType p_surface, uint32_t p_source_id) {
		const real_t x = x_positions[p_boundary];
		const real_t z0 = z_positions[p_z0];
		const real_t z1 = z_positions[p_z1];
		const real_t u0 = real_t(MIN(p_z0 * sampling_step, region_height)) / region_height;
		const real_t u1 = real_t(MIN(p_z1 * sampling_step, region_height)) / region_height;
		const StringName semantic = p_surface == SURFACE_BORDER ? SNAME("HEIGHTMAP_BORDER") : SNAME("HEIGHTMAP_CLIFF");
		const Vector<real_t> levels_0 = get_vertex_levels(p_boundary, p_z0, p_low, p_high);
		const Vector<real_t> levels_1 = get_vertex_levels(p_boundary, p_z1, p_low, p_high);
		append_wall_fan(Vector3(x, 0, z0), Vector3(x, 0, z1), u0, u1, levels_0, levels_1, !p_outward_positive, p_surface, p_source_id, semantic);
	};
	auto append_z_wall = [&](int p_boundary, int p_x0, int p_x1, real_t p_low, real_t p_high, bool p_outward_positive, SurfaceType p_surface, uint32_t p_source_id) {
		const real_t z = z_positions[p_boundary];
		const real_t x0 = x_positions[p_x0];
		const real_t x1 = x_positions[p_x1];
		const real_t u0 = real_t(MIN(p_x0 * sampling_step, region_width)) / region_width;
		const real_t u1 = real_t(MIN(p_x1 * sampling_step, region_width)) / region_width;
		const StringName semantic = p_surface == SURFACE_BORDER ? SNAME("HEIGHTMAP_BORDER") : SNAME("HEIGHTMAP_CLIFF");
		const Vector<real_t> levels_0 = get_vertex_levels(p_x1, p_boundary, p_low, p_high);
		const Vector<real_t> levels_1 = get_vertex_levels(p_x0, p_boundary, p_low, p_high);
		append_wall_fan(Vector3(x1, 0, z), Vector3(x0, 0, z), u1, u0, levels_0, levels_1, !p_outward_positive, p_surface, p_source_id, semantic);
	};

	// Only emit walls where neighboring cells differ. Each wall retains the
	// cell-sized horizontal edge required to match the top grid exactly.
	for (int boundary = 1; boundary < grid_width; boundary++) {
		for (int z = 0; z < grid_depth; z++) {
			const real_t left = heights[z * grid_width + boundary - 1];
			const real_t right = heights[z * grid_width + boundary];
			if (height_equal(left, right)) {
				continue;
			}
			const bool high_on_left = left > right;
			const real_t low = MIN(left, right);
			const real_t high = MAX(left, right);
			const uint32_t source_id = z * grid_width + (high_on_left ? boundary - 1 : boundary);
			append_x_wall(boundary, z, z + 1, low, high, high_on_left, SURFACE_CLIFF, source_id);
		}
	}
	for (int boundary = 1; boundary < grid_depth; boundary++) {
		for (int x = 0; x < grid_width; x++) {
			const real_t negative = heights[(boundary - 1) * grid_width + x];
			const real_t positive = heights[boundary * grid_width + x];
			if (height_equal(negative, positive)) {
				continue;
			}
			const bool high_on_negative = negative > positive;
			const real_t low = MIN(negative, positive);
			const real_t high = MAX(negative, positive);
			const uint32_t source_id = (high_on_negative ? boundary - 1 : boundary) * grid_width + x;
			append_z_wall(boundary, x, x + 1, low, high, high_on_negative, SURFACE_CLIFF, source_id);
		}
	}

	// Perimeter walls use the same subdivision as the top grid.
	for (int boundary_side = 0; boundary_side < 2; boundary_side++) {
		const int x = boundary_side == 0 ? 0 : grid_width - 1;
		for (int z = 0; z < grid_depth; z++) {
			const real_t high = heights[z * grid_width + x];
			append_x_wall(boundary_side == 0 ? 0 : grid_width, z, z + 1, bottom_y, high, boundary_side != 0, SURFACE_BORDER, z * grid_width + x);
		}
	}
	for (int boundary_side = 0; boundary_side < 2; boundary_side++) {
		const int z = boundary_side == 0 ? 0 : grid_depth - 1;
		for (int x = 0; x < grid_width; x++) {
			const real_t high = heights[z * grid_width + x];
			append_z_wall(boundary_side == 0 ? 0 : grid_depth, x, x + 1, bottom_y, high, boundary_side != 0, SURFACE_BORDER, z * grid_width + x);
		}
	}

	// Triangulate the bottom as a compact fan whose boundary vertices exactly
	// match every perimeter cell. This avoids both a full bottom grid and
	// perimeter T-junctions.
	Vector<Vector3> bottom_boundary;
	Vector<Vector2> bottom_uvs;
	for (int z = 0; z < grid_depth; z++) {
		bottom_boundary.push_back(Vector3(x_positions[0], bottom_y, z_positions[z]));
		bottom_uvs.push_back(Vector2(0, real_t(MIN(z * sampling_step, region_height)) / region_height));
	}
	for (int x = 0; x < grid_width; x++) {
		bottom_boundary.push_back(Vector3(x_positions[x], bottom_y, z_positions[grid_depth]));
		bottom_uvs.push_back(Vector2(real_t(MIN(x * sampling_step, region_width)) / region_width, 1));
	}
	for (int z = grid_depth; z > 0; z--) {
		bottom_boundary.push_back(Vector3(x_positions[grid_width], bottom_y, z_positions[z]));
		bottom_uvs.push_back(Vector2(1, real_t(MIN(z * sampling_step, region_height)) / region_height));
	}
	for (int x = grid_width; x > 0; x--) {
		bottom_boundary.push_back(Vector3(x_positions[x], bottom_y, z_positions[0]));
		bottom_uvs.push_back(Vector2(real_t(MIN(x * sampling_step, region_width)) / region_width, 0));
	}
	const Vector3 bottom_center(0, bottom_y, 0);
	const Vector2 bottom_center_uv(0.5, 0.5);
	for (int boundary_vertex = 0; boundary_vertex < bottom_boundary.size(); boundary_vertex++) {
		const int next_vertex = (boundary_vertex + 1) % bottom_boundary.size();
		append_triangle(bottom_center, bottom_boundary[boundary_vertex], bottom_boundary[next_vertex], bottom_center_uv, bottom_uvs[boundary_vertex], bottom_uvs[next_vertex], SURFACE_BOTTOM, 0, base_material, SNAME("HEIGHTMAP_BOTTOM"));
	}

	CSGBrush *height_map_brush = _create_brush_from_arrays(vertices, uvs, smooth, materials, surface_ids);
	for (int face = 0; face < height_map_brush->faces.size(); face++) {
		height_map_brush->faces.write[face].metadata.source_face_id = source_ids[face];
		if (surface_ids[face] != SURFACE_BOTTOM && source_ids[face] < uint32_t(cell_layer_ids.size())) {
			height_map_brush->faces.write[face].metadata.layer_id = cell_layer_ids[source_ids[face]];
		}
		height_map_brush->faces.write[face].metadata.semantic = semantics[face];
	}
	return height_map_brush;
}

CSGBrush *CSGHeightMap3D::_build_contour_layers_brush() {
	if (height_map.is_null()) {
		return memnew(CSGBrush);
	}
	Ref<Image> image = height_map->get_image();
	if (image.is_null() || image->is_empty()) {
		return memnew(CSGBrush);
	}

	const int image_width = image->get_width();
	const int image_height = image->get_height();
	if (image_width <= 0 || image_height <= 0) {
		return memnew(CSGBrush);
	}
	const Rect2i effective_region = _get_effective_region(image_width, image_height);
	if (!effective_region.has_area()) {
		return memnew(CSGBrush);
	}
	const int region_width = effective_region.size.x;
	const int region_height = effective_region.size.y;
	const int grid_width = (region_width + sampling_step - 1) / sampling_step;
	const int grid_depth = (region_height + sampling_step - 1) / sampling_step;
	Vector<real_t> x_positions;
	Vector<real_t> z_positions;
	x_positions.resize(grid_width + 1);
	z_positions.resize(grid_depth + 1);
	for (int x = 0; x <= grid_width; x++) {
		const int pixel_x = MIN(x * sampling_step, region_width);
		x_positions.write[x] = -size.x * 0.5 + size.x * real_t(pixel_x) / region_width;
	}
	for (int z = 0; z <= grid_depth; z++) {
		const int pixel_z = MIN(z * sampling_step, region_height);
		z_positions.write[z] = -size.z * 0.5 + size.z * real_t(pixel_z) / region_height;
	}

	auto sample_color = [this](const Color &p_color) -> real_t {
		switch (height_channel) {
			case HEIGHT_CHANNEL_RED:
				return p_color.r;
			case HEIGHT_CHANNEL_GREEN:
				return p_color.g;
			case HEIGHT_CHANNEL_BLUE:
				return p_color.b;
			case HEIGHT_CHANNEL_ALPHA:
				return p_color.a;
			case HEIGHT_CHANNEL_LUMINANCE:
			default:
				return p_color.get_luminance();
		}
	};

	const real_t bottom_y = -size.y * 0.5;
	const real_t minimum_top_y = bottom_y + MIN(base_thickness, size.y);
	const real_t maximum_top_y = size.y * 0.5;
	Vector<real_t> heights;
	Vector<real_t> levels;
	const Vector<real_t> configured_heights = _build_layer_heights(minimum_top_y, maximum_top_y);
	heights.resize(grid_width * grid_depth);
	for (int z = 0; z < grid_depth; z++) {
		for (int x = 0; x < grid_width; x++) {
			const int sample_x = effective_region.position.x + MIN(x * sampling_step + sampling_step / 2, region_width - 1);
			const int sample_z = effective_region.position.y + MIN(z * sampling_step + sampling_step / 2, region_height - 1);
			real_t value = CLAMP(sample_color(image->get_pixel(sample_x, sample_z)), real_t(0.0), real_t(1.0));
			if (invert_height) {
				value = 1.0 - value;
			}
			real_t height;
			if (height_steps > 1) {
				const int layer_id = CLAMP(int(Math::round(value * (height_steps - 1))), 0, height_steps - 1);
				height = configured_heights[layer_id];
			} else {
				height = Math::lerp(minimum_top_y, maximum_top_y, value);
			}
			heights.write[z * grid_width + x] = height;
			levels.push_back(height);
		}
	}
	levels.sort();
	Vector<real_t> unique_levels;
	for (real_t level : levels) {
		if (unique_levels.is_empty() || !Math::is_equal_approx(unique_levels[unique_levels.size() - 1], level)) {
			unique_levels.push_back(level);
		}
	}
	Vector<int> unique_layer_ids;
	unique_layer_ids.resize(unique_levels.size());
	for (int level_i = 0; level_i < unique_levels.size(); level_i++) {
		int layer_id = 0;
		if (height_steps > 1) {
			real_t closest_distance = Math::abs(unique_levels[level_i] - configured_heights[0]);
			for (int candidate = 1; candidate < configured_heights.size(); candidate++) {
				const real_t distance = Math::abs(unique_levels[level_i] - configured_heights[candidate]);
				if (distance < closest_distance) {
					closest_distance = distance;
					layer_id = candidate;
				}
			}
		}
		unique_layer_ids.write[level_i] = layer_id;
	}

	std::vector<manifold::Manifold> layer_manifolds;
	HashMap<int, int> original_layer_ids;
	layer_manifolds.reserve(unique_levels.size());
	const real_t join_overlap_target = MAX(MIN(size.x / grid_width, size.z / grid_depth) * real_t(0.00001), real_t(CMP_EPSILON));
	for (int level_i = unique_levels.size() - 1; level_i >= 0; level_i--) {
		const real_t level = unique_levels[level_i];
		const int layer_id = unique_layer_ids[level_i];
		const real_t lower_level = level_i == 0 ? bottom_y : unique_levels[level_i - 1];
		const real_t level_interval = level - lower_level;
		const real_t join_overlap = level_i == 0 ? 0.0 : MIN(join_overlap_target, level_interval * real_t(0.25));
		const real_t slab_bottom = lower_level - join_overlap;
		const real_t extrusion_height = level - slab_bottom;
		if (extrusion_height <= CMP_EPSILON) {
			continue;
		}
		manifold::Polygons polygons = _height_map_trace_contours(heights, grid_width, grid_depth, x_positions, z_positions, level);
		if (polygons.empty()) {
			continue;
		}
		// Build only this level interval, with a tiny overlap at its lower join,
		// instead of extruding every cumulative mask from the common bottom. The
		// old overlapping volumes produced dense coplanar subdivisions and harmed
		// beveling.
		manifold::Manifold layer = manifold::Manifold::Extrude(polygons, extrusion_height);
		const real_t layer_slope_width = _get_layer_slope_width(layer_id);
		if (layer_slope_width > 0.0) {
			const HashMap<Vector2, Vector2> slope_targets = _height_map_build_slope_targets(polygons, layer_slope_width, Vector2(size.x, size.z) * 0.5);
			layer = layer.Warp([&](manifold::vec3 &p_point) {
				if (!Math::is_equal_approx(p_point[2], double(extrusion_height))) {
					return;
				}
				const Vector2 source(p_point[0], p_point[1]);
				HashMap<Vector2, Vector2>::ConstIterator target = slope_targets.find(source);
				if (target) {
					p_point[0] = target->value.x;
					p_point[1] = target->value.y;
				}
			});
		}
		layer = layer.Translate(manifold::vec3(0.0, 0.0, slab_bottom - bottom_y));
		if (layer.Status() != manifold::Manifold::Error::NoError || layer.IsEmpty()) {
			return memnew(CSGBrush);
		}
		layer = layer.AsOriginal();
		original_layer_ids.insert(layer.OriginalID(), layer_id);
		layer_manifolds.push_back(std::move(layer));
	}
	if (layer_manifolds.empty()) {
		return memnew(CSGBrush);
	}

	manifold::Manifold result = layer_manifolds.size() == 1 ? std::move(layer_manifolds[0]) : manifold::Manifold::BatchBoolean(layer_manifolds, manifold::OpType::Add);
	if (result.Status() != manifold::Manifold::Error::NoError || result.IsEmpty()) {
		return memnew(CSGBrush);
	}
	result = result.Simplify(join_overlap_target);
	if (result.Status() != manifold::Manifold::Error::NoError || result.IsEmpty()) {
		return memnew(CSGBrush);
	}
	const manifold::MeshGL64 mesh = result.GetMeshGL64();
	if (mesh.triVerts.empty()) {
		return memnew(CSGBrush);
	}

	Vector<Vector3> vertices;
	Vector<Vector2> uvs;
	Vector<bool> smooth;
	Vector<Ref<Material>> materials;
	Vector<int> surface_ids;
	Vector<int> face_layer_ids;
	Vector<StringName> semantics;
	const int face_count = mesh.triVerts.size() / 3;
	vertices.resize(face_count * 3);
	uvs.resize(face_count * 3);
	smooth.resize(face_count);
	materials.resize(face_count);
	surface_ids.resize(face_count);
	face_layer_ids.resize(face_count);
	semantics.resize(face_count);

	const Ref<Material> cliff_material = side_material.is_valid() ? side_material : material;
	const Ref<Material> base_material = bottom_material.is_valid() ? bottom_material : cliff_material;
	const real_t border_epsilon = MAX(MIN(size.x / grid_width, size.z / grid_depth) * real_t(0.0001), real_t(CMP_EPSILON));
	auto to_world = [&](size_t p_vertex) {
		const manifold::vec3 point = mesh.GetVertPos(p_vertex);
		return Vector3(point[0], bottom_y + point[2], point[1]);
	};
	auto planar_uv = [&](const Vector3 &p_point) {
		return Vector2(p_point.x / size.x + 0.5, p_point.z / size.z + 0.5);
	};

	size_t run_i = 0;
	for (int face = 0; face < face_count; face++) {
		while (run_i + 1 < mesh.runIndex.size() && size_t(face * 3) >= mesh.runIndex[run_i + 1]) {
			run_i++;
		}
		int face_layer_id = 0;
		if (run_i < mesh.runOriginalID.size()) {
			HashMap<int, int>::ConstIterator layer_id = original_layer_ids.find(mesh.runOriginalID[run_i]);
			if (layer_id) {
				face_layer_id = layer_id->value;
			}
		}
		Vector3 manifold_vertices[3];
		for (int corner = 0; corner < 3; corner++) {
			manifold_vertices[corner] = to_world(mesh.triVerts[face * 3 + corner]);
		}
		// Mapping Manifold's (X, Y, extrusion-Z) to Godot's (X, Y-up, Z)
		// swaps two axes and reverses winding. The mapped triangle is therefore
		// already in CSGBrush's inward-normal winding; negate it only for surface
		// classification below.
		const Vector3 normal = -(manifold_vertices[1] - manifold_vertices[0]).cross(manifold_vertices[2] - manifold_vertices[0]).normalized();
		const bool horizontal = Math::is_equal_approx(manifold_vertices[0].y, manifold_vertices[1].y) && Math::is_equal_approx(manifold_vertices[0].y, manifold_vertices[2].y);
		SurfaceType surface = SURFACE_CLIFF;
		Ref<Material> face_material = cliff_material;
		StringName semantic = SNAME("HEIGHTMAP_CLIFF");
		if (horizontal && normal.y > 0.0) {
			surface = SURFACE_TOP;
			face_material = material;
			semantic = SNAME("HEIGHTMAP_TOP");
		} else if (horizontal && normal.y < 0.0) {
			surface = SURFACE_BOTTOM;
			face_material = base_material;
			semantic = SNAME("HEIGHTMAP_BOTTOM");
		} else {
			bool border = true;
			for (int corner = 0; corner < 3; corner++) {
				const Vector3 &point = manifold_vertices[corner];
				const real_t x_border_distance = Math::abs(Math::abs(point.x) - size.x * real_t(0.5));
				const real_t z_border_distance = Math::abs(Math::abs(point.z) - size.z * real_t(0.5));
				const bool on_border = x_border_distance <= border_epsilon || z_border_distance <= border_epsilon;
				border &= on_border;
			}
			if (border) {
				surface = SURFACE_BORDER;
				semantic = SNAME("HEIGHTMAP_BORDER");
			}
		}

		constexpr int csg_order[3] = { 0, 1, 2 };
		for (int corner = 0; corner < 3; corner++) {
			const Vector3 point = manifold_vertices[csg_order[corner]];
			vertices.write[face * 3 + corner] = point;
			if (surface == SURFACE_TOP || surface == SURFACE_BOTTOM) {
				uvs.write[face * 3 + corner] = planar_uv(point);
			} else {
				const real_t u = Math::abs(normal.x) > Math::abs(normal.z) ? point.z / size.z + 0.5 : point.x / size.x + 0.5;
				uvs.write[face * 3 + corner] = Vector2(u, (point.y - bottom_y) / size.y);
			}
		}
		smooth.write[face] = false;
		materials.write[face] = face_material;
		surface_ids.write[face] = surface;
		face_layer_ids.write[face] = surface == SURFACE_BOTTOM ? 0 : face_layer_id;
		semantics.write[face] = semantic;
	}

	CSGBrush *height_map_brush = _create_brush_from_arrays(vertices, uvs, smooth, materials, surface_ids);
	for (int face = 0; face < height_map_brush->faces.size(); face++) {
		height_map_brush->faces.write[face].metadata.layer_id = face_layer_ids[face];
		height_map_brush->faces.write[face].metadata.semantic = semantics[face];
	}
	return height_map_brush;
}

void CSGHeightMap3D::_height_map_changed() {
	_make_dirty();
	callable_mp((Node3D *)this, &Node3D::update_gizmos).call_deferred();
}

void CSGHeightMap3D::_layer_settings_changed() {
	_make_dirty();
}

void CSGHeightMap3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_height_map", "height_map"), &CSGHeightMap3D::set_height_map);
	ClassDB::bind_method(D_METHOD("get_height_map"), &CSGHeightMap3D::get_height_map);
	ClassDB::bind_method(D_METHOD("set_region_enabled", "enabled"), &CSGHeightMap3D::set_region_enabled);
	ClassDB::bind_method(D_METHOD("is_region_enabled"), &CSGHeightMap3D::is_region_enabled);
	ClassDB::bind_method(D_METHOD("set_region_rect", "region"), &CSGHeightMap3D::set_region_rect);
	ClassDB::bind_method(D_METHOD("get_region_rect"), &CSGHeightMap3D::get_region_rect);
	ClassDB::bind_method(D_METHOD("set_size", "size"), &CSGHeightMap3D::set_size);
	ClassDB::bind_method(D_METHOD("get_size"), &CSGHeightMap3D::get_size);
	ClassDB::bind_method(D_METHOD("set_height_channel", "channel"), &CSGHeightMap3D::set_height_channel);
	ClassDB::bind_method(D_METHOD("get_height_channel"), &CSGHeightMap3D::get_height_channel);
	ClassDB::bind_method(D_METHOD("set_generation_mode", "mode"), &CSGHeightMap3D::set_generation_mode);
	ClassDB::bind_method(D_METHOD("get_generation_mode"), &CSGHeightMap3D::get_generation_mode);
	ClassDB::bind_method(D_METHOD("set_height_steps", "steps"), &CSGHeightMap3D::set_height_steps);
	ClassDB::bind_method(D_METHOD("get_height_steps"), &CSGHeightMap3D::get_height_steps);
	ClassDB::bind_method(D_METHOD("set_sampling_step", "step"), &CSGHeightMap3D::set_sampling_step);
	ClassDB::bind_method(D_METHOD("get_sampling_step"), &CSGHeightMap3D::get_sampling_step);
	ClassDB::bind_method(D_METHOD("set_slope_width", "width"), &CSGHeightMap3D::set_slope_width);
	ClassDB::bind_method(D_METHOD("get_slope_width"), &CSGHeightMap3D::get_slope_width);
	ClassDB::bind_method(D_METHOD("set_layer_settings", "settings"), &CSGHeightMap3D::set_layer_settings);
	ClassDB::bind_method(D_METHOD("get_layer_settings"), &CSGHeightMap3D::get_layer_settings);
	ClassDB::bind_method(D_METHOD("set_invert_height", "invert"), &CSGHeightMap3D::set_invert_height);
	ClassDB::bind_method(D_METHOD("is_height_inverted"), &CSGHeightMap3D::is_height_inverted);
	ClassDB::bind_method(D_METHOD("set_base_thickness", "thickness"), &CSGHeightMap3D::set_base_thickness);
	ClassDB::bind_method(D_METHOD("get_base_thickness"), &CSGHeightMap3D::get_base_thickness);
	ClassDB::bind_method(D_METHOD("set_material", "material"), &CSGHeightMap3D::set_material);
	ClassDB::bind_method(D_METHOD("get_material"), &CSGHeightMap3D::get_material);
	ClassDB::bind_method(D_METHOD("set_side_material", "material"), &CSGHeightMap3D::set_side_material);
	ClassDB::bind_method(D_METHOD("get_side_material"), &CSGHeightMap3D::get_side_material);
	ClassDB::bind_method(D_METHOD("set_bottom_material", "material"), &CSGHeightMap3D::set_bottom_material);
	ClassDB::bind_method(D_METHOD("get_bottom_material"), &CSGHeightMap3D::get_bottom_material);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "height_map", PROPERTY_HINT_RESOURCE_TYPE, Texture2D::get_class_static()), "set_height_map", "get_height_map");
	ADD_GROUP("Region", "region_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "region_enabled"), "set_region_enabled", "is_region_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::RECT2I, "region_rect", PROPERTY_HINT_NONE, "suffix:px"), "set_region_rect", "get_region_rect");
	ADD_GROUP("", "");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "size", PROPERTY_HINT_NONE, "suffix:m"), "set_size", "get_size");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "height_channel", PROPERTY_HINT_ENUM, "Luminance,Red,Green,Blue,Alpha"), "set_height_channel", "get_height_channel");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "generation_mode", PROPERTY_HINT_ENUM, "Cell Grid,Contour Layers"), "set_generation_mode", "get_generation_mode");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "height_steps", PROPERTY_HINT_RANGE, "0,256,1,or_greater"), "set_height_steps", "get_height_steps");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "sampling_step", PROPERTY_HINT_RANGE, "1,64,1,or_greater"), "set_sampling_step", "get_sampling_step");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "slope_width", PROPERTY_HINT_RANGE, "0,10,0.001,or_greater,suffix:m"), "set_slope_width", "get_slope_width");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "layer_settings", PROPERTY_HINT_ARRAY_TYPE, MAKE_RESOURCE_TYPE_HINT("CSGHeightMapLayer")), "set_layer_settings", "get_layer_settings");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "invert_height"), "set_invert_height", "is_height_inverted");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "base_thickness", PROPERTY_HINT_RANGE, "0.00001,100,0.001,or_greater,suffix:m"), "set_base_thickness", "get_base_thickness");
	ADD_GROUP("Material", "");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material", PROPERTY_HINT_RESOURCE_TYPE, "BaseMaterial3D,ShaderMaterial"), "set_material", "get_material");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "side_material", PROPERTY_HINT_RESOURCE_TYPE, "BaseMaterial3D,ShaderMaterial"), "set_side_material", "get_side_material");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "bottom_material", PROPERTY_HINT_RESOURCE_TYPE, "BaseMaterial3D,ShaderMaterial"), "set_bottom_material", "get_bottom_material");

	BIND_ENUM_CONSTANT(HEIGHT_CHANNEL_LUMINANCE);
	BIND_ENUM_CONSTANT(HEIGHT_CHANNEL_RED);
	BIND_ENUM_CONSTANT(HEIGHT_CHANNEL_GREEN);
	BIND_ENUM_CONSTANT(HEIGHT_CHANNEL_BLUE);
	BIND_ENUM_CONSTANT(HEIGHT_CHANNEL_ALPHA);
	BIND_ENUM_CONSTANT(GENERATION_CELL_GRID);
	BIND_ENUM_CONSTANT(GENERATION_CONTOUR_LAYERS);
}

void CSGHeightMap3D::set_height_map(const Ref<Texture2D> &p_height_map) {
	if (height_map == p_height_map) {
		return;
	}
	const Callable changed_callable = callable_mp(this, &CSGHeightMap3D::_height_map_changed);
	if (height_map.is_valid() && height_map->is_connected(StringName("changed"), changed_callable)) {
		height_map->disconnect(StringName("changed"), changed_callable);
	}
	height_map = p_height_map;
	if (height_map.is_valid()) {
		height_map->connect(StringName("changed"), changed_callable);
	}
	_height_map_changed();
}

Ref<Texture2D> CSGHeightMap3D::get_height_map() const {
	return height_map;
}

void CSGHeightMap3D::set_region_enabled(bool p_enabled) {
	if (region_enabled == p_enabled) {
		return;
	}
	region_enabled = p_enabled;
	_make_dirty();
}

bool CSGHeightMap3D::is_region_enabled() const {
	return region_enabled;
}

void CSGHeightMap3D::set_region_rect(const Rect2i &p_region) {
	const Rect2i normalized_region = p_region.abs();
	if (region_rect == normalized_region) {
		return;
	}
	region_rect = normalized_region;
	_make_dirty();
}

Rect2i CSGHeightMap3D::get_region_rect() const {
	return region_rect;
}

void CSGHeightMap3D::set_size(const Vector3 &p_size) {
	size = Vector3(MAX(p_size.x, real_t(0.001)), MAX(p_size.y, real_t(0.001)), MAX(p_size.z, real_t(0.001)));
	_make_dirty();
	update_gizmos();
}

Vector3 CSGHeightMap3D::get_size() const {
	return size;
}

void CSGHeightMap3D::set_height_channel(HeightChannel p_channel) {
	ERR_FAIL_INDEX(int(p_channel), 5);
	if (height_channel == p_channel) {
		return;
	}
	height_channel = p_channel;
	_make_dirty();
}

CSGHeightMap3D::HeightChannel CSGHeightMap3D::get_height_channel() const {
	return height_channel;
}

void CSGHeightMap3D::set_generation_mode(GenerationMode p_mode) {
	ERR_FAIL_INDEX(int(p_mode), 2);
	if (generation_mode == p_mode) {
		return;
	}
	generation_mode = p_mode;
	_make_dirty();
	update_gizmos();
}

CSGHeightMap3D::GenerationMode CSGHeightMap3D::get_generation_mode() const {
	return generation_mode;
}

void CSGHeightMap3D::set_height_steps(int p_steps) {
	p_steps = p_steps <= 1 ? 0 : p_steps;
	if (height_steps == p_steps) {
		return;
	}
	height_steps = p_steps;
	_make_dirty();
}

int CSGHeightMap3D::get_height_steps() const {
	return height_steps;
}

void CSGHeightMap3D::set_sampling_step(int p_step) {
	p_step = MAX(p_step, 1);
	if (sampling_step == p_step) {
		return;
	}
	sampling_step = p_step;
	_make_dirty();
	update_gizmos();
}

int CSGHeightMap3D::get_sampling_step() const {
	return sampling_step;
}

void CSGHeightMap3D::set_slope_width(real_t p_width) {
	p_width = MAX(p_width, real_t(0.0));
	if (Math::is_equal_approx(slope_width, p_width)) {
		return;
	}
	slope_width = p_width;
	_make_dirty();
}

real_t CSGHeightMap3D::get_slope_width() const {
	return slope_width;
}

void CSGHeightMap3D::set_layer_settings(const TypedArray<CSGHeightMapLayer> &p_settings) {
	const Callable changed_callable = callable_mp(this, &CSGHeightMap3D::_layer_settings_changed);
	for (int i = 0; i < layer_settings.size(); i++) {
		Ref<CSGHeightMapLayer> settings = layer_settings[i];
		if (settings.is_valid() && settings->is_connected(StringName("changed"), changed_callable)) {
			settings->disconnect(StringName("changed"), changed_callable);
		}
	}
	layer_settings = p_settings;
	for (int i = 0; i < layer_settings.size(); i++) {
		Ref<CSGHeightMapLayer> settings = layer_settings[i];
		if (settings.is_valid() && !settings->is_connected(StringName("changed"), changed_callable)) {
			settings->connect(StringName("changed"), changed_callable);
		}
	}
	_make_dirty();
}

TypedArray<CSGHeightMapLayer> CSGHeightMap3D::get_layer_settings() const {
	return layer_settings;
}

void CSGHeightMap3D::set_invert_height(bool p_invert) {
	if (invert_height == p_invert) {
		return;
	}
	invert_height = p_invert;
	_make_dirty();
}

bool CSGHeightMap3D::is_height_inverted() const {
	return invert_height;
}

void CSGHeightMap3D::set_base_thickness(real_t p_thickness) {
	p_thickness = MAX(p_thickness, real_t(0.00001));
	if (Math::is_equal_approx(base_thickness, p_thickness)) {
		return;
	}
	base_thickness = p_thickness;
	_make_dirty();
	update_gizmos();
}

real_t CSGHeightMap3D::get_base_thickness() const {
	return base_thickness;
}

void CSGHeightMap3D::set_material(const Ref<Material> &p_material) {
	if (material == p_material) {
		return;
	}
	material = p_material;
	_make_dirty();
}

Ref<Material> CSGHeightMap3D::get_material() const {
	return material;
}

void CSGHeightMap3D::set_side_material(const Ref<Material> &p_material) {
	if (side_material == p_material) {
		return;
	}
	side_material = p_material;
	_make_dirty();
}

Ref<Material> CSGHeightMap3D::get_side_material() const {
	return side_material;
}

void CSGHeightMap3D::set_bottom_material(const Ref<Material> &p_material) {
	if (bottom_material == p_material) {
		return;
	}
	bottom_material = p_material;
	_make_dirty();
}

Ref<Material> CSGHeightMap3D::get_bottom_material() const {
	return bottom_material;
}

///////////////

CSGBrush *CSGCylinder3D::_build_brush() {
	// set our bounding box

	CSGBrush *new_brush = memnew(CSGBrush);

	int face_count = sides * (cone ? 1 : 2) + sides + (cone ? 0 : sides);

	bool invert_val = get_flip_faces();
	Ref<Material> base_material = get_material();

	Vector<Vector3> faces;
	Vector<Vector2> uvs;
	Vector<bool> smooth;
	Vector<Ref<Material>> materials;
	Vector<bool> invert;

	faces.resize(face_count * 3);
	uvs.resize(face_count * 3);

	smooth.resize(face_count);
	materials.resize(face_count);
	invert.resize(face_count);

	{
		Vector3 *facesw = faces.ptrw();
		Vector2 *uvsw = uvs.ptrw();
		bool *smoothw = smooth.ptrw();
		Ref<Material> *materialsw = materials.ptrw();
		bool *invertw = invert.ptrw();

		int face = 0;

		Vector3 vertex_mul(radius, height * 0.5, radius);

		{
			for (int i = 0; i < sides; i++) {
				float inc = float(i) / sides;
				float inc_n = float((i + 1)) / sides;
				if (i == sides - 1) {
					inc_n = 0;
				}

				float ang = inc * Math::TAU;
				float ang_n = inc_n * Math::TAU;

				Vector3 face_base(Math::cos(ang), 0, Math::sin(ang));
				Vector3 face_base_n(Math::cos(ang_n), 0, Math::sin(ang_n));

				Vector3 face_points[4] = {
					face_base + Vector3(0, -1, 0),
					face_base_n + Vector3(0, -1, 0),
					face_base_n * (cone ? 0.0 : 1.0) + Vector3(0, 1, 0),
					face_base * (cone ? 0.0 : 1.0) + Vector3(0, 1, 0),
				};

				Vector2 u[4] = {
					Vector2(inc, 0),
					Vector2(inc_n, 0),
					Vector2(inc_n, 1),
					Vector2(inc, 1),
				};

				//side face 1
				facesw[face * 3 + 0] = face_points[0] * vertex_mul;
				facesw[face * 3 + 1] = face_points[1] * vertex_mul;
				facesw[face * 3 + 2] = face_points[2] * vertex_mul;

				uvsw[face * 3 + 0] = u[0];
				uvsw[face * 3 + 1] = u[1];
				uvsw[face * 3 + 2] = u[2];

				smoothw[face] = smooth_faces;
				invertw[face] = invert_val;
				materialsw[face] = base_material;

				face++;

				if (!cone) {
					//side face 2
					facesw[face * 3 + 0] = face_points[2] * vertex_mul;
					facesw[face * 3 + 1] = face_points[3] * vertex_mul;
					facesw[face * 3 + 2] = face_points[0] * vertex_mul;

					uvsw[face * 3 + 0] = u[2];
					uvsw[face * 3 + 1] = u[3];
					uvsw[face * 3 + 2] = u[0];

					smoothw[face] = smooth_faces;
					invertw[face] = invert_val;
					materialsw[face] = base_material;
					face++;
				}

				//bottom face 1
				facesw[face * 3 + 0] = face_points[1] * vertex_mul;
				facesw[face * 3 + 1] = face_points[0] * vertex_mul;
				facesw[face * 3 + 2] = Vector3(0, -1, 0) * vertex_mul;

				uvsw[face * 3 + 0] = Vector2(face_points[1].x, face_points[1].y) * 0.5 + Vector2(0.5, 0.5);
				uvsw[face * 3 + 1] = Vector2(face_points[0].x, face_points[0].y) * 0.5 + Vector2(0.5, 0.5);
				uvsw[face * 3 + 2] = Vector2(0.5, 0.5);

				smoothw[face] = false;
				invertw[face] = invert_val;
				materialsw[face] = base_material;
				face++;

				if (!cone) {
					//top face 1
					facesw[face * 3 + 0] = face_points[3] * vertex_mul;
					facesw[face * 3 + 1] = face_points[2] * vertex_mul;
					facesw[face * 3 + 2] = Vector3(0, 1, 0) * vertex_mul;

					uvsw[face * 3 + 0] = Vector2(face_points[1].x, face_points[1].y) * 0.5 + Vector2(0.5, 0.5);
					uvsw[face * 3 + 1] = Vector2(face_points[0].x, face_points[0].y) * 0.5 + Vector2(0.5, 0.5);
					uvsw[face * 3 + 2] = Vector2(0.5, 0.5);

					smoothw[face] = false;
					invertw[face] = invert_val;
					materialsw[face] = base_material;
					face++;
				}
			}
		}

		if (face != face_count) {
			ERR_PRINT("Face mismatch bug! fix code");
		}
	}

	new_brush->build_from_faces(faces, uvs, smooth, materials, invert);

	return new_brush;
}

void CSGCylinder3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_radius", "radius"), &CSGCylinder3D::set_radius);
	ClassDB::bind_method(D_METHOD("get_radius"), &CSGCylinder3D::get_radius);

	ClassDB::bind_method(D_METHOD("set_height", "height"), &CSGCylinder3D::set_height);
	ClassDB::bind_method(D_METHOD("get_height"), &CSGCylinder3D::get_height);

	ClassDB::bind_method(D_METHOD("set_sides", "sides"), &CSGCylinder3D::set_sides);
	ClassDB::bind_method(D_METHOD("get_sides"), &CSGCylinder3D::get_sides);

	ClassDB::bind_method(D_METHOD("set_cone", "cone"), &CSGCylinder3D::set_cone);
	ClassDB::bind_method(D_METHOD("is_cone"), &CSGCylinder3D::is_cone);

	ClassDB::bind_method(D_METHOD("set_material", "material"), &CSGCylinder3D::set_material);
	ClassDB::bind_method(D_METHOD("get_material"), &CSGCylinder3D::get_material);

	ClassDB::bind_method(D_METHOD("set_smooth_faces", "smooth_faces"), &CSGCylinder3D::set_smooth_faces);
	ClassDB::bind_method(D_METHOD("get_smooth_faces"), &CSGCylinder3D::get_smooth_faces);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius", PROPERTY_HINT_RANGE, "0.001,1000.0,0.001,or_greater,exp,suffix:m"), "set_radius", "get_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "height", PROPERTY_HINT_RANGE, "0.001,1000.0,0.001,or_greater,exp,suffix:m"), "set_height", "get_height");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "sides", PROPERTY_HINT_RANGE, "3,64,1"), "set_sides", "get_sides");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "cone"), "set_cone", "is_cone");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "smooth_faces"), "set_smooth_faces", "get_smooth_faces");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material", PROPERTY_HINT_RESOURCE_TYPE, "BaseMaterial3D,ShaderMaterial"), "set_material", "get_material");
}

void CSGCylinder3D::set_radius(const float p_radius) {
	radius = p_radius;
	_make_dirty();
	update_gizmos();
}

float CSGCylinder3D::get_radius() const {
	return radius;
}

void CSGCylinder3D::set_height(const float p_height) {
	height = p_height;
	_make_dirty();
	update_gizmos();
}

float CSGCylinder3D::get_height() const {
	return height;
}

void CSGCylinder3D::set_sides(const int p_sides) {
	ERR_FAIL_COND(p_sides < 3);
	sides = p_sides;
	_make_dirty();
	update_gizmos();
}

int CSGCylinder3D::get_sides() const {
	return sides;
}

void CSGCylinder3D::set_cone(const bool p_cone) {
	cone = p_cone;
	_make_dirty();
	update_gizmos();
}

bool CSGCylinder3D::is_cone() const {
	return cone;
}

void CSGCylinder3D::set_smooth_faces(const bool p_smooth_faces) {
	smooth_faces = p_smooth_faces;
	_make_dirty();
}

bool CSGCylinder3D::get_smooth_faces() const {
	return smooth_faces;
}

void CSGCylinder3D::set_material(const Ref<Material> &p_material) {
	material = p_material;
	_make_dirty();
}

Ref<Material> CSGCylinder3D::get_material() const {
	return material;
}

CSGCylinder3D::CSGCylinder3D() {
	// defaults
	radius = 0.5;
	height = 2.0;
	sides = 8;
	cone = false;
	smooth_faces = true;
}

///////////////

CSGBrush *CSGTorus3D::_build_brush() {
	// set our bounding box

	float min_radius = inner_radius;
	float max_radius = outer_radius;

	if (min_radius == max_radius) {
		return memnew(CSGBrush); //sorry, can't
	}

	if (min_radius > max_radius) {
		SWAP(min_radius, max_radius);
	}

	float radius = (max_radius - min_radius) * 0.5;

	CSGBrush *new_brush = memnew(CSGBrush);

	int face_count = ring_sides * sides * 2;

	bool invert_val = get_flip_faces();
	Ref<Material> base_material = get_material();

	Vector<Vector3> faces;
	Vector<Vector2> uvs;
	Vector<bool> smooth;
	Vector<Ref<Material>> materials;
	Vector<bool> invert;

	faces.resize(face_count * 3);
	uvs.resize(face_count * 3);

	smooth.resize(face_count);
	materials.resize(face_count);
	invert.resize(face_count);

	{
		Vector3 *facesw = faces.ptrw();
		Vector2 *uvsw = uvs.ptrw();
		bool *smoothw = smooth.ptrw();
		Ref<Material> *materialsw = materials.ptrw();
		bool *invertw = invert.ptrw();

		int face = 0;

		{
			for (int i = 0; i < sides; i++) {
				float inci = float(i) / sides;
				float inci_n = float((i + 1)) / sides;
				if (i == sides - 1) {
					inci_n = 0;
				}

				float angi = inci * Math::TAU;
				float angi_n = inci_n * Math::TAU;

				Vector3 normali = Vector3(Math::cos(angi), 0, Math::sin(angi));
				Vector3 normali_n = Vector3(Math::cos(angi_n), 0, Math::sin(angi_n));

				for (int j = 0; j < ring_sides; j++) {
					float incj = float(j) / ring_sides;
					float incj_n = float((j + 1)) / ring_sides;
					if (j == ring_sides - 1) {
						incj_n = 0;
					}

					float angj = incj * Math::TAU;
					float angj_n = incj_n * Math::TAU;

					Vector2 normalj = Vector2(Math::cos(angj), Math::sin(angj)) * radius + Vector2(min_radius + radius, 0);
					Vector2 normalj_n = Vector2(Math::cos(angj_n), Math::sin(angj_n)) * radius + Vector2(min_radius + radius, 0);

					Vector3 face_points[4] = {
						Vector3(normali.x * normalj.x, normalj.y, normali.z * normalj.x),
						Vector3(normali.x * normalj_n.x, normalj_n.y, normali.z * normalj_n.x),
						Vector3(normali_n.x * normalj_n.x, normalj_n.y, normali_n.z * normalj_n.x),
						Vector3(normali_n.x * normalj.x, normalj.y, normali_n.z * normalj.x)
					};

					Vector2 u[4] = {
						Vector2(inci, incj),
						Vector2(inci, incj_n),
						Vector2(inci_n, incj_n),
						Vector2(inci_n, incj),
					};

					// face 1
					facesw[face * 3 + 0] = face_points[0];
					facesw[face * 3 + 1] = face_points[2];
					facesw[face * 3 + 2] = face_points[1];

					uvsw[face * 3 + 0] = u[0];
					uvsw[face * 3 + 1] = u[2];
					uvsw[face * 3 + 2] = u[1];

					smoothw[face] = smooth_faces;
					invertw[face] = invert_val;
					materialsw[face] = base_material;

					face++;

					//face 2
					facesw[face * 3 + 0] = face_points[3];
					facesw[face * 3 + 1] = face_points[2];
					facesw[face * 3 + 2] = face_points[0];

					uvsw[face * 3 + 0] = u[3];
					uvsw[face * 3 + 1] = u[2];
					uvsw[face * 3 + 2] = u[0];

					smoothw[face] = smooth_faces;
					invertw[face] = invert_val;
					materialsw[face] = base_material;
					face++;
				}
			}
		}

		if (face != face_count) {
			ERR_PRINT("Face mismatch bug! fix code");
		}
	}

	new_brush->build_from_faces(faces, uvs, smooth, materials, invert);

	return new_brush;
}

void CSGTorus3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_inner_radius", "radius"), &CSGTorus3D::set_inner_radius);
	ClassDB::bind_method(D_METHOD("get_inner_radius"), &CSGTorus3D::get_inner_radius);

	ClassDB::bind_method(D_METHOD("set_outer_radius", "radius"), &CSGTorus3D::set_outer_radius);
	ClassDB::bind_method(D_METHOD("get_outer_radius"), &CSGTorus3D::get_outer_radius);

	ClassDB::bind_method(D_METHOD("set_sides", "sides"), &CSGTorus3D::set_sides);
	ClassDB::bind_method(D_METHOD("get_sides"), &CSGTorus3D::get_sides);

	ClassDB::bind_method(D_METHOD("set_ring_sides", "sides"), &CSGTorus3D::set_ring_sides);
	ClassDB::bind_method(D_METHOD("get_ring_sides"), &CSGTorus3D::get_ring_sides);

	ClassDB::bind_method(D_METHOD("set_material", "material"), &CSGTorus3D::set_material);
	ClassDB::bind_method(D_METHOD("get_material"), &CSGTorus3D::get_material);

	ClassDB::bind_method(D_METHOD("set_smooth_faces", "smooth_faces"), &CSGTorus3D::set_smooth_faces);
	ClassDB::bind_method(D_METHOD("get_smooth_faces"), &CSGTorus3D::get_smooth_faces);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "inner_radius", PROPERTY_HINT_RANGE, "0.001,1000.0,0.001,or_greater,exp,suffix:m"), "set_inner_radius", "get_inner_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "outer_radius", PROPERTY_HINT_RANGE, "0.001,1000.0,0.001,or_greater,exp,suffix:m"), "set_outer_radius", "get_outer_radius");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "sides", PROPERTY_HINT_RANGE, "3,64,1"), "set_sides", "get_sides");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "ring_sides", PROPERTY_HINT_RANGE, "3,64,1"), "set_ring_sides", "get_ring_sides");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "smooth_faces"), "set_smooth_faces", "get_smooth_faces");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material", PROPERTY_HINT_RESOURCE_TYPE, "BaseMaterial3D,ShaderMaterial"), "set_material", "get_material");
}

void CSGTorus3D::set_inner_radius(const float p_inner_radius) {
	inner_radius = p_inner_radius;
	_make_dirty();
	update_gizmos();
}

float CSGTorus3D::get_inner_radius() const {
	return inner_radius;
}

void CSGTorus3D::set_outer_radius(const float p_outer_radius) {
	outer_radius = p_outer_radius;
	_make_dirty();
	update_gizmos();
}

float CSGTorus3D::get_outer_radius() const {
	return outer_radius;
}

void CSGTorus3D::set_sides(const int p_sides) {
	ERR_FAIL_COND(p_sides < 3);
	sides = p_sides;
	_make_dirty();
	update_gizmos();
}

int CSGTorus3D::get_sides() const {
	return sides;
}

void CSGTorus3D::set_ring_sides(const int p_ring_sides) {
	ERR_FAIL_COND(p_ring_sides < 3);
	ring_sides = p_ring_sides;
	_make_dirty();
	update_gizmos();
}

int CSGTorus3D::get_ring_sides() const {
	return ring_sides;
}

void CSGTorus3D::set_smooth_faces(const bool p_smooth_faces) {
	smooth_faces = p_smooth_faces;
	_make_dirty();
}

bool CSGTorus3D::get_smooth_faces() const {
	return smooth_faces;
}

void CSGTorus3D::set_material(const Ref<Material> &p_material) {
	material = p_material;
	_make_dirty();
}

Ref<Material> CSGTorus3D::get_material() const {
	return material;
}

CSGTorus3D::CSGTorus3D() {
	// defaults
	inner_radius = 0.5;
	outer_radius = 1.0;
	sides = 8;
	ring_sides = 6;
	smooth_faces = true;
}

///////////////

CSGBrush *CSGPolygon3D::_build_brush() {
	CSGBrush *new_brush = memnew(CSGBrush);

	if (polygon.size() < 3) {
		return new_brush;
	}

	// Triangulate polygon shape.
	Vector<Point2> shape_polygon = polygon;
	if (Triangulate::get_area(shape_polygon) > 0) {
		shape_polygon.reverse();
	}
	int shape_sides = shape_polygon.size();
	Vector<int> shape_faces = Geometry2D::triangulate_polygon(shape_polygon);
	ERR_FAIL_COND_V_MSG(shape_faces.size() < 3, new_brush, "Failed to triangulate CSGPolygon. Make sure the polygon doesn't have any intersecting edges.");

	// Get polygon enclosing Rect2.
	Rect2 shape_rect(shape_polygon[0], Vector2());
	for (int i = 1; i < shape_sides; i++) {
		shape_rect.expand_to(shape_polygon[i]);
	}

	// If MODE_PATH, check if curve has changed.
	Ref<Curve3D> curve;
	if (mode == MODE_PATH) {
		Path3D *current_path = Object::cast_to<Path3D>(get_node_or_null(path_node));
		if (path != current_path) {
			if (path) {
				path->disconnect(SceneStringName(tree_exited), callable_mp(this, &CSGPolygon3D::_path_exited));
				path->disconnect("curve_changed", callable_mp(this, &CSGPolygon3D::_path_changed));
				path->set_update_callback(Callable());
			}
			path = current_path;
			if (path) {
				path->connect(SceneStringName(tree_exited), callable_mp(this, &CSGPolygon3D::_path_exited));
				path->connect("curve_changed", callable_mp(this, &CSGPolygon3D::_path_changed));
				path->set_update_callback(callable_mp(this, &CSGPolygon3D::_path_changed));
			}
		}

		if (!path) {
			return new_brush;
		}

		curve = path->get_curve();
		if (curve.is_null() || curve->get_point_count() < 2) {
			return new_brush;
		}
	}

	// Calculate the number extrusions, ends and faces.
	int extrusions = 0;
	int extrusion_face_count = shape_sides * 2;
	int end_count = 0;
	int shape_face_count = shape_faces.size() / 3;
	real_t curve_length = 1.0;
	switch (mode) {
		case MODE_DEPTH:
			extrusions = 1;
			end_count = 2;
			break;
		case MODE_SPIN:
			extrusions = spin_sides;
			if (spin_degrees < 360) {
				end_count = 2;
			}
			break;
		case MODE_PATH: {
			curve_length = curve->get_baked_length();
			if (path_interval_type == PATH_INTERVAL_DISTANCE) {
				extrusions = MAX(1, Math::ceil(curve_length / path_interval)) + 1;
			} else {
				extrusions = Math::ceil(1.0 * curve->get_point_count() / path_interval);
			}
			if (!path_joined) {
				end_count = 2;
				extrusions -= 1;
			}
		} break;
	}
	int face_count = extrusions * extrusion_face_count + end_count * shape_face_count;

	// Initialize variables used to create the mesh.
	Ref<Material> base_material = get_material();

	Vector<Vector3> faces;
	Vector<Vector2> uvs;
	Vector<bool> smooth;
	Vector<Ref<Material>> materials;
	Vector<bool> invert;

	faces.resize(face_count * 3);
	uvs.resize(face_count * 3);
	smooth.resize(face_count);
	materials.resize(face_count);
	invert.resize(face_count);
	int faces_removed = 0;

	{
		Vector3 *facesw = faces.ptrw();
		Vector2 *uvsw = uvs.ptrw();
		bool *smoothw = smooth.ptrw();
		Ref<Material> *materialsw = materials.ptrw();
		bool *invertw = invert.ptrw();

		int face = 0;
		Transform3D base_xform;
		Transform3D current_xform;
		Transform3D previous_xform;
		Transform3D previous_previous_xform;
		double u_step = 1.0 / extrusions;
		if (path_u_distance > 0.0) {
			u_step *= curve_length / path_u_distance;
		}
		double v_step = 1.0 / shape_sides;
		double spin_step = Math::deg_to_rad(spin_degrees / spin_sides);
		double extrusion_step = 1.0 / extrusions;
		if (mode == MODE_PATH) {
			if (path_joined) {
				extrusion_step = 1.0 / (extrusions - 1);
			}
			extrusion_step *= curve_length;
		}

		if (mode == MODE_PATH) {
			if (!path_local && path->is_inside_tree()) {
				base_xform = path->get_global_transform();
			}

			Vector3 current_point;
			Vector3 current_up = Vector3(0, 1, 0);
			Vector3 direction;

			switch (path_rotation) {
				case PATH_ROTATION_POLYGON:
					current_point = curve->sample_baked(0);
					direction = Vector3(0, 0, -1);
					break;
				case PATH_ROTATION_PATH:
				case PATH_ROTATION_PATH_FOLLOW:
					if (!path_rotation_accurate) {
						current_point = curve->sample_baked(0);
						Vector3 next_point = curve->sample_baked(extrusion_step);
						direction = next_point - current_point;

						if (path_joined) {
							Vector3 last_point = curve->sample_baked(curve->get_baked_length());
							direction = next_point - last_point;
						}
					} else {
						Transform3D current_sample_xform = curve->sample_baked_with_rotation(0);
						current_point = current_sample_xform.get_origin();
						direction = current_sample_xform.get_basis().xform(Vector3(0, 0, -1));
					}

					if (path_rotation == PATH_ROTATION_PATH_FOLLOW) {
						current_up = curve->sample_baked_up_vector(0, true);
					}
					break;
			}

			Transform3D facing = Transform3D().looking_at(direction, current_up);
			current_xform = base_xform.translated_local(current_point) * facing;
		}

		// Create the mesh.
		if (end_count > 0) {
			// Add front end face.
			for (int face_idx = 0; face_idx < shape_face_count; face_idx++) {
				for (int face_vertex_idx = 0; face_vertex_idx < 3; face_vertex_idx++) {
					// We need to reverse the rotation of the shape face vertices.
					int index = shape_faces[face_idx * 3 + 2 - face_vertex_idx];
					Point2 p = shape_polygon[index];
					Point2 uv = (p - shape_rect.position) / shape_rect.size;

					// Use the left side of the bottom half of the y-inverted texture.
					uv.x = uv.x / 2;
					uv.y = 1 - (uv.y / 2);

					facesw[face * 3 + face_vertex_idx] = current_xform.xform(Vector3(p.x, p.y, 0));
					uvsw[face * 3 + face_vertex_idx] = uv;
				}

				smoothw[face] = false;
				materialsw[face] = base_material;
				invertw[face] = flip_faces;
				face++;
			}
		}

		real_t angle_simplify_dot = Math::cos(Math::deg_to_rad(path_simplify_angle));
		Vector3 previous_simplify_dir = Vector3(0, 0, 0);
		int faces_combined = 0;

		// Add extrusion faces.
		for (int x0 = 0; x0 < extrusions; x0++) {
			previous_previous_xform = previous_xform;
			previous_xform = current_xform;

			switch (mode) {
				case MODE_DEPTH: {
					current_xform.translate_local(Vector3(0, 0, -depth));
				} break;
				case MODE_SPIN: {
					if (end_count == 0 && x0 == extrusions - 1) {
						current_xform = base_xform;
					} else {
						current_xform.rotate(Vector3(0, 1, 0), spin_step);
					}
				} break;
				case MODE_PATH: {
					double previous_offset = x0 * extrusion_step;
					double current_offset = (x0 + 1) * extrusion_step;
					if (path_joined && x0 == extrusions - 1) {
						current_offset = 0;
					}

					Vector3 previous_point = curve->sample_baked(previous_offset);
					Transform3D current_sample_xform = curve->sample_baked_with_rotation(current_offset);
					Vector3 current_point = current_sample_xform.get_origin();
					Vector3 current_up = Vector3(0, 1, 0);
					Vector3 current_extrusion_dir = (current_point - previous_point).normalized();
					Vector3 direction;

					// If the angles are similar, remove the previous face and replace it with this one.
					if (path_simplify_angle > 0.0 && x0 > 0 && previous_simplify_dir.dot(current_extrusion_dir) > angle_simplify_dot) {
						faces_combined += 1;
						previous_xform = previous_previous_xform;
						face -= extrusion_face_count;
						faces_removed += extrusion_face_count;
					} else {
						faces_combined = 0;
						previous_simplify_dir = current_extrusion_dir;
					}

					switch (path_rotation) {
						case PATH_ROTATION_POLYGON:
							direction = Vector3(0, 0, -1);
							break;
						case PATH_ROTATION_PATH:
						case PATH_ROTATION_PATH_FOLLOW:
							if (!path_rotation_accurate) {
								double next_offset = (x0 + 2) * extrusion_step;
								if (x0 == extrusions - 1) {
									next_offset = path_joined ? extrusion_step : current_offset;
								}
								Vector3 next_point = curve->sample_baked(next_offset);
								direction = next_point - previous_point;
							} else {
								direction = current_sample_xform.get_basis().xform(Vector3(0, 0, -1));
							}

							if (path_rotation == PATH_ROTATION_PATH_FOLLOW) {
								current_up = curve->sample_baked_up_vector(current_offset, true);
							}
							break;
					}

					Transform3D facing = Transform3D().looking_at(direction, current_up);
					current_xform = base_xform.translated_local(current_point) * facing;
				} break;
			}

			double u0 = (x0 - faces_combined) * u_step;
			double u1 = ((x0 + 1) * u_step);
			if (mode == MODE_PATH && !path_continuous_u) {
				u0 = 0.0;
				u1 = 1.0;
			}

			for (int y0 = 0; y0 < shape_sides; y0++) {
				int y1 = (y0 + 1) % shape_sides;
				// Use the top half of the texture.
				double v0 = (y0 * v_step) / 2;
				double v1 = ((y0 + 1) * v_step) / 2;

				Vector3 v[4] = {
					previous_xform.xform(Vector3(shape_polygon[y0].x, shape_polygon[y0].y, 0)),
					current_xform.xform(Vector3(shape_polygon[y0].x, shape_polygon[y0].y, 0)),
					current_xform.xform(Vector3(shape_polygon[y1].x, shape_polygon[y1].y, 0)),
					previous_xform.xform(Vector3(shape_polygon[y1].x, shape_polygon[y1].y, 0)),
				};

				Vector2 u[4] = {
					Vector2(u0, v0),
					Vector2(u1, v0),
					Vector2(u1, v1),
					Vector2(u0, v1),
				};

				// Face 1
				facesw[face * 3 + 0] = v[0];
				facesw[face * 3 + 1] = v[1];
				facesw[face * 3 + 2] = v[2];

				uvsw[face * 3 + 0] = u[0];
				uvsw[face * 3 + 1] = u[1];
				uvsw[face * 3 + 2] = u[2];

				smoothw[face] = smooth_faces;
				invertw[face] = flip_faces;
				materialsw[face] = base_material;

				face++;

				// Face 2
				facesw[face * 3 + 0] = v[2];
				facesw[face * 3 + 1] = v[3];
				facesw[face * 3 + 2] = v[0];

				uvsw[face * 3 + 0] = u[2];
				uvsw[face * 3 + 1] = u[3];
				uvsw[face * 3 + 2] = u[0];

				smoothw[face] = smooth_faces;
				invertw[face] = flip_faces;
				materialsw[face] = base_material;

				face++;
			}
		}

		if (end_count > 1) {
			// Add back end face.
			for (int face_idx = 0; face_idx < shape_face_count; face_idx++) {
				for (int face_vertex_idx = 0; face_vertex_idx < 3; face_vertex_idx++) {
					int index = shape_faces[face_idx * 3 + face_vertex_idx];
					Point2 p = shape_polygon[index];
					Point2 uv = (p - shape_rect.position) / shape_rect.size;

					// Use the x-inverted ride side of the bottom half of the y-inverted texture.
					uv.x = 1 - uv.x / 2;
					uv.y = 1 - (uv.y / 2);

					facesw[face * 3 + face_vertex_idx] = current_xform.xform(Vector3(p.x, p.y, 0));
					uvsw[face * 3 + face_vertex_idx] = uv;
				}

				smoothw[face] = false;
				materialsw[face] = base_material;
				invertw[face] = flip_faces;
				face++;
			}
		}

		face_count -= faces_removed;
		ERR_FAIL_COND_V_MSG(face != face_count, new_brush, "Bug: Failed to create the CSGPolygon mesh correctly.");
	}

	if (faces_removed > 0) {
		faces.resize(face_count * 3);
		uvs.resize(face_count * 3);
		smooth.resize(face_count);
		materials.resize(face_count);
		invert.resize(face_count);
	}

	new_brush->build_from_faces(faces, uvs, smooth, materials, invert);

	return new_brush;
}

void CSGPolygon3D::_notification(int p_what) {
	if (p_what == NOTIFICATION_EXIT_TREE) {
		if (path) {
			path->disconnect(SceneStringName(tree_exited), callable_mp(this, &CSGPolygon3D::_path_exited));
			path->disconnect("curve_changed", callable_mp(this, &CSGPolygon3D::_path_changed));
			path = nullptr;
		}
	}
}

void CSGPolygon3D::_validate_property(PropertyInfo &p_property) const {
	if (p_property.name.begins_with("spin") && mode != MODE_SPIN) {
		p_property.usage = PROPERTY_USAGE_NONE;
	}
	if (p_property.name.begins_with("path") && mode != MODE_PATH) {
		p_property.usage = PROPERTY_USAGE_NONE;
	}
	if (p_property.name == "depth" && mode != MODE_DEPTH) {
		p_property.usage = PROPERTY_USAGE_NONE;
	}
}

void CSGPolygon3D::_path_changed() {
	_make_dirty();
	update_gizmos();
}

void CSGPolygon3D::_path_exited() {
	path = nullptr;
}

void CSGPolygon3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_polygon", "polygon"), &CSGPolygon3D::set_polygon);
	ClassDB::bind_method(D_METHOD("get_polygon"), &CSGPolygon3D::get_polygon);

	ClassDB::bind_method(D_METHOD("set_mode", "mode"), &CSGPolygon3D::set_mode);
	ClassDB::bind_method(D_METHOD("get_mode"), &CSGPolygon3D::get_mode);

	ClassDB::bind_method(D_METHOD("set_depth", "depth"), &CSGPolygon3D::set_depth);
	ClassDB::bind_method(D_METHOD("get_depth"), &CSGPolygon3D::get_depth);

	ClassDB::bind_method(D_METHOD("set_spin_degrees", "degrees"), &CSGPolygon3D::set_spin_degrees);
	ClassDB::bind_method(D_METHOD("get_spin_degrees"), &CSGPolygon3D::get_spin_degrees);

	ClassDB::bind_method(D_METHOD("set_spin_sides", "spin_sides"), &CSGPolygon3D::set_spin_sides);
	ClassDB::bind_method(D_METHOD("get_spin_sides"), &CSGPolygon3D::get_spin_sides);

	ClassDB::bind_method(D_METHOD("set_path_node", "path"), &CSGPolygon3D::set_path_node);
	ClassDB::bind_method(D_METHOD("get_path_node"), &CSGPolygon3D::get_path_node);

	ClassDB::bind_method(D_METHOD("set_path_interval_type", "interval_type"), &CSGPolygon3D::set_path_interval_type);
	ClassDB::bind_method(D_METHOD("get_path_interval_type"), &CSGPolygon3D::get_path_interval_type);

	ClassDB::bind_method(D_METHOD("set_path_interval", "interval"), &CSGPolygon3D::set_path_interval);
	ClassDB::bind_method(D_METHOD("get_path_interval"), &CSGPolygon3D::get_path_interval);

	ClassDB::bind_method(D_METHOD("set_path_simplify_angle", "degrees"), &CSGPolygon3D::set_path_simplify_angle);
	ClassDB::bind_method(D_METHOD("get_path_simplify_angle"), &CSGPolygon3D::get_path_simplify_angle);

	ClassDB::bind_method(D_METHOD("set_path_rotation", "path_rotation"), &CSGPolygon3D::set_path_rotation);
	ClassDB::bind_method(D_METHOD("get_path_rotation"), &CSGPolygon3D::get_path_rotation);

	ClassDB::bind_method(D_METHOD("set_path_rotation_accurate", "enable"), &CSGPolygon3D::set_path_rotation_accurate);
	ClassDB::bind_method(D_METHOD("get_path_rotation_accurate"), &CSGPolygon3D::get_path_rotation_accurate);

	ClassDB::bind_method(D_METHOD("set_path_local", "enable"), &CSGPolygon3D::set_path_local);
	ClassDB::bind_method(D_METHOD("is_path_local"), &CSGPolygon3D::is_path_local);

	ClassDB::bind_method(D_METHOD("set_path_continuous_u", "enable"), &CSGPolygon3D::set_path_continuous_u);
	ClassDB::bind_method(D_METHOD("is_path_continuous_u"), &CSGPolygon3D::is_path_continuous_u);

	ClassDB::bind_method(D_METHOD("set_path_u_distance", "distance"), &CSGPolygon3D::set_path_u_distance);
	ClassDB::bind_method(D_METHOD("get_path_u_distance"), &CSGPolygon3D::get_path_u_distance);

	ClassDB::bind_method(D_METHOD("set_path_joined", "enable"), &CSGPolygon3D::set_path_joined);
	ClassDB::bind_method(D_METHOD("is_path_joined"), &CSGPolygon3D::is_path_joined);

	ClassDB::bind_method(D_METHOD("set_material", "material"), &CSGPolygon3D::set_material);
	ClassDB::bind_method(D_METHOD("get_material"), &CSGPolygon3D::get_material);

	ClassDB::bind_method(D_METHOD("set_smooth_faces", "smooth_faces"), &CSGPolygon3D::set_smooth_faces);
	ClassDB::bind_method(D_METHOD("get_smooth_faces"), &CSGPolygon3D::get_smooth_faces);

	ClassDB::bind_method(D_METHOD("_is_editable_3d_polygon"), &CSGPolygon3D::_is_editable_3d_polygon);
	ClassDB::bind_method(D_METHOD("_has_editable_3d_polygon_no_depth"), &CSGPolygon3D::_has_editable_3d_polygon_no_depth);

	ADD_PROPERTY(PropertyInfo(Variant::PACKED_VECTOR2_ARRAY, "polygon"), "set_polygon", "get_polygon");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "mode", PROPERTY_HINT_ENUM, "Depth,Spin,Path"), "set_mode", "get_mode");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "depth", PROPERTY_HINT_RANGE, "0.01,100.0,0.01,or_greater,exp,suffix:m"), "set_depth", "get_depth");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "spin_degrees", PROPERTY_HINT_RANGE, "1,360,0.1"), "set_spin_degrees", "get_spin_degrees");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "spin_sides", PROPERTY_HINT_RANGE, "3,64,1"), "set_spin_sides", "get_spin_sides");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "path_node", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "Path3D"), "set_path_node", "get_path_node");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "path_interval_type", PROPERTY_HINT_ENUM, "Distance,Subdivide"), "set_path_interval_type", "get_path_interval_type");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "path_interval", PROPERTY_HINT_RANGE, "0.01,1.0,0.01,exp,or_greater"), "set_path_interval", "get_path_interval");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "path_simplify_angle", PROPERTY_HINT_RANGE, "0.0,180.0,0.1"), "set_path_simplify_angle", "get_path_simplify_angle");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "path_rotation", PROPERTY_HINT_ENUM, "Polygon,Path,PathFollow"), "set_path_rotation", "get_path_rotation");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "path_rotation_accurate"), "set_path_rotation_accurate", "get_path_rotation_accurate");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "path_local"), "set_path_local", "is_path_local");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "path_continuous_u"), "set_path_continuous_u", "is_path_continuous_u");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "path_u_distance", PROPERTY_HINT_RANGE, "0.0,10.0,0.01,or_greater,suffix:m"), "set_path_u_distance", "get_path_u_distance");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "path_joined"), "set_path_joined", "is_path_joined");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "smooth_faces"), "set_smooth_faces", "get_smooth_faces");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "material", PROPERTY_HINT_RESOURCE_TYPE, "BaseMaterial3D,ShaderMaterial"), "set_material", "get_material");

	BIND_ENUM_CONSTANT(MODE_DEPTH);
	BIND_ENUM_CONSTANT(MODE_SPIN);
	BIND_ENUM_CONSTANT(MODE_PATH);

	BIND_ENUM_CONSTANT(PATH_ROTATION_POLYGON);
	BIND_ENUM_CONSTANT(PATH_ROTATION_PATH);
	BIND_ENUM_CONSTANT(PATH_ROTATION_PATH_FOLLOW);

	BIND_ENUM_CONSTANT(PATH_INTERVAL_DISTANCE);
	BIND_ENUM_CONSTANT(PATH_INTERVAL_SUBDIVIDE);
}

void CSGPolygon3D::set_polygon(const Vector<Vector2> &p_polygon) {
	polygon = p_polygon;
	_make_dirty();
	update_gizmos();
}

Vector<Vector2> CSGPolygon3D::get_polygon() const {
	return polygon;
}

void CSGPolygon3D::set_mode(Mode p_mode) {
	mode = p_mode;
	_make_dirty();
	update_gizmos();
	notify_property_list_changed();
}

CSGPolygon3D::Mode CSGPolygon3D::get_mode() const {
	return mode;
}

void CSGPolygon3D::set_depth(const float p_depth) {
	ERR_FAIL_COND(p_depth < 0.001);
	depth = p_depth;
	_make_dirty();
	update_gizmos();
}

float CSGPolygon3D::get_depth() const {
	return depth;
}

void CSGPolygon3D::set_path_continuous_u(bool p_enable) {
	path_continuous_u = p_enable;
	_make_dirty();
}

bool CSGPolygon3D::is_path_continuous_u() const {
	return path_continuous_u;
}

void CSGPolygon3D::set_path_u_distance(real_t p_path_u_distance) {
	path_u_distance = p_path_u_distance;
	_make_dirty();
	update_gizmos();
}

real_t CSGPolygon3D::get_path_u_distance() const {
	return path_u_distance;
}

void CSGPolygon3D::set_spin_degrees(const float p_spin_degrees) {
	ERR_FAIL_COND(p_spin_degrees < 0.01 || p_spin_degrees > 360);
	spin_degrees = p_spin_degrees;
	_make_dirty();
	update_gizmos();
}

float CSGPolygon3D::get_spin_degrees() const {
	return spin_degrees;
}

void CSGPolygon3D::set_spin_sides(int p_spin_sides) {
	ERR_FAIL_COND(p_spin_sides < 3);
	spin_sides = p_spin_sides;
	_make_dirty();
	update_gizmos();
}

int CSGPolygon3D::get_spin_sides() const {
	return spin_sides;
}

void CSGPolygon3D::set_path_node(const NodePath &p_path) {
	path_node = p_path;
	_make_dirty();
	update_gizmos();
}

NodePath CSGPolygon3D::get_path_node() const {
	return path_node;
}

void CSGPolygon3D::set_path_interval_type(PathIntervalType p_interval_type) {
	path_interval_type = p_interval_type;
	_make_dirty();
	update_gizmos();
}

CSGPolygon3D::PathIntervalType CSGPolygon3D::get_path_interval_type() const {
	return path_interval_type;
}

void CSGPolygon3D::set_path_interval(float p_interval) {
	path_interval = p_interval;
	_make_dirty();
	update_gizmos();
}

float CSGPolygon3D::get_path_interval() const {
	return path_interval;
}

void CSGPolygon3D::set_path_simplify_angle(float p_angle) {
	path_simplify_angle = p_angle;
	_make_dirty();
	update_gizmos();
}

float CSGPolygon3D::get_path_simplify_angle() const {
	return path_simplify_angle;
}

void CSGPolygon3D::set_path_rotation(PathRotation p_rotation) {
	path_rotation = p_rotation;
	_make_dirty();
	update_gizmos();
}

CSGPolygon3D::PathRotation CSGPolygon3D::get_path_rotation() const {
	return path_rotation;
}

void CSGPolygon3D::set_path_rotation_accurate(bool p_enabled) {
	path_rotation_accurate = p_enabled;
	_make_dirty();
	update_gizmos();
}

bool CSGPolygon3D::get_path_rotation_accurate() const {
	return path_rotation_accurate;
}

void CSGPolygon3D::set_path_local(bool p_enable) {
	path_local = p_enable;
	_make_dirty();
	update_gizmos();
}

bool CSGPolygon3D::is_path_local() const {
	return path_local;
}

void CSGPolygon3D::set_path_joined(bool p_enable) {
	path_joined = p_enable;
	_make_dirty();
	update_gizmos();
}

bool CSGPolygon3D::is_path_joined() const {
	return path_joined;
}

void CSGPolygon3D::set_smooth_faces(const bool p_smooth_faces) {
	smooth_faces = p_smooth_faces;
	_make_dirty();
}

bool CSGPolygon3D::get_smooth_faces() const {
	return smooth_faces;
}

void CSGPolygon3D::set_material(const Ref<Material> &p_material) {
	material = p_material;
	_make_dirty();
}

Ref<Material> CSGPolygon3D::get_material() const {
	return material;
}

bool CSGPolygon3D::_is_editable_3d_polygon() const {
	return true;
}

bool CSGPolygon3D::_has_editable_3d_polygon_no_depth() const {
	return true;
}

CSGPolygon3D::CSGPolygon3D() {
	// defaults
	mode = MODE_DEPTH;
	polygon.push_back(Vector2(0, 0));
	polygon.push_back(Vector2(0, 1));
	polygon.push_back(Vector2(1, 1));
	polygon.push_back(Vector2(1, 0));
	depth = 1.0;
	spin_degrees = 360;
	spin_sides = 8;
	smooth_faces = false;
	path_interval_type = PATH_INTERVAL_DISTANCE;
	path_interval = 1.0;
	path_simplify_angle = 0.0;
	path_rotation = PATH_ROTATION_PATH_FOLLOW;
	path_rotation_accurate = false;
	path_local = false;
	path_continuous_u = true;
	path_u_distance = 1.0;
	path_joined = false;
	path = nullptr;
}
