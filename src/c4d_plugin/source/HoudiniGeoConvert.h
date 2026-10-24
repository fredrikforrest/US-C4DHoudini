// HoudiniGeoConvert: Cinema 4D <-> host-neutral geometry.
#pragma once

#include "c4d.h"
#include "GeoMath.h"
#include "HapiGeometry.h"

namespace c4dhoudini {

struct BuildOptions
{
    hapi::geo::ConvertOptions conv;
    Bool normals = true, uvs = true, colors = true, groups = true, materials = true;
    Int32 instanceMode = 0;       // HDA_INSTANCE_MODE_*
    Float phongAngle = 40.0;      // degrees; <=0 disables the Phong tag
    BaseDocument* doc = nullptr;  // for material lookup by name
};

// Builds a Null hierarchy from a cooked asset. Ownership passes to the caller.
BaseObject* BuildOutputHierarchy(const hapi::AssetOutput& out, const BuildOptions& opts, const maxon::String& rootName);

// Collects every PolygonObject / SplineObject under `root` (a polygonised
// clone) into input structures. `toHoudini` converts Cinema -> Houdini space,
// `rel` maps the object's global matrix into the asset's local space.
void CollectInputGeometry(BaseObject* root, const Matrix& rel, const hapi::geo::ConvertOptions& conv,
                          maxon::BaseArray<hapi::InputMesh>& meshes, maxon::BaseArray<hapi::InputCurve>& curves);

Matrix ToC4DMatrix(const hapi::Matrix4& m);
hapi::Matrix4 FromC4DMatrix(const Matrix& m);

} // namespace c4dhoudini
