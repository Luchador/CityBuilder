# Cities Unlimited Render Kit

A separate C++20/SDL3 desktop tool for turning static GLB and FBX models into four
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
of Dear ImGui, cgltf, ufbx and nlohmann/json. Later runs build changes and launch the editor. This
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

1. Choose **Import model**, paste a path and click **Load path**, or drop a GLB
   or FBX onto the window. Node transforms, static mesh instances and material
   assignments are retained. Embedded or referenced PNG base textures are
   read automatically. See the FBX notes below for axes, units and foliage.
2. Select a material in the left panel. Assign **base color** and/or **opacity**
   PNGs. Dropping a PNG assigns it as the selected material's base color.
3. Choose **Opaque** or **Alpha cut-out**. For cut-out, adjust **Cut-off** from
   0 to 1. **Double-sided** is useful for foliage cards and other thin surfaces.
4. Adjust ambient and directional light colors/strengths on the right. The
   light's world-space direction stays fixed while switching camera views.
   Enable **Bake AO into albedo** for ambient occlusion; adjust its intensity,
   radius and samples as described below.
5. Set output width/height (16..4096), an export folder, and a file prefix.
   **Fit all four views** chooses one scale that fits every rotation, with the
   requested padding. Turn it off to specify **Pixels / unit** explicitly.
6. Click **Export four views**. Progress and Cancel appear below the panels.
   Rendering works from a snapshot of the scene; exports cannot be partially
   changed by adjusting controls midway through a job.

Panels scroll on smaller windows. The preview is capped in resolution for
responsiveness; exports always use the requested full size. The preview's
**Display** selector shows the lit sprite, alpha, albedo (including baked AO), or sun mask.
Texture filtering is bilinear; preview display uses nearest-neighbor scaling
so alpha edges can be inspected. Use **Save Scene...** in the top bar to keep
those edits between sessions. Source models and textures are not modified.

## Save and load scenes (.rks)

The top bar has **Save Scene...** and **Load Scene...** buttons. Import a model
and set it up, then choose **Save Scene...** and a filename such as `oak.rks`.
The `.rks` extension is added if omitted. Choose **Load Scene...** to restore it.
You can also drop a `.rks` onto the window or launch `RenderKit.exe oak.rks`.
Saving is available after importing a model; file operations wait for any active
preview/export job to finish.

A scene stores:

- The GLB or FBX model reference.
- Each material's base-color and opacity image references, including cleared
  slots and images embedded in the source model.
- Tint, factor alpha, opaque/cut-out mode, cutoff, double-sided rendering,
  opacity channel/inversion/strength, base-alpha toggle, UV settings and wrapping.
- Ambient/directional colors and intensities, plus self-shadow settings.
- Ambient occlusion enabled state, intensity, radius, sample count and ground-plane toggle.
- Output dimensions, auto-fit/padding/manual scale, export folder, filename
  prefix and whether to export day/night layers.
- The selected preview view, display channel and material.

Scenes contain **references, not copies of model/PNG data**. Paths are relative
to the `.rks` wherever possible (absolute when the asset is on another Windows
drive). Keep assets with the scene when moving a project folder. Saving to a
new folder recalculates the references. Embedded images are read from the model
again. Replacing or editing a referenced asset affects subsequent loads.

A load is applied only after the scene and required assets are ready. Missing
models/textures, malformed files and incompatible material lists report an error
and keep the current scene. Restore a missing asset to its referenced location
or update its path in the `.rks`; there is no asset-relinking dialog yet. A missing
export folder is restored as a setting with an import note; choose an existing
folder before exporting. Changing a model's material count/names/order requires
reimporting it and saving a new scene to avoid applying settings to wrong slots.

Saves write a complete temporary file before replacing the chosen `.rks`. Load
Scene replaces the current setup; save changes you want to keep first. Scene
files use UTF-8 JSON with a format tag and version, allowing future migrations.
They do not save preview images, generated sprites, undo history or UI layout.
Older `.rks` files without AO settings still load with AO disabled. Save them
again after enabling AO to retain the new settings.
Scenes saved before the ground-plane option was added load with that option off.

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
coordinates are kept; FBX axes are converted to right-handed Y-up and units
are converted to meters. There is no perspective foreshortening.

All four images share one frame center, world-space scale, width and height.
They are not independently cropped. A ground pivot is placed at the horizontal
center of the model's bounds and its minimum Y. `sprites.json` records this
pivot's image coordinates, the frame center, pixel scale and camera bases.
When importing sprites into the game, use that pivot rather than the image's
bottom-center. One model unit is treated as one game tile; author models at
the desired scale or use the recorded pixels-per-model-unit when placing them.
For FBX, one normalized meter is one model unit (regardless of whether the
exporter stored centimeters or meters). Auto-fit adjusts framing, not model scale.

## Materials and alpha

Opaque materials always count as hits, even if their texture has transparent
pixels. By default, cut-out opacity is:

```
baseColorTexture.alpha * baseColorFactor.alpha * vertexColor.alpha * opacityMap
```

Missing textures contribute 1. The opacity map defaults to grayscale/luminance;
choose **Alpha** for a PNG whose mask is in its alpha channel, or **Red** for a
single-channel mask. Opacity data is sampled without gamma correction. The
opacity map uses the material's selected UV set/transform/wrap settings, like
the base-color map. UV sets 0 and 1 and `KHR_texture_transform` are supported
for GLB. FBX bakes each base-color and opacity texture's named UV set and
transform into slots 0 and 1, respectively. Separate imported opacity mapping
can be disabled with **Opacity uses separate UV 1** in **UV settings**.

**Use base-color alpha** lets you exclude the base PNG's alpha when a separate
mask should control the silhouette. **Map strength** scales the selected opacity
channel, then **Invert opacity** changes it to `1 - mask`. Inversion is useful
for legacy FBX transparency maps where white means transparent. Manually
assigning a new opacity PNG resets inversion/strength/channel to ordinary
white-opaque grayscale; an existing FBX opacity UV mapping is retained.

A sample below the cutoff is discarded. The ray continues to other triangles
behind it, so windows, leaves, fences and stacked cut-out surfaces work. Cut-out
also affects shadow and ambient-occlusion rays. Surviving samples have alpha **255** (white); rays
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
self-shadows. Ambient light has a uniform color; optional ambient occlusion is
baked into surface color. There is no environment lighting, specular reflection,
emission or bounce lighting.

**Export day/night layers** additionally writes, for every view:

- `*_albedo.png`: surface color with any enabled AO baked in (sRGB).
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

## Ambient occlusion

Under **Lighting**, enable **Bake AO into albedo**. AO traces a hemisphere of
rays around each visible surface point and darkens places blocked by nearby
parts of the model. It works with both GLB and FBX and respects material cutoffs,
base-color alpha, separate opacity maps and inverted masks. Occluders are tested
on both sides, so thin foliage cards can block nearby light.

- **AO intensity**: 0 bypasses AO; 1 applies its full strength. Start around
  0.5–0.75 for subtle creases and foliage overlap.
- **AO radius**: maximum distance to an occluder, in imported model units
  (meters for FBX). Small values emphasize tight contacts; larger values darken
  broader cavities and foliage clusters. It is independent of image resolution
  and auto-fit. Ctrl+click the slider to type an exact value.
- **AO samples**: 16–256 rays per visible point; default 64. Higher values reduce
  sampling steps but increase CPU rendering time, especially with dense foliage.
  Preview and export use the same deterministic sampling pattern and count.
- **Use Ground Plane**: adds an infinite horizontal AO occluder at the model's
  minimum Y, matching its ground pivot. It darkens nearby model surfaces using
  the same intensity and radius. The plane is invisible in the preview and all
  exported layers; it does not change framing, alpha or the sun mask. It defaults
  to off and only takes effect while **Bake AO into albedo** is enabled.

AO defaults to off, preserving existing scenes. It multiplies **linear base
color before sRGB encoding**, then the normal light calculation uses that color.
The **Albedo (baked AO)** preview isolates the result from directional lighting.
Both the lit PNG and `*_albedo.png` contain AO; no separate AO sprite is produced.
Alpha and the sun mask are unchanged, and the day/night formula above still
works. Since AO is baked into albedo, its darkening affects both ambient and
directional contributions and stays fixed as their colors/intensities change.
It cannot be independently adjusted in the game without rerendering the sprites.

AO sees geometry in the imported model plus the optional invisible ground plane.
It does not render a shadow onto the ground. Radius is a hard distance limit, and finite sampling
can produce visible steps; increase samples when necessary. This is local
occlusion, not global illumination or color bounce. Scene files and export
metadata record the AO settings. **Reset lighting** resets the lights only;
the AO controls remain as set.

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
`RenderKit.exe building.glb` to open a model in the editor. Both commands also
accept `.fbx`, for example:

```bat
build-render-kit\RenderKit.exe --render oak.fbx C:\Sprites 1024 1024
```

You can render a saved scene with its material, lighting and output options:

```bat
build-render-kit\RenderKit.exe --render oak.rks C:\Sprites
```

The command-line export folder overrides the saved folder. Optional width/height
arguments override the saved dimensions; the saved prefix and lighting-layer
choice are retained.

## SpeedTree / FBX workflow

1. Export a **static FBX mesh** from SpeedTree. Both binary and ASCII FBX are
   accepted. Export the intended LOD only, and disable/bake wind, growth, skins
   and deformers. The tool does not automatically choose between exported LODs.
2. Preserve material assignments and export the base-color and opacity textures
   as PNG. Keep the textures beside the FBX or in its referenced subfolders.
   Old absolute paths are a fallback; portable relative paths take priority.
3. Import the FBX. Bark normally starts opaque. Materials with base PNG alpha or
   an opacity map start as cut-out with a 0.5 cutoff and, unless explicitly
   specified otherwise, double-sided rendering for leaf cards.
4. Check leaves in the preview's **Alpha** display. If needed, assign the PNGs
   manually, select the opacity channel and adjust the cutoff. When a legacy
   transparency map uses the opposite convention, toggle **Invert opacity**.

The importer preserves supplied normals, vertex colors, face materials,
per-instance materials, geometry transforms and polygon UV seams. It generates
normals when missing and triangulates quads/polygons. FBX UVs are flipped
vertically to match the renderer's PNG convention. Ordinary mesh instances are
flattened into the static scene; hidden nodes and their children are skipped.

Diffuse/base-color factors are treated as linear multipliers. Embedded and
external PNGs are supported. Recognized PBR opacity is white-opaque; legacy
FBX transparency maps are white-transparent by default, with an import note.
A diffuse PNG also connected as legacy transparency uses its alpha just once.
A separately imported opacity image controls coverage without also multiplying
the base PNG's alpha. These choices remain editable in Materials.

SpeedTree shader graphs, `.stmat` sidecars, wind and growth are not interpreted.
Layered/procedural textures must be baked to PNG. FBX does not have a universal
alpha-cutoff or foliage-culling convention, so review the suggested settings.
Missing/unsupported images produce import notes and can be replaced manually.
Geometry with skins, morphs or vertex caches produces a clear error requiring
static export. Animation tracks otherwise use the default static node pose.

## GLB import scope

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

Tests cover GLB transforms/active scenes/embedded PNGs and binary/ASCII FBX,
Unicode paths, axis/unit conversion, geometric/mirrored transforms, per-instance
materials, external/embedded PNGs, independent opacity UVs and alpha inversion.
Both formats exercise the same binary-alpha renderer and four-view export.
The FBX tests also use small exporter fixtures from the pinned ufbx source.
Existing checks cover cutoff changes, stacked cut-out layers, camera fitting,
lighting-layer reconstruction and cancellation/overwrite protection. Scene-file
checks cover GLB/FBX save/load, all settings, embedded/assigned/cleared textures,
relative-path relocation, missing assets, invalid data and safe replacement.
AO checks cover unobstructed surfaces, overhangs, radius/intensity changes,
linear-color baking, cutouts, stable repeated renders, preserved alpha/sun masks,
four-view exports, and compatibility with scenes saved before AO was added.
Ground-plane checks compare against real floor geometry, verify radius/intensity
and transparent backgrounds, and cover scene compatibility and exported settings.

The standalone project also supports an installed SDL3 via
`-DRENDER_KIT_USE_INSTALLED_SDL=ON`. A window smoke test for development is
`RenderKit --smoke-test model.fbx editor-capture.png` (or `.glb`); it loads and renders a
preview, captures the editor, then exits.

Dependencies: [SDL3](https://github.com/libsdl-org/SDL),
[Dear ImGui](https://github.com/ocornut/imgui),
[cgltf](https://github.com/jkuhlmann/cgltf),
[ufbx](https://github.com/ufbx/ufbx),
[nlohmann/json](https://github.com/nlohmann/json). Their revisions/licenses are included
in the build setup; generated sprites have no dependency on the editor UI.
