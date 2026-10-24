#include "HoudiniGeoConvert.h"
#include "HoudiniSessionManager.h"

#include "lib_description.h"
#include "ttexture.h"
#include "tphong.h"
#include "oinstance.h"

#include <map>
#include <string>

namespace c4dhoudini {

using namespace hapi;

Matrix ToC4DMatrix(const Matrix4& m)
{
    Matrix r;
    r.sqmat.v1 = Vector(m.m[0][0], m.m[0][1], m.m[0][2]);
    r.sqmat.v2 = Vector(m.m[1][0], m.m[1][1], m.m[1][2]);
    r.sqmat.v3 = Vector(m.m[2][0], m.m[2][1], m.m[2][2]);
    r.off = Vector(m.m[3][0], m.m[3][1], m.m[3][2]);
    return r;
}

Matrix4 FromC4DMatrix(const Matrix& c)
{
    Matrix4 m = Matrix4::identity();
    m.m[0][0] = c.sqmat.v1.x; m.m[0][1] = c.sqmat.v1.y; m.m[0][2] = c.sqmat.v1.z;
    m.m[1][0] = c.sqmat.v2.x; m.m[1][1] = c.sqmat.v2.y; m.m[1][2] = c.sqmat.v2.z;
    m.m[2][0] = c.sqmat.v3.x; m.m[2][1] = c.sqmat.v3.y; m.m[2][2] = c.sqmat.v3.z;
    m.m[3][0] = c.off.x; m.m[3][1] = c.off.y; m.m[3][2] = c.off.z;
    return m;
}

namespace {

maxon::String S(const std::string& s) { return maxon::String(s.c_str()); }

// Attribute value for Houdini vertex `v` (index into the vertex list) and
// its point `p`, whichever owner the attribute has. Returns tuple pointer.
const float* attrAt(const AttributeData& a, int vertexIndex, int pointIndex, int primIndex)
{
    if (!a.exists() || a.floats.empty()) return nullptr;
    int idx = 0;
    switch (a.owner)
    {
        case HAPI_ATTROWNER_VERTEX: idx = vertexIndex; break;
        case HAPI_ATTROWNER_POINT: idx = pointIndex; break;
        case HAPI_ATTROWNER_PRIM: idx = primIndex; break;
        default: idx = 0; break;
    }
    if (idx < 0 || idx >= a.count) return nullptr;
    return &a.floats[size_t(idx) * size_t(a.tupleSize)];
}

// Houdini face -> one or more Cinema polygons (fan triangulation for >4).
struct FaceMap
{
    Int32 polyCount = 0;
    // For each Cinema polygon: source face index and the 3/4 Houdini vertex
    // indices (into the vertex list) it uses, d==c for triangles.
    maxon::BaseArray<Int32> face;
    maxon::BaseArray<Int32> v[4];
};

Bool buildFaceMap(const MeshPart& m, const geo::ConvertOptions& conv, FaceMap& fm)
{
    std::vector<int> vl = m.vertexList;
    if (conv.reverseWinding) geo::reverseWinding(vl, m.faceCounts);
    Int32 cursor = 0;
    for (size_t f = 0; f < m.faceCounts.size(); ++f)
    {
        const int n = m.faceCounts[f];
        if (n >= 3)
        {
            if (n <= 4)
            {
                if (fm.face.Append(Int32(f)) == maxon::FAILED) return false;
                for (int k = 0; k < 4; ++k)
                    if (fm.v[k].Append(cursor + (k < n ? k : n - 1)) == maxon::FAILED) return false;
                ++fm.polyCount;
            }
            else
            {
                for (int k = 1; k + 1 < n; ++k)
                {
                    if (fm.face.Append(Int32(f)) == maxon::FAILED) return false;
                    if (fm.v[0].Append(cursor) == maxon::FAILED) return false;
                    if (fm.v[1].Append(cursor + k) == maxon::FAILED) return false;
                    if (fm.v[2].Append(cursor + k + 1) == maxon::FAILED) return false;
                    if (fm.v[3].Append(cursor + k + 1) == maxon::FAILED) return false;
                    ++fm.polyCount;
                }
            }
        }
        cursor += n;
    }
    return true;
}

PolygonObject* buildPolygon(const MeshPart& m, const BuildOptions& o, const std::map<HAPI_NodeId, MaterialOutput>& materials)
{
    FaceMap fm;
    if (!buildFaceMap(m, o.conv, fm)) return nullptr;

    PolygonObject* poly = PolygonObject::Alloc(m.pointCount, fm.polyCount);
    if (!poly) return nullptr;
    poly->SetName(S(m.outputName.exists() ? m.outputName.firstString() : (m.name.empty() ? "mesh" : m.name)));

    Vector* pts = poly->GetPointW();
    for (int i = 0; i < m.pointCount; ++i)
    {
        double p[3];
        geo::toHostPoint(&m.P[size_t(i) * 3], o.conv, p);
        pts[i] = Vector(p[0], p[1], p[2]);
    }

    std::vector<int> vl = m.vertexList;
    if (o.conv.reverseWinding) geo::reverseWinding(vl, m.faceCounts);
    CPolygon* polys = poly->GetPolygonW();
    for (Int32 i = 0; i < fm.polyCount; ++i)
        polys[i] = CPolygon(vl[size_t(fm.v[0][i])], vl[size_t(fm.v[1][i])], vl[size_t(fm.v[2][i])], vl[size_t(fm.v[3][i])]);

    // UVs: Houdini v runs bottom-up, Cinema's top-down.
    if (o.uvs && m.uv.exists() && m.uv.tupleSize >= 2)
    {
        UVWTag* tag = UVWTag::Alloc(fm.polyCount);
        if (tag)
        {
            UVWHandle h = tag->GetDataAddressW();
            for (Int32 i = 0; i < fm.polyCount; ++i)
            {
                Vector c[4];
                for (int k = 0; k < 4; ++k)
                {
                    int vi = fm.v[k][i];
                    const float* t = attrAt(m.uv, vi, vl[size_t(vi)], fm.face[i]);
                    c[k] = t ? Vector(t[0], 1.0 - t[1], 0.0) : Vector();
                }
                UVWTag::Set(h, i, UVWStruct(c[0], c[1], c[2], c[3]));
            }
            poly->InsertTag(tag);
        }
    }

    if (o.normals && m.N.exists() && m.N.tupleSize >= 3)
    {
        NormalTag* tag = NormalTag::Alloc(fm.polyCount);
        if (tag)
        {
            NormalHandle h = tag->GetDataAddressW();
            for (Int32 i = 0; i < fm.polyCount; ++i)
            {
                Vector n[4];
                for (int k = 0; k < 4; ++k)
                {
                    int vi = fm.v[k][i];
                    const float* t = attrAt(m.N, vi, vl[size_t(vi)], fm.face[i]);
                    double d[3] = {0, 1, 0};
                    if (t) geo::toHostVector(t, o.conv, d);
                    n[k] = Vector(d[0], d[1], d[2]);
                }
                NormalTag::Set(h, i, NormalStruct(n[0], n[1], n[2], n[3]));
            }
            poly->InsertTag(tag);
        }
    }
    else if (o.phongAngle > 0.0)
    {
        BaseTag* phong = poly->MakeTag(Tphong);
        if (phong)
        {
            phong->SetParameter(DescID(PHONGTAG_PHONG_ANGLELIMIT), GeData(true), DESCFLAGS_SET::NONE);
            phong->SetParameter(DescID(PHONGTAG_PHONG_ANGLE), GeData(DegToRad(o.phongAngle)), DESCFLAGS_SET::NONE);
        }
    }

    if (o.colors && m.Cd.exists() && m.Cd.tupleSize >= 3)
    {
        const Bool perPoint = m.Cd.owner == HAPI_ATTROWNER_POINT;
        VertexColorTag* tag = VertexColorTag::Alloc(perPoint ? m.pointCount : fm.polyCount);
        if (tag)
        {
            tag->SetPerPointMode(perPoint);
            VertexColorHandle h = tag->GetDataAddressW();
            auto alphaAt = [&](int vi, int pi, int fi) -> Float32 {
                const float* a = attrAt(m.Alpha, vi, pi, fi);
                return a ? a[0] : 1.0f;
            };
            if (perPoint)
            {
                for (int i = 0; i < m.pointCount; ++i)
                {
                    const float* c = &m.Cd.floats[size_t(i) * size_t(m.Cd.tupleSize)];
                    VertexColorTag::SetPoint(h, nullptr, nullptr, i, maxon::ColorA32(c[0], c[1], c[2], alphaAt(-1, i, 0)));
                }
            }
            else
            {
                for (Int32 i = 0; i < fm.polyCount; ++i)
                {
                    VertexColorStruct vc;
                    maxon::ColorA32* slots[4] = {&vc.a, &vc.b, &vc.c, &vc.d};
                    for (int k = 0; k < 4; ++k)
                    {
                        int vi = fm.v[k][i];
                        const float* c = attrAt(m.Cd, vi, vl[size_t(vi)], fm.face[i]);
                        *slots[k] = c ? maxon::ColorA32(c[0], c[1], c[2], alphaAt(vi, vl[size_t(vi)], fm.face[i])) : maxon::ColorA32(1, 1, 1, 1);
                    }
                    VertexColorTag::SetPolygon(h, i, vc);
                }
            }
            poly->InsertTag(tag);
        }
    }

    if (o.groups)
    {
        for (const GroupData& g : m.primGroups)
        {
            SelectionTag* st = SelectionTag::Alloc(Tpolygonselection);
            if (!st) continue;
            st->SetName(S(g.name));
            BaseSelect* bs = st->GetBaseSelect();
            for (Int32 i = 0; i < fm.polyCount; ++i)
                if (g.membership[size_t(fm.face[i])]) bs->Select(i);
            poly->InsertTag(st);
        }
        for (const GroupData& g : m.pointGroups)
        {
            SelectionTag* st = SelectionTag::Alloc(Tpointselection);
            if (!st) continue;
            st->SetName(S(g.name));
            BaseSelect* bs = st->GetBaseSelect();
            for (int i = 0; i < m.pointCount; ++i)
                if (g.membership[size_t(i)]) bs->Select(i);
            poly->InsertTag(st);
        }
    }

    if (o.materials)
    {
        // One polygon selection per Houdini material, plus a texture tag when
        // a Cinema 4D material of the same name exists in the document. The
        // c4d_material attribute (per prim or detail) takes precedence.
        std::map<std::string, std::vector<Int32>> byName;
        for (Int32 i = 0; i < fm.polyCount; ++i)
        {
            const int f = fm.face[i];
            std::string name;
            if (m.hostMaterial.exists())
            {
                if (m.hostMaterial.owner == HAPI_ATTROWNER_PRIM && f < int(m.hostMaterial.strings.size()))
                    name = m.hostMaterial.strings[size_t(f)];
                else
                    name = m.hostMaterial.firstString();
            }
            if (name.empty() && f < int(m.faceMaterials.size()) && m.faceMaterials[size_t(f)] >= 0)
            {
                auto it = materials.find(m.faceMaterials[size_t(f)]);
                if (it != materials.end()) name = it->second.name;
            }
            if (!name.empty()) byName[name].push_back(i);
        }
        for (auto& kv : byName)
        {
            SelectionTag* st = SelectionTag::Alloc(Tpolygonselection);
            if (!st) continue;
            const maxon::String selName = "mat_"_s + S(kv.first);
            st->SetName(selName);
            BaseSelect* bs = st->GetBaseSelect();
            for (Int32 i : kv.second) bs->Select(i);
            poly->InsertTag(st);

            if (o.doc)
            {
                // Accept both the bare node name and a full path's last component.
                std::string leaf = kv.first;
                size_t slash = leaf.find_last_of('/');
                if (slash != std::string::npos) leaf = leaf.substr(slash + 1);
                BaseMaterial* mat = o.doc->SearchMaterial(S(leaf));
                if (mat)
                {
                    TextureTag* tt = TextureTag::Alloc();
                    if (tt)
                    {
                        tt->SetMaterial(mat);
                        tt->SetParameter(DescID(TEXTURETAG_RESTRICTION), GeData(selName), DESCFLAGS_SET::NONE);
                        tt->SetParameter(DescID(TEXTURETAG_PROJECTION), GeData(TEXTURETAG_PROJECTION_UVW), DESCFLAGS_SET::NONE);
                        poly->InsertTag(tt);
                    }
                }
            }
        }
    }

    poly->Message(MSG_UPDATE);
    return poly;
}

SplineObject* buildSpline(const CurvePart& c, const BuildOptions& o)
{
    const Int32 total = Int32(c.P.size() / 3);
    if (total == 0 || c.curveCount == 0) return nullptr;
    // NURBS -> B-Spline (approximation; knots are ignored), Bezier -> Bezier
    // with CVs as points, linear -> linear.
    SPLINETYPE type = SPLINETYPE::LINEAR;
    if (c.type == HAPI_CURVETYPE_NURBS) type = SPLINETYPE::BSPLINE;
    else if (c.type == HAPI_CURVETYPE_BEZIER) type = SPLINETYPE::BEZIER;

    SplineObject* sp = SplineObject::Alloc(total, type);
    if (!sp) return nullptr;
    if (!sp->ResizeObject(total, c.curveCount)) { SplineObject::Free(sp); return nullptr; }
    sp->SetName(S(c.name.empty() ? "curve" : c.name));

    Vector* pts = sp->GetPointW();
    for (Int32 i = 0; i < total; ++i)
    {
        double p[3];
        geo::toHostPoint(&c.P[size_t(i) * 3], o.conv, p);
        pts[i] = Vector(p[0], p[1], p[2]);
    }
    Segment* seg = sp->GetSegmentW();
    for (int i = 0; i < c.curveCount; ++i)
    {
        seg[i].cnt = c.vertexCounts[size_t(i)];
        seg[i].closed = c.closed || c.periodic;
    }
    sp->SetParameter(DescID(SPLINEOBJECT_CLOSED), GeData(c.closed || c.periodic), DESCFLAGS_SET::NONE);
    sp->Message(MSG_UPDATE);
    return sp;
}

BaseObject* buildInstancer(const InstancerPart& ip, const std::map<HAPI_PartId, BaseObject*>& sources,
                           const BuildOptions& o)
{
    BaseObject* holder = BaseObject::Alloc(Onull);
    if (!holder) return nullptr;
    holder->SetName(S(ip.name.empty() ? "instances" : ip.name));

    maxon::BaseArray<Matrix> mats;
    for (const Transform& t : ip.transforms)
        mats.Append(ToC4DMatrix(geo::toHostMatrix(geo::toMatrix(t), o.conv))) iferr_ignore("instance matrices");

    for (HAPI_PartId pid : ip.instancedPartIds)
    {
        auto it = sources.find(pid);
        if (it == sources.end() || !it->second) continue;
        BaseObject* src = it->second;

        if (o.instanceMode == 0)
        {
            // Multi-instance: one object, N matrices. Fastest and MoGraph-friendly.
            InstanceObject* io = InstanceObject::Alloc();
            if (!io) continue;
            io->SetReferenceObject(src);
            io->SetInstanceMatrices(mats) iferr_ignore("SetInstanceMatrices");
            io->SetParameter(DescID(INSTANCEOBJECT_RENDERINSTANCE_MODE), GeData(INSTANCEOBJECT_RENDERINSTANCE_MODE_MULTIINSTANCE), DESCFLAGS_SET::NONE);
            io->SetName(src->GetName() + "_instances"_s);
            io->InsertUnderLast(holder);
        }
        else if (o.instanceMode == 1)
        {
            for (const Matrix& m : mats)
            {
                InstanceObject* io = InstanceObject::Alloc();
                if (!io) continue;
                io->SetReferenceObject(src);
                io->SetParameter(DescID(INSTANCEOBJECT_RENDERINSTANCE_MODE), GeData(INSTANCEOBJECT_RENDERINSTANCE_MODE_SINGLEINSTANCE), DESCFLAGS_SET::NONE);
                io->SetMl(m);
                io->InsertUnderLast(holder);
            }
        }
        else
        {
            for (const Matrix& m : mats)
            {
                BaseObject* copy = static_cast<BaseObject*>(src->GetClone(COPYFLAGS::NONE, nullptr));
                if (!copy) continue;
                copy->SetMl(m);
                copy->InsertUnderLast(holder);
            }
        }
    }
    return holder;
}

} // namespace

BaseObject* BuildOutputHierarchy(const AssetOutput& out, const BuildOptions& opts, const maxon::String& rootName)
{
    BaseObject* root = BaseObject::Alloc(Onull);
    if (!root) return nullptr;
    root->SetName(rootName);

    for (const ObjectOutput& obj : out.objects)
    {
        BaseObject* objNull = BaseObject::Alloc(Onull);
        if (!objNull) continue;
        objNull->SetName(S(obj.name));
        objNull->SetMl(ToC4DMatrix(geo::toHostMatrix(geo::toMatrix(obj.transform), opts.conv)));
        objNull->InsertUnderLast(root);

        for (const GeoOutput& g : obj.geos)
        {
            // Build every mesh part first so instancers can reference them.
            std::map<HAPI_PartId, BaseObject*> built;
            BaseObject* sourcesNull = nullptr;
            for (const MeshPart& m : g.meshes)
            {
                PolygonObject* poly = buildPolygon(m, opts, out.materials);
                if (!poly) continue;
                built[m.id] = poly;
                if (m.isInstanced)
                {
                    // Instanced sources live under a hidden null (like the
                    // 3ds Max plugin hides its instance sources).
                    if (!sourcesNull)
                    {
                        sourcesNull = BaseObject::Alloc(Onull);
                        sourcesNull->SetName("instance_sources"_s);
                        sourcesNull->SetEditorMode(MODE_OFF);
                        sourcesNull->SetRenderMode(MODE_OFF);
                        sourcesNull->InsertUnderLast(objNull);
                    }
                    poly->InsertUnderLast(sourcesNull);
                }
                else
                    poly->InsertUnderLast(objNull);
            }
            for (const CurvePart& c : g.curves)
            {
                SplineObject* sp = buildSpline(c, opts);
                if (sp) sp->InsertUnderLast(objNull);
            }
            for (const InstancerPart& ip : g.instancers)
            {
                BaseObject* inst = buildInstancer(ip, built, opts);
                if (inst) inst->InsertUnderLast(objNull);
            }
            for (const VolumePart& v : g.volumes)
            {
                // Placeholder so the user sees the volume exists.
                BaseObject* n = BaseObject::Alloc(Onull);
                if (!n) continue;
                n->SetName("volume_"_s + S(v.name) + " (not imported)"_s);
                n->SetMl(ToC4DMatrix(geo::toHostMatrix(geo::toMatrix(v.transform), opts.conv)));
                n->InsertUnderLast(objNull);
            }
        }

        // Old-style object instancer: duplicate the object's own geometry
        // per instance transform (packed instancers are the modern path).
        if (obj.isInstancer && !obj.instanceTransforms.empty())
        {
            BaseObject* holder = BaseObject::Alloc(Onull);
            holder->SetName("object_instances"_s);
            holder->InsertUnderLast(objNull);
            for (const Transform& t : obj.instanceTransforms)
            {
                BaseObject* n = BaseObject::Alloc(Onull);
                if (!n) continue;
                n->SetMl(ToC4DMatrix(geo::toHostMatrix(geo::toMatrix(t), opts.conv)));
                n->InsertUnderLast(holder);
            }
        }
    }
    return root;
}

namespace {

void collectOne(BaseObject* obj, const Matrix& rel, const geo::ConvertOptions& conv,
                maxon::BaseArray<InputMesh>& meshes, maxon::BaseArray<InputCurve>& curves)
{
    {
        const Matrix mg = rel * obj->GetMg();
        if (obj->IsInstanceOf(Opolygon))
        {
            PolygonObject* poly = static_cast<PolygonObject*>(obj);
            InputMesh in;
            in.name = ToStd(obj->GetName());
            const Int32 pc = poly->GetPointCount(), fc = poly->GetPolygonCount();
            const Vector* pts = poly->GetPointR();
            const CPolygon* polys = poly->GetPolygonR();
            in.P.resize(size_t(pc) * 3);
            for (Int32 i = 0; i < pc; ++i)
            {
                double p[3] = {pts[i].x, pts[i].y, pts[i].z};
                geo::toHoudiniPoint(p, conv, &in.P[size_t(i) * 3]);
            }
            UVWTag* uvw = static_cast<UVWTag*>(poly->GetTag(Tuvw));
            ConstUVWHandle uvh = uvw ? uvw->GetDataAddressR() : nullptr;
            in.faceCounts.reserve(size_t(fc));
            for (Int32 i = 0; i < fc; ++i)
            {
                const CPolygon& p = polys[i];
                const Bool tri = p.c == p.d;
                const Int32 idx[4] = {p.a, p.b, p.c, p.d};
                const int n = tri ? 3 : 4;
                in.faceCounts.push_back(n);
                UVWStruct uv;
                if (uvh) UVWTag::Get(uvh, i, uv);
                const Vector uvv[4] = {uv.a, uv.b, uv.c, uv.d};
                for (int k = 0; k < n; ++k)
                {
                    in.vertexList.push_back(idx[k]);
                    if (uvh)
                    {
                        in.uv.push_back(float(uvv[k].x));
                        in.uv.push_back(float(1.0 - uvv[k].y));
                        in.uv.push_back(0.0f);
                    }
                }
            }
            if (conv.reverseWinding) geo::reverseWinding(in.vertexList, in.faceCounts);
            if (conv.reverseWinding && !in.uv.empty()) geo::reverseWinding(in.uv, in.faceCounts, 3);

            // Polygon selections -> prim groups; texture tag restrictions -> c4d_material.
            for (BaseTag* tag = poly->GetFirstTag(); tag; tag = tag->GetNext())
            {
                if (tag->IsInstanceOf(Tpolygonselection))
                {
                    BaseSelect* bs = static_cast<SelectionTag*>(tag)->GetBaseSelect();
                    std::vector<int> mem(size_t(fc), 0);
                    for (Int32 i = 0; i < fc; ++i) mem[size_t(i)] = bs->IsSelected(i) ? 1 : 0;
                    in.primGroups.emplace_back(ToStd(tag->GetName()), mem);
                }
                else if (tag->IsInstanceOf(Ttexture))
                {
                    TextureTag* tt = static_cast<TextureTag*>(tag);
                    BaseMaterial* mat = tt->GetMaterial();
                    if (!mat) continue;
                    GeData d;
                    tt->GetParameter(DescID(TEXTURETAG_RESTRICTION), d, DESCFLAGS_GET::NONE);
                    const maxon::String restriction = d.GetString();
                    if (in.primMaterial.empty()) in.primMaterial.assign(size_t(fc), std::string());
                    if (restriction.IsEmpty())
                    {
                        for (std::string& s : in.primMaterial) if (s.empty()) s = ToStd(mat->GetName());
                    }
                    else
                    {
                        for (auto& g : in.primGroups)
                            if (g.first == ToStd(restriction))
                                for (Int32 i = 0; i < fc; ++i) if (g.second[size_t(i)]) in.primMaterial[size_t(i)] = ToStd(mat->GetName());
                    }
                }
            }
            in.transform = geo::toHoudiniMatrix(FromC4DMatrix(mg), conv);
            meshes.Append(std::move(in)) iferr_ignore("input mesh");
        }
        else if (obj->IsInstanceOf(Ospline))
        {
            SplineObject* sp = static_cast<SplineObject*>(obj);
            const Int32 pc = sp->GetPointCount();
            const Vector* pts = sp->GetPointR();
            InputCurve c;
            c.name = ToStd(obj->GetName());
            c.closed = sp->IsClosed();
            c.P.resize(size_t(pc) * 3);
            for (Int32 i = 0; i < pc; ++i)
            {
                const Vector w = mg * pts[i];
                double p[3] = {w.x, w.y, w.z};
                geo::toHoudiniPoint(p, conv, &c.P[size_t(i) * 3]);
            }
            curves.Append(std::move(c)) iferr_ignore("input curve");
        }
    }
    for (BaseObject* child = obj->GetDown(); child; child = child->GetNext())
        collectOne(child, rel, conv, meshes, curves);
}

} // namespace

void CollectInputGeometry(BaseObject* root, const Matrix& rel, const geo::ConvertOptions& conv,
                          maxon::BaseArray<InputMesh>& meshes, maxon::BaseArray<InputCurve>& curves)
{
    if (root) collectOne(root, rel, conv, meshes, curves);
}

} // namespace c4dhoudini
