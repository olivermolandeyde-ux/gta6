# Leonida citygen

```bash
# from repo root
tools/.venv/bin/python tools/citygen/master_generate.py --size-km 2 --seed 42 --out output/city
```

Writes `lanes.bin`, `buildings.bin`, `power.bin`, `weather.bin`, and `output/city/cells/{x}_{y}.cell` (LEONCELL v1) matching C++ POD layouts in `binary_layout.py`.
