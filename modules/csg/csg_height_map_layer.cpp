/**************************************************************************/
/*  csg_height_map_layer.cpp                                              */
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

#include "csg_height_map_layer.h"

#include "core/math/math_funcs.h"
#include "core/object/class_db.h"

void CSGHeightMapLayer::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_height_weight", "weight"), &CSGHeightMapLayer::set_height_weight);
	ClassDB::bind_method(D_METHOD("get_height_weight"), &CSGHeightMapLayer::get_height_weight);
	ClassDB::bind_method(D_METHOD("set_slope_width", "width"), &CSGHeightMapLayer::set_slope_width);
	ClassDB::bind_method(D_METHOD("get_slope_width"), &CSGHeightMapLayer::get_slope_width);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "height_weight", PROPERTY_HINT_RANGE, "0.001,100,0.001,or_greater"), "set_height_weight", "get_height_weight");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "slope_width", PROPERTY_HINT_RANGE, "-1,10,0.001,or_greater,suffix:m"), "set_slope_width", "get_slope_width");
}

void CSGHeightMapLayer::set_height_weight(real_t p_weight) {
	p_weight = MAX(p_weight, real_t(0.001));
	if (Math::is_equal_approx(height_weight, p_weight)) {
		return;
	}
	height_weight = p_weight;
	emit_changed();
}

real_t CSGHeightMapLayer::get_height_weight() const {
	return height_weight;
}

void CSGHeightMapLayer::set_slope_width(real_t p_width) {
	p_width = MAX(p_width, real_t(-1.0));
	if (Math::is_equal_approx(slope_width, p_width)) {
		return;
	}
	slope_width = p_width;
	emit_changed();
}

real_t CSGHeightMapLayer::get_slope_width() const {
	return slope_width;
}
