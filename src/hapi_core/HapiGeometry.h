// HapiGeometry: pull cooked geometry out of Houdini into host-neutral
// structures, and push host meshes/curves into Houdini as inputs.
//
// The traversal order (asset -> objects -> display/output geos -> parts) and
// the attribute conventions (P/N/uv/Cd/Alpha, vertex-then-point lookup,
// `name` splitting, `*_material` / `*_instance` prefixes) follow the SideFX
// Unreal/Unity/Maya/Max plugins so HDAs authored for them behave the same here.
#pragma once

#include "GeoMath.h"
#include "HapiSession.h"

#include <map>
#include <string>
#include <vector>

namespace hapi {

struct AttributeData
{
    std::string name;
    HAPI_AttributeOwner owner = HAPI_ATTROWNER_INVALID;
    HAPI_StorageType storage = HAPI_STORAGETYPE_INVALID;
    int tupleSize = 0;
    int count = 0;                 // number of elements (points, vertices, ...)
    std::vector<float> floats;     // FLOAT/FLOAT64 (converted)
    std::vector<int> ints;         // INT/INT64/INT8/INT16/UINT8 (converted)
    std::vector<std::string> strings;

    bool exists() const { return owner != HAPI_ATTROWNER_INVALID && count > 0; }
    bool isFloat() const { return !floats.empty(); }
    bool isInt() const { return !ints.empty(); }
    bool isString() const { return !strings.empty(); }
    // First string value (handy for detail attributes).
    std::string firstString() const { return strings.empty() ? std::string() : strings[0]; }
};

struct GroupData
{
    std::string name;
    std::vector<int> membership;   // one 0/1 entry per element
    bool allEqual = false;
};

struct MeshPart
{
    HAPI_PartId id = -1;
    std::string name;
    int pointCount = 0, vertexCount = 0, faceCount = 0;
    bool isInstanced = false;          // only referenced by an instancer; do not build standalone

    std::vector<float> P;              // pointCount*3, Houdini space
    std::vector<int> faceCounts;       // faceCount
    std::vector<int> vertexList;       // vertexCount, Houdini winding

    AttributeData N, uv, uv2, Cd, Alpha;
    std::vector<AttributeData> extra;  // optional user-requested attributes

    // Materials per face (HAPI_GetMaterialNodeIdsOnFaces); -1 = none.
    std::vector<HAPI_NodeId> faceMaterials;
    bool faceMaterialsAllSame = true;
    AttributeData hostMaterial;        // c4d_material / unreal_material / unity_material (prim or detail)

    AttributeData nameAttr;            // prim string "name" (split hint)
    AttributeData outputName;          // detail string c4d_output_name
    std::vector<GroupData> primGroups;
    std::vector<GroupData> pointGroups;
};

struct CurvePart
{
    HAPI_PartId id = -1;
    std::string name;
    HAPI_CurveType type = HAPI_CURVETYPE_INVALID;
    int curveCount = 0;
    bool periodic = false, closed = false, hasKnots = false;
    std::vector<int> vertexCounts;     // per curve
    std::vector<int> orders;           // per curve
    std::vector<float> knots;
    std::vector<float> P;              // all curve CVs, Houdini space
    AttributeData Cd;
};

struct InstancerPart
{
    HAPI_PartId id = -1;
    std::string name;
    int instanceCount = 0;
    std::vector<Transform> transforms;             // Houdini space, SRT
    std::vector<HAPI_PartId> instancedPartIds;     // packed primitives in the same geo
    AttributeData instanceAttr;                    // point string c4d_instance / unreal_instance / instance
    AttributeData splitAttr;                       // c4d_split_attr value per instance (optional)
};

struct VolumePart
{
    HAPI_PartId id = -1;
    std::string name;                 // "density", "Cd", ...
    HAPI_VolumeType type = HAPI_VOLUMETYPE_INVALID;
    int xLength = 0, yLength = 0, zLength = 0;
    int tupleSize = 1;
    Transform transform;
    // Voxel values are not fetched in the proof of concept; see docs/roadmap.
};

struct GeoOutput
{
    HAPI_NodeId nodeId = -1;
    std::string name;
    HAPI_GeoType type = HAPI_GEOTYPE_INVALID;
    bool isDisplay = false, isTemplated = false, isEditable = false, hasChanged = true;
    std::vector<MeshPart> meshes;
    std::vector<CurvePart> curves;
    std::vector<InstancerPart> instancers;
    std::vector<VolumePart> volumes;
    int skippedParts = 0;
};

struct ObjectOutput
{
    HAPI_NodeId nodeId = -1;
    std::string name;
    Transform transform;               // relative to the asset, Houdini space
    bool visible = true;
    bool isInstancer = false;          // old-style object instancer
    bool isInstanced = false;
    std::vector<GeoOutput> geos;
    // Object-level instancing (point instancer object): one transform per point.
    std::vector<Transform> instanceTransforms;
    std::vector<HAPI_NodeId> instancedObjectIds;
};

struct MaterialOutput
{
    HAPI_NodeId nodeId = -1;
    std::string name;                  // node name
    std::string path;                  // full node path
    bool hasBaseColor = false;
    float baseColor[3] = {0.7f, 0.7f, 0.7f};
    float roughness = -1.0f;           // <0 = not found
    float metallic = -1.0f;
    float opacity = -1.0f;
    std::string baseColorTexture;      // file path if the shader references one
};

struct AssetOutput
{
    std::vector<ObjectOutput> objects;
    std::map<HAPI_NodeId, MaterialOutput> materials;
    std::vector<std::string> warnings;

    size_t totalMeshes() const;
    size_t totalInstancers() const;
    size_t totalCurves() const;
};

struct OutputOptions
{
    bool readNormals = true;
    bool readUVs = true;
    bool readColors = true;
    bool readGroups = true;
    bool readMaterials = true;
    bool readMaterialDetails = true;       // query the shader for colour/texture
    bool includeTemplated = false;
    bool includeInvisibleObjects = false;
    std::vector<std::string> extraAttributes;  // e.g. {"pscale", "id"}
};

// Host-neutral mesh to send to Houdini.
struct InputMesh
{
    std::string name;                  // becomes the input node name
    std::vector<float> P;              // Houdini space (caller converts)
    std::vector<int> faceCounts;
    std::vector<int> vertexList;       // Houdini winding (caller converts)
    std::vector<float> N;              // optional, per vertex (vertexCount*3)
    std::vector<float> uv;             // optional, per vertex (vertexCount*3, w unused)
    std::vector<float> Cd;             // optional, per vertex (vertexCount*3)
    std::vector<float> Alpha;          // optional, per vertex
    std::vector<std::string> primMaterial;   // optional per-face material name -> prim string "c4d_material"
    std::vector<std::pair<std::string, std::vector<int>>> primGroups;  // selection tags -> prim groups
    std::vector<std::pair<std::string, std::vector<int>>> pointGroups;
    Matrix4 transform = Matrix4::identity();  // object transform in Houdini space
};

struct InputCurve
{
    std::string name;
    std::vector<float> P;              // CVs in Houdini space
    bool closed = false;
    int order = 2;                     // 2 = polyline, 4 = cubic
    HAPI_CurveType type = HAPI_CURVETYPE_LINEAR;
};

// ---- outputs --------------------------------------------------------------
bool fetchAssetOutput(HAPI_NodeId assetNode, const OutputOptions& opts, AssetOutput& out);

// Attribute fetch with owner search. owner == HAPI_ATTROWNER_INVALID looks
// in VERTEX, POINT, PRIM, DETAIL order (vertex data is the most specific).
bool readAttribute(HAPI_NodeId geo, HAPI_PartId part, const std::string& name,
                   HAPI_AttributeOwner owner, AttributeData& out);
std::vector<std::string> attributeNames(HAPI_NodeId geo, HAPI_PartId part, HAPI_AttributeOwner owner);

// ---- inputs ---------------------------------------------------------------
bool createInputMesh(const InputMesh& mesh, HAPI_NodeId& outInputNode);
bool createInputCurve(const InputCurve& curve, HAPI_NodeId& outInputNode);
bool setInputNodeTransform(HAPI_NodeId inputNode, const Matrix4& houdiniSpace);
bool connectInput(HAPI_NodeId assetNode, int inputIndex, HAPI_NodeId inputNode);
bool disconnectInput(HAPI_NodeId assetNode, int inputIndex);
// Merge several input nodes into one (Sop/merge) so N host objects feed one slot.
bool createMergeNode(const std::vector<HAPI_NodeId>& inputs, HAPI_NodeId& outMergeNode);

// Attribute names recognised for host-specific hints, in priority order.
extern const char* const kMaterialAttribs[];     // c4d_material, unreal_material, unity_material, shop_materialpath
extern const char* const kInstanceAttribs[];     // c4d_instance, unreal_instance, unity_instance, instance

} // namespace hapi
