/**************************************************************************/
/*  csg_geometry_data.cpp                                                 */
/**************************************************************************/

#include "csg_geometry_data.h"

#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "core/templates/hash_map.h"

namespace {

struct GeometryEdge {
	int vertices[2] = { -1, -1 };
	int faces[2] = { -1, -1 };
	int use_count = 0;
};

static uint64_t _edge_key(int p_a, int p_b) {
	const uint32_t lo = MIN(p_a, p_b);
	const uint32_t hi = MAX(p_a, p_b);
	return (uint64_t(lo) << 32) | hi;
}

static Vector3i _vertex_cell(const Vector3 &p_position, real_t p_epsilon) {
	return Vector3i(
			Math::floor(p_position.x / p_epsilon),
			Math::floor(p_position.y / p_epsilon),
			Math::floor(p_position.z / p_epsilon));
}

} // namespace

void CSGGeometryData::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_vertex_count"), &CSGGeometryData::get_vertex_count);
	ClassDB::bind_method(D_METHOD("get_face_count"), &CSGGeometryData::get_face_count);
	ClassDB::bind_method(D_METHOD("get_edge_count"), &CSGGeometryData::get_edge_count);
	ClassDB::bind_method(D_METHOD("get_vertices"), &CSGGeometryData::get_vertices);
	ClassDB::bind_method(D_METHOD("get_triangle_vertices"), &CSGGeometryData::get_triangle_vertices);
	ClassDB::bind_method(D_METHOD("get_vertex_faces"), &CSGGeometryData::get_vertex_faces);
	ClassDB::bind_method(D_METHOD("get_vertex_adjacency"), &CSGGeometryData::get_vertex_adjacency);
	ClassDB::bind_method(D_METHOD("get_face_adjacency"), &CSGGeometryData::get_face_adjacency);
	ClassDB::bind_method(D_METHOD("get_face_edges"), &CSGGeometryData::get_face_edges);
	ClassDB::bind_method(D_METHOD("get_face_ids"), &CSGGeometryData::get_face_ids);
	ClassDB::bind_method(D_METHOD("get_source_face_ids"), &CSGGeometryData::get_source_face_ids);
	ClassDB::bind_method(D_METHOD("get_surface_ids"), &CSGGeometryData::get_surface_ids);
	ClassDB::bind_method(D_METHOD("get_material_ids"), &CSGGeometryData::get_material_ids);
	ClassDB::bind_method(D_METHOD("get_brush_ids"), &CSGGeometryData::get_brush_ids);
	ClassDB::bind_method(D_METHOD("get_face_generation"), &CSGGeometryData::get_face_generation);
	ClassDB::bind_method(D_METHOD("get_face_semantics"), &CSGGeometryData::get_face_semantics);
	ClassDB::bind_method(D_METHOD("get_face_custom_metadata"), &CSGGeometryData::get_face_custom_metadata);
	ClassDB::bind_method(D_METHOD("get_edge_vertices"), &CSGGeometryData::get_edge_vertices);
	ClassDB::bind_method(D_METHOD("get_edge_faces"), &CSGGeometryData::get_edge_faces);
	ClassDB::bind_method(D_METHOD("get_boundary_edges"), &CSGGeometryData::get_boundary_edges);
	ClassDB::bind_method(D_METHOD("get_sharp_edges"), &CSGGeometryData::get_sharp_edges);
	ClassDB::bind_method(D_METHOD("get_edge_sharpness"), &CSGGeometryData::get_edge_sharpness);

	BIND_ENUM_CONSTANT(FACE_ORIGINAL);
	BIND_ENUM_CONSTANT(FACE_BOOLEAN_GENERATED);
	BIND_ENUM_CONSTANT(FACE_BEVEL_GENERATED);
	BIND_ENUM_CONSTANT(FACE_TOPOLOGY_GENERATED);
}

void CSGGeometryData::build(const CSGBrush &p_brush, real_t p_merge_epsilon, real_t p_sharp_angle) {
	const real_t merge_epsilon = MAX(p_merge_epsilon, real_t(0.00000001));
	const real_t merge_epsilon_squared = merge_epsilon * merge_epsilon;
	HashMap<Vector3i, Vector<int>> vertex_cells;
	Vector<Vector<int>> vertex_face_lists;
	Vector<Vector<int>> vertex_neighbor_lists;
	Vector<Vector3> face_normals;
	Vector<GeometryEdge> edges;
	HashMap<uint64_t, int> edge_map;

	vertices.clear();
	triangle_vertices.resize(p_brush.faces.size() * 3);
	face_normals.resize(p_brush.faces.size());
	face_adjacency.resize(p_brush.faces.size() * 3);
	face_adjacency.fill(-1);
	face_edges.resize(p_brush.faces.size() * 3);
	face_edges.fill(-1);
	face_ids.resize(p_brush.faces.size());
	source_face_ids.resize(p_brush.faces.size());
	surface_ids.resize(p_brush.faces.size());
	material_ids.resize(p_brush.faces.size());
	brush_ids.resize(p_brush.faces.size());
	face_generation.resize(p_brush.faces.size());
	face_semantics.resize(p_brush.faces.size());
	face_custom_metadata.resize(p_brush.faces.size());

	for (int face_i = 0; face_i < p_brush.faces.size(); face_i++) {
		const CSGBrush::Face &face = p_brush.faces[face_i];
		face_normals.write[face_i] = (face.vertices[1] - face.vertices[0]).cross(face.vertices[2] - face.vertices[0]).normalized();
		face_ids.set(face_i, face.metadata.face_id);
		source_face_ids.set(face_i, face.metadata.source_face_id);
		surface_ids.set(face_i, face.metadata.surface_id);
		material_ids.set(face_i, face.material);
		brush_ids.set(face_i, face.metadata.brush_id);
		face_generation.set(face_i, face.metadata.generation);
		face_semantics.set(face_i, String(face.metadata.semantic));
		face_custom_metadata[face_i] = face.metadata.custom;

		for (int corner = 0; corner < 3; corner++) {
			const Vector3 position = face.vertices[corner];
			const Vector3i cell = _vertex_cell(position, merge_epsilon);
			int geometry_vertex = -1;
			for (int x = -1; x <= 1 && geometry_vertex < 0; x++) {
				for (int y = -1; y <= 1 && geometry_vertex < 0; y++) {
					for (int z = -1; z <= 1 && geometry_vertex < 0; z++) {
						HashMap<Vector3i, Vector<int>>::ConstIterator candidates = vertex_cells.find(cell + Vector3i(x, y, z));
						if (!candidates) {
							continue;
						}
						for (int candidate : candidates->value) {
							if (vertices[candidate].distance_squared_to(position) <= merge_epsilon_squared) {
								geometry_vertex = candidate;
								break;
							}
						}
					}
				}
			}
			if (geometry_vertex < 0) {
				geometry_vertex = vertices.size();
				vertices.push_back(position);
				vertex_cells[cell].push_back(geometry_vertex);
				vertex_face_lists.resize(vertices.size());
				vertex_neighbor_lists.resize(vertices.size());
			}
			triangle_vertices.set(face_i * 3 + corner, geometry_vertex);
			vertex_face_lists.write[geometry_vertex].push_back(face_i);
		}
	}

	for (int face_i = 0; face_i < p_brush.faces.size(); face_i++) {
		for (int corner = 0; corner < 3; corner++) {
			const int a = triangle_vertices[face_i * 3 + corner];
			const int b = triangle_vertices[face_i * 3 + (corner + 1) % 3];
			if (a == b) {
				continue;
			}
			const uint64_t key = _edge_key(a, b);
			HashMap<uint64_t, int>::ConstIterator existing = edge_map.find(key);
			int edge_i;
			if (existing) {
				edge_i = existing->value;
			} else {
				edge_i = edges.size();
				GeometryEdge edge;
				edge.vertices[0] = MIN(a, b);
				edge.vertices[1] = MAX(a, b);
				edges.push_back(edge);
				edge_map.insert(key, edge_i);
				vertex_neighbor_lists.write[a].push_back(b);
				vertex_neighbor_lists.write[b].push_back(a);
			}
			GeometryEdge &edge = edges.write[edge_i];
			if (edge.use_count < 2) {
				edge.faces[edge.use_count] = face_i;
			}
			edge.use_count++;
			face_edges.set(face_i * 3 + corner, edge_i);
		}
	}

	edge_vertices.resize(edges.size() * 2);
	edge_faces.resize(edges.size() * 2);
	boundary_edges.resize(edges.size());
	sharp_edges.resize(edges.size());
	edge_sharpness.resize(edges.size());
	const real_t sharp_angle = CLAMP(p_sharp_angle, real_t(0.0), real_t(Math::TAU));
	for (int edge_i = 0; edge_i < edges.size(); edge_i++) {
		const GeometryEdge &edge = edges[edge_i];
		edge_vertices.set(edge_i * 2, edge.vertices[0]);
		edge_vertices.set(edge_i * 2 + 1, edge.vertices[1]);
		edge_faces.set(edge_i * 2, edge.faces[0]);
		edge_faces.set(edge_i * 2 + 1, edge.faces[1]);
		const bool boundary = edge.use_count != 2;
		real_t angle = 0.0;
		if (!boundary) {
			angle = Math::acos(CLAMP(face_normals[edge.faces[0]].dot(face_normals[edge.faces[1]]), real_t(-1.0), real_t(1.0)));
			for (int side = 0; side < 2; side++) {
				const int face_i = edge.faces[side];
				for (int corner = 0; corner < 3; corner++) {
					if (face_edges[face_i * 3 + corner] == edge_i) {
						face_adjacency.set(face_i * 3 + corner, edge.faces[1 - side]);
					}
				}
			}
		}
		boundary_edges.set(edge_i, boundary ? 1 : 0);
		sharp_edges.set(edge_i, boundary || angle >= sharp_angle ? 1 : 0);
		edge_sharpness.set(edge_i, angle);
	}

	vertex_faces.resize(vertex_face_lists.size());
	vertex_adjacency.resize(vertex_neighbor_lists.size());
	for (int vertex_i = 0; vertex_i < vertex_face_lists.size(); vertex_i++) {
		PackedInt32Array faces_array;
		for (int face_i : vertex_face_lists[vertex_i]) {
			faces_array.push_back(face_i);
		}
		PackedInt32Array adjacency_array;
		for (int neighbor : vertex_neighbor_lists[vertex_i]) {
			adjacency_array.push_back(neighbor);
		}
		vertex_faces[vertex_i] = faces_array;
		vertex_adjacency[vertex_i] = adjacency_array;
	}
}

int CSGGeometryData::get_vertex_count() const {
	return vertices.size();
}
int CSGGeometryData::get_face_count() const {
	return face_ids.size();
}
int CSGGeometryData::get_edge_count() const {
	return boundary_edges.size();
}
PackedVector3Array CSGGeometryData::get_vertices() const {
	return vertices;
}
PackedInt32Array CSGGeometryData::get_triangle_vertices() const {
	return triangle_vertices;
}
Array CSGGeometryData::get_vertex_faces() const {
	return vertex_faces;
}
Array CSGGeometryData::get_vertex_adjacency() const {
	return vertex_adjacency;
}
PackedInt32Array CSGGeometryData::get_face_adjacency() const {
	return face_adjacency;
}
PackedInt32Array CSGGeometryData::get_face_edges() const {
	return face_edges;
}
PackedInt64Array CSGGeometryData::get_face_ids() const {
	return face_ids;
}
PackedInt64Array CSGGeometryData::get_source_face_ids() const {
	return source_face_ids;
}
PackedInt32Array CSGGeometryData::get_surface_ids() const {
	return surface_ids;
}
PackedInt32Array CSGGeometryData::get_material_ids() const {
	return material_ids;
}
PackedInt32Array CSGGeometryData::get_brush_ids() const {
	return brush_ids;
}
PackedByteArray CSGGeometryData::get_face_generation() const {
	return face_generation;
}
PackedStringArray CSGGeometryData::get_face_semantics() const {
	return face_semantics;
}
Array CSGGeometryData::get_face_custom_metadata() const {
	return face_custom_metadata;
}
PackedInt32Array CSGGeometryData::get_edge_vertices() const {
	return edge_vertices;
}
PackedInt32Array CSGGeometryData::get_edge_faces() const {
	return edge_faces;
}
PackedByteArray CSGGeometryData::get_boundary_edges() const {
	return boundary_edges;
}
PackedByteArray CSGGeometryData::get_sharp_edges() const {
	return sharp_edges;
}
PackedFloat32Array CSGGeometryData::get_edge_sharpness() const {
	return edge_sharpness;
}
