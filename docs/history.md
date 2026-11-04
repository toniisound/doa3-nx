# DOA3 port history

Written after the fact from the commit history and the main Codex bring-up
session (2026-10-06 to 2026-10-09). It records the order of the work, why
the main decisions went the way they did, and which earlier conclusions
turned out wrong. [bring-up.md](bring-up.md) keeps the run-by-run evidence
and the full list of stops; the subsystem docs keep the addresses.

## State on 2026-10-11

The lifted program boots through the legal notice, the TECMO logo and the
intro movie to the title, menus, Story, Time Attack, Watch and Sparring
fights with audio, stage transfers, continue and attract mode, paced at
60 Hz. A scripted Story run reaches the final fight with Omega, and its stage
now renders. Stage collision is hand-written C; the rest of the game is still
lifted. After two private test ZIPs (0.0.1 and 0.0.2), 0.1 Alpha and 0.2
Alpha were published on GitHub from the public line. Local `main` now
follows the public line, with the old local history kept as the
`private-history` branch.

## Timeline

Times are America/Phoenix. "Stop" is the runner's own stop report; the
kernel-call count shows how far a run got.

| When | What happened | Commits |
|---|---|---|
| 10-06 afternoon | Scaffolded from the doaxbv-re runtime at `f6ad13e`; lifter pinned to `codex/doa3-recipe` (DOAXBV recipe merged with upstream main) | `53d82f5`, `5583b25` |
| 10-06 | First bounded run: 7,282 functions lifted with none failed; stops in the CRT `memcpy` after 8 kernel calls | `bcc3a33`..`4462ec1` |
| 10-06 | Three parallel agents fix the lifter's inline jump tables, find the D3D8 3925 entry points and rebuild the frame adapter around Present. Stop moves to `_initterm` (162 calls) | `410278f`..`6525bf3` |
| 10-06 | Static callbacks no longer hidden by alias ranges; stop moves into XPP's device tables (171 calls) | `a2751e6`..`d65bb5a` |
| 10-06 evening | XPP, the D3D miniport GPU layer, DirectSound and the CRI ADXM servers replaced; I/O completion APCs; the main loop presents black frames at 60 fps | `5eb1e12`..`5a54a8c` |
| 10-06 night to 10-07 morning | Save-space warning, utility drive storage, cooperative guest threads, a legal-notice regression, a run of lifter callback-discovery fixes, Sofdec movie alpha and the GPU fence. Title, Mode Select and character select reached | `40da0e7`..`9086e74` |
| 10-07 | Audio through XAudio2, combat rendering, voice headroom, movie vblank accounting, unpaced test mode, sound-buffer recycling, image-gap RAM and `AvSetDisplayMode`. Whole fights with sound | `39528fe`..`f3c97fe` |
| 10-07 afternoon | Walls and danger-zone falls fixed by a lifter fix to `cmp` joins; the scripted title route changes | `d8ff60b`..`16d361c` |
| 10-07 evening | Stage transfers confirmed; shift masking, narrow `imul`, `fnsave`/`frstor` and false function starts fixed in the lifter; the window title marks unpaced runs | `01f0c63`..`a348500` |
| 10-07 night | First boot with an empty cache, movie decoder speed, flags passed across calls; continue and attract mode confirmed | `b823005`..`a507e35` |
| 10-08 morning | `tools/play.ps1`; hand-written collision; an intermittent fighter crash fixed in the lifter; legal notice fast-forward | `997f060`..`4bfa8bf` |
| 10-08 afternoon | Tester build and packaging, test release 0.0.1; 3D audio (panning, then distance, Doppler and HRTF); music static | `ee40232`..`66a61d4` |
| 10-08 evening | SMAA, widescreen notes, `Launcher.cmd`, unclipped HRTF, test release 0.0.2, select-screen viewport | `9e4b50f`..`d52440d` |
| 10-09 | Public sync as one commit on top of public `main`, opened as PR #3 | public line `5d35085` |
| 10-09 early morning | Omega's final stage drew black under the HUD; its frame-buffer pass now blends over the scene | `bb479cf`, merged as `1581a18` |
| 10-09 morning | 0.1 Alpha prepared on PR #3 (changelog, launcher fixes for paths with spaces and failed ISO extraction), then published; public `main` fast-forwarded and tagged `v0.1` | public line `d4596dc` |
| 10-09 morning | PR #2 rebased onto the release and landed on public `main` by fast-forward; the PR was closed | public line `0fcb61d`, `54bbb4e` |
| 10-09 evening | 0.2 Alpha (PR #4), worked in separate worktrees: lighting, journaled saves, kernel timer and memory fixes, released as `v0.2` | public line `5747204`..`08cf8ae` |
| 10-11 | Local `main` moved onto public `main`; the old local line kept as `private-history` | public line `08cf8ae` |

## Decisions

### DOAXBV bindings are gated, not deleted

Every adapter arrived bound to DOAXBV addresses, which in DOA3 would have
hijacked unrelated functions. `recomp_lookup_manual()` answers DOAXBV
addresses only when `RECOMP_DOAXBV_BINDINGS` is defined, and only the
adapter tests define it, so the tests keep exercising the adapters while
each one is re-found and rebound for DOA3.

### Libraries are replaced at their API; game code is lifted

The XPP stop forced the first real choice. Lifting XPP's ten table targets
would have cleared that stop, but lifted XPP then drives the USB host
controller, and no controller ever appears without emulating USB end to end,
which AGENTS.md rules out. Binding the eight XPP entry points to the
existing native input model took a signature search and no new mechanism, and
gave real controller input. The same reasoning replaced DirectSound, the
fiber service, the CRI ADXM server pump and the D3D GPU layer.

D3D was replaced one layer down from where DOAXBV replaced it. DOA3's 3925
CreateDevice builds its frame buffers inside the static device at
`0x001C0800`, so CreateDevice stays lifted, the miniport layer under
`CDevice::Init` is replaced, and the frame model starts from the static
device on the first Clear, Present or SetGammaRamp. DOA3 has no
`D3DDevice_Swap`; frames end in Present.

### Lifter bugs are fixed in the lifter, and repeated stops end runtime changes

Generated C is never edited. When two runs stopped at the same place, runtime
changes stopped, the runs were compared to the first point they differed, and
one hypothesis was tested. Most of those cases ended in the lifter, each fix
as a commit on `codex/doa3-recipe` with a synthetic test. The classes of
lifter bug found:

- jump-table arms placed after their inline table (`memcpy`);
- alias ranges about 100 KB long hiding static initializers;
- callback discovery: no-return calls padded with `int3`, tail closure,
  forwarding thunks, shared tails, and a fixed 64-instruction probe replaced
  by a bounded control-flow proof;
- branches joining flags from two `cmp` instructions of different widths
  (the wall and fall bug);
- unmasked shift counts, 8-bit `imul` treated as 32-bit, skipped
  `fnsave`/`frstor`;
- padding and bad jump-table entries taken as function starts;
- flags read across calls and returns;
- an integer immediate inside an instruction accepted as a function start,
  truncating the callback around it (the intermittent fighter crash).

### Guest threads run cooperatively

Guest threads first ran inline to completion. DOA3's cache worker never
returns, so the game task that created it never resumed. Each guest thread
now runs on its own fiber and switches at blocking waits, vblank, and resume
or priority changes. Giving threads stacks then pushed the heap past the
game's 1.25 MiB font allocation, which blanked the legal screen; reclaiming
an obsolete 1 MiB startup-stack reservation fixed it.

### One guest clock, and an unpaced test mode

Runs were capped at 55 seconds, too short to reach late fights at real speed.
`RECOMP_UNPACED=1` advances one shared guest clock to the next idle deadline
instead of waiting, so vblank, QPC, timestamps, kernel waits and sound
cursors all speed up together. It is what produced the 120-180 FPS seen
during testing; `tools/play.ps1` clears it. The first boot's legal notice
reuses the same skip once, and vblank deadlines are compared against the
guest clock so the first paced frame after a skip does not wait out the
skipped time.

### Flags cross calls despite the size cost

The last three branches that could never be taken read flags set by the
calling function. Passing flags through `g_eflags`, saved per guest
thread, removed them at the cost of about 45% more generated C and a
2.5-minute Release build. Paced fights still measure 60 FPS.

### Collision is the first hand-written game code

Stage collision was chosen first because it had already been proved wrong
once and because a differential harness existed for it. The boundary
exemption, action-category match, per-frame boundary pass and danger-zone
pass are ordinary C behind thin adapters. `tools/doa3_abi.py` compares all
RAM plus callee-saved registers between the hand-written and lifted versions;
the geometry and math helpers they call stay lifted.

### Audio levels follow the library and xemu

DSOUND 3936 reserves 6 dB of headroom per ordinary voice, and the adapter now
applies it. GetCurrentPosition reports the position already sent to XAudio2,
so the game never refills ring data that has not played. For HRTF, DOA3's
filter table carries no scale, and a guessed scale clipped fight effects. The
filters are now normalized the way xemu does it (each ear divided by the sum
of its absolute taps) and 3D voices stay floating point until the mix. This
matches xemu, not a recording of real hardware. Fight effects average about
-28 dB and the left/right difference is subtler than before.

### Widescreen stays opt-in

DOA3's box lists no 16:9 support, but the game honors the dashboard flag and
renders a true anamorphic 3D view with more of the stage at the sides. Its 2D
layer is not adjusted, so the HUD, menus and subtitles stretch by a third.
DOAXBV has no HUD correction to port, and drawing the 2D layer at 4:3 would
leave bars beside full-screen fades, so it was left alone.

Later the presenter narrowed only screen-space quads narrower than the
centered 4:3 area, which leaves fades full width. The movie code already
pillarboxes its quad to that area at 16:9 (0-720 at 4:3, 90-630 at 16:9),
while subtitles and the watermark draw identically in both modes. Frame
dumps of Mode Select, Character Select, the pause menu and a Story fight
show the 2D layer at its 4:3 shape.

### Test releases build from the tester's own disc

The ZIP holds runtime source, the pinned lifter source, the DOA3 function
lists, `extract-xiso` and the launchers; it holds no game data.
`tools/build_game.py` checks the XBE hash and refuses a lifted program
whose manifest differs from `PROGRAM_SHA256`, so changing the lifter pin,
`tools/doa3/*.json` or `program_forwards.c` means regenerating and
updating that hash before packaging. `tools/package_test.py <version>`
writes the ZIP to `private/release/`; SMAA's 48 MB demo folder is left out.

### The public sync is one commit

The public history was rewritten earlier to remove a private path from
`AGENTS.md`, and local `main` still contains it. Pushing local history
would publish the path again, so `codex/public-sync` holds a single commit on
top of public `main`, and `public-export.json` on that line lists what
is exported. Files added on local `main` reach the public tree only once
they are listed there. On 10-11 local `main` was moved onto the public line,
so it picked up PR #2 and the 0.2 work and no longer contains the private
path. The old local line is kept as `private-history`, which still contains
the path and must not be pushed.

### Frame-buffer textures read the host frame

Omega's stage ends each frame with a full-screen pass that blends a slightly
zoomed copy of the frame over the scene. The runtime decoded neither of its
constant-alpha blend factors, so the pass drew opaquely, and its texture
pointed at guest frame-buffer memory, which stays zero because the port
renders on the host GPU. Together they covered the scene with black. The
constant-alpha factors now map to the host's blend factor with the alpha
replicated, and a texture over either embedded frame-buffer surface samples
the host back buffer. On hardware that texture alternates between the current
and previous frame as buffers flip. The first fix always used the current
frame; since 0.2, `GetBackBuffer(-1)` samples a copy of the previous frame
instead. No hardware capture exists to compare the haze against.

### Releases ship from the public line

Releases are tagged on public `main` and built from it, so a release
contains only what has been synced there. 0.1 Alpha is not marked as a
prerelease, so it shows as Latest the way doaxbv-re's releases do. Its ZIP
was rebuilt from a fresh folder against the disc and produced the same
program manifest as the play build. The uncommitted launcher redesign was
left out of 0.1, and so was PR #2, which conflicted with the release and had
not been tested on DOA3.

### PR #2 keeps DOA3's pacing

PR #2 brought doaxbv-re's frame timing, which would have replaced the
guest-clock vblank pacing that fixed DOA3 running too fast. The rebase kept
DOA3's pacing unchanged and took only the sleep helper the new
variable-refresh option needs, with one timer per thread because both the
guest and the presenter sleep. Two presenter test programs link the guest
clock and a vblank stub, as the vblank test already did. Marking PR #2 merged
would have needed a force push of its branch, so it was closed with the
landed commits listed instead.

## Conclusions that turned out wrong

| Earlier claim | What was true |
|---|---|
| The game loops over the T: directory | A bounded FindFirstFile search over 19 slots; the real wait was the CRI main server |
| Fighters take no damage | A visual misreading. Health falls (300 to 177 in one Story fight); Story's infinite timer is the retail design |
| Stage transfers are broken | The scripted inputs hit solid walls. Breakable walls, windows and terraces all transfer |
| The runtime runs the game at 120-180 FPS | That was `RECOMP_UNPACED=1` from the test scripts |
| The title drops to about 20 FPS | That was the intro movie, which runs at 30 pictures a second; the title holds 60 |
| The ADPCM decoder drops one sample per block, causing static | The 64th nibble is zero padding in every block DOA3 creates; the static came from the music ring cursor |
| The title START route works | It worked only because of the `cmp` join bug; fixing it needed one more START and later inputs shifted by 105 polls |
| The select-screen model spills out of its box | Fixed-function draws ignored the viewport, so the whole model was laid out for the full screen |

## How runs were checked

- Bounded runs used `--expect-stop`, `--milestone-log`, `RECOMP_WATCHDOG_MS`,
  isolated storage and a 55-second hard cap. Repeated stops were compared by
  their normalized kernel-call records to find the first difference.
- Scripted controller input (`--input-*-at`) drove menus and fights; no
  guest state was forced. The working route is in [bring-up.md](bring-up.md).
- Frame dumps, draw capture and audio capture recorded what the player would
  see and hear; `RECOMP_PERF_COUNTER` and PresentMon measured pacing.
- Suspect lifted functions ran on the same RAM snapshot in a native DLL and
  as original x86 in Unicorn. This proved the wall bug and later checked the
  hand-written collision.
- `fpdiff`, on the lifter branch, runs instruction patterns natively and
  through lifted C. After the integer and x87 fixes it found no difference
  across 585,794 inputs.
- Headless Ghidra cross-checked function boundaries the lifter disputed.
- The runner must start visible; the presenter checks its window on the first
  present.

The options and flags are listed in [runtime-options.md](runtime-options.md).

## Open items

- The Story ending has not been reached; a scripted run reaches the final
  fight with Omega.
- How long the first-boot cache fill takes across boots has not been measured
  or compared with hardware.
- One 161 ms movie stall was seen once and not again.
- Runtime replacements of kernel and library calls do not update the flags
  that now pass across calls. Nothing has been seen to depend on them.
- The cage-stage light flare stopped appearing without an identified cause.
- Unpaced runs drop queued audio buffers.
- Widescreen 2D is corrected by a width rule, not by the game's own layout:
  a screen-space quad narrower than the centered 4:3 area is narrowed. A
  narrow quad meant to reach the screen edge would be pulled in, and one draw
  that batches 2D quads across the 4:3 area keeps its stretch.
- Point lists with FVF `0x42` (two to six points) were declined in one play
  session; their stage and render states have not been traced, and they now
  draw through the general point path.
- The HRTF level matches xemu, not measured hardware.
- The launcher was tested by a script that picked settings, not by clicking
  its window. The test ZIP was built and run only on the development machine.
- The frame-buffer haze on Omega's stage has not been compared with hardware.
