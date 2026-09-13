/**************************************************************************/
/*  test_csg.h                                                            */
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

#include "../csg_bevel_modifier.h"
#include "../csg_shape.h"

#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "tests/test_macros.h"

namespace TestCSG {

TEST_CASE("[SceneTree][CSG] CSGBevelModifier") {
	CSGBox3D *box = memnew(CSGBox3D);
	SceneTree::get_singleton()->get_root()->add_child(box);

	const Vector<Vector3> original_faces = box->get_brush_faces();
	CHECK(original_faces.size() == 36);

	Ref<CSGBevelModifier> bevel;
	bevel.instantiate();
	bevel->set_width(0.1);
	TypedArray<CSGModifier> modifiers;
	modifiers.push_back(bevel);
	box->set_modifiers(modifiers);

	const Vector<Vector3> beveled_faces = box->get_brush_faces();
	CHECK(beveled_faces.size() > original_faces.size());
	CHECK(box->get_aabb().is_equal_approx(AABB(Vector3(-0.5, -0.5, -0.5), Vector3(1, 1, 1))));

	bevel->set_enabled(false);
	CHECK(box->get_brush_faces().size() == original_faces.size());

	SceneTree::get_singleton()->get_root()->remove_child(box);
	memdelete(box);
}

TEST_CASE("[SceneTree][CSG] CSGBevelModifier on parent") {
	CSGCombiner3D *combiner = memnew(CSGCombiner3D);
	SceneTree::get_singleton()->get_root()->add_child(combiner);

	CSGBox3D *first_box = memnew(CSGBox3D);
	CSGBox3D *second_box = memnew(CSGBox3D);
	second_box->set_position(Vector3(2, 0, 0));
	combiner->add_child(first_box);
	combiner->add_child(second_box);

	const Vector<Vector3> original_faces = combiner->get_brush_faces();
	Ref<CSGBevelModifier> bevel;
	bevel.instantiate();
	TypedArray<CSGModifier> modifiers;
	modifiers.push_back(bevel);
	combiner->set_modifiers(modifiers);

	CHECK(combiner->get_brush_faces().size() > original_faces.size());

	SceneTree::get_singleton()->get_root()->remove_child(combiner);
	memdelete(combiner);
}

TEST_CASE("[SceneTree][CSG] CSGPolygon3D") {
	SUBCASE("[SceneTree][CSG] CSGPolygon3D: using accurate path tangent for polygon rotation") {
		const float polygon_radius = 10.0f;

		const Vector3 expected_min_bounds = Vector3(-polygon_radius, -polygon_radius, 0);
		const Vector3 expected_max_bounds = Vector3(100 + polygon_radius, polygon_radius, 100);
		const AABB expected_aabb = AABB(expected_min_bounds, expected_max_bounds - expected_min_bounds);

		Ref<Curve3D> curve;
		curve.instantiate();
		curve->add_point(
				// p_position
				Vector3(0, 0, 0),
				// p_in
				Vector3(),
				// p_out
				Vector3(0, 0, 60));
		curve->add_point(
				// p_position
				Vector3(100, 0, 100),
				// p_in
				Vector3(0, 0, -60),
				// p_out
				Vector3());

		Path3D *path = memnew(Path3D);
		path->set_curve(curve);

		CSGPolygon3D *csg_polygon_3d = memnew(CSGPolygon3D);
		SceneTree::get_singleton()->get_root()->add_child(csg_polygon_3d);

		csg_polygon_3d->add_child(path);
		csg_polygon_3d->set_path_node(csg_polygon_3d->get_path_to(path));
		csg_polygon_3d->set_mode(CSGPolygon3D::Mode::MODE_PATH);

		PackedVector2Array polygon;
		polygon.append(Vector2(-polygon_radius, 0));
		polygon.append(Vector2(0, polygon_radius));
		polygon.append(Vector2(polygon_radius, 0));
		polygon.append(Vector2(0, -polygon_radius));
		csg_polygon_3d->set_polygon(polygon);

		csg_polygon_3d->set_path_rotation(CSGPolygon3D::PathRotation::PATH_ROTATION_PATH);
		csg_polygon_3d->set_path_rotation_accurate(true);

		// Minimize the number of extrusions.
		// This decreases the number of samples taken from the curve.
		// Having fewer samples increases the inaccuracy of the line between samples as an approximation of the tangent of the curve.
		// With correct polygon orientation, the bounding box for the given curve should be independent of the number of extrusions.
		csg_polygon_3d->set_path_interval_type(CSGPolygon3D::PathIntervalType::PATH_INTERVAL_DISTANCE);
		csg_polygon_3d->set_path_interval(1000.0f);

		// Call get_brush_faces to force the bounding box to update.
		csg_polygon_3d->get_brush_faces();

		CHECK(csg_polygon_3d->get_aabb().is_equal_approx(expected_aabb));

		// Perform the bounding box check again with a greater number of extrusions.
		csg_polygon_3d->set_path_interval(1.0f);
		csg_polygon_3d->get_brush_faces();

		CHECK(csg_polygon_3d->get_aabb().is_equal_approx(expected_aabb));

		csg_polygon_3d->remove_child(path);
		SceneTree::get_singleton()->get_root()->remove_child(csg_polygon_3d);

		memdelete(csg_polygon_3d);
		memdelete(path);
	}
}

} // namespace TestCSG
