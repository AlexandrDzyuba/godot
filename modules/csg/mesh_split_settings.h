/**************************************************************************/
/*  mesh_split_settings.h                                                 */
/**************************************************************************/

#pragma once

#include "core/io/resource.h"
#include "scene/resources/material.h"

class MeshSplitSettings : public Resource {
	GDCLASS(MeshSplitSettings, Resource);

public:
	enum Mode {
		MODE_BALANCED_GRID,
		MODE_RANDOM_PLANES,
	};
	enum CapMode {
		CAP_NONE,
		CAP_CLOSED_LOOPS,
		CAP_REPAIR_OPEN_LOOPS,
	};
	enum PartCenter {
		PART_CENTER_ORIGINAL,
		PART_CENTER_AABB,
		PART_CENTER_TOP,
		PART_CENTER_BOTTOM,
		PART_CENTER_X_FRONT,
		PART_CENTER_Y_FRONT,
		PART_CENTER_Z_FRONT,
		PART_CENTER_X_BACK,
		PART_CENTER_Y_BACK,
		PART_CENTER_Z_BACK,
	};
	enum Output {
		OUTPUT_MESHES,
		OUTPUT_RIGID_BODIES,
	};

private:
	Mode mode = MODE_BALANCED_GRID;
	int iterations = 3;
	uint64_t seed = 1;
	real_t rotation_jitter = Math::deg_to_rad(35.0);
	real_t offset_jitter = 0.35;
	int max_pieces = 256;
	int retry_count = 8;
	int min_triangles = 1;
	bool decompose_islands = true;
	real_t weld_tolerance = 0.0001;
	CapMode cap_mode = CAP_REPAIR_OPEN_LOOPS;
	Ref<Material> cap_material;
	real_t cap_uv_scale = 1.0;
	Color cap_color = Color(1, 1, 1, 1);
	bool cap_custom_enabled[4] = {};
	Vector4 cap_custom[4];
	PartCenter part_center = PART_CENTER_AABB;
	Output output = OUTPUT_MESHES;

protected:
	static void _bind_methods();
	void _validate_property(PropertyInfo &p_property) const;

public:
	void set_mode(Mode p_mode);
	Mode get_mode() const;
	void set_iterations(int p_iterations);
	int get_iterations() const;
	void set_seed(uint64_t p_seed);
	uint64_t get_seed() const;
	void set_rotation_jitter(real_t p_jitter);
	real_t get_rotation_jitter() const;
	void set_offset_jitter(real_t p_jitter);
	real_t get_offset_jitter() const;
	void set_max_pieces(int p_max_pieces);
	int get_max_pieces() const;
	void set_retry_count(int p_retry_count);
	int get_retry_count() const;
	void set_min_triangles(int p_min_triangles);
	int get_min_triangles() const;
	void set_decompose_islands(bool p_enabled);
	bool is_decomposing_islands() const;
	void set_weld_tolerance(real_t p_tolerance);
	real_t get_weld_tolerance() const;
	void set_cap_mode(CapMode p_mode);
	CapMode get_cap_mode() const;
	void set_cap_material(const Ref<Material> &p_material);
	Ref<Material> get_cap_material() const;
	void set_cap_uv_scale(real_t p_scale);
	real_t get_cap_uv_scale() const;
	void set_cap_color(const Color &p_color);
	Color get_cap_color() const;
	void set_cap_custom_enabled(int p_channel, bool p_enabled);
	bool is_cap_custom_enabled(int p_channel) const;
	void set_cap_custom(int p_channel, const Vector4 &p_value);
	Vector4 get_cap_custom(int p_channel) const;
	void set_part_center(PartCenter p_center);
	PartCenter get_part_center() const;
	void set_output(Output p_output);
	Output get_output() const;
};

VARIANT_ENUM_CAST(MeshSplitSettings::Mode);
VARIANT_ENUM_CAST(MeshSplitSettings::CapMode);
VARIANT_ENUM_CAST(MeshSplitSettings::PartCenter);
VARIANT_ENUM_CAST(MeshSplitSettings::Output);
