/**************************************************************************/
/*  mesh_splitter.cpp                                                     */
/**************************************************************************/

#include "mesh_splitter.h"

#include "core/math/geometry_2d.h"
#include "core/math/random_pcg.h"
#include "core/object/class_db.h"

namespace {

struct SplitVertex {
	Vector3 position;
	Vector3 normal;
	Vector4 tangent;
	Color color = Color(1, 1, 1, 1);
	Vector2 uv;
	Vector2 uv2;
	Vector4 custom[4];
};

struct SplitTriangle {
	SplitVertex vertices[3];
};

struct SplitSurface {
	Vector<SplitTriangle> triangles;
	Ref<Material> material;
	StringName name;
	bool has_normal = false;
	bool has_tangent = false;
	bool has_color = false;
	bool has_uv = false;
	bool has_uv2 = false;
	bool has_custom[4] = {};
	Mesh::ArrayCustomFormat custom_format[4] = {
		Mesh::ARRAY_CUSTOM_RGBA_FLOAT,
		Mesh::ARRAY_CUSTOM_RGBA_FLOAT,
		Mesh::ARRAY_CUSTOM_RGBA_FLOAT,
		Mesh::ARRAY_CUSTOM_RGBA_FLOAT,
	};
};

struct SplitPiece {
	Vector<SplitSurface> surfaces;
};

struct CutSegment {
	Vector3 a;
	Vector3 b;
};

struct GraphEdge {
	int a = 0;
	int b = 0;
};

static SplitVertex _interpolate_vertex(const SplitVertex &p_a, const SplitVertex &p_b, real_t p_weight) {
	SplitVertex result;
	result.position = p_a.position.lerp(p_b.position, p_weight);
	result.normal = p_a.normal.lerp(p_b.normal, p_weight).normalized();
	Vector3 tangent = Vector3(p_a.tangent.x, p_a.tangent.y, p_a.tangent.z).lerp(Vector3(p_b.tangent.x, p_b.tangent.y, p_b.tangent.z), p_weight).normalized();
	result.tangent = Vector4(tangent.x, tangent.y, tangent.z, p_weight < 0.5 ? p_a.tangent.w : p_b.tangent.w);
	result.color = p_a.color.lerp(p_b.color, p_weight);
	result.uv = p_a.uv.lerp(p_b.uv, p_weight);
	result.uv2 = p_a.uv2.lerp(p_b.uv2, p_weight);
	for (int channel = 0; channel < 4; channel++) {
		result.custom[channel] = p_a.custom[channel].lerp(p_b.custom[channel], p_weight);
	}
	return result;
}

static Vector<SplitVertex> _clip_polygon(const SplitTriangle &p_triangle, const Plane &p_plane, bool p_positive, real_t p_epsilon) {
	Vector<SplitVertex> input;
	for (int corner = 0; corner < 3; corner++) {
		input.push_back(p_triangle.vertices[corner]);
	}
	Vector<SplitVertex> output;
	for (int vertex_i = 0; vertex_i < input.size(); vertex_i++) {
		const SplitVertex &current = input[vertex_i];
		const SplitVertex &next = input[(vertex_i + 1) % input.size()];
		const real_t current_distance = p_plane.distance_to(current.position);
		const real_t next_distance = p_plane.distance_to(next.position);
		const bool current_inside = p_positive ? current_distance >= -p_epsilon : current_distance <= p_epsilon;
		const bool next_inside = p_positive ? next_distance >= -p_epsilon : next_distance <= p_epsilon;
		if (current_inside) {
			output.push_back(current);
		}
		if (current_inside != next_inside) {
			const real_t denominator = current_distance - next_distance;
			if (!Math::is_zero_approx(denominator)) {
				output.push_back(_interpolate_vertex(current, next, CLAMP(current_distance / denominator, real_t(0.0), real_t(1.0))));
			}
		}
	}
	return output;
}

static void _triangulate_fan(const Vector<SplitVertex> &p_polygon, Vector<SplitTriangle> &r_triangles) {
	for (int vertex_i = 1; vertex_i + 1 < p_polygon.size(); vertex_i++) {
		SplitTriangle triangle;
		triangle.vertices[0] = p_polygon[0];
		triangle.vertices[1] = p_polygon[vertex_i];
		triangle.vertices[2] = p_polygon[vertex_i + 1];
		if ((triangle.vertices[1].position - triangle.vertices[0].position).cross(triangle.vertices[2].position - triangle.vertices[0].position).length_squared() > CMP_EPSILON2) {
			r_triangles.push_back(triangle);
		}
	}
}

static bool _triangle_cut_segment(const SplitTriangle &p_triangle, const Plane &p_plane, real_t p_epsilon, CutSegment &r_segment) {
	Vector<Vector3> intersections;
	for (int edge = 0; edge < 3; edge++) {
		const Vector3 &a = p_triangle.vertices[edge].position;
		const Vector3 &b = p_triangle.vertices[(edge + 1) % 3].position;
		const real_t da = p_plane.distance_to(a);
		const real_t db = p_plane.distance_to(b);
		if (Math::abs(da) <= p_epsilon) {
			bool duplicate = false;
			for (const Vector3 &point : intersections) {
				duplicate |= point.distance_squared_to(a) <= p_epsilon * p_epsilon;
			}
			if (!duplicate) {
				intersections.push_back(a);
			}
		}
		if ((da > p_epsilon && db < -p_epsilon) || (da < -p_epsilon && db > p_epsilon)) {
			const Vector3 point = a.lerp(b, da / (da - db));
			bool duplicate = false;
			for (const Vector3 &existing : intersections) {
				duplicate |= existing.distance_squared_to(point) <= p_epsilon * p_epsilon;
			}
			if (!duplicate) {
				intersections.push_back(point);
			}
		}
	}
	if (intersections.size() != 2 || intersections[0].distance_squared_to(intersections[1]) <= p_epsilon * p_epsilon) {
		return false;
	}
	r_segment = { intersections[0], intersections[1] };
	return true;
}

static int _piece_triangle_count(const SplitPiece &p_piece) {
	int count = 0;
	for (const SplitSurface &surface : p_piece.surfaces) {
		count += surface.triangles.size();
	}
	return count;
}

static AABB _piece_bounds(const SplitPiece &p_piece) {
	AABB bounds;
	bool initialized = false;
	for (const SplitSurface &surface : p_piece.surfaces) {
		for (const SplitTriangle &triangle : surface.triangles) {
			for (int corner = 0; corner < 3; corner++) {
				if (initialized) {
					bounds.expand_to(triangle.vertices[corner].position);
				} else {
					bounds.position = triangle.vertices[corner].position;
					initialized = true;
				}
			}
		}
	}
	return bounds;
}

static Vector3 _balanced_normal(const AABB &p_bounds) {
	const Vector3 size = p_bounds.size;
	if (size.x >= size.y && size.x >= size.z) {
		return Vector3(1, 0, 0);
	}
	if (size.y >= size.z) {
		return Vector3(0, 1, 0);
	}
	return Vector3(0, 0, 1);
}

static Vector3 _random_normal(const Vector3 &p_base, real_t p_jitter, RandomPCG &r_rng) {
	if (Math::is_zero_approx(p_jitter)) {
		return p_base;
	}
	Vector3 tangent = p_base.cross(Vector3(0, 1, 0));
	if (tangent.length_squared() < CMP_EPSILON) {
		tangent = p_base.cross(Vector3(1, 0, 0));
	}
	tangent.normalize();
	const Vector3 bitangent = p_base.cross(tangent).normalized();
	const real_t deviation = r_rng.random(0.0f, p_jitter);
	const real_t azimuth = r_rng.random(0.0f, real_t(Math::TAU));
	return (p_base * Math::cos(deviation) + (tangent * Math::cos(azimuth) + bitangent * Math::sin(azimuth)) * Math::sin(deviation)).normalized();
}

static int _graph_node(const Vector3 &p_point, real_t p_tolerance, HashMap<Vector3i, int> &r_lookup, Vector<Vector3> &r_nodes) {
	const Vector3i key(
			Math::round(p_point.x / p_tolerance),
			Math::round(p_point.y / p_tolerance),
			Math::round(p_point.z / p_tolerance));
	if (const int *existing = r_lookup.getptr(key)) {
		return *existing;
	}
	const int index = r_nodes.size();
	r_nodes.push_back(p_point);
	r_lookup.insert(key, index);
	return index;
}

static Vector<Vector<Vector3>> _build_cut_contours(const Vector<CutSegment> &p_segments, real_t p_tolerance, MeshSplitSettings::CapMode p_mode) {
	Vector<Vector<Vector3>> contours;
	if (p_mode == MeshSplitSettings::CAP_NONE || p_segments.is_empty()) {
		return contours;
	}
	HashMap<Vector3i, int> lookup;
	Vector<Vector3> nodes;
	Vector<GraphEdge> edges;
	for (const CutSegment &segment : p_segments) {
		const int a = _graph_node(segment.a, p_tolerance, lookup, nodes);
		const int b = _graph_node(segment.b, p_tolerance, lookup, nodes);
		if (a != b) {
			edges.push_back({ a, b });
		}
	}
	Vector<Vector<int>> adjacency;
	adjacency.resize(nodes.size());
	for (int edge_i = 0; edge_i < edges.size(); edge_i++) {
		adjacency.write[edges[edge_i].a].push_back(edge_i);
		adjacency.write[edges[edge_i].b].push_back(edge_i);
	}
	Vector<bool> used;
	used.resize(edges.size());
	used.fill(false);
	for (int pass = 0; pass < 2; pass++) {
		for (int edge_i = 0; edge_i < edges.size(); edge_i++) {
			if (used[edge_i]) {
				continue;
			}
			int start = edges[edge_i].a;
			if (pass == 0) {
				if (adjacency[start].size() != 1 && adjacency[edges[edge_i].b].size() == 1) {
					start = edges[edge_i].b;
				}
				if (adjacency[start].size() != 1) {
					continue;
				}
			}
			Vector<int> chain;
			chain.push_back(start);
			int current = start;
			int previous_edge = -1;
			while (true) {
				int next_edge = -1;
				for (int candidate : adjacency[current]) {
					if (!used[candidate] && candidate != previous_edge) {
						next_edge = candidate;
						break;
					}
				}
				if (next_edge < 0) {
					break;
				}
				used.write[next_edge] = true;
				const GraphEdge &edge = edges[next_edge];
				current = edge.a == current ? edge.b : edge.a;
				chain.push_back(current);
				previous_edge = next_edge;
				if (current == start) {
					break;
				}
			}
			const bool closed = chain.size() > 2 && chain[chain.size() - 1] == start;
			if (!closed && p_mode != MeshSplitSettings::CAP_REPAIR_OPEN_LOOPS) {
				continue;
			}
			if (closed) {
				chain.resize(chain.size() - 1);
			}
			if (chain.size() >= 3) {
				Vector<Vector3> contour;
				for (int node : chain) {
					contour.push_back(nodes[node]);
				}
				contours.push_back(contour);
			}
		}
	}
	return contours;
}

static void _add_caps(SplitPiece &r_piece, const Vector<Vector<Vector3>> &p_contours, const Vector3 &p_normal, const Ref<MeshSplitSettings> &p_settings, bool p_positive_side) {
	if (p_contours.is_empty()) {
		return;
	}
	SplitSurface cap;
	cap.name = "SplitCaps";
	cap.material = p_settings->get_cap_material();
	cap.has_normal = true;
	cap.has_uv = true;
	cap.has_color = true;
	for (int channel = 0; channel < 4; channel++) {
		cap.has_custom[channel] = p_settings->is_cap_custom_enabled(channel);
		cap.custom_format[channel] = Mesh::ARRAY_CUSTOM_RGBA_FLOAT;
	}
	const Vector3 cap_normal = p_positive_side ? -p_normal : p_normal;
	Vector3 axis_u = p_normal.cross(Vector3(0, 1, 0));
	if (axis_u.length_squared() < CMP_EPSILON) {
		axis_u = p_normal.cross(Vector3(1, 0, 0));
	}
	axis_u.normalize();
	const Vector3 axis_v = p_normal.cross(axis_u).normalized();
	for (const Vector<Vector3> &contour : p_contours) {
		Vector<Vector2> polygon;
		for (const Vector3 &point : contour) {
			polygon.push_back(Vector2(point.dot(axis_u), point.dot(axis_v)));
		}
		const Vector<int> indices = Geometry2D::triangulate_polygon(polygon);
		for (int index_i = 0; index_i + 2 < indices.size(); index_i += 3) {
			SplitTriangle triangle;
			for (int corner = 0; corner < 3; corner++) {
				const int point_index = indices[index_i + corner];
				SplitVertex &vertex = triangle.vertices[corner];
				vertex.position = contour[point_index];
				vertex.normal = cap_normal;
				vertex.uv = polygon[point_index] * p_settings->get_cap_uv_scale();
				vertex.color = p_settings->get_cap_color();
				for (int channel = 0; channel < 4; channel++) {
					vertex.custom[channel] = p_settings->get_cap_custom(channel);
				}
			}
			// Godot treats clockwise triangles as front-facing, so their visible
			// geometric normal is the opposite of the conventional cross product.
			const Vector3 winding_normal = -(triangle.vertices[1].position - triangle.vertices[0].position).cross(triangle.vertices[2].position - triangle.vertices[0].position);
			if (winding_normal.dot(cap_normal) < 0.0) {
				SWAP(triangle.vertices[1], triangle.vertices[2]);
			}
			cap.triangles.push_back(triangle);
		}
	}
	if (!cap.triangles.is_empty()) {
		r_piece.surfaces.push_back(cap);
	}
}

static bool _split_piece(const SplitPiece &p_piece, const Plane &p_plane, const Ref<MeshSplitSettings> &p_settings, SplitPiece &r_positive, SplitPiece &r_negative) {
	Vector<CutSegment> segments;
	const real_t epsilon = p_settings->get_weld_tolerance();
	for (const SplitSurface &source_surface : p_piece.surfaces) {
		SplitSurface positive_surface = source_surface;
		SplitSurface negative_surface = source_surface;
		positive_surface.triangles.clear();
		negative_surface.triangles.clear();
		for (const SplitTriangle &triangle : source_surface.triangles) {
			bool coplanar = true;
			for (int corner = 0; corner < 3; corner++) {
				coplanar &= Math::abs(p_plane.distance_to(triangle.vertices[corner].position)) <= epsilon;
			}
			if (coplanar) {
				const Vector3 triangle_normal = (triangle.vertices[1].position - triangle.vertices[0].position).cross(triangle.vertices[2].position - triangle.vertices[0].position);
				if (triangle_normal.dot(p_plane.normal) >= 0.0) {
					positive_surface.triangles.push_back(triangle);
				} else {
					negative_surface.triangles.push_back(triangle);
				}
				continue;
			}
			_triangulate_fan(_clip_polygon(triangle, p_plane, true, epsilon), positive_surface.triangles);
			_triangulate_fan(_clip_polygon(triangle, p_plane, false, epsilon), negative_surface.triangles);
			CutSegment segment;
			if (_triangle_cut_segment(triangle, p_plane, epsilon, segment)) {
				segments.push_back(segment);
			}
		}
		if (!positive_surface.triangles.is_empty()) {
			r_positive.surfaces.push_back(positive_surface);
		}
		if (!negative_surface.triangles.is_empty()) {
			r_negative.surfaces.push_back(negative_surface);
		}
	}
	if (_piece_triangle_count(r_positive) < p_settings->get_min_triangles() || _piece_triangle_count(r_negative) < p_settings->get_min_triangles()) {
		return false;
	}
	const Vector<Vector<Vector3>> contours = _build_cut_contours(segments, epsilon, p_settings->get_cap_mode());
	_add_caps(r_positive, contours, p_plane.normal, p_settings, true);
	_add_caps(r_negative, contours, p_plane.normal, p_settings, false);
	return true;
}

static Vector<Vector4> _unpack_custom(const Variant &p_array, Mesh::ArrayCustomFormat p_format, int p_vertex_count) {
	Vector<Vector4> result;
	result.resize(p_vertex_count);
	result.fill(Vector4());
	if (p_format >= Mesh::ARRAY_CUSTOM_R_FLOAT) {
		const int components = int(p_format) - int(Mesh::ARRAY_CUSTOM_R_FLOAT) + 1;
		const PackedFloat32Array values = p_array;
		if (values.size() != p_vertex_count * components) {
			return Vector<Vector4>();
		}
		for (int vertex = 0; vertex < p_vertex_count; vertex++) {
			for (int component = 0; component < components; component++) {
				result.write[vertex][component] = values[vertex * components + component];
			}
		}
		return result;
	}
	const bool half = p_format == Mesh::ARRAY_CUSTOM_RG_HALF || p_format == Mesh::ARRAY_CUSTOM_RGBA_HALF;
	const int components = p_format == Mesh::ARRAY_CUSTOM_RG_HALF ? 2 : 4;
	const int component_size = half ? 2 : 1;
	const PackedByteArray values = p_array;
	if (values.size() != p_vertex_count * components * component_size) {
		return Vector<Vector4>();
	}
	for (int vertex = 0; vertex < p_vertex_count; vertex++) {
		for (int component = 0; component < components; component++) {
			const int offset = (vertex * components + component) * component_size;
			if (half) {
				result.write[vertex][component] = Math::half_to_float(uint16_t(values[offset]) | (uint16_t(values[offset + 1]) << 8));
			} else if (p_format == Mesh::ARRAY_CUSTOM_RGBA8_SNORM) {
				result.write[vertex][component] = MAX(real_t(int8_t(values[offset])) / real_t(127.0), real_t(-1.0));
			} else {
				result.write[vertex][component] = real_t(values[offset]) / real_t(255.0);
			}
		}
	}
	return result;
}

static Variant _pack_custom(const Vector<Vector4> &p_values, Mesh::ArrayCustomFormat p_format) {
	if (p_format >= Mesh::ARRAY_CUSTOM_R_FLOAT) {
		const int components = int(p_format) - int(Mesh::ARRAY_CUSTOM_R_FLOAT) + 1;
		PackedFloat32Array result;
		result.resize(p_values.size() * components);
		for (int vertex = 0; vertex < p_values.size(); vertex++) {
			for (int component = 0; component < components; component++) {
				result.set(vertex * components + component, p_values[vertex][component]);
			}
		}
		return result;
	}
	const bool half = p_format == Mesh::ARRAY_CUSTOM_RG_HALF || p_format == Mesh::ARRAY_CUSTOM_RGBA_HALF;
	const int components = p_format == Mesh::ARRAY_CUSTOM_RG_HALF ? 2 : 4;
	const int component_size = half ? 2 : 1;
	PackedByteArray result;
	result.resize(p_values.size() * components * component_size);
	for (int vertex = 0; vertex < p_values.size(); vertex++) {
		for (int component = 0; component < components; component++) {
			const int offset = (vertex * components + component) * component_size;
			if (half) {
				const uint16_t encoded = Math::make_half_float(p_values[vertex][component]);
				result.set(offset, encoded & 0xff);
				result.set(offset + 1, encoded >> 8);
			} else if (p_format == Mesh::ARRAY_CUSTOM_RGBA8_SNORM) {
				result.set(offset, uint8_t(int8_t(CLAMP(Math::round(p_values[vertex][component] * 127.0f), -127.0f, 127.0f))));
			} else {
				result.set(offset, uint8_t(CLAMP(Math::round(p_values[vertex][component] * 255.0f), 0.0f, 255.0f)));
			}
		}
	}
	return result;
}

static bool _read_mesh(const Ref<Mesh> &p_mesh, SplitPiece &r_piece) {
	Ref<ArrayMesh> source_array_mesh = p_mesh;
	for (int surface_i = 0; surface_i < p_mesh->get_surface_count(); surface_i++) {
		if (p_mesh->surface_get_primitive_type(surface_i) != Mesh::PRIMITIVE_TRIANGLES) {
			continue;
		}
		const Array arrays = p_mesh->surface_get_arrays(surface_i);
		const PackedVector3Array vertices = arrays[Mesh::ARRAY_VERTEX];
		if (vertices.is_empty()) {
			continue;
		}
		const PackedVector3Array normals = arrays[Mesh::ARRAY_NORMAL];
		const PackedFloat32Array tangents = arrays[Mesh::ARRAY_TANGENT];
		const PackedColorArray colors = arrays[Mesh::ARRAY_COLOR];
		const PackedVector2Array uvs = arrays[Mesh::ARRAY_TEX_UV];
		const PackedVector2Array uv2s = arrays[Mesh::ARRAY_TEX_UV2];
		const PackedInt32Array indices = arrays[Mesh::ARRAY_INDEX];
		const uint64_t format = static_cast<uint64_t>(p_mesh->surface_get_format(surface_i));
		SplitSurface surface;
		surface.material = p_mesh->surface_get_material(surface_i);
		if (source_array_mesh.is_valid()) {
			surface.name = source_array_mesh->surface_get_name(surface_i);
		}
		surface.has_normal = normals.size() == vertices.size();
		surface.has_tangent = tangents.size() == vertices.size() * 4;
		surface.has_color = colors.size() == vertices.size();
		surface.has_uv = uvs.size() == vertices.size();
		surface.has_uv2 = uv2s.size() == vertices.size();
		Vector<Vector4> custom_values[4];
		for (int channel = 0; channel < 4; channel++) {
			const uint64_t channel_flag = uint64_t(Mesh::ARRAY_FORMAT_CUSTOM0) << channel;
			if (!(format & channel_flag)) {
				continue;
			}
			const int shift = Mesh::ARRAY_FORMAT_CUSTOM0_SHIFT + channel * Mesh::ARRAY_FORMAT_CUSTOM_BITS;
			surface.custom_format[channel] = Mesh::ArrayCustomFormat((format >> shift) & Mesh::ARRAY_FORMAT_CUSTOM_MASK);
			custom_values[channel] = _unpack_custom(arrays[Mesh::ARRAY_CUSTOM0 + channel], surface.custom_format[channel], vertices.size());
			surface.has_custom[channel] = custom_values[channel].size() == vertices.size();
		}
		const int element_count = indices.is_empty() ? vertices.size() : indices.size();
		if (element_count % 3 != 0) {
			continue;
		}
		for (int element = 0; element < element_count; element += 3) {
			SplitTriangle triangle;
			bool valid = true;
			for (int corner = 0; corner < 3; corner++) {
				const int vertex_i = indices.is_empty() ? element + corner : indices[element + corner];
				if (vertex_i < 0 || vertex_i >= vertices.size()) {
					valid = false;
					break;
				}
				SplitVertex &vertex = triangle.vertices[corner];
				vertex.position = vertices[vertex_i];
				if (surface.has_normal) {
					vertex.normal = normals[vertex_i];
				}
				if (surface.has_tangent) {
					vertex.tangent = Vector4(tangents[vertex_i * 4], tangents[vertex_i * 4 + 1], tangents[vertex_i * 4 + 2], tangents[vertex_i * 4 + 3]);
				}
				if (surface.has_color) {
					vertex.color = colors[vertex_i];
				}
				if (surface.has_uv) {
					vertex.uv = uvs[vertex_i];
				}
				if (surface.has_uv2) {
					vertex.uv2 = uv2s[vertex_i];
				}
				for (int channel = 0; channel < 4; channel++) {
					if (surface.has_custom[channel]) {
						vertex.custom[channel] = custom_values[channel][vertex_i];
					}
				}
			}
			if (valid) {
				surface.triangles.push_back(triangle);
			}
		}
		if (!surface.triangles.is_empty()) {
			r_piece.surfaces.push_back(surface);
		}
	}
	return _piece_triangle_count(r_piece) > 0;
}

static Vector3 _part_center(const AABB &p_bounds, MeshSplitSettings::PartCenter p_mode) {
	const Vector3 center = p_bounds.get_center();
	const Vector3 end = p_bounds.get_end();
	switch (p_mode) {
		case MeshSplitSettings::PART_CENTER_ORIGINAL:
			return Vector3();
		case MeshSplitSettings::PART_CENTER_AABB:
			return center;
		case MeshSplitSettings::PART_CENTER_TOP:
			return Vector3(center.x, end.y, center.z);
		case MeshSplitSettings::PART_CENTER_BOTTOM:
			return Vector3(center.x, p_bounds.position.y, center.z);
		case MeshSplitSettings::PART_CENTER_X_FRONT:
			return Vector3(end.x, center.y, center.z);
		case MeshSplitSettings::PART_CENTER_Y_FRONT:
			return Vector3(center.x, end.y, center.z);
		case MeshSplitSettings::PART_CENTER_Z_FRONT:
			return Vector3(center.x, center.y, end.z);
		case MeshSplitSettings::PART_CENTER_X_BACK:
			return Vector3(p_bounds.position.x, center.y, center.z);
		case MeshSplitSettings::PART_CENTER_Y_BACK:
			return Vector3(center.x, p_bounds.position.y, center.z);
		case MeshSplitSettings::PART_CENTER_Z_BACK:
			return Vector3(center.x, center.y, p_bounds.position.z);
	}
	return center;
}

struct TriangleReference {
	int surface = 0;
	int triangle = 0;
};

static int _island_find(Vector<int> &r_parents, int p_index) {
	int root = p_index;
	while (r_parents[root] != root) {
		root = r_parents[root];
	}
	while (r_parents[p_index] != p_index) {
		const int next = r_parents[p_index];
		r_parents.write[p_index] = root;
		p_index = next;
	}
	return root;
}

static void _island_union(Vector<int> &r_parents, int p_a, int p_b) {
	const int root_a = _island_find(r_parents, p_a);
	const int root_b = _island_find(r_parents, p_b);
	if (root_a != root_b) {
		r_parents.write[root_b] = root_a;
	}
}

static Vector<SplitPiece> _decompose_piece(const SplitPiece &p_piece, real_t p_tolerance) {
	Vector<TriangleReference> references;
	for (int surface_i = 0; surface_i < p_piece.surfaces.size(); surface_i++) {
		for (int triangle_i = 0; triangle_i < p_piece.surfaces[surface_i].triangles.size(); triangle_i++) {
			references.push_back({ surface_i, triangle_i });
		}
	}
	Vector<int> parents;
	parents.resize(references.size());
	for (int triangle_i = 0; triangle_i < parents.size(); triangle_i++) {
		parents.write[triangle_i] = triangle_i;
	}
	HashMap<Vector3i, int> first_triangle;
	for (int reference_i = 0; reference_i < references.size(); reference_i++) {
		const TriangleReference &reference = references[reference_i];
		const SplitTriangle &triangle = p_piece.surfaces[reference.surface].triangles[reference.triangle];
		for (int corner = 0; corner < 3; corner++) {
			const Vector3 &point = triangle.vertices[corner].position;
			const Vector3i key(
					Math::round(point.x / p_tolerance),
					Math::round(point.y / p_tolerance),
					Math::round(point.z / p_tolerance));
			if (const int *other = first_triangle.getptr(key)) {
				_island_union(parents, reference_i, *other);
			} else {
				first_triangle.insert(key, reference_i);
			}
		}
	}
	HashMap<int, int> component_indices;
	Vector<SplitPiece> components;
	for (int reference_i = 0; reference_i < references.size(); reference_i++) {
		const int root = _island_find(parents, reference_i);
		int *component_index = component_indices.getptr(root);
		if (!component_index) {
			SplitPiece component;
			component.surfaces = p_piece.surfaces;
			for (SplitSurface &surface : component.surfaces) {
				surface.triangles.clear();
			}
			const int new_index = components.size();
			components.push_back(component);
			component_indices.insert(root, new_index);
			component_index = component_indices.getptr(root);
		}
		const TriangleReference &reference = references[reference_i];
		components.write[*component_index].surfaces.write[reference.surface].triangles.push_back(p_piece.surfaces[reference.surface].triangles[reference.triangle]);
	}
	for (SplitPiece &component : components) {
		Vector<SplitSurface> non_empty_surfaces;
		for (const SplitSurface &surface : component.surfaces) {
			if (!surface.triangles.is_empty()) {
				non_empty_surfaces.push_back(surface);
			}
		}
		component.surfaces = non_empty_surfaces;
	}
	return components;
}

static Ref<ArrayMesh> _build_mesh(SplitPiece p_piece, MeshSplitSettings::PartCenter p_center_mode) {
	const Vector3 center = _part_center(_piece_bounds(p_piece), p_center_mode);
	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	for (SplitSurface &surface : p_piece.surfaces) {
		if (surface.triangles.is_empty()) {
			continue;
		}
		const int vertex_count = surface.triangles.size() * 3;
		PackedVector3Array vertices;
		PackedVector3Array normals;
		PackedFloat32Array tangents;
		PackedColorArray colors;
		PackedVector2Array uvs;
		PackedVector2Array uv2s;
		Vector<Vector4> customs[4];
		vertices.resize(vertex_count);
		if (surface.has_normal) {
			normals.resize(vertex_count);
		}
		if (surface.has_tangent) {
			tangents.resize(vertex_count * 4);
		}
		if (surface.has_color) {
			colors.resize(vertex_count);
		}
		if (surface.has_uv) {
			uvs.resize(vertex_count);
		}
		if (surface.has_uv2) {
			uv2s.resize(vertex_count);
		}
		for (int channel = 0; channel < 4; channel++) {
			if (surface.has_custom[channel]) {
				customs[channel].resize(vertex_count);
			}
		}
		int vertex_i = 0;
		for (const SplitTriangle &triangle : surface.triangles) {
			for (int corner = 0; corner < 3; corner++, vertex_i++) {
				const SplitVertex &vertex = triangle.vertices[corner];
				vertices.set(vertex_i, vertex.position - center);
				if (surface.has_normal) {
					normals.set(vertex_i, vertex.normal);
				}
				if (surface.has_tangent) {
					for (int component = 0; component < 4; component++) {
						tangents.set(vertex_i * 4 + component, vertex.tangent[component]);
					}
				}
				if (surface.has_color) {
					colors.set(vertex_i, vertex.color);
				}
				if (surface.has_uv) {
					uvs.set(vertex_i, vertex.uv);
				}
				if (surface.has_uv2) {
					uv2s.set(vertex_i, vertex.uv2);
				}
				for (int channel = 0; channel < 4; channel++) {
					if (surface.has_custom[channel]) {
						customs[channel].write[vertex_i] = vertex.custom[channel];
					}
				}
			}
		}
		Array arrays;
		arrays.resize(Mesh::ARRAY_MAX);
		arrays[Mesh::ARRAY_VERTEX] = vertices;
		if (surface.has_normal) {
			arrays[Mesh::ARRAY_NORMAL] = normals;
		}
		if (surface.has_tangent) {
			arrays[Mesh::ARRAY_TANGENT] = tangents;
		}
		if (surface.has_color) {
			arrays[Mesh::ARRAY_COLOR] = colors;
		}
		if (surface.has_uv) {
			arrays[Mesh::ARRAY_TEX_UV] = uvs;
		}
		if (surface.has_uv2) {
			arrays[Mesh::ARRAY_TEX_UV2] = uv2s;
		}
		uint64_t format_flags = 0;
		for (int channel = 0; channel < 4; channel++) {
			if (surface.has_custom[channel]) {
				arrays[Mesh::ARRAY_CUSTOM0 + channel] = _pack_custom(customs[channel], surface.custom_format[channel]);
				format_flags |= uint64_t(surface.custom_format[channel]) << (Mesh::ARRAY_FORMAT_CUSTOM0_SHIFT + channel * Mesh::ARRAY_FORMAT_CUSTOM_BITS);
			}
		}
		const int output_surface = mesh->get_surface_count();
		mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays, Array(), Dictionary(), format_flags);
		mesh->surface_set_material(output_surface, surface.material);
		mesh->surface_set_name(output_surface, surface.name);
	}
	mesh->set_meta(SNAME("split_center"), center);
	return mesh;
}

} // namespace

void MeshSplitter::_bind_methods() {
	ClassDB::bind_method(D_METHOD("split_mesh", "mesh", "settings"), &MeshSplitter::split_mesh, DEFVAL(Ref<MeshSplitSettings>()));
}

TypedArray<ArrayMesh> MeshSplitter::split_mesh(const Ref<Mesh> &p_mesh, const Ref<MeshSplitSettings> &p_settings) const {
	TypedArray<ArrayMesh> result;
	ERR_FAIL_COND_V_MSG(p_mesh.is_null(), result, "Cannot split a null mesh.");
	Ref<MeshSplitSettings> settings = p_settings;
	if (settings.is_null()) {
		settings.instantiate();
	}
	SplitPiece source;
	ERR_FAIL_COND_V_MSG(!_read_mesh(p_mesh, source), result, "Mesh has no triangle surfaces to split.");
	Vector<SplitPiece> pieces;
	pieces.push_back(source);
	RandomPCG rng(settings->get_seed());
	for (int iteration = 0; iteration < settings->get_iterations(); iteration++) {
		Vector<SplitPiece> next;
		for (int piece_i = 0; piece_i < pieces.size(); piece_i++) {
			const int remaining = pieces.size() - piece_i;
			if (next.size() + remaining >= settings->get_max_pieces()) {
				next.push_back(pieces[piece_i]);
				continue;
			}
			const AABB bounds = _piece_bounds(pieces[piece_i]);
			const Vector3 base_normal = _balanced_normal(bounds);
			const int attempts = settings->get_mode() == MeshSplitSettings::MODE_RANDOM_PLANES ? settings->get_retry_count() : 1;
			bool split = false;
			for (int attempt = 0; attempt < attempts; attempt++) {
				const Vector3 normal = settings->get_mode() == MeshSplitSettings::MODE_RANDOM_PLANES ? _random_normal(base_normal, settings->get_rotation_jitter(), rng) : base_normal;
				Vector3 plane_point = bounds.get_center();
				if (settings->get_mode() == MeshSplitSettings::MODE_RANDOM_PLANES) {
					const real_t extent = (Math::abs(normal.x) * bounds.size.x + Math::abs(normal.y) * bounds.size.y + Math::abs(normal.z) * bounds.size.z) * 0.5;
					plane_point += normal * (rng.random(-settings->get_offset_jitter(), settings->get_offset_jitter()) * extent);
				}
				SplitPiece positive;
				SplitPiece negative;
				if (_split_piece(pieces[piece_i], Plane(normal, normal.dot(plane_point)), settings, positive, negative)) {
					next.push_back(positive);
					next.push_back(negative);
					split = true;
					break;
				}
			}
			if (!split) {
				next.push_back(pieces[piece_i]);
			}
		}
		pieces = next;
	}
	if (settings->is_decomposing_islands()) {
		Vector<SplitPiece> decomposed;
		for (int piece_i = 0; piece_i < pieces.size(); piece_i++) {
			const SplitPiece &piece = pieces[piece_i];
			Vector<SplitPiece> components = _decompose_piece(piece, settings->get_weld_tolerance());
			const int remaining_pieces = pieces.size() - piece_i - 1;
			if (components.size() > 1 && decomposed.size() + components.size() + remaining_pieces <= settings->get_max_pieces()) {
				decomposed.append_array(components);
			} else {
				decomposed.push_back(piece);
			}
		}
		pieces = decomposed;
	}
	for (SplitPiece &piece : pieces) {
		Ref<ArrayMesh> mesh = _build_mesh(piece, settings->get_part_center());
		if (mesh.is_valid() && mesh->get_surface_count() > 0) {
			result.push_back(mesh);
		}
	}
	return result;
}
