/**************************************************************************/
/*  csg_topology_settings.h                                               */
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

#include "core/io/resource.h"
#include "core/math/math_funcs.h"

class CSGTopologySettings : public Resource {
	GDCLASS(CSGTopologySettings, Resource);

public:
	enum TopologyMode {
		TOPOLOGY_NONE,
		TOPOLOGY_EDGE_VOLUME,
	};

private:
	TopologyMode mode = TOPOLOGY_NONE;
	real_t edge_width = 0.05;
	real_t angle_threshold = Math::deg_to_rad(30.0);
	real_t merge_epsilon = 0.00001;

protected:
	static void _bind_methods();

public:
	void set_mode(TopologyMode p_mode);
	TopologyMode get_mode() const;

	void set_edge_width(real_t p_edge_width);
	real_t get_edge_width() const;

	void set_angle_threshold(real_t p_angle_threshold);
	real_t get_angle_threshold() const;

	void set_merge_epsilon(real_t p_merge_epsilon);
	real_t get_merge_epsilon() const;
};

VARIANT_ENUM_CAST(CSGTopologySettings::TopologyMode);
