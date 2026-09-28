#!/usr/bin/env python3
"""Orchestrate road / building / power / weather export into Phase-6 .cell blobs."""

from __future__ import annotations

import argparse
import math
import os
import struct
import sys
from collections import defaultdict

from binary_layout import (
    CELL_FILE_MAGIC,
    CELL_FILE_VERSION,
    CELL_SIZE_M,
    cell_index,
    pack_renderable,
    pack_streaming_cell,
    pack_traffic_lane,
    pack_transform,
    pack_window,
    pack_window_pose,
    pack_power_node,
    pack_street_light,
    pack_weather,
    verify_layouts,
)
from building_placer import BuildingPlacer
from power_grid import PowerGridGenerator
from road_network import RoadNetworkGenerator
from weather_zones import WeatherZoneGenerator


def write_cell(path: str, cx: int, cy: int, lanes, buildings, windows, lights, powers, weathers) -> int:
    body = bytearray()
    body += struct.pack("<I", len(lanes))
    for n in lanes:
        body += pack_traffic_lane(n.node_id, n.x, n.y, n.z, n.fx, n.fy, n.fz,
                                  n.speed, n.next_id, n.left_id, n.right_id)
    body += struct.pack("<I", len(buildings))
    for b in buildings:
        half = 0.5 * b.yaw
        qy = math.sin(half)
        qw = math.cos(half)
        body += pack_transform(b.px, b.py, b.pz, 0.0, qy, 0.0, qw, b.sx, b.sy, b.sz)
        body += pack_renderable(b.mesh_id, b.material_id, b.transform_id)
        body += pack_streaming_cell(cx, cy, 0)
    body += struct.pack("<I", len(windows))
    for w in windows:
        body += pack_window(w.mesh_intact, w.mesh_shattered, w.integrity, w.seed)
        body += pack_window_pose(w.cx, w.cy, w.cz, w.nx, w.ny, w.nz, w.hw, w.hh)
    body += struct.pack("<I", len(lights))
    for L in lights:
        body += pack_street_light(L.handle, L.power_grid_node_id, L.flicker, L.voltage, L.wear)
    body += struct.pack("<I", len(powers))
    for p in powers:
        body += pack_power_node(p.node_id, p.x, p.y, p.z, p.load, p.capacity,
                                p.operational, p.lights, len(p.lights))
    body += struct.pack("<I", len(weathers))
    for z in weathers:
        body += pack_weather(z.cell_id, z.rain, z.wind_x, z.wind_z, z.temp_c)

    header = CELL_FILE_MAGIC + struct.pack("<IIII", CELL_FILE_VERSION, cx, cy, len(body))
    blob = header + body
    with open(path, "wb") as f:
        f.write(blob)
    return len(blob)


def main() -> int:
    parser = argparse.ArgumentParser(description="Leonida procedural city generator")
    parser.add_argument("--size-km", type=float, default=2.0)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--coast", type=str, default="west")
    parser.add_argument("--out", type=str, default="output/city")
    args = parser.parse_args()

    verify_layouts()

    out = args.out
    os.makedirs(os.path.join(out, "cells"), exist_ok=True)

    print(f"[citygen] {args.size_km:.1f} km  seed={args.seed}  coast={args.coast}")

    roads = RoadNetworkGenerator(args.size_km, args.seed)
    lanes = roads.generate()
    print(f"[citygen] road nodes: {len(lanes)}")

    placer = BuildingPlacer(roads, args.seed + 1)
    buildings = placer.generate_buildings()
    windows = [w for b in buildings for w in b.windows]
    print(f"[citygen] buildings: {len(buildings)}  windows: {len(windows)}")

    grid = PowerGridGenerator(roads, args.seed + 2)
    powers, lights = grid.generate()
    print(f"[citygen] transformers: {len(powers)}  street lights: {len(lights)}")

    weather = WeatherZoneGenerator(args.size_km, args.coast, args.seed + 3)
    zones = weather.generate()
    print(f"[citygen] weather zones: {len(zones)}")

    lane_bytes = roads.export_binary(os.path.join(out, "lanes.bin"))
    bld_bytes = placer.export_binary(os.path.join(out, "buildings.bin"))
    pwr_bytes = grid.export_binary(os.path.join(out, "power.bin"))
    wth_bytes = weather.export_binary(os.path.join(out, "weather.bin"))

    buckets: dict[tuple[int, int], dict] = defaultdict(lambda: {
        "lanes": [], "buildings": [], "windows": [], "lights": [], "powers": [], "weathers": [],
    })
    for n in lanes:
        buckets[cell_index(n.x, n.z)]["lanes"].append(n)
    for b in buildings:
        key = cell_index(b.px, b.pz)
        buckets[key]["buildings"].append(b)
        buckets[key]["windows"].extend(b.windows)
    for L in lights:
        buckets[cell_index(L.x, L.z)]["lights"].append(L)
    for p in powers:
        buckets[cell_index(p.x, p.z)]["powers"].append(p)
    for z in zones:
        buckets[(z.cell_x, z.cell_y)]["weathers"].append(z)

    cell_bytes = 0
    for (cx, cy), bun in buckets.items():
        path = os.path.join(out, "cells", f"{cx}_{cy}.cell")
        cell_bytes += write_cell(path, cx, cy, bun["lanes"], bun["buildings"], bun["windows"],
                                 bun["lights"], bun["powers"], bun["weathers"])

    n_cells = int(__import__("math").ceil(args.size_km * 1000.0 / CELL_SIZE_M)) ** 2
    total = lane_bytes + bld_bytes + pwr_bytes + wth_bytes + cell_bytes
    stats = (
        f"MICRO-PHASE 9 citygen statistics\n"
        f"  city size            : {args.size_km:.1f} km × {args.size_km:.1f} km\n"
        f"  streaming cells      : {n_cells} ({n_cells**0.5:.0f}² at {CELL_SIZE_M:.0f} m)\n"
        f"  cells with content   : {len(buckets)}\n"
        f"  road nodes           : {len(lanes)}\n"
        f"  buildings            : {len(buildings)}\n"
        f"  windows              : {len(windows)}\n"
        f"  street lights        : {len(lights)}\n"
        f"  power grid nodes     : {len(powers)}\n"
        f"  weather zones        : {len(zones)}\n"
        f"  binary bytes         : {total} ({total / (1024*1024):.2f} MiB)\n"
    )
    print(stats)
    with open(os.path.join(out, "stats.txt"), "w", encoding="utf-8") as f:
        f.write(stats)
    return 0


if __name__ == "__main__":
    sys.exit(main())
