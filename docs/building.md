# Building on macOS and Linux

The runner also builds natively for macOS (Apple Silicon or Intel) and Linux
x86-64, including SteamOS. It is the same lifted program and runtime as the
Windows build. Only the host layer differs:

| | Windows | macOS | Linux |
|---|---|---|---|
| Rendering | Direct3D 11 | Metal through SDL_GPU | Vulkan through SDL_GPU |
| Audio | XAudio2 | SDL3 (CoreAudio) | SDL3 (PipeWire, PulseAudio or ALSA) |
| Input | SDL3 gamepads, keyboard | same | same |
| Fibers and threads | Win32 | `win32_compat.c` and `fiber_switch_*.S` | same |

The macOS and Linux host layer comes from the sister project
[doaxbv-re](https://github.com/NoRain211/doaxbv-re), whose macOS and Linux
ports are shared code.

## Get the source

Download `DOA3-<version>-macOS.tar.gz` or `DOA3-<version>-Linux.tar.gz` from the latest
[release](https://github.com/NoRain211/doa3-re/releases/latest) and extract it
(for example `tar xzf DOA3-<version>-Linux.tar.gz`), then run the steps below from
the extracted folder. It holds the source, the pinned lifter and the scripts,
with no game files or prebuilt game. Instead of the archive you can clone the
repository with `git clone --recurse-submodules`.

## macOS

Install CMake, Ninja, SDL3 and Python 3.12 or newer:

```bash
brew install cmake ninja sdl3 python@3.13
```

Homebrew has no `extract-xiso`. Build it from
[XboxDev/extract-xiso](https://github.com/XboxDev/extract-xiso) and put it on
`PATH`, or pass its location with `--extractor`:

```bash
git clone https://github.com/XboxDev/extract-xiso.git
cmake -S extract-xiso -B extract-xiso/build && cmake --build extract-xiso/build
```

The lifter needs Capstone 5.0.7. Homebrew's Python refuses a global
`pip install`, so put it in the virtual environment the scripts look for
(from a clone, as here, or from the extracted archive):

```bash
git clone --recurse-submodules https://github.com/NoRain211/doa3-re.git
cd doa3-re
python3.13 -m venv private/venv
private/venv/bin/pip install capstone==5.0.7
```

Then build from your ISO or extracted disc folder:

```bash
./build_game.sh /path/to/doa3.iso
```

Lifting takes several minutes. The generated program is checked against the
same manifest hash as the Windows build, so a different lifter or input stops
the build before it compiles.

`./launcher.sh` opens a window to pick resolution, anti-aliasing, volume and
widescreen, saves them to `private/launcher.json` and starts the game.
`./run_game.sh` starts the game with the saved choices. As on Windows, the
game keeps its cache and saves in `private/play-disc` and each session's log
in `private/play-logs`.

## Linux and SteamOS

Linux renders with Vulkan and compiles shaders at runtime with shaderc, so the
host needs `libshaderc_shared.so.1` (SteamOS includes it) or, on Debian and
Ubuntu, `libshaderc.so.1` from the `libshaderc1` package.

Build in an x86-64 distribution with CMake, Ninja, a C++17 compiler, Python
3.12 or newer with the `venv` module, `extract-xiso` built as above, and
SDL3's build dependencies. `build_game.sh` builds SDL3 when the distribution
has none:

```bash
python3 -m venv private/venv
private/venv/bin/pip install capstone==5.0.7
./build_game.sh /path/to/doa3.iso
```

SteamOS has a read-only system and no compiler, so build inside a container
such as an Ubuntu 24.04 `distrobox` that shares your home folder; the
finished checkout then runs on SteamOS itself. `./launcher.sh` opens a
gamepad-friendly settings screen, then starts the game; `./run_game.sh`
starts the game with the saved choices.

For Steam Game Mode, add `launcher.sh` (or `run_game.sh`) with **Add a
Non-Steam Game** in Desktop Mode. Under Steam the game and launcher open full
screen. If one controller drives two player ports, turn Steam Input off for
that shortcut.

## Differences from the Windows build

- SMAA needs the Direct3D 11 presenter. The launchers offer MSAA only.
- `RECOMP_D3D_VRR` and `RECOMP_D3D_PRESENT_LOG` are Direct3D 11 features.
- `RECOMP_AUDIO_CAPTURE` records SDL's final mix as a float WAV and does not
  stop after 60 seconds.
- `RECOMP_HEADLESS=1` hides the window, for scripted runs.
- On ARM64 hosts `RCPSS` and `RSQRTSS` return the exact reciprocal instead of
  Intel's table approximation. The result is within Intel's documented
  error, but not bit-identical. x86-64 hosts use the native instructions.

## Runtime tests

On every platform, configuration downloads Dear ImGui 1.91.9, which draws
the swimsuit switch's and the free camera's panels, and checks its SHA-256;
set `FETCHCONTENT_SOURCE_DIR_IMGUI` to an unpacked copy to configure
offline.

```bash
cmake -S recomp-runtime -B build/recomp-runtime -G Ninja
cmake --build build/recomp-runtime
ctest --test-dir build/recomp-runtime --output-on-failure
```

These build test executables, not the game runner, and need no game files.
