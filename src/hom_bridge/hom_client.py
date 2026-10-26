"""
Client for hom_server.py. Pure Python (works inside Cinema 4D's Python).

    from hom_client import HomBridge
    b = HomBridge.spawn(hfs="/opt/hfs22.0")      # or HomBridge.connect(port)
    lib = b.load_hda("asset.hda")
    aid = b.instantiate(lib[0]["type"], lib[0]["category"], "myasset")
    parms = b.get_parms(aid)
    b.set_parms(aid, {"size": [2.0]})
    print(b.cook(aid))
    out = b.get_output(aid)   # objects with decoded arrays
"""
import os
import socket
import struct
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from protocol import recv_message, send_message  # noqa: E402


def _f32(b):
    return struct.unpack("<%df" % (len(b) // 4), b) if b else ()


def _i32(b):
    return struct.unpack("<%di" % (len(b) // 4), b) if b else ()


class BridgeError(RuntimeError):
    pass


class HomBridge(object):
    def __init__(self, sock, process=None):
        self.sock = sock
        self.process = process

    # ---- lifecycle ------------------------------------------------------
    @classmethod
    def connect(cls, port=18900, host="127.0.0.1", timeout=5.0):
        s = socket.create_connection((host, port), timeout=timeout)
        s.settimeout(None)
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        return cls(s)

    @classmethod
    def spawn(cls, hfs=None, port=18900, timeout=60.0, log=None):
        hfs = hfs or os.environ.get("HFS")
        if not hfs:
            raise BridgeError("set HFS or pass hfs=")
        exe = os.path.join(hfs, "bin", "hython.exe" if os.name == "nt" else "hython")
        script = os.path.join(os.path.dirname(os.path.abspath(__file__)), "hom_server.py")
        env = dict(os.environ)
        for k in ("PYTHONPATH", "PYTHONHOME", "LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH", "QT_PLUGIN_PATH"):
            env.pop(k, None)
        proc = subprocess.Popen([exe, script, "--port", str(port)], env=env,
                                stdout=log or subprocess.DEVNULL, stderr=log or subprocess.DEVNULL)
        deadline = time.time() + timeout
        while time.time() < deadline:
            if proc.poll() is not None:
                raise BridgeError("hython exited with code %s (license?)" % proc.returncode)
            try:
                b = cls.connect(port)
                b.process = proc
                return b
            except OSError:
                time.sleep(0.25)
        proc.kill()
        raise BridgeError("hython did not start listening within %.0fs" % timeout)

    def close(self):
        try:
            if self.process is not None:
                self.call("shutdown")
        except Exception:   # noqa: BLE001
            pass
        self.sock.close()
        if self.process is not None:
            try:
                self.process.wait(timeout=10)
            except Exception:   # noqa: BLE001
                self.process.kill()

    # ---- raw --------------------------------------------------------------
    def call(self, cmd, blobs=None, **kwargs):
        header = dict(kwargs)
        header["cmd"] = cmd
        send_message(self.sock, header, blobs)
        reply, out = recv_message(self.sock)
        if not reply.get("ok", False):
            raise BridgeError(reply.get("error", "unknown error") + "\n" + reply.get("traceback", ""))
        reply.pop("ok", None)
        return reply, out

    # ---- commands ---------------------------------------------------------
    def hello(self):
        return self.call("hello")[0]

    def load_hda(self, path):
        return self.call("load_hda", path=path)[0]["assets"]

    def instantiate(self, type_name, category="Sop", label="asset"):
        r = self.call("instantiate", type=type_name, category=category, label=label)[0]
        return r

    def get_parms(self, aid):
        return self.call("get_parms", id=aid)[0]["parms"]

    def set_parms(self, aid, values):
        return self.call("set_parms", id=aid, values=values)[0]["failed"]

    def press_button(self, aid, name):
        self.call("press_button", id=aid, name=name)

    def set_time(self, frame=None, fps=None):
        kw = {}
        if frame is not None:
            kw["frame"] = frame
        if fps is not None:
            kw["fps"] = fps
        return self.call("set_time", **kw)[0]

    def cook(self, aid, force=False):
        return self.call("cook", id=aid, force=force)[0]

    def get_output(self, aid, unpack=True, attributes=None):
        opts = {"unpack": unpack}
        if attributes:
            opts["attributes"] = attributes
        reply, blobs = self.call("get_output", id=aid, options=opts)
        objects = []
        for o in reply["objects"]:
            obj = {"name": o["name"], "transform": o["transform"], "mesh": self._decode_mesh(o.get("mesh"), blobs),
                   "instancers": []}
            for inst in o.get("instancers", []):
                obj["instancers"].append({
                    "count": inst["count"],
                    "transforms": _f32(blobs.get(inst["key"] + ":xforms", b"")),
                    "source": self._decode_mesh(inst["source"], blobs),
                })
            objects.append(obj)
        return objects

    @staticmethod
    def _decode_mesh(part, blobs):
        if not part:
            return None
        p = part["name"]
        mesh = {
            "point_count": part["point_count"],
            "prim_count": part["prim_count"],
            "P": _f32(blobs.get(p + ":P", b"")),
            "face_counts": _i32(blobs.get(p + ":nv", b"")),
            "closed": _i32(blobs.get(p + ":closed", b"")),
            "vertex_list": _i32(blobs.get(p + ":pt", b"")),
            "prim_types": part["strings"].get("type", []),
            "attribs": {},
            "groups": {g: _i32(blobs.get(p + ":group:" + g, b"")) for g in part.get("groups", [])},
            "strings": {k: v for k, v in part["strings"].items() if k != "type"},
        }
        for name, info in part["attribs"].items():
            mesh["attribs"][name] = dict(info, values=_f32(blobs.get(p + ":" + name, b"")))
        return mesh

    def set_input(self, aid, index, meshes):
        """meshes: list of dicts with P (floats), face_counts, vertex_list,
        optional uv/N (per vertex floats), materials (per prim str),
        groups {name: [0/1]}, transform (16 floats)."""
        blobs = {}
        descs = []
        for i, m in enumerate(meshes):
            key = "in%d" % i
            blobs[key + ":P"] = struct.pack("<%df" % len(m["P"]), *m["P"])
            blobs[key + ":nv"] = struct.pack("<%di" % len(m["face_counts"]), *m["face_counts"])
            blobs[key + ":pt"] = struct.pack("<%di" % len(m["vertex_list"]), *m["vertex_list"])
            for a in ("uv", "N"):
                if m.get(a):
                    blobs[key + ":" + a] = struct.pack("<%df" % len(m[a]), *m[a])
            d = {"key": key}
            if m.get("materials"):
                d["materials"] = m["materials"]
            if m.get("groups"):
                d["groups"] = m["groups"]
            if m.get("transform"):
                d["transform"] = m["transform"]
            descs.append(d)
        return self.call("set_input", blobs, id=aid, index=index, meshes=descs)[0]

    def clear_input(self, aid, index):
        self.call("clear_input", id=aid, index=index)

    def delete(self, aid):
        self.call("delete", id=aid)

    def save_hip(self, path):
        self.call("save_hip", path=path)
