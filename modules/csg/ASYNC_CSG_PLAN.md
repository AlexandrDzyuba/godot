# Asynchronous orphan CSG generation

For a practical project-facing guide, see [ASYNC_CSG_USAGE.md](ASYNC_CSG_USAGE.md).

## Contract and implementation

1. Build a standalone `CSGShape3D` tree on the main thread. The root must be
   unparented and no node may be in the scene tree.
2. Call `root.generate_mesh_async(create_static_body)`. A successful call
   transfers ownership of the root and every child to the returned `CSGAsyncJob`.
   A failed call returns `null` and leaves ownership with the caller.
3. The caller must not touch, free, reparent, or submit the tree again while the
   job owns it. The caller must ensure all shared resources and any script
   modifiers are safe for concurrent access and remain unchanged. The API does
   not duplicate the tree. It reads height-map images and prepares the local
   geometry of `CSGMesh3D` on the main thread before scheduling the worker,
   because their source resources can require synchronous renderer readback.
   Height maps sharing a texture in one job share one readback. Different jobs
   perform their own readback; large images and meshes can therefore cause
   main-thread stalls and memory usage before the worker starts.
4. A `WorkerThreadPool` worker computes the CSG brush using the existing native
   Boolean, modifier, imprint, bevel, and topology processing. This is opt-in;
   ordinary scene CSG stays on its existing synchronous path.
5. The job finishes on the main thread: it builds the `ArrayMesh`, optionally
   creates a `StaticBody3D` with `ConcavePolygonShape3D`, destroys the input
   tree, and emits `completed`. Mesh construction and collision creation still
   have main-thread cost.
6. Cancellation is cooperative: it prevents publishing a result, but an
   in-progress Boolean cannot be interrupted. The job stays alive even when the
   caller drops its reference. Engine shutdown waits for outstanding jobs before
   shutting down extensions, languages, or resources and destroys their trees.
   An unclaimed static body belongs to the job and is
   destroyed when the job is released.

Calls and result access are intended for the main thread. The generated static
body is for static terrain or structures; concave collision is not a suitable
general-purpose collision shape for moving rigid bodies.

## Usage

```gdscript
var root := CSGCombiner3D.new()
var box := CSGBox3D.new()
box.size = Vector3(5, 5, 5)
root.add_child(box)

var cutter := CSGSphere3D.new()
cutter.operation = CSGShape3D.OPERATION_SUBTRACTION
root.add_child(cutter)

# On success, root is now owned by the job: do not use or free it again.
var job := root.generate_mesh_async(true)
if job == null:
    root.free() # Submission failed; ownership remains with the caller.
    return

await job.completed
if job.get_status() == CSGAsyncJob.STATUS_COMPLETED:
    $MeshInstance3D.mesh = job.get_mesh()
    var body := job.take_static_body()
    if body != null:
        add_child(body) # The caller now owns the body.
else:
    push_warning(job.get_error_message())
```

Output mesh coordinates are in the root CSG's local space. Apply the desired
world transform to the receiving mesh and body. Colors, custom channels, UVs,
and material surfaces use the same mesh conversion as synchronous CSG; face
metadata is not serialized into `ArrayMesh` by this API.

## Remaining validation and follow-up

- Run the CSG unit tests and compare sync/async geometry for complex Boolean,
  height map, imprint, native modifier, bevel and topology cases.
- Stress cancellation, repeated jobs, orphan destruction, shutdown, shared
  immutable resources, and simultaneous independent Manifold operations.
- Measure main-thread `ArrayMesh` and collision creation for large meshes.
- Integrate the job with the runtime chunk generator. Keep a revision per chunk
  and discard obsolete results; coalesce queued edits to the same chunk.
- For terrain editing, add a separate workflow using a baked mesh as the input
  operand. Define cache ownership before sharing mutable resources across jobs.
- Script modifiers and custom resource implementations can run arbitrary code;
  their worker safety cannot be inferred by this API. In particular, they must
  not synchronously wait for the main thread or renderer, including at shutdown.
  Prefer native modifiers, and profile main-thread texture and mesh readback.
