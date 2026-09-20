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

#include "../csg_attribute_modifier.h"
#include "../csg_bevel_settings.h"
#include "../csg_geometry_data.h"
#include "../csg_shape.h"
#include "../csg_topology_settings.h"

#include "core/io/image.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"
#include "scene/resources/image_texture.h"
#include "tests/test_macros.h"

namespace TestCSG {

TEST_CASE("[SceneTree][CSG] CSGHeightMap3D builds one closed stepped solid") {
	Ref<Image> image = memnew(Image(2, 1, false, Image::FORMAT_RGBA8));
	image->set_pixel(0, 0, Color(0, 0, 0, 1));
	image->set_pixel(1, 0, Color(1, 1, 1, 1));
	Ref<ImageTexture> texture = ImageTexture::create_from_image(image);

	CSGHeightMap3D *height_map = memnew(CSGHeightMap3D);
	height_map->set_height_map(texture);
	height_map->set_size(Vector3(4, 2, 2));
	CHECK(height_map->get_generation_mode() == CSGHeightMap3D::GENERATION_CONTOUR_LAYERS);
	CHECK(height_map->get_height_steps() == 16);
	SceneTree::get_singleton()->get_root()->add_child(height_map);

	const Vector<Vector3> faces = height_map->get_brush_faces();
	CHECK_FALSE(faces.is_empty());
	real_t upward_area = 0.0;
	for (int face = 0; face < faces.size(); face += 3) {
		const Vector3 outward_area_vector = -(faces[face + 1] - faces[face]).cross(faces[face + 2] - faces[face]) * 0.5;
		if (outward_area_vector.y > CMP_EPSILON && Math::is_zero_approx(outward_area_vector.x) && Math::is_zero_approx(outward_area_vector.z)) {
			upward_area += outward_area_vector.y;
		}
	}
	CHECK(upward_area == doctest::Approx(8.0));
	Ref<CSGGeometryData> geometry = height_map->get_geometry_data();
	CHECK(geometry.is_valid());
	CHECK(geometry->get_vertex_count() > 0);
	for (uint8_t boundary : geometry->get_boundary_edges()) {
		CHECK(boundary == 0);
	}

	bool found_top = false;
	bool found_cliff = false;
	bool found_border = false;
	bool found_bottom = false;
	for (const String &semantic : geometry->get_face_semantics()) {
		found_top |= semantic == "HEIGHTMAP_TOP";
		found_cliff |= semantic == "HEIGHTMAP_CLIFF";
		found_border |= semantic == "HEIGHTMAP_BORDER";
		found_bottom |= semantic == "HEIGHTMAP_BOTTOM";
	}
	CHECK(found_top);
	CHECK(found_cliff);
	CHECK(found_border);
	CHECK(found_bottom);
	height_map->set_generation_mode(CSGHeightMap3D::GENERATION_CELL_GRID);
	CHECK_FALSE(height_map->get_brush_faces().is_empty());

	SceneTree::get_singleton()->get_root()->remove_child(height_map);
	memdelete(height_map);
}

TEST_CASE("[SceneTree][CSG] CSGHeightMap3D preserves holes between contour layers") {
	Ref<Image> image = memnew(Image(3, 3, false, Image::FORMAT_RGBA8));
	image->fill(Color(1, 1, 1, 1));
	image->set_pixel(1, 1, Color(0, 0, 0, 1));
	Ref<ImageTexture> texture = ImageTexture::create_from_image(image);

	CSGHeightMap3D *height_map = memnew(CSGHeightMap3D);
	height_map->set_height_map(texture);
	height_map->set_size(Vector3(3, 2, 3));
	SceneTree::get_singleton()->get_root()->add_child(height_map);

	const Vector<Vector3> faces = height_map->get_brush_faces();
	REQUIRE_FALSE(faces.is_empty());
	real_t highest_y = faces[0].y;
	for (const Vector3 &vertex : faces) {
		highest_y = MAX(highest_y, vertex.y);
	}
	real_t highest_upward_area = 0.0;
	for (int face = 0; face < faces.size(); face += 3) {
		const Vector3 outward_area_vector = -(faces[face + 1] - faces[face]).cross(faces[face + 2] - faces[face]) * 0.5;
		if (outward_area_vector.y > CMP_EPSILON &&
				Math::is_equal_approx(faces[face].y, highest_y) &&
				Math::is_equal_approx(faces[face + 1].y, highest_y) &&
				Math::is_equal_approx(faces[face + 2].y, highest_y)) {
			highest_upward_area += outward_area_vector.y;
		}
	}
	// Marching squares chamfers the four corners of the low center pixel, so its
	// contour area is 0.5. If the hole becomes another outer, this becomes 9.
	CHECK(highest_upward_area == doctest::Approx(8.5));

	SceneTree::get_singleton()->get_root()->remove_child(height_map);
	memdelete(height_map);
}

TEST_CASE("[SceneTree][CSG] CSGHeightMap3D slopes upper contour vertices") {
	Ref<Image> image = memnew(Image(3, 3, false, Image::FORMAT_RGBA8));
	image->fill(Color(0, 0, 0, 1));
	image->set_pixel(1, 1, Color(1, 1, 1, 1));
	Ref<ImageTexture> texture = ImageTexture::create_from_image(image);

	CSGHeightMap3D *height_map = memnew(CSGHeightMap3D);
	height_map->set_height_map(texture);
	height_map->set_size(Vector3(3, 2, 3));
	SceneTree::get_singleton()->get_root()->add_child(height_map);

	auto get_highest_x_extent = [](const Vector<Vector3> &p_faces) {
		real_t highest_y = p_faces[0].y;
		for (const Vector3 &vertex : p_faces) {
			highest_y = MAX(highest_y, vertex.y);
		}
		real_t extent = 0.0;
		for (const Vector3 &vertex : p_faces) {
			if (Math::is_equal_approx(vertex.y, highest_y)) {
				extent = MAX(extent, Math::abs(vertex.x));
			}
		}
		return extent;
	};
	const Vector<Vector3> vertical_faces = height_map->get_brush_faces();
	REQUIRE_FALSE(vertical_faces.is_empty());
	const real_t vertical_extent = get_highest_x_extent(vertical_faces);

	height_map->set_slope_width(0.3);
	CHECK(height_map->get_slope_width() == doctest::Approx(0.3));
	const Vector<Vector3> sloped_faces = height_map->get_brush_faces();
	REQUIRE_FALSE(sloped_faces.is_empty());
	CHECK(get_highest_x_extent(sloped_faces) < vertical_extent - 0.25);
	Ref<CSGGeometryData> geometry = height_map->get_geometry_data();
	REQUIRE(geometry.is_valid());
	for (uint8_t boundary : geometry->get_boundary_edges()) {
		CHECK(boundary == 0);
	}

	SceneTree::get_singleton()->get_root()->remove_child(height_map);
	memdelete(height_map);
}

TEST_CASE("[SceneTree][CSG] CSGHeightMap3D samples a texture region") {
	Ref<Image> image = memnew(Image(4, 1, false, Image::FORMAT_RGBA8));
	image->set_pixel(0, 0, Color(0, 0, 0, 1));
	image->set_pixel(1, 0, Color(0, 0, 0, 1));
	image->set_pixel(2, 0, Color(1, 1, 1, 1));
	image->set_pixel(3, 0, Color(1, 1, 1, 1));
	Ref<ImageTexture> texture = ImageTexture::create_from_image(image);

	CSGHeightMap3D *height_map = memnew(CSGHeightMap3D);
	height_map->set_height_map(texture);
	height_map->set_size(Vector3(2, 2, 1));
	height_map->set_region_enabled(true);
	height_map->set_region_rect(Rect2i(2, 0, 96, 96));
	CHECK(height_map->is_region_enabled());
	CHECK(height_map->get_region_rect() == Rect2i(2, 0, 96, 96));
	SceneTree::get_singleton()->get_root()->add_child(height_map);

	auto get_highest_vertex = [](const Vector<Vector3> &p_faces) {
		real_t highest = p_faces[0].y;
		for (const Vector3 &vertex : p_faces) {
			highest = MAX(highest, vertex.y);
		}
		return highest;
	};
	Vector<Vector3> faces = height_map->get_brush_faces();
	REQUIRE_FALSE(faces.is_empty());
	CHECK(get_highest_vertex(faces) == doctest::Approx(1.0));

	height_map->set_region_rect(Rect2i(0, 0, 2, 1));
	faces = height_map->get_brush_faces();
	REQUIRE_FALSE(faces.is_empty());
	CHECK(get_highest_vertex(faces) == doctest::Approx(-0.99));

	height_map->set_region_rect(Rect2i(2, 0, 2, 1));
	height_map->set_generation_mode(CSGHeightMap3D::GENERATION_CELL_GRID);
	faces = height_map->get_brush_faces();
	REQUIRE_FALSE(faces.is_empty());
	CHECK(get_highest_vertex(faces) == doctest::Approx(1.0));

	SceneTree::get_singleton()->get_root()->remove_child(height_map);
	memdelete(height_map);
}

TEST_CASE("[SceneTree][CSG] CSGHeightMap3D supports per-layer settings and IDs") {
	Ref<Image> image = memnew(Image(3, 1, false, Image::FORMAT_RGBA8));
	image->set_pixel(0, 0, Color(0, 0, 0, 1));
	image->set_pixel(1, 0, Color(0.5, 0.5, 0.5, 1));
	image->set_pixel(2, 0, Color(1, 1, 1, 1));
	Ref<ImageTexture> texture = ImageTexture::create_from_image(image);

	Ref<CSGHeightMapLayer> middle_layer;
	middle_layer.instantiate();
	middle_layer->set_height_weight(3.0);
	middle_layer->set_slope_width(0.1);
	Ref<CSGHeightMapLayer> high_layer;
	high_layer.instantiate();
	high_layer->set_height_weight(1.0);
	high_layer->set_slope_width(0.2);
	TypedArray<CSGHeightMapLayer> settings;
	settings.resize(3);
	settings[1] = middle_layer;
	settings[2] = high_layer;

	CSGHeightMap3D *height_map = memnew(CSGHeightMap3D);
	height_map->set_height_map(texture);
	height_map->set_size(Vector3(3, 4, 1));
	height_map->set_base_thickness(0.1);
	height_map->set_height_steps(3);
	height_map->set_layer_settings(settings);
	CHECK(height_map->get_layer_settings().size() == 3);
	SceneTree::get_singleton()->get_root()->add_child(height_map);

	const Vector<Vector3> faces = height_map->get_brush_faces();
	REQUIRE_FALSE(faces.is_empty());
	bool found_weighted_middle_height = false;
	for (const Vector3 &vertex : faces) {
		found_weighted_middle_height |= Math::is_equal_approx(vertex.y, real_t(1.025));
	}
	CHECK(found_weighted_middle_height);

	Ref<CSGGeometryData> geometry = height_map->get_geometry_data();
	REQUIRE(geometry.is_valid());
	bool found_layers[3] = {};
	for (int32_t layer_id : geometry->get_layer_ids()) {
		if (layer_id >= 0 && layer_id < 3) {
			found_layers[layer_id] = true;
		}
	}
	CHECK(found_layers[0]);
	CHECK(found_layers[1]);
	CHECK(found_layers[2]);

	SceneTree::get_singleton()->get_root()->remove_child(height_map);
	memdelete(height_map);
}

TEST_CASE("[SceneTree][CSG] Native bevel processes complex CSGHeightMap3D contours") {
	Ref<Image> image = memnew(Image(8, 8, false, Image::FORMAT_RGBA8));
	for (int z = 0; z < image->get_height(); z++) {
		for (int x = 0; x < image->get_width(); x++) {
			const float height = float((x * 3 + z * 5 + (x ^ z)) % 4) / 3.0f;
			image->set_pixel(x, z, Color(height, height, height, 1));
		}
	}
	Ref<ImageTexture> texture = ImageTexture::create_from_image(image);

	CSGHeightMap3D *height_map = memnew(CSGHeightMap3D);
	height_map->set_height_map(texture);
	height_map->set_size(Vector3(8, 2, 8));
	height_map->set_height_steps(4);
	SceneTree::get_singleton()->get_root()->add_child(height_map);

	const Vector<Vector3> original_faces = height_map->get_brush_faces();
	REQUIRE_FALSE(original_faces.is_empty());
	Ref<CSGBevelSettings> bevel_settings;
	bevel_settings.instantiate();
	bevel_settings->set_width(0.05);
	bevel_settings->set_angle_threshold(0.0);
	height_map->set_bevel_settings(bevel_settings);

	const Vector<Vector3> beveled_faces = height_map->get_brush_faces();
	CHECK(beveled_faces.size() > original_faces.size());
	for (const Vector3 &vertex : beveled_faces) {
		CHECK(vertex.is_finite());
	}

	SceneTree::get_singleton()->get_root()->remove_child(height_map);
	memdelete(height_map);
}

TEST_CASE("[CSG] Semantic geometry data and native attribute modifier") {
	Vector<Vector3> vertices;
	vertices.push_back(Vector3(0, 0, 0));
	vertices.push_back(Vector3(1, 0, 0));
	vertices.push_back(Vector3(1, 1, 0));
	vertices.push_back(Vector3(0, 0, 0));
	vertices.push_back(Vector3(1, 1, 0));
	vertices.push_back(Vector3(0, 1, 0));
	CSGBrush brush;
	brush.build_from_faces(vertices, Vector<Vector2>(), Vector<bool>(), Vector<Ref<Material>>(), Vector<bool>());
	brush.faces.write[0].metadata.brush_id = 7;
	brush.faces.write[0].metadata.surface_id = 3;
	brush.faces.write[0].metadata.layer_id = 5;
	brush.faces.write[1].metadata.brush_id = 7;
	brush.faces.write[1].metadata.surface_id = 3;
	brush.faces.write[1].metadata.layer_id = 5;

	Ref<CSGModifierContext> context;
	context.instantiate();
	context->setup(&brush);
	Ref<CSGGeometryData> geometry = context->get_geometry_data();
	CHECK(geometry->get_vertex_count() == 4);
	CHECK(geometry->get_face_count() == 2);
	CHECK(geometry->get_edge_count() == 5);
	int boundary_count = 0;
	for (uint8_t boundary : geometry->get_boundary_edges()) {
		boundary_count += boundary != 0;
	}
	CHECK(boundary_count == 4);
	int face_zero_neighbors = 0;
	int face_one_neighbors = 0;
	for (int32_t adjacent_face : geometry->get_face_adjacency()) {
		face_zero_neighbors += adjacent_face == 0;
		face_one_neighbors += adjacent_face == 1;
	}
	CHECK(face_zero_neighbors == 1);
	CHECK(face_one_neighbors == 1);

	Ref<CSGAttributeModifier> modifier;
	modifier.instantiate();
	modifier->set_custom_override_enabled(3, true);
	Ref<CSGModifierChannelOverride> custom = modifier->get_custom_override(3);
	custom->get_red()->set_source(CSGModifierValue::SOURCE_BRUSH_ID);
	custom->get_green()->set_source(CSGModifierValue::SOURCE_LAYER_ID);
	custom->get_blue()->set_source(CSGModifierValue::SOURCE_FACE_ID);
	modifier->process(context);
	CHECK(brush.custom_channels & (1u << 3));
	CHECK(brush.faces[0].customs[3][0].x == doctest::Approx(7.0));
	CHECK(brush.faces[0].customs[3][0].y == doctest::Approx(5.0));
	CHECK(brush.faces[0].customs[3][0].z == doctest::Approx(0.0));
	CHECK(brush.faces[0].customs[3][0].w == doctest::Approx(1.0));

	custom->get_red()->set_source(CSGModifierValue::SOURCE_TRIANGLE_EDGE_DISTANCE_0);
	custom->get_green()->set_source(CSGModifierValue::SOURCE_TRIANGLE_EDGE_DISTANCE_1);
	custom->get_blue()->set_source(CSGModifierValue::SOURCE_TRIANGLE_EDGE_DISTANCE_2);
	modifier->process(context);
	// The diagonal shared by the two coplanar triangles is opposite corner 1.
	// Its green component must be neutral instead of drawing the triangulation.
	CHECK(brush.faces[0].customs[3][0].y == doctest::Approx(Math::SQRT2));
	CHECK(brush.faces[0].customs[3][1].y == doctest::Approx(Math::SQRT2));
	CHECK(brush.faces[0].customs[3][2].y == doctest::Approx(Math::SQRT2));
	CHECK(brush.faces[0].customs[3][0].x == doctest::Approx(1.0));
	CHECK(brush.faces[0].customs[3][1].x == doctest::Approx(0.0));
	CHECK(brush.faces[0].customs[3][2].z == doctest::Approx(1.0));

	brush.faces.write[0].metadata.generation = CSGBrush::FACE_BEVEL_GENERATED;
	Ref<CSGFaceSemanticModifier> semantic_modifier;
	semantic_modifier.instantiate();
	semantic_modifier->set_original_semantic("STONE_WALL");
	semantic_modifier->set_bevel_semantic("STONE_EDGE");
	semantic_modifier->process(context);
	CHECK(brush.faces[0].metadata.semantic == StringName("STONE_EDGE"));
	CHECK(brush.faces[1].metadata.semantic == StringName("STONE_WALL"));
}

TEST_CASE("[SceneTree][CSG] CSG EDGE_VOLUME topology") {
	CSGBox3D *box = memnew(CSGBox3D);
	SceneTree::get_singleton()->get_root()->add_child(box);

	const Vector<Vector3> original_faces = box->get_brush_faces();
	CHECK(original_faces.size() == 36);
	Ref<CSGGeometryData> original_geometry = box->get_geometry_data();
	CHECK(original_geometry.is_valid());
	CHECK(original_geometry->get_face_count() == 12);
	CHECK(original_geometry->get_vertex_count() == 8);

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

TEST_CASE("[SceneTree][CSG] Native parent bevel processes child topology") {
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

	Ref<CSGBevelSettings> bevel_settings;
	bevel_settings.instantiate();
	bevel_settings->set_width(0.01);
	combiner->set_bevel_settings(bevel_settings);

	const Vector<Vector3> beveled_faces = combiner->get_brush_faces();
	CHECK_FALSE(beveled_faces.is_empty());
	CHECK(beveled_faces.size() != topology_faces.size());

	SceneTree::get_singleton()->get_root()->remove_child(combiner);
	memdelete(combiner);
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
