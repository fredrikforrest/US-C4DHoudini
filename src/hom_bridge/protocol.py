"""
Wire protocol shared by the hython server and the client.

Message = 4-byte big-endian length, JSON header (utf-8), then the binary
blobs the header announces in "blobs": [[name, nbytes], ...]. Arrays are
float32 / int32 little-endian. No third-party dependencies, so it runs in
hython, in Cinema 4D's Python and in plain Python.
"""
import json
import socket
import struct

HEADER_LEN = struct.Struct(">I")


def send_message(sock, header, blobs=None):
    blobs = blobs or {}
    header = dict(header)
    header["blobs"] = [[name, len(data)] for name, data in blobs.items()]
    raw = json.dumps(header).encode("utf-8")
    sock.sendall(HEADER_LEN.pack(len(raw)) + raw)
    for name, data in blobs.items():
        if data:
            sock.sendall(data)


def _recv_exact(sock, n):
    chunks = []
    got = 0
    while got < n:
        chunk = sock.recv(min(n - got, 1 << 20))
        if not chunk:
            raise ConnectionError("connection closed")
        chunks.append(chunk)
        got += len(chunk)
    return b"".join(chunks)


def recv_message(sock):
    (n,) = HEADER_LEN.unpack(_recv_exact(sock, 4))
    header = json.loads(_recv_exact(sock, n).decode("utf-8"))
    blobs = {}
    for name, size in header.pop("blobs", []):
        blobs[name] = _recv_exact(sock, size) if size else b""
    return header, blobs
