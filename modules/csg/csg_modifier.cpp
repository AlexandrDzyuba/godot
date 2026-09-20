/**************************************************************************/
/*  csg_modifier.cpp                                                      */
/**************************************************************************/
#include "csg_modifier.h"

#include "core/object/class_db.h"

void CSGModifierContext::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_vertices"), &CSGModifierContext::get_vertices);
	ClassDB::bind_method(D_METHOD("get_vertex_count"), &CSGModifierContext::get_vertex_count);
	ClassDB::bind_method(D_METHOD("get_face_count"), &CSGModifierContext::get_face_count);
	ClassDB::bind_method(D_METHOD("get_geometry_data", "merge_epsilon", "sharp_angle"), &CSGModifierContext::get_geometry_data, DEFVAL(0.00001), DEFVAL(Math::deg_to_rad(30.0)));
	ClassDB::bind_method(D_METHOD("get_face_ids"), &CSGModifierContext::get_face_ids);
	ClassDB::bind_method(D_METHOD("get_source_face_ids"), &CSGModifierContext::get_source_face_ids);
	ClassDB::bind_method(D_METHOD("get_surface_ids"), &CSGModifierContext::get_surface_ids);
	ClassDB::bind_method(D_METHOD("get_material_ids"), &CSGModifierContext::get_material_ids);
	ClassDB::bind_method(D_METHOD("get_brush_ids"), &CSGModifierContext::get_brush_ids);
	ClassDB::bind_method(D_METHOD("get_layer_ids"), &CSGModifierContext::get_layer_ids);
	ClassDB::bind_method(D_METHOD("get_face_generation"), &CSGModifierContext::get_face_generation);
	ClassDB::bind_method(D_METHOD("get_face_semantics"), &CSGModifierContext::get_face_semantics);
	ClassDB::bind_method(D_METHOD("set_face_semantics", "semantics"), &CSGModifierContext::set_face_semantics);
	ClassDB::bind_method(D_METHOD("get_face_custom_metadata"), &CSGModifierContext::get_face_custom_metadata);
	ClassDB::bind_method(D_METHOD("set_face_custom_metadata", "metadata"), &CSGModifierContext::set_face_custom_metadata);

	ClassDB::bind_method(D_METHOD("has_colors"), &CSGModifierContext::has_colors);
	ClassDB::bind_method(D_METHOD("get_colors"), &CSGModifierContext::get_colors);
	ClassDB::bind_method(D_METHOD("set_colors", "colors"), &CSGModifierContext::set_colors);
	ClassDB::bind_method(D_METHOD("clear_colors"), &CSGModifierContext::clear_colors);

	ClassDB::bind_method(D_METHOD("has_custom", "channel"), &CSGModifierContext::has_custom);
	ClassDB::bind_method(D_METHOD("get_custom_format", "channel"), &CSGModifierContext::get_custom_format);
	ClassDB::bind_method(D_METHOD("set_custom_format", "channel", "format"), &CSGModifierContext::set_custom_format);
	ClassDB::bind_method(D_METHOD("get_custom", "channel"), &CSGModifierContext::get_custom);
	ClassDB::bind_method(D_METHOD("set_custom", "channel", "values"), &CSGModifierContext::set_custom);
	ClassDB::bind_method(D_METHOD("clear_custom", "channel"), &CSGModifierContext::clear_custom);
}

void CSGModifierContext::setup(CSGBrush *p_brush) {
	brush = p_brush;
	geometry_data.unref();
	geometry_merge_epsilon = -1.0;
	geometry_sharp_angle = -1.0;
}

int CSGModifierContext::get_face_count() const {
	return brush ? brush->faces.size() : 0;
}

CSGBrush *CSGModifierContext::get_brush() const {
	return brush;
}

void CSGModifierContext::invalidate_geometry_data() {
	geometry_data.unref();
}

int CSGModifierContext::get_vertex_count() const {
	return brush ? brush->faces.size() * 3 : 0;
}

PackedVector3Array CSGModifierContext::get_vertices() const {
	PackedVector3Array vertices;
	if (!brush) {
		return vertices;
	}

	vertices.resize(get_vertex_count());
	Vector3 *verticesw = vertices.ptrw();
	int index = 0;
	for (const CSGBrush::Face &face : brush->faces) {
		for (int i = 0; i < 3; i++) {
			verticesw[index++] = face.vertices[i];
		}
	}
	return vertices;
}

Ref<CSGGeometryData> CSGModifierContext::get_geometry_data(real_t p_merge_epsilon, real_t p_sharp_angle) const {
	ERR_FAIL_NULL_V(brush, Ref<CSGGeometryData>());
	if (geometry_data.is_null() || !Math::is_equal_approx(geometry_merge_epsilon, p_merge_epsilon) || !Math::is_equal_approx(geometry_sharp_angle, p_sharp_angle)) {
		geometry_data.instantiate();
		geometry_data->build(*brush, p_merge_epsilon, p_sharp_angle);
		geometry_merge_epsilon = p_merge_epsilon;
		geometry_sharp_angle = p_sharp_angle;
	}
	return geometry_data;
}

PackedInt64Array CSGModifierContext::get_face_ids() const {
	Ref<CSGGeometryData> data = get_geometry_data();
	return data.is_valid() ? data->get_face_ids() : PackedInt64Array();
}
PackedInt64Array CSGModifierContext::get_source_face_ids() const {
	Ref<CSGGeometryData> data = get_geometry_data();
	return data.is_valid() ? data->get_source_face_ids() : PackedInt64Array();
}
PackedInt32Array CSGModifierContext::get_surface_ids() const {
	Ref<CSGGeometryData> data = get_geometry_data();
	return data.is_valid() ? data->get_surface_ids() : PackedInt32Array();
}
PackedInt32Array CSGModifierContext::get_material_ids() const {
	Ref<CSGGeometryData> data = get_geometry_data();
	return data.is_valid() ? data->get_material_ids() : PackedInt32Array();
}
PackedInt32Array CSGModifierContext::get_brush_ids() const {
	Ref<CSGGeometryData> data = get_geometry_data();
	return data.is_valid() ? data->get_brush_ids() : PackedInt32Array();
}
PackedInt32Array CSGModifierContext::get_layer_ids() const {
	Ref<CSGGeometryData> data = get_geometry_data();
	return data.is_valid() ? data->get_layer_ids() : PackedInt32Array();
}
PackedByteArray CSGModifierContext::get_face_generation() const {
	Ref<CSGGeometryData> data = get_geometry_data();
	return data.is_valid() ? data->get_face_generation() : PackedByteArray();
}

PackedStringArray CSGModifierContext::get_face_semantics() const {
	PackedStringArray semantics;
	if (!brush) {
		return semantics;
	}
	semantics.resize(brush->faces.size());
	for (int i = 0; i < brush->faces.size(); i++) {
		semantics.set(i, String(brush->faces[i].metadata.semantic));
	}
	return semantics;
}

void CSGModifierContext::set_face_semantics(const PackedStringArray &p_semantics) {
	ERR_FAIL_NULL(brush);
	ERR_FAIL_COND_MSG(p_semantics.size() != brush->faces.size(), "CSG modifier semantic count must match face count.");
	for (int i = 0; i < brush->faces.size(); i++) {
		brush->faces.write[i].metadata.semantic = p_semantics[i];
	}
	invalidate_geometry_data();
}

Array CSGModifierContext::get_face_custom_metadata() const {
	Array face_metadata_array;
	if (!brush) {
		return face_metadata_array;
	}
	face_metadata_array.resize(brush->faces.size());
	for (int i = 0; i < brush->faces.size(); i++) {
		face_metadata_array[i] = brush->faces[i].metadata.custom;
	}
	return face_metadata_array;
}

void CSGModifierContext::set_face_custom_metadata(const Array &p_metadata) {
	ERR_FAIL_NULL(brush);
	ERR_FAIL_COND_MSG(p_metadata.size() != brush->faces.size(), "CSG modifier metadata count must match face count.");
	for (int i = 0; i < brush->faces.size(); i++) {
		ERR_CONTINUE_MSG(p_metadata[i].get_type() != Variant::DICTIONARY, "Every CSG face metadata entry must be a Dictionary.");
		brush->faces.write[i].metadata.custom = p_metadata[i];
	}
	invalidate_geometry_data();
}

bool CSGModifierContext::has_colors() const {
	return brush && brush->has_colors;
}

PackedColorArray CSGModifierContext::get_colors() const {
	PackedColorArray colors;
	if (!brush || !brush->has_colors) {
		return colors;
	}

	colors.resize(get_vertex_count());
	Color *colorsw = colors.ptrw();
	int index = 0;
	for (const CSGBrush::Face &face : brush->faces) {
		for (int i = 0; i < 3; i++) {
			colorsw[index++] = face.colors[i];
		}
	}
	return colors;
}

void CSGModifierContext::set_colors(const PackedColorArray &p_colors) {
	ERR_FAIL_NULL(brush);
	ERR_FAIL_COND_MSG(p_colors.size() != get_vertex_count(), "CSG modifier color count must match vertex count.");

	const Color *colors = p_colors.ptr();
	int index = 0;
	for (CSGBrush::Face &face : brush->faces) {
		for (int i = 0; i < 3; i++) {
			face.colors[i] = colors[index++];
		}
	}
	brush->has_colors = true;
}

void CSGModifierContext::clear_colors() {
	ERR_FAIL_NULL(brush);
	brush->has_colors = false;
}

bool CSGModifierContext::has_custom(int p_channel) const {
	ERR_FAIL_INDEX_V(p_channel, CSGBrush::CUSTOM_CHANNEL_COUNT, false);
	return brush && (brush->custom_channels & (1u << p_channel));
}

Mesh::ArrayCustomFormat CSGModifierContext::get_custom_format(int p_channel) const {
	ERR_FAIL_INDEX_V(p_channel, CSGBrush::CUSTOM_CHANNEL_COUNT, Mesh::ARRAY_CUSTOM_RGBA_FLOAT);
	ERR_FAIL_NULL_V(brush, Mesh::ARRAY_CUSTOM_RGBA_FLOAT);
	return brush->custom_formats[p_channel];
}

void CSGModifierContext::set_custom_format(int p_channel, Mesh::ArrayCustomFormat p_format) {
	ERR_FAIL_INDEX(p_channel, CSGBrush::CUSTOM_CHANNEL_COUNT);
	ERR_FAIL_NULL(brush);
	ERR_FAIL_INDEX(int(p_format), int(Mesh::ARRAY_CUSTOM_MAX));
	brush->custom_formats[p_channel] = p_format;
}

PackedVector4Array CSGModifierContext::get_custom(int p_channel) const {
	PackedVector4Array values;
	ERR_FAIL_INDEX_V(p_channel, CSGBrush::CUSTOM_CHANNEL_COUNT, values);
	if (!brush || !(brush->custom_channels & (1u << p_channel))) {
		return values;
	}

	values.resize(get_vertex_count());
	Vector4 *valuesw = values.ptrw();
	int index = 0;
	for (const CSGBrush::Face &face : brush->faces) {
		for (int i = 0; i < 3; i++) {
			valuesw[index++] = face.customs[p_channel][i];
		}
	}
	return values;
}

void CSGModifierContext::set_custom(int p_channel, const PackedVector4Array &p_values) {
	ERR_FAIL_INDEX(p_channel, CSGBrush::CUSTOM_CHANNEL_COUNT);
	ERR_FAIL_NULL(brush);
	ERR_FAIL_COND_MSG(p_values.size() != get_vertex_count(), "CSG modifier custom data count must match vertex count.");

	const Vector4 *values = p_values.ptr();
	int index = 0;
	for (CSGBrush::Face &face : brush->faces) {
		for (int i = 0; i < 3; i++) {
			face.customs[p_channel][i] = values[index++];
		}
	}
	brush->custom_channels |= 1u << p_channel;
}

void CSGModifierContext::clear_custom(int p_channel) {
	ERR_FAIL_INDEX(p_channel, CSGBrush::CUSTOM_CHANNEL_COUNT);
	ERR_FAIL_NULL(brush);
	brush->custom_channels &= ~(1u << p_channel);
}

void CSGModifier::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &CSGModifier::set_enabled);
	ClassDB::bind_method(D_METHOD("is_enabled"), &CSGModifier::is_enabled);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");

	GDVIRTUAL_BIND(_process, "context");
}

void CSGModifier::set_enabled(bool p_enabled) {
	if (enabled == p_enabled) {
		return;
	}
	enabled = p_enabled;
	emit_changed();
}

bool CSGModifier::is_enabled() const {
	return enabled;
}

void CSGModifier::process(const Ref<CSGModifierContext> &p_context) {
	if (!enabled) {
		return;
	}
	GDVIRTUAL_CALL(_process, p_context);
}
