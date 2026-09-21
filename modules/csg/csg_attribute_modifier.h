/**************************************************************************/
/*  csg_attribute_modifier.h                                              */
/**************************************************************************/

#pragma once

#include "csg_modifier.h"

class CSGModifierValue : public Resource {
	GDCLASS(CSGModifierValue, Resource);

public:
	enum Source {
		SOURCE_CONSTANT,
		SOURCE_BRUSH_ID,
		SOURCE_FACE_ID,
		SOURCE_SOURCE_FACE_ID,
		SOURCE_SURFACE_ID,
		SOURCE_MATERIAL_ID,
		SOURCE_FACE_GENERATION,
		SOURCE_TRIANGLE_EDGE_DISTANCE_0,
		SOURCE_TRIANGLE_EDGE_DISTANCE_1,
		SOURCE_TRIANGLE_EDGE_DISTANCE_2,
		SOURCE_BOUNDARY_EDGE,
		SOURCE_SHARP_EDGE,
		SOURCE_LAYER_ID,
		SOURCE_KEEP_EXISTING,
	};

private:
	Source source = SOURCE_CONSTANT;
	real_t constant_value = 0.0;
	real_t scale = 1.0;
	real_t bias = 0.0;
	bool clamp_enabled = false;
	real_t min_value = 0.0;
	real_t max_value = 1.0;

protected:
	static void _bind_methods();
	void _validate_property(PropertyInfo &p_property) const;

public:
	void set_source(Source p_source);
	Source get_source() const;
	void set_constant_value(real_t p_value);
	real_t get_constant_value() const;
	void set_scale(real_t p_scale);
	real_t get_scale() const;
	void set_bias(real_t p_bias);
	real_t get_bias() const;
	void set_clamp_enabled(bool p_enabled);
	bool is_clamp_enabled() const;
	void set_min_value(real_t p_value);
	real_t get_min_value() const;
	void set_max_value(real_t p_value);
	real_t get_max_value() const;

	real_t transform(real_t p_value) const;
};

VARIANT_ENUM_CAST(CSGModifierValue::Source);

class CSGModifierChannelOverride : public Resource {
	GDCLASS(CSGModifierChannelOverride, Resource);

	Ref<CSGModifierValue> red;
	Ref<CSGModifierValue> green;
	Ref<CSGModifierValue> blue;
	Ref<CSGModifierValue> alpha;
	Mesh::ArrayCustomFormat custom_format = Mesh::ARRAY_CUSTOM_RGBA_FLOAT;

	void _value_changed();
	void _set_value(Ref<CSGModifierValue> &r_target, const Ref<CSGModifierValue> &p_value);

protected:
	static void _bind_methods();

public:
	CSGModifierChannelOverride();

	void set_red(const Ref<CSGModifierValue> &p_value);
	Ref<CSGModifierValue> get_red() const;
	void set_green(const Ref<CSGModifierValue> &p_value);
	Ref<CSGModifierValue> get_green() const;
	void set_blue(const Ref<CSGModifierValue> &p_value);
	Ref<CSGModifierValue> get_blue() const;
	void set_alpha(const Ref<CSGModifierValue> &p_value);
	Ref<CSGModifierValue> get_alpha() const;
	void set_custom_format(Mesh::ArrayCustomFormat p_format);
	Mesh::ArrayCustomFormat get_custom_format() const;
	void set_constant_color(const Color &p_color);
	Color get_constant_color() const;

	Ref<CSGModifierValue> get_component(int p_component) const;
};

class CSGAttributeModifier : public CSGModifier {
	GDCLASS(CSGAttributeModifier, CSGModifier);

	bool channel_enabled[CSGBrush::CUSTOM_CHANNEL_COUNT + 1] = {};
	Ref<CSGModifierChannelOverride> channels[CSGBrush::CUSTOM_CHANNEL_COUNT + 1];
	real_t merge_epsilon = 0.00001;
	real_t sharp_angle = Math::deg_to_rad(30.0);

	void _channel_changed();
	void _set_channel(int p_channel, const Ref<CSGModifierChannelOverride> &p_override);
	void _set_channel_enabled(int p_channel, bool p_enabled);

protected:
	static void _bind_methods();
	void _validate_property(PropertyInfo &p_property) const;

public:
	CSGAttributeModifier();

	void set_vertex_color_override_enabled(bool p_enabled);
	bool is_vertex_color_override_enabled() const;
	void set_vertex_color_override(const Ref<CSGModifierChannelOverride> &p_override);
	Ref<CSGModifierChannelOverride> get_vertex_color_override() const;

	void set_custom_override_enabled(int p_channel, bool p_enabled);
	bool is_custom_override_enabled(int p_channel) const;
	void set_custom_override(int p_channel, const Ref<CSGModifierChannelOverride> &p_override);
	Ref<CSGModifierChannelOverride> get_custom_override(int p_channel) const;
	void set_channel_source(int p_slot, CSGModifierValue::Source p_source);
	CSGModifierValue::Source get_channel_source(int p_slot) const;

	void set_merge_epsilon(real_t p_epsilon);
	real_t get_merge_epsilon() const;
	void set_sharp_angle(real_t p_angle);
	real_t get_sharp_angle() const;

	virtual void process(const Ref<CSGModifierContext> &p_context) override;
};

class CSGFaceSemanticModifier : public CSGModifier {
	GDCLASS(CSGFaceSemanticModifier, CSGModifier);

	StringName original_semantic;
	StringName boolean_semantic;
	StringName bevel_semantic;
	StringName topology_semantic;
	bool preserve_non_empty = true;

protected:
	static void _bind_methods();

public:
	void set_original_semantic(const StringName &p_semantic);
	StringName get_original_semantic() const;
	void set_boolean_semantic(const StringName &p_semantic);
	StringName get_boolean_semantic() const;
	void set_bevel_semantic(const StringName &p_semantic);
	StringName get_bevel_semantic() const;
	void set_topology_semantic(const StringName &p_semantic);
	StringName get_topology_semantic() const;
	void set_preserve_non_empty(bool p_enabled);
	bool is_preserving_non_empty() const;

	virtual void process(const Ref<CSGModifierContext> &p_context) override;
};
