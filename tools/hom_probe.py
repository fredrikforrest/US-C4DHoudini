#!/usr/bin/env python3
"""
hom_probe: the hapi_probe counterpart for the HOM (hython) backend.

    python3 tools/hom_probe.py asset.hda [--hfs /opt/hfs22.0] [--connect PORT]
        [--asset TYPE] [--parm name=value ...] [--frame N --fps F]
        [--input mesh.obj] [--obj out.obj] [--instances] [--scale 100] [--no-flip]
        [--hip debug.hiplc]

Spawns hython (or connects to a server started inside a Houdini GUI with
hom_server.start_in_ui), loads the HDA, prints the parameter interface,
cooks and dumps the output in Cinema 4D space, exactly like hapi_probe.
"""
import argparse
import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "hom_bridge"))
from hom_client import HomBridge  # noqa: E402


def to_host(p, scale, flip):
    return (p[0] * scale, p[1] * scale, (-p[2] if flip else p[2]) * scale)


def read_obj(path, scale, flip):
    P, fc, vl = [], [], []
    for line in open(path):
        parts = line.split()
        if not parts:
            continue
        if parts[0] == "v":
            x, y, z = (float(v) for v in parts[1:4])
            P.extend([x / scale, y / scale, (-z if flip else z) / scale])
        elif parts[0] == "f":
            idx = [int(t.split("/")[0]) - 1 for t in parts[1:]]
            fc.append(len(idx))
            vl.extend(idx)
    return {"P": P, "face_counts": fc, "vertex_list": vl}


def write_obj(path, objects, scale, flip):
    base = 1
    with open(path, "w") as o:
        o.write("# hom_probe, Cinema 4D space\n")
        for obj in objects:
            m = obj["mesh"]
            if not m or not m["P"]:
                continue
            o.write("o %s\n" % obj["name"])
            P = m["P"]
            xf = obj["transform"]
            for i in range(0, len(P), 3):
                x, y, z = P[i], P[i + 1], P[i + 2]
                # row-vector transform by the 4x4 object matrix
                tx = x * xf[0] + y * xf[4] + z * xf[8] + xf[12]
                ty = x * xf[1] + y * xf[5] + z * xf[9] + xf[13]
                tz = x * xf[2] + y * xf[6] + z * xf[10] + xf[14]
                hx, hy, hz = to_host((tx, ty, tz), scale, flip)
                o.write("v %g %g %g\n" % (hx, hy, hz))
            cursor = 0
            for n, closed, t in zip(m["face_counts"], m["closed"], m["prim_types"]):
                if t == "Poly" and closed and n >= 3:
                    o.write("f " + " ".join(str(base + m["vertex_list"][cursor + k]) for k in range(n)) + "\n")
                elif n >= 2:
                    o.write("l " + " ".join(str(base + m["vertex_list"][cursor + k]) for k in range(n)) + "\n")
                cursor += n
            base += m["point_count"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("hda")
    ap.add_argument("--hfs")
    ap.add_argument("--connect", type=int)
    ap.add_argument("--port", type=int, default=18900)
    ap.add_argument("--asset")
    ap.add_argument("--parm", action="append", default=[])
    ap.add_argument("--frame", type=float)
    ap.add_argument("--fps", type=float, default=25.0)
    ap.add_argument("--input")
    ap.add_argument("--obj")
    ap.add_argument("--hip")
    ap.add_argument("--instances", action="store_true", help="keep packed prims as instancers")
    ap.add_argument("--scale", type=float, default=100.0)
    ap.add_argument("--no-flip", action="store_true")
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args()
    flip = not a.no_flip

    t0 = time.time()
    b = HomBridge.connect(a.connect) if a.connect else HomBridge.spawn(a.hfs, a.port)
    hello = b.hello()
    print("backend  : Houdini %s, license %s, %s, up in %.0f ms" % (
        hello["houdini"], hello["license"], "GUI" if hello["ui"] else "hython", (time.time() - t0) * 1000))

    lib = b.load_hda(os.path.abspath(a.hda))
    print("library  : %d asset(s)" % len(lib))
    for d in lib:
        print("           - %s (%s) \"%s\"" % (d["type"], d["category"], d["label"]))
    if not lib:
        return 1
    chosen = next((d for d in lib if d["type"] == a.asset), lib[0])
    inst = b.instantiate(chosen["type"], chosen["category"], "probe")
    aid = inst["id"]
    print("asset    : %s at %s, inputs %s" % (inst["full_type"], inst["path"], inst["inputs"]))

    parms = b.get_parms(aid)
    if not a.quiet:
        print("parms    : %d" % len(parms))
        depth = {None: 0}
        for p in parms:
            d = depth.get(p["parent"], 0)
            depth[p["name"]] = d + 1
            line = "  " * (d + 1) + "%-10s %-24s \"%s\"" % (p["kind"], p["name"], p["label"])
            if "value" in p:
                line += " = %s" % (p["value"],)
            if p.get("menu_items"):
                line += "  menu{%s}" % ",".join(p["menu_items"])
            if p.get("hidden"):
                line += " (hidden)"
            if p.get("disabled"):
                line += " (disabled)"
            if p.get("count") is not None:
                line += "  instances=%d" % p["count"]
            print(line)

    values = {}
    for kv in a.parm:
        name, _, val = kv.partition("=")
        parts = val.split(",")
        conv = []
        for v in parts:
            try:
                conv.append(int(v) if v.lstrip("-").isdigit() else float(v))
            except ValueError:
                conv.append(v)
        values[name] = conv if len(conv) > 1 else conv[0]
    if values:
        failed = b.set_parms(aid, values)
        print("set      : %s%s" % (values, (" FAILED " + str(failed)) if failed else ""))

    if a.input:
        m = read_obj(a.input, a.scale, flip)
        r = b.set_input(aid, 0, [m])
        print("input    : %s -> %d points, %d faces (%s)" % (a.input, len(m["P"]) // 3, len(m["face_counts"]), r.get("node")))

    if a.frame is not None:
        b.set_time(frame=a.frame, fps=a.fps)
        print("time     : frame %g @ %g fps" % (a.frame, a.fps))

    cr = b.cook(aid, force=True)
    print("cook     : %s in %.0f ms%s" % ("ok" if cr["ok"] else "FAILED", cr["cook_ms"],
                                          ", time dependent" if cr["time_dependent"] else ""))
    for w in cr["warnings"]:
        print("  warning:", w)
    for e in cr["errors"]:
        print("  error:", e)

    t1 = time.time()
    out = b.get_output(aid, unpack=not a.instances)
    print("output   : fetched in %.0f ms: %d object(s)" % ((time.time() - t1) * 1000, len(out)))
    for o in out:
        m = o["mesh"]
        if m:
            kinds = {}
            for t in m["prim_types"]:
                kinds[t] = kinds.get(t, 0) + 1
            print("  object '%s': %d pts, %d prims %s, attribs %s, groups %s" % (
                o["name"], m["point_count"], m["prim_count"], kinds,
                {k: "%s[%d]" % (v["owner"], v["size"]) for k, v in m["attribs"].items()},
                list(m["groups"].keys())))
            for k, v in m["strings"].items():
                print("    %s: %d values, e.g. %s" % (k, len(v), sorted(set(v))[:5]))
        for inst in o["instancers"]:
            print("  instancer: %d instances of a %d-point source" % (inst["count"], inst["source"]["point_count"] if inst["source"] else 0))

    if a.obj:
        write_obj(a.obj, out, a.scale, flip)
        print("obj      : wrote", a.obj)
    if a.hip:
        b.save_hip(os.path.abspath(a.hip))
        print("hip      : wrote", a.hip)
    b.delete(aid)
    b.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
