# DOA3 NX

Nintendo Switch port of *Dead or Alive 3* (Xbox, 2001), built on
[NoRain211/doa3-re](https://github.com/NoRain211/doa3-re): a native static
recompilation of the original Xbox executable with a hand-written runtime.
This repository keeps its full history and code; everything Switch-specific lives in
[`switch/`](switch), and the upstream README is kept as
[`README.doa3-re.md`](README.doa3-re.md).

> [!IMPORTANT]
> This repository contains **no game files and no prebuilt `.nro`**. The
> `.nro` carries the game's own executable, translated to C and compiled, so
> each user builds it from their own legally obtained copy of the **USA**
> Xbox release, whose `default.xbe` must have SHA-256
> `e523dd15d0765899a43a1824c023e0219d129e9a8b41e8213e4e645755d2bfa3`.

## Status

**Work in progress.** The whole game runs as a libnx homebrew with audio,
input, saves and an OpenGL renderer, and plays correctly in the Eden
emulator. Bring-up on real hardware and performance work are ongoing.

The game thread runs on core 0; an OpenGL 4.3 renderer (devkitPro mesa,
a port of upstream's SDL_GPU presenter) replays each recorded frame on
core 1, and audio mixes on core 2.

| Area | Switch host layer | State |
|---|---|---|
| Lifted program + runtime | shared with doa3-re (`recomp-runtime/`) | builds for AArch64 |
| Fibers | `fiber_switch_arm64.S` (shared) | — |
| Video | `switch/src/d3d_presenter_gl.cpp` + `d3d_presenter_gl_thread.cpp` (OpenGL 4.3, render thread on core 1) | in progress |
| Audio | `switch/src/audio_output_switch.cpp` (audout, mixer on core 2) | on |
| Input | `switch/src/input_host_switch.c` (libnx pads, rumble) | 4 ports |
| Files and saves | `win32_compat.c` + `switch/include` shims | SD card |

## Building

Requirements: Linux or WSL with devkitPro (`switch-dev`, plus `switch-mesa` and
`switch-glad` from `switch-portlibs`), CMake 3.20+,
Ninja (optional) and everything [`docs/building.md`](docs/building.md) lists for the Linux
build.

Clone with submodules (the lifter is one):

```bash
git clone --recurse-submodules https://github.com/toniisound/doa3-nx.git
cd doa3-nx
```

1. Build the desktop version once. This lifts `default.xbe` to C and checks
   it against the manifest the runtime was validated with:

   ```bash
   python3 -m venv private/venv
   private/venv/bin/pip install capstone==5.0.7
   ./build_game.sh /path/to/extracted/disc --movies
   ```

2. Build the `.nro` from that program:

   ```bash
   ./switch/build_switch.sh            # JOBS=3 for more parallel jobs
   ```

   Each generated chunk needs about 3 GB of RAM to compile. The build needs a 256x256 JPEG homebrew menu icon (not included) in
   `private/icon.jpg`.

## Installing

```
sdmc:/switch/doa3-nx/doa3-nx.nro
sdmc:/switch/doa3-nx/disc/default.xbe     <- the whole extracted USA disc
```

Start it from the homebrew menu, preferably through a game (title takeover)
for the full memory budget. Saves go to `disc/.recomp-storage`, and each boot
rewrites `sdmc:/switch/doa3-nx/doa3-nx.log`.

Held at launch, **R** runs unpaced (as fast as the CPU allows). The CPU is not
boosted by the app, since boost mode slows the GPU; use sys-clk to raise the
CPU clock. `-DDOA3_NX_PRESENTER=probe` builds the stage 1 CPU-only presenter.

Controls: Switch B/A/Y/X are Xbox A/B/X/Y by position; L/R are Black/White,
ZL/ZR the triggers, + is START and - is BACK. Quit with HOME or
ZL+ZR+(-)+(+).

## Credits

- [NoRain211](https://github.com/NoRain211): [doa3-re](https://github.com/NoRain211/doa3-re),
  its runtime (from [doaxbv-re](https://github.com/NoRain211/doaxbv-re)) and the
  [xboxrecomp](https://github.com/NoRain211/xboxrecomp) lifter fork.
- [devkitPro](https://devkitpro.org) and [switchbrew](https://github.com/switchbrew/libnx):
  devkitA64, libnx, mesa and glad for Switch.
- Switch port: Toniisound.

## License

GPL-3.0-or-later, as doa3-re. Dead or Alive 3 and its assets belong to Koei
Tecmo; nothing from the game is distributed here.

## Donations

This emulator is completely free and open-source. Optional donations to support the development are always welcome:

[![Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20me-FF5E5B?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/toniisound)
[![PayPal](https://img.shields.io/badge/PayPal-Donate-00457C?style=for-the-badge&logo=paypal&logoColor=white)](https://paypal.me/toniisound)
