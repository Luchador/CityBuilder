# Terrain and water on the minimap

The minimap now displays the generated world using its terrain vertex colors
and the same below-sea-level test as the water renderer. Its orientation stays
fixed while the camera rotates. Existing click-to-center behavior, panel layout,
buttons, and clipped red camera footprint continue to use the same coordinates.

## Implementation

- `minimap_map.hpp/.cpp` creates a 512 x 512 opaque RGBA image from the mesh
  returned by `BuildTerrain`. Columns represent world X; rows represent world Z.
  It samples the actual vertex colors, so changing `TerrainPalette` or the
  colors supplied to `BuildTerrain` is reflected on the minimap.
- Each world tile uses the same two triangles as terrain and water, including
  their shared diagonal. Interpolated height below `TerrainSeaLevel` produces
  `WaterColor`; exactly sea level is dry. Four subpixel samples per image pixel
  soften the shoreline. A partially submerged tile is not painted entirely blue.
- Colors show elevation without directional lighting, terrain grid lines, or
  the rock skirt. This is a topographic map of X/Z positions, so hills do not
  displace map landmarks upward or hide terrain behind them.
- `TextureManager::CreateRgba()` uploads generated images using the same
  premultiplied-alpha upload path as PNGs. Generated textures are uncached and
  returned as owning `std::unique_ptr<Texture>` objects. Destroy them before
  the GPU device; `Application` does this explicitly for the minimap texture.
- `UiMinimap::DrawMap()` draws the image with the existing UI image renderer.
  Its two triangles exactly cover the diamond used by `MapToScreen` and
  `TryScreenToMap`, including when the panel changes size.
- `Application::RebuildMinimap()` builds and uploads the image after world
  generation. Camera movement, zoom, rotation, and resizing reuse the texture.
  The image takes 1 MiB of GPU pixel storage and adds one six-vertex draw.
- Rendering restores the solid-color UI pipeline and screen uniforms before
  drawing the existing red camera outline above the map.

The map is a finite-resolution image, not an exact vector shoreline: very small
features below its sampling resolution can be softened or missed. The existing
red footprint still represents the camera's view on the sea-level reference
plane, rather than following the uneven terrain surface.

## Future zones

`BuildMinimapImage` and `RebuildMinimap` accept an optional array of
`MinimapTileOverlay`. Supply one entry for each of the 256 x 256 tiles, indexed
by `z * MapSize + x`. Opacity zero leaves the terrain visible; opacity one uses
the overlay color on dry land. Underwater samples retain `WaterColor`.

For example, a future zone system can prepare:

```cpp
std::vector<MinimapTileOverlay> overlays(MapSize * MapSize);
overlays[z * MapSize + x] = {{0.2f, 0.8f, 0.2f}, 0.85f};
RebuildMinimap(mesh, overlays);
```

No zones are created or displayed by this change. Once world editing is added,
rebuild after terrain/palette/zone changes, or mark the minimap dirty and rebuild
once after a batch of edits. Keep the mesh argument consistent with the rendered
world. There is no reason to rebuild it every frame.

## Verification

Strict C++20 syntax checks passed against the project's SDL3 headers, with a
declaration-only Windows shader compiler stub. Automated tests passed with
AddressSanitizer and UndefinedBehaviorSanitizer (leak detection disabled):

- All-water, dry sea-level, flat terrain, maximum elevation, and custom palettes.
- Vertex-color interpolation, X/Z orientation, and image borders.
- Analytical saddle and partially submerged tile cases that catch an incorrect
  bilinear height test or whole-tile water fill.
- Overlay indexing, transparency, water visibility, input validation, and
  repeatable output for identical terrain.
- Generated/PNG upload parity, alpha premultiplication, padded row layout,
  ownership, failed upload cleanup, and preserving a texture after a failed rebuild.
- Image/picking alignment at nine panel/window sizes and clipped camera
  outlines in all four rotations.

The patch was also applied to a copy containing the sound walkthrough changes
and syntax-checked there. A software preview used the actual generated pixels
and UI geometry at two panel sizes. A native Windows build and interactive
Direct3D 12 rendering still need to be checked on Windows.

To run the included focused tests from the developer command prompt:

```bat
cmake -S . -B build -DCITY_BUILD_MINIMAP_TESTS=ON
cmake --build build --target CityMinimapMapTests CityMinimapTextureTests
ctest --test-dir build -R "^CityMinimap(Map|Texture)Tests$" --output-on-failure
```

This option is independent of the project's broader `CITY_BUILD_TESTS` switch.
