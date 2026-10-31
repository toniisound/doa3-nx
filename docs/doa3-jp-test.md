# Japan 3.1

Japan 3.1 is the main build. It began as a time-boxed test (two sessions) of
lifting the Japan 3.1 XBE (XDK 4134) on the USA runtime; the user supplies
only the Japan disc.

## Build

`tools/build_game.py` (and the launcher's Build) recognizes the Japan
`default.xbe`, lifts it with `tools/doa3-jp`, checks the generated program
against its own `PROGRAM` pin in `VERSIONS`, and configures the runner with
`-DRECOMP_DOA3_JP=ON`, which compiles `program_forwards_jp.c` and defines
`RECOMP_DOA3_JP_BINDINGS`. The extraction goes to `private/imported-disc-jp`
and the play folder, with the saves, to `private/play-disc-jp`; the USA disc
keeps `private/imported-disc` and `private/play-disc`.

- `tools/doa3-jp/manual_functions.json` and `seed_functions.json`: the USA
  recipe moved to Japan addresses with Ghidra Version Tracking and BSim
  (session `DOA3-US-to-JP`).
- `program_forwards_jp.c` holds the Japan stubs and a Japan-to-USA table.
  `recomp_lookup_manual` translates through it, so USA-keyed adapters bind
  unchanged. Addresses outside the table get no adapter: three Japan functions
  sit at USA adapter keys (0x00021930, 0x000E8100, 0x001B8890).
- `doa3_build.h`: `DOA3(usa, japan)` picks a guest address for the lifted XBE.
- Japan calls `D3DDevice_Swap` (0x001E14D0, `ret 4`) where the USA build calls
  Present; `d3d_frame_adapter.c` binds it to its own adapter.
- DSOUND 4134 `SetMixBinVolumes` takes `LPCDSMIXBINS` (`ret 8`); the trace-only
  adapter pops two arguments on Japan. `SetMixBins` keeps `ret 8`.
- The fiber adapter reads XAPI's TLS index directly: USA 0x00B228C8, Japan
  0x0040A178. With the USA value it published the current fiber into game
  data, task slot 0's frame countdown at 0x0073C404, so the top task almost
  never ran.
- CRI `mwSndOpenPort` calls DSOUND 4134 `IDirectSoundBuffer_SetHeadroom`
  (0x001F1DA5, `ret 8`), which USA's 3936 lacks. Lifted, it dereferences the
  adapter's unmapped buffer handle; a Japan-only adapter returns `S_OK`.
- The game claims nearly all RAM at boot. A 1 MiB D3D contiguous block landed
  in the gap after the XBE on USA but just above the heap on Japan, so the
  heap met contiguous memory and the next fiber stack failed. The guest heap
  now continues in that gap too; this applies to every build.

The Japan recipe binds 78 of the 83 USA hooks, plus `SetHeadroom`. The 17
D3D state and draw
hooks (`SetTexture`, `SetStreamSource`, `SetIndices`, `SetVertexShader`, the
render-state setters and the three draw calls) bind to adapters ported to
4134 (below). The 5 game seams (collision passes, action category,
auto-save) stay lifted: their adapters model USA game data.

## D3D8 4134 layout

`CDevice` fields, read from `KickOff`, `D3D_SetFence`, `GetBackBuffer`,
`InitializeFrameBuffers`, `SetTile` and the render-state setters:

| Field | USA 3925 | Japan 4134 |
|---|---|---|
| flags | 0x000C | 0x0008 |
| kicked put | 0x0018 | 0x002C |
| fence | 0x001C | 0x0030 |
| fence context | 0x03F0 | 0x0034 |
| fence records | 0x00E4 | 0x0050 |
| alternate put | 0x0400 | 0x035C |
| pixel shader | 0x0414 | 0x0370 |
| render target | 0x040C | 0x2070 |
| depth stencil | 0x0410 | 0x2074 |
| back buffers | embedded at 0x2150 | pointer table at 0x207C, surfaces at 0x208C |
| auto depth surface | 0x219C | 0x20D4 |
| back-buffer size | 0x21B4, 0x21B8 | only in the surface `Size` word |
| tiles | 0x21BC | 0x211C |
| channel | 0x2304 | 0x2264 |
| flips done | 0x2518 | 0x2458 |
| frame counter | 0x2B60 | 0x2ABC |

Globals: `g_pDevice` 0x001C3390 to 0x001EDE80, static device 0x001C0800 to
0x001EB3A0. The dirty mask left the device: USA 0x001C0808 (device + 8) is
Japan 0x001EAD68, where device + 8 holds the flags. The 3925 adapters that OR
dirty bits into device + 8 would corrupt Japan's flags.

The state and draw adapters take Japan values through `DOA3()`, read from the
4134 setters and their callers:

| State | USA 3925 | Japan 4134 |
|---|---|---|
| texture slots, palettes | device + 0xBA0, + 0xBB0 | device + 0xA78, + 0xA88 |
| texture format shadow | device + 0x4DC | device + 0x0C |
| index buffer, base vertex | device + 0x47C, + 0x478 | device + 0x38C, + 0x1C |
| index data | 0x001C017C | 0x001EAD64 |
| vertex declaration, handle | device + 0x470, + 0x474 | device + 0x380, + 0x384 |
| transforms | device + 0x880 | device + 0x750 |
| viewport, material, lights | device + 0xB00, + 0xB18, + 0x488 | device + 0x9D0, + 0x9F0, + 0x398 |
| vertex shader constants | device + 0xCC0 | device + 0xB98 |
| texture stage block | COLOROP 0x001C0180, ADDRESSU + 0x28 | ADDRESSU 0x001EAD70, COLOROP + 0x30 |
| render states | 0x001C0478 to 0x001C0524 | + 0x2ABF0 |
| render states | 0x001C0554 to 0x001C0590 | + 0x2ABF4 |

4134 already uses 4928's texture stage order and vertex shader record
(0x114 bytes), and its SetVertexShader, FVF builder and SetViewport match
DOAXBV's, so Japan reuses the DOAXBV branches with 4134 addresses. Where it
differs from 4928: the declaration dirty bits are 0x1200, SetTexture
retains with a plain add and records the fence at device + 0x30, and
SetStreamSource marks 0x40 or 0x70 in the dirty mask.

## Address map

A diff of matched function pairs maps USA constants to Japan ones; unvoted
literals take the delta of agreeing neighbours, since modules move as blocks
(CRI ADXM code +0x2A000, its data -0x718640). Of 519 distinct USA literals in
`recomp-runtime`, 79 are hooks, 69 are not guest addresses, about 140 map
with medium or high confidence, and about 230 have no proposal. Most of those
are DOAXBV-only code or opt-in debug watches, not DOA3 paths.

## Results

| Boot | Change | Result |
|---|---|---|
| 1 | first lift | stalls in `Direct3D_CreateDevice` (3925 `CDevice` offsets) |
| 2 | miniport and frame layouts | clears CreateDevice; stops calling CRI vblank worker A (not translated) |
| 3 | lookup translation | first CRI threads run; stops in CRI ADXM server (USA addresses) |
| 4 | CRI ADXM addresses | first frame presented, fibers run; crashes in `SetStreamSource` adapter |
| 5 | D3D state, draw and game seams left lifted | runs 120 s without a stop: 525k kernel calls, fills the `Z:` cache with `voice.afs` and `bgm.afs` |
| 6 | same build, 60 s paced | 60 fps steady (3541 presents, no late frames); restarts the `Z:` cache fill, which a killed run never finished |
| 7 | same build, `RECOMP_UNPACED=1`, 240 s | finishes the cache (`loadfile.afs` 438,065,152 bytes, copy thread exits), then the main loop runs 1.15M frames with no stop, no file reads and no audio buffers |
| 8 | 17 D3D adapters ported and bound, 60 s paced | 60 fps, no stop; the render-state adapters take 74k updates by swap 1200, but no draw reaches the draw adapters (0 submitted, 0 declined) and no audio buffers are submitted |
| 9 | fiber TLS index | the top task runs: copyright notice and Tecmo logo draw; stops in lifted `SetHeadroom` as the opening movie starts |
| 10 | `SetHeadroom` adapter, 150 s | opening movie plays with sound; stops creating a fiber stack (heap met contiguous memory) |
| 11 | heap uses the gap after the XBE, 180 s | no stop: title screen and 3D attract stages at 60 fps, 13,466 audio buffers, no underruns; 8 point-list draws declined |
| 12 | gamepad device type, aligned contiguous reuse, lifter resyncs direct jumps past inline tables | controller works; Story picks Kasumi and starts a fight against Leifang. Before the lifter fix, `0x72F50` returned from phase 5 every frame, so no match started |

Boots 7 and 8 stalled because the fiber adapter wrote into task slot 0's
countdown. Frame dumps from boot 11 show the Japanese copyright notice, the
Tecmo logo, the opening movie, the title screen and textured, lit 3D stages.

## Remaining work for a Japan target

- Play past the title: menus, a fight, and the 3.1 moves and costumes.
- Re-derive the 5 game seams on Japan code: the boundary and collision passes
  and the save-journal auto-save.
- Apply the address map to the remaining adapters, and re-check each USA
  feature (saves, audio, movies, collision, pacing) on Japan.
