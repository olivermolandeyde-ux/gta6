"""Grid road network → TrafficLaneNodeComponent blobs.

Arterials every 500 m (33 m/s), residential every 100 m (13 m/s).
Two-way streets; arterials get dual lanes with left/right linkage.
Sidewalk chains run 6 m off centreline at 1.4 m/s.
"""

from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

from binary_layout import pack_traffic_lane, cell_index, CELL_SIZE_M

HIGHWAY_MS = 33.0
RESIDENTIAL_MS = 13.0
SIDEWALK_MS = 1.4
NODE_SPACING_M = 25.0
LANE_WIDTH_M = 3.5
SIDEWALK_OFFSET_M = 6.0
ARTERIAL_EVERY_M = 500.0
MINOR_EVERY_M = 100.0


@dataclass
class TrafficLaneNode:
    node_id: int
    x: float
    y: float
    z: float
    fx: float
    fy: float
    fz: float
    speed: float
    next_id: int = 0
    left_id: int = 0
    right_id: int = 0
    is_sidewalk: bool = False
    is_arterial: bool = False


class RoadNetworkGenerator:
    def __init__(self, grid_size_km: float, seed: int):
        self.grid_size_m = float(grid_size_km) * 1000.0
        self.seed = int(seed)
        self.rng = np.random.default_rng(self.seed)
        self.nodes: list[TrafficLaneNode] = []

    def _is_arterial(self, coord_m: float) -> bool:
        return abs(round(coord_m / ARTERIAL_EVERY_M) * ARTERIAL_EVERY_M - coord_m) < 0.51

    def _add(self, x: float, y: float, z: float, fx: float, fy: float, fz: float,
             speed: float, arterial: bool, sidewalk: bool) -> TrafficLaneNode:
        n = TrafficLaneNode(
            node_id=len(self.nodes) + 1,
            x=x, y=y, z=z, fx=fx, fy=fy, fz=fz,
            speed=speed, is_arterial=arterial, is_sidewalk=sidewalk,
        )
        self.nodes.append(n)
        return n

    def _chain(self, chain: list[TrafficLaneNode]) -> None:
        for i, n in enumerate(chain):
            n.next_id = chain[i + 1].node_id if i + 1 < len(chain) else 0

    def _emit_line(self, axis: str, fixed: float, arterial: bool) -> None:
        size = self.grid_size_m
        xs = np.arange(0.0, size + 0.01, NODE_SPACING_M)
        speed = HIGHWAY_MS if arterial else RESIDENTIAL_MS
        n_lanes = 2 if arterial else 1

        def pos_fwd(s: float, lane: int) -> tuple[float, float]:
            off = (lane + 0.5) * LANE_WIDTH_M
            if axis == "x":
                return s, fixed + off
            return fixed + off, s

        def pos_rev(s: float, lane: int) -> tuple[float, float]:
            off = (lane + 0.5) * LANE_WIDTH_M
            if axis == "x":
                return s, fixed - off
            return fixed - off, s

        for lane in range(n_lanes):
            fwd: list[TrafficLaneNode] = []
            rev: list[TrafficLaneNode] = []
            for s in xs:
                px, pz = pos_fwd(float(s), lane)
                if axis == "x":
                    fwd.append(self._add(px, 0.0, pz, 1.0, 0.0, 0.0, speed, arterial, False))
                else:
                    fwd.append(self._add(px, 0.0, pz, 0.0, 0.0, 1.0, speed, arterial, False))
            for s in xs[::-1]:
                px, pz = pos_rev(float(s), lane)
                if axis == "x":
                    rev.append(self._add(px, 0.0, pz, -1.0, 0.0, 0.0, speed, arterial, False))
                else:
                    rev.append(self._add(px, 0.0, pz, 0.0, 0.0, -1.0, speed, arterial, False))
            self._chain(fwd)
            self._chain(rev)

            if n_lanes == 2 and lane == 0:
                # Inner/outer linkage filled after both lanes exist — stored on first pass skip.
                pass

        if n_lanes == 2:
            # Relink last two fwd/rev pairs: we appended inner then outer for fwd, then inner/outer rev.
            # Easier: walk nodes we just added. Count = 2 dirs * 2 lanes * len(xs)
            count = 2 * 2 * len(xs)
            added = self.nodes[-count:]
            half = len(xs)
            # added: fwd0, rev0, fwd1, rev1 each of length half
            fwd0, rev0, fwd1, rev1 = (
                added[0:half],
                added[half:2 * half],
                added[2 * half:3 * half],
                added[3 * half:4 * half],
            )
            for a, b in zip(fwd0, fwd1):
                a.right_id = b.node_id
                b.left_id = a.node_id
            for a, b in zip(rev0, rev1):
                a.right_id = b.node_id
                b.left_id = a.node_id

        # Sidewalks both sides, full length.
        sw_f: list[TrafficLaneNode] = []
        sw_r: list[TrafficLaneNode] = []
        for s in xs:
            if axis == "x":
                sw_f.append(self._add(float(s), 0.0, fixed + SIDEWALK_OFFSET_M, 1.0, 0.0, 0.0,
                                      SIDEWALK_MS, False, True))
            else:
                sw_f.append(self._add(fixed + SIDEWALK_OFFSET_M, 0.0, float(s), 0.0, 0.0, 1.0,
                                      SIDEWALK_MS, False, True))
        for s in xs[::-1]:
            if axis == "x":
                sw_r.append(self._add(float(s), 0.0, fixed - SIDEWALK_OFFSET_M, -1.0, 0.0, 0.0,
                                      SIDEWALK_MS, False, True))
            else:
                sw_r.append(self._add(fixed - SIDEWALK_OFFSET_M, 0.0, float(s), 0.0, 0.0, -1.0,
                                      SIDEWALK_MS, False, True))
        self._chain(sw_f)
        self._chain(sw_r)

    def generate(self) -> list[TrafficLaneNode]:
        self.nodes.clear()
        size = self.grid_size_m
        coords = np.arange(0.0, size + 0.01, MINOR_EVERY_M)
        for c in coords:
            self._emit_line("x", float(c), self._is_arterial(float(c)))
            self._emit_line("z", float(c), self._is_arterial(float(c)))
        return self.nodes

    def export_binary(self, filepath: str) -> int:
        blob = bytearray()
        for n in self.nodes:
            blob += pack_traffic_lane(n.node_id, n.x, n.y, n.z, n.fx, n.fy, n.fz,
                                      n.speed, n.next_id, n.left_id, n.right_id)
        with open(filepath, "wb") as f:
            f.write(blob)
        return len(blob)

    def nodes_in_cell(self, cx: int, cy: int) -> list[TrafficLaneNode]:
        out = []
        for n in self.nodes:
            icx, icy = cell_index(n.x, n.z)
            if icx == cx and icy == cy:
                out.append(n)
        return out

    @property
    def intersections(self) -> list[tuple[float, float, bool]]:
        size = self.grid_size_m
        xs = np.arange(0.0, size + 0.01, MINOR_EVERY_M)
        pts = []
        for x in xs:
            for z in xs:
                arterial = self._is_arterial(float(x)) or self._is_arterial(float(z))
                pts.append((float(x), float(z), arterial))
        return pts
