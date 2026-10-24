"""
Cinema 4D side regression for the Houdini Asset object. Run inside Cinema
(Script Manager) or with c4dpy once the plugin is loaded:

    c4dpy -g_additionalModulePath=<plugin folder> tests/c4d/c4d_regression.py <hda_dir>

Each check prints PASS/FAIL and the script exits non-zero on any failure.
Assumes the HDAs from tests/houdini/make_test_hdas.py.
"""
import os
import sys

import c4d

ID_HOUDINI_ASSET = 1064320          # keep in sync with HoudiniAssetObject.cpp
HDA_FILE = 1001
HDA_ASSET_INDEX = 1002
HDA_COOK = 1004
HDA_STATUS = 1007
HDA_SCALE = 1101
HDA_FLIP_Z = 1102
HDA_INPUT_0 = 1201
HDA_DYNAMIC_PARM_BASE = 100000

failures = []


def check(cond, msg):
    print(("PASS " if cond else "FAIL ") + msg)
    if not cond:
        failures.append(msg)


def dyn_parm_id(obj, name):
    """Find the description id the plugin assigned to an HDA parm name."""
    desc = obj.GetDescription(c4d.DESCFLAGS_DESC_NONE)
    for bc, paramid, groupid in desc:
        if paramid[0].id >= HDA_DYNAMIC_PARM_BASE and bc[c4d.DESC_NAME] == name:
            return paramid
    return None


def make_asset(doc, hda_path, name):
    obj = c4d.BaseObject(ID_HOUDINI_ASSET)
    check(obj is not None, "plugin object type registered")
    obj.SetName(name)
    obj[HDA_FILE] = hda_path
    doc.InsertObject(obj)
    c4d.EventAdd()
    doc.ExecutePasses(None, True, True, True, c4d.BUILDFLAGS_NONE)
    return obj


def cache_polys(obj):
    """All PolygonObjects in the generator cache."""
    out = []

    def walk(o):
        while o:
            if o.CheckType(c4d.Opolygon):
                out.append(o)
            walk(o.GetDown())
            o = o.GetNext()

    walk(obj.GetCache())
    return out


def test_facing(doc, hda_dir):
    obj = make_asset(doc, os.path.join(hda_dir, "facing_check.hda"), "facing")
    polys = cache_polys(obj)
    check(len(polys) >= 1, "facing_check produced polygons")
    if not polys:
        return
    tri = [p for p in polys if p.GetPolygonCount() == 1]
    check(len(tri) == 1, "exactly one single-polygon object (the triangle)")
    if tri:
        p = tri[0]
        pts = p.GetAllPoints()
        poly = p.GetPolygon(0)
        n = (pts[poly.b] - pts[poly.a]).Cross(pts[poly.c] - pts[poly.a])
        check(n.y > 0, "triangle faces +Y in Cinema (n=%s)" % n)
        zs = sorted(round(v.z, 3) for v in pts)
        check(zs[0] == -100.0, "Z flipped and scaled: expected a point at z=-100, got %s" % zs)
    marker = [p for p in polys if p.GetPolygonCount() == 6]
    if marker:
        mg = marker[0].GetMg()
        c = sum((mg * v for v in marker[0].GetAllPoints()), c4d.Vector()) / 8.0
        check(abs(c.z + 200.0) < 1.0, "marker box lands at z=-200 (got %.1f)" % c.z)


def test_parm_zoo(doc, hda_dir):
    obj = make_asset(doc, os.path.join(hda_dir, "parm_zoo.hda"), "zoo")
    polys = cache_polys(obj)
    check(len(polys) == 1, "parm_zoo outputs one mesh")
    before = polys[0].GetPointCount() if polys else 0
    did = dyn_parm_id(obj, "Divisions")
    check(did is not None, "Divisions parm appears in description")
    if did:
        obj[did] = 6
        doc.ExecutePasses(None, True, True, True, c4d.BUILDFLAGS_NONE)
        after = cache_polys(obj)[0].GetPointCount()
        check(after > before, "raising Divisions recooks (points %d -> %d)" % (before, after))
    sid = dyn_parm_id(obj, "Size")
    if sid:
        obj[sid] = 2.0
        doc.ExecutePasses(None, True, True, True, c4d.BUILDFLAGS_NONE)
        rad = cache_polys(obj)[0].GetRad()
        check(abs(rad.x - 100.0) < 1.0, "Size 2 m -> 200 cm box (half extent %.1f)" % rad.x)
    for label in ("Smooth", "Mode", "Quality", "Tint", "Label", "Texture", "Reset Tint", "Scale"):
        check(dyn_parm_id(obj, label) is not None, "description has '%s'" % label)
    check(dyn_parm_id(obj, "Hidden When Smooth") is not None, "conditionally hidden parm visible while Smooth off")
    sm = dyn_parm_id(obj, "Smooth")
    if sm:
        obj[sm] = True
        doc.ExecutePasses(None, True, True, True, c4d.BUILDFLAGS_NONE)
        check(dyn_parm_id(obj, "Hidden When Smooth") is None, "hide-when condition re-evaluated after edit")


def test_inputs(doc, hda_dir):
    obj = make_asset(doc, os.path.join(hda_dir, "deformer.hda"), "deformer")
    base = cache_polys(obj)
    check(len(base) == 1, "deformer without input uses fallback sphere")
    cube = c4d.BaseObject(c4d.Ocube)
    cube.InsertUnder(obj)
    doc.ExecutePasses(None, True, True, True, c4d.BUILDFLAGS_NONE)
    polys = cache_polys(obj)
    check(len(polys) == 1 and polys[0].GetPolygonCount() == 6, "child cube goes through the HDA (6 polys back)")
    doc.SetTime(c4d.BaseTime(1.0))
    doc.ExecutePasses(None, True, True, True, c4d.BUILDFLAGS_NONE)
    t1 = cache_polys(obj)[0].GetAllPoints()
    doc.SetTime(c4d.BaseTime(2.0))
    doc.ExecutePasses(None, True, True, True, c4d.BUILDFLAGS_NONE)
    t2 = cache_polys(obj)[0].GetAllPoints()
    check(t1 != t2, "time dependent asset recooks on frame change")


def test_instancer(doc, hda_dir):
    obj = make_asset(doc, os.path.join(hda_dir, "packed_scatter.hda"), "scatter")
    found = []

    def walk(o):
        while o:
            if o.CheckType(c4d.Oinstance):
                found.append(o)
            walk(o.GetDown())
            o = o.GetNext()

    walk(obj.GetCache())
    check(len(found) >= 1, "packed scatter produces instance objects")
    if found and hasattr(found[0], "GetInstanceCount"):
        check(found[0].GetInstanceCount() == 200, "multi-instance holds 200 matrices (got %d)" % found[0].GetInstanceCount())


def test_named_parts(doc, hda_dir):
    obj = make_asset(doc, os.path.join(hda_dir, "named_parts.hda"), "named")
    polys = cache_polys(obj)
    check(len(polys) >= 1, "named_parts cooks")
    if polys:
        p = polys[0]
        check(p.GetTag(c4d.Tuvw) is not None, "UVW tag present")
        check(p.GetTag(c4d.Tnormal) is not None, "Normal tag present")
        check(p.GetTag(c4d.Tvertexcolor) is not None, "Vertex Color tag present")
        sels = [t.GetName() for t in p.GetTags() if t.CheckType(c4d.Tpolygonselection)]
        check(any(s.startswith("grp_") for s in sels), "prim groups became selection tags (%s)" % sels)
        check(any(s.startswith("mat_") for s in sels), "c4d_material became selection tags (%s)" % sels)


def test_save_reload(doc, hda_dir):
    obj = make_asset(doc, os.path.join(hda_dir, "parm_zoo.hda"), "persist")
    did = dyn_parm_id(obj, "Divisions")
    obj[did] = 9
    doc.ExecutePasses(None, True, True, True, c4d.BUILDFLAGS_NONE)
    path = os.path.join(hda_dir, "_persist_test.c4d")
    c4d.documents.SaveDocument(doc, path, c4d.SAVEDOCUMENTFLAGS_NONE, c4d.FORMAT_C4DEXPORT)
    doc2 = c4d.documents.LoadDocument(path, c4d.SCENEFILTER_OBJECTS, None)
    o2 = doc2.SearchObject("persist")
    check(o2 is not None, "object survives save/load")
    if o2:
        doc2.ExecutePasses(None, True, True, True, c4d.BUILDFLAGS_NONE)
        d2 = dyn_parm_id(o2, "Divisions")
        check(d2 is not None and o2[d2] == 9, "parameter value restored after reload")
        check(d2 == did, "description id stable across save/load")


def main():
    hda_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "houdini", "hda")
    doc = c4d.documents.BaseDocument()
    c4d.documents.InsertBaseDocument(doc)
    test_facing(doc, hda_dir)
    test_parm_zoo(doc, hda_dir)
    test_inputs(doc, hda_dir)
    test_instancer(doc, hda_dir)
    test_named_parts(doc, hda_dir)
    test_save_reload(doc, hda_dir)
    print("\n%d failure(s)" % len(failures))
    for f in failures:
        print("  -", f)
    return len(failures)


if __name__ == "__main__":
    rc = main()
    if rc:
        sys.exit(1)
