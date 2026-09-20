/**************************************************************************/
/*  csg_attribute_modifier.cpp                                            */
/**************************************************************************/

#include "csg_attribute_modifier.h"

#include "core/math/geometry_3d.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"

void CSGModifierValue::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_source", "source"), &CSGModifierValue::set_source);
	ClassDB::bind_method(D_METHOD("get_source"), &CSGModifierValue::get_source);
	ClassDB::bind_method(D_METHOD("set_constant_value", "value"), &CSGModifierValue::set_constant_value);
	ClassDB::bind_method(D_METHOD("get_constant_value"), &CSGModifierValue::get_constant_value);
	ClassDB::bind_method(D_METHOD("set_scale", "scale"), &CSGModifierValue::set_scale);
	ClassDB::bind_method(D_METHOD("get_scale"), &CSGModifierValue::get_scale);
	ClassDB::bind_method(D_METHOD("set_bias", "bias"), &CSGModifierValue::set_bias);
	ClassDB::bind_method(D_METHOD("get_bias"), &CSGModifierValue::get_bias);
	ClassDB::bind_method(D_METHOD("set_clamp_enabled", "enabled"), &CSGModifierValue::set_clamp_enabled);
	ClassDB::bind_method(D_METHOD("is_clamp_enabled"), &CSGModifierValue::is_clamp_enabled);
	ClassDB::bind_method(D_METHOD("set_min_value", "value"), &CSGModifierValue::set_min_value);
	ClassDB::bind_method(D_METHOD("get_min_value"), &CSGModifierValue::get_min_value);
	ClassDB::bind_method(D_METHOD("set_max_value", "value"), &CSGModifierValue::set_max_value);
	ClassDB::bind_method(D_METHOD("get_max_value"), &CSGModifierValue::get_max_value);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "source", PROPERTY_HINT_ENUM, "Constant,Brush ID,Face ID,Source Face ID,Surface ID,Material ID,Face Generation,Triangle Edge Distance 0,Triangle Edge Distance 1,Triangle Edge Distance 2,Boundary Edge,Sharp Edge,Layer ID", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_UPDATE_ALL_IF_MODIFIED), "set_source", "get_source");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "constant_value"), "set_constant_value", "get_constant_value");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "scale"), "set_scale", "get_scale");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bias"), "set_bias", "get_bias");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "clamp_enabled", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_UPDATE_ALL_IF_MODIFIED), "set_clamp_enabled", "is_clamp_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_value"), "set_min_value", "get_min_value");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "max_value"), "set_max_value", "get_max_value");

	BIND_ENUM_CONSTANT(SOURCE_CONSTANT);
	BIND_ENUM_CONSTANT(SOURCE_BRUSH_ID);
	BIND_ENUM_CONSTANT(SOURCE_FACE_ID);
	BIND_ENUM_CONSTANT(SOURCE_SOURCE_FACE_ID);
	BIND_ENUM_CONSTANT(SOURCE_SURFACE_ID);
	BIND_ENUM_CONSTANT(SOURCE_MATERIAL_ID);
	BIND_ENUM_CONSTANT(SOURCE_FACE_GENERATION);
	BIND_ENUM_CONSTANT(SOURCE_TRIANGLE_EDGE_DISTANCE_0);
	BIND_ENUM_CONSTANT(SOURCE_TRIANGLE_EDGE_DISTANCE_1);
	BIND_ENUM_CONSTANT(SOURCE_TRIANGLE_EDGE_DISTANCE_2);
	BIND_ENUM_CONSTANT(SOURCE_BOUNDARY_EDGE);
	BIND_ENUM_CONSTANT(SOURCE_SHARP_EDGE);
	BIND_ENUM_CONSTANT(SOURCE_LAYER_ID);
}

void CSGModifierValue::_validate_property(PropertyInfo &p_property) const {
	if (p_property.name == "constant_value" && source != SOURCE_CONSTANT) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
	if ((p_property.name == "min_value" || p_property.name == "max_value") && !clamp_enabled) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
}

#define CSG_VALUE_SETTER(m_name, m_type, m_field) \
	void CSGModifierValue::set_##m_name(m_type p_value) { \
		if (m_field == p_value) { \
			return; \
		} \
		m_field = p_value; \
		emit_changed(); \
	}

CSG_VALUE_SETTER(constant_value, real_t, constant_value)
CSG_VALUE_SETTER(scale, real_t, scale)
CSG_VALUE_SETTER(bias, real_t, bias)
CSG_VALUE_SETTER(min_value, real_t, min_value)
CSG_VALUE_SETTER(max_value, real_t, max_value)

#undef CSG_VALUE_SETTER

CSGModifierValue::Source CSGModifierValue::get_source() const {
	return source;
}
real_t CSGModifierValue::get_constant_value() const {
	return constant_value;
}
real_t CSGModifierValue::get_scale() const {
	return scale;
}
real_t CSGModifierValue::get_bias() const {
	return bias;
}
bool CSGModifierValue::is_clamp_enabled() const {
	return clamp_enabled;
}
real_t CSGModifierValue::get_min_value() const {
	return min_value;
}
real_t CSGModifierValue::get_max_value() const {
	return max_value;
}

void CSGModifierValue::set_source(Source p_source) {
	ERR_FAIL_INDEX(int(p_source), int(SOURCE_LAYER_ID) + 1);
	if (source == p_source) {
		return;
	}
	source = p_source;
	notify_property_list_changed();
	emit_changed();
}

void CSGModifierValue::set_clamp_enabled(bool p_enabled) {
	if (clamp_enabled == p_enabled) {
		return;
	}
	clamp_enabled = p_enabled;
	notify_property_list_changed();
	emit_changed();
}

real_t CSGModifierValue::transform(real_t p_value) const {
	real_t value = p_value * scale + bias;
	if (clamp_enabled) {
		value = CLAMP(value, MIN(min_value, max_value), MAX(min_value, max_value));
	}
	return value;
}

void CSGModifierChannelOverride::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_red", "value"), &CSGModifierChannelOverride::set_red);
	ClassDB::bind_method(D_METHOD("get_red"), &CSGModifierChannelOverride::get_red);
	ClassDB::bind_method(D_METHOD("set_green", "value"), &CSGModifierChannelOverride::set_green);
	ClassDB::bind_method(D_METHOD("get_green"), &CSGModifierChannelOverride::get_green);
	ClassDB::bind_method(D_METHOD("set_blue", "value"), &CSGModifierChannelOverride::set_blue);
	ClassDB::bind_method(D_METHOD("get_blue"), &CSGModifierChannelOverride::get_blue);
	ClassDB::bind_method(D_METHOD("set_alpha", "value"), &CSGModifierChannelOverride::set_alpha);
	ClassDB::bind_method(D_METHOD("get_alpha"), &CSGModifierChannelOverride::get_alpha);
	ClassDB::bind_method(D_METHOD("set_custom_format", "format"), &CSGModifierChannelOverride::set_custom_format);
	ClassDB::bind_method(D_METHOD("get_custom_format"), &CSGModifierChannelOverride::get_custom_format);
	ClassDB::bind_method(D_METHOD("set_constant_color", "color"), &CSGModifierChannelOverride::set_constant_color);
	ClassDB::bind_method(D_METHOD("get_constant_color"), &CSGModifierChannelOverride::get_constant_color);

	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "constant_color"), "set_constant_color", "get_constant_color");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "red", PROPERTY_HINT_RESOURCE_TYPE, "CSGModifierValue"), "set_red", "get_red");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "green", PROPERTY_HINT_RESOURCE_TYPE, "CSGModifierValue"), "set_green", "get_green");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "blue", PROPERTY_HINT_RESOURCE_TYPE, "CSGModifierValue"), "set_blue", "get_blue");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "alpha", PROPERTY_HINT_RESOURCE_TYPE, "CSGModifierValue"), "set_alpha", "get_alpha");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "custom_format", PROPERTY_HINT_ENUM, "RGBA8 UNORM,RGBA8 SNORM,RG Half,RGBA Half,R Float,RG Float,RGB Float,RGBA Float"), "set_custom_format", "get_custom_format");
}

CSGModifierChannelOverride::CSGModifierChannelOverride() {
	red.instantiate();
	green.instantiate();
	blue.instantiate();
	alpha.instantiate();
	alpha->set_constant_value(1.0);
	Callable changed = callable_mp(this, &CSGModifierChannelOverride::_value_changed);
	red->connect(StringName("changed"), changed);
	green->connect(StringName("changed"), changed);
	blue->connect(StringName("changed"), changed);
	alpha->connect(StringName("changed"), changed);
}

void CSGModifierChannelOverride::_value_changed() {
	emit_changed();
}

void CSGModifierChannelOverride::_set_value(Ref<CSGModifierValue> &r_target, const Ref<CSGModifierValue> &p_value) {
	if (r_target == p_value) {
		return;
	}
	Callable changed = callable_mp(this, &CSGModifierChannelOverride::_value_changed);
	if (r_target.is_valid() && r_target->is_connected(StringName("changed"), changed)) {
		r_target->disconnect(StringName("changed"), changed);
	}
	r_target = p_value;
	if (r_target.is_valid() && !r_target->is_connected(StringName("changed"), changed)) {
		r_target->connect(StringName("changed"), changed);
	}
	emit_changed();
}

void CSGModifierChannelOverride::set_red(const Ref<CSGModifierValue> &p_value) {
	_set_value(red, p_value);
}
Ref<CSGModifierValue> CSGModifierChannelOverride::get_red() const {
	return red;
}
void CSGModifierChannelOverride::set_green(const Ref<CSGModifierValue> &p_value) {
	_set_value(green, p_value);
}
Ref<CSGModifierValue> CSGModifierChannelOverride::get_green() const {
	return green;
}
void CSGModifierChannelOverride::set_blue(const Ref<CSGModifierValue> &p_value) {
	_set_value(blue, p_value);
}
Ref<CSGModifierValue> CSGModifierChannelOverride::get_blue() const {
	return blue;
}
void CSGModifierChannelOverride::set_alpha(const Ref<CSGModifierValue> &p_value) {
	_set_value(alpha, p_value);
}
Ref<CSGModifierValue> CSGModifierChannelOverride::get_alpha() const {
	return alpha;
}

void CSGModifierChannelOverride::set_custom_format(Mesh::ArrayCustomFormat p_format) {
	ERR_FAIL_INDEX(int(p_format), int(Mesh::ARRAY_CUSTOM_MAX));
	if (custom_format == p_format) {
		return;
	}
	custom_format = p_format;
	emit_changed();
}

Mesh::ArrayCustomFormat CSGModifierChannelOverride::get_custom_format() const {
	return custom_format;
}

void CSGModifierChannelOverride::set_constant_color(const Color &p_color) {
	const real_t components[4] = { p_color.r, p_color.g, p_color.b, p_color.a };
	for (int i = 0; i < 4; i++) {
		Ref<CSGModifierValue> value = get_component(i);
		if (value.is_valid()) {
			value->set_constant_value(components[i]);
		}
	}
}

Color CSGModifierChannelOverride::get_constant_color() const {
	return Color(
			red.is_valid() ? red->get_constant_value() : 0.0,
			green.is_valid() ? green->get_constant_value() : 0.0,
			blue.is_valid() ? blue->get_constant_value() : 0.0,
			alpha.is_valid() ? alpha->get_constant_value() : 0.0);
}

Ref<CSGModifierValue> CSGModifierChannelOverride::get_component(int p_component) const {
	switch (p_component) {
		case 0:
			return red;
		case 1:
			return green;
		case 2:
			return blue;
		case 3:
			return alpha;
		default:
			return Ref<CSGModifierValue>();
	}
}

void CSGAttributeModifier::_bind_methods() {
	static const char *source_hint = "Constant,Brush ID,Face ID,Source Face ID,Surface ID,Material ID,Face Generation,Triangle Edge Distance 0,Triangle Edge Distance 1,Triangle Edge Distance 2,Boundary Edge,Sharp Edge,Layer ID";
	ClassDB::bind_method(D_METHOD("set_vertex_color_override_enabled", "enabled"), &CSGAttributeModifier::set_vertex_color_override_enabled);
	ClassDB::bind_method(D_METHOD("is_vertex_color_override_enabled"), &CSGAttributeModifier::is_vertex_color_override_enabled);
	ClassDB::bind_method(D_METHOD("set_vertex_color_override", "override"), &CSGAttributeModifier::set_vertex_color_override);
	ClassDB::bind_method(D_METHOD("get_vertex_color_override"), &CSGAttributeModifier::get_vertex_color_override);
	ClassDB::bind_method(D_METHOD("set_custom_override_enabled", "channel", "enabled"), &CSGAttributeModifier::set_custom_override_enabled);
	ClassDB::bind_method(D_METHOD("is_custom_override_enabled", "channel"), &CSGAttributeModifier::is_custom_override_enabled);
	ClassDB::bind_method(D_METHOD("set_custom_override", "channel", "override"), &CSGAttributeModifier::set_custom_override);
	ClassDB::bind_method(D_METHOD("get_custom_override", "channel"), &CSGAttributeModifier::get_custom_override);
	ClassDB::bind_method(D_METHOD("set_channel_source", "slot", "source"), &CSGAttributeModifier::set_channel_source);
	ClassDB::bind_method(D_METHOD("get_channel_source", "slot"), &CSGAttributeModifier::get_channel_source);
	ClassDB::bind_method(D_METHOD("set_merge_epsilon", "epsilon"), &CSGAttributeModifier::set_merge_epsilon);
	ClassDB::bind_method(D_METHOD("get_merge_epsilon"), &CSGAttributeModifier::get_merge_epsilon);
	ClassDB::bind_method(D_METHOD("set_sharp_angle", "angle"), &CSGAttributeModifier::set_sharp_angle);
	ClassDB::bind_method(D_METHOD("get_sharp_angle"), &CSGAttributeModifier::get_sharp_angle);

	ADD_GROUP("Vertex Color", "vertex_color_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "vertex_color_override_enabled", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_UPDATE_ALL_IF_MODIFIED), "set_vertex_color_override_enabled", "is_vertex_color_override_enabled");
	const char *component_names[4] = { "red", "green", "blue", "alpha" };
	for (int component = 0; component < 4; component++) {
		ADD_PROPERTYI(PropertyInfo(Variant::INT, "vertex_color_" + String(component_names[component]) + "_source", PROPERTY_HINT_ENUM, source_hint), "set_channel_source", "get_channel_source", component);
	}
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "vertex_color_override", PROPERTY_HINT_RESOURCE_TYPE, "CSGModifierChannelOverride"), "set_vertex_color_override", "get_vertex_color_override");
	for (int i = 0; i < CSGBrush::CUSTOM_CHANNEL_COUNT; i++) {
		const String prefix = "custom" + itos(i) + "_";
		ADD_GROUP("Custom " + itos(i), prefix);
		ADD_PROPERTYI(PropertyInfo(Variant::BOOL, prefix + "override_enabled", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_UPDATE_ALL_IF_MODIFIED), "set_custom_override_enabled", "is_custom_override_enabled", i);
		for (int component = 0; component < 4; component++) {
			ADD_PROPERTYI(PropertyInfo(Variant::INT, prefix + component_names[component] + "_source", PROPERTY_HINT_ENUM, source_hint), "set_channel_source", "get_channel_source", (i + 1) * 4 + component);
		}
		ADD_PROPERTYI(PropertyInfo(Variant::OBJECT, prefix + "override", PROPERTY_HINT_RESOURCE_TYPE, "CSGModifierChannelOverride"), "set_custom_override", "get_custom_override", i);
	}
	ADD_GROUP("Topology", "topology_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "topology_merge_epsilon", PROPERTY_HINT_RANGE, "0.00000001,0.01,0.00000001,or_greater,suffix:m"), "set_merge_epsilon", "get_merge_epsilon");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "topology_sharp_angle", PROPERTY_HINT_RANGE, "0,180,0.1,radians_as_degrees"), "set_sharp_angle", "get_sharp_angle");
}

CSGAttributeModifier::CSGAttributeModifier() {
	Callable changed = callable_mp(this, &CSGAttributeModifier::_channel_changed);
	for (int i = 0; i < CSGBrush::CUSTOM_CHANNEL_COUNT + 1; i++) {
		channels[i].instantiate();
		channels[i]->connect(StringName("changed"), changed);
	}
}

void CSGAttributeModifier::_channel_changed() {
	emit_changed();
}

void CSGAttributeModifier::_set_channel(int p_channel, const Ref<CSGModifierChannelOverride> &p_override) {
	ERR_FAIL_INDEX(p_channel, CSGBrush::CUSTOM_CHANNEL_COUNT + 1);
	if (channels[p_channel] == p_override) {
		return;
	}
	Callable changed = callable_mp(this, &CSGAttributeModifier::_channel_changed);
	if (channels[p_channel].is_valid() && channels[p_channel]->is_connected(StringName("changed"), changed)) {
		channels[p_channel]->disconnect(StringName("changed"), changed);
	}
	channels[p_channel] = p_override;
	if (channels[p_channel].is_valid() && !channels[p_channel]->is_connected(StringName("changed"), changed)) {
		channels[p_channel]->connect(StringName("changed"), changed);
	}
	emit_changed();
}

void CSGAttributeModifier::_set_channel_enabled(int p_channel, bool p_enabled) {
	ERR_FAIL_INDEX(p_channel, CSGBrush::CUSTOM_CHANNEL_COUNT + 1);
	if (channel_enabled[p_channel] == p_enabled) {
		return;
	}
	channel_enabled[p_channel] = p_enabled;
	notify_property_list_changed();
	emit_changed();
}

void CSGAttributeModifier::set_vertex_color_override_enabled(bool p_enabled) {
	_set_channel_enabled(0, p_enabled);
}
bool CSGAttributeModifier::is_vertex_color_override_enabled() const {
	return channel_enabled[0];
}
void CSGAttributeModifier::set_vertex_color_override(const Ref<CSGModifierChannelOverride> &p_override) {
	_set_channel(0, p_override);
}
Ref<CSGModifierChannelOverride> CSGAttributeModifier::get_vertex_color_override() const {
	return channels[0];
}
void CSGAttributeModifier::set_custom_override_enabled(int p_channel, bool p_enabled) {
	ERR_FAIL_INDEX(p_channel, CSGBrush::CUSTOM_CHANNEL_COUNT);
	_set_channel_enabled(p_channel + 1, p_enabled);
}
bool CSGAttributeModifier::is_custom_override_enabled(int p_channel) const {
	ERR_FAIL_INDEX_V(p_channel, CSGBrush::CUSTOM_CHANNEL_COUNT, false);
	return channel_enabled[p_channel + 1];
}
void CSGAttributeModifier::set_custom_override(int p_channel, const Ref<CSGModifierChannelOverride> &p_override) {
	ERR_FAIL_INDEX(p_channel, CSGBrush::CUSTOM_CHANNEL_COUNT);
	_set_channel(p_channel + 1, p_override);
}
Ref<CSGModifierChannelOverride> CSGAttributeModifier::get_custom_override(int p_channel) const {
	ERR_FAIL_INDEX_V(p_channel, CSGBrush::CUSTOM_CHANNEL_COUNT, Ref<CSGModifierChannelOverride>());
	return channels[p_channel + 1];
}

void CSGAttributeModifier::set_channel_source(int p_slot, CSGModifierValue::Source p_source) {
	ERR_FAIL_INDEX(p_slot, (CSGBrush::CUSTOM_CHANNEL_COUNT + 1) * 4);
	const int channel = p_slot / 4;
	const int component = p_slot % 4;
	ERR_FAIL_COND(channels[channel].is_null());
	Ref<CSGModifierValue> value = channels[channel]->get_component(component);
	ERR_FAIL_COND(value.is_null());
	value->set_source(p_source);
}

CSGModifierValue::Source CSGAttributeModifier::get_channel_source(int p_slot) const {
	ERR_FAIL_INDEX_V(p_slot, (CSGBrush::CUSTOM_CHANNEL_COUNT + 1) * 4, CSGModifierValue::SOURCE_CONSTANT);
	const int channel = p_slot / 4;
	const int component = p_slot % 4;
	if (channels[channel].is_null()) {
		return CSGModifierValue::SOURCE_CONSTANT;
	}
	Ref<CSGModifierValue> value = channels[channel]->get_component(component);
	return value.is_valid() ? value->get_source() : CSGModifierValue::SOURCE_CONSTANT;
}

void CSGAttributeModifier::set_merge_epsilon(real_t p_epsilon) {
	p_epsilon = MAX(p_epsilon, real_t(0.00000001));
	if (merge_epsilon != p_epsilon) {
		merge_epsilon = p_epsilon;
		emit_changed();
	}
}
real_t CSGAttributeModifier::get_merge_epsilon() const {
	return merge_epsilon;
}
void CSGAttributeModifier::set_sharp_angle(real_t p_angle) {
	p_angle = CLAMP(p_angle, real_t(0.0), real_t(Math::PI));
	if (sharp_angle != p_angle) {
		sharp_angle = p_angle;
		emit_changed();
	}
}
real_t CSGAttributeModifier::get_sharp_angle() const {
	return sharp_angle;
}

void CSGAttributeModifier::_validate_property(PropertyInfo &p_property) const {
	if (p_property.name == "vertex_color_override" && !channel_enabled[0]) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
	if (p_property.name.begins_with("vertex_color_") && p_property.name.ends_with("_source") && !channel_enabled[0]) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
	for (int i = 0; i < CSGBrush::CUSTOM_CHANNEL_COUNT; i++) {
		const String prefix = "custom" + itos(i) + "_";
		if ((p_property.name == prefix + "override" || (p_property.name.begins_with(prefix) && p_property.name.ends_with("_source"))) && !channel_enabled[i + 1]) {
			p_property.usage = PROPERTY_USAGE_NO_EDITOR;
		}
	}
}

static real_t _triangle_edge_distance(const CSGBrush::Face &p_face, int p_face_index, int p_corner, int p_edge, const PackedInt32Array &p_face_edges, const PackedByteArray &p_sharp_edges, real_t p_ignored_edge_distance) {
	// Barycentric component N measures the edge opposite corner N. face_edges
	// stores the edge starting at each corner, hence the one-corner offset.
	const int topology_edge = p_face_edges[p_face_index * 3 + (p_edge + 1) % 3];
	if (topology_edge < 0 || !p_sharp_edges[topology_edge]) {
		// A constant large value removes coplanar triangulation edges from
		// min(R, G, B) without introducing a discontinuity within the triangle.
		return p_ignored_edge_distance;
	}
	if (p_corner != p_edge) {
		return 0.0;
	}
	const Vector3 a = p_face.vertices[(p_edge + 1) % 3];
	const Vector3 b = p_face.vertices[(p_edge + 2) % 3];
	return Geometry3D::get_closest_point_to_segment(p_face.vertices[p_corner], a, b).distance_to(p_face.vertices[p_corner]);
}

static real_t _evaluate_value(const Ref<CSGModifierValue> &p_value, const CSGBrush::Face &p_face, int p_face_index, int p_corner, const PackedInt32Array &p_face_edges, const PackedByteArray &p_boundary_edges, const PackedByteArray &p_sharp_edges, real_t p_ignored_edge_distance) {
	if (p_value.is_null()) {
		return 0.0;
	}
	real_t value = 0.0;
	switch (p_value->get_source()) {
		case CSGModifierValue::SOURCE_CONSTANT:
			value = p_value->get_constant_value();
			break;
		case CSGModifierValue::SOURCE_BRUSH_ID:
			value = p_face.metadata.brush_id;
			break;
		case CSGModifierValue::SOURCE_LAYER_ID:
			value = p_face.metadata.layer_id;
			break;
		case CSGModifierValue::SOURCE_FACE_ID:
			value = p_face.metadata.face_id;
			break;
		case CSGModifierValue::SOURCE_SOURCE_FACE_ID:
			value = p_face.metadata.source_face_id;
			break;
		case CSGModifierValue::SOURCE_SURFACE_ID:
			value = p_face.metadata.surface_id;
			break;
		case CSGModifierValue::SOURCE_MATERIAL_ID:
			value = p_face.material;
			break;
		case CSGModifierValue::SOURCE_FACE_GENERATION:
			value = p_face.metadata.generation;
			break;
		case CSGModifierValue::SOURCE_TRIANGLE_EDGE_DISTANCE_0:
			value = _triangle_edge_distance(p_face, p_face_index, p_corner, 0, p_face_edges, p_sharp_edges, p_ignored_edge_distance);
			break;
		case CSGModifierValue::SOURCE_TRIANGLE_EDGE_DISTANCE_1:
			value = _triangle_edge_distance(p_face, p_face_index, p_corner, 1, p_face_edges, p_sharp_edges, p_ignored_edge_distance);
			break;
		case CSGModifierValue::SOURCE_TRIANGLE_EDGE_DISTANCE_2:
			value = _triangle_edge_distance(p_face, p_face_index, p_corner, 2, p_face_edges, p_sharp_edges, p_ignored_edge_distance);
			break;
		case CSGModifierValue::SOURCE_BOUNDARY_EDGE:
		case CSGModifierValue::SOURCE_SHARP_EDGE: {
			const int edge_a = p_face_edges[p_face_index * 3 + p_corner];
			const int edge_b = p_face_edges[p_face_index * 3 + (p_corner + 2) % 3];
			const PackedByteArray &flags = p_value->get_source() == CSGModifierValue::SOURCE_BOUNDARY_EDGE ? p_boundary_edges : p_sharp_edges;
			value = (edge_a >= 0 && flags[edge_a]) || (edge_b >= 0 && flags[edge_b]) ? 1.0 : 0.0;
		} break;
	}
	return p_value->transform(value);
}

void CSGAttributeModifier::process(const Ref<CSGModifierContext> &p_context) {
	if (!is_enabled() || p_context.is_null()) {
		return;
	}
	bool any_enabled = false;
	for (bool channel_is_enabled : channel_enabled) {
		any_enabled |= channel_is_enabled;
	}
	if (!any_enabled) {
		return;
	}
	CSGBrush *brush = p_context->get_brush();
	ERR_FAIL_NULL(brush);
	Ref<CSGGeometryData> geometry = p_context->get_geometry_data(merge_epsilon, sharp_angle);
	const PackedInt32Array face_edges = geometry->get_face_edges();
	const PackedByteArray boundary_edges = geometry->get_boundary_edges();
	const PackedByteArray sharp_edges = geometry->get_sharp_edges();
	real_t ignored_edge_distance = CMP_EPSILON;
	if (!brush->faces.is_empty()) {
		AABB bounds = brush->faces[0].aabb;
		for (int face_i = 1; face_i < brush->faces.size(); face_i++) {
			bounds.merge_with(brush->faces[face_i].aabb);
		}
		ignored_edge_distance = MAX(bounds.size.length(), real_t(CMP_EPSILON));
	}

	for (int face_i = 0; face_i < brush->faces.size(); face_i++) {
		CSGBrush::Face &face = brush->faces.write[face_i];
		for (int corner = 0; corner < 3; corner++) {
			for (int channel = 0; channel < CSGBrush::CUSTOM_CHANNEL_COUNT + 1; channel++) {
				if (!channel_enabled[channel] || channels[channel].is_null()) {
					continue;
				}
				Vector4 result;
				for (int component = 0; component < 4; component++) {
					result[component] = _evaluate_value(channels[channel]->get_component(component), face, face_i, corner, face_edges, boundary_edges, sharp_edges, ignored_edge_distance);
				}
				if (channel == 0) {
					face.colors[corner] = Color(result.x, result.y, result.z, result.w);
				} else {
					face.customs[channel - 1][corner] = result;
				}
			}
		}
	}
	if (channel_enabled[0]) {
		brush->has_colors = true;
	}
	for (int channel = 1; channel < CSGBrush::CUSTOM_CHANNEL_COUNT + 1; channel++) {
		if (channel_enabled[channel] && channels[channel].is_valid()) {
			brush->custom_channels |= 1u << (channel - 1);
			brush->custom_formats[channel - 1] = channels[channel]->get_custom_format();
		}
	}
}

void CSGFaceSemanticModifier::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_original_semantic", "semantic"), &CSGFaceSemanticModifier::set_original_semantic);
	ClassDB::bind_method(D_METHOD("get_original_semantic"), &CSGFaceSemanticModifier::get_original_semantic);
	ClassDB::bind_method(D_METHOD("set_boolean_semantic", "semantic"), &CSGFaceSemanticModifier::set_boolean_semantic);
	ClassDB::bind_method(D_METHOD("get_boolean_semantic"), &CSGFaceSemanticModifier::get_boolean_semantic);
	ClassDB::bind_method(D_METHOD("set_bevel_semantic", "semantic"), &CSGFaceSemanticModifier::set_bevel_semantic);
	ClassDB::bind_method(D_METHOD("get_bevel_semantic"), &CSGFaceSemanticModifier::get_bevel_semantic);
	ClassDB::bind_method(D_METHOD("set_topology_semantic", "semantic"), &CSGFaceSemanticModifier::set_topology_semantic);
	ClassDB::bind_method(D_METHOD("get_topology_semantic"), &CSGFaceSemanticModifier::get_topology_semantic);
	ClassDB::bind_method(D_METHOD("set_preserve_non_empty", "enabled"), &CSGFaceSemanticModifier::set_preserve_non_empty);
	ClassDB::bind_method(D_METHOD("is_preserving_non_empty"), &CSGFaceSemanticModifier::is_preserving_non_empty);

	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "original_semantic"), "set_original_semantic", "get_original_semantic");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "boolean_semantic"), "set_boolean_semantic", "get_boolean_semantic");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "bevel_semantic"), "set_bevel_semantic", "get_bevel_semantic");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "topology_semantic"), "set_topology_semantic", "get_topology_semantic");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "preserve_non_empty"), "set_preserve_non_empty", "is_preserving_non_empty");
}

#define CSG_SEMANTIC_SETTER(m_name, m_field) \
	void CSGFaceSemanticModifier::set_##m_name(const StringName &p_semantic) { \
		if (m_field == p_semantic) { \
			return; \
		} \
		m_field = p_semantic; \
		emit_changed(); \
	}

CSG_SEMANTIC_SETTER(original_semantic, original_semantic)
CSG_SEMANTIC_SETTER(boolean_semantic, boolean_semantic)
CSG_SEMANTIC_SETTER(bevel_semantic, bevel_semantic)
CSG_SEMANTIC_SETTER(topology_semantic, topology_semantic)

#undef CSG_SEMANTIC_SETTER

StringName CSGFaceSemanticModifier::get_original_semantic() const {
	return original_semantic;
}
StringName CSGFaceSemanticModifier::get_boolean_semantic() const {
	return boolean_semantic;
}
StringName CSGFaceSemanticModifier::get_bevel_semantic() const {
	return bevel_semantic;
}
StringName CSGFaceSemanticModifier::get_topology_semantic() const {
	return topology_semantic;
}

void CSGFaceSemanticModifier::set_preserve_non_empty(bool p_enabled) {
	if (preserve_non_empty == p_enabled) {
		return;
	}
	preserve_non_empty = p_enabled;
	emit_changed();
}

bool CSGFaceSemanticModifier::is_preserving_non_empty() const {
	return preserve_non_empty;
}

void CSGFaceSemanticModifier::process(const Ref<CSGModifierContext> &p_context) {
	if (!is_enabled() || p_context.is_null()) {
		return;
	}
	CSGBrush *brush = p_context->get_brush();
	ERR_FAIL_NULL(brush);
	for (CSGBrush::Face &face : brush->faces) {
		if (preserve_non_empty && !face.metadata.semantic.is_empty()) {
			continue;
		}
		switch (face.metadata.generation) {
			case CSGBrush::FACE_BOOLEAN_GENERATED:
				face.metadata.semantic = boolean_semantic;
				break;
			case CSGBrush::FACE_BEVEL_GENERATED:
				face.metadata.semantic = bevel_semantic;
				break;
			case CSGBrush::FACE_TOPOLOGY_GENERATED:
				face.metadata.semantic = topology_semantic;
				break;
			default:
				face.metadata.semantic = original_semantic;
				break;
		}
	}
	p_context->invalidate_geometry_data();
}
