#include "HapiGeometry.h"

#include <algorithm>
#include <cstring>

namespace hapi {

const char* const kMaterialAttribs[] = {"c4d_material", "unreal_material", "unity_material", "shop_materialpath", nullptr};
const char* const kInstanceAttribs[] = {"c4d_instance", "unreal_instance", "unity_instance", "instance", nullptr};

size_t AssetOutput::totalMeshes() const
{
    size_t n = 0;
    for (const ObjectOutput& o : objects) for (const GeoOutput& g : o.geos) n += g.meshes.size();
    return n;
}
size_t AssetOutput::totalInstancers() const
{
    size_t n = 0;
    for (const ObjectOutput& o : objects) for (const GeoOutput& g : o.geos) n += g.instancers.size();
    return n;
}
size_t AssetOutput::totalCurves() const
{
    size_t n = 0;
    for (const ObjectOutput& o : objects) for (const GeoOutput& g : o.geos) n += g.curves.size();
    return n;
}

namespace {

Transform fromHapi(const HAPI_Transform& t)
{
    Transform r;
    for (int i = 0; i < 3; ++i) { r.position[i] = t.position[i]; r.scale[i] = t.scale[i]; r.shear[i] = t.shear[i]; }
    for (int i = 0; i < 4; ++i) r.quaternion[i] = t.rotationQuaternion[i];
    return r;
}

// Reads one attribute of a known owner. Returns false if it does not exist.
bool readAttributeOwner(Session& s, const HAPI_Session* h, HAPI_NodeId geo, HAPI_PartId part,
                        const std::string& name, HAPI_AttributeOwner owner, AttributeData& out)
{
    HAPI_AttributeInfo info = Lib::AttributeInfo_Create ? Lib::AttributeInfo_Create() : HAPI_AttributeInfo{};
    if (Lib::GetAttributeInfo(h, geo, part, name.c_str(), owner, &info) != HAPI_RESULT_SUCCESS) return false;
    if (!info.exists) return false;

    out = AttributeData();
    out.name = name;
    out.owner = owner;
    out.storage = info.storage;
    out.tupleSize = info.tupleSize;
    out.count = info.count;
    const size_t n = size_t(info.count) * size_t(info.tupleSize);
    if (n == 0) return true;

    switch (info.storage)
    {
        case HAPI_STORAGETYPE_FLOAT:
            out.floats.resize(n);
            return s.check(Lib::GetAttributeFloatData(h, geo, part, name.c_str(), &info, -1, out.floats.data(), 0, info.count), "HAPI_GetAttributeFloatData");
        case HAPI_STORAGETYPE_FLOAT64:
        {
            std::vector<double> d(n);
            if (!s.check(Lib::GetAttributeFloat64Data(h, geo, part, name.c_str(), &info, -1, d.data(), 0, info.count), "HAPI_GetAttributeFloat64Data")) return false;
            out.floats.assign(d.begin(), d.end());
            return true;
        }
        case HAPI_STORAGETYPE_INT:
            out.ints.resize(n);
            return s.check(Lib::GetAttributeIntData(h, geo, part, name.c_str(), &info, -1, out.ints.data(), 0, info.count), "HAPI_GetAttributeIntData");
        case HAPI_STORAGETYPE_INT64:
        {
            std::vector<HAPI_Int64> d(n);
            if (!s.check(Lib::GetAttributeInt64Data(h, geo, part, name.c_str(), &info, -1, d.data(), 0, info.count), "HAPI_GetAttributeInt64Data")) return false;
            out.ints.assign(d.begin(), d.end());
            return true;
        }
        case HAPI_STORAGETYPE_INT8:
        {
            std::vector<HAPI_Int8> d(n);
            if (!Lib::GetAttributeInt8Data || !s.check(Lib::GetAttributeInt8Data(h, geo, part, name.c_str(), &info, -1, d.data(), 0, info.count), "HAPI_GetAttributeInt8Data")) return false;
            out.ints.assign(d.begin(), d.end());
            return true;
        }
        case HAPI_STORAGETYPE_INT16:
        {
            std::vector<HAPI_Int16> d(n);
            if (!Lib::GetAttributeInt16Data || !s.check(Lib::GetAttributeInt16Data(h, geo, part, name.c_str(), &info, -1, d.data(), 0, info.count), "HAPI_GetAttributeInt16Data")) return false;
            out.ints.assign(d.begin(), d.end());
            return true;
        }
        case HAPI_STORAGETYPE_UINT8:
        {
            std::vector<HAPI_UInt8> d(n);
            if (!Lib::GetAttributeUInt8Data || !s.check(Lib::GetAttributeUInt8Data(h, geo, part, name.c_str(), &info, -1, d.data(), 0, info.count), "HAPI_GetAttributeUInt8Data")) return false;
            out.ints.resize(n);
            for (size_t i = 0; i < n; ++i) out.ints[i] = int((unsigned char)d[i]);
            return true;
        }
        case HAPI_STORAGETYPE_STRING:
        {
            std::vector<HAPI_StringHandle> hs(n);
            if (!s.check(Lib::GetAttributeStringData(h, geo, part, name.c_str(), &info, hs.data(), 0, info.count), "HAPI_GetAttributeStringData")) return false;
            out.strings = s.getStrings(hs);
            return true;
        }
        default:
            // Array / dictionary attributes are not needed by the PoC.
            return false;
    }
}

bool readFirstOf(Session& s, const HAPI_Session* h, HAPI_NodeId geo, HAPI_PartId part,
                 const char* const* names, const std::vector<HAPI_AttributeOwner>& owners, AttributeData& out)
{
    for (int i = 0; names[i]; ++i)
        for (HAPI_AttributeOwner o : owners)
            if (readAttributeOwner(s, h, geo, part, names[i], o, out)) return true;
    return false;
}

void readGroups(Session& s, const HAPI_Session* h, HAPI_NodeId geo, const HAPI_GeoInfo& gi,
                HAPI_PartId part, HAPI_GroupType type, int elementCount, std::vector<GroupData>& out)
{
    int groupCount = (type == HAPI_GROUPTYPE_PRIM) ? gi.primitiveGroupCount : gi.pointGroupCount;
    if (groupCount <= 0 || elementCount <= 0) return;
    std::vector<HAPI_StringHandle> hs(static_cast<size_t>(groupCount));
    if (Lib::GetGroupNames(h, geo, type, hs.data(), groupCount) != HAPI_RESULT_SUCCESS) return;
    std::vector<std::string> names = s.getStrings(hs);
    for (const std::string& name : names)
    {
        GroupData g;
        g.name = name;
        g.membership.resize(size_t(elementCount));
        HAPI_Bool allEqual = false;
        if (Lib::GetGroupMembership(h, geo, part, type, name.c_str(), &allEqual, g.membership.data(), 0, elementCount) != HAPI_RESULT_SUCCESS)
            continue;
        g.allEqual = allEqual;
        // Skip groups this part has no members in.
        bool any = false;
        for (int m : g.membership) if (m) { any = true; break; }
        if (any) out.push_back(std::move(g));
    }
}

bool readMeshPart(Session& s, const HAPI_Session* h, HAPI_NodeId geo, const HAPI_GeoInfo& gi,
                  const HAPI_PartInfo& pi, const OutputOptions& opts, MeshPart& m, std::vector<std::string>& warnings)
{
    m.id = pi.id;
    m.name = s.getString(pi.nameSH);
    m.pointCount = pi.pointCount;
    m.vertexCount = pi.vertexCount;
    m.faceCount = pi.faceCount;
    m.isInstanced = pi.isInstanced;

    AttributeData P;
    if (!readAttributeOwner(s, h, geo, pi.id, HAPI_ATTRIB_POSITION, HAPI_ATTROWNER_POINT, P) || P.tupleSize < 3)
    {
        warnings.push_back("part '" + m.name + "' has no P attribute");
        return false;
    }
    m.P = std::move(P.floats);

    if (pi.faceCount > 0)
    {
        m.faceCounts.resize(size_t(pi.faceCount));
        if (!s.check(Lib::GetFaceCounts(h, geo, pi.id, m.faceCounts.data(), 0, pi.faceCount), "HAPI_GetFaceCounts")) return false;
        m.vertexList.resize(size_t(pi.vertexCount));
        if (!s.check(Lib::GetVertexList(h, geo, pi.id, m.vertexList.data(), 0, pi.vertexCount), "HAPI_GetVertexList")) return false;
    }

    const std::vector<HAPI_AttributeOwner> vp = {HAPI_ATTROWNER_VERTEX, HAPI_ATTROWNER_POINT};
    const std::vector<HAPI_AttributeOwner> pd = {HAPI_ATTROWNER_PRIM, HAPI_ATTROWNER_DETAIL};
    if (opts.readNormals)
    {
        const char* const n[] = {HAPI_ATTRIB_NORMAL, nullptr};
        readFirstOf(s, h, geo, pi.id, n, vp, m.N);
    }
    if (opts.readUVs)
    {
        const char* const uv[] = {HAPI_ATTRIB_UV, nullptr};
        const char* const uv2[] = {HAPI_ATTRIB_UV2, nullptr};
        readFirstOf(s, h, geo, pi.id, uv, vp, m.uv);
        readFirstOf(s, h, geo, pi.id, uv2, vp, m.uv2);
    }
    if (opts.readColors)
    {
        const char* const cd[] = {HAPI_ATTRIB_COLOR, nullptr};
        const char* const alpha[] = {"Alpha", nullptr};
        std::vector<HAPI_AttributeOwner> all = {HAPI_ATTROWNER_VERTEX, HAPI_ATTROWNER_POINT, HAPI_ATTROWNER_PRIM, HAPI_ATTROWNER_DETAIL};
        readFirstOf(s, h, geo, pi.id, cd, all, m.Cd);
        readFirstOf(s, h, geo, pi.id, alpha, all, m.Alpha);
    }
    for (const std::string& extra : opts.extraAttributes)
    {
        AttributeData a;
        if (readAttribute(geo, pi.id, extra, HAPI_ATTROWNER_INVALID, a)) m.extra.push_back(std::move(a));
    }

    if (opts.readMaterials && pi.faceCount > 0)
    {
        m.faceMaterials.resize(size_t(pi.faceCount), -1);
        HAPI_Bool same = true;
        if (Lib::GetMaterialNodeIdsOnFaces(h, geo, pi.id, &same, m.faceMaterials.data(), 0, pi.faceCount) == HAPI_RESULT_SUCCESS)
            m.faceMaterialsAllSame = same;
        else
            m.faceMaterials.clear();
        readFirstOf(s, h, geo, pi.id, kMaterialAttribs, pd, m.hostMaterial);
    }

    readAttributeOwner(s, h, geo, pi.id, HAPI_ATTRIB_NAME, HAPI_ATTROWNER_PRIM, m.nameAttr);
    {
        const char* const on[] = {"c4d_output_name", "unreal_output_name", nullptr};
        readFirstOf(s, h, geo, pi.id, on, {HAPI_ATTROWNER_DETAIL, HAPI_ATTROWNER_PRIM}, m.outputName);
    }

    if (opts.readGroups)
    {
        readGroups(s, h, geo, gi, pi.id, HAPI_GROUPTYPE_PRIM, pi.faceCount, m.primGroups);
        readGroups(s, h, geo, gi, pi.id, HAPI_GROUPTYPE_POINT, pi.pointCount, m.pointGroups);
    }
    return true;
}

bool readCurvePart(Session& s, const HAPI_Session* h, HAPI_NodeId geo, const HAPI_PartInfo& pi, CurvePart& c)
{
    c.id = pi.id;
    c.name = s.getString(pi.nameSH);
    HAPI_CurveInfo ci = Lib::CurveInfo_Create ? Lib::CurveInfo_Create() : HAPI_CurveInfo{};
    if (!s.check(Lib::GetCurveInfo(h, geo, pi.id, &ci), "HAPI_GetCurveInfo")) return false;
    c.type = ci.curveType;
    c.curveCount = ci.curveCount;
    c.periodic = ci.isPeriodic;
    c.closed = ci.isClosed;
    c.hasKnots = ci.hasKnots;
    if (ci.curveCount > 0)
    {
        c.vertexCounts.resize(size_t(ci.curveCount));
        if (!s.check(Lib::GetCurveCounts(h, geo, pi.id, c.vertexCounts.data(), 0, ci.curveCount), "HAPI_GetCurveCounts")) return false;
        c.orders.resize(size_t(ci.curveCount));
        if (ci.order == HAPI_CURVE_ORDER_VARYING)
        {
            if (!s.check(Lib::GetCurveOrders(h, geo, pi.id, c.orders.data(), 0, ci.curveCount), "HAPI_GetCurveOrders")) return false;
        }
        else
            std::fill(c.orders.begin(), c.orders.end(), ci.order);
        if (ci.hasKnots && ci.knotCount > 0)
        {
            c.knots.resize(size_t(ci.knotCount));
            Lib::GetCurveKnots(h, geo, pi.id, c.knots.data(), 0, ci.knotCount);
        }
    }
    AttributeData P;
    if (readAttributeOwner(s, h, geo, pi.id, HAPI_ATTRIB_POSITION, HAPI_ATTROWNER_POINT, P)) c.P = std::move(P.floats);
    readAttributeOwner(s, h, geo, pi.id, HAPI_ATTRIB_COLOR, HAPI_ATTROWNER_POINT, c.Cd);
    return true;
}

bool readInstancerPart(Session& s, const HAPI_Session* h, HAPI_NodeId geo, const HAPI_PartInfo& pi, InstancerPart& ip)
{
    ip.id = pi.id;
    ip.name = s.getString(pi.nameSH);
    ip.instanceCount = pi.instanceCount;
    if (pi.instanceCount > 0)
    {
        std::vector<HAPI_Transform> ts(size_t(pi.instanceCount));
        if (!s.check(Lib::GetInstancerPartTransforms(h, geo, pi.id, HAPI_SRT, ts.data(), 0, pi.instanceCount), "HAPI_GetInstancerPartTransforms")) return false;
        ip.transforms.reserve(ts.size());
        for (const HAPI_Transform& t : ts) ip.transforms.push_back(fromHapi(t));
    }
    if (pi.instancedPartCount > 0)
    {
        ip.instancedPartIds.resize(size_t(pi.instancedPartCount));
        if (!s.check(Lib::GetInstancedPartIds(h, geo, pi.id, ip.instancedPartIds.data(), 0, pi.instancedPartCount), "HAPI_GetInstancedPartIds")) return false;
    }
    readFirstOf(s, h, geo, pi.id, kInstanceAttribs, {HAPI_ATTROWNER_POINT, HAPI_ATTROWNER_DETAIL}, ip.instanceAttr);
    {
        // c4d_split_attr names another attribute whose values group instances.
        AttributeData splitName;
        const char* const sn[] = {"c4d_split_attr", "unreal_split_attr", "unity_split_attr", nullptr};
        if (readFirstOf(s, h, geo, pi.id, sn, {HAPI_ATTROWNER_DETAIL}, splitName) && !splitName.firstString().empty())
            readAttribute(geo, pi.id, splitName.firstString(), HAPI_ATTROWNER_INVALID, ip.splitAttr);
    }
    return true;
}

// Attribute-instancer: a mesh part with points but no faces and an
// `*_instance` point attribute (Unreal's "AttributeInstancer").
bool readPointInstancerPart(Session& s, const HAPI_Session* h, HAPI_NodeId geo, const HAPI_PartInfo& pi, InstancerPart& ip)
{
    ip.id = pi.id;
    ip.name = s.getString(pi.nameSH);
    ip.instanceCount = pi.pointCount;
    std::vector<HAPI_Transform> ts(size_t(pi.pointCount));
    if (!s.check(Lib::GetInstanceTransformsOnPart(h, geo, pi.id, HAPI_SRT, ts.data(), 0, pi.pointCount), "HAPI_GetInstanceTransformsOnPart")) return false;
    for (const HAPI_Transform& t : ts) ip.transforms.push_back(fromHapi(t));
    readFirstOf(s, h, geo, pi.id, kInstanceAttribs, {HAPI_ATTROWNER_POINT, HAPI_ATTROWNER_DETAIL}, ip.instanceAttr);
    return true;
}

bool readVolumePart(Session& s, const HAPI_Session* h, HAPI_NodeId geo, const HAPI_PartInfo& pi, VolumePart& v)
{
    v.id = pi.id;
    HAPI_VolumeInfo vi = Lib::VolumeInfo_Create ? Lib::VolumeInfo_Create() : HAPI_VolumeInfo{};
    if (!s.check(Lib::GetVolumeInfo(h, geo, pi.id, &vi), "HAPI_GetVolumeInfo")) return false;
    v.name = s.getString(vi.nameSH);
    v.type = vi.type;
    v.xLength = vi.xLength; v.yLength = vi.yLength; v.zLength = vi.zLength;
    v.tupleSize = vi.tupleSize;
    v.transform = fromHapi(vi.transform);
    return true;
}

bool hasPointInstanceAttr(const HAPI_Session* h, HAPI_NodeId geo, HAPI_PartId part)
{
    for (int i = 0; kInstanceAttribs[i]; ++i)
    {
        HAPI_AttributeInfo info = Lib::AttributeInfo_Create ? Lib::AttributeInfo_Create() : HAPI_AttributeInfo{};
        if (Lib::GetAttributeInfo(h, geo, part, kInstanceAttribs[i], HAPI_ATTROWNER_POINT, &info) == HAPI_RESULT_SUCCESS && info.exists)
            return true;
        if (Lib::GetAttributeInfo(h, geo, part, kInstanceAttribs[i], HAPI_ATTROWNER_DETAIL, &info) == HAPI_RESULT_SUCCESS && info.exists)
            return true;
    }
    return false;
}

bool readGeo(Session& s, const HAPI_Session* h, const HAPI_GeoInfo& gi, const OutputOptions& opts,
             GeoOutput& g, std::vector<std::string>& warnings)
{
    g.nodeId = gi.nodeId;
    g.name = s.getString(gi.nameSH);
    g.type = gi.type;
    g.isDisplay = gi.isDisplayGeo;
    g.isTemplated = gi.isTemplated;
    g.isEditable = gi.isEditable;
    g.hasChanged = gi.hasGeoChanged;

    for (int p = 0; p < gi.partCount; ++p)
    {
        HAPI_PartInfo pi = Lib::PartInfo_Create ? Lib::PartInfo_Create() : HAPI_PartInfo{};
        if (!s.check(Lib::GetPartInfo(h, gi.nodeId, p, &pi), "HAPI_GetPartInfo")) { ++g.skippedParts; continue; }
        switch (pi.type)
        {
            case HAPI_PARTTYPE_MESH:
            {
                if (pi.faceCount == 0 && pi.pointCount > 0 && hasPointInstanceAttr(h, gi.nodeId, pi.id))
                {
                    InstancerPart ip;
                    if (readPointInstancerPart(s, h, gi.nodeId, pi, ip)) g.instancers.push_back(std::move(ip));
                    else ++g.skippedParts;
                    break;
                }
                MeshPart m;
                if (readMeshPart(s, h, gi.nodeId, gi, pi, opts, m, warnings)) g.meshes.push_back(std::move(m));
                else ++g.skippedParts;
                break;
            }
            case HAPI_PARTTYPE_CURVE:
            {
                CurvePart c;
                if (readCurvePart(s, h, gi.nodeId, pi, c)) g.curves.push_back(std::move(c));
                else ++g.skippedParts;
                break;
            }
            case HAPI_PARTTYPE_INSTANCER:
            {
                InstancerPart ip;
                if (readInstancerPart(s, h, gi.nodeId, pi, ip)) g.instancers.push_back(std::move(ip));
                else ++g.skippedParts;
                break;
            }
            case HAPI_PARTTYPE_VOLUME:
            {
                VolumePart v;
                if (readVolumePart(s, h, gi.nodeId, pi, v)) g.volumes.push_back(std::move(v));
                else ++g.skippedParts;
                break;
            }
            default:
                ++g.skippedParts;
                break;
        }
    }
    return true;
}

void readMaterial(Session& s, const HAPI_Session* h, HAPI_NodeId matNode, bool details, MaterialOutput& m)
{
    m.nodeId = matNode;
    HAPI_NodeInfo ni = Lib::NodeInfo_Create ? Lib::NodeInfo_Create() : HAPI_NodeInfo{};
    if (Lib::GetNodeInfo(h, matNode, &ni) == HAPI_RESULT_SUCCESS)
    {
        m.name = s.getString(ni.nameSH);
        m.path = s.getString(ni.internalNodePathSH);
    }
    if (!details) return;

    // Principled Shader / Material Builder parm names, with the OpenGL
    // fallbacks the Maya and Max plugins use. Missing parms simply fail.
    auto getF = [&](const char* name, int idx, float& out) -> bool {
        return Lib::GetParmFloatValue(h, matNode, name, idx, &out) == HAPI_RESULT_SUCCESS;
    };
    auto getS = [&](const char* name, std::string& out) -> bool {
        HAPI_StringHandle sh = 0;
        if (Lib::GetParmStringValue(h, matNode, name, 0, true, &sh) != HAPI_RESULT_SUCCESS) return false;
        out = s.getString(sh);
        return !out.empty();
    };
    const char* colorParms[] = {"basecolor", "ogl_diff", "diffuse_color", nullptr};
    for (int i = 0; colorParms[i] && !m.hasBaseColor; ++i)
    {
        float c[3];
        if (getF(colorParms[i], 0, c[0]) && getF(colorParms[i], 1, c[1]) && getF(colorParms[i], 2, c[2]))
        {
            m.hasBaseColor = true;
            std::memcpy(m.baseColor, c, sizeof(c));
        }
    }
    float v = 0;
    if (getF("rough", 0, v) || getF("ogl_rough", 0, v)) m.roughness = v;
    if (getF("metallic", 0, v) || getF("ogl_metallic", 0, v)) m.metallic = v;
    if (getF("opac", 0, v) || getF("ogl_alpha", 0, v)) m.opacity = v;
    const char* texParms[] = {"basecolor_texture", "ogl_tex1", "diffuse_texture", nullptr};
    for (int i = 0; texParms[i] && m.baseColorTexture.empty(); ++i) getS(texParms[i], m.baseColorTexture);
}

// Collect every geo we should output for one object node.
bool collectGeos(Session& s, const HAPI_Session* h, HAPI_NodeId objectNode, const OutputOptions& opts,
                 ObjectOutput& obj, std::vector<std::string>& warnings)
{
    std::vector<HAPI_GeoInfo> geos;

    // Preferred: output nodes (Output SOPs / "output" flag), newer HAPI.
    if (Lib::GetOutputGeoCount && Lib::GetOutputGeoInfos)
    {
        int n = 0;
        if (Lib::GetOutputGeoCount(h, objectNode, &n) == HAPI_RESULT_SUCCESS && n > 0)
        {
            geos.resize(size_t(n));
            if (Lib::GetOutputGeoInfos(h, objectNode, geos.data(), n) != HAPI_RESULT_SUCCESS) geos.clear();
        }
    }
    // Fallback: the display SOP.
    if (geos.empty())
    {
        HAPI_GeoInfo gi = Lib::GeoInfo_Create ? Lib::GeoInfo_Create() : HAPI_GeoInfo{};
        if (Lib::GetDisplayGeoInfo(h, objectNode, &gi) == HAPI_RESULT_SUCCESS) geos.push_back(gi);
    }
    // Editable/templated geos are intentionally skipped unless asked.
    for (const HAPI_GeoInfo& gi : geos)
    {
        if (gi.isTemplated && !opts.includeTemplated) continue;
        GeoOutput g;
        readGeo(s, h, gi, opts, g, warnings);
        obj.geos.push_back(std::move(g));
    }
    return true;
}

} // namespace

bool readAttribute(HAPI_NodeId geo, HAPI_PartId part, const std::string& name, HAPI_AttributeOwner owner, AttributeData& out)
{
    Session& s = Session::get();
    SessionLock lock;
    const HAPI_Session* h = s.handle();
    if (!h) return false;
    if (owner != HAPI_ATTROWNER_INVALID) return readAttributeOwner(s, h, geo, part, name, owner, out);
    const HAPI_AttributeOwner order[] = {HAPI_ATTROWNER_VERTEX, HAPI_ATTROWNER_POINT, HAPI_ATTROWNER_PRIM, HAPI_ATTROWNER_DETAIL};
    for (HAPI_AttributeOwner o : order)
        if (readAttributeOwner(s, h, geo, part, name, o, out)) return true;
    return false;
}

std::vector<std::string> attributeNames(HAPI_NodeId geo, HAPI_PartId part, HAPI_AttributeOwner owner)
{
    Session& s = Session::get();
    SessionLock lock;
    const HAPI_Session* h = s.handle();
    std::vector<std::string> out;
    if (!h) return out;
    HAPI_PartInfo pi = Lib::PartInfo_Create ? Lib::PartInfo_Create() : HAPI_PartInfo{};
    if (Lib::GetPartInfo(h, geo, part, &pi) != HAPI_RESULT_SUCCESS) return out;
    int n = pi.attributeCounts[owner];
    if (n <= 0) return out;
    std::vector<HAPI_StringHandle> hs(static_cast<size_t>(n));
    if (Lib::GetAttributeNames(h, geo, part, owner, hs.data(), n) != HAPI_RESULT_SUCCESS) return out;
    return s.getStrings(hs);
}

bool fetchAssetOutput(HAPI_NodeId assetNode, const OutputOptions& opts, AssetOutput& out)
{
    Session& s = Session::get();
    SessionLock lock;
    const HAPI_Session* h = s.handle();
    if (!h) { s.check(HAPI_RESULT_INVALID_SESSION, "fetchAssetOutput"); return false; }
    out = AssetOutput();

    HAPI_NodeInfo ni = Lib::NodeInfo_Create ? Lib::NodeInfo_Create() : HAPI_NodeInfo{};
    if (!s.check(Lib::GetNodeInfo(h, assetNode, &ni), "HAPI_GetNodeInfo")) return false;

    std::vector<HAPI_ObjectInfo> objects;
    std::vector<HAPI_Transform> transforms;

    if (ni.type == HAPI_NODETYPE_SOP)
    {
        // SOP-level HDA: HAPI wrapped it in an OBJ; that object is the only one.
        HAPI_ObjectInfo oi = Lib::ObjectInfo_Create ? Lib::ObjectInfo_Create() : HAPI_ObjectInfo{};
        if (!s.check(Lib::GetObjectInfo(h, ni.parentId, &oi), "HAPI_GetObjectInfo")) return false;
        objects.push_back(oi);
        HAPI_Transform t = Lib::Transform_Create ? Lib::Transform_Create() : HAPI_Transform{};
        if (Lib::GetObjectTransform) Lib::GetObjectTransform(h, ni.parentId, -1, HAPI_SRT, &t);
        transforms.push_back(t);
    }
    else
    {
        // OBJ-level HDA (subnet): enumerate the geometry objects inside it.
        int count = 0;
        if (!s.check(Lib::ComposeObjectList(h, assetNode, nullptr, &count), "HAPI_ComposeObjectList")) return false;
        if (count > 0)
        {
            objects.resize(size_t(count));
            transforms.resize(size_t(count));
            if (!s.check(Lib::GetComposedObjectList(h, assetNode, objects.data(), 0, count), "HAPI_GetComposedObjectList")) return false;
            if (!s.check(Lib::GetComposedObjectTransforms(h, assetNode, HAPI_SRT, transforms.data(), 0, count), "HAPI_GetComposedObjectTransforms")) return false;
        }
        else
        {
            // A plain geometry OBJ asset: the asset node is the object.
            HAPI_ObjectInfo oi = Lib::ObjectInfo_Create ? Lib::ObjectInfo_Create() : HAPI_ObjectInfo{};
            if (!s.check(Lib::GetObjectInfo(h, assetNode, &oi), "HAPI_GetObjectInfo")) return false;
            objects.push_back(oi);
            HAPI_Transform t = Lib::Transform_Create ? Lib::Transform_Create() : HAPI_Transform{};
            if (Lib::GetObjectTransform) Lib::GetObjectTransform(h, assetNode, -1, HAPI_SRT, &t);
            transforms.push_back(t);
        }
    }

    for (size_t i = 0; i < objects.size(); ++i)
    {
        const HAPI_ObjectInfo& oi = objects[i];
        if (!oi.isVisible && !opts.includeInvisibleObjects) continue;
        ObjectOutput obj;
        obj.nodeId = oi.nodeId;
        obj.name = s.getString(oi.nameSH);
        obj.transform = fromHapi(transforms[i]);
        obj.visible = oi.isVisible;
        obj.isInstancer = oi.isInstancer;
        obj.isInstanced = oi.isInstanced;

        collectGeos(s, h, oi.nodeId, opts, obj, out.warnings);

        if (oi.isInstancer)
        {
            // Old-style object instancer: transforms from the display part's points.
            for (GeoOutput& g : obj.geos)
                for (MeshPart& m : g.meshes)
                {
                    if (m.pointCount <= 0) continue;
                    std::vector<HAPI_Transform> ts(size_t(m.pointCount));
                    if (Lib::GetInstanceTransformsOnPart(h, g.nodeId, m.id, HAPI_SRT, ts.data(), 0, m.pointCount) == HAPI_RESULT_SUCCESS)
                        for (const HAPI_Transform& t : ts) obj.instanceTransforms.push_back(fromHapi(t));
                    if (Lib::GetInstancedObjectIds)
                    {
                        obj.instancedObjectIds.resize(size_t(m.pointCount), -1);
                        if (Lib::GetInstancedObjectIds(h, oi.nodeId, obj.instancedObjectIds.data(), 0, m.pointCount) != HAPI_RESULT_SUCCESS)
                            obj.instancedObjectIds.clear();
                    }
                    break;
                }
        }
        out.objects.push_back(std::move(obj));
    }

    if (opts.readMaterials)
    {
        for (const ObjectOutput& obj : out.objects)
            for (const GeoOutput& g : obj.geos)
                for (const MeshPart& m : g.meshes)
                    for (HAPI_NodeId mat : m.faceMaterials)
                    {
                        if (mat < 0 || out.materials.count(mat)) continue;
                        MaterialOutput mo;
                        readMaterial(s, h, mat, opts.readMaterialDetails, mo);
                        out.materials[mat] = mo;
                    }
    }
    return true;
}

// ---- inputs ---------------------------------------------------------------

namespace {

bool addFloatAttr(Session& s, const HAPI_Session* h, HAPI_NodeId node, const char* name, HAPI_AttributeOwner owner,
                  int tuple, HAPI_AttributeTypeInfo typeInfo, const std::vector<float>& data)
{
    if (data.empty()) return true;
    HAPI_AttributeInfo info = Lib::AttributeInfo_Create ? Lib::AttributeInfo_Create() : HAPI_AttributeInfo{};
    info.exists = true;
    info.owner = owner;
    info.storage = HAPI_STORAGETYPE_FLOAT;
    info.originalOwner = owner;
    info.tupleSize = tuple;
    info.count = int(data.size()) / tuple;
    info.typeInfo = typeInfo;
    if (!s.check(Lib::AddAttribute(h, node, 0, name, &info), "HAPI_AddAttribute")) return false;
    return s.check(Lib::SetAttributeFloatData(h, node, 0, name, &info, data.data(), 0, info.count), "HAPI_SetAttributeFloatData");
}

bool addStringAttr(Session& s, const HAPI_Session* h, HAPI_NodeId node, const char* name, HAPI_AttributeOwner owner,
                   const std::vector<std::string>& data)
{
    if (data.empty()) return true;
    HAPI_AttributeInfo info = Lib::AttributeInfo_Create ? Lib::AttributeInfo_Create() : HAPI_AttributeInfo{};
    info.exists = true;
    info.owner = owner;
    info.storage = HAPI_STORAGETYPE_STRING;
    info.originalOwner = owner;
    info.tupleSize = 1;
    info.count = int(data.size());
    std::vector<const char*> ptrs;
    ptrs.reserve(data.size());
    for (const std::string& d : data) ptrs.push_back(d.c_str());
    if (!s.check(Lib::AddAttribute(h, node, 0, name, &info), "HAPI_AddAttribute")) return false;
    return s.check(Lib::SetAttributeStringData(h, node, 0, name, &info, ptrs.data(), 0, info.count), "HAPI_SetAttributeStringData");
}

bool addGroups(Session& s, const HAPI_Session* h, HAPI_NodeId node, HAPI_GroupType type,
               const std::vector<std::pair<std::string, std::vector<int>>>& groups)
{
    for (const auto& g : groups)
    {
        if (g.second.empty()) continue;
        if (!s.check(Lib::AddGroup(h, node, 0, type, g.first.c_str()), "HAPI_AddGroup")) return false;
        if (!s.check(Lib::SetGroupMembership(h, node, 0, type, g.first.c_str(), g.second.data(), 0, int(g.second.size())), "HAPI_SetGroupMembership")) return false;
    }
    return true;
}

} // namespace

bool createInputMesh(const InputMesh& mesh, HAPI_NodeId& outInputNode)
{
    Session& s = Session::get();
    SessionLock lock;
    const HAPI_Session* h = s.handle();
    if (!h) return false;
    outInputNode = -1;

    const int pointCount = int(mesh.P.size() / 3);
    const int vertexCount = int(mesh.vertexList.size());
    const int faceCount = int(mesh.faceCounts.size());

    if (!s.check(Lib::CreateInputNode(h, -1, &outInputNode, mesh.name.empty() ? "c4d_input" : mesh.name.c_str()), "HAPI_CreateInputNode"))
        return false;

    HAPI_PartInfo part = Lib::PartInfo_Create ? Lib::PartInfo_Create() : HAPI_PartInfo{};
    part.id = 0;
    part.type = HAPI_PARTTYPE_MESH;
    part.pointCount = pointCount;
    part.vertexCount = vertexCount;
    part.faceCount = faceCount;
    if (!s.check(Lib::SetPartInfo(h, outInputNode, 0, &part), "HAPI_SetPartInfo")) return false;

    if (!addFloatAttr(s, h, outInputNode, HAPI_ATTRIB_POSITION, HAPI_ATTROWNER_POINT, 3, HAPI_ATTRIBUTE_TYPE_POINT, mesh.P)) return false;
    if (faceCount > 0)
    {
        if (!s.check(Lib::SetVertexList(h, outInputNode, 0, mesh.vertexList.data(), 0, vertexCount), "HAPI_SetVertexList")) return false;
        if (!s.check(Lib::SetFaceCounts(h, outInputNode, 0, mesh.faceCounts.data(), 0, faceCount), "HAPI_SetFaceCounts")) return false;
    }
    if (!addFloatAttr(s, h, outInputNode, HAPI_ATTRIB_NORMAL, HAPI_ATTROWNER_VERTEX, 3, HAPI_ATTRIBUTE_TYPE_NORMAL, mesh.N)) return false;
    if (!addFloatAttr(s, h, outInputNode, HAPI_ATTRIB_UV, HAPI_ATTROWNER_VERTEX, 3, HAPI_ATTRIBUTE_TYPE_TEXTURE, mesh.uv)) return false;
    if (!addFloatAttr(s, h, outInputNode, HAPI_ATTRIB_COLOR, HAPI_ATTROWNER_VERTEX, 3, HAPI_ATTRIBUTE_TYPE_COLOR, mesh.Cd)) return false;
    if (!addFloatAttr(s, h, outInputNode, "Alpha", HAPI_ATTROWNER_VERTEX, 1, HAPI_ATTRIBUTE_TYPE_NONE, mesh.Alpha)) return false;
    if (!addStringAttr(s, h, outInputNode, "c4d_material", HAPI_ATTROWNER_PRIM, mesh.primMaterial)) return false;
    if (!addStringAttr(s, h, outInputNode, "c4d_input_name", HAPI_ATTROWNER_DETAIL, std::vector<std::string>{mesh.name})) return false;
    if (!addGroups(s, h, outInputNode, HAPI_GROUPTYPE_PRIM, mesh.primGroups)) return false;
    if (!addGroups(s, h, outInputNode, HAPI_GROUPTYPE_POINT, mesh.pointGroups)) return false;

    if (!s.check(Lib::CommitGeo(h, outInputNode), "HAPI_CommitGeo")) return false;
    return setInputNodeTransform(outInputNode, mesh.transform);
}

bool createInputCurve(const InputCurve& curve, HAPI_NodeId& outInputNode)
{
    Session& s = Session::get();
    SessionLock lock;
    const HAPI_Session* h = s.handle();
    if (!h) return false;
    outInputNode = -1;
    if (!Lib::CreateInputCurveNode || !Lib::SetInputCurveInfo || !Lib::SetInputCurvePositions)
    {
        s.check(HAPI_RESULT_FAILURE, "Input curves need Houdini 19.5+");
        return false;
    }
    if (!s.check(Lib::CreateInputCurveNode(h, -1, &outInputNode, curve.name.empty() ? "c4d_curve" : curve.name.c_str()), "HAPI_CreateInputCurveNode"))
        return false;
    HAPI_InputCurveInfo info = Lib::InputCurveInfo_Create ? Lib::InputCurveInfo_Create() : HAPI_InputCurveInfo{};
    info.curveType = curve.type;
    info.order = curve.order;
    info.closed = curve.closed;
    info.reverse = false;
    info.inputMethod = HAPI_CURVEMETHOD_CVS;
    info.breakpointParameterization = HAPI_CURVEPARAMETERIZATION_UNIFORM;
    if (!s.check(Lib::SetInputCurveInfo(h, outInputNode, 0, &info), "HAPI_SetInputCurveInfo")) return false;
    return s.check(Lib::SetInputCurvePositions(h, outInputNode, 0, curve.P.data(), 0, int(curve.P.size())), "HAPI_SetInputCurvePositions");
}

bool setInputNodeTransform(HAPI_NodeId inputNode, const Matrix4& m)
{
    Session& s = Session::get();
    SessionLock lock;
    const HAPI_Session* h = s.handle();
    if (!h || !Lib::ConvertMatrixToEuler || !Lib::SetObjectTransform) return false;

    HAPI_NodeInfo ni = Lib::NodeInfo_Create ? Lib::NodeInfo_Create() : HAPI_NodeInfo{};
    if (!s.check(Lib::GetNodeInfo(h, inputNode, &ni), "HAPI_GetNodeInfo")) return false;

    float flat[16];
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) flat[i * 4 + j] = float(m.m[i][j]);
    HAPI_TransformEuler te = Lib::TransformEuler_Create ? Lib::TransformEuler_Create() : HAPI_TransformEuler{};
    if (!s.check(Lib::ConvertMatrixToEuler(h, flat, HAPI_SRT, HAPI_XYZ, &te), "HAPI_ConvertMatrixToEuler")) return false;
    // The input SOP lives inside an OBJ created by HAPI_CreateInputNode.
    return s.check(Lib::SetObjectTransform(h, ni.parentId, &te), "HAPI_SetObjectTransform");
}

bool connectInput(HAPI_NodeId assetNode, int inputIndex, HAPI_NodeId inputNode)
{
    Session& s = Session::get();
    SessionLock lock;
    const HAPI_Session* h = s.handle();
    if (!h) return false;
    return s.check(Lib::ConnectNodeInput(h, assetNode, inputIndex, inputNode, 0), "HAPI_ConnectNodeInput");
}

bool disconnectInput(HAPI_NodeId assetNode, int inputIndex)
{
    Session& s = Session::get();
    SessionLock lock;
    const HAPI_Session* h = s.handle();
    if (!h) return false;
    return s.check(Lib::DisconnectNodeInput(h, assetNode, inputIndex), "HAPI_DisconnectNodeInput");
}

bool createMergeNode(const std::vector<HAPI_NodeId>& inputs, HAPI_NodeId& outMergeNode)
{
    Session& s = Session::get();
    SessionLock lock;
    const HAPI_Session* h = s.handle();
    if (!h || inputs.empty()) return false;
    HAPI_NodeInfo ni = Lib::NodeInfo_Create ? Lib::NodeInfo_Create() : HAPI_NodeInfo{};
    if (!s.check(Lib::GetNodeInfo(h, inputs[0], &ni), "HAPI_GetNodeInfo")) return false;
    // Create the merge next to the first input (same OBJ) so paths stay local.
    if (!s.check(Lib::CreateNode(h, ni.parentId, "merge", "c4d_merge", false, &outMergeNode), "HAPI_CreateNode(merge)")) return false;
    for (size_t i = 0; i < inputs.size(); ++i)
    {
        // Inputs in other OBJs need an object_merge; keep it simple here and
        // rely on HAPI accepting cross-object wiring for input nodes.
        if (!s.check(Lib::ConnectNodeInput(h, outMergeNode, int(i), inputs[i], 0), "HAPI_ConnectNodeInput(merge)")) return false;
    }
    return true;
}

} // namespace hapi
