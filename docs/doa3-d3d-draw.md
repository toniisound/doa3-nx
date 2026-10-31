# DOA3 D3D8 3925 draw bindings

Address/offset map for the draw, render-state, texture, vertex-shader and tile
adapters. The 4928 column identifies the copied DOAXBV layout, not DOA3
facts. Evidence addresses name DOA3 disassembly. Unverified fields are not
used by the DOA3 draw path. No instruction bytes are recorded here.

## API addresses

| API | DOAXBV 4928 | DOA3 3925 | Stack bytes popped | Evidence / status |
|---|---|---|---|---|
| DrawIndexedVertices | `0x001E78B0` | `0x001B3940` | `0x0C` | Verified; primitive, count, direct index pointer; base vertex from device `+0x478` |
| DrawVerticesUP | `0x001E7750` | `0x001B3760` | `0x10` | Verified; primitive, count, vertex pointer, stride |
| DrawVertices | No adapter | `0x001B38A0` | `0x0C` | Verified; primitive, start vertex, count; no SetIndices base vertex |
| SetTexture | `0x001E43F0` | `0x001B1CC0` | `0x08` | Verified; stage, resource |
| SetVertexShader | `0x001E7170` | `0x001B45F0` | `0x04` | Verified; FVF or tagged declaration pointer |
| SetStreamSource | No adapter | `0x001B4230` | `0x0C` | Verified; stream, buffer, stride |
| SetIndices | No adapter | `0x001B1EA0` | `0x08` | Verified; resource, base vertex |
| SetRenderState_Simple | `0x001E4D80` | `0x001B2390` | `0x00` | Verified; method in ECX, value in EDX |
| SetRenderState_EdgeAntiAlias | `0x001E5080` | `0x001B2470` | `0x04` | Verified |
| SetRenderState_CullMode | `0x001E5150` | `0x001B2520` | `0x04` | Verified |
| SetRenderState_NormalizeNormals | `0x001E5200` | `0x001B25D0` | `0x04` | Verified |
| SetRenderState_TextureFactor | `0x001E5240` | `0x001B2600` | `0x04` | Verified |
| SetRenderState_FillMode | `0x001E5470` | `0x001B2800` | `0x04` | Verified |
| SetRenderState_ZEnable | `0x001E6190` | `0x001B3040` | `0x04` | Verified |
| SetRenderState_StencilEnable | `0x001E6220` | `0x001B30B0` | `0x04` | Verified |
| SetRenderState_StencilFail | `0x001E62B0` | `0x001B3130` | `0x04` | Verified |
| SetRenderState_MultiSampleAntiAlias | `0x001E6510` | `0x001B32F0` | `0x04` | Verified; `0x001B3340` is the mask setter, despite overlapping signatures |
| SetTile | `0x001E4930` | `0x001B1F30` | `0x08` | Verified; index, descriptor |
| SetPalette | `0x001E45D0` | `0x001B1E20` | `0x08` | Verified; remains generated |
| SetTransform | `0x001E36D0` | `0x001B0EC0` | `0x08` | Verified; remains generated |
| Texture_LockRect | `0x001E8090` | `0x001B4B30` | `0x14` | Verified; texture, level, locked rect, rect, flags; remains generated |
| Resource destruction helper | `0x001E7C90` | `0x001B4880` | `0x04` | Verified; temporary generated dependency for final unbind destruction |
| Resource internal AddRef | Unmapped | `0x001B4960` | `0x04` | Verified; surface-parent reference rule |

Only the first 18 APIs are newly manual. Draw calls replace their library
bodies wholesale. DrawVertices and DrawVerticesUP share the indexed presenter
path using sequential host indices. The DOA3 path supports fixed-function
single-stream layouts and the verified programmable water layout described
below. Other declarations and advanced materials remain unverified.
SetTransform, SetPalette, resource creation/destruction and unused state
setters remain generated. The resource-destruction seam is also marked
beside its dispatch in `d3d_texture_adapter.c`.

## Device and resource fields

| Field | DOAXBV 4928 | DOA3 3925 | DOA3 evidence |
|---|---|---|---|
| Device global | `0x001F2978` | `0x001C3390` | `0x001B1CC8`, `0x001B45FA`, `0x001B3948` |
| Static device | `0x001F3120` | `0x001C0800` | `0x001B4EC0` |
| Dirty flags | `0x001F2984` | device `+0x008` (`0x001C0808`) | `0x001B1D32`, `0x001B4626` |
| Current render target | `+0x21B4` | `+0x40C` | `0x001B1350`, `0x001B3390` |
| Current depth surface | `+0x21B8` | `+0x410` | `0x001B3055` |
| Back buffer | pointer at `+0x21C0` | embedded at `+0x2150`, stride `0x18` | `0x001B15B0`, `0x001B4CC0` |
| Texture slots | `+0xB38 + 4*stage` | `+0xBA0 + 4*stage` | `0x001B1CCE`, `0x001B1D03` |
| Palette slots | `+0xB48 + 4*stage` | `+0xBB0 + 4*stage` | `0x001B1E2D`, `0x001B1E65` |
| Texture format cache | `+0x00C + 4*stage` | `+0x4DC + 4*stage` | `0x001B1DC2`, `0x001B1DDE` |
| Texture enable cache | Unused by adapter | `+0x4CC + 4*stage` | `0x001B1DD3`, `0x001B6620` |
| Stream stride | `0x001F2E20 + 12*stream` | `0x001C05C8 + 12*stream` | `0x001B4281`, `0x001B42AA` |
| Stream buffer | `0x001F2E28 + 12*stream` | `0x001C05D0 + 12*stream` | `0x001B4251`, `0x001B42A4` |
| Index resource | Unused by adapter | `+0x47C` | `0x001B1ECA`, `0x001B1F1C` |
| Base vertex | Unused by adapter | `+0x478` | `0x001B1F23`, `0x001B394E` |
| Index data cache | Unused by adapter | `0x001C017C` | `0x001B1EB9` |
| Declaration pointer | `+0x380` | `+0x470` | `0x001B4600`, `0x001B4636` |
| Shader/FVF handle | `+0x384` | `+0x474` | `0x001B463C` |
| Separate vertex-program field | `+0x388` | Unverified; **not** `+0x478` | `+0x478` is base vertex |
| Default FVF declaration | `0x001F2FF8` | `0x001C0688` | `0x001B4612` |
| Transform base/stride | `+0x810`, `0x40` | `+0x880`, `0x40` | `0x001B0ED1`-`0x001B0EE0` |
| View / projection / world slots | `0`, `1`, `6` | `0`, `1`, `6` | view in `0x001B729F`; projection in `0x001B0EEE`; world in `0x001B74A0` |
| Tile array/stride | `+0x2260`, `0x18` | `+0x21BC`, `0x18` | `0x001B1F6D`, `0x001B1FD1` |
| Resource common/data/lock | `+0`, `+4`, `+8` | `+0`, `+4`, `+8` | `0x001B1CDD`-`0x001B1CEB`, `0x001B4CC0` |
| Resource lock value | adapter writes device `+0x30` address | value at device `+0x1C` | `0x001B1CDD`, `0x001B425B` |
| Resource format/size | `+0xC`, `+0x10` | `+0xC`, `+0x10` | `0x001B4CD9`, `0x001B4CE4`, `0x001B9F40` |
| Surface parent | `+0x14` | `+0x14` | `0x001B497E` |
| Format descriptor table | `0x001F16B8` | `0x001BEF20` | `0x001B11EA`, `0x001BA0BF` |

The surface decoder's field positions match 3925. `0x001B9F40` verifies the
width/height fields and linear pitch; `0x001B11B0` verifies pitch and format
lookup; `0x001BA090` verifies render-target/depth descriptor flags. Surface
objects occupy `0x18` bytes. Texture unbind does not overwrite the format cache
in 3925. Tile descriptors occupy six words; words at `+0x10` and `+0x14` are
cleared for ordinary, non-depth-compressed tiles (`0x001B1F54`-`0x001B1F62`).

The FVF builder at `0x001B3BB0` clears `0x16C` bytes. Declaration flags remain
at `+4`; position/weights/normal/diffuse/specular offset-and-format pairs move
from `+0x18/+0x1C` with stride `0x10` to `+0x2C/+0x30`. Texture pairs move
from `+0xA8/+0xAC` to `+0x12C/+0x130`; coordinate sizes are separate words at
`+0x18 + 4*stage`, and count is at `+0xC`. This differs from 4928's packed
coordinate sizes at `+0x10`. The adapter translates the existing FVF model
into this layout; it does not reinterpret 4928 declaration memory as 3925.

## Render-state shadows

| State | DOAXBV 4928 | DOA3 3925 | Evidence |
|---|---|---|---|
| Color mask | `0x001F2C94` | `0x001C048C` | `0x001B23E0`, method table `0x001ED338`, state base `0x001C0380` |
| Alpha blend source/destination | `0x001F2C80`, `0x001F2C84` | `0x001C0478`, `0x001C047C` | same setter and method table |
| Blend equation | `0x001F2CB4` | `0x001C04A8` | same setter and method table; state order differs |
| Vertex blend | `0x001F2DAC` | `0x001C0554` | `0x001B2918` |
| Fill mode | `0x001F2DB4` | `0x001C055C` | `0x001B2834` |
| Two-sided lighting | `0x001F2DBC` | `0x001C0564` | `0x001B280D`, `0x001B720B` |
| Normalize normals | `0x001F2DC0` | `0x001C0568` | `0x001B25F3`; dirty bit differs |
| Z enable | `0x001F2DC4` | `0x001C056C` | `0x001B307E` |
| Stencil enable | `0x001F2DC8` | `0x001C0570` | `0x001B3126` |
| Stencil fail | `0x001F2DCC` | `0x001C0574` | `0x001B318F` |
| Cull mode | `0x001F2DD4` | `0x001C057C` | `0x001B2543` |
| Texture factor | `0x001F2DD8` | `0x001C0580` | `0x001B2636` |
| Edge antialias | `0x001F2DE4` | `0x001C058C` | `0x001B2492` |
| Multisample antialias | `0x001F2DE8` | `0x001C0590` | `0x001B32FD` |
| Lighting | `0x001F2D20` | `0x001C04F0` | `0x001B71B8`; read by the DOA3 lighting path |
| Specular enable | Unverified | `0x001C04F4` | `0x001B7180` |
| Local viewer | Unverified | `0x001C04F8` | `0x001B7180` (light control bit 16) |
| Color vertex | `0x001F2D2C` | `0x001C04FC` | `0x001B5F30`, followed by the four material sources |
| Ambient | `0x001F2D54` | `0x001C0524` | `0x001B6EA0`; back ambient at `0x001C0520` |
| Fog enable / table mode | Unverified | `0x001C04C8`, `0x001C04CC` | `FUN_001B6810` programs them; the host reads mode, start, end, density and the color at `0x001C0558` |
| Point size, min, sprite enable, scale enable, scale A/B/C, max | Unverified | `0x001C0528`-`0x001C0544` | `D3D_LazySetPointParams` (`0x001B6120`); Japan 4134 keeps them after AMBIENT (`0x001EB118`) |

## Texture-stage fields

Stage stride remains `0x80`, but the state ordering changes. The 3925 base is
`0x001C0180`, verified by `0x001B2440` and `0x001B2AF0`.

| Field, stage zero unless stated | DOAXBV 4928 | DOA3 3925 | Evidence |
|---|---|---|---|
| Address U/V | `0x001F2988`, `0x001F298C` | `0x001C01A8`, `0x001C01AC` | `0x001B6636`-`0x001B6674` packs U/V/W from relative `-4/0/+4` |
| Color operation | `0x001F29B8` | `0x001C0180` | `0x001BCC28`, `0x001B675D` |
| Color argument 1/2 | `0x001F29C0`, `0x001F29C4` | `0x001C0188`, `0x001C018C` | `0x001BCCA5`, `0x001BCCB9`, `0x001BC7BA`, `0x001BC7F9` |
| Alpha operation | `0x001F29C8` | `0x001C0190` | `0x001BC2D4` |
| Alpha argument 1/2 | `0x001F29D0`, `0x001F29D4` | `0x001C0198`, `0x001C019C` | `0x001BBFB6`, `0x001BBFF3` |
| Stage 1 argument block | `0x001F2A38` | `0x001C0200` | same stage stride and argument readers |
| Stage 1 coordinate index | `0x001F2A78` | `0x001C0270` | `0x001B293F`, stage stride |
| Texture transform flags | `0x001F29DC` | `0x001C01A4` | `0x00156300` sets state 9 to COUNT2 beside each generated coordinate index |
| Coordinate index | `0x001F29F8` | `0x001C01F0` | `0x001B2930`; `0x30000` programs NV2A reflection-map texgen |
| Stage 2 color operation | `0x001F2AB8` | `0x001C0280` | `0x001BCC15`-`0x001BCC28` |

## Additional copied draw reads

The DOA3 programmable draw path uses the verified viewport and program
fields below. Copied DOAXBV effects and game-owned diagnostics remain
inactive for DOA3.

Fixed-function lighting has its own DOA3 path, `attach_doa3_lighting`. A
census of paced fights found lighting on about 95% of fixed-function draws
with normals: always textured, never with vertex color, two to four point and
directional lights, specular on about 77% of them (material power 30-40), no
spot lights, no two-sided lighting and no local viewer. The host evaluates
D3D lighting per vertex in world space: scene ambient plus emissive, per-light
ambient and diffuse with point-light range and attenuation, and specular with
a non-local viewer. Stage 0 combines texture and lit diffuse with
SELECTARG1, SELECTARG2 or MODULATE, and specular is added after it. Spot
lights, two-sided lighting, vertex color sources and an enabled stage 1 keep
the unlit path. Every lit draw also enables table fog; four captured stages
use linear mode at draw time, with range fog off and no vertex-program
draws. The draw capture records `fog`, `fog_state` (enable, table mode,
range flag, color) and `fog_range` (start, end, density) per draw. The host applies the D3D8 table
fog formulas to the planar depth (`w`) of fixed-function draws. Range fog,
vertex-program draws and the hardware coefficient encoding at `0x0021593C` and
`0x00215938` are not modeled. `RECOMP_D3D_FOG=0` turns the host fog off.
`0x001B6810` sets NV2A planar fog (radial only with range fog) and, for
linear mode, pushes `1 + end/(end - start)` and `-1/(end - start)`. NV2A
subtracts a fixed 1 after combining them (as xemu does), which leaves the D3D
linear factor on eye-space depth. That depth equals clip `w` under the
game's projections (fourth column 0, 0, 1, 0), so the host's linear fog
matches. EXP and EXP2 scale the density by those two constants; no traced
stage uses them, so they are unchecked. A SNOW (`0x44`)
Sparring capture shows linear fog from 0 to 35 with color `0xC8C8C8`; a
frame with `RECOMP_D3D_FOG=0` shows the distant trees darker and sharper.
Fog also tints additive draws toward the fog color, as D3D8 does.

| Read | DOAXBV 4928 | DOA3 3925 | Evidence / remaining uncertainty |
|---|---|---|---|
| Pixel-shader pointer | device `+0x370` | device `+0x414` | `0x001B5A1E`, `0x001B5A24` |
| Pixel-shader state block | `0x001F2B88` | Unverified layout | `0x001B5A10` copies shader data to state base `0x001C0380`; member correspondence unverified |
| Material | device `+0xAB0` | device `+0xB18` (D3DMATERIAL8: diffuse, ambient, specular, emissive, power) | SetMaterial `0x001B100B`; `0x001B6DA0` and `0x001B6EA0` multiply it into the light and scene colors |
| Material emissive | device `+0xAE0` | device `+0xB48` | same material copy, relative `+0x30` |
| Active light list | device `+0x398` | device `+0x488` | `0x001B7180`: 0x90-byte records holding D3DLIGHT8, flags at `+0x68`, negated direction at `+0x6C`, spot terms `+0x78`-`+0x88`, next at `+0x8C` |
| Viewport | device `+0xA90` | device `+0xB00` | `0x001B199D` |
| Viewport depth range | device `+0xAA0` | device `+0xB10` | `0x001B603B`-`0x001B6057` |
| Viewport scale | device `+0x518` | device `+0x500` | `0x001B5FF7`, `0x001B601D` |
| Depth scale | device `+0x510` | device `+0x4F8` | `0x001B6047`, `0x001B6051` |
| Viewport offset | device `+0xAA8` | Computed 17/32-pixel bias, with multisample adjustment | `0x001B2BF0`; no 4928 saved-offset field reused |
| Vertex constants | device `+0xC58` for hardware index zero | device `+0xCC0` for hardware index zero; `+0x12C0` for API index zero | `0x001B46E4`, `0x001B46FC`-`0x001B4709`: API indices are biased by 96 |
| Packed vertex program | declaration `+0x114` | declaration `+0x168` | CreateVertexShader `0x001B40D0`, LoadVertexShader `0x001B42C0` |
| Camera/view probes | `0x004D56E4`, `0x004D92F0`, `0x009EEE70`, `0x009D5240`, `0x009D9A44`-`0x009D9A4B`, `0x009D5884`, `0x005DEABE`, `0x005DEAC0` | Unverified | DOAXBV game-owned addresses; inactive for DOA3 |
| TLS/camera probes | `0x003B5258`, `0x0041A800`, `0x0041A850`, `0x004D6F20` | Unverified | DOAXBV game-owned diagnostics; inactive for DOA3 |

## Upstream execution dependency

| Entry / field | DOA3 address / offset | Status |
|---|---|---|
| Main frame loop | `0x000A06D0` | Generated |
| Game fiber scheduler | `0x0009E67B` | Generated |
| Scheduler switch call | `0x0009E751` | Calls `0x00164FEF` |
| XAPI SwitchToFiber | `0x00164FEF` | Unbound; generated C returns to its caller after guest stack replacement |
| XAPI CreateFiber / DeleteFiber / ConvertThreadToFiber | `0x00164F50`, `0x00164FDC`, `0x0016502E` | Unbound; outside DRAW ownership |
| Fiber saved stack | `+0x0C` | `0x00165014`, `0x00165017` |
| TLS index | `0x00B228C8` | `0x00164FEF`, `0x0016502E` |

The scheduler's library context switch requires an API replacement. Generated
C's ordinary return cannot resume another fiber's native call stack. This is a
dependency of reaching the game's draw callers, not a reason to force game state.

| Callback recovery | DOA3 address | Lifter status |
|---|---|---|
| Initial game task | `0x00084340` | Address passed at `0x000A06BE`; closed loop without a return |
| Task loop back edge | `0x0008456E` to `0x00084430` | Lifter `d2792f3` recovers bounded, closed callback CFGs, including indexed table arms |
| Callback tail-call dependency | `0x00176DC0` to `0x00176D60` | Lifter `e0df408` scans newly recovered callback bodies for referenced gap entries |
| Second game task / tail exit | `0x00080200` / `0x0008080E` to `0x0009E525` | Lifter `5eed0de` checks conditional edges and permits an unconditional exit to a known function |
| Callback following inline switch data | `0x00050D30` | Lifter `1077fe3` recovers it after proving the preceding `0x00050A10` control flow and tables end before the callback |

The adapter check uses synthetic geometry and an in-memory presenter. Whole-game
rendering also depends on the separate XAPI fiber bindings above. No input/XAPI
implementation is changed by the DRAW commit.

## Sofdec movie frames

Game `0x0009DDB0` creates two 720x480 linear BGRA textures (format `0x12`).
`0x0009E1F0` obtains a decoded frame through `0x00177810`, then
`0x0009DE90` locks the alternate texture through `0x001B4B30` and calls
CRI `0x001762B0`. That wrapper reaches color conversion `0x001779D0` through
`0x00175C90`. Its cdecl arguments are planar Y/U/V pointers and strides,
a four-word destination (pixels, width, height, pitch), and three 256-entry
BGRA word tables. The native adapter reuses `movie_color_model.c`; decoding,
frame readiness, consumption and playback state remain generated.

The game binds that texture with SetTexture `0x001B1CC0` and draws four
XYZRHW/TEX1 vertices (FVF `0x104`, stride 24) as a triangle strip with
DrawVerticesUP `0x001B3760`. UVs are in texels. The existing D3D11 presenter
uploads the mutable linear texture and normalizes those coordinates. This
path does not use a video overlay.

The observed stage state is RGB MODULATE(texture, diffuse), alpha
SELECTARG2(texture, diffuse). XYZRHW without a diffuse field supplies white
alpha. The alpha test remains GREATER with reference 1; testing the converted
texture's zero alpha instead rejected every movie pixel. The DOA3 draw adapter
now selects default diffuse alpha through the presenter's existing alpha mode.

## Combat programmable draws and UI lines

The 3925 declaration builder at `0x001B3E10` writes stream/offset/type slots
at `+0x28 + 0x10*register`. CreateVertexShader `0x001B40D0` marks a program
with flag `4` at `+4`, stores its instruction count at `+0x10` and packed
word count at `+0x14`, and writes upload blocks at `+0x168`. LoadVertexShader
`0x001B42C0` copies those blocks. These differ from the 4928 offsets and flag.

The observed title-water program has 25 instructions, position/normal/UV0
in registers 0/2/9, and a 32-byte stream. The draw adapter validates this
layout, unpacks bounded program blocks through the vertex-shader model, and
uses the existing HLSL translator. Other upload command types and layouts
are rejected. Programmable output uses its own constants; the presenter
no longer requires a fixed-function transform for it.

`0x001B2BF0` computes reserved viewport constants from viewport `+0xB00`,
scale `+0x500`, depth scale `+0x4F8` and depth range `+0xB10`. Its grid bias
is 17/32 pixel, reduced by half a pixel for the back-buffer multisample case
selected by `0x001C0598` bit `0x1000` and `0x001C0590`. The adapter supplies
these constants at hardware indices 58/59. The verified water material
mirrors the scene texture and wraps its alpha mask; the host respects that
scene addressing. Stage-1 arguments are at `0x001C0200`; texture-transform
flags are stage state 9 (`+0x24`), which `0x00156300` sets through
`0x001B2440`. Stage `+0x74` is the border color (`0x001B2A70`).

Character-select UI also uses primitive 2 with FVF `0x44`. All three draw
APIs now admit even-sized line lists and the presenter submits native D3D11
line lists. Synthetic checks cover packed-block bounds, shader draws without
fixed transforms, mirrored program sampling, and line pixels/odd counts.

## Floor reflections

DOA3's reflective floors are planar reflections. The game draws the stage a
second time with a mirrored camera into a texture, then draws the floor again
with that texture. It never samples the frame's own color or depth. Two
separate code paths do this.

Azuchi's interior uses `0x0011A4B0`, once per frame:

1. `0x00157EC0` saves the current color and depth surfaces and binds a slot
   from the table at `0x0090FE08` (three slots, `0x28` bytes each, color at
   `+0x14`, depth at `+0x18`). In the dojo the color is 512x256 with its own
   512x256 depth.
2. `0x00118E60` loads a stored mirrored view (`0x008C7490`) and projection
   (`0x008C87B0`) and draws the room model. Compared with the main camera,
   the composed matrix has its Y row negated, and these draws use the other
   cull mode.
3. `0x00157F60` restores and releases the saved surfaces.
4. `0x00118D20` draws a denser floor mesh over the opaque floor tiles. It
   samples the target, blends with material alpha 0.6, and uses FVF `0x112`.
   Each vertex's UV is its position projected through the mirrored camera;
   vertices outside that view hold clamped edge values.

A draw capture of an unpaced attract-mode dojo frame (present 8000) confirms
this: 14 room draws into the offscreen target, then the overlay. On the 222
visible overlay vertices, the projected positions match the UVs within
0.005 (median 0.0009). The capture does not show whether the CPU recomputes
those UVs each frame. In that frame no fighter draw is mirrored or
offscreen, yet other dojo frames show the fighters in the reflection; that
path is not traced yet.

The shared pass `0x000DAC40` runs on ICE CAVE (`0x37`) and LOST WORLD
(LOWER) (`0x40`). Stage IDs are the selectors in `0x000DE4A0` and
`0x000DF7A0`; names come from the table at `0x00369400`, and the per-frame
stage draw is the jump table in `0x000DF960`. Those draws register end-of-frame
callbacks through `0x001589E0`: ICE CAVE's `0x000F7540` registers
`0x000F5740`, which calls the pass, and LOST WORLD's `0x00105340` registers
the pass itself. Each stage registers a callback block of `0x40` bytes per
player at `0x00864B18` through `0x000DA730`, and creates a 256x256 target
(128x128 on LOST WORLD) plus a vertex program through `0x000DAC00`. The pass translates
to the floor height, scales by (1, -1, 1) and translates back, then draws
the stage through the callbacks, optionally limited by a stencil mask of
the floor rectangle. It then draws the floor with the vertex program, which
computes projected UVs. It is skipped when its fade value falls below a
threshold. This path comes from static analysis only; no capture covers it.

BEACH (EVENING) (`0x38`) and BEACH (DAY) (`0x47`) also call `0x000DA730`,
but their draws reach their own render-target functions, `0x000E8360` and
`0x000E4210`. Those bind a per-player texture with depth and stencil off
and stored transforms; what they render is not traced.

LORELEI (`0x34`, draw `0x000F53B0`) reflects the stage with baked
geometry instead of a pass. The stage model holds upside-down copies of
pillars below the marble floors, and the floor is drawn over them with
material alpha 0.9 and DXT5 alpha between 130 and 255, so the copies show
through. A draw capture of a paced Sparring frame in the indoor hall shows
this for the pillar on the raised platform (floor at Y 0.87, +Y up): mirrored
across that floor, draws 68, 71, 69, 318 and 67 match the pillar draws above
it within a median of 0.03, 0.08, 0.13, 0.13 and 0.23 units. Draw 318 is
itself blended. Under the outdoor terrace the evidence is weaker: of six
draws below the floor in a Watch Mode capture, only the largest (660
vertices) matched geometry above it closely (median 0.38 units). The fighters
are not reflected: each fighter mesh is drawn once, no draw uses a mirrored
matrix, and no target is bound.
No path from the terrace draw, or from LORELEI (INDOOR) (`0x42`,
`0x000F9C00`), reaches `SetRenderTarget`; the registered callback
`0x000F4A80` draws camera-facing sprites.

The Azuchi and shared paths bind targets with their own depth surfaces, so the host's
offscreen targets depth-test at any `RECOMP_D3D_SCALE` or MSAA setting. The
host creates offscreen targets at their guest size, so reflections are not
upscaled with the main view.

## Point sprites

SNOW draws its falling snow as four 3480-point lists a frame (`0x00160410`
on Japan): FVF `0x002`, point sprites and scaling on, POINTSIZE 0.0459,
min 2.5, max 15, scale A 0.5, B 0, C 1, with the flake texture bound to
stage 3. With sprites on, `0x001B6410` builds the only combiner stage from
texture stage 3, where NV2A generates the sprite coordinates. AQUA PALACE
draws three more lists (150, 70 and 11 points), the last with material alpha
0.25 under lighting.

The host expands each point into a camera-facing quad in view space and draws
it with the projection alone, so depth, fog and widescreen treat it as 3D
geometry. A scaled point is `viewport height * size / sqrt(A + B*d + C*d*d)`
pixels at eye distance `d`, clamped to the min and max states and to 64
pixels. Sprites sample stage 3 with corner coordinates; lit points without
vertex color take the material's diffuse alpha. NV2A's own point-size
encoding is not modeled.

## DOA3 capture and remaining cage issue

`RECOMP_D3D_DRAW_CAPTURE` and `RECOMP_D3D_DRAW_CAPTURE_AT` now cover accepted
DOA3 draws; previously only the DOAXBV path emitted draw rows. The existing
frame/row bounds apply. JSON records include transforms, stream bindings,
render state and callers. Numbered `.vertices`, `.indices`, `.texture` and
`.palette` sidecars retain the inputs at submission. Use a new capture path
under `private/`. Programmable draws currently record their instruction count,
not a complete replayable program/constant snapshot.

The cage obstruction was reproduced in a capture with 1,183 accepted draws
and no declines. Pixel/triangle matching isolates a four-vertex triangle strip
using FVF `0x112`, with no vertex program or weights. It samples a light-flare
texture while blending is disabled and depth writes are enabled. The quad
crosses the near plane and covers the foreground. Its root cause remains
unresolved; neither removing the quad nor disabling wall depth/collision is
a verified correction. Later unobscured cage captures do not establish a fix.
After the shared-clock merge, an unpaced Watch Mode run reaches the cage
through Stage Select and renders a Kasumi mirror match/replay with fences
visible. It does not reproduce the earlier Bayman obstruction.

The next comparison is the offending draw's caller and guest resource/state
inputs against the 3925 library's behavior. No lifter defect has been proven.
The omitted stream byte offset is a separate mapping gap: observed offsets in
the later cage capture are zero, so that gap does not explain the obstruction.
