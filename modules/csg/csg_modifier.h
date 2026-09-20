/**************************************************************************/
/*  csg_modifier.h                                                        */
/**************************************************************************/

#pragma once

#include "csg.h"
#include "csg_geometry_data.h"

#include "core/io/resource.h"
#include "core/math/math_funcs.h"
#include "core/object/gdvirtual.gen.h"
#include "core/object/ref_counted.h"
#include "core/variant/typed_array.h"

class CSGModifierContext : public RefCounted {
	GDCLASS(CSGModifierContext, RefCounted);
	friend class CSGAttributeModifier;
	friend class CSGFaceSemanticModifier;

	CSGBrush *brush = nullptr;
	mutable Ref<CSGGeometryData> geometry_data;
	mutable real_t geometry_merge_epsilon = -1.0;
	mutable real_t geometry_sharp_angle = -1.0;

	CSGBrush *get_brush() const;
	void invalidate_geometry_data();

protected:
	static void _bind_methods();

public:
	void setup(CSGBrush *p_brush);

	PackedVector3Array get_vertices() const;
	Ref<CSGGeometryData> get_geometry_data(real_t p_merge_epsilon = 0.00001, real_t p_sharp_angle = Math::deg_to_rad(30.0)) const;

	PackedInt64Array get_face_ids() const;
	PackedInt64Array get_source_face_ids() const;
	PackedInt32Array get_surface_ids() const;
	PackedInt32Array get_material_ids() const;
	PackedInt32Array get_brush_ids() const;
	PackedInt32Array get_layer_ids() const;
	PackedByteArray get_face_generation() const;
	PackedStringArray get_face_semantics() const;
	void set_face_semantics(const PackedStringArray &p_semantics);
	Array get_face_custom_metadata() const;
	void set_face_custom_metadata(const Array &p_metadata);

	bool has_colors() const;
	PackedColorArray get_colors() const;
	void set_colors(const PackedColorArray &p_colors);
	void clear_colors();

	bool has_custom(int p_channel) const;
	Mesh::ArrayCustomFormat get_custom_format(int p_channel) const;
	void set_custom_format(int p_channel, Mesh::ArrayCustomFormat p_format);
	PackedVector4Array get_custom(int p_channel) const;
	void set_custom(int p_channel, const PackedVector4Array &p_values);
	void clear_custom(int p_channel);

	int get_vertex_count() const;
	int get_face_count() const;
};

class CSGModifier : public Resource {
	GDCLASS(CSGModifier, Resource);

private:
	bool enabled = true;

protected:
	static void _bind_methods();
	GDVIRTUAL1(_process, Ref<CSGModifierContext>);

public:
	void set_enabled(bool p_enabled);
	bool is_enabled() const;

	virtual void process(const Ref<CSGModifierContext> &p_context);
};
