/**************************************************************************/
/*  mesh_split_settings.cpp                                               */
/**************************************************************************/

#include "mesh_split_settings.h"

#include "core/object/class_db.h"

void MeshSplitSettings::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_mode", "mode"), &MeshSplitSettings::set_mode);
	ClassDB::bind_method(D_METHOD("get_mode"), &MeshSplitSettings::get_mode);
	ClassDB::bind_method(D_METHOD("set_iterations", "iterations"), &MeshSplitSettings::set_iterations);
	ClassDB::bind_method(D_METHOD("get_iterations"), &MeshSplitSettings::get_iterations);
	ClassDB::bind_method(D_METHOD("set_seed", "seed"), &MeshSplitSettings::set_seed);
	ClassDB::bind_method(D_METHOD("get_seed"), &MeshSplitSettings::get_seed);
	ClassDB::bind_method(D_METHOD("set_rotation_jitter", "jitter"), &MeshSplitSettings::set_rotation_jitter);
	ClassDB::bind_method(D_METHOD("get_rotation_jitter"), &MeshSplitSettings::get_rotation_jitter);
	ClassDB::bind_method(D_METHOD("set_offset_jitter", "jitter"), &MeshSplitSettings::set_offset_jitter);
	ClassDB::bind_method(D_METHOD("get_offset_jitter"), &MeshSplitSettings::get_offset_jitter);
	ClassDB::bind_method(D_METHOD("set_max_pieces", "max_pieces"), &MeshSplitSettings::set_max_pieces);
	ClassDB::bind_method(D_METHOD("get_max_pieces"), &MeshSplitSettings::get_max_pieces);
	ClassDB::bind_method(D_METHOD("set_retry_count", "retry_count"), &MeshSplitSettings::set_retry_count);
	ClassDB::bind_method(D_METHOD("get_retry_count"), &MeshSplitSettings::get_retry_count);
	ClassDB::bind_method(D_METHOD("set_min_triangles", "min_triangles"), &MeshSplitSettings::set_min_triangles);
	ClassDB::bind_method(D_METHOD("get_min_triangles"), &MeshSplitSettings::get_min_triangles);
	ClassDB::bind_method(D_METHOD("set_decompose_islands", "enabled"), &MeshSplitSettings::set_decompose_islands);
	ClassDB::bind_method(D_METHOD("is_decomposing_islands"), &MeshSplitSettings::is_decomposing_islands);
	ClassDB::bind_method(D_METHOD("set_weld_tolerance", "tolerance"), &MeshSplitSettings::set_weld_tolerance);
	ClassDB::bind_method(D_METHOD("get_weld_tolerance"), &MeshSplitSettings::get_weld_tolerance);
	ClassDB::bind_method(D_METHOD("set_cap_mode", "mode"), &MeshSplitSettings::set_cap_mode);
	ClassDB::bind_method(D_METHOD("get_cap_mode"), &MeshSplitSettings::get_cap_mode);
	ClassDB::bind_method(D_METHOD("set_cap_material", "material"), &MeshSplitSettings::set_cap_material);
	ClassDB::bind_method(D_METHOD("get_cap_material"), &MeshSplitSettings::get_cap_material);
	ClassDB::bind_method(D_METHOD("set_cap_uv_scale", "scale"), &MeshSplitSettings::set_cap_uv_scale);
	ClassDB::bind_method(D_METHOD("get_cap_uv_scale"), &MeshSplitSettings::get_cap_uv_scale);
	ClassDB::bind_method(D_METHOD("set_cap_color", "color"), &MeshSplitSettings::set_cap_color);
	ClassDB::bind_method(D_METHOD("get_cap_color"), &MeshSplitSettings::get_cap_color);
	ClassDB::bind_method(D_METHOD("set_cap_custom_enabled", "channel", "enabled"), &MeshSplitSettings::set_cap_custom_enabled);
	ClassDB::bind_method(D_METHOD("is_cap_custom_enabled", "channel"), &MeshSplitSettings::is_cap_custom_enabled);
	ClassDB::bind_method(D_METHOD("set_cap_custom", "channel", "value"), &MeshSplitSettings::set_cap_custom);
	ClassDB::bind_method(D_METHOD("get_cap_custom", "channel"), &MeshSplitSettings::get_cap_custom);
	ClassDB::bind_method(D_METHOD("set_part_center", "part_center"), &MeshSplitSettings::set_part_center);
	ClassDB::bind_method(D_METHOD("get_part_center"), &MeshSplitSettings::get_part_center);
	ClassDB::bind_method(D_METHOD("set_output", "output"), &MeshSplitSettings::set_output);
	ClassDB::bind_method(D_METHOD("get_output"), &MeshSplitSettings::get_output);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "mode", PROPERTY_HINT_ENUM, "Balanced Grid,Random Planes", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_UPDATE_ALL_IF_MODIFIED), "set_mode", "get_mode");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "iterations", PROPERTY_HINT_RANGE, "0,10,1"), "set_iterations", "get_iterations");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "seed"), "set_seed", "get_seed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rotation_jitter", PROPERTY_HINT_RANGE, "0,180,0.1,radians_as_degrees"), "set_rotation_jitter", "get_rotation_jitter");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "offset_jitter", PROPERTY_HINT_RANGE, "0,0.85,0.01"), "set_offset_jitter", "get_offset_jitter");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_pieces", PROPERTY_HINT_RANGE, "1,4096,1,or_greater"), "set_max_pieces", "get_max_pieces");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "retry_count", PROPERTY_HINT_RANGE, "1,64,1"), "set_retry_count", "get_retry_count");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "min_triangles", PROPERTY_HINT_RANGE, "1,1000000,1,or_greater"), "set_min_triangles", "get_min_triangles");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "decompose_islands"), "set_decompose_islands", "is_decomposing_islands");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "weld_tolerance", PROPERTY_HINT_RANGE, "0.000001,1,0.000001,or_greater"), "set_weld_tolerance", "get_weld_tolerance");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "cap_mode", PROPERTY_HINT_ENUM, "None,Closed Loops Only,Repair Open Loops"), "set_cap_mode", "get_cap_mode");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "cap_material", PROPERTY_HINT_RESOURCE_TYPE, "Material"), "set_cap_material", "get_cap_material");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cap_uv_scale", PROPERTY_HINT_RANGE, "0.0001,10000,0.01,or_greater"), "set_cap_uv_scale", "get_cap_uv_scale");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "cap_color"), "set_cap_color", "get_cap_color");
	for (int channel = 0; channel < 4; channel++) {
		ADD_PROPERTYI(PropertyInfo(Variant::BOOL, vformat("cap_custom%d_enabled", channel), PROPERTY_HINT_NONE, "", PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_UPDATE_ALL_IF_MODIFIED), "set_cap_custom_enabled", "is_cap_custom_enabled", channel);
		ADD_PROPERTYI(PropertyInfo(Variant::VECTOR4, vformat("cap_custom%d", channel)), "set_cap_custom", "get_cap_custom", channel);
	}
	ADD_PROPERTY(PropertyInfo(Variant::INT, "part_center", PROPERTY_HINT_ENUM, "Original Center,AABB Center,Top,Bottom,X Front,Y Front,Z Front,X Back,Y Back,Z Back"), "set_part_center", "get_part_center");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "output", PROPERTY_HINT_ENUM, "Meshes,Meshes with Rigid Bodies"), "set_output", "get_output");

	BIND_ENUM_CONSTANT(MODE_BALANCED_GRID);
	BIND_ENUM_CONSTANT(MODE_RANDOM_PLANES);
	BIND_ENUM_CONSTANT(CAP_NONE);
	BIND_ENUM_CONSTANT(CAP_CLOSED_LOOPS);
	BIND_ENUM_CONSTANT(CAP_REPAIR_OPEN_LOOPS);
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

void MeshSplitSettings::_validate_property(PropertyInfo &p_property) const {
	if ((p_property.name == "rotation_jitter" || p_property.name == "offset_jitter" || p_property.name == "retry_count") && mode != MODE_RANDOM_PLANES) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
	for (int channel = 0; channel < 4; channel++) {
		if (p_property.name == vformat("cap_custom%d", channel) && !cap_custom_enabled[channel]) {
			p_property.usage = PROPERTY_USAGE_NO_EDITOR;
		}
	}
}

#define MESH_SPLIT_SETTER(m_name, m_type, m_field, m_value) \
	void MeshSplitSettings::set_##m_name(m_type p_value) { \
		p_value = m_value; \
		if (m_field == p_value) { \
			return; \
		} \
		m_field = p_value; \
		emit_changed(); \
	}

MESH_SPLIT_SETTER(iterations, int, iterations, CLAMP(p_value, 0, 10))
MESH_SPLIT_SETTER(rotation_jitter, real_t, rotation_jitter, CLAMP(p_value, real_t(0.0), real_t(Math::PI)))
MESH_SPLIT_SETTER(offset_jitter, real_t, offset_jitter, CLAMP(p_value, real_t(0.0), real_t(0.85)))
MESH_SPLIT_SETTER(max_pieces, int, max_pieces, MAX(p_value, 1))
MESH_SPLIT_SETTER(retry_count, int, retry_count, CLAMP(p_value, 1, 64))
MESH_SPLIT_SETTER(min_triangles, int, min_triangles, MAX(p_value, 1))
MESH_SPLIT_SETTER(weld_tolerance, real_t, weld_tolerance, MAX(p_value, real_t(0.000001)))
MESH_SPLIT_SETTER(cap_uv_scale, real_t, cap_uv_scale, MAX(p_value, real_t(0.0001)))

#undef MESH_SPLIT_SETTER

void MeshSplitSettings::set_mode(Mode p_mode) {
	ERR_FAIL_INDEX(int(p_mode), int(MODE_RANDOM_PLANES) + 1);
	if (mode != p_mode) {
		mode = p_mode;
		notify_property_list_changed();
		emit_changed();
	}
}
MeshSplitSettings::Mode MeshSplitSettings::get_mode() const {
	return mode;
}
int MeshSplitSettings::get_iterations() const {
	return iterations;
}
void MeshSplitSettings::set_seed(uint64_t p_seed) {
	seed = p_seed;
	emit_changed();
}
uint64_t MeshSplitSettings::get_seed() const {
	return seed;
}
real_t MeshSplitSettings::get_rotation_jitter() const {
	return rotation_jitter;
}
real_t MeshSplitSettings::get_offset_jitter() const {
	return offset_jitter;
}
int MeshSplitSettings::get_max_pieces() const {
	return max_pieces;
}
int MeshSplitSettings::get_retry_count() const {
	return retry_count;
}
int MeshSplitSettings::get_min_triangles() const {
	return min_triangles;
}
void MeshSplitSettings::set_decompose_islands(bool p_enabled) {
	if (decompose_islands != p_enabled) {
		decompose_islands = p_enabled;
		emit_changed();
	}
}
bool MeshSplitSettings::is_decomposing_islands() const {
	return decompose_islands;
}
real_t MeshSplitSettings::get_weld_tolerance() const {
	return weld_tolerance;
}
void MeshSplitSettings::set_cap_mode(CapMode p_mode) {
	ERR_FAIL_INDEX(int(p_mode), int(CAP_REPAIR_OPEN_LOOPS) + 1);
	cap_mode = p_mode;
	emit_changed();
}
MeshSplitSettings::CapMode MeshSplitSettings::get_cap_mode() const {
	return cap_mode;
}
void MeshSplitSettings::set_cap_material(const Ref<Material> &p_material) {
	cap_material = p_material;
	emit_changed();
}
Ref<Material> MeshSplitSettings::get_cap_material() const {
	return cap_material;
}
real_t MeshSplitSettings::get_cap_uv_scale() const {
	return cap_uv_scale;
}
void MeshSplitSettings::set_cap_color(const Color &p_color) {
	cap_color = p_color;
	emit_changed();
}
Color MeshSplitSettings::get_cap_color() const {
	return cap_color;
}
void MeshSplitSettings::set_cap_custom_enabled(int p_channel, bool p_enabled) {
	ERR_FAIL_INDEX(p_channel, 4);
	cap_custom_enabled[p_channel] = p_enabled;
	notify_property_list_changed();
	emit_changed();
}
bool MeshSplitSettings::is_cap_custom_enabled(int p_channel) const {
	ERR_FAIL_INDEX_V(p_channel, 4, false);
	return cap_custom_enabled[p_channel];
}
void MeshSplitSettings::set_cap_custom(int p_channel, const Vector4 &p_value) {
	ERR_FAIL_INDEX(p_channel, 4);
	cap_custom[p_channel] = p_value;
	emit_changed();
}
Vector4 MeshSplitSettings::get_cap_custom(int p_channel) const {
	ERR_FAIL_INDEX_V(p_channel, 4, Vector4());
	return cap_custom[p_channel];
}
void MeshSplitSettings::set_part_center(PartCenter p_center) {
	ERR_FAIL_INDEX(int(p_center), int(PART_CENTER_Z_BACK) + 1);
	part_center = p_center;
	emit_changed();
}
MeshSplitSettings::PartCenter MeshSplitSettings::get_part_center() const {
	return part_center;
}
void MeshSplitSettings::set_output(Output p_output) {
	ERR_FAIL_INDEX(int(p_output), int(OUTPUT_RIGID_BODIES) + 1);
	output = p_output;
	emit_changed();
}
MeshSplitSettings::Output MeshSplitSettings::get_output() const {
	return output;
}
