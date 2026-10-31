# Runtime options

Environment variables and command-line flags read by
`recomp_program_runner`. An unset variable means off unless the table says
otherwise. Diagnostics print to stderr.

## Player settings

`Launcher.exe` and `DOA3.exe` set all but `RECOMP_D3D_FOG`, then start the game. On macOS and Linux,
`launcher.sh` saves them to `private/launcher.json` and `run_game.sh` applies
them.

| Variable | Effect |
|---|---|
| `RECOMP_D3D_SCALE` | Render height multiplier, clamped to 1-8. 3 renders 1440 lines natively. The launcher sets height / 480. |
| `RECOMP_D3D_MSAA` | MSAA sample count, clamped to 1-32. The GPU must support the count. |
| `RECOMP_D3D_SMAA` | Any value except `0` enables SMAA. Direct3D 11 only. |
| `RECOMP_D3D_WIDESCREEN` | Any value except `0` reports the dashboard's 16:9 setting to the game and sizes the window at 16:9. The 3D view widens; the HUD and menus keep their 4:3 shape in the center. |
| `RECOMP_FREE_CAMERA` | `1` lets F1, `` ` `` or the right stick's click take the camera and fly it ([Free camera](free-camera.md)); its other options are listed there. |
| `RECOMP_AUDIO_GAIN` | Master gain from 0 to 1, default 1. 0 or an invalid value mutes output. |
| `RECOMP_D3D_FOG` | `0` disables the host table-fog model, for before/after comparisons. |

The swimsuit switch reads `RECOMP_DOAXBV_DISC` and `RECOMP_COSTUME_PACK`;
`run_game.sh`, `Launcher.exe` and `DOA3.exe` set both when a DOAXBV disc is chosen. See
[DOAXBV swimsuits](doaxbv-swimsuits.md) for those and its other options.

The launcher does not set this one:

| Variable | Effect |
|---|---|
| `RECOMP_D3D_VRR=1` | For variable-refresh displays: the game's own 60 Hz timer paces frames instead of the display's refresh. Direct3D 11 only. |

## Test harness

| Variable | Effect |
|---|---|
| `RECOMP_UNPACED=1` | Skips host waits by advancing the shared guest clock, so the game runs as fast as the CPU allows. The window title ends in "(unpaced)". Audio drops buffers. `tools/play.ps1` clears it. |
| `RECOMP_WATCHDOG_MS` | After this many milliseconds, samples registers, dispatch frames and the guest stack from a host thread, then stops. `tools/play.ps1` clears it. |
| `RECOMP_PERF_COUNTER=1` | Shows FPS and frame time in the window title and reports late frames. A `recomp pacing` line each second also gives the slowest game frame, the time spent waiting on the render thread, and draws per second and per frame. |
| `RECOMP_HEADLESS=1` | macOS and Linux: hides the window, for scripted runs. |
| `RECOMP_UNIMPL_TRAP=1` | Stops at the first untranslated instruction reached. Without it the first 32 are reported and run as no-ops. |
| `RECOMP_SAVE_SHORT_WRITE_AT`, `RECOMP_SAVE_INTERRUPT_AFTER_WRITE` | Save fault injection: shorten, or interrupt after, save write number N (1-1024). |
| `RECOMP_RAM_DUMP_AT` with `RECOMP_RAM_DUMP` | Writes all 64 MiB of guest RAM to that file at the given frame (1 or later), to locate a desync that `--ram-hash-log` reports. |

## Capture

| Variable | Effect |
|---|---|
| `RECOMP_D3D_FRAME_DUMP` | Writes the back buffer to this BMP path. `_AT` picks the first present (default 1), `_COUNT` a burst of numbered frames (default 1), `_INTERVAL_MS` spaces them in host time. |
| `RECOMP_D3D_DRAW_CAPTURE` | Writes one present's draw inputs to this path. `_AT` names the present; with `_AT=0`, `_TRIGGER` names a file whose appearance starts the capture. `_FVF` captures the first draw with that vertex format. |
| `RECOMP_D3D_PROGRAM_TEXTURE_DUMP` | Path prefix for textures used by programmable draws, gated by the draw-capture trigger. |
| `RECOMP_AUDIO_CAPTURE` | Records the final mix to this new file as IEEE-float WAV. An existing file is never overwritten. |
| `RECOMP_AUDIO_DUMP` | macOS and Linux: as `RECOMP_AUDIO_CAPTURE`, but overwrites. |
| `RECOMP_AUDIO_TRACE` | Logs DirectSound calls, including 3D position updates. |
| `RECOMP_INPUT_TRACE` | Logs host controller input. |

## Investigation traces

These were added for single investigations and are kept for reuse:
`RECOMP_FAULT_TRACE` (host stack on a guest fault), `RECOMP_WATCH` (hex guest
address whose writers are reported; `RECOMP_WATCH_HITS` sets how many,
default 40; it disables the fast memory path), `RECOMP_XFORM_DUMP_AT`,
`RECOMP_D3D_SLOTTRACE`, `RECOMP_D3D_RTTRACE`, `RECOMP_D3D_WHOCALLS`,
`RECOMP_D3D_YTRACE`, `RECOMP_D3D_YTRACE_DRAW`, `RECOMP_FIBER_FPU` and
`RECOMP_FIBER_WAIT` (code addresses on a fiber's stack each time it yields
from a new place; finds a task that waits forever).

`RECOMP_SPINPROBE`, `RECOMP_REPOST`, `RECOMP_D3D_VIEWSEL` and
`RECOMP_D3D_VIEWARG` came from DOAXBV and read DOAXBV addresses;
`RECOMP_REPOST` also changes behavior. `RECOMP_USER_MUSIC` and
`RECOMP_MUSIC_SHUFFLE` belong to DOAXBV's custom soundtrack, which DOA3
does not have.

## Runner flags

| Flag | Effect |
|---|---|
| `--xbe <path>` | The `default.xbe` to run; its folder is the disc root. Required. |
| `--vsync` | Also syncs presentation to the monitor. |
| `--expect-stop <id>` | Marks the run a match when it stops with this stop ID. |
| `--milestone-log <path>` | Appends the stop ID, result and kernel-call count to this file. |
| `--stop-at <id>` | Stops cleanly at a named boundary. The boundaries live in DOAXBV-bound adapters, so DOA3 does not reach them yet. |
| `--input-start-pulse-at <poll>`, `--input-a-pulse-at <poll>` | Presses START or A at that controller poll. Repeatable. |
| `--input-buttons-at <poll>:<hexmask>` | Presses digital buttons: `0x01`-`0x08` D-pad, `0x10` START, `0x20` BACK, `0x40`/`0x80` thumb clicks. Higher bits are refused. |
| `--input-analog-at <poll>:<index>:<value>` | Presses an analog button: 0 A, 1 B, 2 X, 3 Y, 4 Black, 5 White, 6 left trigger, 7 right trigger; value 0-255. |
| `--input-host-after-poll <poll>` | Hands input to the real controller after that poll. |
| `--input-wait-after-poll <poll>` with `--input-resume-file <path>` | Pauses the script after that poll until the file exists. |
| `--input-paused-analog-file <path>` | While paused, reads one press from this file and deletes it. |
| `--input-record <path>` | Writes every controller sample to an input log and runs on the deterministic clock. Combines with scripted input. |
| `--input-replay <path>` | Feeds the game from an input log instead of a controller. Stops with `input-replay-end:<frame>` when the log runs out, `input-replay-short:<frame>` when it ends partway through a record, or `input-replay-desync` when the game polls a frame or port the log does not have. |
| `--ram-hash-log <path>` | Writes one line per presented frame: frame, swap count, guest clock, clock reads, then a hash of each 4 MiB of guest RAM. Two runs that should match are compared line by line. The runner refuses a path that names the input log or `RECOMP_RAM_DUMP`. |

Any input flag replaces live controller input with the script.
A poll is one sample of one port. Each port is sampled once per presented
frame, or once per 60 Hz frame of guest time while nothing is presented, so
a poll number counts frames per port, not `XInputGetState` calls.

Recording or replaying switches guest time to a counter that advances only
when the game waits, by exactly the time it waits for. Pacing to 60 Hz is
then host-side only, the system clock starts at 2001-11-15, and DirectSound
cursors move on the game thread. Controller ports are sampled the same way in
every mode. A guest loop that spins on the clock without waiting never sees
time move, so the watchdog stops it.
