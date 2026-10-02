"""Lot / facade placement along the generated street grid."""

from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

from binary_layout import (
    pack_transform,
    pack_renderable,
    pack_window,
    pack_window_pose,
    cell_index,
)
from road_network import ARTERIAL_EVERY_M, MINOR_EVERY_M, RoadNetworkGenerator


@dataclass
class WindowPrefab:
    mesh_intact: int
    mesh_shattered: int
    integrity: float
    seed: int
    cx: float
    cy: float
    cz: float
    nx: float
    ny: float
    nz: float
    hw: float
    hh: float


@dataclass
class BuildingPrefab:
    transform_id: int
    px: float
    py: float
    pz: float
    yaw: float
    sx: float
    sy: float
    sz: float
    mesh_id: int
    material_id: int
    commercial: bool
    windows: list[WindowPrefab] = field(default_factory=list)


class BuildingPlacer:
    def __init__(self, road_network: RoadNetworkGenerator, seed: int):
        self.roads = road_network
        self.rng = np.random.default_rng(int(seed))
        self.buildings: list[BuildingPrefab] = []

    def _windows_for_facade(self, px: float, pz: float, width: float, height: float,
                            nx: float, nz: float, seed0: int) -> list[WindowPrefab]:
        windows: list[WindowPrefab] = []
        spacing = 3.0
        floors = 4 if height > 12.0 else 2
        n_across = max(1, int(width / spacing))
        hw, hh = 0.6, 0.7
        for floor in range(floors):
            wy = 1.2 + floor * 3.2
            for i in range(n_across):
                t = (i + 0.5) / n_across
                # Tangent along facade (perp to outward normal).
                tx, tz = -nz, nx
                fx = px + tx * (t - 0.5) * width + nx * 0.15
                fz = pz + tz * (t - 0.5) * width + nz * 0.15
                windows.append(WindowPrefab(
                    mesh_intact=100, mesh_shattered=101, integrity=1.0,
                    seed=(seed0 + floor * 17 + i) & 0xFFFF,
                    cx=fx, cy=wy, cz=fz, nx=nx, ny=0.0, nz=nz, hw=hw, hh=hh,
                ))
        return windows

    def generate_buildings(self) -> list[BuildingPrefab]:
        self.buildings.clear()
        size = self.roads.grid_size_m
        block = MINOR_EVERY_M
        tid = 1
        xs = np.arange(0.0, size - block + 0.01, block)
        for x0 in xs:
            for z0 in xs:
                arterial = (
                    abs(round(x0 / ARTERIAL_EVERY_M) * ARTERIAL_EVERY_M - x0) < 0.51
                    or abs(round(z0 / ARTERIAL_EVERY_M) * ARTERIAL_EVERY_M - z0) < 0.51
                )
                if arterial:
                    lot_w, lot_d, height = 50.0, 50.0, 18.0
                    mesh, mat = 200, 7
                    commercial = True
                    stride = 55.0
                else:
                    lot_w, lot_d, height = 20.0, 30.0, 8.0
                    mesh, mat = 201, 3
                    commercial = False
                    stride = 25.0

                # South face of the block (along +X road at z0), setback 8 m into the lot.
                cursor = 8.0
                while cursor + lot_w < block - 8.0:
                    px = float(x0) + cursor + lot_w * 0.5
                    pz = float(z0) + 8.0 + lot_d * 0.5
                    b = BuildingPrefab(
                        transform_id=tid, px=px, py=0.0, pz=pz, yaw=0.0,
                        sx=lot_w, sy=height, sz=lot_d, mesh_id=mesh, material_id=mat,
                        commercial=commercial,
                    )
                    b.windows = self._windows_for_facade(px, float(z0) + 8.0, lot_w, height,
                                                         0.0, -1.0, tid * 13)
                    self.buildings.append(b)
                    tid += 1
                    cursor += stride

                # West face (along +Z road at x0).
                cursor = 8.0
                while cursor + lot_w < block - 8.0:
                    px = float(x0) + 8.0 + lot_d * 0.5
                    pz = float(z0) + cursor + lot_w * 0.5
                    b = BuildingPrefab(
                        transform_id=tid, px=px, py=0.0, pz=pz, yaw=1.5707963,
                        sx=lot_w, sy=height, sz=lot_d, mesh_id=mesh, material_id=mat,
                        commercial=commercial,
                    )
                    b.windows = self._windows_for_facade(float(x0) + 8.0, pz, lot_w, height,
                                                         -1.0, 0.0, tid * 17)
                    self.buildings.append(b)
                    tid += 1
                    cursor += stride
        return self.buildings

    def export_binary(self, filepath: str) -> int:
        blob = bytearray()
        for b in self.buildings:
            qw = 1.0
            qx = qy = qz = 0.0
            # Yaw around Y: q = (0, sin(yaw/2), 0, cos(yaw/2))
            half = 0.5 * b.yaw
            qy = float(np.sin(half))
            qw = float(np.cos(half))
            blob += pack_transform(b.px, b.py, b.pz, qx, qy, qz, qw, b.sx, b.sy, b.sz)
            blob += pack_renderable(b.mesh_id, b.material_id, b.transform_id)
            blob += pack_window(0, 0, 0.0, len(b.windows))  # count sentinel unused
        with open(filepath, "wb") as f:
            f.write(blob)
        path_w = filepath.replace(".bin", "_windows.bin")
        wblob = bytearray()
        for b in self.buildings:
            for w in b.windows:
                wblob += pack_window(w.mesh_intact, w.mesh_shattered, w.integrity, w.seed)
                wblob += pack_window_pose(w.cx, w.cy, w.cz, w.nx, w.ny, w.nz, w.hw, w.hh)
        with open(path_w, "wb") as f:
            f.write(wblob)
        return len(blob) + len(wblob)

    def buildings_in_cell(self, cx: int, cy: int) -> list[BuildingPrefab]:
        out = []
        for b in self.buildings:
            icx, icy = cell_index(b.px, b.pz)
            if icx == cx and icy == cy:
                out.append(b)
        return out
