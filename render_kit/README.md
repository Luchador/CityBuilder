# Cities Unlimited Render Kit

A separate C++20/SDL3 desktop tool for turning static GLB models into four
transparent sprites with the game's orthographic camera. The renderer traces
rays on CPU worker threads; SDL displays the preview and Dear ImGui provides
the editor controls. No Blender installation or additional GPU API setup is
required.

## Build and run on Windows 11

Use the same x64 Visual Studio developer terminal that builds Cities Unlimited.
From the repository root:

```bat
run-render-kit.bat
```

The first run configures a separate Release build and downloads pinned revisions
of Dear ImGui and cgltf. Later runs build changes and launch the editor. This
build uses the existing vendored SDL and does not need the game's SDL_ttf build
or terrain textures. Release mode matters for CPU rendering performance.

Equivalent commands:

```bat
cmake -S render_kit -B build-render-kit -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-render-kit --target RenderKit
build-render-kit\RenderKit.exe
```

The executable uses the `render-kit-data` folder beside it for its font and
third-party notices. Keep that folder beside the executable when moving it.
Source: `render_kit/src`; game camera source: `src/camera.cpp` / `src/camera.hpp`.
The normal game build remains independent. To add the tool to that build tree,
configure the root with `-DCITY_BUILD_RENDER_KIT=ON` and build target `RenderKit`.

## Use the editor

1. Choose **Import GLB**, paste a path and click **Load path**, or drop a GLB onto
   the window. The active glTF scene is imported, including node transforms and
   instanced references to ordinary meshes. Embedded or referenced PNG base
   textures are read automatically.
2. Select a material in the left panel. Assign **base color** and/or **opacity**
   PNGs. Dropping a PNG assigns it as the selected material's base color.
3. Choose **Opaque** or **Alpha cut-out**. For cut-out, adjust **Cut-off** from
   0 to 1. **Double-sided** is useful for foliage cards and other thin surfaces.
4. Adjust ambient and directional light colors/strengths on the right. The
   light's world-space direction stays fixed while switching camera views.
5. Set output width/height (16..4096), an export folder, and a file prefix.
   **Fit all four views** chooses one scale that fits every rotation, with the
   requested padding. Turn it off to specify **Pixels / unit** explicitly.
6. Click **Export four views**. Progress and Cancel appear below the panels.
   Rendering works from a snapshot of the scene; exports cannot be partially
   changed by adjusting controls midway through a job.

Panels scroll on smaller windows. The preview is capped in resolution for
responsiveness; exports always use the requested full size. The preview's
**Display** selector shows the lit sprite, alpha, unlit color, or sun mask.
Texture filtering is bilinear; preview display uses nearest-neighbor scaling
so alpha edges can be inspected. This first version keeps material edits in
memory; it does not modify the source GLB or save editor projects.

## Camera and alignment

Camera orientation comes directly from the game's `GetCameraOrientation()`:

| Export | Game quarter-turn | Yaw | Pitch |
| --- | ---: | ---: | ---: |
| `sprite_view0.png` | 0 | 45 degrees | 33 degrees |
| `sprite_view1.png` | 1 | 135 degrees | 33 degrees |
| `sprite_view2.png` | 2 | 225 degrees | 33 degrees |
| `sprite_view3.png` | 3 | 315 degrees | 33 degrees |

Increasing the view number matches the game's clockwise camera rotation.
Geometry and light stay in world space; only the camera rotates. glTF's Y-up
coordinates are kept. There is no perspective foreshortening.

All four images share one frame center, world-space scale, width and height.
They are not independently cropped. A ground pivot is placed at the horizontal
center of the model's bounds and its minimum Y. `sprites.json` records this
pivot's image coordinates, the frame center, pixel scale and camera bases.
When importing sprites into the game, use that pivot rather than the image's
bottom-center. One model unit is treated as one game tile; author models at
the desired scale or use the recorded pixels-per-model-unit when placing them.

## Materials and alpha

Opaque materials always count as hits, even if their texture has transparent
pixels. Cut-out opacity is:

```
baseColorTexture.alpha * baseColorFactor.alpha * vertexColor.alpha * opacityMap
```

Missing textures contribute 1. The opacity map defaults to grayscale/luminance;
choose **Alpha** for a PNG whose mask is in its alpha channel, or **Red** for a
single-channel mask. Opacity data is sampled without gamma correction. The
opacity map uses the material's selected UV set/transform/wrap settings, like
the base-color map. UV sets 0 and 1 and `KHR_texture_transform` are supported.

A sample below the cutoff is discarded. The ray continues to other triangles
behind it, so windows, leaves, fences and stacked cut-out surfaces work. Cut-out
also affects shadow rays. Surviving samples have alpha **255** (white); rays
that miss have alpha **0** (black), including misses through cut-out holes.
Dark or unlit model surfaces remain opaque. There are no fractional-alpha
antialiased edge pixels. RGB is straight/unassociated, with transparent-black
background pixels. No ground plane or its shadow is included in the output.

Imported `MASK` becomes cut-out; imported `BLEND` is converted to cut-out with
an import note. Semitransparent glass is intentionally outside this binary-alpha
workflow. Base-color factors and vertex colors are multiplied in linear space,
then converted to sRGB before applying the game's display-space light response.

## Fixed lighting and future day/night rendering

Defaults match the current game's `WorldBrightness` convention:

- Direction toward the light: `(-0.75, 0.5, 0.25)`.
- Ambient strength: `0.55`; directional strength: `1.0`.
- White ambient and directional colors; brightness divisor: `0.89`.

The vector retains the game's magnitude for diffuse intensity. Only its
normalized direction is used for shadow rays. Direct light supports hard
self-shadows. Ambient is uniform; there is no ambient occlusion, environment
lighting, specular reflection, emission or bounce lighting.

**Export day/night layers** additionally writes, for every view:

- `*_albedo.png`: unlit surface color (sRGB).
- `*_sun.png`: grayscale directional response, including cut-out/self-shadow
  occlusion. This is linear data, not an sRGB color texture.

Both layers carry exactly the same binary alpha as the lit sprite. To reproduce
the current game's lighting response, the future sprite shader can calculate:

```
displayRGB = albedo_sRGB *
    (ambientColor * ambientIntensity + sunColor * sunIntensity * sunMask) / 0.89
```

Clamp for display. The layers allow changing light colors and intensities
without rerendering while its direction stays fixed. A prelit sprite alone
cannot independently change those two light contributions. This patch supplies
the layers and metadata; it does not yet add building sprite rendering to the
game. If the game's lighting formula changes later, keep `render.hpp` and this
formula synchronized. Camera rotation/pitch are already shared source.

## Exports

Each export creates a new numbered subfolder, e.g. `sprite_001`, containing the
four PNGs, optional layers, and `sprites.json`. Existing exports are preserved.
Cancel or a save failure removes the newly created incomplete batch. Prefixes
accept letters, numbers, underscores and hyphens. Choose an existing parent
folder using the folder dialog or the path field.

For default-material command-line exports without opening the editor:

```bat
build-render-kit\RenderKit.exe --render building.glb C:\Sprites 1024 1024
```

This uses imported material settings, default lighting, auto-fit and day/night
layers. It creates another numbered `sprite_###` folder. You can also launch
`RenderKit.exe building.glb` to open a model in the editor.

## Import scope

Supports indexed/non-indexed triangles, triangle strips/fans, nested node
transforms, ordinary mesh instances, inverse-transpose normals, mirrored
transforms, vertex colors, sparse/normalized accessors, PNGs and common texture
wrapping. Flat normals are generated when normals are absent. Material names
and per-primitive assignments are retained.

Use a static, uncompressed glTF 2.0 GLB. Bake skins, morph targets and GPU mesh
instancing before export. Animation tracks are ignored with an import note;
the default node transforms are rendered. Unsupported required extensions are
reported. JPEG/KTX/WebP images are not decoded; assign a PNG replacement in the
materials panel. PBR normal/metallic/roughness/emissive textures are not used by
this diffuse sprite renderer. Imported cameras/lights are replaced by the game
camera and the Render Kit's fixed lighting.

## Checks

```bat
cmake -S render_kit -B build-render-kit -DRENDER_KIT_BUILD_TESTS=ON
cmake --build build-render-kit --target RenderKitTests
ctest --test-dir build-render-kit --output-on-failure
```

Tests cover GLB transforms/active scenes/embedded PNGs, mirrored normals, binary
alpha, cutoff changes, multiple cut-out layers, camera fitting, reconstruction
from lighting layers, PNG roundtrips and cancellation/overwrite protection.

The standalone project also supports an installed SDL3 via
`-DRENDER_KIT_USE_INSTALLED_SDL=ON`. A window smoke test for development is
`RenderKit --smoke-test model.glb editor-capture.png`; it loads and renders a
preview, captures the editor, then exits.

Dependencies: [SDL3](https://github.com/libsdl-org/SDL),
[Dear ImGui](https://github.com/ocornut/imgui),
[cgltf](https://github.com/jkuhlmann/cgltf). Their revisions/licenses are included
in the build setup; generated sprites have no dependency on the editor UI.
