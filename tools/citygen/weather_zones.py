"""Per-streaming-cell weather. West coast wetter; downtown heat island."""

from __future__ import annotations

from dataclasses import dataclass

import numpy as np

from binary_layout import CELL_SIZE_M, pack_weather


@dataclass
class WeatherZone:
    cell_id: int
    cell_x: int
    cell_y: int
    rain: float
    wind_x: float
    wind_z: float
    temp_c: float


class WeatherZoneGenerator:
    def __init__(self, grid_size_km: float, coastline_direction: str, seed: int):
        self.grid_size_m = float(grid_size_km) * 1000.0
        self.coast = coastline_direction.lower()
        self.rng = np.random.default_rng(int(seed))
        self.zones: list[WeatherZone] = []

    def generate(self) -> list[WeatherZone]:
        self.zones.clear()
        n = int(np.ceil(self.grid_size_m / CELL_SIZE_M))
        cx0 = (n - 1) * 0.5
        cy0 = (n - 1) * 0.5
        for cy in range(n):
            for cx in range(n):
                nx = cx / max(1, n - 1)
                nz = cy / max(1, n - 1)
                if self.coast in ("west", "w"):
                    rain = 0.85 - 0.55 * nx
                elif self.coast in ("east", "e"):
                    rain = 0.30 + 0.55 * nx
                elif self.coast in ("south", "s"):
                    rain = 0.85 - 0.55 * nz
                else:
                    rain = 0.30 + 0.55 * nz
                rain = float(np.clip(rain + self.rng.normal(0.0, 0.04), 0.0, 1.0))
                dx = (cx - cx0) / max(1.0, cx0)
                dy = (cy - cy0) / max(1.0, cy0)
                downtown = float(np.exp(-3.0 * (dx * dx + dy * dy)))
                temp = 18.0 + 7.0 * downtown - 4.0 * rain
                wind_x = 2.0 + 6.0 * rain
                wind_z = 1.0 * (1.0 - downtown)
                cell_id = (cx << 16) | cy
                self.zones.append(WeatherZone(
                    cell_id=cell_id, cell_x=cx, cell_y=cy,
                    rain=rain, wind_x=wind_x, wind_z=wind_z, temp_c=temp,
                ))
        return self.zones

    def export_binary(self, filepath: str) -> int:
        blob = bytearray()
        for z in self.zones:
            blob += pack_weather(z.cell_id, z.rain, z.wind_x, z.wind_z, z.temp_c)
        with open(filepath, "wb") as f:
            f.write(blob)
        return len(blob)
