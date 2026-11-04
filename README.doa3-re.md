<div align="center">

# DOA3 Native PC Port

**A native PC port of _Dead or Alive 3_ (Windows, macOS & Linux), built by static recompilation, with the goal of rewriting it into readable source.**

[![CI](https://github.com/NoRain211/doa3-re/actions/workflows/public-ci.yml/badge.svg)](https://github.com/NoRain211/doa3-re/actions/workflows/public-ci.yml)
[![License: GPL-3.0-or-later](https://img.shields.io/badge/license-GPL--3.0--or--later-blue)](LICENSE)
![Platform: Windows, macOS & Linux](https://img.shields.io/badge/platform-Windows%20%7C%20macOS%20%7C%20Linux-0078D6)

[Status](docs/bring-up.md) ·
[Report a bug](https://github.com/NoRain211/doa3-re/issues)

<a href="https://discord.gg/dUdgUF4A"><img src="https://img.shields.io/badge/Join%20the%20Discord-5865F2?style=for-the-badge&logo=discord&logoColor=white" alt="Join the Discord" height="30"></a>

[![Support me on Ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/norainsrecomps)

</div>

> [!IMPORTANT]
> This project ships **no game files**. You need your own legally obtained copy
> of the Xbox game: the **Japan 3.1** disc, the main build, or the USA 3.0
> disc. It is not affiliated with or endorsed by the game's rights holders.

## About

The original Xbox executable is lifted to C with a static recompiler, then
linked against a hand-written runtime that replaces the Xbox kernel imports
and, one entry point at a time, the Direct3D 8 and DirectSound APIs with native
equivalents: Direct3D 11 and XAudio2 on Windows, SDL_GPU (Metal or Vulkan) and
SDL3 audio on macOS and Linux. The rest of the game, including its statically linked
libraries, runs as generated code. The plan is to replace game logic with
readable, hand-written source one data-structure cluster at a time once the
game runs.

The runtime comes from the sister project
[doaxbv-re](https://github.com/NoRain211/doaxbv-re), which is fully playable on
the same approach.

## Status

> [!NOTE]
> **Playable, in early testing.** Intro movie, title, menus, Story, Time
> Attack, Watch and Sparring fights with audio, stage transfers, continue and
> attract mode run on the whole-program recomp. See
> [bring-up](docs/bring-up.md) for run evidence, [history](docs/history.md)
> for how the port got here, and [runtime options](docs/runtime-options.md)
> for settings and runner flags.

The main build is the Japan 3.1 disc (XDK 4134), with 3.1's moves, balance
and costumes; the USA 3.0 disc still builds. `tools/build_game.py` and the
launcher tell them apart by `default.xbe`, keep each in its own extraction and
play folder (so saves never mix), and the launcher shows which one is
installed. Japan specifics are in [Japan 3.1](docs/doa3-jp-test.md).

| Area | State |
| --- | --- |
| Whole-program lift | Runs from boot to fights, paced to 60 Hz |
| Kernel imports | Shared with doaxbv-re |
| D3D8 3925 | Device, frame, draws, textures, vertex programs and viewport bound to DOA3 |
| DirectSound 3936 | Bound; XAudio2 (Windows) or SDL3 (macOS, Linux) output with 3D distance, Doppler and HRTF |
| macOS and Linux | Native builds; see [building on macOS and Linux](docs/building.md) |
| CRI Sofdec movies | Play natively |
| Hand-written game logic | Stage boundary and danger-zone collision |
| DOAXBV swimsuits | Every DOA3 woman can wear her DOAXBV swimsuits, switched live in a fight, built from your own DOAXBV disc |
| Free camera | Take the game's camera and fly it through fights and cutscenes, freeze and step the game, hide the HUD, change swimsuits (with your DOAXBV disc) and save screenshots; see [Free camera](docs/free-camera.md) |

### DOAXBV swimsuits

With your own _Dead or Alive Xtreme Beach Volleyball_ disc (an `.iso` or its
extracted folder), Kasumi, Ayane, Christie, Tina, Hitomi, Helena and Lei Fang
can each wear any of her 307 DOAXBV swimsuits. They keep their DOA3 heads,
hair and moves; the suit is built on the fly from your disc, and skirts swing
and clear the legs, and tassels swing, with DOAXBV's own cloth motion. In a fight, click
**L3** (the left stick) on any controller to open the swimsuit menu (click
again to close): its left half dresses P1
and its right half P2. **LB** / **LT** and **RB** / **RT** change the
swimsuits (hold to scroll), **X** / **B** mark a favorite (saved per girl),
and **Y** browses only favorites. A panel lists the controls
while the menu is open, and the keyboard's **1** / **2** switch too. Works on
Japan 3.1 and USA 3.0. The game finds
a DOAXBV `.iso` or extracted folder in `private/` (or in a `doaxbv-re`
checkout beside this one); otherwise choose or drop it in the launcher (steps
in [Getting the swimsuits](docs/doaxbv-swimsuits.md#getting-the-swimsuits)). The
suit recipes (`recipes/doaxbv-suits.pack.gz`) ship with the
port; no DOAXBV data does. See
[DOAXBV swimsuits](docs/doaxbv-swimsuits.md).

### Free camera

Tick **Free camera** in the launcher, then in a fight or cutscene press
**F1** or click the right stick to take the game's camera and fly it: the
sticks or **WASD** and the arrow keys move and look, the triggers or **Q** /
**E** go down and up. **Start** or **P** freezes the game and the right
shoulder or **.** steps it a frame, the left shoulder or **H** hides the
HUD, and the d-pad's left or **\\** turns to a fighter whose swimsuit the
d-pad's up and down change. **F12**, or the d-pad's right while flying,
saves a screenshot to `DOA3` in your Pictures folder. It works on both
Japan 3.1 and USA 3.0. No control uses Back, which DOA3 acts on first. See [Free camera](docs/free-camera.md).

`recomp-runtime/` started from doaxbv-re at `f6ad13e`. Its DOAXBV address
bindings are gated off; DOA3's D3D8, DirectSound, XAPI and CRI functions are
re-bound in its adapters.

| Library | DOA3 | DOAXBV |
|---------|------|--------|
| XAPILIB, D3DX8, XGRAPHC, XBOXKRNL | 3911 | 4928 |
| D3D8 | 3925 | 4928 |
| DSOUND | 3936 | 4928 |

Both games carry CRI Sofdec (`PSGSFD*`), XPP and DOLBY sections and ship AFS
archives and SFD movies.

## Build the tests from source

The runtime tests build without any game files or submodules:

```powershell
git clone https://github.com/NoRain211/doa3-re.git
cd doa3-re
cmake -S recomp-runtime -B build/recomp-runtime -G "Visual Studio 17 2022"
cmake --build build/recomp-runtime --config Release --parallel 2
ctest --test-dir build/recomp-runtime -C Release --output-on-failure
```

This produces test executables, not the game runner. Passing tests does not
establish game accuracy.

## Set up a local build

```powershell
git submodule update --init
python tools/extract_iso.py <your-doa3.iso>        # -> private/imported-disc/disc
python tools/xbe_info.py private/imported-disc/disc/default.xbe
```

`extract_iso.py` needs XboxDev
[extract-xiso](https://github.com/XboxDev/extract-xiso/releases) on PATH or at
`tools/artifacts/extract-xiso.exe`. The lift and runner build are in
[docs/bring-up.md](docs/bring-up.md).

## Play

Download the ZIP from the latest
[release](https://github.com/NoRain211/doa3-re/releases/latest) and extract it
to a folder with a short path. Drag a DOA3 ISO or extracted disc folder onto
`Launcher.exe`, or choose it on the launcher's **BUILD GAME** page, to build
the game; **START GAME**
plays with the resolution, anti-aliasing, widescreen and volume you pick; see
[docs/test-release.md](docs/test-release.md). After that, `DOA3.exe` starts
the game with those settings. In a checkout,
`python tools/package_test.py --launchers .` builds both exes. On Windows the build needs
Python 3.12+, CMake 3.20+ and Visual Studio's C++
tools; if no Visual Studio C++ tools are installed, the build installs the
Visual Studio 2022 Build Tools first (several GB, after a Windows prompt).
On macOS and Linux, extract the release's
`DOA3-<version>-macOS.tar.gz` or `DOA3-<version>-Linux.tar.gz` and run
`./build_game.sh` with your ISO, then `./launcher.sh` or `./run_game.sh`; see
[docs/building.md](docs/building.md). The game keeps its cache and
saves in `private/play-disc` and each session's log in `private/play-logs`.

## Display settings

`RECOMP_D3D_SCALE` (1-8, render height multiplier; 3 renders 1920x1440
natively), `RECOMP_D3D_MSAA` (sample count, for example 4) and
`RECOMP_D3D_SMAA=1` (built from the `third_party/smaa` submodule; Direct3D 11
only) work as in doaxbv-re.

Widescreen defaults off because DOA3's box lists no 16:9 support, but the
game honors the dashboard flag that `RECOMP_D3D_WIDESCREEN=1` reports: its 3D
view renders anamorphic 16:9 with a wider horizontal view, and the window
shows it at 16:9. The game leaves its 2D layer at 4:3, so the presenter
narrows screen-space quads that are narrower than the centered 4:3 area by
3/4 toward the center. The HUD, menus and subtitles keep their 4:3 shape;
full-screen fades, backdrops and the already pillarboxed movies are left as
drawn.

## Repository layout

| Path | Contents |
| --- | --- |
| [`recomp-runtime/`](recomp-runtime) | Runtime, kernel and input adapters, audio, D3D8 replacements, presentation, tests |
| [`xbe/`](xbe) | XBE parsing and hashing |
| [`tools/`](tools) | ISO extraction, XBE info, lifter submodule, DOA3 function lists, build, launcher, packaging and export checks |
| [`third_party/`](third_party) | SMAA submodule |
| [`docs/`](docs) | Bring-up status, lift and build steps, D3D8, audio, input and fiber research |
| `private/` | Ignored local game inputs, generated output and run evidence |

## Contributing

Read the [contribution guide](CONTRIBUTING.md) first. Translation defects are
fixed in the lifter and regenerated; generated game C is never patched by hand.

When [reporting a bug](https://github.com/NoRain211/doa3-re/issues), include
your build version, steps to reproduce, what happened, your hardware and
controller, and the stop or crash message
([how to read logs](recomp-runtime/README.md#reading-stop-and-crash-logs)).
Remove private paths from logs, and never upload game binaries, generated game
C, assets, extracted filenames, saves, BIOS data or private run evidence.

## AI assistance

This project uses large language models and AI coding agents extensively for
code, reverse-engineering analysis, debugging, tests and documentation. Their
output can contain mistakes; changes are reviewed and tested, and unverified
behavior is tracked as such.

## License

Original code and documentation are licensed under
[GPL-3.0-or-later](LICENSE); see also [NOTICE](NOTICE). Third-party components
keep their own licenses. The license grants no rights to the game or its assets.
