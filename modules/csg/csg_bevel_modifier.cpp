/**************************************************************************/
/*  csg_bevel_modifier.cpp                                                */
/**************************************************************************/

#include "csg_bevel_modifier.h"

#include "core/math/math_funcs.h"
#include "core/object/class_db.h"
#include "core/templates/hash_map.h"

namespace {

struct BevelFace {
	int vertices[3];
	Vector3 normal;
};

struct BevelEdge {
	int vertices[2];
	Vector<int> faces;
	bool selected = false;
};

struct BevelPoint {
	Vector3 position;
	Vector2 uv;
	Color color;
	Vector4 customs[CSGBrush::CUSTOM_CHANNEL_COUNT];
};

struct CapPoint {
	BevelPoint point;
	Vector<int> neighbors;
};

struct BevelEdgeUse {
	int count = 0;
	int direction_balance = 0;
};

static uint64_t _edge_key(int p_a, int p_b) {
	const uint32_t a = MIN(p_a, p_b);
	const uint32_t b = MAX(p_a, p_b);
	return (uint64_t(a) << 32) | b;
}

static constexpr real_t BEVEL_MERGE_EPSILON = 0.00001;

static Vector3i _vertex_cell(const Vector3 &p_position, real_t p_epsilon) {
	return Vector3i(
			Math::floor(p_position.x / p_epsilon),
			Math::floor(p_position.y / p_epsilon),
			Math::floor(p_position.z / p_epsilon));
}

static int _weld_vertex(const Vector3 &p_position, real_t p_epsilon, HashMap<Vector3i, Vector<int>> &r_cells, Vector<Vector3> &r_vertices) {
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

static bool _same_plane(const CSGBrush::Face &p_face_a, const BevelFace &p_info_a, const CSGBrush::Face &p_face_b, const BevelFace &p_info_b, real_t p_epsilon) {
	if (p_info_a.normal.dot(p_info_b.normal) < 1.0 - CMP_EPSILON * 10.0) {
		return false;
	}
	return Math::abs(p_info_a.normal.dot(p_face_b.vertices[0] - p_face_a.vertices[0])) <= p_epsilon;
}

static BevelPoint _interpolate_point(const CSGBrush::Face &p_face, const Vector3 &p_position) {
	BevelPoint result;
	result.position = p_position;

	const Vector3 v0 = p_face.vertices[1] - p_face.vertices[0];
	const Vector3 v1 = p_face.vertices[2] - p_face.vertices[0];
	const Vector3 v2 = p_position - p_face.vertices[0];
	const real_t d00 = v0.dot(v0);
	const real_t d01 = v0.dot(v1);
	const real_t d11 = v1.dot(v1);
	const real_t d20 = v2.dot(v0);
	const real_t d21 = v2.dot(v1);
	const real_t denominator = d00 * d11 - d01 * d01;

	real_t weights[3] = { 1.0, 0.0, 0.0 };
	if (!Math::is_zero_approx(denominator)) {
		weights[1] = (d11 * d20 - d01 * d21) / denominator;
		weights[2] = (d00 * d21 - d01 * d20) / denominator;
		weights[0] = 1.0 - weights[1] - weights[2];
	}

	result.uv = p_face.uvs[0] * weights[0] + p_face.uvs[1] * weights[1] + p_face.uvs[2] * weights[2];
	result.color = p_face.colors[0] * weights[0] + p_face.colors[1] * weights[1] + p_face.colors[2] * weights[2];
	for (int channel = 0; channel < CSGBrush::CUSTOM_CHANNEL_COUNT; channel++) {
		result.customs[channel] = p_face.customs[channel][0] * weights[0] + p_face.customs[channel][1] * weights[1] + p_face.customs[channel][2] * weights[2];
	}
	return result;
}

static void _append_triangle(Vector<CSGBrush::Face> &r_faces, const BevelPoint &p_a, const BevelPoint &p_b, const BevelPoint &p_c, const CSGBrush::Face &p_source, const Vector3 &p_expected_normal) {
	if ((p_b.position - p_a.position).cross(p_c.position - p_a.position).length_squared() <= CMP_EPSILON2) {
		return;
	}

	const BevelPoint *points[3] = { &p_a, &p_b, &p_c };
	if ((p_b.position - p_a.position).cross(p_c.position - p_a.position).dot(p_expected_normal) < 0.0) {
		SWAP(points[1], points[2]);
	}

	CSGBrush::Face face;
	face.material = p_source.material;
	face.smooth = p_source.smooth;
	face.invert = p_source.invert;
	for (int i = 0; i < 3; i++) {
		face.vertices[i] = points[i]->position;
		face.uvs[i] = points[i]->uv;
		face.colors[i] = points[i]->color;
		for (int channel = 0; channel < CSGBrush::CUSTOM_CHANNEL_COUNT; channel++) {
			face.customs[channel][i] = points[i]->customs[channel];
		}
	}
	r_faces.push_back(face);
}

static int _find_corner(const BevelFace &p_face, int p_vertex) {
	for (int i = 0; i < 3; i++) {
		if (p_face.vertices[i] == p_vertex) {
			return i;
		}
	}
	return -1;
}

static int _find_region_root(Vector<int> &r_parents, int p_face) {
	int root = p_face;
	while (r_parents[root] != root) {
		root = r_parents[root];
	}
	while (r_parents[p_face] != p_face) {
		const int parent = r_parents[p_face];
		r_parents.write[p_face] = root;
		p_face = parent;
	}
	return root;
}

static void _join_regions(Vector<int> &r_parents, int p_a, int p_b) {
	const int root_a = _find_region_root(r_parents, p_a);
	const int root_b = _find_region_root(r_parents, p_b);
	if (root_a != root_b) {
		r_parents.write[root_b] = root_a;
	}
}

static bool _has_region_pair(const BevelEdge &p_edge, int p_region_a, int p_region_b, const Vector<int> &p_face_regions) {
	if (p_edge.faces.size() != 2) {
		return false;
	}
	const int edge_region_a = p_face_regions[p_edge.faces[0]];
	const int edge_region_b = p_face_regions[p_edge.faces[1]];
	return (edge_region_a == p_region_a && edge_region_b == p_region_b) || (edge_region_a == p_region_b && edge_region_b == p_region_a);
}

static real_t _get_edge_run_length(uint64_t p_edge_key, const HashMap<uint64_t, BevelEdge> &p_edges, const Vector<Vector<uint64_t>> &p_vertex_edges, const Vector<Vector3> &p_vertices, const Vector<int> &p_face_regions) {
	const BevelEdge &initial_edge = p_edges[p_edge_key];
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
				const BevelEdge &candidate = p_edges[candidate_key];
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

static bool _is_closed_manifold(const Vector<CSGBrush::Face> &p_faces, real_t p_merge_epsilon) {
	HashMap<Vector3i, Vector<int>> vertex_cells;
	Vector<Vector3> vertices;
	HashMap<uint64_t, BevelEdgeUse> edge_uses;
	for (const CSGBrush::Face &face : p_faces) {
		int face_vertices[3];
		for (int corner = 0; corner < 3; corner++) {
			face_vertices[corner] = _weld_vertex(face.vertices[corner], p_merge_epsilon, vertex_cells, vertices);
		}
		for (int corner = 0; corner < 3; corner++) {
			const int from = face_vertices[corner];
			const int to = face_vertices[(corner + 1) % 3];
			if (from == to) {
				return false;
			}
			BevelEdgeUse &use = edge_uses[_edge_key(from, to)];
			use.count++;
			use.direction_balance += from < to ? 1 : -1;
		}
	}
	for (const KeyValue<uint64_t, BevelEdgeUse> &entry : edge_uses) {
		if (entry.value.count != 2 || entry.value.direction_balance != 0) {
			return false;
		}
	}
	return !p_faces.is_empty();
}

} // namespace

void CSGBevelModifier::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_width", "width"), &CSGBevelModifier::set_width);
	ClassDB::bind_method(D_METHOD("get_width"), &CSGBevelModifier::get_width);
	ClassDB::bind_method(D_METHOD("set_angle", "angle"), &CSGBevelModifier::set_angle);
	ClassDB::bind_method(D_METHOD("get_angle"), &CSGBevelModifier::get_angle);
	ClassDB::bind_method(D_METHOD("set_application_mode", "mode"), &CSGBevelModifier::set_application_mode);
	ClassDB::bind_method(D_METHOD("get_application_mode"), &CSGBevelModifier::get_application_mode);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "width", PROPERTY_HINT_RANGE, "0,10,0.001,or_greater,suffix:m"), "set_width", "get_width");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "angle", PROPERTY_HINT_RANGE, "0,180,0.1,radians_as_degrees"), "set_angle", "get_angle");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "application_mode", PROPERTY_HINT_ENUM, "Operands,Result,Both"), "set_application_mode", "get_application_mode");

	BIND_ENUM_CONSTANT(APPLICATION_MODE_OPERANDS);
	BIND_ENUM_CONSTANT(APPLICATION_MODE_RESULT);
	BIND_ENUM_CONSTANT(APPLICATION_MODE_BOTH);
}

void CSGBevelModifier::set_width(real_t p_width) {
	p_width = MAX(p_width, 0.0);
	if (Math::is_equal_approx(width, p_width)) {
		return;
	}
	width = p_width;
	emit_changed();
}

real_t CSGBevelModifier::get_width() const {
	return width;
}

void CSGBevelModifier::set_angle(real_t p_angle) {
	p_angle = CLAMP(p_angle, 0.0, Math::PI);
	if (Math::is_equal_approx(angle, p_angle)) {
		return;
	}
	angle = p_angle;
	emit_changed();
}

real_t CSGBevelModifier::get_angle() const {
	return angle;
}

void CSGBevelModifier::set_application_mode(ApplicationMode p_mode) {
	ERR_FAIL_INDEX(int(p_mode), int(APPLICATION_MODE_BOTH) + 1);
	if (application_mode == p_mode) {
		return;
	}
	application_mode = p_mode;
	emit_changed();
}

CSGBevelModifier::ApplicationMode CSGBevelModifier::get_application_mode() const {
	return application_mode;
}

uint32_t CSGBevelModifier::get_process_stages() const {
	switch (application_mode) {
		case APPLICATION_MODE_OPERANDS:
			return PROCESS_STAGE_OPERANDS;
		case APPLICATION_MODE_BOTH:
			return PROCESS_STAGE_OPERANDS | PROCESS_STAGE_RESULT;
		case APPLICATION_MODE_RESULT:
		default:
			return PROCESS_STAGE_RESULT;
	}
}

void CSGBevelModifier::process(const Ref<CSGModifierContext> &p_context) {
	if (!is_enabled() || p_context.is_null() || width <= 0.0) {
		return;
	}
	_process_brush(p_context->get_brush());
}

void CSGBevelModifier::_process_brush(CSGBrush *p_brush) const {
	ERR_FAIL_NULL(p_brush);
	if (p_brush->faces.is_empty()) {
		return;
	}

	HashMap<Vector3i, Vector<int>> vertex_cells;
	Vector<Vector3> vertices;
	Vector<BevelFace> faces;
	faces.resize(p_brush->faces.size());
	Vector<Vector<int>> vertex_faces;
	Vector<Vector<uint64_t>> vertex_edges;
	HashMap<uint64_t, BevelEdge> edges;

	for (int face_i = 0; face_i < p_brush->faces.size(); face_i++) {
		const CSGBrush::Face &face = p_brush->faces[face_i];
		BevelFace &face_info = faces.write[face_i];
		face_info.normal = (face.vertices[1] - face.vertices[0]).cross(face.vertices[2] - face.vertices[0]).normalized();
		for (int corner = 0; corner < 3; corner++) {
			const int previous_vertex_count = vertices.size();
			const int vertex = _weld_vertex(face.vertices[corner], BEVEL_MERGE_EPSILON, vertex_cells, vertices);
			if (vertices.size() != previous_vertex_count) {
				vertex_faces.resize(vertices.size());
			}
			face_info.vertices[corner] = vertex;
			vertex_faces.write[vertex].push_back(face_i);
		}

		for (int corner = 0; corner < 3; corner++) {
			const int a = face_info.vertices[corner];
			const int b = face_info.vertices[(corner + 1) % 3];
			const uint64_t key = _edge_key(a, b);
			HashMap<uint64_t, BevelEdge>::Iterator edge = edges.find(key);
			if (!edge) {
				BevelEdge new_edge;
				new_edge.vertices[0] = MIN(a, b);
				new_edge.vertices[1] = MAX(a, b);
				new_edge.faces.push_back(face_i);
				edges.insert(key, new_edge);
			} else {
				edge->value.faces.push_back(face_i);
			}
		}
	}
	vertex_edges.resize(vertices.size());
	for (const KeyValue<uint64_t, BevelEdge> &entry : edges) {
		vertex_edges.write[entry.value.vertices[0]].push_back(entry.key);
		vertex_edges.write[entry.value.vertices[1]].push_back(entry.key);
	}

	const real_t normal_dot_limit = Math::cos(angle);
	int selected_edge_count = 0;
	for (KeyValue<uint64_t, BevelEdge> &entry : edges) {
		BevelEdge &edge = entry.value;
		if (edge.faces.size() != 2) {
			continue;
		}
		const int face_a = edge.faces[0];
		const int face_b = edge.faces[1];
		if (_same_plane(p_brush->faces[face_a], faces[face_a], p_brush->faces[face_b], faces[face_b], BEVEL_MERGE_EPSILON)) {
			continue;
		}
		const real_t normal_dot = CLAMP(faces[face_a].normal.dot(faces[face_b].normal), -1.0, 1.0);
		edge.selected = normal_dot < normal_dot_limit && normal_dot > -1.0 + CMP_EPSILON;
		selected_edge_count += edge.selected ? 1 : 0;
	}
	if (selected_edge_count == 0) {
		return;
	}

	// Join coplanar triangles into logical face regions. Every triangle in a
	// region must use the exact same inset position for a shared vertex;
	// otherwise the result contains tiny cracks and cannot be used as a child
	// operand in a subsequent Manifold Boolean operation.
	Vector<int> face_regions;
	face_regions.resize(faces.size());
	for (int face_i = 0; face_i < faces.size(); face_i++) {
		face_regions.write[face_i] = face_i;
	}
	for (const KeyValue<uint64_t, BevelEdge> &entry : edges) {
		const BevelEdge &edge = entry.value;
		if (edge.faces.size() == 2 && _same_plane(p_brush->faces[edge.faces[0]], faces[edge.faces[0]], p_brush->faces[edge.faces[1]], faces[edge.faces[1]], BEVEL_MERGE_EPSILON)) {
			_join_regions(face_regions, edge.faces[0], edge.faces[1]);
		}
	}
	for (int face_i = 0; face_i < faces.size(); face_i++) {
		face_regions.write[face_i] = _find_region_root(face_regions, face_i);
	}

	HashMap<uint64_t, Vector3> region_vertex_positions;
	HashMap<uint64_t, real_t> edge_run_lengths;
	Vector<BevelPoint> mapped_points;
	mapped_points.resize(faces.size() * 3);
	for (int face_i = 0; face_i < faces.size(); face_i++) {
		const BevelFace &face_info = faces[face_i];
		const CSGBrush::Face &source_face = p_brush->faces[face_i];
		for (int corner = 0; corner < 3; corner++) {
			const int vertex = face_info.vertices[corner];
			const int region = face_regions[face_i];
			const uint64_t region_vertex_key = (uint64_t(uint32_t(region)) << 32) | uint32_t(vertex);
			HashMap<uint64_t, Vector3>::ConstIterator cached_position = region_vertex_positions.find(region_vertex_key);
			if (cached_position) {
				mapped_points.write[face_i * 3 + corner] = _interpolate_point(source_face, cached_position->value);
				continue;
			}

			Vector<Vector3> constraints;
			real_t local_width = width;

			for (uint64_t edge_key : vertex_edges[vertex]) {
				const BevelEdge &edge = edges[edge_key];
				if (!edge.selected) {
					continue;
				}

				int plane_face = -1;
				for (int edge_face : edge.faces) {
					if (face_regions[edge_face] == region) {
						plane_face = edge_face;
						break;
					}
				}
				if (plane_face < 0) {
					continue;
				}

				const int other = edge.vertices[0] == vertex ? edge.vertices[1] : edge.vertices[0];
				const Vector3 edge_direction = (vertices[other] - vertices[vertex]).normalized();
				const BevelFace &boundary_face = faces[plane_face];
				Vector3 inward;
				for (int i = 0; i < 3; i++) {
					const int candidate = boundary_face.vertices[i];
					if (candidate != vertex && candidate != other) {
						const Vector3 to_inside = vertices[candidate] - vertices[vertex];
						inward = (to_inside - edge_direction * to_inside.dot(edge_direction)).normalized();
						break;
					}
				}
				if (inward.is_zero_approx()) {
					continue;
				}

				bool duplicate = false;
				for (const Vector3 &existing : constraints) {
					if (existing.dot(inward) > 1.0 - CMP_EPSILON * 10.0) {
						duplicate = true;
						break;
					}
				}
				if (!duplicate) {
					constraints.push_back(inward);
				}
				HashMap<uint64_t, real_t>::ConstIterator cached_run_length = edge_run_lengths.find(edge_key);
				if (!cached_run_length) {
					edge_run_lengths.insert(edge_key, _get_edge_run_length(edge_key, edges, vertex_edges, vertices, face_regions));
					cached_run_length = edge_run_lengths.find(edge_key);
				}
				local_width = MIN(local_width, cached_run_length->value * 0.49);
			}

			Vector3 offset;
			if (constraints.size() == 1) {
				offset = constraints[0] * local_width;
			} else if (constraints.size() >= 2) {
				const Vector3 tangent_u = constraints[0];
				const Vector3 tangent_v = face_info.normal.cross(tangent_u).normalized();
				real_t a00 = 0.0;
				real_t a01 = 0.0;
				real_t a11 = 0.0;
				real_t b0 = 0.0;
				real_t b1 = 0.0;
				for (const Vector3 &constraint : constraints) {
					const real_t x = constraint.dot(tangent_u);
					const real_t y = constraint.dot(tangent_v);
					a00 += x * x;
					a01 += x * y;
					a11 += y * y;
					b0 += x * local_width;
					b1 += y * local_width;
				}
				const real_t determinant = a00 * a11 - a01 * a01;
				if (!Math::is_zero_approx(determinant)) {
					offset = tangent_u * ((b0 * a11 - b1 * a01) / determinant) + tangent_v * ((a00 * b1 - a01 * b0) / determinant);
				} else {
					offset = tangent_u * local_width;
				}
			}
			const Vector3 mapped_position = vertices[vertex] + offset;
			region_vertex_positions.insert(region_vertex_key, mapped_position);
			mapped_points.write[face_i * 3 + corner] = _interpolate_point(source_face, mapped_position);
		}
	}

	Vector<CSGBrush::Face> result_faces;
	result_faces.reserve(p_brush->faces.size() + selected_edge_count * 2);
	for (int face_i = 0; face_i < faces.size(); face_i++) {
		_append_triangle(result_faces, mapped_points[face_i * 3], mapped_points[face_i * 3 + 1], mapped_points[face_i * 3 + 2], p_brush->faces[face_i], faces[face_i].normal);
	}

	for (const KeyValue<uint64_t, BevelEdge> &entry : edges) {
		const BevelEdge &edge = entry.value;
		if (!edge.selected) {
			continue;
		}
		const int face_a = edge.faces[0];
		const int face_b = edge.faces[1];
		const int a0 = _find_corner(faces[face_a], edge.vertices[0]);
		const int a1 = _find_corner(faces[face_a], edge.vertices[1]);
		const int b0 = _find_corner(faces[face_b], edge.vertices[0]);
		const int b1 = _find_corner(faces[face_b], edge.vertices[1]);
		if (a0 < 0 || a1 < 0 || b0 < 0 || b1 < 0) {
			continue;
		}
		const BevelPoint &p0 = mapped_points[face_a * 3 + a0];
		const BevelPoint &p1 = mapped_points[face_a * 3 + a1];
		const BevelPoint &p2 = mapped_points[face_b * 3 + b1];
		const BevelPoint &p3 = mapped_points[face_b * 3 + b0];
		const Vector3 expected_normal = (faces[face_a].normal + faces[face_b].normal).normalized();
		_append_triangle(result_faces, p0, p1, p2, p_brush->faces[face_a], expected_normal);
		_append_triangle(result_faces, p0, p2, p3, p_brush->faces[face_a], expected_normal);
	}

	for (int vertex = 0; vertex < vertices.size(); vertex++) {
		Vector<CapPoint> cap_points;
		HashMap<int, int> region_to_cap;
		for (int face_i : vertex_faces[vertex]) {
			const int corner = _find_corner(faces[face_i], vertex);
			if (corner < 0) {
				continue;
			}
			const BevelPoint &point = mapped_points[face_i * 3 + corner];
			if (point.position.distance_squared_to(vertices[vertex]) <= BEVEL_MERGE_EPSILON * BEVEL_MERGE_EPSILON) {
				continue;
			}
			const int region = face_regions[face_i];
			if (!region_to_cap.has(region)) {
				int cap = -1;
				for (int i = 0; i < cap_points.size(); i++) {
					if (cap_points[i].point.position.distance_squared_to(point.position) <= BEVEL_MERGE_EPSILON * BEVEL_MERGE_EPSILON) {
						cap = i;
						break;
					}
				}
				if (cap < 0) {
					CapPoint cap_point;
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

		// The selected bevel edges define the cap boundary topologically. Using
		// this connectivity avoids relying on a projected angular sort, whose
		// order can change when a Boolean operation triangulates the same corner
		// differently after an operand moves.
		for (uint64_t edge_key : vertex_edges[vertex]) {
			const BevelEdge &edge = edges[edge_key];
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
		HashMap<int, bool> normal_regions;
		for (int face_i : vertex_faces[vertex]) {
			const int region = face_regions[face_i];
			if (!normal_regions.has(region)) {
				normal_regions.insert(region, true);
				expected_normal += faces[face_i].normal;
			}
		}
		expected_normal.normalize();

		// Two points connected twice represent a straight continuation through a
		// triangulation vertex. Its neighboring bevel quads already meet.
		if (cap_points.size() == 2 && cap_points[0].neighbors.size() == 2 && cap_points[1].neighbors.size() == 2) {
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
			continue;
		}

		Vector<BevelPoint> boundary_points;
		boundary_points.reserve(ordered_points.size() + (open_boundary ? 1 : 0));
		for (int point_index : ordered_points) {
			boundary_points.push_back(cap_points[point_index].point);
		}
		if (open_boundary) {
			boundary_points.push_back(_interpolate_point(p_brush->faces[vertex_faces[vertex][0]], vertices[vertex]));
		}

		if (boundary_points.size() == 3) {
			_append_triangle(result_faces, boundary_points[0], boundary_points[1], boundary_points[2], p_brush->faces[vertex_faces[vertex][0]], expected_normal);
			continue;
		}

		BevelPoint center_point;
		for (const BevelPoint &point : boundary_points) {
			center_point.position += point.position;
			center_point.uv += point.uv;
			center_point.color += point.color;
			for (int channel = 0; channel < CSGBrush::CUSTOM_CHANNEL_COUNT; channel++) {
				center_point.customs[channel] += point.customs[channel];
			}
		}
		const real_t point_count = boundary_points.size();
		center_point.position /= point_count;
		center_point.uv /= point_count;
		center_point.color /= point_count;
		for (int channel = 0; channel < CSGBrush::CUSTOM_CHANNEL_COUNT; channel++) {
			center_point.customs[channel] /= point_count;
		}

		for (int i = 0; i < boundary_points.size(); i++) {
			const BevelPoint &point_a = boundary_points[i];
			const BevelPoint &point_b = boundary_points[(i + 1) % boundary_points.size()];
			_append_triangle(result_faces, center_point, point_a, point_b, p_brush->faces[vertex_faces[vertex][0]], expected_normal);
		}
	}

	if (!_is_closed_manifold(result_faces, BEVEL_MERGE_EPSILON)) {
		WARN_PRINT("CSGBevelModifier could not create closed manifold geometry; the original brush was preserved.");
		return;
	}
	p_brush->faces = result_faces;
	p_brush->_regen_face_aabbs();
}
