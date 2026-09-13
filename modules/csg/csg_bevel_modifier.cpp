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
	real_t angle = 0.0;
};

struct CapPointSort {
	_FORCE_INLINE_ bool operator()(const CapPoint &p_a, const CapPoint &p_b) const {
		return p_a.angle < p_b.angle;
	}
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

static bool _same_plane(const CSGBrush::Face &p_face_a, const BevelFace &p_info_a, const CSGBrush::Face &p_face_b, const BevelFace &p_info_b) {
	if (p_info_a.normal.dot(p_info_b.normal) < 1.0 - CMP_EPSILON * 10.0) {
		return false;
	}
	return Math::is_zero_approx(p_info_a.normal.dot(p_face_b.vertices[0] - p_face_a.vertices[0]));
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

static bool _is_closed_manifold(const Vector<CSGBrush::Face> &p_faces) {
	HashMap<Vector3, int> vertex_map;
	HashMap<uint64_t, BevelEdgeUse> edge_uses;
	int next_vertex = 0;
	for (const CSGBrush::Face &face : p_faces) {
		int face_vertices[3];
		for (int corner = 0; corner < 3; corner++) {
			HashMap<Vector3, int>::ConstIterator found = vertex_map.find(face.vertices[corner]);
			if (found) {
				face_vertices[corner] = found->value;
			} else {
				face_vertices[corner] = next_vertex++;
				vertex_map.insert(face.vertices[corner], face_vertices[corner]);
			}
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

	HashMap<Vector3, int> vertex_map;
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
			const Vector3 position = face.vertices[corner];
			HashMap<Vector3, int>::ConstIterator found = vertex_map.find(position);
			int vertex;
			if (found) {
				vertex = found->value;
			} else {
				vertex = vertices.size();
				vertex_map.insert(position, vertex);
				vertices.push_back(position);
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
		const real_t normal_dot = CLAMP(faces[edge.faces[0]].normal.dot(faces[edge.faces[1]].normal), -1.0, 1.0);
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
		if (edge.faces.size() == 2 && _same_plane(p_brush->faces[edge.faces[0]], faces[edge.faces[0]], p_brush->faces[edge.faces[1]], faces[edge.faces[1]])) {
			_join_regions(face_regions, edge.faces[0], edge.faces[1]);
		}
	}
	for (int face_i = 0; face_i < faces.size(); face_i++) {
		face_regions.write[face_i] = _find_region_root(face_regions, face_i);
	}

	HashMap<uint64_t, Vector3> region_vertex_positions;
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
				local_width = MIN(local_width, vertices[other].distance_to(vertices[vertex]) * 0.49);
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
		for (int face_i : vertex_faces[vertex]) {
			const int corner = _find_corner(faces[face_i], vertex);
			if (corner < 0) {
				continue;
			}
			const BevelPoint &point = mapped_points[face_i * 3 + corner];
			if (point.position.is_equal_approx(vertices[vertex])) {
				continue;
			}
			bool duplicate = false;
			for (const CapPoint &existing : cap_points) {
				if (existing.point.position.is_equal_approx(point.position)) {
					duplicate = true;
					break;
				}
			}
			if (!duplicate) {
				CapPoint cap_point;
				cap_point.point = point;
				cap_points.push_back(cap_point);
			}
		}
		if (cap_points.size() < 3) {
			continue;
		}

		Vector3 center;
		for (const CapPoint &point : cap_points) {
			center += point.point.position;
		}
		center /= cap_points.size();
		Vector3 expected_normal;
		for (int face_i : vertex_faces[vertex]) {
			expected_normal += faces[face_i].normal;
		}
		expected_normal.normalize();
		Vector3 axis_u = expected_normal.cross(Vector3(0, 1, 0));
		if (axis_u.length_squared() <= CMP_EPSILON2) {
			axis_u = expected_normal.cross(Vector3(1, 0, 0));
		}
		axis_u.normalize();
		const Vector3 axis_v = expected_normal.cross(axis_u).normalized();
		for (CapPoint &point : cap_points) {
			const Vector3 direction = point.point.position - center;
			point.angle = Math::atan2(direction.dot(axis_v), direction.dot(axis_u));
		}
		cap_points.sort_custom<CapPointSort>();
		for (int i = 1; i < cap_points.size() - 1; i++) {
			_append_triangle(result_faces, cap_points[0].point, cap_points[i].point, cap_points[i + 1].point, p_brush->faces[vertex_faces[vertex][0]], expected_normal);
		}
	}

	if (!_is_closed_manifold(result_faces)) {
		WARN_PRINT("CSGBevelModifier could not create closed manifold geometry; the original brush was preserved.");
		return;
	}
	p_brush->faces = result_faces;
	p_brush->_regen_face_aabbs();
}
