/**************************************************************************/
/*  mesh_splitter.h                                                       */
/**************************************************************************/

#pragma once

#include "mesh_split_settings.h"

#include "core/object/ref_counted.h"
#include "scene/resources/mesh.h"

class MeshSplitter : public RefCounted {
	GDCLASS(MeshSplitter, RefCounted);

protected:
	static void _bind_methods();

public:
	TypedArray<ArrayMesh> split_mesh(const Ref<Mesh> &p_mesh, const Ref<MeshSplitSettings> &p_settings = Ref<MeshSplitSettings>()) const;
};
