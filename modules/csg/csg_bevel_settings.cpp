/**************************************************************************/
/*  csg_bevel_settings.cpp                                                */
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

#include "csg_bevel_settings.h"

#include "core/object/class_db.h"

void CSGBevelSettings::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &CSGBevelSettings::set_enabled);
	ClassDB::bind_method(D_METHOD("is_enabled"), &CSGBevelSettings::is_enabled);
	ClassDB::bind_method(D_METHOD("set_debug_print", "enabled"), &CSGBevelSettings::set_debug_print);
	ClassDB::bind_method(D_METHOD("is_debug_printing"), &CSGBevelSettings::is_debug_printing);
	ClassDB::bind_method(D_METHOD("set_width", "width"), &CSGBevelSettings::set_width);
	ClassDB::bind_method(D_METHOD("get_width"), &CSGBevelSettings::get_width);
	ClassDB::bind_method(D_METHOD("set_angle_threshold", "angle_threshold"), &CSGBevelSettings::set_angle_threshold);
	ClassDB::bind_method(D_METHOD("get_angle_threshold"), &CSGBevelSettings::get_angle_threshold);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "debug_print"), "set_debug_print", "is_debug_printing");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "width", PROPERTY_HINT_RANGE, "0,10,0.001,or_greater,suffix:m"), "set_width", "get_width");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "angle_threshold", PROPERTY_HINT_RANGE, "0,180,0.1,radians_as_degrees"), "set_angle_threshold", "get_angle_threshold");
}

void CSGBevelSettings::set_enabled(bool p_enabled) {
	if (enabled == p_enabled) {
		return;
	}
	enabled = p_enabled;
	emit_changed();
}

bool CSGBevelSettings::is_enabled() const {
	return enabled;
}

void CSGBevelSettings::set_debug_print(bool p_enabled) {
	if (debug_print == p_enabled) {
		return;
	}
	debug_print = p_enabled;
	emit_changed();
}

bool CSGBevelSettings::is_debug_printing() const {
	return debug_print;
}

void CSGBevelSettings::set_width(real_t p_width) {
	p_width = MAX(p_width, 0.0);
	if (Math::is_equal_approx(width, p_width)) {
		return;
	}
	width = p_width;
	emit_changed();
}

real_t CSGBevelSettings::get_width() const {
	return width;
}

void CSGBevelSettings::set_angle_threshold(real_t p_angle_threshold) {
	p_angle_threshold = CLAMP(p_angle_threshold, 0.0, Math::PI);
	if (Math::is_equal_approx(angle_threshold, p_angle_threshold)) {
		return;
	}
	angle_threshold = p_angle_threshold;
	emit_changed();
}

real_t CSGBevelSettings::get_angle_threshold() const {
	return angle_threshold;
}
