/**************************************************************************/
/*  csg_topology_settings.cpp                                             */
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

#include "csg_topology_settings.h"

#include "core/object/class_db.h"

void CSGTopologySettings::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_mode", "mode"), &CSGTopologySettings::set_mode);
	ClassDB::bind_method(D_METHOD("get_mode"), &CSGTopologySettings::get_mode);
	ClassDB::bind_method(D_METHOD("set_edge_width", "edge_width"), &CSGTopologySettings::set_edge_width);
	ClassDB::bind_method(D_METHOD("get_edge_width"), &CSGTopologySettings::get_edge_width);
	ClassDB::bind_method(D_METHOD("set_angle_threshold", "angle_threshold"), &CSGTopologySettings::set_angle_threshold);
	ClassDB::bind_method(D_METHOD("get_angle_threshold"), &CSGTopologySettings::get_angle_threshold);
	ClassDB::bind_method(D_METHOD("set_merge_epsilon", "merge_epsilon"), &CSGTopologySettings::set_merge_epsilon);
	ClassDB::bind_method(D_METHOD("get_merge_epsilon"), &CSGTopologySettings::get_merge_epsilon);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "mode", PROPERTY_HINT_ENUM, "None,Edge Volume"), "set_mode", "get_mode");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "edge_width", PROPERTY_HINT_RANGE, "0.000001,10,0.001,or_greater,suffix:m"), "set_edge_width", "get_edge_width");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "angle_threshold", PROPERTY_HINT_RANGE, "0,180,0.1,radians_as_degrees"), "set_angle_threshold", "get_angle_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "merge_epsilon", PROPERTY_HINT_RANGE, "0.0000001,1,0.000001,or_greater,suffix:m"), "set_merge_epsilon", "get_merge_epsilon");

	BIND_ENUM_CONSTANT(TOPOLOGY_NONE);
	BIND_ENUM_CONSTANT(TOPOLOGY_EDGE_VOLUME);
}

void CSGTopologySettings::set_mode(TopologyMode p_mode) {
	ERR_FAIL_INDEX(int(p_mode), int(TOPOLOGY_EDGE_VOLUME) + 1);
	if (mode == p_mode) {
		return;
	}
	mode = p_mode;
	emit_changed();
}

CSGTopologySettings::TopologyMode CSGTopologySettings::get_mode() const {
	return mode;
}

void CSGTopologySettings::set_edge_width(real_t p_edge_width) {
	p_edge_width = MAX(p_edge_width, CMP_EPSILON);
	if (Math::is_equal_approx(edge_width, p_edge_width)) {
		return;
	}
	edge_width = p_edge_width;
	emit_changed();
}

real_t CSGTopologySettings::get_edge_width() const {
	return edge_width;
}

void CSGTopologySettings::set_angle_threshold(real_t p_angle_threshold) {
	p_angle_threshold = CLAMP(p_angle_threshold, 0.0, Math::PI);
	if (Math::is_equal_approx(angle_threshold, p_angle_threshold)) {
		return;
	}
	angle_threshold = p_angle_threshold;
	emit_changed();
}

real_t CSGTopologySettings::get_angle_threshold() const {
	return angle_threshold;
}

void CSGTopologySettings::set_merge_epsilon(real_t p_merge_epsilon) {
	p_merge_epsilon = MAX(p_merge_epsilon, CMP_EPSILON);
	if (Math::is_equal_approx(merge_epsilon, p_merge_epsilon)) {
		return;
	}
	merge_epsilon = p_merge_epsilon;
	emit_changed();
}

real_t CSGTopologySettings::get_merge_epsilon() const {
	return merge_epsilon;
}
