# Terrain material

The terrain now samples a repeating base-color image and a tangent-space normal
map. It blends the base color with the existing elevation colors, then lights
each pixel using the perturbed surface normal. Terrain remains fully opaque.

## Assets and building

Keep these files in your project (they can remain ignored by Git):

```
assets/materials/ground/grass01_basecolor.png
assets/materials/ground/grass01_normal.png
```

The existing `copy_assets` build target copies the whole assets directory beside
the executable, including ignored files. No CMake asset-list change is needed.
New C++ files are discovered by the existing source glob. Build and run from
your Visual Studio developer command prompt:

```bat
cmake --build build && build\CityBuilder.exe
```

A missing or unreadable PNG produces an error containing the asset path.

## Tuning

Edit the defaults in `src/terrain_material.hpp`, then rebuild:

```cpp
float tilesPerRepeat = 4.0f;
float textureBlend = 0.5f;
float normalStrength = 0.6f;
bool flipNormalY = false;
```

| Setting | Meaning |
| --- | --- |
| `tilesPerRepeat` | Number of tiles across one complete image repeat in X and Z. `4` repeats every 4 x 4 tiles; `1` repeats on every tile. Smaller values make the grass detail smaller. Valid range: greater than 0, up to 1024. |
| `textureBlend` | `0` uses elevation colors, `1` uses the base-color image, `0.5` mixes them equally in linear color space. Valid range: 0 to 1. |
| `normalStrength` | `0` uses only the terrain mesh's normal; `1` uses the full normal-map detail. Valid range: 0 to 4. |
| `flipNormalY` | Reverses the normal map's green direction. Try `true` if the lighting makes bumps look inverted. |

To compare against untextured terrain, set both `textureBlend` and
`normalStrength` to zero. Lighting now runs per pixel, so sloping ground can
still look slightly different from the previous vertex-lit terrain.

Ambient intensity, directional intensity, and light direction remain in
`WorldBrightness()` in `src/shaders.hpp`. That function is shared by the
terrain and rock skirt. This change retains the game's existing display-space
lighting response; it does not convert the entire renderer to linear lighting.

Normal-map direction cannot reliably be inferred from the PNG alone. By
default, positive red points along increasing texture U and positive green
along increasing V. On level ground these are world +X and +Z. The normal is
transformed into a slope-aware orthonormal frame before applying world lighting.

## Code layout

- `terrain_material.hpp/.cpp`: material settings, two borrowed textures, owned
  repeating sampler, and binding the fragment resources/uniforms.
- `terrain_shaders.hpp`: world-space UVs, base-color blending, and normal mapping.
- `texture.hpp/.cpp`: reusable loading options, caching, and mipmap generation.
- `main.cpp`: loads this grass material, creates its shaders/pipeline, binds it
  before the terrain draw, and releases resources on shutdown.

The existing `Vertex` layout is unchanged. UV coordinates come from world X/Z
divided by `tilesPerRepeat`; they stay fixed as the camera pans, zooms, or turns.
The sampler repeats UVs, uses trilinear mip filtering, and requests 8x anisotropic
filtering. A 2048 x 2048 image gets 12 mip levels. Normal samples are renormalized
after filtering and strength adjustment.

The shared loader now supports:

```cpp
textures.Load("ui/button.png"); // Existing UI default: premultiplied RGBA8.
textures.Load("materials/ground/grass01_basecolor.png", {TextureContent::Color, true});
textures.Load("materials/ground/grass01_normal.png", {TextureContent::Data, true});
```

`Color` uses an sRGB GPU texture; `Data` uses a linear UNORM texture. Both retain
straight RGB values. UI images retain their previous format and premultiplication.
The cache includes the content type and mipmap setting in its key.

## Extending it later

This first material covers all terrain, including lake beds and high elevations.
Water remains its own blue surface, the skirt keeps its rock layers, and the
minimap retains its elevation/water coloring. Normal mapping changes lighting,
not geometry, collision, or silhouettes.

Additional materials can reuse this loader and shader setup. A later step can
introduce texture arrays and per-tile or per-vertex blend weights to choose
grass, rock, snow, and other surfaces without duplicating loading code.

## Verification

- C++20 syntax check of all game sources with warnings treated as errors. The
  Linux check uses a declaration-only Windows shader-compiler header; it is
  not a Windows link/build.
- Microsoft DXC compilation of terrain vertex/fragment shaders and the affected
  grid, skirt, and water vertex shaders. Checked the terrain's two texture/sampler
  bindings in space2 and its 16-byte material uniform at b0, space3.
- Upload/material test doubles under AddressSanitizer and UndefinedBehaviorSanitizer:
  color/data format and alpha handling, mip setup/order, cache separation,
  material uniforms/bindings, failed initialization, cleanup, and existing
  minimap rendering/picking behavior.
- UI visibility tests and CPU checks of slope tangent frames and color round trips.

DXC checks used Shader Model 6.0. The game still uses Windows `D3DCompile` and
Shader Model 5.1/DXBC. The actual Windows build and Direct3D 12 visual result
must be checked on your PC; no GPU rendering or fresh decode of your PNGs was
performed in this verification run.

Optional existing texture/minimap tests on Windows:

```bat
cmake -S . -B build -DCITY_BUILD_MINIMAP_TESTS=ON
cmake --build build --target CityMinimapTextureTests
ctest --test-dir build -R CityMinimapTextureTests --output-on-failure
```

For a visual check, compare normal strength 0 versus 0.6 on level ground and
hills, try all four camera rotations, then zoom out and toggle the grid and UI.
The grass pattern should stay fixed to the world, with the existing panning
graphics still available when the UI is minimized.
