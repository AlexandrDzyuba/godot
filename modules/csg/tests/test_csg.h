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
#include "../csg_bevel_settings.h"
#include "../csg_shape.h"
#include "../csg_topology_settings.h"

#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "tests/test_macros.h"

namespace TestCSG {

TEST_CASE("[SceneTree][CSG] CSG EDGE_VOLUME topology") {
	CSGBox3D *box = memnew(CSGBox3D);
	SceneTree::get_singleton()->get_root()->add_child(box);

	const Vector<Vector3> original_faces = box->get_brush_faces();
	CHECK(original_faces.size() == 36);

	Ref<CSGTopologySettings> topology_settings;
	topology_settings.instantiate();
	box->set_topology_settings(topology_settings);
	CHECK(box->get_brush_faces().size() == original_faces.size());

	topology_settings->set_mode(CSGTopologySettings::TOPOLOGY_EDGE_VOLUME);
	const Vector<Vector3> edge_volume_faces = box->get_brush_faces();
	CHECK_FALSE(edge_volume_faces.is_empty());
	CHECK(box->get_aabb().size.x > 1.0);
	CHECK(box->get_aabb().size.y > 1.0);
	CHECK(box->get_aabb().size.z > 1.0);

	topology_settings->set_mode(CSGTopologySettings::TOPOLOGY_NONE);
	CHECK(box->get_brush_faces().size() == original_faces.size());

	SceneTree::get_singleton()->get_root()->remove_child(box);
	memdelete(box);
}

TEST_CASE("[SceneTree][CSG] CSGBevelModifier") {
	CSGBox3D *box = memnew(CSGBox3D);
	SceneTree::get_singleton()->get_root()->add_child(box);

	const Vector<Vector3> original_faces = box->get_brush_faces();
	CHECK(original_faces.size() == 36);

	Ref<CSGBevelModifier> bevel;
	bevel.instantiate();
	CHECK(bevel->get_application_mode() == CSGBevelModifier::APPLICATION_MODE_OPERANDS);
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

TEST_CASE("[SceneTree][CSG] Parent bevel processes child topology") {
	CSGCombiner3D *combiner = memnew(CSGCombiner3D);
	SceneTree::get_singleton()->get_root()->add_child(combiner);

	CSGBox3D *box = memnew(CSGBox3D);
	Ref<CSGTopologySettings> topology_settings;
	topology_settings.instantiate();
	topology_settings->set_mode(CSGTopologySettings::TOPOLOGY_EDGE_VOLUME);
	box->set_topology_settings(topology_settings);
	combiner->add_child(box);

	const Vector<Vector3> topology_faces = combiner->get_brush_faces();
	CHECK_FALSE(topology_faces.is_empty());

	Ref<CSGBevelModifier> bevel;
	bevel.instantiate();
	bevel->set_width(0.01);
	TypedArray<CSGModifier> modifiers;
	modifiers.push_back(bevel);
	combiner->set_modifiers(modifiers);

	const Vector<Vector3> beveled_faces = combiner->get_brush_faces();
	CHECK_FALSE(beveled_faces.is_empty());
	CHECK(beveled_faces.size() != topology_faces.size());

	SceneTree::get_singleton()->get_root()->remove_child(combiner);
	memdelete(combiner);
}

TEST_CASE("[SceneTree][CSG] Result bevel remains stable when moving a subtraction operand") {
	CSGBox3D *shape = memnew(CSGBox3D);
	shape->set_size(Vector3(4, 2, 4));
	SceneTree::get_singleton()->get_root()->add_child(shape);

	CSGBox3D *cutter = memnew(CSGBox3D);
	cutter->set_size(Vector3(1, 2, 2));
	cutter->set_operation(CSGShape3D::OPERATION_SUBTRACTION);
	cutter->set_position(Vector3(0.0, 1.0, 0.0));
	shape->add_child(cutter);

	Ref<CSGBevelModifier> bevel;
	bevel.instantiate();
	bevel->set_application_mode(CSGBevelModifier::APPLICATION_MODE_RESULT);
	bevel->set_width(0.1);
	TypedArray<CSGModifier> modifiers;
	modifiers.push_back(bevel);

	const int first_unmodified_face_count = shape->get_brush_faces().size();
	shape->set_modifiers(modifiers);
	CHECK(shape->get_brush_faces().size() > first_unmodified_face_count);

	shape->set_modifiers(TypedArray<CSGModifier>());
	cutter->set_position(Vector3(0.137, 1.0, -0.083));
	const int second_unmodified_face_count = shape->get_brush_faces().size();
	shape->set_modifiers(modifiers);
	CHECK(shape->get_brush_faces().size() > second_unmodified_face_count);

	SceneTree::get_singleton()->get_root()->remove_child(shape);
	memdelete(shape);
}

TEST_CASE("[SceneTree][CSG] Zero-angle result bevel ignores coplanar Boolean triangulation") {
	CSGBox3D *shape = memnew(CSGBox3D);
	shape->set_size(Vector3(3.6121826, 1, 1));
	SceneTree::get_singleton()->get_root()->add_child(shape);

	CSGBox3D *cutter = memnew(CSGBox3D);
	cutter->set_size(Vector3(0.4831543, 1, 1.6128159));
	cutter->set_operation(CSGShape3D::OPERATION_SUBTRACTION);
	cutter->set_position(Vector3(0.51507425, 0.43687516, 0.04569912));
	shape->add_child(cutter);

	const int unmodified_face_count = shape->get_brush_faces().size();
	Ref<CSGBevelModifier> bevel;
	bevel.instantiate();
	bevel->set_application_mode(CSGBevelModifier::APPLICATION_MODE_RESULT);
	bevel->set_width(0.02);
	bevel->set_angle(0.0);
	TypedArray<CSGModifier> modifiers;
	modifiers.push_back(bevel);
	shape->set_modifiers(modifiers);

	CHECK(shape->get_brush_faces().size() > unmodified_face_count);

	shape->set_modifiers(TypedArray<CSGModifier>());
	CSGBox3D *additional_union = memnew(CSGBox3D);
	additional_union->set_size(Vector3(0.5, 0.5, 0.5));
	additional_union->set_position(Vector3(-1.75, 0.0, 0.0));
	shape->add_child(additional_union);
	const int unmodified_with_union_face_count = shape->get_brush_faces().size();
	shape->set_modifiers(modifiers);
	CHECK(shape->get_brush_faces().size() > unmodified_with_union_face_count);

	SceneTree::get_singleton()->get_root()->remove_child(shape);
	memdelete(shape);
}

TEST_CASE("[SceneTree][CSG] Native bevel processes the node Boolean result") {
	CSGBox3D *shape = memnew(CSGBox3D);
	shape->set_size(Vector3(3.6121826, 1, 1));
	SceneTree::get_singleton()->get_root()->add_child(shape);

	CSGBox3D *cutter = memnew(CSGBox3D);
	cutter->set_size(Vector3(0.4831543, 1, 1.6128159));
	cutter->set_operation(CSGShape3D::OPERATION_SUBTRACTION);
	cutter->set_position(Vector3(0.51507425, 0.43687516, 0.04569912));
	shape->add_child(cutter);

	const int boolean_face_count = shape->get_brush_faces().size();
	Ref<CSGBevelSettings> bevel_settings;
	bevel_settings.instantiate();
	bevel_settings->set_width(0.02);
	bevel_settings->set_angle_threshold(0.0);
	shape->set_bevel_settings(bevel_settings);
	CHECK(shape->get_brush_faces().size() > boolean_face_count);

	CSGBox3D *additional_union = memnew(CSGBox3D);
	additional_union->set_size(Vector3(0.5, 0.5, 0.5));
	additional_union->set_position(Vector3(-1.75, 0.0, 0.0));
	shape->add_child(additional_union);
	CHECK_FALSE(shape->get_brush_faces().is_empty());

	bevel_settings->set_enabled(false);
	CHECK(shape->get_brush_faces().size() >= boolean_face_count);

	SceneTree::get_singleton()->get_root()->remove_child(shape);
	memdelete(shape);
}

TEST_CASE("[SceneTree][CSG] Native bevel placement follows the CSG tree") {
	CSGCombiner3D *combiner = memnew(CSGCombiner3D);
	SceneTree::get_singleton()->get_root()->add_child(combiner);

	CSGBox3D *box = memnew(CSGBox3D);
	combiner->add_child(box);
	Ref<CSGBevelSettings> child_bevel;
	child_bevel.instantiate();
	child_bevel->set_width(0.05);
	box->set_bevel_settings(child_bevel);
	CHECK(combiner->get_brush_faces().size() > 36);

	box->set_bevel_settings(Ref<CSGBevelSettings>());
	Ref<CSGBevelSettings> parent_bevel;
	parent_bevel.instantiate();
	parent_bevel->set_width(0.05);
	combiner->set_bevel_settings(parent_bevel);
	CHECK(combiner->get_brush_faces().size() > 36);

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
