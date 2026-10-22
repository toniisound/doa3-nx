# DOA3 NX

Nintendo Switch port of [doa3-re](https://github.com/NoRain211/doa3-re), the
native static recompilation of *Dead or Alive 3* (Xbox, USA), as a libnx
homebrew `.nro`.

> [!IMPORTANT]
> No game files are included. You need your own legally obtained copy of the
> USA Xbox release; its `default.xbe` must have SHA-256
> `e523dd15d0765899a43a1824c023e0219d129e9a8b41e8213e4e645755d2bfa3`.

## Status

**Stage 1 — CPU bring-up.** The whole lifted program runs on the console, but
the presenter draws nothing and audio is off: the screen shows the guest frame
rate, draws per frame and frame times. This stage measures whether the
console's CPU can run the game at 60 fps before the GPU backend is written.

| Area | Switch host layer | State |
|---|---|---|
| Lifted program + runtime | shared with doa3-re (`recomp-runtime/`) | builds for AArch64 |
| Fibers | `fiber_switch_arm64.S` (shared) | — |
| Video | `switch/src/d3d_presenter_switch_probe.cpp` | probe only (no rendering) |
| Audio | `switch/src/audio_output_null.cpp` | off |
| Input | `switch/src/input_host_switch.c` (libnx pads, rumble) | 4 ports |
| Files and saves | `win32_compat.c` + `switch/include` shims | SD card |

## Building

Requirements: Linux or WSL with devkitPro (`switch-dev`), CMake 3.20+,
Ninja (optional) and everything `../docs/building.md` lists for the Linux
build.

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

   Each generated chunk needs about 3 GB of RAM to compile. An optional
   256x256 `switch/icon.jpg` becomes the homebrew menu icon.

## Installing

```
sdmc:/switch/doa3-nx/doa3-nx.nro
sdmc:/switch/doa3-nx/disc/default.xbe     <- the whole extracted USA disc
```

Start it from the homebrew menu, preferably through a game (title takeover)
for the full memory budget. Saves go to `disc/.recomp-storage`, and each boot
rewrites `sdmc:/switch/doa3-nx/doa3-nx.log`.

Held at launch: **R** runs unpaced (as fast as the CPU allows), **L** keeps
the CPU at its normal clock instead of boost mode (1785 MHz).

Controls: Switch B/A/Y/X are Xbox A/B/X/Y by position; L/R are Black/White,
ZL/ZR the triggers, + is START and - is BACK. Quit with HOME or
ZL+ZR+(-)+(+).

## License

GPL-3.0-or-later, as doa3-re. Dead or Alive 3 and its assets belong to Koei
Tecmo; nothing from the game is distributed here.

## Donations

This emulator is completely free and open-source. Optional donations to support the development are always welcome:

[![Ko-fi](https://img.shields.io/badge/Ko--fi-Support%20me-FF5E5B?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/toniisound)
[![PayPal](https://img.shields.io/badge/PayPal-Donate-00457C?style=for-the-badge&logo=paypal&logoColor=white)](https://paypal.me/toniisound)
