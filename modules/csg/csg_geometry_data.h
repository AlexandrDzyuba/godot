/**************************************************************************/
/*  csg_geometry_data.h                                                   */
/**************************************************************************/

#pragma once

#include "csg.h"

#include "core/object/ref_counted.h"
#include "core/variant/array.h"

class CSGGeometryData : public RefCounted {
	GDCLASS(CSGGeometryData, RefCounted);

public:
	enum FaceGeneration {
		FACE_ORIGINAL = CSGBrush::FACE_ORIGINAL,
		FACE_BOOLEAN_GENERATED = CSGBrush::FACE_BOOLEAN_GENERATED,
		FACE_BEVEL_GENERATED = CSGBrush::FACE_BEVEL_GENERATED,
		FACE_TOPOLOGY_GENERATED = CSGBrush::FACE_TOPOLOGY_GENERATED,
	};

private:
	PackedVector3Array vertices;
	PackedInt32Array triangle_vertices;
	Array vertex_faces;
	Array vertex_adjacency;

	PackedInt32Array face_adjacency;
	PackedInt32Array face_edges;
	PackedInt64Array face_ids;
	PackedInt64Array source_face_ids;
	PackedInt32Array surface_ids;
	PackedInt32Array material_ids;
	PackedInt32Array brush_ids;
	PackedByteArray face_generation;
	PackedStringArray face_semantics;
	Array face_custom_metadata;

	PackedInt32Array edge_vertices;
	PackedInt32Array edge_faces;
	PackedByteArray boundary_edges;
	PackedByteArray sharp_edges;
	PackedFloat32Array edge_sharpness;

protected:
	static void _bind_methods();

public:
	void build(const CSGBrush &p_brush, real_t p_merge_epsilon, real_t p_sharp_angle);

	int get_vertex_count() const;
	int get_face_count() const;
	int get_edge_count() const;

	PackedVector3Array get_vertices() const;
	PackedInt32Array get_triangle_vertices() const;
	Array get_vertex_faces() const;
	Array get_vertex_adjacency() const;
	PackedInt32Array get_face_adjacency() const;
	PackedInt32Array get_face_edges() const;

	PackedInt64Array get_face_ids() const;
	PackedInt64Array get_source_face_ids() const;
	PackedInt32Array get_surface_ids() const;
	PackedInt32Array get_material_ids() const;
	PackedInt32Array get_brush_ids() const;
	PackedByteArray get_face_generation() const;
	PackedStringArray get_face_semantics() const;
	Array get_face_custom_metadata() const;

	PackedInt32Array get_edge_vertices() const;
	PackedInt32Array get_edge_faces() const;
	PackedByteArray get_boundary_edges() const;
	PackedByteArray get_sharp_edges() const;
	PackedFloat32Array get_edge_sharpness() const;
};

VARIANT_ENUM_CAST(CSGGeometryData::FaceGeneration);
