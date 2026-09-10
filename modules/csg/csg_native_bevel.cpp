/**************************************************************************/
/*  csg_native_bevel.cpp                                                  */
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

#include "csg_native_bevel.h"

#include "core/string/print_string.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"

namespace {

struct NativeFace {
	int vertices[3];
	uint64_t property_vertices[3];
	Vector3 normal;
	uint32_t original_id = uint32_t(-1);
};

struct NativeEdge {
	int vertices[2];
	Vector<int> faces;
	bool selected = false;
};

struct NativePoint {
	Vector3 position;
	Vector<double> properties;
};

struct NativeTriangle {
	NativePoint points[3];
	uint32_t original_id = uint32_t(-1);
	bool source_surface = false;
};

struct NativeCapPoint {
	NativePoint point;
	Vector<int> neighbors;
};

struct NativeEdgeUse {
	int count = 0;
	int direction_balance = 0;
};

struct NativeTriangleEdgeUse {
	int triangle = -1;
	int direction = 0;
};

struct NativeOrientationEdge {
	Vector<NativeTriangleEdgeUse> uses;
};

static uint64_t _edge_key(int p_a, int p_b);

static Vector3i _vertex_cell(const Vector3 &p_position, real_t p_epsilon) {
	return Vector3i(
			Math::floor(p_position.x / p_epsilon),
			Math::floor(p_position.y / p_epsilon),
			Math::floor(p_position.z / p_epsilon));
}

static int _weld_output_vertex(const Vector3 &p_position, real_t p_epsilon, HashMap<Vector3i, Vector<int>> &r_cells, Vector<Vector3> &r_vertices) {
	const Vector3i cell = _vertex_cell(p_position, p_epsilon);
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

static bool _orient_triangles(Vector<NativeTriangle> &r_triangles, real_t p_merge_epsilon, int &r_component_count, int &r_flipped_triangle_count, int &r_flipped_source_triangle_count, int &r_conflict_count) {
	HashMap<Vector3i, Vector<int>> vertex_cells;
	Vector<Vector3> vertices;
	Vector<int> triangle_vertices;
	triangle_vertices.resize(r_triangles.size() * 3);
	Vector<Vector<uint64_t>> triangle_edges;
	triangle_edges.resize(r_triangles.size());
	HashMap<uint64_t, NativeOrientationEdge> edges;

	for (int triangle_i = 0; triangle_i < r_triangles.size(); triangle_i++) {
		for (int corner = 0; corner < 3; corner++) {
			triangle_vertices.write[triangle_i * 3 + corner] = _weld_output_vertex(r_triangles[triangle_i].points[corner].position, p_merge_epsilon, vertex_cells, vertices);
		}
		for (int corner = 0; corner < 3; corner++) {
			const int from = triangle_vertices[triangle_i * 3 + corner];
			const int to = triangle_vertices[triangle_i * 3 + (corner + 1) % 3];
			if (from == to) {
				continue;
			}
			const uint64_t edge_key = _edge_key(from, to);
			NativeTriangleEdgeUse use;
			use.triangle = triangle_i;
			use.direction = from < to ? 1 : -1;
			edges[edge_key].uses.push_back(use);
			triangle_edges.write[triangle_i].push_back(edge_key);
		}
	}

	Vector<int8_t> orientations;
	orientations.resize(r_triangles.size());
	orientations.fill(0);
	for (int triangle_i = 0; triangle_i < r_triangles.size(); triangle_i++) {
		if (orientations[triangle_i] != 0) {
			continue;
		}
		r_component_count++;
		orientations.write[triangle_i] = 1;
		Vector<int> pending;
		Vector<int> component_triangles;
		pending.push_back(triangle_i);
		component_triangles.push_back(triangle_i);
		while (!pending.is_empty()) {
			const int current_triangle = pending[pending.size() - 1];
			pending.resize(pending.size() - 1);
			for (uint64_t edge_key : triangle_edges[current_triangle]) {
				const NativeOrientationEdge &edge = edges[edge_key];
				if (edge.uses.size() != 2) {
					continue;
				}
				const NativeTriangleEdgeUse &current_use = edge.uses[edge.uses[0].triangle == current_triangle ? 0 : 1];
				const NativeTriangleEdgeUse &neighbor_use = edge.uses[edge.uses[0].triangle == current_triangle ? 1 : 0];
				const int8_t required_orientation = -current_use.direction * orientations[current_triangle] * neighbor_use.direction;
				if (orientations[neighbor_use.triangle] == 0) {
					orientations.write[neighbor_use.triangle] = required_orientation;
					pending.push_back(neighbor_use.triangle);
					component_triangles.push_back(neighbor_use.triangle);
				} else if (orientations[neighbor_use.triangle] != required_orientation) {
					r_conflict_count++;
				}
			}
		}

		int source_orientation_score = 0;
		for (int component_triangle : component_triangles) {
			if (r_triangles[component_triangle].source_surface) {
				source_orientation_score += orientations[component_triangle];
			}
		}
		if (source_orientation_score < 0) {
			for (int component_triangle : component_triangles) {
				orientations.write[component_triangle] = -orientations[component_triangle];
			}
		}
	}

	if (r_conflict_count != 0) {
		return false;
	}
	for (int triangle_i = 0; triangle_i < r_triangles.size(); triangle_i++) {
		if (orientations[triangle_i] < 0) {
			SWAP(r_triangles.write[triangle_i].points[1], r_triangles.write[triangle_i].points[2]);
			r_flipped_triangle_count++;
			r_flipped_source_triangle_count += r_triangles[triangle_i].source_surface ? 1 : 0;
		}
	}
	return true;
}

static const char *_status_name(manifold::Manifold::Error p_status) {
	switch (p_status) {
		case manifold::Manifold::Error::NoError:
			return "NoError";
		case manifold::Manifold::Error::NonFiniteVertex:
			return "NonFiniteVertex";
		case manifold::Manifold::Error::NotManifold:
			return "NotManifold";
		case manifold::Manifold::Error::VertexOutOfBounds:
			return "VertexOutOfBounds";
		case manifold::Manifold::Error::PropertiesWrongLength:
			return "PropertiesWrongLength";
		case manifold::Manifold::Error::MissingPositionProperties:
			return "MissingPositionProperties";
		case manifold::Manifold::Error::MergeVectorsDifferentLengths:
			return "MergeVectorsDifferentLengths";
		case manifold::Manifold::Error::MergeIndexOutOfBounds:
			return "MergeIndexOutOfBounds";
		case manifold::Manifold::Error::TransformWrongLength:
			return "TransformWrongLength";
		case manifold::Manifold::Error::RunIndexWrongLength:
			return "RunIndexWrongLength";
		case manifold::Manifold::Error::FaceIDWrongLength:
			return "FaceIDWrongLength";
		case manifold::Manifold::Error::InvalidConstruction:
			return "InvalidConstruction";
		case manifold::Manifold::Error::ResultTooLarge:
			return "ResultTooLarge";
	}
	return "Unknown";
}

static uint64_t _edge_key(int p_a, int p_b) {
	const uint32_t a = MIN(p_a, p_b);
	const uint32_t b = MAX(p_a, p_b);
	return (uint64_t(a) << 32) | b;
}

static int _find_root(Vector<int> &r_parents, int p_vertex) {
	int root = p_vertex;
	while (r_parents[root] != root) {
		root = r_parents[root];
	}
	while (r_parents[p_vertex] != p_vertex) {
		const int parent = r_parents[p_vertex];
		r_parents.write[p_vertex] = root;
		p_vertex = parent;
	}
	return root;
}

static void _join(Vector<int> &r_parents, int p_a, int p_b) {
	const int root_a = _find_root(r_parents, p_a);
	const int root_b = _find_root(r_parents, p_b);
	if (root_a != root_b) {
		r_parents.write[root_b] = root_a;
	}
}

static bool _has_region_pair(const NativeEdge &p_edge, int p_region_a, int p_region_b, const Vector<int> &p_face_regions) {
	if (p_edge.faces.size() != 2) {
		return false;
	}
	const int edge_region_a = p_face_regions[p_edge.faces[0]];
	const int edge_region_b = p_face_regions[p_edge.faces[1]];
	return (edge_region_a == p_region_a && edge_region_b == p_region_b) || (edge_region_a == p_region_b && edge_region_b == p_region_a);
}

static real_t _get_edge_run_length(uint64_t p_edge_key, const HashMap<uint64_t, NativeEdge> &p_edges, const Vector<Vector<uint64_t>> &p_vertex_edges, const Vector<Vector3> &p_vertices, const Vector<int> &p_face_regions) {
	const NativeEdge &initial_edge = p_edges[p_edge_key];
	const int region_a = p_face_regions[initial_edge.faces[0]];
	const int region_b = p_face_regions[initial_edge.faces[1]];
	real_t run_length = p_vertices[initial_edge.vertices[0]].distance_to(p_vertices[initial_edge.vertices[1]]);

	for (int side = 0; side < 2; side++) {
		const int previous_vertex = initial_edge.vertices[1 - side];
		int current_vertex = initial_edge.vertices[side];
		const Vector3 run_direction = (p_vertices[current_vertex] - p_vertices[previous_vertex]).normalized();
		uint64_t previous_edge_key = p_edge_key;

		for (uint32_t step = 0; step < p_edges.size(); step++) {
			uint64_t next_edge_key = 0;
			int next_vertex = -1;
			for (uint64_t candidate_key : p_vertex_edges[current_vertex]) {
				if (candidate_key == previous_edge_key) {
					continue;
				}
				const NativeEdge &candidate = p_edges[candidate_key];
				if (!candidate.selected || !_has_region_pair(candidate, region_a, region_b, p_face_regions)) {
					continue;
				}
				const int candidate_vertex = candidate.vertices[0] == current_vertex ? candidate.vertices[1] : candidate.vertices[0];
				const Vector3 candidate_direction = (p_vertices[candidate_vertex] - p_vertices[current_vertex]).normalized();
				if (candidate_direction.dot(run_direction) > 1.0 - CMP_EPSILON * 10.0) {
					next_edge_key = candidate_key;
					next_vertex = candidate_vertex;
					break;
				}
			}
			if (next_vertex < 0) {
				break;
			}
			run_length += p_vertices[current_vertex].distance_to(p_vertices[next_vertex]);
			current_vertex = next_vertex;
			previous_edge_key = next_edge_key;
		}
	}
	return run_length;
}

static bool _same_plane(const NativeFace &p_a, const NativeFace &p_b, const Vector<Vector3> &p_vertices, real_t p_tolerance) {
	if (p_a.normal.dot(p_b.normal) < 1.0 - CMP_EPSILON * 10.0) {
		return false;
	}
	return Math::abs(p_a.normal.dot(p_vertices[p_b.vertices[0]] - p_vertices[p_a.vertices[0]])) <= p_tolerance;
}

static int _find_corner(const NativeFace &p_face, int p_vertex) {
	for (int i = 0; i < 3; i++) {
		if (p_face.vertices[i] == p_vertex) {
			return i;
		}
	}
	return -1;
}

static NativePoint _make_point(const manifold::MeshGL64 &p_mesh, const NativeFace &p_face, const Vector<Vector3> &p_vertices, const Vector3 &p_position) {
	NativePoint point;
	point.position = p_position;
	const int property_count = p_mesh.numProp - 3;
	point.properties.resize(property_count);

	const Vector3 v0 = p_vertices[p_face.vertices[1]] - p_vertices[p_face.vertices[0]];
	const Vector3 v1 = p_vertices[p_face.vertices[2]] - p_vertices[p_face.vertices[0]];
	const Vector3 v2 = p_position - p_vertices[p_face.vertices[0]];
	const real_t d00 = v0.dot(v0);
	const real_t d01 = v0.dot(v1);
	const real_t d11 = v1.dot(v1);
	const real_t d20 = v2.dot(v0);
	const real_t d21 = v2.dot(v1);
	const real_t denominator = d00 * d11 - d01 * d01;
	double weights[3] = { 1.0, 0.0, 0.0 };
	if (!Math::is_zero_approx(denominator)) {
		weights[1] = (d11 * d20 - d01 * d21) / denominator;
		weights[2] = (d00 * d21 - d01 * d20) / denominator;
		weights[0] = 1.0 - weights[1] - weights[2];
	}
	for (int property = 0; property < property_count; property++) {
		double value = 0.0;
		for (int corner = 0; corner < 3; corner++) {
			const size_t offset = p_face.property_vertices[corner] * p_mesh.numProp + 3 + property;
			value += p_mesh.vertProperties[offset] * weights[corner];
		}
		point.properties.write[property] = value;
	}
	return point;
}

static bool _append_ordered_triangle(Vector<NativeTriangle> &r_triangles, const NativePoint &p_a, const NativePoint &p_b, const NativePoint &p_c, uint32_t p_original_id, bool p_source_surface = false) {
	const Vector3 normal = (p_b.position - p_a.position).cross(p_c.position - p_a.position);
	if (normal.length_squared() == 0.0) {
		return false;
	}
	NativeTriangle triangle;
	triangle.original_id = p_original_id;
	triangle.source_surface = p_source_surface;
	triangle.points[0] = p_a;
	triangle.points[1] = p_b;
	triangle.points[2] = p_c;
	r_triangles.push_back(triangle);
	return true;
}

static bool _append_triangle(Vector<NativeTriangle> &r_triangles, const NativePoint &p_a, const NativePoint &p_b, const NativePoint &p_c, uint32_t p_original_id, const Vector3 &p_expected_normal, bool p_source_surface = false) {
	const Vector3 normal = (p_b.position - p_a.position).cross(p_c.position - p_a.position);
	if (normal.dot(p_expected_normal) >= 0.0) {
		return _append_ordered_triangle(r_triangles, p_a, p_b, p_c, p_original_id, p_source_surface);
	}
	return _append_ordered_triangle(r_triangles, p_a, p_c, p_b, p_original_id, p_source_surface);
}

static NativePoint _average_points(const Vector<NativePoint> &p_points) {
	NativePoint result;
	if (p_points.is_empty()) {
		return result;
	}
	result.properties.resize(p_points[0].properties.size());
	for (const NativePoint &point : p_points) {
		result.position += point.position;
		for (int i = 0; i < result.properties.size(); i++) {
			result.properties.write[i] += point.properties[i];
		}
	}
	result.position /= p_points.size();
	for (int i = 0; i < result.properties.size(); i++) {
		result.properties.write[i] /= p_points.size();
	}
	return result;
}

} // namespace

bool csg_build_native_bevel(const manifold::Manifold &p_source, const Ref<CSGBevelSettings> &p_settings, manifold::Manifold &r_result) {
	ERR_FAIL_COND_V(p_settings.is_null(), false);
	const bool debug_print = p_settings->is_debug_printing();
	if (!p_settings->is_enabled() || p_settings->get_width() <= 0.0 || p_source.IsEmpty()) {
		if (debug_print) {
			print_line(vformat("[CSGBevel] skipped: enabled=%s width=%f source_empty=%s source_status=%s", p_settings->is_enabled(), p_settings->get_width(), p_source.IsEmpty(), _status_name(p_source.Status())));
		}
		r_result = p_source;
		return true;
	}

	const manifold::MeshGL64 mesh = p_source.GetMeshGL64();
	if (debug_print) {
		print_line(vformat("[CSGBevel] input: triangles=%d property_vertices=%d properties=%d merge_pairs=%d width=%f angle=%fdeg tolerance=%f status=%s", mesh.NumTri(), mesh.NumVert(), mesh.numProp, mesh.mergeFromVert.size(), p_settings->get_width(), Math::rad_to_deg(p_settings->get_angle_threshold()), mesh.tolerance, _status_name(p_source.Status())));
	}
	if (mesh.numProp < 3 || mesh.mergeFromVert.size() != mesh.mergeToVert.size()) {
		if (debug_print) {
			print_line(vformat("[CSGBevel] invalid input arrays: numProp=%d mergeFrom=%d mergeTo=%d", mesh.numProp, mesh.mergeFromVert.size(), mesh.mergeToVert.size()));
		}
		return false;
	}
	const int property_vertex_count = mesh.NumVert();
	Vector<int> property_parents;
	property_parents.resize(property_vertex_count);
	for (int i = 0; i < property_vertex_count; i++) {
		property_parents.write[i] = i;
	}
	for (size_t i = 0; i < mesh.mergeFromVert.size(); i++) {
		ERR_FAIL_COND_V(mesh.mergeFromVert[i] >= uint64_t(property_vertex_count) || mesh.mergeToVert[i] >= uint64_t(property_vertex_count), false);
		_join(property_parents, mesh.mergeFromVert[i], mesh.mergeToVert[i]);
	}

	HashMap<int, int> root_to_vertex;
	Vector<Vector3> vertices;
	Vector<int> property_to_vertex;
	property_to_vertex.resize(property_vertex_count);
	for (int i = 0; i < property_vertex_count; i++) {
		const int root = _find_root(property_parents, i);
		HashMap<int, int>::ConstIterator found = root_to_vertex.find(root);
		if (found) {
			property_to_vertex.write[i] = found->value;
		} else {
			const int vertex = vertices.size();
			const size_t offset = size_t(i) * mesh.numProp;
			vertices.push_back(Vector3(mesh.vertProperties[offset], mesh.vertProperties[offset + 1], mesh.vertProperties[offset + 2]));
			root_to_vertex.insert(root, vertex);
			property_to_vertex.write[i] = vertex;
		}
	}

	const int face_count = mesh.NumTri();
	Vector<NativeFace> faces;
	faces.resize(face_count);
	Vector<Vector<int>> vertex_faces;
	vertex_faces.resize(vertices.size());
	HashMap<uint64_t, NativeEdge> edges;
	size_t run = 0;
	for (int face_i = 0; face_i < face_count; face_i++) {
		while (run + 1 < mesh.runIndex.size() && size_t(face_i * 3) >= mesh.runIndex[run + 1]) {
			run++;
		}
		NativeFace &face = faces.write[face_i];
		if (run < mesh.runOriginalID.size()) {
			face.original_id = mesh.runOriginalID[run];
		}
		for (int corner = 0; corner < 3; corner++) {
			const uint64_t property_vertex = mesh.triVerts[face_i * 3 + corner];
			ERR_FAIL_COND_V(property_vertex >= uint64_t(property_vertex_count), false);
			face.property_vertices[corner] = property_vertex;
			face.vertices[corner] = property_to_vertex[property_vertex];
			vertex_faces.write[face.vertices[corner]].push_back(face_i);
		}
		face.normal = (vertices[face.vertices[1]] - vertices[face.vertices[0]]).cross(vertices[face.vertices[2]] - vertices[face.vertices[0]]).normalized();
		for (int corner = 0; corner < 3; corner++) {
			const int a = face.vertices[corner];
			const int b = face.vertices[(corner + 1) % 3];
			if (a == b) {
				continue;
			}
			const uint64_t key = _edge_key(a, b);
			HashMap<uint64_t, NativeEdge>::Iterator edge = edges.find(key);
			if (edge) {
				edge->value.faces.push_back(face_i);
			} else {
				NativeEdge new_edge;
				new_edge.vertices[0] = MIN(a, b);
				new_edge.vertices[1] = MAX(a, b);
				new_edge.faces.push_back(face_i);
				edges.insert(key, new_edge);
			}
		}
	}

	Vector<int> face_regions;
	face_regions.resize(face_count);
	for (int i = 0; i < face_count; i++) {
		face_regions.write[i] = i;
	}
	const real_t tolerance = MAX(real_t(mesh.tolerance), real_t(CMP_EPSILON));
	for (const KeyValue<uint64_t, NativeEdge> &entry : edges) {
		const NativeEdge &edge = entry.value;
		if (edge.faces.size() == 2 && _same_plane(faces[edge.faces[0]], faces[edge.faces[1]], vertices, tolerance)) {
			_join(face_regions, edge.faces[0], edge.faces[1]);
		}
	}
	for (int i = 0; i < face_count; i++) {
		face_regions.write[i] = _find_root(face_regions, i);
	}

	Vector<Vector<uint64_t>> vertex_edges;
	vertex_edges.resize(vertices.size());
	const real_t dot_limit = Math::cos(p_settings->get_angle_threshold());
	int selected_edge_count = 0;
	int non_manifold_edge_count = 0;
	int coplanar_edge_count = 0;
	int below_threshold_edge_count = 0;
	int opposite_edge_count = 0;
	for (KeyValue<uint64_t, NativeEdge> &entry : edges) {
		NativeEdge &edge = entry.value;
		vertex_edges.write[edge.vertices[0]].push_back(entry.key);
		vertex_edges.write[edge.vertices[1]].push_back(entry.key);
		if (edge.faces.size() != 2) {
			non_manifold_edge_count++;
			continue;
		}
		if (face_regions[edge.faces[0]] == face_regions[edge.faces[1]]) {
			coplanar_edge_count++;
			continue;
		}
		const real_t dot = CLAMP(faces[edge.faces[0]].normal.dot(faces[edge.faces[1]].normal), -1.0, 1.0);
		if (dot <= -1.0 + CMP_EPSILON) {
			opposite_edge_count++;
			continue;
		}
		edge.selected = dot <= dot_limit;
		below_threshold_edge_count += edge.selected ? 0 : 1;
		selected_edge_count += edge.selected ? 1 : 0;
	}
	if (debug_print) {
		print_line(vformat("[CSGBevel] edges: total=%d selected=%d coplanar=%d below_threshold=%d boundary_or_nonmanifold=%d opposite=%d logical_vertices=%d", edges.size(), selected_edge_count, coplanar_edge_count, below_threshold_edge_count, non_manifold_edge_count, opposite_edge_count, vertices.size()));
	}
	if (selected_edge_count == 0) {
		if (debug_print) {
			print_line("[CSGBevel] unchanged: no geometric edges passed the angle threshold.");
		}
		r_result = p_source;
		return true;
	}

	HashMap<uint64_t, Vector3> region_positions;
	HashMap<uint64_t, real_t> edge_run_lengths;
	Vector<NativePoint> mapped_points;
	mapped_points.resize(face_count * 3);
	int width_clamp_count = 0;
	real_t minimum_effective_width = p_settings->get_width();
	for (int face_i = 0; face_i < face_count; face_i++) {
		const NativeFace &face = faces[face_i];
		for (int corner = 0; corner < 3; corner++) {
			const int vertex = face.vertices[corner];
			const int region = face_regions[face_i];
			const uint64_t key = (uint64_t(uint32_t(region)) << 32) | uint32_t(vertex);
			HashMap<uint64_t, Vector3>::ConstIterator cached = region_positions.find(key);
			if (!cached) {
				Vector<Vector3> constraints;
				real_t local_width = p_settings->get_width();
				for (uint64_t edge_key : vertex_edges[vertex]) {
					const NativeEdge &edge = edges[edge_key];
					if (!edge.selected) {
						continue;
					}
					int region_face = -1;
					for (int edge_face : edge.faces) {
						if (face_regions[edge_face] == region) {
							region_face = edge_face;
							break;
						}
					}
					if (region_face < 0) {
						continue;
					}
					const int other = edge.vertices[0] == vertex ? edge.vertices[1] : edge.vertices[0];
					const Vector3 direction = (vertices[other] - vertices[vertex]).normalized();
					Vector3 inward;
					for (int i = 0; i < 3; i++) {
						const int candidate = faces[region_face].vertices[i];
						if (candidate != vertex && candidate != other) {
							const Vector3 inside = vertices[candidate] - vertices[vertex];
							inward = (inside - direction * inside.dot(direction)).normalized();
							break;
						}
					}
					if (!inward.is_zero_approx()) {
						bool duplicate = false;
						for (const Vector3 &existing : constraints) {
							duplicate |= existing.dot(inward) > 1.0 - CMP_EPSILON * 10.0;
						}
						if (!duplicate) {
							constraints.push_back(inward);
						}
					}
					HashMap<uint64_t, real_t>::ConstIterator cached_run_length = edge_run_lengths.find(edge_key);
					if (!cached_run_length) {
						edge_run_lengths.insert(edge_key, _get_edge_run_length(edge_key, edges, vertex_edges, vertices, face_regions));
						cached_run_length = edge_run_lengths.find(edge_key);
					}
					const real_t maximum_width = cached_run_length->value * 0.49;
					if (maximum_width < local_width) {
						local_width = maximum_width;
						width_clamp_count++;
					}
				}
				minimum_effective_width = MIN(minimum_effective_width, local_width);

				Vector3 offset;
				if (constraints.size() == 1) {
					offset = constraints[0] * local_width;
				} else if (constraints.size() >= 2) {
					const Vector3 u = constraints[0];
					const Vector3 v = face.normal.cross(u).normalized();
					real_t a00 = 0.0, a01 = 0.0, a11 = 0.0, b0 = 0.0, b1 = 0.0;
					for (const Vector3 &constraint : constraints) {
						const real_t x = constraint.dot(u);
						const real_t y = constraint.dot(v);
						a00 += x * x;
						a01 += x * y;
						a11 += y * y;
						b0 += x * local_width;
						b1 += y * local_width;
					}
					const real_t determinant = a00 * a11 - a01 * a01;
					if (!Math::is_zero_approx(determinant)) {
						offset = u * ((b0 * a11 - b1 * a01) / determinant) + v * ((a00 * b1 - a01 * b0) / determinant);
					}
				}
				region_positions.insert(key, vertices[vertex] + offset);
				cached = region_positions.find(key);
			}
			mapped_points.write[face_i * 3 + corner] = _make_point(mesh, face, vertices, cached->value);
		}
	}

	Vector<NativeTriangle> triangles;
	triangles.reserve(face_count + selected_edge_count * 2);
	int exact_degenerate_triangle_count = 0;
	for (int face_i = 0; face_i < face_count; face_i++) {
		exact_degenerate_triangle_count += _append_triangle(triangles, mapped_points[face_i * 3], mapped_points[face_i * 3 + 1], mapped_points[face_i * 3 + 2], faces[face_i].original_id, faces[face_i].normal, true) ? 0 : 1;
	}
	for (const KeyValue<uint64_t, NativeEdge> &entry : edges) {
		const NativeEdge &edge = entry.value;
		if (!edge.selected) {
			continue;
		}
		const int face_a = edge.faces[0];
		const int face_b = edge.faces[1];
		const int a0 = _find_corner(faces[face_a], edge.vertices[0]);
		const int a1 = _find_corner(faces[face_a], edge.vertices[1]);
		const int b0 = _find_corner(faces[face_b], edge.vertices[0]);
		const int b1 = _find_corner(faces[face_b], edge.vertices[1]);
		const NativePoint &p0 = mapped_points[face_a * 3 + a0];
		const NativePoint &p1 = mapped_points[face_a * 3 + a1];
		const NativePoint &p2 = mapped_points[face_b * 3 + b1];
		const NativePoint &p3 = mapped_points[face_b * 3 + b0];

		// The bevel strip must traverse the shared boundary opposite to the
		// adjacent source face. Unlike a normal-based choice, this remains
		// unambiguous for both convex and concave Boolean edges.
		if ((a0 + 1) % 3 == a1) {
			exact_degenerate_triangle_count += _append_ordered_triangle(triangles, p1, p0, p3, faces[face_a].original_id) ? 0 : 1;
			exact_degenerate_triangle_count += _append_ordered_triangle(triangles, p1, p3, p2, faces[face_a].original_id) ? 0 : 1;
		} else {
			exact_degenerate_triangle_count += _append_ordered_triangle(triangles, p0, p1, p2, faces[face_a].original_id) ? 0 : 1;
			exact_degenerate_triangle_count += _append_ordered_triangle(triangles, p0, p2, p3, faces[face_a].original_id) ? 0 : 1;
		}
	}

	// The selected edge graph defines each vertex cap independently of the
	// triangulation produced by the preceding Boolean operation.
	int completed_cap_count = 0;
	int open_cap_count = 0;
	int straight_cap_count = 0;
	int invalid_cap_count = 0;
	for (int vertex = 0; vertex < vertices.size(); vertex++) {
		Vector<NativeCapPoint> cap_points;
		HashMap<int, int> region_to_cap;
		for (int face_i : vertex_faces[vertex]) {
			const int corner = _find_corner(faces[face_i], vertex);
			if (corner < 0) {
				continue;
			}
			const NativePoint &point = mapped_points[face_i * 3 + corner];
			if (point.position.is_equal_approx(vertices[vertex])) {
				continue;
			}
			const int region = face_regions[face_i];
			if (!region_to_cap.has(region)) {
				int cap = -1;
				for (int i = 0; i < cap_points.size(); i++) {
					if (cap_points[i].point.position.is_equal_approx(point.position)) {
						cap = i;
						break;
					}
				}
				if (cap < 0) {
					NativeCapPoint cap_point;
					cap_point.point = point;
					cap = cap_points.size();
					cap_points.push_back(cap_point);
				}
				region_to_cap.insert(region, cap);
			}
		}
		if (cap_points.size() < 2) {
			continue;
		}

		for (uint64_t edge_key : vertex_edges[vertex]) {
			const NativeEdge &edge = edges[edge_key];
			if (!edge.selected || edge.faces.size() != 2) {
				continue;
			}
			const int region_a = face_regions[edge.faces[0]];
			const int region_b = face_regions[edge.faces[1]];
			HashMap<int, int>::ConstIterator cap_a = region_to_cap.find(region_a);
			HashMap<int, int>::ConstIterator cap_b = region_to_cap.find(region_b);
			if (!cap_a || !cap_b || cap_a->value == cap_b->value) {
				continue;
			}
			cap_points.write[cap_a->value].neighbors.push_back(cap_b->value);
			cap_points.write[cap_b->value].neighbors.push_back(cap_a->value);
		}

		Vector3 expected_normal;
		HashSet<int> normal_regions;
		for (int face_i : vertex_faces[vertex]) {
			const int region = face_regions[face_i];
			if (!normal_regions.has(region)) {
				normal_regions.insert(region);
				expected_normal += faces[face_i].normal;
			}
		}
		expected_normal.normalize();

		if (cap_points.size() == 2 && cap_points[0].neighbors.size() == 2 && cap_points[1].neighbors.size() == 2) {
			straight_cap_count++;
			continue;
		}

		int first_endpoint = -1;
		int endpoint_count = 0;
		bool valid_boundary = true;
		for (int i = 0; i < cap_points.size(); i++) {
			const int neighbor_count = cap_points[i].neighbors.size();
			if (neighbor_count == 1) {
				first_endpoint = i;
				endpoint_count++;
			} else if (neighbor_count != 2) {
				valid_boundary = false;
				break;
			}
		}
		const bool open_boundary = endpoint_count == 2;
		if (!valid_boundary || (endpoint_count != 0 && !open_boundary)) {
			invalid_cap_count++;
			continue;
		}

		Vector<int> ordered_points;
		ordered_points.reserve(cap_points.size());
		int previous = -1;
		int current = open_boundary ? first_endpoint : 0;
		const int first = current;
		while (ordered_points.size() < cap_points.size()) {
			ordered_points.push_back(current);
			if (open_boundary && current != first && cap_points[current].neighbors.size() == 1) {
				break;
			}
			const Vector<int> &neighbors = cap_points[current].neighbors;
			int next = neighbors[0];
			if (next == previous && neighbors.size() == 2) {
				next = neighbors[1];
			}
			previous = current;
			current = next;
			if (!open_boundary && current == first) {
				break;
			}
		}
		if (ordered_points.size() != cap_points.size() || (!open_boundary && current != first)) {
			invalid_cap_count++;
			continue;
		}

		Vector<NativePoint> boundary_points;
		boundary_points.reserve(ordered_points.size() + (open_boundary ? 1 : 0));
		for (int point_index : ordered_points) {
			boundary_points.push_back(cap_points[point_index].point);
		}
		if (open_boundary) {
			open_cap_count++;
			boundary_points.push_back(_make_point(mesh, faces[vertex_faces[vertex][0]], vertices, vertices[vertex]));
		}
		completed_cap_count++;

		if (boundary_points.size() == 3) {
			exact_degenerate_triangle_count += _append_triangle(triangles, boundary_points[0], boundary_points[1], boundary_points[2], faces[vertex_faces[vertex][0]].original_id, expected_normal) ? 0 : 1;
			continue;
		}
		const NativePoint center = _average_points(boundary_points);
		for (int i = 0; i < boundary_points.size(); i++) {
			exact_degenerate_triangle_count += _append_triangle(triangles, center, boundary_points[i], boundary_points[(i + 1) % boundary_points.size()], faces[vertex_faces[vertex][0]].original_id, expected_normal) ? 0 : 1;
		}
	}

	const real_t output_merge_epsilon = MAX(real_t(mesh.tolerance), real_t(0.00000001));
	int orientation_component_count = 0;
	int flipped_triangle_count = 0;
	int flipped_source_triangle_count = 0;
	int orientation_conflict_count = 0;
	const bool orientation_valid = _orient_triangles(triangles, output_merge_epsilon, orientation_component_count, flipped_triangle_count, flipped_source_triangle_count, orientation_conflict_count);
	if (debug_print) {
		print_line(vformat("[CSGBevel] orientation repair: components=%d flipped_triangles=%d flipped_source_triangles=%d conflicts=%d valid=%s", orientation_component_count, flipped_triangle_count, flipped_source_triangle_count, orientation_conflict_count, orientation_valid));
	}
	if (!orientation_valid) {
		return false;
	}

	HashMap<uint32_t, Vector<NativeTriangle>> triangles_by_original;
	for (const NativeTriangle &triangle : triangles) {
		triangles_by_original[triangle.original_id].push_back(triangle);
	}
	Vector<uint32_t> original_ids;
	original_ids.reserve(triangles_by_original.size());
	for (const KeyValue<uint32_t, Vector<NativeTriangle>> &entry : triangles_by_original) {
		original_ids.push_back(entry.key);
	}
	original_ids.sort();

	manifold::MeshGL64 output;
	output.numProp = mesh.numProp;
	output.tolerance = mesh.tolerance;
	HashMap<Vector3i, Vector<int>> output_vertex_cells;
	Vector<Vector3> output_vertices;
	Vector<uint64_t> first_property_vertices;
	HashMap<uint64_t, NativeEdgeUse> output_edge_uses;
	for (uint32_t original_id : original_ids) {
		output.runIndex.push_back(output.triVerts.size());
		output.runOriginalID.push_back(original_id);
		for (const NativeTriangle &triangle : triangles_by_original[original_id]) {
			int geometry_vertices[3];
			for (int corner = 0; corner < 3; corner++) {
				const uint64_t property_vertex = output.vertProperties.size() / output.numProp;
				output.triVerts.push_back(property_vertex);
				const NativePoint &point = triangle.points[corner];
				output.vertProperties.push_back(point.position.x);
				output.vertProperties.push_back(point.position.y);
				output.vertProperties.push_back(point.position.z);
				for (double property : point.properties) {
					output.vertProperties.push_back(property);
				}

				const int previous_vertex_count = output_vertices.size();
				const int geometry_vertex = _weld_output_vertex(point.position, output_merge_epsilon, output_vertex_cells, output_vertices);
				geometry_vertices[corner] = geometry_vertex;
				if (output_vertices.size() != previous_vertex_count) {
					first_property_vertices.push_back(property_vertex);
				} else {
					output.mergeFromVert.push_back(property_vertex);
					output.mergeToVert.push_back(first_property_vertices[geometry_vertex]);
				}
			}
			for (int corner = 0; corner < 3; corner++) {
				const int from = geometry_vertices[corner];
				const int to = geometry_vertices[(corner + 1) % 3];
				NativeEdgeUse &edge_use = output_edge_uses[_edge_key(from, to)];
				edge_use.count++;
				edge_use.direction_balance += from < to ? 1 : -1;
			}
		}
	}
	output.runIndex.push_back(output.triVerts.size());
	int boundary_edge_count = 0;
	int overused_edge_count = 0;
	int orientation_error_count = 0;
	for (const KeyValue<uint64_t, NativeEdgeUse> &entry : output_edge_uses) {
		boundary_edge_count += entry.value.count == 1 ? 1 : 0;
		overused_edge_count += entry.value.count > 2 ? 1 : 0;
		orientation_error_count += entry.value.count == 2 && entry.value.direction_balance != 0 ? 1 : 0;
	}
	r_result = manifold::Manifold(output);
	const bool valid_result = r_result.Status() == manifold::Manifold::Error::NoError && !r_result.IsEmpty();
	if (debug_print) {
		print_line(vformat("[CSGBevel] caps: completed=%d open=%d straight_continuations=%d invalid=%d; width_clamps=%d minimum_width=%f exact_degenerate_triangles=%d", completed_cap_count, open_cap_count, straight_cap_count, invalid_cap_count, width_clamp_count, minimum_effective_width, exact_degenerate_triangle_count));
		print_line(vformat("[CSGBevel] output topology: physical_vertices=%d edges=%d boundary=%d overused=%d orientation_errors=%d merge_epsilon=%f", output_vertices.size(), output_edge_uses.size(), boundary_edge_count, overused_edge_count, orientation_error_count, output_merge_epsilon));
		print_line(vformat("[CSGBevel] output: generated_triangles=%d property_vertices=%d explicit_merge_pairs=%d status=%s empty=%s accepted=%s", output.NumTri(), output.NumVert(), output.mergeFromVert.size(), _status_name(r_result.Status()), r_result.IsEmpty(), valid_result));
	}
	return valid_result;
}
