// hapi_probe: exercise the core library from the command line on a machine
// that has Houdini installed. Useful for debugging an HDA without launching
// Cinema 4D, and for verifying coordinate/winding conventions by writing
// the cooked output as an OBJ in Cinema 4D space.
//
//   hapi_probe [options] asset.hda
//     --lib <path>          explicit libHAPIL (default: search HFS, registry, install dirs)
//     --session pipe|socket|shm|inprocess   (default pipe)
//     --connect             do not spawn HARS, connect to an existing server / Houdini
//     --port N              socket port (default 9090)
//     --log <file>          HARS log file
//     --asset <opname>      which asset in the library (default: first)
//     --parm name=value     set a parameter before cooking (repeatable; "1,2,3" for tuples)
//     --frame N --fps F     set Houdini time before cooking
//     --input <mesh.obj>    send an OBJ file to input 0
//     --obj <out.obj>       write cooked meshes (converted to C4D space) to an OBJ
//     --no-flip --scale S   conversion options for --obj / --input
//     --reverse-winding     also reverse polygon vertex order (normally not needed)
//     --attrs a,b,c         extra attributes to read
//     --hip <file.hip>      save the session as a HIP file for inspection in Houdini
//     --quiet               less output
#include "GeoMath.h"
#include "HapiAsset.h"
#include "HapiGeometry.h"
#include "HapiParms.h"
#include "HapiSession.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace hapi;

namespace {

struct Args
{
    std::string lib, hda, asset, log, inputObj, outObj, hip, attrs;
    SessionType session = SessionType::NamedPipe;
    bool connect = false, quiet = false, flip = true, reverse = false;
    int port = 9090;
    double scale = 100.0;
    double frame = -1, fps = 24;
    std::vector<std::pair<std::string, std::string>> parms;
};

bool parseArgs(int argc, char** argv, Args& a)
{
    for (int i = 1; i < argc; ++i)
    {
        std::string s = argv[i];
        auto next = [&](std::string& dst) { if (i + 1 >= argc) return false; dst = argv[++i]; return true; };
        std::string v;
        if (s == "--lib" && next(v)) a.lib = v;
        else if (s == "--session" && next(v))
        {
            if (v == "pipe") a.session = SessionType::NamedPipe;
            else if (v == "socket") a.session = SessionType::Socket;
            else if (v == "shm") a.session = SessionType::SharedMemory;
            else if (v == "inprocess") a.session = SessionType::InProcess;
            else { std::fprintf(stderr, "unknown session type %s\n", v.c_str()); return false; }
        }
        else if (s == "--connect") a.connect = true;
        else if (s == "--port" && next(v)) a.port = std::atoi(v.c_str());
        else if (s == "--log" && next(v)) a.log = v;
        else if (s == "--asset" && next(v)) a.asset = v;
        else if (s == "--parm" && next(v))
        {
            size_t eq = v.find('=');
            if (eq == std::string::npos) { std::fprintf(stderr, "--parm expects name=value\n"); return false; }
            a.parms.emplace_back(v.substr(0, eq), v.substr(eq + 1));
        }
        else if (s == "--frame" && next(v)) a.frame = std::atof(v.c_str());
        else if (s == "--fps" && next(v)) a.fps = std::atof(v.c_str());
        else if (s == "--input" && next(v)) a.inputObj = v;
        else if (s == "--obj" && next(v)) a.outObj = v;
        else if (s == "--hip" && next(v)) a.hip = v;
        else if (s == "--attrs" && next(v)) a.attrs = v;
        else if (s == "--no-flip") a.flip = false;
        else if (s == "--reverse-winding") a.reverse = true;
        else if (s == "--scale" && next(v)) a.scale = std::atof(v.c_str());
        else if (s == "--quiet") a.quiet = true;
        else if (!s.empty() && s[0] == '-') { std::fprintf(stderr, "unknown option %s\n", s.c_str()); return false; }
        else a.hda = s;
    }
    return !a.hda.empty();
}

void printParmTree(const ParmSet& ps, int idx, int depth)
{
    const Parm& p = ps.parms[size_t(idx)];
    std::string indent(size_t(depth) * 2, ' ');
    std::printf("%s%-16s %-28s \"%s\"", indent.c_str(), parmTypeName(p.type), p.name.c_str(), p.label.c_str());
    if (p.size > 1) std::printf(" [%d]", p.size);
    if (p.invisible) std::printf(" (hidden)");
    if (p.disabled) std::printf(" (disabled)");
    if (p.isInt() && !p.isButton()) { std::printf(" ="); for (int v : p.intValues) std::printf(" %d", v); }
    if (p.isFloat()) { std::printf(" ="); for (float v : p.floatValues) std::printf(" %g", v); }
    if (p.isString()) { std::printf(" ="); for (const std::string& v : p.stringValues) std::printf(" \"%s\"", v.c_str()); }
    if (p.hasMenu())
    {
        std::printf("  menu{");
        for (size_t i = 0; i < p.choices.size(); ++i) std::printf("%s%s", i ? "," : "", p.choices[i].value.c_str());
        std::printf("}");
    }
    if (p.isMultiparm()) std::printf("  instances=%d", p.instanceCount);
    if (p.isRamp()) std::printf("  ramp=%s", p.rampType == HAPI_RAMPTYPE_COLOR ? "color" : "float");
    if (p.hasUIMin || p.hasUIMax) std::printf("  ui[%g..%g]", p.uiMin, p.uiMax);
    std::printf("\n");
    for (int c : p.children) printParmTree(ps, c, depth + 1);
}

// Minimal OBJ reader (v / f, n-gons, 1-based, ignores everything else).
bool readObj(const std::string& path, InputMesh& mesh, const geo::ConvertOptions& conv)
{
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    std::vector<double> pts;
    while (std::getline(in, line))
    {
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "v")
        {
            double x, y, z; ss >> x >> y >> z;
            pts.push_back(x); pts.push_back(y); pts.push_back(z);
        }
        else if (tag == "f")
        {
            std::string tok; int n = 0;
            while (ss >> tok)
            {
                int idx = std::atoi(tok.c_str());
                if (idx < 0) idx = int(pts.size() / 3) + idx + 1;
                mesh.vertexList.push_back(idx - 1);
                ++n;
            }
            mesh.faceCounts.push_back(n);
        }
    }
    // The OBJ is assumed to be in Cinema 4D space; convert like the plugin does.
    mesh.P.resize(pts.size());
    for (size_t i = 0; i < pts.size(); i += 3)
    {
        float out[3];
        geo::toHoudiniPoint(&pts[i], conv, out);
        mesh.P[i] = out[0]; mesh.P[i + 1] = out[1]; mesh.P[i + 2] = out[2];
    }
    if (conv.reverseWinding) geo::reverseWinding(mesh.vertexList, mesh.faceCounts);
    mesh.name = "probe_input";
    return true;
}

void writeObj(const std::string& path, const AssetOutput& out, const geo::ConvertOptions& conv)
{
    std::ofstream o(path);
    o << "# written by hapi_probe, Cinema 4D space (scale " << conv.scale << (conv.flipZ ? ", Z flipped" : "") << ")\n";
    int base = 1;
    for (const ObjectOutput& obj : out.objects)
    {
        Matrix4 objM = geo::toHostMatrix(geo::toMatrix(obj.transform), conv);
        for (const GeoOutput& g : obj.geos)
            for (const MeshPart& m : g.meshes)
            {
                if (m.isInstanced) continue;
                o << "o " << obj.name << "_" << g.name << "_" << (m.name.empty() ? std::to_string(m.id) : m.name) << "\n";
                for (int i = 0; i < m.pointCount; ++i)
                {
                    double p[3], q[3];
                    geo::toHostPoint(&m.P[size_t(i) * 3], conv, p);
                    objM.transformPoint(p, q);
                    o << "v " << q[0] << " " << q[1] << " " << q[2] << "\n";
                }
                std::vector<int> vl = m.vertexList;
                if (conv.reverseWinding) geo::reverseWinding(vl, m.faceCounts);
                size_t cursor = 0;
                for (int fc : m.faceCounts)
                {
                    o << "f";
                    for (int k = 0; k < fc; ++k) o << " " << (base + vl[cursor + size_t(k)]);
                    o << "\n";
                    cursor += size_t(fc);
                }
                base += m.pointCount;
            }
    }
}

} // namespace

int main(int argc, char** argv)
{
    Args a;
    if (!parseArgs(argc, argv, a))
    {
        std::fprintf(stderr, "usage: hapi_probe [options] asset.hda   (see source for options)\n");
        return 2;
    }

    std::string err;
    if (!Lib::load(a.lib, err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    const LibraryInfo& li = Lib::info();
    std::printf("libHAPIL : %s\n", li.libraryPath.c_str());
    std::printf("version  : %s\n", li.versionString().c_str());
    std::printf("HFS      : %s\n", li.hfs.c_str());
    std::printf("HARS     : %s\n", li.harsPath.empty() ? "(not found)" : li.harsPath.c_str());
    if (!Lib::missingSymbols().empty())
        std::printf("note     : %zu HAPI symbols from the headers are missing in this library\n", Lib::missingSymbols().size());

    SessionOptions so;
    so.type = a.session;
    so.autoStartServer = !a.connect;
    so.port = a.port;
    so.serverLogFile = a.log;
    auto t0 = std::chrono::steady_clock::now();
    Session& s = Session::get();
    if (!s.start(so, err)) { std::fprintf(stderr, "session start failed:\n%s\n", err.c_str()); return 1; }
    auto t1 = std::chrono::steady_clock::now();
    std::printf("session  : up in %.0f ms (HARS pid %d)\n",
                std::chrono::duration<double, std::milli>(t1 - t0).count(), int(s.serverProcessId()));

    AssetLibrary lib;
    if (!loadAssetLibrary(a.hda, lib)) { std::fprintf(stderr, "%s\n", s.lastError().c_str()); return 1; }
    std::printf("library  : %s (%zu assets)\n", lib.path.c_str(), lib.assetNames.size());
    for (const std::string& n : lib.assetNames) std::printf("           - %s\n", n.c_str());
    if (lib.assetNames.empty()) return 1;
    std::string opName = a.asset.empty() ? lib.assetNames[0] : a.asset;

    HAPI_NodeId node = -1;
    if (!instantiateAsset(opName, "probe", node)) { std::fprintf(stderr, "%s\n", s.lastError().c_str()); return 1; }
    AssetInfo ai;
    getAssetInfo(node, ai);
    std::printf("asset    : %s (node %d, %s level, %d geo inputs%s)\n", ai.label.c_str(), node,
                ai.nodeType == HAPI_NODETYPE_SOP ? "SOP" : "OBJ", ai.geoInputCount,
                ai.isTimeDependent ? ", time dependent" : "");
    for (const AssetInput& in : ai.inputs) std::printf("           input %d: %s\n", in.index, in.name.c_str());

    ParmSet parms;
    if (!fetchParms(node, parms)) { std::fprintf(stderr, "%s\n", s.lastError().c_str()); return 1; }
    if (!a.quiet)
    {
        std::printf("parms    : %zu\n", parms.parms.size());
        for (int r : parms.roots) printParmTree(parms, r, 1);
    }
    for (const auto& kv : a.parms)
    {
        if (!setParmFromString(node, parms, kv.first, kv.second))
            std::fprintf(stderr, "warning: could not set %s: %s\n", kv.first.c_str(), s.lastError().c_str());
        else
            std::printf("set      : %s = %s\n", kv.first.c_str(), kv.second.c_str());
    }

    geo::ConvertOptions conv;
    conv.scale = a.scale;
    conv.flipZ = a.flip;
    conv.reverseWinding = a.reverse;

    if (!a.inputObj.empty())
    {
        InputMesh mesh;
        if (!readObj(a.inputObj, mesh, conv)) { std::fprintf(stderr, "cannot read %s\n", a.inputObj.c_str()); return 1; }
        HAPI_NodeId inNode = -1;
        if (!createInputMesh(mesh, inNode) || !connectInput(node, 0, inNode))
        {
            std::fprintf(stderr, "input failed: %s\n", s.lastError().c_str());
            return 1;
        }
        std::printf("input    : %s -> %zu points, %zu faces (node %d)\n", a.inputObj.c_str(), mesh.P.size() / 3, mesh.faceCounts.size(), inNode);
    }

    if (a.frame >= 0)
    {
        s.setTimeline(a.fps, 0.0, 1000.0 / a.fps);
        s.setTime(a.frame / a.fps);
        std::printf("time     : frame %g @ %g fps\n", a.frame, a.fps);
    }

    t0 = std::chrono::steady_clock::now();
    CookResult cr = s.cook(node, nullptr, [&](int cur, int total) {
        if (!a.quiet && total > 0) { std::printf("\r  cooking %d/%d", cur, total); std::fflush(stdout); }
    });
    t1 = std::chrono::steady_clock::now();
    if (!a.quiet) std::printf("\r");
    std::printf("cook     : %s in %.0f ms%s\n", cr.succeeded ? "ok" : "FAILED",
                std::chrono::duration<double, std::milli>(t1 - t0).count(),
                cr.hadCookErrors ? " (with cook errors)" : "");
    if (!cr.message.empty()) std::printf("%s\n", cr.message.c_str());
    if (!cr.succeeded) return 1;

    // Parameters can change after a cook (menus, disabled state, callbacks).
    fetchParms(node, parms);

    OutputOptions oo;
    if (!a.attrs.empty())
    {
        std::stringstream ss(a.attrs);
        std::string item;
        while (std::getline(ss, item, ',')) oo.extraAttributes.push_back(item);
    }
    AssetOutput out;
    t0 = std::chrono::steady_clock::now();
    if (!fetchAssetOutput(node, oo, out)) { std::fprintf(stderr, "%s\n", s.lastError().c_str()); return 1; }
    t1 = std::chrono::steady_clock::now();
    std::printf("output   : fetched in %.0f ms: %zu objects, %zu meshes, %zu instancers, %zu curves, %zu materials\n",
                std::chrono::duration<double, std::milli>(t1 - t0).count(),
                out.objects.size(), out.totalMeshes(), out.totalInstancers(), out.totalCurves(), out.materials.size());
    for (const std::string& w : out.warnings) std::printf("  warning: %s\n", w.c_str());
    for (const ObjectOutput& obj : out.objects)
    {
        std::printf("  object '%s' (node %d)%s%s  t=(%g %g %g)\n", obj.name.c_str(), obj.nodeId,
                    obj.visible ? "" : " hidden", obj.isInstancer ? " instancer" : "",
                    obj.transform.position[0], obj.transform.position[1], obj.transform.position[2]);
        for (const GeoOutput& g : obj.geos)
        {
            std::printf("    geo '%s' (node %d)%s%s\n", g.name.c_str(), g.nodeId, g.isDisplay ? " display" : "", g.isTemplated ? " templated" : "");
            for (const MeshPart& m : g.meshes)
            {
                std::printf("      mesh part %d '%s': %d pts, %d verts, %d faces%s", m.id, m.name.c_str(), m.pointCount, m.vertexCount, m.faceCount,
                            m.isInstanced ? " (instanced)" : "");
                if (m.N.exists()) std::printf(", N(%s)", m.N.owner == HAPI_ATTROWNER_VERTEX ? "vtx" : "pt");
                if (m.uv.exists()) std::printf(", uv(%s)", m.uv.owner == HAPI_ATTROWNER_VERTEX ? "vtx" : "pt");
                if (m.Cd.exists()) std::printf(", Cd");
                if (!m.faceMaterials.empty()) std::printf(", materials%s", m.faceMaterialsAllSame ? "(uniform)" : "(per face)");
                if (m.hostMaterial.exists()) std::printf(", %s=\"%s\"", m.hostMaterial.name.c_str(), m.hostMaterial.firstString().c_str());
                if (!m.primGroups.empty()) { std::printf(", groups{"); for (size_t i = 0; i < m.primGroups.size(); ++i) std::printf("%s%s", i ? "," : "", m.primGroups[i].name.c_str()); std::printf("}"); }
                for (const AttributeData& e : m.extra) std::printf(", %s[%d]", e.name.c_str(), e.tupleSize);
                std::printf("\n");
            }
            for (const CurvePart& c : g.curves)
                std::printf("      curve part %d: %d curves, type %d, %zu cvs%s\n", c.id, c.curveCount, int(c.type), c.P.size() / 3, c.closed ? " closed" : "");
            for (const InstancerPart& ip : g.instancers)
                std::printf("      instancer part %d: %d instances of %zu part(s)%s%s\n", ip.id, ip.instanceCount, ip.instancedPartIds.size(),
                            ip.instanceAttr.exists() ? (" attr " + ip.instanceAttr.name).c_str() : "",
                            ip.splitAttr.exists() ? " (split)" : "");
            for (const VolumePart& v : g.volumes)
                std::printf("      volume part %d '%s': %dx%dx%d%s\n", v.id, v.name.c_str(), v.xLength, v.yLength, v.zLength, v.type == HAPI_VOLUMETYPE_VDB ? " VDB" : "");
            if (g.skippedParts) std::printf("      (%d part(s) skipped)\n", g.skippedParts);
        }
    }
    for (const auto& kv : out.materials)
    {
        const MaterialOutput& m = kv.second;
        std::printf("  material %d '%s' %s", m.nodeId, m.name.c_str(), m.path.c_str());
        if (m.hasBaseColor) std::printf(" basecolor=(%.2f %.2f %.2f)", m.baseColor[0], m.baseColor[1], m.baseColor[2]);
        if (!m.baseColorTexture.empty()) std::printf(" tex=%s", m.baseColorTexture.c_str());
        std::printf("\n");
    }

    if (!a.outObj.empty())
    {
        writeObj(a.outObj, out, conv);
        std::printf("obj      : wrote %s\n", a.outObj.c_str());
    }
    if (!a.hip.empty() && Lib::SaveHIPFile)
    {
        if (Lib::SaveHIPFile(s.handle(), a.hip.c_str(), false) == HAPI_RESULT_SUCCESS) std::printf("hip      : wrote %s\n", a.hip.c_str());
        else std::printf("hip      : failed: %s\n", s.lastCallError().c_str());
    }

    deleteNode(node);
    s.stop();
    Lib::unload();
    return 0;
}
