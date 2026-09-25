def can_build(env, platform):
    return not env["disable_3d"]


def configure(env):
    pass


def get_doc_classes():
    return [
        "CSGAttributeModifier",
        "CSGBevelSettings",
        "CSGBox3D",
        "CSGCombiner3D",
        "CSGCylinder3D",
        "CSGFaceSemanticModifier",
        "CSGGeometryData",
        "CSGHeightMap3D",
        "CSGHeightMapLayer",
        "CSGImprint3D",
        "CSGMesh3D",
        "CSGModifier",
        "CSGModifierChannelOverride",
        "CSGModifierContext",
        "CSGModifierValue",
        "CSGPolygon3D",
        "CSGPrimitive3D",
        "CSGShape3D",
        "CSGSplitSettings",
        "CSGSphere3D",
        "CSGTopologySettings",
        "CSGTorus3D",
        "MeshSplitSettings",
        "MeshSplitter",
    ]


def get_doc_path():
    return "doc_classes"
