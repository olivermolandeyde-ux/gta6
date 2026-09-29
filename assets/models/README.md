# Local tree GLBs (not in git)

Put your own models here. The city loader reads them from disk at startup:

- `oak_tree_realistic.glb`
- `pine_tree_realistic.glb`
- `palm_tree_realistic.glb`
- `skyscraper-2.glb` (downtown, ~40% of the center 4×4)
- `gatelys_klassisk.glb` / `gatelys_moderne.glb` (sidewalk lamps, 50/50)

Paths tried (first hit wins): project root `assets/models/`, current working directory, `../assets/models` (when launched from `build/`), next to `leonida_city`, and `build/assets/models/`.

These files are gitignored so 4.5 MB assets stay on your Mac.
