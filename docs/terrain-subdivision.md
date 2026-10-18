# Nine quads per terrain tile

The logical world is still 256 by 256 tiles. `TerrainHeightmap` still holds the
257 by 257 shared corner heights, including the existing elevation increments.

`BuildTerrain` now puts render vertices at 0%, 10%, 90%, and 100% along each tile
axis. That produces one large center quad, four narrow edge quads, and four small
corner quads. Each quad is rendered as two triangles. Adjacent tiles share their
edge vertices, so their positions, colors, and normals agree at the join.

## Appearance

The four inset vertices use the tile's average slope for their lighting normal.
An edge vertex averages the two adjacent tiles' slopes; a corner averages up to
four. Lighting therefore blends across the narrow border while the large center
has a more consistent appearance. Flat neighboring tiles remain flat.

This is shading-based smoothing. The inserted heights lie on the original two
terrain triangles, retaining the existing slopes, stepped corner heights, lake
shorelines, and map outline. It does not flatten tile centers or round the
physical silhouette. Those would require changing the surface-height model as a
separate step.

The inset offsets are mirrored, so the small triangle diagonals line up with the
original tile diagonal. This lets the water keep one quad per wet logical tile
and lets the rock skirt keep its straight segments without gaps. The G-key grid
also keeps one line per logical edge rather than exposing all subdivisions.

## Adjust the border

In `src/terrain.hpp`, change:

```cpp
constexpr float TerrainTileBorder = 0.10f;
```

The value is the width on **each** side of a tile. `0.10f` gives a center 80% of a
tile wide. `0.15f` gives a 70% center and broader lighting transitions. Values
must be greater than 0 and less than 0.5. Rebuild after changing it.

## Indexing and minimap

- Logical heightmap: `TerrainStride` and `TerrainVertexCount` (257 by 257).
- Render mesh: `TerrainMeshStride` and `TerrainMeshVertexCount` (769 by 769).
- Render vertex position on one axis: `TerrainMeshCoordinate(index)`.
- Logical corner `(x, z)` in the mesh: `(z * TerrainMeshStride + x) * TerrainSubdivisions`.

The minimap now samples the new subtriangle colors. Its water classification
still uses the original tile triangles, matching the water mesh, and future zone
overlays still have one entry per logical tile.

There are 591,361 shared terrain vertices and 1,179,648 terrain triangles. This
is nine times the old triangle count; GPU performance should be checked in the
actual game, especially at distant zoom levels. Vertex layout and shaders are
unchanged.

## Verification

The CPU tests cover triangle counts/winding, shared vertices, original surface
heights, grid/skirt/water agreement, border normal blending, and minimap colors,
shorelines, and overlays. Tests pass with AddressSanitizer/UndefinedBehaviorSanitizer;
all application C++ files pass strict syntax checks with the available SDL headers.
The Windows Direct3D 12 application was not run in this environment.

Optional Windows test commands, from the developer command prompt:

```bat
cmake -S . -B build -DCITY_BUILD_SUBDIVISION_TESTS=ON -DCITY_BUILD_MINIMAP_TESTS=ON
cmake --build build --target CitySubdivisionTests CityMinimapMapTests
ctest --test-dir build -R "CitySubdivisionTests|CityMinimapMapTests" --output-on-failure
```
