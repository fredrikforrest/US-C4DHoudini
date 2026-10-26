"""
HOM bridge server: Houdini Engine functionality through Houdini's Python
object model, runnable under a Houdini Indie license.

Headless:   hython src/hom_bridge/hom_server.py --port 18900
Attached:   paste into the Python shell of a running Houdini (Indie GUI):
                import hom_server; hom_server.start_in_ui(18900)

Commands (JSON "cmd"): hello, load_hda, instantiate, get_parms, set_parms,
press_button, set_time, cook, get_output, set_input, clear_input, delete,
save_hip, shutdown. See hom_client.py for the client side.

Design notes
- One hidden container /obj/c4d_bridge holds everything we create.
- Topology is read through bulk accessors by appending two VEX wrangles
  after the asset that write i@__nv per primitive and i@__pt per vertex;
  hou.Geometry has bulk accessors for attributes but not for the vertex
  list itself.
- Packed primitives: with unpack=True (default) they are unpacked so the
  result is plain polygons; with unpack=False, transforms are returned per
  packed prim and one source mesh per unique geometryid.
"""
import argparse
import json
import socket
import struct
import sys
import threading
import time
import traceback

import hou

sys.path.insert(0, __file__.rsplit("/", 1)[0] if "/" in __file__ else ".")
from protocol import recv_message, send_message  # noqa: E402

TOPO_PRIM_VEX = (
    'i@__nv = primvertexcount(0, @primnum);\n'
    'i@__closed = primintrinsic(0, "closed", @primnum);\n'
    's@__type = primintrinsic(0, "typename", @primnum);\n'
)
TOPO_VTX_VEX = 'i@__pt = @ptnum;\n'


class Asset(object):
    def __init__(self, node, container, is_sop):
        self.node = node
        self.container = container      # geo container for SOP assets, else None
        self.is_sop = is_sop
        self.inputs = {}                # index -> merge node
        self.export = None              # export chain nodes (dict)


class Bridge(object):
    def __init__(self):
        self.assets = {}
        self.next_id = 1
        root = hou.node("/obj")
        self.root = root.node("c4d_bridge") or root.createNode("subnet", "c4d_bridge")
        self.root.setDisplayFlag(False)

    # ------------------------------------------------------------ helpers
    def _asset(self, msg):
        a = self.assets.get(int(msg["id"]))
        if a is None:
            raise KeyError("unknown asset id %r" % msg.get("id"))
        return a

    def _display_sop(self, a):
        if a.is_sop:
            return a.node
        return None

    # ------------------------------------------------------------ commands
    def cmd_hello(self, msg, blobs):
        lic = hou.licenseCategory()
        return {
            "houdini": hou.applicationVersionString(),
            "license": str(lic).split(".")[-1],
            "ui": hou.isUIAvailable(),
            "python": sys.version.split()[0],
        }, {}

    def cmd_load_hda(self, msg, blobs):
        path = msg["path"]
        hou.hda.installFile(path, force_use_assets=True)
        defs = []
        for d in hou.hda.definitionsInFile(path):
            defs.append({
                "type": d.nodeTypeName(),
                "category": d.nodeTypeCategory().name(),
                "label": d.description(),
                "version": d.version(),
            })
        return {"assets": defs}, {}

    def cmd_instantiate(self, msg, blobs):
        type_name = msg["type"]
        category = msg.get("category", "Sop")
        label = msg.get("label") or "asset"
        if category == "Sop":
            container = self.root.createNode("geo", label)
            node = container.createNode(type_name, label)
            node.setDisplayFlag(True)
            node.setRenderFlag(True)
            is_sop = True
        else:
            container = None
            node = self.root.createNode(type_name, label)
            is_sop = False
        aid = self.next_id
        self.next_id += 1
        self.assets[aid] = Asset(node, container, is_sop)
        labels = list(node.inputLabels()) if is_sop else []
        return {"id": aid, "is_sop": is_sop, "path": node.path(), "inputs": labels,
                "full_type": node.type().nameWithCategory()}, {}

    # ---- parameters -----------------------------------------------------
    def _template_info(self, node, t, out, parent, multi_prefix=None):
        kind = t.type().name()
        name = t.name()
        if multi_prefix is not None:
            name = name.replace("#", str(multi_prefix))
        info = {
            "name": name,
            "template": t.name(),
            "label": t.label(),
            "kind": kind,                       # Folder, FolderSet, Int, Float, String, Toggle, Menu, Button, Ramp, Separator, Label, Data
            "parent": parent,
            "hidden": t.isHidden(),
            "help": t.help(),
        }
        if kind in ("Int", "Float", "String", "Toggle", "Menu", "Button", "Ramp"):
            info["size"] = t.numComponents() if hasattr(t, "numComponents") else 1
        if kind in ("Int", "Float"):
            info["min"] = t.minValue()
            info["max"] = t.maxValue()
            info["min_strict"] = t.minIsStrict()
            info["max_strict"] = t.maxIsStrict()
            info["look"] = t.look().name()
            info["naming"] = t.namingScheme().name()
        if kind == "String":
            info["string_type"] = t.stringType().name()    # Regular, FileReference, NodeReference, ...
            info["file_type"] = t.fileType().name() if hasattr(t, "fileType") else ""
        if kind in ("Int", "String", "Menu") and t.menuItems():
            info["menu_items"] = list(t.menuItems())
            info["menu_labels"] = list(t.menuLabels())
        if kind == "Ramp":
            info["ramp_type"] = t.parmType().name()
        if kind == "Folder":
            info["folder_type"] = t.folderType().name()   # Tabs, Collapsible, Simple, MultiparmBlock, ...
        # Current values and evaluated conditionals.
        pt = node.parmTuple(name)
        if pt is not None and kind in ("Int", "Float", "String", "Toggle", "Menu"):
            try:
                info["value"] = list(pt.eval())
            except hou.Error:
                info["value"] = None
            info["disabled"] = pt.isDisabled()
            info["hidden"] = info["hidden"] or pt.isHidden()
        if kind == "Folder" and t.folderType() == hou.folderType.MultiparmBlock:
            p = node.parm(name)
            count = int(p.eval()) if p else 0
            info["count"] = count
            out.append(info)
            for i in range(1, count + 1):
                for c in t.parmTemplates():
                    self._template_info(node, c, out, name, multi_prefix=i)
            return
        out.append(info)
        if kind in ("Folder", "FolderSet"):
            for c in t.parmTemplates():
                self._template_info(node, c, out, name, multi_prefix)

    def cmd_get_parms(self, msg, blobs):
        a = self._asset(msg)
        out = []
        for t in a.node.parmTemplateGroup().entries():
            self._template_info(a.node, t, out, None)
        return {"parms": out}, {}

    def cmd_set_parms(self, msg, blobs):
        a = self._asset(msg)
        failed = []
        for name, value in msg["values"].items():
            pt = a.node.parmTuple(name)
            if pt is None:
                failed.append(name)
                continue
            try:
                if isinstance(value, list):
                    pt.set(tuple(value))
                else:
                    pt[0].set(value)
            except hou.Error as e:
                failed.append("%s: %s" % (name, e))
        return {"failed": failed}, {}

    def cmd_press_button(self, msg, blobs):
        a = self._asset(msg)
        a.node.parm(msg["name"]).pressButton()
        return {}, {}

    def cmd_set_time(self, msg, blobs):
        if "fps" in msg:
            hou.setFps(float(msg["fps"]))
        if "frame" in msg:
            hou.setFrame(float(msg["frame"]))
        elif "time" in msg:
            hou.setTime(float(msg["time"]))
        return {"frame": hou.frame()}, {}

    def cmd_cook(self, msg, blobs):
        a = self._asset(msg)
        t0 = time.time()
        ok = True
        try:
            if a.is_sop:
                a.node.cook(force=bool(msg.get("force", False)))
            else:
                for child in a.node.children():
                    if child.type().name() == "geo" and child.displayNode():
                        child.displayNode().cook(force=bool(msg.get("force", False)))
        except hou.OperationFailed:
            ok = False
        errors = list(a.node.errors())
        warnings = list(a.node.warnings())
        return {"ok": ok and not errors, "errors": errors, "warnings": warnings,
                "time_dependent": a.node.isTimeDependent(), "cook_ms": (time.time() - t0) * 1000.0}, {}

    # ---- outputs --------------------------------------------------------
    def _export_chain(self, a, source_path, unpack):
        """object_merge -> (unpack) -> prim wrangle -> vertex wrangle."""
        parent = a.container if a.is_sop else self.root
        chain = a.export
        if chain is None:
            om = parent.createNode("object_merge", "c4d_export_merge")
            om.parm("xformtype").set(1)   # into this object
            up = parent.createNode("unpack", "c4d_export_unpack")
            up.parm("transfer_attributes").set("*")
            up.parm("transfer_groups").set("*")
            wp = parent.createNode("attribwrangle", "c4d_export_prim")
            wp.parm("class").set(1)
            wv = parent.createNode("attribwrangle", "c4d_export_vtx")
            wv.parm("class").set(3)
            wv.parm("snippet").set(TOPO_VTX_VEX)
            chain = {"merge": om, "unpack": up, "prim": wp, "vtx": wv}
            a.export = chain
        chain["merge"].parm("objpath1").set(source_path)
        if unpack:
            chain["unpack"].setInput(0, chain["merge"])
            chain["prim"].setInput(0, chain["unpack"])
        else:
            chain["prim"].setInput(0, chain["merge"])
        chain["vtx"].setInput(0, chain["prim"])
        return chain

    def _extract_geometry(self, a, source_path, unpack, opts, blobs, prefix):
        chain = self._export_chain(a, source_path, unpack)
        # Group membership as int prim attributes, generated per geometry.
        base_geo = chain["merge"].geometry()
        groups = [g.name() for g in base_geo.primGroups()] if base_geo else []
        vex = TOPO_PRIM_VEX
        for i, g in enumerate(groups):
            vex += 'i@__g%d = inprimgroup(0, "%s", @primnum);\n' % (i, g)
        chain["prim"].parm("snippet").set(vex)
        geo = chain["vtx"].geometry()
        if geo is None:
            return None
        part = {"name": prefix, "point_count": len(geo.iterPoints()) if False else geo.intrinsicValue("pointcount"),
                "prim_count": geo.intrinsicValue("primitivecount"), "vertex_count": geo.intrinsicValue("vertexcount"),
                "attribs": {}, "groups": groups, "strings": {}}
        if part["point_count"] == 0:
            return part

        def blob(key, data):
            blobs[prefix + ":" + key] = data

        blob("P", geo.pointFloatAttribValuesAsString("P", float_type=hou.numericData.Float32))
        if part["prim_count"]:
            blob("nv", geo.primIntAttribValuesAsString("__nv", int_type=hou.numericData.Int32))
            blob("closed", geo.primIntAttribValuesAsString("__closed", int_type=hou.numericData.Int32))
            part["strings"]["type"] = list(geo.primStringAttribValues("__type"))
            for i, g in enumerate(groups):
                blob("group:" + g, geo.primIntAttribValuesAsString("__g%d" % i, int_type=hou.numericData.Int32))
        if part["vertex_count"]:
            blob("pt", geo.vertexIntAttribValuesAsString("__pt", int_type=hou.numericData.Int32))

        wanted = opts.get("attributes", ["N", "uv", "uv2", "Cd", "Alpha"])
        for name in wanted:
            for owner, finder, reader in (
                ("vertex", geo.findVertexAttrib, geo.vertexFloatAttribValuesAsString),
                ("point", geo.findPointAttrib, geo.pointFloatAttribValuesAsString),
                ("prim", geo.findPrimAttrib, geo.primFloatAttribValuesAsString),
            ):
                at = finder(name)
                if at is not None and at.dataType() == hou.attribData.Float:
                    blob(name, reader(name, float_type=hou.numericData.Float32))
                    part["attribs"][name] = {"owner": owner, "size": at.size()}
                    break
        for sname in ("name", "shop_materialpath", "c4d_material", "unreal_material", "c4d_output_name"):
            at = geo.findPrimAttrib(sname)
            if at is not None and at.dataType() == hou.attribData.String:
                part["strings"][sname] = list(geo.primStringAttribValues(sname))
            else:
                at = geo.findGlobalAttrib(sname)
                if at is not None and at.dataType() == hou.attribData.String:
                    part["strings"][sname] = [geo.attribValue(sname)]
        return part

    def _packed_instances(self, a, source_path, opts, blobs, prefix):
        """Transforms per packed prim + one unpacked source per geometryid."""
        chain = self._export_chain(a, source_path, False)
        geo = chain["merge"].geometry()
        if geo is None:
            return []
        instancers = {}
        for prim in geo.prims():
            if not prim.type().name().startswith("Packed"):
                continue
            gid = prim.intrinsicValue("geometryid")
            m = prim.intrinsicValue("packedfulltransform")   # 16 floats, row major
            instancers.setdefault(gid, []).extend([float(x) for x in m])
        out = []
        for i, (gid, mats) in enumerate(instancers.items()):
            key = "%s:inst%d" % (prefix, i)
            blobs[key + ":xforms"] = struct.pack("<%df" % len(mats), *mats)
            # Source: blast everything but this geometryid, then unpack.
            parent = a.container if a.is_sop else self.root
            bl = parent.node("c4d_export_blast") or parent.createNode("blast", "c4d_export_blast")
            bl.setInput(0, chain["merge"])
            bl.parm("group").set('@geometryid=%d' % gid)
            bl.parm("negate").set(1)
            src = self._extract_geometry(a, bl.path(), True, opts, blobs, key + ":src")
            out.append({"key": key, "count": len(mats) // 16, "source": src, "geometryid": gid})
        return out

    def cmd_get_output(self, msg, blobs):
        a = self._asset(msg)
        opts = msg.get("options", {})
        unpack = opts.get("unpack", True)
        objects = []
        if a.is_sop:
            objs = [(a.node, a.container, hou.Matrix4(1))]
        else:
            objs = []
            for child in a.node.children():
                if child.type().name() == "geo" and child.displayNode() and child.isDisplayFlagSet():
                    objs.append((child.displayNode(), child, child.localTransform()))
        for idx, (sop, obj, xform) in enumerate(objs):
            prefix = "obj%d" % idx
            entry = {"name": obj.name(), "transform": [float(x) for x in xform.asTuple()], "key": prefix}
            entry["mesh"] = self._extract_geometry(a, sop.path(), unpack, opts, blobs, prefix)
            if not unpack:
                entry["instancers"] = self._packed_instances(a, sop.path(), opts, blobs, prefix)
            objects.append(entry)
        return {"objects": objects}, {}

    # ---- inputs ---------------------------------------------------------
    def cmd_set_input(self, msg, blobs):
        a = self._asset(msg)
        index = int(msg["index"])
        parent = a.container if a.is_sop else self.root
        self._clear_input(a, index)
        stashes = []
        for i, m in enumerate(msg["meshes"]):
            key = m["key"]
            P = struct.unpack("<%df" % (len(blobs[key + ":P"]) // 4), blobs[key + ":P"])
            nv = struct.unpack("<%di" % (len(blobs[key + ":nv"]) // 4), blobs[key + ":nv"]) if key + ":nv" in blobs else ()
            pt = struct.unpack("<%di" % (len(blobs[key + ":pt"]) // 4), blobs[key + ":pt"]) if key + ":pt" in blobs else ()
            g = hou.Geometry()
            points = g.createPoints([hou.Vector3(P[j], P[j + 1], P[j + 2]) for j in range(0, len(P), 3)])
            cursor = 0
            polys = []
            for n in nv:
                polys.append([points[pt[cursor + k]] for k in range(n)])
                cursor += n
            if polys:
                g.createPolygons(polys)
            if key + ":uv" in blobs:
                g.addAttrib(hou.attribType.Vertex, "uv", (0.0, 0.0, 0.0))
                g.setVertexFloatAttribValuesFromString("uv", blobs[key + ":uv"], float_type=hou.numericData.Float32)
            if key + ":N" in blobs:
                g.addAttrib(hou.attribType.Vertex, "N", (0.0, 0.0, 0.0))
                g.setVertexFloatAttribValuesFromString("N", blobs[key + ":N"], float_type=hou.numericData.Float32)
            if m.get("materials"):
                g.addAttrib(hou.attribType.Prim, "c4d_material", "")
                g.setPrimStringAttribValues("c4d_material", list(m["materials"]))
            for gname, members in m.get("groups", {}).items():
                grp = g.createPrimGroup(gname)
                prims = g.prims()
                grp.add([prims[j] for j, on in enumerate(members) if on])
            stash = parent.createNode("stash", "c4d_in%d_%d" % (index, i))
            stash.parm("stash").set(g)
            if "transform" in m:
                xf = parent.createNode("xform", "c4d_in%d_%d_xf" % (index, i))
                xf.setInput(0, stash)
                xf.setParms({"xOrd": 0})
                mat = hou.Matrix4(m["transform"])
                xf.parmTuple("t").set(mat.extractTranslates())
                xf.parmTuple("r").set(mat.extractRotates())
                xf.parmTuple("s").set(mat.extractScales())
                stash = xf
            stashes.append(stash)
        if not stashes:
            return {"connected": False}, {}
        if len(stashes) == 1:
            src = stashes[0]
        else:
            src = parent.createNode("merge", "c4d_in%d_merge" % index)
            for i, s in enumerate(stashes):
                src.setInput(i, s)
        if a.is_sop:
            a.node.setInput(index, src)
        a.inputs[index] = src
        return {"connected": True, "node": src.path()}, {}

    def _clear_input(self, a, index):
        parent = a.container if a.is_sop else self.root
        for n in list(parent.children()):
            if n.name().startswith("c4d_in%d_" % index):
                n.destroy()
        if a.is_sop and index < len(a.node.inputs()) and a.node.inputs()[index] is not None:
            a.node.setInput(index, None)
        a.inputs.pop(index, None)

    def cmd_clear_input(self, msg, blobs):
        self._clear_input(self._asset(msg), int(msg["index"]))
        return {}, {}

    def cmd_delete(self, msg, blobs):
        a = self._asset(msg)
        (a.container or a.node).destroy()
        del self.assets[int(msg["id"])]
        return {}, {}

    def cmd_save_hip(self, msg, blobs):
        hou.hipFile.save(msg["path"])
        return {"path": msg["path"]}, {}

    def cmd_shutdown(self, msg, blobs):
        return {"bye": True}, {}


def serve(port, once=False):
    bridge = Bridge()
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", port))
    srv.listen(1)
    sys.stderr.write("[hom_bridge] listening on 127.0.0.1:%d (%s)\n" % (port, hou.applicationVersionString()))
    sys.stderr.flush()
    running = True
    while running:
        conn, _ = srv.accept()
        conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        try:
            while True:
                header, blobs = recv_message(conn)
                cmd = header.get("cmd")
                handler = getattr(bridge, "cmd_" + str(cmd), None)
                try:
                    if handler is None:
                        raise ValueError("unknown command %r" % cmd)
                    reply, out_blobs = handler(header, blobs)
                    reply["ok"] = reply.get("ok", True)
                    send_message(conn, reply, out_blobs)
                except Exception as e:   # noqa: BLE001
                    send_message(conn, {"ok": False, "error": "%s: %s" % (type(e).__name__, e),
                                        "traceback": traceback.format_exc()})
                if cmd == "shutdown":
                    running = False
                    break
        except ConnectionError:
            pass
        finally:
            conn.close()
        if once:
            break
    srv.close()


def start_in_ui(port=18900):
    """Run the server on a background thread inside a Houdini GUI session."""
    t = threading.Thread(target=serve, args=(port,), daemon=True)
    t.start()
    return t


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=18900)
    args = ap.parse_args()
    serve(args.port)
