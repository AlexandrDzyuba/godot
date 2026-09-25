/**************************************************************************/
/*  csg_split_settings.h                                                  */
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

#pragma once

#include "csg_modifier.h"

#include "core/io/resource.h"

class CSGSplitSettings : public Resource {
	GDCLASS(CSGSplitSettings, Resource);

public:
	enum Mode {
		MODE_BALANCED_GRID,
		MODE_RANDOM_PLANES,
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
	real_t min_piece_volume = 0.000001;
	int max_pieces = 256;
	int retry_count = 8;
	bool decompose_islands = true;
	PartCenter part_center = PART_CENTER_AABB;
	Output output = OUTPUT_MESHES;
	real_t cut_uv_scale = 1.0;
	TypedArray<CSGModifier> cut_modifiers;

	void _cut_modifier_changed();

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
	void set_min_piece_volume(real_t p_volume);
	real_t get_min_piece_volume() const;
	void set_max_pieces(int p_max_pieces);
	int get_max_pieces() const;
	void set_retry_count(int p_retry_count);
	int get_retry_count() const;
	void set_decompose_islands(bool p_enabled);
	bool is_decomposing_islands() const;
	void set_part_center(PartCenter p_part_center);
	PartCenter get_part_center() const;
	void set_output(Output p_output);
	Output get_output() const;
	void set_cut_uv_scale(real_t p_scale);
	real_t get_cut_uv_scale() const;
	void set_cut_modifiers(const TypedArray<CSGModifier> &p_modifiers);
	TypedArray<CSGModifier> get_cut_modifiers() const;
};

VARIANT_ENUM_CAST(CSGSplitSettings::Mode);
VARIANT_ENUM_CAST(CSGSplitSettings::PartCenter);
VARIANT_ENUM_CAST(CSGSplitSettings::Output);
