"""Transformers at arterial intersections + street lights along every curb."""

from __future__ import annotations

from dataclasses import dataclass, field

import numpy as np

from binary_layout import pack_power_node, pack_street_light, cell_index
from road_network import ARTERIAL_EVERY_M, MINOR_EVERY_M, RoadNetworkGenerator

LIGHT_SPACING_M = 30.0


@dataclass
class PowerGridNode:
    node_id: int
    x: float
    y: float
    z: float
    load: float
    capacity: float
    operational: bool
    lights: list[int] = field(default_factory=list)


@dataclass
class StreetLight:
    handle: int
    power_grid_node_id: int
    x: float
    y: float
    z: float
    flicker: float
    voltage: float
    wear: float


class PowerGridGenerator:
    def __init__(self, road_network: RoadNetworkGenerator, seed: int):
        self.roads = road_network
        self.rng = np.random.default_rng(int(seed))
        self.nodes: list[PowerGridNode] = []
        self.lights: list[StreetLight] = []

    def _nearest_transformer(self, x: float, z: float) -> PowerGridNode:
        best = self.nodes[0]
        best_d = 1e30
        for n in self.nodes:
            d = (n.x - x) ** 2 + (n.z - z) ** 2
            if d < best_d:
                best_d = d
                best = n
        return best

    def generate(self) -> tuple[list[PowerGridNode], list[StreetLight]]:
        self.nodes.clear()
        self.lights.clear()
        size = self.roads.grid_size_m
        nid = 1
        xs = np.arange(0.0, size + 0.01, MINOR_EVERY_M)
        for x in xs:
            for z in xs:
                arterial = (
                    abs(round(float(x) / ARTERIAL_EVERY_M) * ARTERIAL_EVERY_M - float(x)) < 0.51
                    and abs(round(float(z) / ARTERIAL_EVERY_M) * ARTERIAL_EVERY_M - float(z)) < 0.51
                )
                if not arterial:
                    continue
                self.nodes.append(PowerGridNode(
                    node_id=nid, x=float(x), y=6.0, z=float(z),
                    load=0.35, capacity=1.0, operational=True,
                ))
                nid += 1

        handle = 1
        coords = np.arange(0.0, size + 0.01, MINOR_EVERY_M)
        along = np.arange(0.0, size + 0.01, LIGHT_SPACING_M)
        for c in coords:
            for s in along:
                for offset in (5.0, -5.0):
                    # Horizontal curb.
                    x, z = float(s), float(c) + offset
                    parent = self._nearest_transformer(x, z)
                    light = StreetLight(
                        handle=handle, power_grid_node_id=parent.node_id,
                        x=x, y=5.5, z=z, flicker=0.02, voltage=1.0, wear=0.05,
                    )
                    self.lights.append(light)
                    if len(parent.lights) < 32:
                        parent.lights.append(handle)
                    handle += 1
                    # Vertical curb.
                    x, z = float(c) + offset, float(s)
                    parent = self._nearest_transformer(x, z)
                    light = StreetLight(
                        handle=handle, power_grid_node_id=parent.node_id,
                        x=x, y=5.5, z=z, flicker=0.02, voltage=1.0, wear=0.05,
                    )
                    self.lights.append(light)
                    if len(parent.lights) < 32:
                        parent.lights.append(handle)
                    handle += 1

        for n in self.nodes:
            n.load = min(1.0, 0.15 + 0.02 * len(n.lights))
        return self.nodes, self.lights

    def export_binary(self, filepath: str) -> int:
        pblob = bytearray()
        for n in self.nodes:
            pblob += pack_power_node(n.node_id, n.x, n.y, n.z, n.load, n.capacity,
                                     n.operational, n.lights, len(n.lights))
        lblob = bytearray()
        for L in self.lights:
            lblob += pack_street_light(L.handle, L.power_grid_node_id, L.flicker, L.voltage, L.wear)
        with open(filepath, "wb") as f:
            f.write(pblob)
        with open(filepath.replace(".bin", "_lights.bin"), "wb") as f:
            f.write(lblob)
        return len(pblob) + len(lblob)
