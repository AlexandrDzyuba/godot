/**************************************************************************/
/*  csg_split_settings.cpp                                                */
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

#include "csg_split_settings.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"

void CSGSplitSettings::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_mode", "mode"), &CSGSplitSettings::set_mode);
	ClassDB::bind_method(D_METHOD("get_mode"), &CSGSplitSettings::get_mode);
	ClassDB::bind_method(D_METHOD("set_iterations", "iterations"), &CSGSplitSettings::set_iterations);
	ClassDB::bind_method(D_METHOD("get_iterations"), &CSGSplitSettings::get_iterations);
	ClassDB::bind_method(D_METHOD("set_seed", "seed"), &CSGSplitSettings::set_seed);
	ClassDB::bind_method(D_METHOD("get_seed"), &CSGSplitSettings::get_seed);
	ClassDB::bind_method(D_METHOD("set_rotation_jitter", "jitter"), &CSGSplitSettings::set_rotation_jitter);
	ClassDB::bind_method(D_METHOD("get_rotation_jitter"), &CSGSplitSettings::get_rotation_jitter);
	ClassDB::bind_method(D_METHOD("set_offset_jitter", "jitter"), &CSGSplitSettings::set_offset_jitter);
	ClassDB::bind_method(D_METHOD("get_offset_jitter"), &CSGSplitSettings::get_offset_jitter);
	ClassDB::bind_method(D_METHOD("set_min_piece_volume", "volume"), &CSGSplitSettings::set_min_piece_volume);
	ClassDB::bind_method(D_METHOD("get_min_piece_volume"), &CSGSplitSettings::get_min_piece_volume);
	ClassDB::bind_method(D_METHOD("set_max_pieces", "max_pieces"), &CSGSplitSettings::set_max_pieces);
	ClassDB::bind_method(D_METHOD("get_max_pieces"), &CSGSplitSettings::get_max_pieces);
	ClassDB::bind_method(D_METHOD("set_retry_count", "retry_count"), &CSGSplitSettings::set_retry_count);
	ClassDB::bind_method(D_METHOD("get_retry_count"), &CSGSplitSettings::get_retry_count);
	ClassDB::bind_method(D_METHOD("set_decompose_islands", "enabled"), &CSGSplitSettings::set_decompose_islands);
	ClassDB::bind_method(D_METHOD("is_decomposing_islands"), &CSGSplitSettings::is_decomposing_islands);
	ClassDB::bind_method(D_METHOD("set_part_center", "part_center"), &CSGSplitSettings::set_part_center);
	ClassDB::bind_method(D_METHOD("get_part_center"), &CSGSplitSettings::get_part_center);
	ClassDB::bind_method(D_METHOD("set_output", "output"), &CSGSplitSettings::set_output);
	ClassDB::bind_method(D_METHOD("get_output"), &CSGSplitSettings::get_output);
	ClassDB::bind_method(D_METHOD("set_cut_uv_scale", "scale"), &CSGSplitSettings::set_cut_uv_scale);
	ClassDB::bind_method(D_METHOD("get_cut_uv_scale"), &CSGSplitSettings::get_cut_uv_scale);
	ClassDB::bind_method(D_METHOD("set_cut_modifiers", "modifiers"), &CSGSplitSettings::set_cut_modifiers);
	ClassDB::bind_method(D_METHOD("get_cut_modifiers"), &CSGSplitSettings::get_cut_modifiers);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "mode", PROPERTY_HINT_ENUM, "Balanced Grid,Random Planes", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_UPDATE_ALL_IF_MODIFIED), "set_mode", "get_mode");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "iterations", PROPERTY_HINT_RANGE, "0,10,1"), "set_iterations", "get_iterations");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "seed"), "set_seed", "get_seed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rotation_jitter", PROPERTY_HINT_RANGE, "0,180,0.1,radians_as_degrees"), "set_rotation_jitter", "get_rotation_jitter");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "offset_jitter", PROPERTY_HINT_RANGE, "0,0.85,0.01"), "set_offset_jitter", "get_offset_jitter");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "min_piece_volume", PROPERTY_HINT_RANGE, "0,1000000,0.000001,or_greater,suffix:m³"), "set_min_piece_volume", "get_min_piece_volume");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_pieces", PROPERTY_HINT_RANGE, "1,4096,1,or_greater"), "set_max_pieces", "get_max_pieces");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "retry_count", PROPERTY_HINT_RANGE, "1,64,1"), "set_retry_count", "get_retry_count");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "decompose_islands"), "set_decompose_islands", "is_decomposing_islands");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "part_center", PROPERTY_HINT_ENUM, "Original Center,AABB Center,Top,Bottom,X Front,Y Front,Z Front,X Back,Y Back,Z Back"), "set_part_center", "get_part_center");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "output", PROPERTY_HINT_ENUM, "Meshes,Meshes with Rigid Bodies"), "set_output", "get_output");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cut_uv_scale", PROPERTY_HINT_RANGE, "0.0001,10000,0.01,or_greater"), "set_cut_uv_scale", "get_cut_uv_scale");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "cut_modifiers", PROPERTY_HINT_ARRAY_TYPE, MAKE_RESOURCE_TYPE_HINT("CSGModifier")), "set_cut_modifiers", "get_cut_modifiers");

	BIND_ENUM_CONSTANT(MODE_BALANCED_GRID);
	BIND_ENUM_CONSTANT(MODE_RANDOM_PLANES);

	BIND_ENUM_CONSTANT(PART_CENTER_ORIGINAL);
	BIND_ENUM_CONSTANT(PART_CENTER_AABB);
	BIND_ENUM_CONSTANT(PART_CENTER_TOP);
	BIND_ENUM_CONSTANT(PART_CENTER_BOTTOM);
	BIND_ENUM_CONSTANT(PART_CENTER_X_FRONT);
	BIND_ENUM_CONSTANT(PART_CENTER_Y_FRONT);
	BIND_ENUM_CONSTANT(PART_CENTER_Z_FRONT);
	BIND_ENUM_CONSTANT(PART_CENTER_X_BACK);
	BIND_ENUM_CONSTANT(PART_CENTER_Y_BACK);
	BIND_ENUM_CONSTANT(PART_CENTER_Z_BACK);

	BIND_ENUM_CONSTANT(OUTPUT_MESHES);
	BIND_ENUM_CONSTANT(OUTPUT_RIGID_BODIES);
}

void CSGSplitSettings::_validate_property(PropertyInfo &p_property) const {
	if ((p_property.name == "rotation_jitter" || p_property.name == "offset_jitter" || p_property.name == "retry_count") && mode != MODE_RANDOM_PLANES) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
}

#define CSG_SPLIT_SETTER(m_name, m_type, m_field, m_value) \
	void CSGSplitSettings::set_##m_name(m_type p_value) { \
		p_value = m_value; \
		if (m_field == p_value) { \
			return; \
		} \
		m_field = p_value; \
		emit_changed(); \
	}

CSG_SPLIT_SETTER(iterations, int, iterations, CLAMP(p_value, 0, 10))
CSG_SPLIT_SETTER(rotation_jitter, real_t, rotation_jitter, CLAMP(p_value, real_t(0.0), real_t(Math::PI)))
CSG_SPLIT_SETTER(offset_jitter, real_t, offset_jitter, CLAMP(p_value, real_t(0.0), real_t(0.85)))
CSG_SPLIT_SETTER(min_piece_volume, real_t, min_piece_volume, MAX(p_value, real_t(0.0)))
CSG_SPLIT_SETTER(max_pieces, int, max_pieces, MAX(p_value, 1))
CSG_SPLIT_SETTER(retry_count, int, retry_count, CLAMP(p_value, 1, 64))
CSG_SPLIT_SETTER(cut_uv_scale, real_t, cut_uv_scale, MAX(p_value, real_t(0.0001)))

#undef CSG_SPLIT_SETTER

void CSGSplitSettings::set_mode(Mode p_mode) {
	ERR_FAIL_INDEX(int(p_mode), int(MODE_RANDOM_PLANES) + 1);
	if (mode == p_mode) {
		return;
	}
	mode = p_mode;
	notify_property_list_changed();
	emit_changed();
}

CSGSplitSettings::Mode CSGSplitSettings::get_mode() const {
	return mode;
}
void CSGSplitSettings::set_seed(uint64_t p_seed) {
	if (seed == p_seed) {
		return;
	}
	seed = p_seed;
	emit_changed();
}
void CSGSplitSettings::set_decompose_islands(bool p_enabled) {
	if (decompose_islands == p_enabled) {
		return;
	}
	decompose_islands = p_enabled;
	emit_changed();
}
int CSGSplitSettings::get_iterations() const {
	return iterations;
}
uint64_t CSGSplitSettings::get_seed() const {
	return seed;
}
real_t CSGSplitSettings::get_rotation_jitter() const {
	return rotation_jitter;
}
real_t CSGSplitSettings::get_offset_jitter() const {
	return offset_jitter;
}
real_t CSGSplitSettings::get_min_piece_volume() const {
	return min_piece_volume;
}
int CSGSplitSettings::get_max_pieces() const {
	return max_pieces;
}
int CSGSplitSettings::get_retry_count() const {
	return retry_count;
}
bool CSGSplitSettings::is_decomposing_islands() const {
	return decompose_islands;
}
void CSGSplitSettings::set_part_center(PartCenter p_part_center) {
	ERR_FAIL_INDEX(int(p_part_center), int(PART_CENTER_Z_BACK) + 1);
	if (part_center == p_part_center) {
		return;
	}
	part_center = p_part_center;
	emit_changed();
}
CSGSplitSettings::PartCenter CSGSplitSettings::get_part_center() const {
	return part_center;
}
void CSGSplitSettings::set_output(Output p_output) {
	ERR_FAIL_INDEX(int(p_output), int(OUTPUT_RIGID_BODIES) + 1);
	if (output == p_output) {
		return;
	}
	output = p_output;
	emit_changed();
}
CSGSplitSettings::Output CSGSplitSettings::get_output() const {
	return output;
}
real_t CSGSplitSettings::get_cut_uv_scale() const {
	return cut_uv_scale;
}

void CSGSplitSettings::_cut_modifier_changed() {
	emit_changed();
}

void CSGSplitSettings::set_cut_modifiers(const TypedArray<CSGModifier> &p_modifiers) {
	Callable changed_callable = callable_mp(this, &CSGSplitSettings::_cut_modifier_changed);
	for (int i = 0; i < cut_modifiers.size(); i++) {
		Ref<CSGModifier> modifier = cut_modifiers[i];
		if (modifier.is_valid() && modifier->is_connected(SNAME("changed"), changed_callable)) {
			modifier->disconnect(SNAME("changed"), changed_callable);
		}
	}
	cut_modifiers = p_modifiers;
	for (int i = 0; i < cut_modifiers.size(); i++) {
		Ref<CSGModifier> modifier = cut_modifiers[i];
		if (modifier.is_valid() && !modifier->is_connected(SNAME("changed"), changed_callable)) {
			modifier->connect(SNAME("changed"), changed_callable);
		}
	}
	emit_changed();
}

TypedArray<CSGModifier> CSGSplitSettings::get_cut_modifiers() const {
	return cut_modifiers;
}
