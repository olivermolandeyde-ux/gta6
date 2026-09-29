# Local models (not all in git)

Trees are Wavefront OBJ + MTL + PNG (drop these on the Mac):

- `oak_tree.obj` / `oak_tree.mtl` / `oak_tree_bark.png` / `oak_tree_leaves.png`
- `pine_tree.obj` / `pine_tree.mtl` / `pine_tree_bark.png` / `pine_tree_leaves.png`
- `palm_tree.obj` / `palm_tree.mtl` / `palm_tree_bark.png` / `palm_tree_leaves.png`

Lamps and downtown still use GLB:

- `skyscraper-2.glb`
- `gatelys_klassisk.glb` / `gatelys_moderne.glb`

Paths tried (first hit wins): project root `assets/models/`, current working directory, `../assets/models` (when launched from `build/`), next to `leonida_city`, and `build/assets/models/`.

CMake copies `assets/` into the build directory. GLBs stay gitignored.
