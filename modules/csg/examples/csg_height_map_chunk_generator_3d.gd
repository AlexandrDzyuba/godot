class_name CSGHeightMapChunkGenerator3D
extends Node3D

## Runtime chunk orchestrator for CSGHeightMap3D.
##
## Heavy terrain geometry remains in the native CSGHeightMap3D implementation.
## This node only maps one shared texture into pixel regions and incrementally
## creates or removes chunk scene instances.
##
## An optional chunk_scene may contain water, fog, navigation helpers, props, or
## a CSGHeightMap3D placeholder. If terrain_node_path is empty (or does not point
## to a CSGHeightMap3D), a terrain node is created under terrain_parent_path.
##
## A chunk scene root may implement:
##
##     func setup_csg_height_map_chunk(
##             generator: CSGHeightMapChunkGenerator3D,
##             chunk_coordinate: Vector2i,
##             source_region: Rect2i,
##             terrain: CSGHeightMap3D) -> void:
##         pass
##
## Use rebuild_chunks() after changing generation settings at runtime.

signal chunk_created(chunk_coordinate: Vector2i, chunk_root: Node3D, terrain: CSGHeightMap3D)
signal chunk_removed(chunk_coordinate: Vector2i, chunk_root: Node3D)
signal generation_progress(loaded_chunks: int, requested_chunks: int)
signal generation_finished

enum WorldMode {
	FULL_TEXTURE,
	AROUND_TARGET,
}

@export_category("World Generation")
@export var generation_enabled := true
@export var world_mode: WorldMode = WorldMode.AROUND_TARGET
@export var height_map: Texture2D
@export var chunk_size_pixels := Vector2i(96, 96)
@export var world_units_per_pixel := Vector2(1.0, 1.0)
@export var world_height := 32.0

@export_category("Streaming")
@export var stream_target: Node3D
@export_range(0.0, 1000000.0, 0.1, "or_greater", "suffix:m") var load_distance := 256.0
@export_range(0.0, 1000000.0, 0.1, "or_greater", "suffix:m") var unload_distance := 320.0
@export_range(0.01, 60.0, 0.01, "or_greater", "suffix:s") var stream_update_interval := 0.25
@export_range(1, 64, 1, "or_greater") var max_chunk_creations_per_frame := 1

@export_category("Chunk Scene")
@export var chunk_scene: PackedScene
@export var terrain_node_path: NodePath
@export var terrain_parent_path: NodePath = NodePath(".")
@export var terrain_node_name := "Terrain"

@export_category("Height Map")
@export_enum("Luminance", "Red", "Green", "Blue", "Alpha") var height_channel: int = CSGHeightMap3D.HEIGHT_CHANNEL_LUMINANCE
@export_enum("None", "Luminance", "Red", "Green", "Blue", "Alpha") var height_multiplier_channel: int = CSGHeightMap3D.HEIGHT_MULTIPLIER_CHANNEL_NONE
@export_range(0.0, 8.0, 0.01, "or_greater") var height_multiplier := 1.0
@export_enum("Cell Grid", "Contour Layers", "Direct Contour Mesh") var terrain_generation_mode: int = CSGHeightMap3D.GENERATION_DIRECT_CONTOUR_MESH
@export_range(0, 256, 1, "or_greater") var height_steps := 16
@export_range(1, 64, 1, "or_greater") var sampling_step := 1
@export_range(0.0, 1000.0, 0.001, "or_greater", "suffix:m") var slope_width := 0.0
@export var layer_settings: Array[CSGHeightMapLayer] = []
@export var invert_height := false
@export_range(0.00001, 100.0, 0.001, "or_greater", "suffix:m") var base_thickness := 0.01

@export_category("Materials")
@export var material: Material
@export var side_material: Material
@export var bottom_material: Material

@export_category("CSG Processing")
@export var modifiers: Array[CSGModifier] = []
@export var bevel_settings: CSGBevelSettings
@export var topology_settings: CSGTopologySettings
@export var split_settings: CSGSplitSettings

@export_category("Render and Collision")
@export var calculate_tangents := true
@export var autosmooth := false
@export_range(0.0, 180.0, 0.1, "or_greater", "suffix:°") var smoothing_angle := 50.0
@export var generate_collision := false
@export_flags_3d_physics var collision_layer := 1
@export_flags_3d_physics var collision_mask := 1
@export_range(0.0, 1000.0, 0.01, "or_greater") var collision_priority := 1.0

var _chunks: Dictionary = {}
var _queued: Dictionary = {}
var _pending_chunks: Array[Vector2i] = []
var _wanted_chunks: Dictionary = {}
var _stream_timer := 0.0
var _initialized := false
var _generation_active := false
var _sort_position := Vector2.ZERO


func _ready() -> void:
	_initialize_generation()


func _process(delta: float) -> void:
	if not generation_enabled:
		return
	if not _initialized:
		_initialize_generation()
		if not _initialized:
			return

	if world_mode == WorldMode.AROUND_TARGET:
		_stream_timer -= delta
		if _stream_timer <= 0.0:
			_stream_timer = maxf(stream_update_interval, 0.01)
			_refresh_requested_chunks()

	_create_pending_chunks()


## Re-evaluates which chunks should exist without replacing already loaded ones.
func refresh_chunks() -> void:
	if not _initialized:
		_initialize_generation()
	elif generation_enabled:
		_refresh_requested_chunks()


## Removes all generated chunks and regenerates them from current properties.
func rebuild_chunks() -> void:
	clear_chunks()
	_initialized = false
	_initialize_generation()


## Removes all generated chunks and clears pending generation work.
func clear_chunks() -> void:
	_pending_chunks.clear()
	_queued.clear()
	_wanted_chunks.clear()
	_generation_active = false
	for coordinate: Vector2i in _chunks.keys():
		_remove_chunk(coordinate)
	_chunks.clear()


func get_loaded_chunk(chunk_coordinate: Vector2i) -> Node3D:
	return _chunks.get(chunk_coordinate) as Node3D


func get_loaded_chunk_coordinates() -> Array[Vector2i]:
	var result: Array[Vector2i] = []
	for coordinate: Vector2i in _chunks.keys():
		result.push_back(coordinate)
	return result


func get_chunk_source_region(chunk_coordinate: Vector2i) -> Rect2i:
	var texture_size := _get_texture_size()
	var safe_chunk_size := _get_safe_chunk_size()
	var position := chunk_coordinate * safe_chunk_size
	if position.x < 0 or position.y < 0 or position.x >= texture_size.x or position.y >= texture_size.y:
		return Rect2i()
	return Rect2i(
			position,
			Vector2i(
					mini(safe_chunk_size.x, texture_size.x - position.x),
					mini(safe_chunk_size.y, texture_size.y - position.y)))


func get_chunk_local_center(chunk_coordinate: Vector2i) -> Vector3:
	var region := get_chunk_source_region(chunk_coordinate)
	if not region.has_area():
		return Vector3.ZERO
	var texture_world_size := Vector2(_get_texture_size()) * _get_safe_pixel_size()
	var region_center_pixels := Vector2(region.position) + Vector2(region.size) * 0.5
	var center_xz := region_center_pixels * _get_safe_pixel_size() - texture_world_size * 0.5
	return Vector3(center_xz.x, 0.0, center_xz.y)


func _initialize_generation() -> void:
	if not generation_enabled or height_map == null:
		return
	if _get_texture_size().x <= 0 or _get_texture_size().y <= 0:
		push_warning("CSGHeightMapChunkGenerator3D requires a non-empty height_map texture.")
		return
	_initialized = true
	_stream_timer = maxf(stream_update_interval, 0.01)
	_refresh_requested_chunks()


func _refresh_requested_chunks() -> void:
	if height_map == null:
		return
	for coordinate: Vector2i in _chunks.keys():
		if not is_instance_valid(_chunks[coordinate]):
			_chunks.erase(coordinate)

	var requested: Dictionary = {}
	var chunk_count := _get_chunk_count()
	var target_xz := _get_stream_position()
	_sort_position = target_xz

	for z in range(chunk_count.y):
		for x in range(chunk_count.x):
			var coordinate := Vector2i(x, z)
			if world_mode == WorldMode.FULL_TEXTURE or _distance_to_chunk(coordinate, target_xz) <= maxf(load_distance, 0.0):
				requested[coordinate] = true

	if world_mode == WorldMode.AROUND_TARGET:
		var safe_unload_distance := maxf(unload_distance, load_distance)
		for coordinate: Vector2i in _chunks.keys():
			if _distance_to_chunk(coordinate, target_xz) > safe_unload_distance:
				_remove_chunk(coordinate)

	_wanted_chunks = requested
	_rebuild_pending_queue()


func _rebuild_pending_queue() -> void:
	var next_pending: Array[Vector2i] = []
	_queued.clear()
	for coordinate: Vector2i in _pending_chunks:
		if _wanted_chunks.has(coordinate) and not _chunks.has(coordinate):
			next_pending.push_back(coordinate)
			_queued[coordinate] = true
	_pending_chunks = next_pending

	for coordinate: Vector2i in _wanted_chunks.keys():
		if not _chunks.has(coordinate) and not _queued.has(coordinate):
			_pending_chunks.push_back(coordinate)
			_queued[coordinate] = true

	_pending_chunks.sort_custom(_sort_chunks_nearest_first)
	if not _pending_chunks.is_empty():
		_generation_active = true


func _sort_chunks_nearest_first(a: Vector2i, b: Vector2i) -> bool:
	return _distance_to_chunk(a, _sort_position) < _distance_to_chunk(b, _sort_position)


func _create_pending_chunks() -> void:
	var creation_budget := maxi(max_chunk_creations_per_frame, 1)
	while creation_budget > 0 and not _pending_chunks.is_empty():
		var coordinate: Vector2i = _pending_chunks.pop_front()
		_queued.erase(coordinate)
		if _wanted_chunks.has(coordinate) and not _chunks.has(coordinate):
			_create_chunk(coordinate)
		creation_budget -= 1

	if _generation_active and _pending_chunks.is_empty():
		_generation_active = false
		generation_finished.emit()


func _create_chunk(coordinate: Vector2i) -> void:
	var source_region := get_chunk_source_region(coordinate)
	if not source_region.has_area():
		return

	var chunk_root: Node3D
	if chunk_scene != null:
		var instance := chunk_scene.instantiate()
		if not instance is Node3D:
			push_error("CSGHeightMapChunkGenerator3D chunk_scene root must inherit Node3D.")
			instance.free()
			return
		chunk_root = instance as Node3D
	else:
		chunk_root = Node3D.new()

	chunk_root.name = "Chunk_%d_%d" % [coordinate.x, coordinate.y]
	chunk_root.position = get_chunk_local_center(coordinate)
	chunk_root.set_meta(&"csg_height_map_chunk_coordinate", coordinate)
	chunk_root.set_meta(&"csg_height_map_source_region", source_region)

	var terrain := _find_or_create_terrain(chunk_root)
	if terrain == null:
		chunk_root.free()
		return
	_configure_terrain(terrain, source_region)

	add_child(chunk_root)
	_chunks[coordinate] = chunk_root

	if chunk_root.has_method(&"setup_csg_height_map_chunk"):
		chunk_root.call(&"setup_csg_height_map_chunk", self, coordinate, source_region, terrain)

	chunk_created.emit(coordinate, chunk_root, terrain)
	generation_progress.emit(_chunks.size(), _wanted_chunks.size())


func _find_or_create_terrain(chunk_root: Node3D) -> CSGHeightMap3D:
	if not terrain_node_path.is_empty():
		var existing := chunk_root.get_node_or_null(terrain_node_path)
		if existing is CSGHeightMap3D:
			return existing as CSGHeightMap3D
		if existing != null:
			push_warning("terrain_node_path does not point to CSGHeightMap3D; creating a terrain node instead.")

	var terrain_parent: Node = chunk_root.get_node_or_null(terrain_parent_path)
	if terrain_parent == null:
		push_error("terrain_parent_path does not exist in chunk_scene: %s" % terrain_parent_path)
		return null

	var terrain := CSGHeightMap3D.new()
	terrain.name = terrain_node_name if not terrain_node_name.is_empty() else "Terrain"
	terrain_parent.add_child(terrain)
	return terrain


func _configure_terrain(terrain: CSGHeightMap3D, source_region: Rect2i) -> void:
	var pixel_size := _get_safe_pixel_size()
	terrain.height_map = height_map
	terrain.region_enabled = true
	terrain.region_rect = source_region
	terrain.size = Vector3(
			source_region.size.x * pixel_size.x,
			maxf(world_height, 0.001),
			source_region.size.y * pixel_size.y)
	terrain.height_channel = height_channel
	terrain.height_multiplier_channel = height_multiplier_channel
	terrain.height_multiplier = maxf(height_multiplier, 0.0)
	terrain.generation_mode = terrain_generation_mode
	terrain.height_steps = height_steps
	terrain.sampling_step = maxi(sampling_step, 1)
	terrain.slope_width = maxf(slope_width, 0.0)
	terrain.layer_settings = layer_settings
	terrain.invert_height = invert_height
	terrain.base_thickness = maxf(base_thickness, 0.00001)
	terrain.material = material
	terrain.side_material = side_material
	terrain.bottom_material = bottom_material
	terrain.modifiers = modifiers
	terrain.bevel_settings = bevel_settings
	terrain.topology_settings = topology_settings
	terrain.split_settings = split_settings
	terrain.calculate_tangents = calculate_tangents
	terrain.autosmooth = autosmooth
	terrain.smoothing_angle = smoothing_angle
	terrain.use_collision = generate_collision
	terrain.collision_layer = collision_layer
	terrain.collision_mask = collision_mask
	terrain.collision_priority = collision_priority


func _remove_chunk(coordinate: Vector2i) -> void:
	var chunk_root := _chunks.get(coordinate) as Node3D
	_chunks.erase(coordinate)
	if chunk_root == null:
		return
	chunk_removed.emit(coordinate, chunk_root)
	chunk_root.queue_free()


func _get_texture_size() -> Vector2i:
	if height_map == null:
		return Vector2i.ZERO
	return Vector2i(height_map.get_width(), height_map.get_height())


func _get_safe_chunk_size() -> Vector2i:
	return Vector2i(maxi(chunk_size_pixels.x, 1), maxi(chunk_size_pixels.y, 1))


func _get_safe_pixel_size() -> Vector2:
	return Vector2(maxf(world_units_per_pixel.x, 0.00001), maxf(world_units_per_pixel.y, 0.00001))


func _get_chunk_count() -> Vector2i:
	var texture_size := _get_texture_size()
	var safe_chunk_size := _get_safe_chunk_size()
	return Vector2i(
			ceili(float(texture_size.x) / safe_chunk_size.x),
			ceili(float(texture_size.y) / safe_chunk_size.y))


func _get_stream_position() -> Vector2:
	if is_instance_valid(stream_target):
		var local_position := to_local(stream_target.global_position)
		return Vector2(local_position.x, local_position.z)
	return Vector2.ZERO


func _distance_to_chunk(coordinate: Vector2i, point: Vector2) -> float:
	var region := get_chunk_source_region(coordinate)
	if not region.has_area():
		return INF
	var center_3d := get_chunk_local_center(coordinate)
	var center := Vector2(center_3d.x, center_3d.z)
	var half_size := Vector2(region.size) * _get_safe_pixel_size() * 0.5
	var outside := (point - center).abs() - half_size
	outside = Vector2(maxf(outside.x, 0.0), maxf(outside.y, 0.0))
	return outside.length()
