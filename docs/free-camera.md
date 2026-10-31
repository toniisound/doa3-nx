# Free camera

The free camera takes over the game's camera and flies it anywhere in the
scene, for screenshots or a closer look at the fighters and stages. It is off
by default. Tick **Free camera** in the launcher, or set
`RECOMP_FREE_CAMERA=1` before the runner starts. Then, in a fight or a
cutscene, press **F1** or **`** (backquote), or click the right stick, to
take the camera; press again to give it back. The controls follow the DOAXBV
port's free camera, with one change: DOA3 acts on **Back** (it resets
training and skips cutscenes) before a combo could catch it, so no control
uses Back, and the controller's screenshot is the d-pad's right instead of
Back + A.

While it flies, the pad and keyboard are the camera's, and the game reads a
still controller, so it ignores what moves the camera. The game itself keeps
running until you freeze it.

| Action | Keyboard | Controller |
|---|---|---|
| Take / give back the camera | **F1** or **`** | Click the right stick |
| Move | **W A S D** | Left stick |
| Down / up | **Q** / **E** | Left / right trigger |
| Look | Arrow keys, or drag with the right mouse button | Right stick |
| Zoom in / out | **Z** / **X** | (move closer with the left stick) |
| Faster / slower | **Shift** / **Ctrl** (held) | **A** / **B** (held) |
| Next camera the game built | **C** | **X** |
| Back to the view it started from | **R** | **Y** |
| Freeze / resume the game (P works anywhere, camera on or off) | **P** | **Start** (while flying) |
| Clean view: hide the panel and the game's 2D overlay | **H** | Left shoulder |
| Advance one frame while frozen; hold to keep stepping | **.** (period) | Right shoulder |
| Turn to the next fighter and choose her | **\\** (backslash) | D-pad left |
| Previous / next swimsuit on the chosen fighter; hold to scroll, worn when let go | **-** / **=** | D-pad down / up |
| Screenshot of the game's frame, without the panel | **F12** (works with the camera off too) | D-pad right (while flying) |

While the camera flies, a panel at the top left names the camera, whether the
game is frozen and, with [DOAXBV swimsuits](doaxbv-swimsuits.md) on, the
chosen fighter's swimsuit; a line along the bottom lists the controls: the
keyboard's after a key was last used, the controller's after a controller was
(and from the start on a Steam Deck). The camera starts where the game's
camera was. Its speed scales with how far the game's camera stood from what
it looked at, and it steps by the time since its last step, so it flies at
the same speed at any frame rate. Giving the camera back resumes a frozen
game.

Clicking the left stick opens the [swimsuit menu](doaxbv-swimsuits.md)
while the camera flies too; while the menu is open it has the controller,
and the camera and the right stick's click wait until it closes.

Linux and macOS save the launcher choice in `private/launcher.json` as
`free_camera`, and `tools/run_game.py` turns it into the variable. The Windows
launcher sets the variable itself.

## Screenshots

**F12**, or the d-pad's right while flying, saves the game's frame as a PNG
without the panel or anything else the runtime draws over it. Screenshots go
to `DOA3` in the user's Pictures folder: on Windows the Pictures known
folder, which follows a relocated or OneDrive-backed Pictures; elsewhere
`~/Pictures/DOA3`. `RECOMP_SCREENSHOT_DIR` chooses another folder. Paths are
UTF-8, and Windows opens them as UTF-16, so any user name works.
`screenshot.c` writes the PNG without an image library. The Direct3D 11
presenter saves the window's back buffer, the SDL_GPU presenter the game's
frame at its rendering size.

## How it works

The free camera works on both discs. The addresses below are USA 3.0's;
Japan 3.1's are in the table after them.

DOA3 keeps five camera objects of 0xCC0 bytes from 0x003C29C0. The game's
camera logic sets each one's eye (+0x00), target (+0x10) and vertical field
of view (+0x30) and roll (+0x34), both in radians, and keeps a working copy
of them at +0xA8, +0xB8, +0xD8 and +0xDC. 0x0002A540 builds the view and
projection matrices (+0x150, +0x190) that the frame's draws use from that
copy, rolled by +0xDC. It takes the object in ESI. The free camera writes
its eye, target and field of view in both places and a level roll in the
working copy: the game keeps animating the roll while the camera flies
(intro shots roll up to about 50 degrees, fights sway it slightly), which
would otherwise tilt the view.

| | USA 3.0 | Japan 3.1 |
|---|---|---|
| Camera objects | 5 x 0xCC0 bytes from 0x003C29C0 | 5 x 0xC80 bytes from 0x004DAE90 |
| Camera build, object in | 0x0002A540, ESI | 0x00027720, EAX |
| Camera task | 0x0002D060 | 0x000235B0 |
| Fighter layout task | 0x000B4AC0 | 0x000B4720 |
| Task table, running task | 0x005E5A08, 0x005E5C4C | 0x0073C400, 0x0073C644 |

The object's fields up to the matrices sit at the same offsets on both.
Japan's were matched by code shape: the same instructions with other
registers and addresses, called from the same places.

`recomp-runtime/program_forwards.c` (Japan: `program_forwards_jp.c`) wraps
that build. Its line `extern void sub_0002A540_gen(void);` tells the lifter
(`--exclude-manual`) to emit the generated body as `sub_0002A540_gen` and
send every call to `sub_0002A540`, which the file defines: the wrapper puts
the free camera's eye, target and field of view in both places, then runs
the game's build (`recomp-runtime/free_camera_adapter.c`). The game then
builds its matrices, and culls, for the free camera, so nothing at the edges
of the new view goes missing. The values stay in the object, since the game
culls against the eye and target it finds there; when the camera is given
back, the game's camera logic takes the object over again. Because the wrap
changes the generated program, `tools/build_game.py` authenticates the
re-lifted program (`PROGRAM_SHA256`). The flying itself is
`recomp-runtime/free_camera.c`, shared with the DOAXBV port's free camera
and tested by `free_camera_test.c`.

The free camera flies the first camera object built after it is taken up; in
fights and their cutscenes that is the main view. It takes the next one
built whenever the one it flies stops being built, and **Next camera** moves
it to the next object the game built in the previous frame.

## Freezing the scene

DOA3 runs its systems as cooperative tasks, one turn each per frame on its own
fiber. The task table at `0x005E5A08` holds 17 records of 32 bytes: `+0` the
state (bit `0x10` running), `+4` frames to wait, `+8` the fiber, `+0xC` the
function; the scheduler keeps the running task's index at `0x005E5C4C`.
`recomp-runtime/scene_freeze_adapter.c` hooks the fiber adapter's
`SwitchToFiber` and, while frozen, declines the turns of tasks that have not
drawn lately (`scene_freeze.c`, shared with the DOAXBV port and tested by
`scene_freeze_test.c`). The drawing tasks keep their turns, so the last state
is drawn again, and presented, every frame from wherever the camera is. Two
tasks that draw nothing are kept too: `0x000B4AC0` lays out the fighters for
the drawing tasks, and `0x0002D060` builds the cameras while the free camera
flies. The game polls the controller from a task the freeze skips, so while
frozen the camera samples it once a frame itself. A frame step lets every
task take its turns until the next frame is presented, then holds the scene
again.

The hooks are installed only with `RECOMP_FREE_CAMERA=1`; without it, input
samples and task switches go through unhooked.

The clean view leaves out the game's draws that are already in screen space
(the life bars, timer, names and subtitles) in `d3d_draw_adapter.c`; DOA3
draws them from the same task as the fighters, so they cannot be left out by
task as in the DOAXBV port. Subtitles are timed by a clock the freeze does not
stop, so a line can end while the scene is frozen. A movie the game draws in
screen space is left out too; H shows it again.

## Keys and the mouse

The input host (`input_host_win32.c`) reads the keys the camera uses for
itself: on Windows with `GetAsyncKeyState` while the game window has the
keyboard, elsewhere from SDL's keyboard state. A right-mouse drag turns the
camera by the pixels moved. On Windows it honors swapped mouse buttons (the
logical right button) and starts only over the game's window.

For the game, the input host presses controller buttons for some keys: A, S,
Z, X, Q, W, E and R press X, Y, A, B, White, Black and the triggers, the
arrows press the d-pad, and Return and Backspace press Start and Back. The
free camera leaves those buttons out of what it reads while their keys are
down, since it reads the keys themselves: otherwise holding S would press Y
and reset the view, and the right arrow would take a screenshot.

The swimsuit menu keeps the stick clicks from the game. The input host
hands the camera the pad's R3 apart from the rest of the sample, so the
camera's toggle works with the swimsuits on.

## Options

| Variable | Effect |
|---|---|
| `RECOMP_FREE_CAMERA=1` | Allows the free camera (the launcher's **Free camera**). |
| `RECOMP_SCREENSHOT_DIR` | Folder for screenshots. |
| `RECOMP_FREE_CAMERA_AT=<n>` | Test: takes the camera up at the n-th camera build. |
| `RECOMP_FREE_CAMERA_DRIFT=right,up,forward,turn,pitch` | Test: holds those controls (each -1 to 1) while flying. |
| `RECOMP_FREE_CAMERA_FREEZE_AT=<frame>` | Test: freezes the scene at that presented frame (`RECOMP_FREE_CAMERA_HIDE=1` also hides the overlay). |
| `RECOMP_FREE_CAMERA_TEST_AT=<frame>` | Test: requests a screenshot and moves the chosen fighter's swimsuit on one. |

## Recorded runs

On 2026-10-22, in the attract fight (`--input-start-pulse-at 1000000000`),
with the camera taken by `RECOMP_FREE_CAMERA_AT=9836` at the fight's
close-up, frozen by `RECOMP_FREE_CAMERA_FREEZE_AT=13030` and orbiting with
`RECOMP_FREE_CAMERA_DRIFT=0.25,0,0,-0.2,0`; frame dumps at 2x scale:

- macOS (Apple Silicon, SDL_GPU on Metal): the fight held mid-kick while the
  camera moved around it; with the camera still, frozen frames six seconds
  apart were identical. The clean view left out the attract's logo, and
  `RECOMP_FREE_CAMERA_TEST_AT` saved a 1280x960 PNG of the game's frame and
  moved P1's swimsuit.
- Windows 11 (RTX 3070, Direct3D 11): the same, with the panel (camera,
  frozen, P1's swimsuit) and the keyboard's or, with `SteamDeck=1`, the
  controller's hint drawn over the game; the screenshot left the panel out.

On 2026-10-26, rebased on the Japan main build, on macOS: in Japan 3.1's
attract fight (camera taken at build 3150, frozen at frame 11450, the same
drift), the camera moved around the frozen fighters, frozen frames were
identical, the fighters stayed while frozen, the clean view left out the
logo and the test screenshot was saved; USA 3.0 repeated its run above.

Keys, the mouse and the pad buttons were not pressed in these runs; the
test variables stand in for them.
