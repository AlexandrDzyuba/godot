# Extended CSG API — project context

This file describes the CSG functionality added in this Godot fork beyond the
upstream Godot CSG API. It is intended both as developer documentation and as a
context file for an AI assistant working on a game project built with this
engine.

The C++ headers and `modules/csg/doc_classes/*.xml` remain the source of truth.
The API below reflects the repository state as of 2026-10-01.

## Important context for future work

- These classes are engine/module classes, not a GDScript addon. A project must
  run an editor/export template built from this fork.
- Do not replace this functionality with upstream Godot assumptions. In
  particular, this fork preserves and processes vertex color and `CUSTOM0` to
  `CUSTOM3` throughout the extended CSG pipeline.
- CSG updates are deferred. After changing a CSG node from script, wait for one
  rendered frame before calling `bake_static_mesh()`, `get_geometry_data()`, or
  `split_meshes()` when the current result is required.
- The current CSG build path is not documented as thread-safe. Do not access or
  mutate scene nodes/resources from worker threads. Background terrain-edit
  jobs, persistent blueprint/storage databases, content-addressed deduplication,
  and asynchronous chunk replacement have been discussed, but are **not
  implemented by this API**.
- Native bevel can be expensive and can reject difficult output as non-manifold
  or non-finite. For large stepped height maps, `slope_width` and shader edge
  data are the preferred baseline.

## Processing order

For a normal `CSGShape3D`, the effective processing order is:

1. Build the node's local primitive/mesh.
2. Apply child CSG Boolean operations.
3. Apply `bevel_settings`, if enabled.
4. Apply `topology_settings`, if enabled.
5. Apply entries in `modifiers` in array order.
6. Build render, collision, and navigation output.

A child's completed result becomes an operand of its parent. Therefore a bevel
or modifier on a child is evaluated before the parent's Boolean operation. A
modifier on the root sees the completed root result.

`CSGImprint3D` has special behavior described below: its modifiers affect only
the selected fragments of its parent surface.

## `CSGShape3D` additions

### Properties

```gdscript
var modifiers: Array[CSGModifier]
var bevel_settings: CSGBevelSettings
var topology_settings: CSGTopologySettings
var split_settings: CSGSplitSettings
```

### Methods

```gdscript
func get_geometry_data(
        merge_epsilon: float = 0.00001,
        sharp_angle: float = 0.523599
    ) -> CSGGeometryData

func split_meshes(settings: CSGSplitSettings = null) -> Array[ArrayMesh]
func bake_static_mesh() -> ArrayMesh
func bake_collision_shape() -> ConcavePolygonShape3D
```

`split_meshes()` does not modify the scene tree. If `settings` is `null`, the
node's `split_settings` is used, then defaults if that is also empty.

`bake_static_mesh()` and `bake_collision_shape()` only produce meaningful output
for a root CSG shape.

Typical deferred use:

```gdscript
csg.size = Vector3(8, 4, 8)
await get_tree().process_frame

var mesh := csg.bake_static_mesh()
var geometry := csg.get_geometry_data()
```

## Semantic geometry: `CSGGeometryData`

`CSGGeometryData` is a read-only snapshot of the current result. It exposes
logical welded vertices, faces, edges, adjacency, stable/source identifiers,
and semantic metadata.

### Counts and geometry

```gdscript
get_vertex_count() -> int
get_face_count() -> int
get_edge_count() -> int
get_vertices() -> PackedVector3Array
get_triangle_vertices() -> PackedInt32Array
```

`get_triangle_vertices()` contains three logical vertex indices per face.

### Adjacency

```gdscript
get_vertex_faces() -> Array
get_vertex_adjacency() -> Array
get_face_adjacency() -> PackedInt32Array
get_face_edges() -> PackedInt32Array
```

- `vertex_faces[v]` is the list of faces using logical vertex `v`.
- `vertex_adjacency[v]` is the list of neighboring logical vertices.
- `face_adjacency` and `face_edges` contain three entries per face.
- A missing neighboring face is represented by `-1`.

### Face identity and metadata

```gdscript
get_face_ids() -> PackedInt64Array
get_source_face_ids() -> PackedInt64Array
get_surface_ids() -> PackedInt32Array
get_material_ids() -> PackedInt32Array
get_brush_ids() -> PackedInt32Array
get_layer_ids() -> PackedInt32Array
get_face_generation() -> PackedByteArray
get_face_semantics() -> PackedStringArray
get_face_custom_metadata() -> Array[Dictionary]
```

Face generation values:

```text
CSGGeometryData.FACE_ORIGINAL           = 0
CSGGeometryData.FACE_BOOLEAN_GENERATED  = 1
CSGGeometryData.FACE_BEVEL_GENERATED    = 2
CSGGeometryData.FACE_TOPOLOGY_GENERATED = 3
```

`face_id` identifies a face in the current semantic pipeline;
`source_face_id` tracks the originating face where that relationship exists.
Generated faces are explicitly tagged. IDs are intended for propagation and
inspection within CSG results; do not treat triangle indices or face IDs as an
unchanging save-game key across arbitrary topology edits.

### Edge information

```gdscript
get_edge_vertices() -> PackedInt32Array
get_edge_faces() -> PackedInt32Array
get_boundary_edges() -> PackedByteArray
get_sharp_edges() -> PackedByteArray
get_edge_sharpness() -> PackedFloat32Array
```

`edge_vertices` and `edge_faces` contain two values per edge. Boundary and sharp
flags contain one value per edge. Sharpness is derived using the requested
`sharp_angle`.

## Modifier framework

### `CSGModifier`

`CSGModifier` is a `Resource` with an `enabled` property. Native modifiers
override `process()`. Script resources may implement:

```gdscript
extends CSGModifier

func _process(context: CSGModifierContext) -> void:
    # Read or replace per-corner attributes and face metadata here.
    pass
```

Prefer native modifiers for frequent or large geometry updates. A script
modifier crosses the scripting boundary and usually copies packed arrays.

### `CSGModifierContext`

Geometry access:

```gdscript
get_vertices() -> PackedVector3Array
get_vertex_count() -> int
get_face_count() -> int
get_geometry_data(merge_epsilon := 0.00001, sharp_angle := 0.523599) \
    -> CSGGeometryData
```

Face data:

```gdscript
get_face_ids() -> PackedInt64Array
get_source_face_ids() -> PackedInt64Array
get_surface_ids() -> PackedInt32Array
get_material_ids() -> PackedInt32Array
get_brush_ids() -> PackedInt32Array
get_layer_ids() -> PackedInt32Array
get_face_generation() -> PackedByteArray
get_face_semantics() -> PackedStringArray
set_face_semantics(values: PackedStringArray) -> void
get_face_custom_metadata() -> Array
set_face_custom_metadata(values: Array) -> void
```

Vertex/corner channels:

```gdscript
has_colors() -> bool
get_colors() -> PackedColorArray
set_colors(values: PackedColorArray) -> void
clear_colors() -> void

has_custom(channel: int) -> bool
get_custom_format(channel: int) -> Mesh.ArrayCustomFormat
set_custom_format(channel: int, format: Mesh.ArrayCustomFormat) -> void
get_custom(channel: int) -> PackedVector4Array
set_custom(channel: int, values: PackedVector4Array) -> void
clear_custom(channel: int) -> void
```

Custom channel indices are `0` through `3`. Attribute arrays are per physical
triangle corner, so their expected size is normally `face_count * 3`, not the
welded logical vertex count returned by `CSGGeometryData`.

## Native attribute modifiers

### `CSGModifierValue`

Each output component is computed from a source, then transformed by:

```text
value = source_value * scale + bias
```

It can optionally be clamped to `min_value ... max_value`.

Available `source` values:

```text
SOURCE_CONSTANT                 = 0
SOURCE_BRUSH_ID                 = 1
SOURCE_FACE_ID                  = 2
SOURCE_SOURCE_FACE_ID           = 3
SOURCE_SURFACE_ID               = 4
SOURCE_MATERIAL_ID              = 5
SOURCE_FACE_GENERATION          = 6
SOURCE_TRIANGLE_EDGE_DISTANCE_0 = 7
SOURCE_TRIANGLE_EDGE_DISTANCE_1 = 8
SOURCE_TRIANGLE_EDGE_DISTANCE_2 = 9
SOURCE_BOUNDARY_EDGE            = 10
SOURCE_SHARP_EDGE               = 11
SOURCE_LAYER_ID                 = 12
SOURCE_KEEP_EXISTING            = 13
```

`SOURCE_TRIANGLE_EDGE_DISTANCE_0..2` are barycentric-style per-corner distances
to meaningful geometric edges. Coplanar diagonals introduced only by polygon
triangulation are ignored. Boundary and sharp edges are controlled by the
attribute modifier's `merge_epsilon` and `sharp_angle`.

`SOURCE_KEEP_EXISTING` is especially important for imprints: it changes only
the selected components and preserves the others.

### `CSGModifierChannelOverride`

A channel override owns four `CSGModifierValue` resources:

```gdscript
var red: CSGModifierValue
var green: CSGModifierValue
var blue: CSGModifierValue
var alpha: CSGModifierValue
var constant_color: Color
var custom_format: Mesh.ArrayCustomFormat
```

The default custom format is `Mesh.ARRAY_CUSTOM_RGBA_FLOAT`. `constant_color`
is a convenience property for setting the four constant values.

### `CSGAttributeModifier`

The modifier can independently enable vertex color and all four custom channels:

```gdscript
set_vertex_color_override_enabled(enabled: bool)
get_vertex_color_override() -> CSGModifierChannelOverride

set_custom_override_enabled(channel: int, enabled: bool)
get_custom_override(channel: int) -> CSGModifierChannelOverride

var topology_merge_epsilon: float
var topology_sharp_angle: float # radians in script, degrees in the inspector
```

Example: write edge distances to `CUSTOM0.rgb` and height-map layer ID to
`CUSTOM0.a`:

```gdscript
var modifier := CSGAttributeModifier.new()
modifier.set_custom_override_enabled(0, true)

var output := modifier.get_custom_override(0)
output.custom_format = Mesh.ARRAY_CUSTOM_RGBA_FLOAT
output.red.source = CSGModifierValue.SOURCE_TRIANGLE_EDGE_DISTANCE_0
output.green.source = CSGModifierValue.SOURCE_TRIANGLE_EDGE_DISTANCE_1
output.blue.source = CSGModifierValue.SOURCE_TRIANGLE_EDGE_DISTANCE_2
output.alpha.source = CSGModifierValue.SOURCE_LAYER_ID

csg.modifiers = [modifier]
```

There is also a flat `set_channel_source(slot, source)` convenience API. Slots
are laid out as `(channel * 4 + component)`, where channel `0` is vertex color,
channels `1..4` are `CUSTOM0..3`, and components are RGBA (`0..3`). Prefer the
explicit override resources in reusable game code because they are clearer.

### `CSGFaceSemanticModifier`

Maps generated face categories to `StringName` semantics:

```gdscript
var original_semantic: StringName
var boolean_semantic: StringName
var bevel_semantic: StringName
var topology_semantic: StringName
var preserve_non_empty: bool = true
```

Example semantics include `STONE_WALL`, `STONE_EDGE`, `BROKEN_STONE`, or a
project-specific tag. Semantics do not create additional rendering surfaces by
themselves; write data to vertex/custom channels when the shader needs it.

## `CSGImprint3D`: non-destructive surface selection

`CSGImprint3D` inherits `CSGCombiner3D`. It uses closed child CSG shapes as a
selection volume that splits the direct parent's surface without changing the
parent solid's volume. Its modifier stack applies only to parent-surface
fragments inside that volume. Temporary volume and cut faces are discarded.

Typical scene tree:

```text
CSGHeightMap3D
└── CSGImprint3D                 # modifiers live here
    └── CSGBox3D / CSGMesh3D     # one or more closed selection volumes
```

Use it for roads, biome masks, path coordinates, damage masks, or localized
shader IDs. It is more flexible than adding another Boolean operation because
it selects/annotates existing surfaces rather than changing the solid.

Constant and keep-existing attribute sources use a faster direct property path.
Topology-dependent sources and script modifiers use the general brush path.
Each imprint still performs intersection and subtraction internally; many
persistent imprints increase topology, so bake/simplify accumulated edits.

## Native bevel and topology processing

### `CSGBevelSettings`

```gdscript
var enabled: bool = true
var width: float = 0.1
var angle_threshold: float = deg_to_rad(30.0)
var debug_print: bool = false
```

The native bevel is applied to the completed result of the node. It propagates
face metadata and tags new faces as `FACE_BEVEL_GENERATED`. Width may be clamped
near small features. When output fails Manifold validation, it is rejected
rather than replacing valid source geometry. Enable `debug_print` for diagnostic
counts/status.

This bevel has no segment-count option. Large, contour-heavy height maps can
generate many polygons and difficult caps, so bevel is not the default solution
for their visual edge treatment.

### `CSGTopologySettings`

```text
TOPOLOGY_NONE        = 0
TOPOLOGY_EDGE_VOLUME = 1
```

Properties:

```gdscript
var mode: CSGTopologySettings.TopologyMode
var edge_width: float = 0.05
var angle_threshold: float = deg_to_rad(30.0)
var merge_epsilon: float = 0.00001
```

`TOPOLOGY_EDGE_VOLUME` replaces the shape with closed volumes around real
geometric edges. Coplanar triangulation diagonals are ignored.

## `CSGHeightMap3D`

`CSGHeightMap3D` creates a closed, stepped CSG solid from a `Texture2D`. It does
not create and union one cube per pixel.

### Main properties

```gdscript
var height_map: Texture2D
var region_enabled: bool = false
var region_rect: Rect2i
var size: Vector3 = Vector3(10, 2, 10)
var height_channel: CSGHeightMap3D.HeightChannel
var height_multiplier_channel: CSGHeightMap3D.HeightMultiplierChannel
var height_multiplier: float = 1.0
var generation_mode: CSGHeightMap3D.GenerationMode
var height_steps: int = 16
var sampling_step: int = 1
var slope_width: float = 0.0
var layer_settings: Array[CSGHeightMapLayer]
var invert_height: bool = false
var base_thickness: float = 0.01
var material: Material
var side_material: Material
var bottom_material: Material
```

Height channels are `HEIGHT_CHANNEL_LUMINANCE`, `RED`, `GREEN`, `BLUE`, and
`ALPHA`.

The optional height multiplier channel is selected independently:

```text
HEIGHT_MULTIPLIER_CHANNEL_NONE      = 0
HEIGHT_MULTIPLIER_CHANNEL_LUMINANCE = 1
HEIGHT_MULTIPLIER_CHANNEL_RED       = 2
HEIGHT_MULTIPLIER_CHANNEL_GREEN     = 3
HEIGHT_MULTIPLIER_CHANNEL_BLUE      = 4
HEIGHT_MULTIPLIER_CHANNEL_ALPHA     = 5
```

The effective normalized height is:

```text
clamp(primary_height * texture_channel_multiplier * height_multiplier, 0, 1)
```

`NONE` supplies a texture multiplier of `1.0`, preserving previous scenes and
allowing `height_multiplier` to act as a global scalar. This is useful when one
texture channel contains local terrain detail and another contains a broad biome
mask: low mask values produce plains while high values retain hills or mountain
ranges. Multiplication happens before `height_steps` quantization, so stepped
contour modes do not create a unique contour level for every smooth mask value.

Generation modes:

```text
GENERATION_CELL_GRID          = 0
GENERATION_CONTOUR_LAYERS     = 1
GENERATION_DIRECT_CONTOUR_MESH = 2
```

- `CELL_GRID` is the detailed fallback path.
- `CONTOUR_LAYERS` builds cumulative contour masks as non-overlapping slabs and
  batches Boolean work. It is the default.
- `DIRECT_CONTOUR_MESH` directly emits the closed top/cliff/border/bottom mesh
  and is normally fastest. It automatically falls back to contour layers if the
  direct result is open/invalid.

`height_steps` quantizes the texture. Values `0` and `1` disable useful stepped
quantization. `sampling_step` reduces source resolution and geometry cost.

### Layers and slopes

Every quantized height layer receives a `layer_id`, available through
`CSGModifierValue.SOURCE_LAYER_ID` and `CSGGeometryData.get_layer_ids()`.

`CSGHeightMapLayer` overrides a layer with:

```gdscript
var height_weight: float = 1.0
var slope_width: float = -1.0 # negative inherits the node value
```

Height weights are normalized so the highest layer still reaches the configured
top of `size.y`. Layer zero retains the base-thickness role. `slope_width`
insets the upper contour boundary to form a terrace slope. It is automatically
clamped when geometry cannot support the requested inset; chunk-border vertices
remain fixed so neighboring chunks can match. It is a contour-layer feature,
not a `CELL_GRID` deformation.

### Surfaces and semantics

Generated faces use these surface IDs and semantics:

| Surface ID | Meaning | Semantic |
| ---: | --- | --- |
| 0 | top | `HEIGHTMAP_TOP` |
| 1 | internal cliff/side | `HEIGHTMAP_CLIFF` |
| 2 | outer border | `HEIGHTMAP_BORDER` |
| 3 | bottom | `HEIGHTMAP_BOTTOM` |

`side_material` falls back to `material`. `bottom_material` falls back to
`side_material`, then `material`.

### Chunking a large texture

Use multiple `CSGHeightMap3D` nodes pointing at the same texture. Enable
`region_enabled` and assign a pixel-space `region_rect` such as 96×96. `size`
maps the effective region, so the game must position nodes consistently in world
space. Keep the same sampling, height, step, and layer settings across adjacent
chunks when seamless borders are required.

```gdscript
heightmap.height_map = shared_height_texture
heightmap.region_enabled = true
heightmap.region_rect = Rect2i(chunk_px * Vector2i(96, 96), Vector2i(96, 96))
heightmap.size = Vector3(96.0, 32.0, 96.0)
heightmap.height_channel = CSGHeightMap3D.HEIGHT_CHANNEL_RED
heightmap.height_multiplier_channel = \
        CSGHeightMap3D.HEIGHT_MULTIPLIER_CHANNEL_GREEN
heightmap.height_multiplier = 1.0
heightmap.generation_mode = CSGHeightMap3D.GENERATION_DIRECT_CONTOUR_MESH
```

### Runtime chunk generator example

`modules/csg/examples/csg_height_map_chunk_generator_3d.gd` is a project-side
GDScript template named `CSGHeightMapChunkGenerator3D`. Copy it into the game
project and attach it to a `Node3D`. Geometry generation remains in native C++;
the script is responsible only for chunk lifecycle and configuration.

It supports:

- `FULL_TEXTURE`, which incrementally creates every pixel region in the source;
- `AROUND_TARGET`, which loads regions within `load_distance` and removes them
  beyond `unload_distance`;
- `max_chunk_creations_per_frame`, which limits how many new CSG nodes enter the
  tree in one frame;
- one shared height texture, arbitrary `chunk_size_pixels`, non-square world
  pixel scale, and smaller edge chunks;
- the complete height-map, modifier, material, bevel, topology, render, and
  collision configuration used for every generated terrain node;
- an optional `PackedScene` instantiated once per chunk for water, fog,
  navigation helpers, or gameplay content.

The chunk scene root must inherit `Node3D`. It may contain an existing
`CSGHeightMap3D` selected by `terrain_node_path`, or the generator creates one
under `terrain_parent_path`. Its root can optionally receive chunk context:

```gdscript
func setup_csg_height_map_chunk(
        generator: CSGHeightMapChunkGenerator3D,
        chunk_coordinate: Vector2i,
        source_region: Rect2i,
        terrain: CSGHeightMap3D
    ) -> void:
    $Water.position.y = water_level
```

Generated roots also store `csg_height_map_chunk_coordinate` and
`csg_height_map_source_region` metadata. Public lifecycle methods are
`refresh_chunks()`, `rebuild_chunks()`, and `clear_chunks()`.

The per-frame creation budget spreads main-thread work over time; it does not
make the CSG build threaded. Worker-thread mesh generation and atomic mesh
replacement remain future architecture work.

### Shader channel contract example

The height map does not reserve a fixed custom-channel layout. Define the
layout with `CSGAttributeModifier`. For the example above (`CUSTOM0.rgb` edge
distance, `CUSTOM0.a` layer ID):

```glsl
shader_type spatial;

uniform sampler2DArray terrain_textures;
uniform float texture_scale = 0.1;

varying vec3 edge_distance;
varying flat float layer_id;
varying vec3 world_position;
varying vec3 world_normal;

void vertex() {
    edge_distance = CUSTOM0.rgb;
    layer_id = CUSTOM0.a;
    world_position = (MODEL_MATRIX * vec4(VERTEX, 1.0)).xyz;
    world_normal = normalize(MODEL_NORMAL_MATRIX * NORMAL);
}

void fragment() {
    vec3 weights = pow(abs(world_normal), vec3(4.0));
    weights /= max(weights.x + weights.y + weights.z, 0.00001);
    float slice = floor(layer_id + 0.5);

    vec3 x_tex = texture(terrain_textures,
            vec3(world_position.zy * texture_scale, slice)).rgb;
    vec3 y_tex = texture(terrain_textures,
            vec3(world_position.xz * texture_scale, slice)).rgb;
    vec3 z_tex = texture(terrain_textures,
            vec3(world_position.xy * texture_scale, slice)).rgb;

    ALBEDO = x_tex * weights.x + y_tex * weights.y + z_tex * weights.z;
}
```

## Importing an `ArrayMesh` through `CSGMesh3D`

This fork's `CSGMesh3D` importer accepts triangle surfaces and preserves:

- materials and source surface IDs;
- UV;
- vertex color;
- `CUSTOM0` through `CUSTOM3`, including their declared custom formats.

If surfaces use different formats for the same custom channel, the result is
promoted to `Mesh.ARRAY_CUSTOM_RGBA_FLOAT` so values are not discarded. Missing
colors/custom values on another surface receive neutral defaults.

The input still needs geometry suitable for CSG Boolean operations. Import
preservation does not make an open or self-intersecting mesh manifold. Use the
independent `MeshSplitter` when the task is merely to cut arbitrary triangle
geometry.

## Splitting completed CSG solids

### `CSGSplitSettings`

Modes:

```text
MODE_BALANCED_GRID = 0
MODE_RANDOM_PLANES = 1
```

Core properties:

```gdscript
var mode
var iterations: int = 3
var seed: int = 1
var rotation_jitter: float = deg_to_rad(35.0)
var offset_jitter: float = 0.35
var min_piece_volume: float = 0.000001
var max_pieces: int = 256
var retry_count: int = 8
var decompose_islands: bool = true
var part_center
var output
var cut_uv_scale: float = 1.0
var cut_modifiers: Array[CSGModifier]
```

Each iteration can split every current piece once, bounded by `max_pieces`.
Random mode is deterministic for a given `seed`. Newly exposed faces receive
the semantic `CSG_SPLIT_CUT`; `cut_modifiers` affect only those faces. Use this
to encode material/shader data such as `BROKEN_STONE` without requiring another
rendering surface.

Part center values:

```text
PART_CENTER_ORIGINAL, PART_CENTER_AABB,
PART_CENTER_TOP, PART_CENTER_BOTTOM,
PART_CENTER_X_FRONT, PART_CENTER_Y_FRONT, PART_CENTER_Z_FRONT,
PART_CENTER_X_BACK, PART_CENTER_Y_BACK, PART_CENTER_Z_BACK
```

`split_meshes()` preserves UV, materials, vertex colors, and all custom
channels. Each returned mesh is recentered and contains:

```gdscript
mesh.get_meta("csg_split_center")    # Vector3 local offset from source node
mesh.get_meta("csg_split_direction") # normalized accumulated split direction
```

Direct API example:

```gdscript
var settings := CSGSplitSettings.new()
settings.mode = CSGSplitSettings.MODE_RANDOM_PLANES
settings.iterations = 3
settings.seed = 42
settings.max_pieces = 64

for part: ArrayMesh in csg.split_meshes(settings):
    var instance := MeshInstance3D.new()
    instance.mesh = part
    instance.position = part.get_meta("csg_split_center", Vector3.ZERO)
    parts_root.add_child(instance)
```

### Editor output and velocity

With a CSG node selected, use **CSG > Create Split Meshes**. The editor creates
a sibling `Node3D` named from the source plus `_Splits`. `output` chooses plain
mesh instances or rigid bodies with convex collision.

Velocity modes:

```text
LINEAR_VELOCITY_NONE               = 0
LINEAR_VELOCITY_CONSTANT           = 1
LINEAR_VELOCITY_FROM_OBJECT_CENTER = 2
LINEAR_VELOCITY_FROM_SPLIT_PLANES  = 3
```

`linear_velocity` is used by constant mode. `linear_speed` scales the two
directional modes. `output` and velocity are editor scene-generation concerns;
the runtime `split_meshes()` method always returns `ArrayMesh` resources and
leaves node/physics creation to the caller.

## Splitting arbitrary meshes

`MeshSplitter` is independent from CSG and is intended for ordinary imported
meshes, including many open/non-manifold triangle soups.

```gdscript
var splitter := MeshSplitter.new()
var settings := MeshSplitSettings.new()
var parts: Array[ArrayMesh] = splitter.split_mesh(source_mesh, settings)
```

It clips triangle surfaces directly and interpolates/preserves:

- material and surface name;
- normals and tangents;
- vertex colors;
- UV and UV2;
- `CUSTOM0` through `CUSTOM3`, including custom formats.

It does not currently preserve skinning, bones/weights, blend shapes, or
non-triangle primitive surfaces.

### `MeshSplitSettings`

The common mode, iteration, seed, jitter, max-piece, retry, island, center,
output, and velocity options mirror `CSGSplitSettings`. Mesh-specific options:

```gdscript
var min_triangles: int = 1
var weld_tolerance: float = 0.0001
var cap_mode: MeshSplitSettings.CapMode = CAP_REPAIR_OPEN_LOOPS
var cap_material: Material
var cap_uv_scale: float = 1.0
var cap_color: Color = Color.WHITE
```

For each channel `0..3`:

```gdscript
settings.set_cap_custom_enabled(channel, true)
settings.set_cap_custom(channel, Vector4(...))
```

Cap modes:

```text
CAP_NONE              = 0
CAP_CLOSED_LOOPS      = 1
CAP_REPAIR_OPEN_LOOPS = 2
```

Cap triangles use a surface named `SplitCaps`, corrected winding/outward
normals, and the configured cap attributes. Repair mode attempts to close cut
chains from imperfect input, but cannot guarantee valid solids for severe
self-intersection or corrupt topology.

Every returned mesh contains:

```gdscript
mesh.get_meta("split_center")
mesh.get_meta("split_direction")
```

In the editor, select `MeshInstance3D` and use **Mesh > Split Mesh...**. The
dialog can load/save `MeshSplitSettings` as `.tres`/`.res` and creates mesh
instances or rigid bodies with convex collision. Current settings are not
written into node metadata. Legacy `mesh_split_settings` node metadata is
imported and removed through undo/redo when encountered.

## Performance and architecture guidance

- Prefer one root/combiner for connected static CSG geometry when a single
  combined mesh is wanted. Draw calls are still determined by material surfaces.
- Avoid keeping thousands of live Boolean nodes for persistent terrain edits.
  Bake stable chunk results to `ArrayMesh`, retain color/custom channels, and
  use `CSGMesh3D` only when a baked mesh must re-enter CSG.
- `CSGHeightMap3D.GENERATION_DIRECT_CONTOUR_MESH`, fewer `height_steps`, and a
  larger `sampling_step` are the main height-map generation controls.
- Many imprints are cheaper conceptually than duplicated overlay geometry, but
  they still split the mesh and increase topology.
- Bevel often dominates generation time and polygon count. Use it selectively.
- Split piece count can grow approximately exponentially with iterations; always
  set a practical `max_pieces`.
- Rigid bodies with generated convex collision have additional CPU/physics cost.
  Prefer spawning only nearby/active debris and baking distant results.

## Implemented versus planned

Implemented in this fork:

- semantic face/edge/adjacency snapshots;
- metadata propagation through CSG and native bevel;
- vertex color and four custom-channel modifiers;
- real-edge distance, boundary, sharp-edge, and layer sources;
- surface imprinting;
- native bevel and edge-volume topology mode;
- contour/chunked stepped height maps with per-layer controls;
- color/custom-preserving `CSGMesh3D` import;
- recursive closed-solid CSG splitting;
- cap-producing arbitrary triangle-mesh splitting;
- editor dialogs, settings resources, rigid-body output, and initial velocity.

Not implemented merely by the APIs in this document:

- a persistent ID-addressed CSG structure/blueprint database;
- immutable revisions, content-addressed deduplication, or chunk edit logs;
- automatic merging of 25 placed gameplay blocks into one persistent combiner;
- background/threaded scene-tree CSG generation;
- automatic terrain chunk streaming or runtime LOD;
- a guarantee that arbitrary non-manifold input becomes Boolean-safe CSG.

Treat those items as future architecture work, not callable engine features.
