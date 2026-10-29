# Baked global illumination

Enable **Lighting > Bake global illumination**. This is an optional diffuse-light
renderer; disabling it restores the existing lighting/AO renderer. Old scenes
load with GI disabled. GI-enabled `.rks` files use version 2 so older Render Kit
builds will reject them rather than silently render different lighting.

## Controls

| Setting | Meaning | Starting value |
| --- | --- | --- |
| GI strength | Scales bounced directional light in the combined sprite; does not modify the exported unit-light maps | 1 |
| GI bounces | Additional diffuse surfaces visited after the camera-visible surface | 2 |
| Bake samples | Paths per covered AA sample during export | 64 |
| Preview samples | Preview path budget, capped at Bake samples | 16 |
| GI ground plane | Invisible diffuse plane at the model's minimum Y | On |
| Ground color | Ground reflectance, chosen with an sRGB color picker and stored in linear space | Neutral gray |

Start with 2x2 anti-aliasing. GI samples and AA samples multiply: 64 paths with
2x2 AA means up to 256 diffuse paths per output pixel. More samples reduce grain;
more bounces recover light in recesses but increase bake time. There is no denoiser
in this first implementation. Preview and export share framing and AA; set both
sample counts equal when comparing their pixels. An export uses **Bake samples**.

The existing Ambient color/strength becomes sky color/strength in GI mode.
Directional color/strength controls both direct sunlight and its bounced light.
The sun and moon share the fixed world-space light direction. Changing these
colors, strengths, or GI strength recombines a completed preview without tracing
again. Changing geometry, material settings, sample count, ground settings,
shadows, view or framing requires a new bake.

AO is bypassed while GI is enabled, even if the scene has saved AO enabled. Its
settings are retained for legacy mode. This avoids multiplying occlusion into
the material and then accounting for the same obstruction again with light paths.

The Display menu exposes **Direct light**, **Indirect light** and **Sky light**.
These show the unit-white-light layers with the same coverage alpha as the lit
sprite. Preview displays are sRGB and clamp at white; the floating-point exports
retain brighter values. **GI strength** changes the combined image, not the raw
indirect-layer display.

## Output files

With **Export lighting layers** checked, each of the four camera views writes:

| File suffix | Contents |
| --- | --- |
| `.png` | Combined, display-ready, sRGB sprite using the current lighting settings; straight coverage alpha |
| `_direct.dds` | Unit-white directional-light response, including material color and self-shadows |
| `_indirect.dds` | Unit-white directional light bounced between surfaces, including material colors and color bleeding |
| `_sky.dds` | Unit-white upper-hemisphere sky response, including occlusion and diffuse bounces |
| `_albedo.png` | Surface base color without AO in GI mode; retained for inspection/other workflows |
| `_sun.png` | Direct visibility times cosine of the normalized light direction; linear grayscale data |

All views have the same framing and scale. Each export gets a new numbered folder.
With layer export unchecked, only the four combined PNGs and metadata are written.
Albedo and sun PNGs alone do **not** reconstruct a GI sprite.

The DDS files are standard DX10-header **R16G16B16A16_FLOAT** 2D textures. They
contain linear HDR RGB, **premultiplied by coverage alpha**, and one mip level.
The DDS header explicitly declares premultiplied alpha. Premultiplication allows
bilinear filtering and later mip generation without dark fringes from the
transparent background. Alpha is derived from the same 8-bit coverage used by
the PNG exports, then stored as a half-float; it represents coverage, not light
intensity or glass transparency. Bright values are not clipped to 1.

Each DDS uses approximately `width * height * 8` bytes. Three maps across four
512x512 views occupy about 24 MiB before packaging/compression. Export processes
one view at a time. The float bake buffers also increase RAM use; very large
4096x4096 bakes can use around 0.8 GiB for image buffers alone.

## Compositing in Cities Unlimited

`sprites.json` version 2 records the lighting model, per-view filenames, ground
settings, bounce/sample settings, coverage convention and reconstruction formula.
The game currently has no building-sprite consumer for these layers; this update
adds their production and inspection in Render Kit, not game-side sprite rendering.

Load DDS RGB as **linear data**, not sRGB. Convert the UI's saved light colors
from sRGB to linear once before supplying them to the shader. Conceptually:

```hlsl
// RGB from the three DDS samples is already premultiplied by alpha.
float3 color = (directSample.rgb + indirectSample.rgb * giStrength)
            * directionalColorLinear * directionalIntensity
            + skySample.rgb * skyColorLinear * skyIntensity;

return float4(color, directSample.a);
```

Use premultiplied blending: source **ONE**, destination **ONE_MINUS_SRC_ALPHA**.
Compose into a linear render target, then display-encode once. Do not multiply
by albedo, AO, or alpha a second time. Do not apply the legacy `0.89` divisor.
For HDR scenes, tone mapping belongs after the scene is composed; the Render Kit
combined PNG uses sRGB encoding and display clamping rather than a filmic curve.

For exact texel comparison to the straight-alpha PNG, first unpremultiply the
reconstructed linear RGB by alpha (when nonzero), then sRGB-encode and clamp.
Half-float storage and PNG quantization introduce small rounding differences.

Lighting changes can continuously tint and scale both sun and moon contributions
without rebaking. When the directional intensity reaches zero, direct and bounced
sunlight both disappear. Night-time illuminated windows will need a separate
emissive layer in a future update.

## Bake model and limits

This is a deterministic, cosine-weighted diffuse path tracer using the existing
BVH and material opacity tests. Secondary rays take the nearest accepted surface
hit, including backfaces of opaque cards; cut-out holes do not block them. Each
surface bounce includes a visibility ray toward the fixed sun. Sky paths escape
into a uniform upper hemisphere; the lower hemisphere is black. At the bounce
limit a blocked path contributes no further light. No arbitrary AO radius limits
GI travel distance. Samples do not depend on worker scheduling.

The directional vector is normalized in GI mode. A directly facing diffuse
surface has response equal to its base color at unit directional strength; an
unobstructed upward-facing surface has that response to a unit white sky. Light
colors, surface textures, bounce multiplication and AA averaging are all evaluated
in linear space. A legacy scene may therefore look different after opting into GI;
adjust the sky and sun strengths to taste.

The ground is an infinite diffuse plane at the model's base. It can receive light,
block secondary paths and reflect colored light back onto the model, but it is
never a camera hit and never changes framing or alpha. The Self shadows checkbox
controls visibility to the sun at both primary and bounced surfaces; diffuse
paths still encounter geometry even when that checkbox is off.

This first version supports diffuse reflection and cut-outs. It does not simulate
specular reflections, glass, leaf transmission, emission, or neighboring buildings.
Ground reflectance is a bake-time approximation of the object's eventual site.
All four camera views retain the same world-space sun direction; rotating the
building itself relative to that direction requires another bake.

## Verification

The RenderKitTests target checks analytic plane lighting, one-bounce ground/wall
lighting, colored bounce light, additional bounces, nearest backface hits, cut-outs,
coverage and AA, deterministic output, relighting without rebaking, half-float
encoding including HDR/subnormal/tie cases, four-view DDS reconstruction, scene
roundtrips/legacy defaults, invalid settings, and cancellation cleanup. Existing
legacy lighting, import, scene, AO and AA tests remain enabled.
