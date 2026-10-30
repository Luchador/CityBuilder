# Cities Unlimited shaders

Edit shaders here, then build and restart the game using `run.bat`.
No extra shader compiler installation is required. The game still uses the
Windows SDK's `D3DCompile` and Shader Model 5.1 / DXBC at startup.

## Files

| File | Purpose |
| --- | --- |
| `terrain.vert.hlsl`, `terrain.frag.hlsl` | Terrain projection, materials, lighting, and zones |
| `grid.vert.hlsl`, `grid.frag.hlsl` | Terrain grid |
| `water.vert.hlsl`, `water.frag.hlsl` | Water surface |
| `terrain_skirt.vert.hlsl`, `terrain_skirt.frag.hlsl` | Rock skirt |
| `road.vert.hlsl`, `road.frag.hlsl` | Road meshes and tool previews |
| `ui/panel.*.hlsl` | UI panels and colored geometry |
| `ui/text.*.hlsl` | Text |
| `ui/image.*.hlsl` | Images and panning graphics |
| `ui/tool_button.frag.hlsl` | Tool button masks and overlays |
| `common/world_view.hlsli` | Shared camera projection |
| `common/world_lighting.hlsli` | Shared directional and ambient lighting |
| `common/zone_overlay.hlsli` | Zones, previews, lot outlines, and arrows |

`.vert.hlsl` files are vertex shaders; `.frag.hlsl` files are pixel/fragment
shaders. `.hlsli` files contain shared HLSL code included by other shaders.
Open the entry-point `.hlsl` file when working with a specific rendering pass.
Roads and water define their zoning texture/register bindings before including
the shared zone overlay; terrain uses its defaults.

## Build behavior

CMake expands quoted, relative `#include` directives and embeds each complete
shader in a generated C++ header under `build/shaders/generated/embedded/`.
Do not edit those generated headers or add them to Git.

The build tracks both entry-point files and their included files. An ordinary
`cmake --build build` automatically reruns generation after an edit, and only
rewrites headers whose contents changed. The executable contains the shader
sources, so a released game does not need this folder beside it. Changes take
effect after rebuilding and restarting; this does not add live shader reload.

Compiler diagnostics use `#line` directives to report the original shader
filename and line number rather than a position in the generated C++ header.

The include expander supports quoted filenames relative to the including file,
including nested includes. Keep the include graph acyclic. Macro-generated
filenames and angle-bracket includes are not supported. Includes are expanded
textually even inside conditional blocks, so all referenced files must exist;
the HLSL compiler still evaluates the conditionals and macros themselves.

## Adding a shader

1. Create a `.vert.hlsl` or `.frag.hlsl` file with a `main` entry point.
2. Add its C++ symbol and relative file path as a pair in the appropriate
   `city_embed_shader_group` call in `shaders/CMakeLists.txt`.
3. Use that symbol in the C++ rendering code and specify the matching sampler
   and uniform-buffer counts when calling `CompileShader`.

A shared `.hlsli` helper needs only a quoted `#include` from a shader that uses it;
it does not need a separate entry in the CMake list.
