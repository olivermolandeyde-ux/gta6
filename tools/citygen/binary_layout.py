"""Exact POD layouts matching Leonida C++20 components (Phases 2, 3, 6, 8).

All formats are little-endian ('<') to match the engine on x86_64.
Padding is explicit so Python struct sizes equal sizeof(T) under SysV ABI.
"""

from __future__ import annotations

import struct

# TrafficLaneNodeComponent  sizeof = 44
#   u32 node_id @0  float3 pos @4  float3 fwd @16  float speed @28
#   u32 next @32  u32 left @36  u32 right @40
TRAFFIC_LANE_NODE_FORMAT = "<I3f3ffIII"
TRAFFIC_LANE_NODE_SIZE = 44

# BreakableWindowComponent  sizeof = 16
BREAKABLE_WINDOW_FORMAT = "<IIfHH"
BREAKABLE_WINDOW_SIZE = 16

# BreakableWindowPose  sizeof = 32
BREAKABLE_WINDOW_POSE_FORMAT = "<3f3fff"
BREAKABLE_WINDOW_POSE_SIZE = 32

# PowerGridNodeComponent  sizeof = 160
# bool @24, 3-byte pad, u32 lights[32] @28, count @156
POWER_GRID_NODE_FORMAT = "<I3fff?xxx32II"
POWER_GRID_NODE_SIZE = 160

# StreetLightComponent  sizeof = 20
STREET_LIGHT_FORMAT = "<IIfff"
STREET_LIGHT_SIZE = 20

# WeatherZoneComponent  sizeof = 20
WEATHER_ZONE_FORMAT = "<Iffff"
WEATHER_ZONE_SIZE = 20

# TransformComponent  sizeof = 48
TRANSFORM_FORMAT = "<3ff4f3ff"
TRANSFORM_SIZE = 48

# RenderableComponent  sizeof = 12
RENDERABLE_FORMAT = "<III"
RENDERABLE_SIZE = 12

# StreamingCellId  sizeof = 12
STREAMING_CELL_FORMAT = "<IIHH"
STREAMING_CELL_SIZE = 12

SIGNATURE_FORMAT = "<4Q"
SIGNATURE_SIZE = 32

CELL_FILE_MAGIC = b"LEONCELL"
CELL_FILE_VERSION = 1
CELL_SIZE_M = 32.0


def verify_layouts() -> None:
    assert struct.calcsize(TRAFFIC_LANE_NODE_FORMAT) == TRAFFIC_LANE_NODE_SIZE == 44
    assert struct.calcsize(BREAKABLE_WINDOW_FORMAT) == BREAKABLE_WINDOW_SIZE == 16
    assert struct.calcsize(BREAKABLE_WINDOW_POSE_FORMAT) == BREAKABLE_WINDOW_POSE_SIZE == 32
    assert struct.calcsize(POWER_GRID_NODE_FORMAT) == POWER_GRID_NODE_SIZE == 160
    assert struct.calcsize(STREET_LIGHT_FORMAT) == STREET_LIGHT_SIZE == 20
    assert struct.calcsize(WEATHER_ZONE_FORMAT) == WEATHER_ZONE_SIZE == 20
    assert struct.calcsize(TRANSFORM_FORMAT) == TRANSFORM_SIZE == 48
    assert struct.calcsize(RENDERABLE_FORMAT) == RENDERABLE_SIZE == 12
    assert struct.calcsize(STREAMING_CELL_FORMAT) == STREAMING_CELL_SIZE == 12
    assert struct.calcsize(SIGNATURE_FORMAT) == SIGNATURE_SIZE == 32
    assert struct.calcsize("<I3fff?") == 25
    assert struct.calcsize("<I3fff?xxx") == 28


def pack_traffic_lane(node_id: int, px: float, py: float, pz: float,
                      fx: float, fy: float, fz: float, speed: float,
                      nxt: int, left: int, right: int) -> bytes:
    return struct.pack(TRAFFIC_LANE_NODE_FORMAT, node_id, px, py, pz, fx, fy, fz,
                       speed, nxt, left, right)


def pack_window(mesh_intact: int, mesh_shattered: int, integrity: float, seed: int) -> bytes:
    return struct.pack(BREAKABLE_WINDOW_FORMAT, mesh_intact, mesh_shattered, integrity,
                       seed & 0xFFFF, 0)


def pack_window_pose(cx: float, cy: float, cz: float, nx: float, ny: float, nz: float,
                     hw: float, hh: float) -> bytes:
    return struct.pack(BREAKABLE_WINDOW_POSE_FORMAT, cx, cy, cz, nx, ny, nz, hw, hh)


def pack_power_node(node_id: int, px: float, py: float, pz: float, load: float,
                    cap: float, operational: bool, lights: list[int], count: int) -> bytes:
    padded = (list(lights[:32]) + [0] * 32)[:32]
    return struct.pack(POWER_GRID_NODE_FORMAT, node_id, px, py, pz, load, cap,
                       bool(operational), *padded, count)


def pack_street_light(handle: int, grid_id: int, flicker: float, voltage: float,
                      wear: float) -> bytes:
    return struct.pack(STREET_LIGHT_FORMAT, handle, grid_id, flicker, voltage, wear)


def pack_weather(cell_id: int, rain: float, wx: float, wz: float, temp: float) -> bytes:
    return struct.pack(WEATHER_ZONE_FORMAT, cell_id, rain, wx, wz, temp)


def pack_transform(px: float, py: float, pz: float, qx: float, qy: float, qz: float, qw: float,
                   sx: float, sy: float, sz: float) -> bytes:
    return struct.pack(TRANSFORM_FORMAT, px, py, pz, 0.0, qx, qy, qz, qw, sx, sy, sz, 0.0)


def pack_renderable(mesh_id: int, material_id: int, transform_id: int) -> bytes:
    return struct.pack(RENDERABLE_FORMAT, mesh_id, material_id, transform_id)


def pack_streaming_cell(cx: int, cy: int, lod: int = 0) -> bytes:
    return struct.pack(STREAMING_CELL_FORMAT, cx, cy, lod, 0)


def cell_index(x_m: float, z_m: float, cell_size: float = CELL_SIZE_M) -> tuple[int, int]:
    return int(max(0.0, x_m) // cell_size), int(max(0.0, z_m) // cell_size)
