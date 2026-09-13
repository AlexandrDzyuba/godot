/**************************************************************************/
/*  csg_modifier.h                                                        */
/**************************************************************************/

#pragma once

#include "csg.h"

#include "core/io/resource.h"
#include "core/object/gdvirtual.gen.h"
#include "core/object/ref_counted.h"
#include "core/variant/typed_array.h"

class CSGModifierContext : public RefCounted {
	GDCLASS(CSGModifierContext, RefCounted);

	CSGBrush *brush = nullptr;

	CSGBrush *get_brush() const;

protected:
	static void _bind_methods();

public:
	void setup(CSGBrush *p_brush);

	PackedVector3Array get_vertices() const;

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
