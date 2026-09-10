def can_build(env, platform):
    return not env["disable_3d"]


def configure(env):
    pass


def get_doc_classes():
    return [
        "CSGBevelSettings",
        "CSGBox3D",
        "CSGCombiner3D",
        "CSGCylinder3D",
        "CSGMesh3D",
        "CSGModifier",
        "CSGModifierContext",
        "CSGPolygon3D",
        "CSGPrimitive3D",
        "CSGShape3D",
        "CSGSphere3D",
        "CSGTopologySettings",
        "CSGTorus3D",
    ]


def get_doc_path():
    return "doc_classes"
