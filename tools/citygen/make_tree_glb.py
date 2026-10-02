#!/usr/bin/env python3
"""Build oak / pine / palm GLB trees with embedded PNG textures (trunk + alpha leaves)."""

from __future__ import annotations

import json
import math
import os
import struct
import zlib

OUT_DIR = os.path.normpath(
    os.path.join(os.path.dirname(__file__), "..", "..", "assets", "models")
)


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def png_chunk(tag: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc32(tag + data))


def write_png_rgba(w: int, h: int, rgba: bytes) -> bytes:
    raw = bytearray()
    stride = w * 4
    for y in range(h):
        raw.append(0)
        raw += rgba[y * stride : (y + 1) * stride]
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    return (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", ihdr)
        + png_chunk(b"IDAT", zlib.compress(bytes(raw), 6))
        + png_chunk(b"IEND", b"")
    )


def hash21(x: float, y: float) -> float:
    return math.sin(x * 127.1 + y * 311.7) * 43758.5453 % 1.0


def clamp(v: float, lo: float = 0.0, hi: float = 1.0) -> float:
    return lo if v < lo else hi if v > hi else v


def bark_tex(kind: str, n: int = 128) -> bytes:
    pix = bytearray(n * n * 4)
    for y in range(n):
        for x in range(n):
            u = x / n
            v = y / n
            grain = 0.55 + 0.45 * math.sin(u * 42.0 + math.sin(v * 9.0) * 3.0)
            noise = hash21(x * 0.37, y * 0.19)
            if kind == "palm":
                ring = 0.5 + 0.5 * math.sin(v * 70.0 + noise * 2.0)
                r, g, b = 0.45 + 0.2 * ring, 0.32 + 0.12 * ring, 0.16 + 0.06 * ring
            elif kind == "pine":
                r = 0.28 + 0.12 * grain + 0.08 * noise
                g = 0.18 + 0.08 * grain
                b = 0.10 + 0.04 * noise
            else:
                r = 0.38 + 0.22 * grain + 0.08 * noise
                g = 0.22 + 0.10 * grain
                b = 0.10 + 0.05 * noise
            crack = 1.0 if abs(math.sin(u * 28.0 + v * 2.0)) > 0.92 else 0.0
            r *= 1.0 - 0.25 * crack
            i = (y * n + x) * 4
            pix[i] = int(clamp(r) * 255)
            pix[i + 1] = int(clamp(g) * 255)
            pix[i + 2] = int(clamp(b) * 255)
            pix[i + 3] = 255
    return write_png_rgba(n, n, bytes(pix))


def leaf_tex(kind: str, n: int = 128) -> bytes:
    pix = bytearray(n * n * 4)
    for y in range(n):
        for x in range(n):
            u = (x + 0.5) / n * 2.0 - 1.0
            v = (y + 0.5) / n * 2.0 - 1.0
            i = (y * n + x) * 4
            vein = abs(u) * 0.15
            if kind == "pine":
                d = abs(u) * 1.8 + (v + 1.0) * 0.15
                blade = 1.0 if abs(u) < 0.22 * (1.0 - abs(v)) and v > -0.95 else 0.0
                a = 1.0 if blade > 0.5 and d < 1.6 else 0.0
                r, g, b = 0.18, 0.38 + 0.1 * hash21(x, y), 0.14
            elif kind == "palm":
                # long frond leaflet: opaque along a slightly curved strip
                cu = u + 0.15 * math.sin(v * 6.0)
                a = 1.0 if abs(cu) < 0.18 * (1.0 - abs(v) * 0.35) and abs(v) < 0.96 else 0.0
                r, g, b = 0.20, 0.52 + 0.12 * (0.5 - v * 0.3), 0.16
            else:
                # oak leaf silhouette
                t = math.atan2(u, -v)
                rad = math.sqrt(u * u + v * v)
                lobes = 0.72 + 0.22 * math.cos(t * 5.0) + 0.08 * math.cos(t * 11.0)
                a = 1.0 if rad < lobes * 0.92 and rad > 0.04 else 0.0
                stem = 1.0 if abs(u) < 0.04 and v > 0.15 else 0.0
                a = max(a, stem)
                r = 0.22 + 0.08 * hash21(x, y) - vein
                g = 0.48 + 0.18 * (1.0 - rad) + 0.06 * hash21(y, x)
                b = 0.14
            pix[i] = int(clamp(r) * 255)
            pix[i + 1] = int(clamp(g) * 255)
            pix[i + 2] = int(clamp(b) * 255)
            pix[i + 3] = int(clamp(a) * 255)
    return write_png_rgba(n, n, bytes(pix))


class Mesh:
    def __init__(self) -> None:
        self.pos: list[float] = []
        self.nrm: list[float] = []
        self.uv: list[float] = []
        self.idx: list[int] = []

    def add_vert(self, p, n, uv) -> int:
        i = len(self.pos) // 3
        self.pos.extend(p)
        self.nrm.extend(n)
        self.uv.extend(uv)
        return i

    def tri(self, a: int, b: int, c: int) -> None:
        self.idx.extend((a, b, c))

    def nverts(self) -> int:
        return len(self.pos) // 3


def add_taper_cyl(m: Mesh, x, y, z, r0, r1, h, segs=12, rings=8, u_tile=1.0, v_tile=1.0) -> None:
    base = m.nverts()
    for j in range(rings + 1):
        t = j / rings
        yy = y + t * h
        rr = r0 * (1.0 - t) + r1 * t
        for i in range(segs + 1):
            a = i / segs * math.tau
            c, s = math.cos(a), math.sin(a)
            nx, nz = c, s
            ny = (r0 - r1) / max(h, 1e-4)
            inv = 1.0 / math.sqrt(nx * nx + ny * ny + nz * nz)
            m.add_vert((x + c * rr, yy, z + s * rr), (nx * inv, ny * inv, nz * inv), (i / segs * u_tile, t * v_tile))
    stride = segs + 1
    for j in range(rings):
        for i in range(segs):
            a = base + j * stride + i
            b = a + stride
            m.tri(a, b, a + 1)
            m.tri(a + 1, b, b + 1)


def add_quad(m: Mesh, p0, p1, p2, p3, n, uv0=(0, 0), uv1=(1, 0), uv2=(1, 1), uv3=(0, 1)) -> None:
    a = m.add_vert(p0, n, uv0)
    b = m.add_vert(p1, n, uv1)
    c = m.add_vert(p2, n, uv2)
    d = m.add_vert(p3, n, uv3)
    m.tri(a, b, c)
    m.tri(a, c, d)


def add_leaf_card(m: Mesh, cx, cy, cz, w, h, yaw, pitch) -> None:
    cyaw, syaw = math.cos(yaw), math.sin(yaw)
    cp, sp = math.cos(pitch), math.sin(pitch)
    right = (cyaw, 0.0, -syaw)
    up = (-syaw * sp, cp, -cyaw * sp)
    n = (right[1] * up[2] - right[2] * up[1], right[2] * up[0] - right[0] * up[2], right[0] * up[1] - right[1] * up[0])
    ln = math.sqrt(n[0] ** 2 + n[1] ** 2 + n[2] ** 2) or 1.0
    n = (n[0] / ln, n[1] / ln, n[2] / ln)
    hw, hh = w * 0.5, h * 0.5

    def pt(sx, sy):
        return (
            cx + right[0] * sx * hw + up[0] * sy * hh,
            cy + right[1] * sx * hw + up[1] * sy * hh,
            cz + right[2] * sx * hw + up[2] * sy * hh,
        )

    add_quad(m, pt(-1, -1), pt(1, -1), pt(1, 1), pt(-1, 1), n)


def make_oak() -> tuple[Mesh, Mesh]:
    trunk, leaves = Mesh(), Mesh()
    add_taper_cyl(trunk, 0, 0, 0, 0.38, 0.12, 7.2, 8, 4, 2.0, 3.0)
    for k in range(3):
        a = k * 2.094 + 0.4
        by = 3.6 + k * 0.7
        dx, dz = math.cos(a), math.sin(a)
        add_taper_cyl(trunk, dx * 0.12, by, dz * 0.12, 0.08, 0.03, 1.8, 6, 3, 1.0, 1.5)
    for i in range(90):
        h = hash21(i, 3)
        t = hash21(i, 7)
        a = h * math.tau
        r = math.sqrt(max(t, 0.0)) * 2.4
        y = 5.2 + hash21(i, 11) * 3.0
        x, z = math.cos(a) * r, math.sin(a) * r
        add_leaf_card(leaves, x, y, z, 0.85, 1.05, a + 0.4, -0.25 + 0.4 * hash21(i, 19))
    return trunk, leaves


def make_pine() -> tuple[Mesh, Mesh]:
    trunk, leaves = Mesh(), Mesh()
    add_taper_cyl(trunk, 0, 0, 0, 0.26, 0.06, 9.5, 8, 4, 1.5, 4.0)
    for layer in range(6):
        y = 2.2 + layer * 1.15
        rad = 2.2 - layer * 0.28
        n = 6
        for k in range(n):
            a = k / n * math.tau + layer * 0.21
            x, z = math.cos(a) * rad * 0.45, math.sin(a) * rad * 0.45
            add_leaf_card(leaves, x, y, z, 0.55, 1.35, a, 0.85)
    return trunk, leaves


def make_palm() -> tuple[Mesh, Mesh]:
    trunk, leaves = Mesh(), Mesh()
    add_taper_cyl(trunk, 0, 0, 0, 0.22, 0.11, 11.5, 8, 6, 1.0, 6.0)
    for k in range(8):
        a = k / 8 * math.tau
        for s in range(8):
            t = s / 7
            along = 0.4 + t * 3.2
            drop = t * t * 1.6
            x = math.cos(a) * along
            z = math.sin(a) * along
            y = 11.3 - drop
            add_leaf_card(leaves, x, y, z, 0.70, 1.35, a + 1.57, 0.15 + t * 0.7)
    return trunk, leaves


def accessor(bview, count, ctype, typ, offset=0, mn=None, mx=None):
    a = {"bufferView": bview, "componentType": ctype, "count": count, "type": typ, "byteOffset": offset}
    if mn is not None:
        a["min"] = mn
        a["max"] = mx
    return a


def align4(buf: bytearray) -> None:
    while len(buf) % 4:
        buf.append(0)


def pack_mesh(bin_buf: bytearray, mesh: Mesh):
    align4(bin_buf)
    pos_off = len(bin_buf)
    pos = struct.pack("<%sf" % len(mesh.pos), *mesh.pos)
    bin_buf += pos
    nrm_off = len(bin_buf)
    bin_buf += struct.pack("<%sf" % len(mesh.nrm), *mesh.nrm)
    uv_off = len(bin_buf)
    bin_buf += struct.pack("<%sf" % len(mesh.uv), *mesh.uv)
    align4(bin_buf)
    idx_off = len(bin_buf)
    bin_buf += struct.pack("<%sI" % len(mesh.idx), *mesh.idx)
    px = mesh.pos[0::3]
    py = mesh.pos[1::3]
    pz = mesh.pos[2::3]
    mn = [min(px), min(py), min(pz)]
    mx = [max(px), max(py), max(pz)]
    return {
        "pos_off": pos_off,
        "pos_len": len(pos),
        "nrm_off": nrm_off,
        "nrm_len": len(mesh.nrm) * 4,
        "uv_off": uv_off,
        "uv_len": len(mesh.uv) * 4,
        "idx_off": idx_off,
        "idx_len": len(mesh.idx) * 4,
        "nvert": mesh.nverts(),
        "nidx": len(mesh.idx),
        "min": mn,
        "max": mx,
    }


def build_glb(path: str, trunk: Mesh, leaves: Mesh, bark_png: bytes, leaf_png: bytes, name: str) -> int:
    bin_buf = bytearray()
    img0 = len(bin_buf)
    bin_buf += bark_png
    img0_len = len(bark_png)
    align4(bin_buf)
    img1 = len(bin_buf)
    bin_buf += leaf_png
    img1_len = len(leaf_png)
    t = pack_mesh(bin_buf, trunk)
    l = pack_mesh(bin_buf, leaves)

    views = [
        {"buffer": 0, "byteOffset": img0, "byteLength": img0_len},
        {"buffer": 0, "byteOffset": img1, "byteLength": img1_len},
        {"buffer": 0, "byteOffset": t["pos_off"], "byteLength": t["pos_len"]},
        {"buffer": 0, "byteOffset": t["nrm_off"], "byteLength": t["nrm_len"]},
        {"buffer": 0, "byteOffset": t["uv_off"], "byteLength": t["uv_len"]},
        {"buffer": 0, "byteOffset": t["idx_off"], "byteLength": t["idx_len"]},
        {"buffer": 0, "byteOffset": l["pos_off"], "byteLength": l["pos_len"]},
        {"buffer": 0, "byteOffset": l["nrm_off"], "byteLength": l["nrm_len"]},
        {"buffer": 0, "byteOffset": l["uv_off"], "byteLength": l["uv_len"]},
        {"buffer": 0, "byteOffset": l["idx_off"], "byteLength": l["idx_len"]},
    ]
    acc = [
        accessor(2, t["nvert"], 5126, "VEC3", 0, t["min"], t["max"]),
        accessor(3, t["nvert"], 5126, "VEC3"),
        accessor(4, t["nvert"], 5126, "VEC2"),
        accessor(5, t["nidx"], 5125, "SCALAR"),
        accessor(6, l["nvert"], 5126, "VEC3", 0, l["min"], l["max"]),
        accessor(7, l["nvert"], 5126, "VEC3"),
        accessor(8, l["nvert"], 5126, "VEC2"),
        accessor(9, l["nidx"], 5125, "SCALAR"),
    ]
    gltf = {
        "asset": {"version": "2.0", "generator": "leonida-tree-glb"},
        "buffers": [{"byteLength": len(bin_buf)}],
        "bufferViews": views,
        "accessors": acc,
        "images": [
            {"bufferView": 0, "mimeType": "image/png", "name": name + "_bark"},
            {"bufferView": 1, "mimeType": "image/png", "name": name + "_leaf"},
        ],
        "samplers": [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}],
        "textures": [{"sampler": 0, "source": 0}, {"sampler": 0, "source": 1}],
        "materials": [
            {
                "name": "bark",
                "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicFactor": 0.0, "roughnessFactor": 0.9},
                "alphaMode": "OPAQUE",
            },
            {
                "name": "leaves",
                "pbrMetallicRoughness": {"baseColorTexture": {"index": 1}, "metallicFactor": 0.0, "roughnessFactor": 0.8},
                "alphaMode": "MASK",
                "alphaCutoff": 0.5,
                "doubleSided": True,
            },
        ],
        "meshes": [
            {
                "name": name + "_trunk",
                "primitives": [
                    {"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2}, "indices": 3, "material": 0}
                ],
            },
            {
                "name": name + "_leaves",
                "primitives": [
                    {"attributes": {"POSITION": 4, "NORMAL": 5, "TEXCOORD_0": 6}, "indices": 7, "material": 1}
                ],
            },
        ],
        "nodes": [{"name": "trunk", "mesh": 0}, {"name": "leaves", "mesh": 1}],
        "scenes": [{"nodes": [0, 1]}],
        "scene": 0,
    }
    js = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    while len(js) % 4:
        js += b" "
    while len(bin_buf) % 4:
        bin_buf.append(0)
        gltf["buffers"][0]["byteLength"] = len(bin_buf)
        js = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
        while len(js) % 4:
            js += b" "

    json_chunk = struct.pack("<I", len(js)) + b"JSON" + js
    bin_chunk = struct.pack("<I", len(bin_buf)) + b"BIN\x00" + bytes(bin_buf)
    total = 12 + len(json_chunk) + len(bin_chunk)
    header = struct.pack("<III", 0x46546C67, 2, total)
    with open(path, "wb") as f:
        f.write(header + json_chunk + bin_chunk)
    nverts = trunk.nverts() + leaves.nverts()
    ntri = (len(trunk.idx) + len(leaves.idx)) // 3
    print(f"[tree] wrote {path} verts={nverts} tris={ntri} bytes={total}")
    return nverts


def main() -> None:
    os.makedirs(OUT_DIR, exist_ok=True)
    oak_t, oak_l = make_oak()
    pine_t, pine_l = make_pine()
    palm_t, palm_l = make_palm()
    build_glb(
        os.path.join(OUT_DIR, "oak_tree_realistic.glb"),
        oak_t,
        oak_l,
        bark_tex("oak"),
        leaf_tex("oak"),
        "oak",
    )
    build_glb(
        os.path.join(OUT_DIR, "pine_tree_realistic.glb"),
        pine_t,
        pine_l,
        bark_tex("pine"),
        leaf_tex("pine"),
        "pine",
    )
    build_glb(
        os.path.join(OUT_DIR, "palm_tree_realistic.glb"),
        palm_t,
        palm_l,
        bark_tex("palm"),
        leaf_tex("palm"),
        "palm",
    )


if __name__ == "__main__":
    main()
