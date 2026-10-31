# Changelog

## Unreleased

### Changed

- **Beach launcher.** The Windows, Linux and macOS launchers share one look:
  a sunset beach between letterbox bars, with page tabs and the chosen page's
  settings side by side in the bottom bar. Left and right move between pages
  and settings. Down opens a page; inside it, up and down change a value. The
  Linux and macOS launchers move to pages too.

## 0.4 — 2026-10-27

A free camera for screenshots, chrome costumes that reflect the stage,
falling snow on SNOW, and exe launchers on Windows.

### Added

- **Free camera.** With **Free camera** ticked in the launcher (or
  `RECOMP_FREE_CAMERA=1`), F1, `` ` `` or a click of the right stick takes the
  game's camera and flies it through fights and cutscenes with the DOAXBV
  port's controls: the sticks or WASD and the arrow keys, a right-mouse
  drag to look, freeze (P, or Start while flying) and step the game, switch camera, reset,
  turn to a fighter and change her swimsuit, and a clean view that hides the
  HUD and the panel. F12, or the d-pad's right while flying, saves a
  screenshot of the game's frame to `DOA3` in the Pictures folder (the
  Windows known folder). No control uses Back. The game's culling follows
  the free camera, so nothing at the edges of the view drops out: the camera build (USA 0x0002A540, Japan
  0x00027720) is wrapped, not replaced, so both generated programs' hashes
  change. Works on Japan 3.1 and USA 3.0. Keys and the mouse are
  read through the input host on every platform; the SDL_GPU overlay panel
  resizes with the window. See `docs/free-camera.md`.

### Changed

- **The launcher and the game are exes** (Windows). `Launcher.exe` replaces
  `Launcher.cmd` and does everything it did: build, settings, drag and
  drop, controller. `DOA3.exe` starts the game with the saved settings.
  Neither needs PowerShell; both come from `tools/launcher.cs`, built with
  the C# compiler every Windows ships.

### Fixed

- **Chrome costumes reflect the stage.** Costumes whose texture the Xbox
  generates from the reflection vector, such as Zack's third, showed that
  texture stretched along the body; the reflection is now generated as the
  Xbox does on every backend.

- **Snow falls on SNOW.** Point lists, the game's particles, were dropped,
  so SNOW had no snow and AQUA PALACE lost its particles. Each point is now
  drawn as a camera-facing quad sized as Direct3D sizes point sprites, with
  depth and fog like the rest of the scene.

## 0.3.1 Alpha (experimental) — 2026-10-26

A fix for Windows players whose game stopped at the second start.

### Fixed

- **Second start with a save stopped at boot** (Windows). With a save in
  `TDATA`, the boot runs the legal notice and the save load without waiting,
  and a graphics card still busy with the previous frame made the game stop
  with `Present failed hr=0x887A000A`. That frame is now skipped instead,
  and the log notes the first skip once.

## 0.3 Alpha (experimental) — 2026-10-24

The Japan 3.1 disc becomes the main build, one launcher now builds and plays
the game and shows which version is installed, every DOA3 woman can wear her
DOAXBV swimsuits from the player's own DOAXBV disc, and the movies are
optional. The runner also builds on macOS and Linux.

### Changed

- **Japan 3.1 is the main build.** The build and the launcher take the Japan
  3.1 disc, with 3.1's moves, balance and costumes, as well as the USA 3.0
  disc, telling them apart by `default.xbe`. Each version keeps its own
  extraction (`private/imported-disc-jp`, `private/imported-disc`) and play
  folder with its saves (`private/play-disc-jp`, `private/play-disc`), so
  building one never touches the other. The launcher shows the installed
  version in its top left corner and on the BUILD GAME and START GAME pages,
  and the game log's first runner line names the disc (title, region,
  version and `default.xbe` hash).

- **One launcher builds and plays.** `Launcher.cmd` gains a **BUILD GAME**
  page: drop your DOA3 ISO or disc folder onto `Launcher.cmd` or the
  launcher window, or choose it there, set Movies and build, with the
  build's progress in the information line and its output in
  `private/build.log`. **START GAME** plays and sends you to the build page
  until a build exists. `BuildGame.cmd`, `Play.cmd` and `ExtractIso.cmd`
  are gone; `tools/build_game.py` and `tools/play.ps1` still run on their
  own. An Xbox controller now drives the launcher's menus: D-pad or left
  stick, A to select, B to go back.

- **Movies are optional.** The build page's **Movies** setting leaves out the
  18 movies, 3.4 GB of the 4.5 GB disc; the game then skips each one,
  including the intro. Building again with Movies on restores them.

- **The stick moves the fighter.** Analog Button is turned on at every boot,
  since DOA3 reads the stick only with it on, and a tap of a face button
  reads as a light press, so a tap of P stays a P instead of a hard →+P.

### Added

- **DOAXBV swimsuits.** With the player's own DOAXBV disc (an `.iso` or the
  extracted folder), every DOA3 woman can wear any of her DOAXBV swimsuits,
  switched live in a fight on USA 3.0 and Japan 3.1: click L3 on any
  controller to open the swimsuit menu, then LB / LT for P1 or
  RB / RT for P2, hold to scroll, with the suit's number and name shown under
  the life bars and a panel listing the controls. X / B marks a
  favorite and Y browses only favorites (saved per girl in
  `private/suit-favorites.txt`), and the launcher can start each fighter in
  her first favorite. Fighters keep their DOA3 heads, hair and animation; the
  neck turns with the head, joints bend as DOAXBV bends them, hands close
  with DOA3's fists, ankles stay smooth with the toes up, and skirts and
  tassels swing and clear the body as DOAXBV moves them. Suits are built
  at run time from the disc through recipes of numbers and disc locations
  (`recipes/doaxbv-suits.pack.gz`); no DOAXBV data ships. The disc is
  found in `private/` or a `doaxbv-re` checkout beside this one, or chosen
  or dropped in the launcher. See `docs/doaxbv-swimsuits.md`.

- **macOS and Linux builds.** The runner builds natively on macOS (Apple
  Silicon and Intel) and Linux x86-64, including SteamOS, with the host layer
  of doaxbv-re's ports: SDL_GPU rendering through Metal or Vulkan, SDL3 audio,
  POSIX fibers and file APIs. The SDL_GPU presenter carries DOA3's
  fixed-function lighting (point lights, specular, the stage-0 combine),
  table fog, the gamma ramp, the widescreen HUD at 4:3, guest viewports,
  line lists, constant-alpha blending, front-buffer reads and mipmapped
  frame copies above native resolution. `build_game.sh`,
  `launcher.sh` and `run_game.sh` build, configure and play; see
  `docs/building.md`. The release has `DOA3-0.3-Alpha-macOS.tar.gz` and
  `DOA3-0.3-Alpha-Linux.tar.gz` beside the Windows ZIP, with the same
  source and the scripts kept executable; extract one instead of cloning.

- **Skippable legal notice.** Any controller button skips the scrolling
  legal notice at boot.

### Fixed

- **Forest dust** draws as faint puffs instead of a solid brown blob.
- **DOATEC HK street:** wet-road reflections show only in puddles, and the
  road no longer flickers under the street lights.
- **Select screen:** at 16:9 the portrait stays inside its 4:3 frame, the line
  under names is gone, and portraits appear almost at once.
- **Performance:** fewer per-draw vertex copies and capture work off the
  game thread.
- **Swimsuit menu:** LB, RB, X, B and Y register again; buttons report a light
  press, which the menu used to ignore.
- **Windows build without Visual Studio:** the build no longer stops with a
  CMake "NMake Makefiles does not support platform specification" error when
  no Visual Studio C++ tools are installed. It checks for them before lifting
  and installs the Visual Studio 2022 Build Tools' C++ workload (several GB,
  after a Windows prompt) when they are missing.

### Known limits

- Japan 3.1: the Story ending runs at 8-11 fps, animation can look slightly
  off, the KO slowdown plays only when the computer's fighter is knocked out,
  the auto-save runs without the crash journal, and reverb sends are only
  logged.

## 0.2 Alpha (experimental) — 2026-10-09

The game now looks much closer to the Xbox: fighters and stages are lit by the
game's own lights instead of drawn at full brightness, and the save survives a
crash in the middle of saving. A review of the runtime against the original
executable also fixed a set of timing, file and kernel details, and the
renderer picked up the runtime fixes from the sister project doaxbv-re.

### Added

- **Fixed-function lighting.** About 95% of DOA3's 3D draws use Direct3D's
  built-in lighting, which 0.1 skipped, so fighters and stages showed flat,
  full-bright textures. The renderer now evaluates it per vertex as the Xbox
  does: scene ambient and emissive light, up to eight directional and point
  lights with range and distance falloff, and specular highlights (most
  fighter materials use a specular power of 30-40). The first texture stage
  combines texture and lit color the way the game sets it up, so translucent
  materials take their transparency from the material.
- **Crash-safe saves.** The auto-save after matches now runs inside the
  runtime's save journal, which also covers the title-data folder where DOA3
  keeps its save. If the game crashes or a disk write fails during a save, the
  previous save is restored right away or on the next start. Tested with a
  normal save, a forced short write and a forced exit right after the write.
- **Variable refresh rate (opt-in).** `RECOMP_D3D_VRR=1` lets the game's own
  60 Hz timer pace a VRR display. See `docs/runtime-options.md`.

### Fixed

- **Omega's heat haze** in the final Story stage blends the previous frame, as
  the Xbox does, so its afterimage shows instead of cancelling itself out.
- Even 60 Hz pacing on high-refresh displays.
- Closing the window during a frame exits normally instead of reporting an
  error.
- Depth-of-field and other effects that shrink the frame average it like the
  Xbox when rendering above native resolution.
- Unbinding a texture now resets the format the next texture is compared
  with, so a texture of the same format no longer keeps stale state.
- Kernel timers run their callback when they expire, not when they are set.
- Directory searches keep their wildcard after the first result, and file
  reads, writes and hashes check the whole buffer the game passes.
- The stage-edge collision pass keeps its per-stage state between frames, as
  the original does.
- Reserving memory inside an existing block no longer clears it.
- 3D audio: an invalid position no longer plays a sound at full volume,
  Doppler limits apply only to 3D sounds, and the play position keeps moving
  when audio output is muted.
- After the boot fast-forward, frame pacing waits the right time on systems
  without high-resolution timers.
- Builds no longer print SMAA conversion warnings, and a frame-buffer texture
  the renderer cannot map is logged instead of silently drawn untextured.

### Tester flow

- `BuildGame.cmd` extracts again when you drop a different ISO or an earlier
  extraction never finished, and refuses to replace a folder that holds saves.
- `Play.cmd` prepares its copy of the disc in a temporary folder, so closing
  the first launch early no longer leaves a broken copy, and it stays open
  with an error when the game exits abnormally.
- Packaging checks its bundled tools before it writes the ZIP.

### Upgrading from 0.1

Extract 0.2 into a new folder and run `BuildGame.cmd` again; the generated
program changed (the auto-save is now hand-written), so a 0.1 build is
refused. To keep your progress, launch 0.2 once, close it, then copy
`private\play-disc\.recomp-storage` from your 0.1 folder over the new one.

## 0.1 Alpha (experimental) — 2026-10-09

The first public release of the playable whole-program recomp. Private test
builds 0.0.1 and 0.0.2 came before it; the fixes below the viewport fix came
after 0.0.2.

### Added

- **Playable game.** Intro movie, title, menus, Story, Time Attack, Watch and
  Sparring fights with audio, stage transfers, continue and attract mode run
  on the lifted program, paced to 60 Hz.
- **Hand-written collision.** The per-frame boundary pass, the danger-zone
  pass and their exemption predicates replace the lifted code, so fighters
  stop at walls and stage transfers work.
- **Audio.** DOA3's DirectSound 3936 buffers play through XAudio2, with 3D
  distance, Doppler and the Xbox's HRTF at its own gain.
- **Display.** Resolution scale, MSAA, SMAA (from the `third_party/smaa`
  submodule) and opt-in anamorphic widescreen.
- **Tester flow.** `BuildGame.cmd` builds from your own ISO or disc folder,
  `Launcher.cmd` picks display settings and volume, and `Play.cmd` plays.
  The boot legal notice is fast-forwarded.

### Fixed

- Audio static: GetCurrentPosition reports the position already sent to the
  device.
- 3D voices no longer clip.
- Fixed-function draws honor the game's viewport, so the select-screen
  portrait stays inside its box.
- The final Story stage against Omega shows its arena and fighters instead of
  a black screen: its full-screen haze pass now blends with constant alpha and
  samples the rendered frame.
