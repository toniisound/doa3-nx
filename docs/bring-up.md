# DOA3 bring-up

Status of the first whole-program run of Dead or Alive 3 (Xbox, USA) on the
recomp runtime. Everything derived from the game stays under `private/`.

## Lift

Run from `tools/xboxrecomp` (branch `codex/doa3-recipe`) against a copy of
`default.xbe` in `private/lift/`, so the parser's analysis JSON does not land in
the verified disc import. The current lift uses `c397950`, the tracked manual
list `tools/doa3/manual_functions.json`, the seed list
`tools/doa3/seed_functions.json` (one observed entry; the game reset
is also discovered without its seed), and
`--exclude-manual` on `program_forwards.c`, which wraps the CRI status polls as
`sub_X_gen`. Use fresh disassembly and generated-output directories when
changing lifter revisions so cached results cannot hide the change.

```powershell
python -m tools.xbe_parser ../../private/lift/default.xbe --json ../../private/lift/default_analysis.json --quiet
python -u -m tools.disasm ../../private/lift/default.xbe -o ../../private/lift/disasm-seed -v `
  --seed-functions ../../tools/doa3/seed_functions.json
python -u -m tools.recomp ../../private/lift/default.xbe --all --split 1000 --game-name "Dead or Alive 3" `
  --functions ../../private/lift/disasm-seed/functions.json --labels ../../private/lift/disasm-seed/labels.json `
  --func-id-dir ../../private/lift/no-func-id --abi-dir ../../private/lift/no-abi `
  --icall-sites ../../private/lift/no-icall-sites.json `
  --manual-functions ../../tools/doa3/manual_functions.json `
  --exclude-manual ../../recomp-runtime/program_forwards.c `
  --gen-dir ../../private/lift-cri2/generated --output-dir ../../private/lift-cri2/metadata
```

Disassembly takes every code section (no `--text-only`), because D3D, DSOUND and
XPP code lives outside `.text`. No recipe, recoveries, func_id or ABI input
were used; the empty paths keep stale lifter output out.

| Result | Value |
|---|---|
| Functions | 7,099 from disassembly, 7,146 after callback recovery and manual-entry handling |
| Translated / failed | 7,074 / 0; 72 entries without generated bodies; 2 generated bodies wrapped as `sub_X_gen` |
| Generated C | 1,138,393 lines in 8 chunks |
| Unresolved call targets | 42, almost all data decoded as `call` |
| Untranslated instructions | 185 sites, 36 mnemonics, emitted as `RECOMP_UNIMPL` |
| Lifter warnings | identified and ABI inputs not found (intentional) |

Lifter `799afc8`..`c397950` replaces the weak callback instruction budget with
a bounded CFG proof using the existing decoder and measured switch tables.
Every local edge must reach a decoded instruction; invalid instructions,
privileged/trap operations and overlapping streams fail the probe. Existing
shared tails and the no-return-call padding pattern remain supported. Table
callbacks establish ownership before weak immediates, and that ownership
survives repeated passes. Callable shared entries are retained; suffixes that
restore registers absent from their decoded saves are rejected. Otherwise an
immediate naming a valid instruction suffix can split its callback before
callback discovery runs. Direct tail dependencies are proved in their own
bounded gaps, and an external backward tail is not treated as a closed loop.

Against a fresh seed-free `b8665cb` disassembly, the seed-free result adds
**60 entries**, removes **342 entries**, and changes **24 existing ends**.
`0x000E00D0`, `0x001E3720` and `0x0016ABF0` are present without seeds. The seven
suspect words (`0x001A0001`, `0x001A001A`, `0x001A002C`, `0x001A2306`,
`0x001B001B`, `0x001B0032`, `0x001B0089`) remain absent in disassembly and
generated dispatch. Guided Ghidra disassembly places them inside three
initializer bodies; it also places `0x001A4000` inside `0x001A3BC0`.

Most untranslated sites (`aam`, `arpl`, `insb`, ...) are data decoded as code.
`in`, `out`, `wbinvd`, `cli` and `sti` are real hardware access.

## Build

The hand-written collision entries in `recomp-runtime/doa3_collision_adapter.c`
are boundary exemption (`0x000A1380`), action-category matching (`0x000A9AC0`),
the per-frame boundary pass (`0x0008D180`) and the danger-zone pass
(`0x000A40E0`). Ordinary C functions implement the logic; thin cdecl adapters
read arguments and return through AL or pop the return slot for the void passes.
`doa3_fighter.h` names the guest fields they use. Tables remain in guest RAM.

The four addresses are in `tools/doa3/manual_functions.json`. Regenerate when
using these bindings: the lifter omits their bodies and routes callers through
manual dispatch. There is no collision-specific `RECOMP_ABI_CALL` override or
runtime enable switch. Private differential harnesses supply the original lift.

The auto-save (`0x00021930`) is hand-written in `recomp-runtime/save_adapter.c`
and is also in the manual list. It is the only routine that writes the game's
save file (XAPI still writes the nickname and title metadata under UDATA): one
`CREATE_ALWAYS` write of the title-data save file named at `0x0021B50C` (0x3C0C bytes from `0x00484D78`,
their XOR and padding), then 62 "NOW SAVING" frames. The save journal brackets
it, and the journal now covers all of partition 1 (`UDATA` and `TDATA`). A
failed or short write rolls the previous save back, and an interrupted save is
restored at the next start. Paced runs exercised all three: a normal save, an
injected short write (`RECOMP_SAVE_SHORT_WRITE_AT=1`) and an injected exit
after the write (`RECOMP_SAVE_INTERRUPT_AFTER_WRITE=1`) followed by a boot.

`tools/doa3_abi.py` compares all RAM except the dead callee stack below the entry
return-address slot, plus ESP, EBX, ESI, EDI and EBP. The return slot and caller
arguments remain compared. It supports AL/AX/EAX return widths and normalized
x87 ST0 values. These predicates return AL; neither pass has a used return value.
The caller audit found 26 action-predicate calls, two exemption calls, one
boundary-pass call and its tail call to the danger pass. No direct caller uses
ECX, EDX or upper EAX before redefining them or making its next ABI call.
Subsequent stack-reservation `push ecx` instructions are not argument passing;
`sbb eax,eax` computes from carry independently of incoming EAX.

The geometry query (`0x000D6170`), region queries, action-table matcher,
reaction/transfer helpers and angle/matrix helpers remain lifted. One shared
cdecl bridge handles stack arguments; small wrappers supply the math helpers'
register arguments and consume their x87 return values. Named guest scratch
holds output parameters that still-lifted callees need as guest addresses.
The danger pass retains a compatibility read of two originally uninitialized
stack bytes on the stage-0x35 branch; this is documented beside the binding.

Lifter `8879b52` publishes general-register EBP as well as frame-pointer EBP
before direct and indirect calls, and publishes restored EBP on return. This
prevents geometry helpers from saving a stale callee frame when the boundary
loop uses EBP as a table pointer. Its regression uses synthetic instructions;
the runtime tests and ABI-gate tests also use only synthetic inputs.

The manifest is computed the way doaxbv-re's `build_game.py` does it: one
`name<TAB>size<TAB>sha256` line per `recomp_NNNN.c`, then `recomp_dispatch.c` and
`recomp_funcs.h`, hashed with SHA-256.

```powershell
cmake -S recomp-runtime -B build/recomp-program -G "Visual Studio 17 2022" -A x64 `
  -DRECOMP_PROGRAM_DIR=private/lift-init2/generated `
  -DRECOMP_PROGRAM_MANIFEST_SHA256=<manifest sha256> `
  -DRECOMP_PROGRAM_EBP_EXPECTED=0
cmake --build build/recomp-program --config Debug --target recomp_program_runner
```

`RECOMP_PROGRAM_EBP_EXPECTED` is 0: upstream's emitter initializes `ebp` itself,
so the uninitialized prologue local that DOAXBV's build patched (11,052 times)
no longer occurs. Configure generates 83 fail-loud stubs: absent bodies, direct
calls to garbage addresses from data decoded as `call`, and DOAXBV bodies named
by the gated adapters.

Upstream also writes its own `recomp_types.h` beside `recomp_funcs.h`. Configure
now copies `recomp_funcs.h` next to the derived chunks and leaves the snapshot
directory off the include path, so generated code includes the runtime's header.

## Runtime changes

- Without `RECOMP_DOAXBV_BINDINGS`, `recomp_lookup_manual()` answers only the
  rebound DOA3 entry points: XPP input (`input_adapter.c`), the D3D 3925 GPU
  layer under `CDevice::Init` and KickOff (`d3d_miniport_adapter.c`), the 26
  DirectSound 3936 wrappers game code calls (`dsound_api_adapter.c`), and the D3D
  frame functions (SetGammaRamp `0x001B0E00`, Clear `0x001B3390`, Present
  `0x001B5850`). Only `recomp_runtime_test` defines the switch. The DOAXBV
  vibration default poke in `runner.cpp` is behind the same switch. Kernel
  imports are unaffected.
- DirectSound now submits PCM and decoded Xbox ADPCM to XAudio2. Buffers remain
  handles in unmapped guest space, with separate guest and output cursors on
  one clock. Three additional public entries cover HRTF selection and the
  retail SetCooperativeLevel/Unlock no-ops. See [DOA3 audio](doa3-audio.md) for
  capture controls, playback coverage and remaining DSP/sync limitations.
- `ADXF_GetStat` `0x00168DE0` and `ADXF_GetPtStat` `0x00169150` remain
  wrapped in `program_forwards.c` for non-yielding polls: each runs one pass
  of the two vertical-blank servers and the main server
  (`cri_adxm_adapter.c`), then the generated body. The CRI workers now also
  run through the cooperative scheduler; see the current stopping point.
- `NtReadFile` queues its APC (ReadFileEx completion) and alertable
  `KeDelayExecutionThread`/`NtWaitForSingleObjectEx` deliver it;
  `NtUserIoApcDispatcher` (232) calls the completion routine.
- The frame model starts lazily from the static device `0x001C0800` on the
  first Clear, Present or SetGammaRamp, because DOA3 keeps the generated
  CreateDevice.
- `RECOMP_WATCHDOG_MS` samples registers, dispatch frames and the guest stack
  from a host thread, then stops. It found both spins below.
- Upstream emits ordinary direct calls as `RECOMP_ABI_CALL(va, sub_X)`, which
  bypasses `recomp_lookup_manual()`. For an address in `--manual-functions` it
  generates no body and emits every call as indirect dispatch, which checks
  `recomp_lookup_manual()` first. `program_forwards.c` still defines each `sub_X`
  (the dispatch table names it); it stops with `manual-unbound:<va>` if nothing
  is bound.
- `recomp_types.h` gained what the generated code uses: `RECOMP_ABI_CALL`,
  `RECOMP_ICALL_SAFE_AT`, `RECOMP_FP_PC` with `recomp_fp_round24`, `RECOMP_FCMP_CC`,
  `g_fp_cc`, `recomp_fxam`, `recomp_frndint`, `recomp_fist`, `RECOMP_DF_STEP` and
  `g_df`, `RECOMP_ATOMIC_CAS32`, `RECOMP_UNIMPL`, `xbox_ReadTimeStampCounter`,
  `ROL8`/`ROR8`/`RC_ROT`, `XMM_CMP_PRED`, and `XBOX_FS_BASE` (0, where the runner
  builds the TIB). MMX registers are now `RecompMmx` lanes in `RecompRuntime`,
  with the 15 MMX ops DOA3 emits; the old-lifter `MMX_CVTPS2PI` is gone.
  `RECOMP_UNIMPL_TRAP=1` stops at the first untranslated instruction reached.
- `recomp_debug_service` (`int 0x2d` DbgPrint) prints to stderr.
- Every costume and character is offered. The save block holds one byte per character at
  `0x00487806`: the costume tiers earned (new saves start at 2, the maximum
  is 3 on USA and 5 on Japan 3.1). The select screen reads `min(byte, the
  slot's tier count)`. `recomp_save_unlock_all()` (`save_adapter.c`) raises
  each byte to its slots' largest count from the game's own table, and sets
  the extra-characters flag at `0x00488837` to 1, on every Present, so new
  and loaded saves both show every tier, and the next auto-save stores them.
- `KeQueryPerformanceCounter` (126) and `KeQueryPerformanceFrequency` (127) are
  served from the existing XAPI time model. DOA3 imports them; DOAXBV did not.
  `KeQueryInterruptTime` (125) is derived from the same counter, and
  `IoDismountVolumeByName` (91) returns success because no volume cache is
  modeled. No run has reached either call yet.
- Kernel data exports and the synthetic thread objects moved from
  `0x00740000` to `XBOX_KERNEL_DATA_BASE` (`0x00F80000`). The old block sat inside
  DOA3's `.data` BSS (`0x00219640`-`0x00C27F5C`). The runner now refuses an XBE
  image that reaches the block.
- `xbe/` (the runner's XBE parser) was copied unchanged from doaxbv-re.

## Current stopping point

Lifter `6fec6e5` fixes a truncated callback behind the intermittent fighter
traversal crash. `0x0004F390` initializes EDI to the fixed fighter array and
advances it by `0x68`; it does not load EDI from a pointer list. Its callee
`0x0004EFA0` can call `0x0004E7B0`, whose fall-through epilogue was missing.
An unrelated integer immediate `0x0004E800` was accepted as a function start
inside the instruction at `0x0004E7FF`, during the same discovery pass that
accepted the enclosing callback. The resulting body ended at `0x0004E802`.
Taking that path skipped the x87 pop, EDI/ESI restores and guest return,
leaving ESP 12 bytes low. The caller could then restore registers from
temporary stack data and fault on the next `mov al,[edi+0x51]` at `0x0004F3A0`.
Immediate discovery now protects each accepted callback's reachable
instruction bytes before considering the next candidate. Regenerate both
disassembly and lifted sources; rebuilding an old snapshot retains the bug.
The synthetic regression covers a false entry inside an operand byte and
checks that both epilogues remain in the enclosing function.

Natural input now reaches **KO, replay, the next round, WINNER, and the next
fight** inside a 53-second watchdog / 55-second hard cap. With
`RECOMP_UNPACED=1`, Time Attack wins both first-stage rounds against Hayate,
shows WINNER at present **6048**, and reaches stage-two combat at **6502**.
It later shows YOU LOSE in stage two at **10404**, then stops at the watchdog
with **1,413,507 kernel calls**. No missing body, import or memory fault occurs.

Story wins against Tina and Bass (WINNER at presents **4493** and **7021**),
plays the next opponent's dialogue, and reaches the third fight against
Ayane. Present **9729** shows active combat and depleted health bars;
the watchdog stops at **1,260,247 kernel calls**. Opponents and stages vary
naturally. These are scripted controller inputs; no guest state is forced.
Ending and continue screens remain unobserved.

Lifter `68d7dbc` corrected the title's START check (`0x00050004`), which
had acted on a START the route never sent. The route now adds START at poll
2175 and moves the Time Attack right press and every later input 105 polls
later (right at 2250). With lifter `fdb7dde`, two runs reach Time Attack
stage two with walls holding (deepest penetration 0.051 and 0.000 units).

Stage walls and danger-zone falls now work. Before lifter `66b9b74`, both
fighters crossed stage boundaries: on the castle stage they ended up 13.7
units past a wall, standing on the hillside behind it. No fall from a stage
tier was observed. Both failures had one cause: the lifter dropped branches
whose flags came from two `cmp` instructions of different widths.

- The per-frame boundary pass at `0x0008D180` first asks `0x000A1380`
  whether a fighter is exempt. That calls `0x000A9AC0`, which joins
  `cmp al, 1` with `cmp ecx, eax` before a `jne`. The lifted branch was
  never taken, so every fighter counted as exempt and the wall clamp and
  edge reactions were skipped.
- The danger-zone code at `0x000A40E0` joins `cmp al, 'L'` with
  `cmp eax, 0x18` before a `je`. It always fell through to the path that
  clears the fighter's fall state at offset `0x62`.

The proof compares lifted and x86 code on identical inputs. The same RAM
snapshot ran through the lifted functions in a native DLL and through the
original bytes in Unicorn. The old lifted `0x000A1380` returns 1 and Unicorn
returns 0. The old lifted `0x0008D180` writes nothing, while Unicorn writes
13 words of fighter state, edge type, contact angle and boundary vectors. The re-lifted
code matches Unicorn exactly on four snapshots. The geometry query
`0x000D6170` matched on 1,766 points before the fix.

Each run's sampled positions were checked against that stage's edge list.
Before the fix, castle and forest fights spent 24-252 samples per
10-second window inside walls, up to **13.7** units deep. After the fix,
forest, dojo, cliff and castle fights touch walls but never go deeper than
**0.07** units. In the after run, a nine-hit combo knocks Brad Wong over a
cliff edge (type `0x0E`). The camera follows the fall, the attacker jumps
after him, and the fight resumes on the lower level. Frame dumps show the
whole sequence. The route uses the same START/A schedule as above with dpad
right or left held on polls 2630-3500. Opponents and stages still vary from
run to run, so the same stage cannot be forced.

Stage Select also permits repeatable transfer checks on the unchanged
`fdb7dde` lift. Scripted Sparring inputs break Azuchi's side panel, follow
both fighters onto the roof, and continue combat there and after a further
drop to the courtyard. DOATEC Hong Kong's window shatters, the camera follows
the fall past the signs, and fighting resumes on the street. Both Sparring
routes complete twice. Watch Mode also reaches the fall from Lorelei's
terrace and resumes combat in the lower area. These are natural controller
inputs with no guest-state overrides; no additional lifter or runtime fix
was needed. Earlier Azuchi attempts mostly hit the solid interior wall;
a sidestep before advancing and attacking reaches the breakable side panel.
Other transfer locations and full visual parity remain unverified.

Lifter `d29ea9e` masks shift counts like x86 (419 sites), lifts the 10
one-operand 8-bit `imul` sites at their real width, implements
`fnsave`/`frstor` against the modeled x87 state, and stops treating
padding and non-callable jump-table entries as function starts. The
disassembly falls from 7,099 to 6,899 functions (6,869 of 6,946 lifted, none
failed) and the `_flags` fallback consumers from 52 to 15 (12 table-data,
3 caller-flags). Two Time Attack runs on this lift reach stage-two combat
and stop at the watchdog after **1,053,538 / 1,032,801 kernel calls**, past
the one-off `0x3e79be00` read seen at 1,023,918 calls on `fdb7dde`; that
read did not recur in six reproduction attempts on `fdb7dde` either. The
cage stage's flare no longer covers the fighters in three cage fights; its
cause was not identified.

The runtime paces presents to 60 Hz: paced menu and fight runs measure
60.00 FPS in `RECOMP_PERF_COUNTER` and 59.4 FPS in PresentMon. With
`RECOMP_UNPACED=1` the game runs at 120-180 FPS; the window title says so.

Lifter `ec4626c` removes the last 15 `_flags` fallback consumers: blocks
that only linear decoding reaches (inline tables, padding) are dropped, and
arithmetic flags now cross calls and returns through `g_eflags`, which the
scheduler saves per guest thread. Every function now computes carry, and the
generated C grows by about 45%; paced fights still measure 60.00 FPS. The
lift has 6,864 of 6,941 functions, none failed. Two Time Attack runs reach
the watchdog without a stop (**1,213,986 / 1,233,074 kernel calls**); one is
in stage-two combat and one in stage one's second round.

A true first boot with no `.recomp-storage` used to stop after 149 kernel
calls: partition 5's raw device was mapped to its cache directory, so the
format failed. Raw utility devices now have their own backing file. A paced
first boot reaches the legal notice, the movie and the title (about 48 s)
while the game fills its cache; the fill sleeps 16 ms per 64 KiB, so it
spans more than one 55-second run and resumes on the next boot.

The intro movie's decoder spent its time in scalar MMX helpers and
out-of-line guest memory accesses. With packed SSE2 helpers and inline byte,
word and 64-bit RAM access, present gaps over 25 ms in the same movie
interval fall from 288 to 39 and the worst gap from 461 ms to 34 ms. The
movie itself runs at 30 pictures per second; the title holds 60 FPS.

Losing a fight shows YOU LOSE, the CONTINUE countdown, GAME OVER, the title
and then attract mode. Attract mode plays demo fights, including stage
transfers; START returns to the title and idling resumes the demos. The
Story ending is not reachable in one 55-second run: an unpaced Story run
beats two opponents and reaches the third.

The successful route uses START at polls **1983, 2090, 2130**, A every
**20 polls from 2160 through 2680**, digital right (`0x8`) on every poll
**2630-2790**, and analog index **3**, value **255**, every **8 polls from
2690 through 8994**. For Time Attack, add digital right at **2145** before
confirming the mode. The runner's analog schedule capacity is now 1024.
The legal-notice loop at `0x00022330` lasts 30 times the game's frame-rate
value and does not test START/A. The logo routine at `0x000833E0` performs
168 fade frames before accepting movie skips. Neither fixed loop is skippable.
START 1983 is the earliest repeatedly successful skip previously observed.

The runner now fast-forwards the first boot's legal notice: while the boot
routine `0x00021F70` holds `0x004B8438` at 1, waits are skipped as in
unpaced mode, once only, so later saves that reuse the flag stay paced. A
paced boot shows TECMO at about 11 s (was about 32 s), then the movie at 60
FPS. Paced vblank waits compare deadlines with the guest clock, which runs
ahead of the host clock after any skip; comparing with the host clock had
spun the first paced refresh for the whole skipped span (about 20 s).

A paced attempt with analog index 3 every four polls reaches a nine-hit combo
and reduces CPU health **240 -> 104**, but reaches no KO before the watchdog
(**342,518 calls**). Unpaced mode removes host waits by advancing the shared
clock to an idle scheduler deadline; runnable threads still execute first.
Vblank, QPC, CPU timestamp reads, kernel waits and sound cursors use that clock.
An explicit vblank followed by Present still consumes only one refresh.
The option defaults off. A final paced check reaches damaging Time Attack
combat at 60 fps (**341,437 calls**) with zero dropped audio buffers.
Unpaced audio queues drop buffers at accelerated playback rates; these runs do not establish audio synchronization parity.

Round progression exposed two allocation paths and one missing import:

- DirectSound owned storage used the monotonic backing arena and was never
  released. A repeated 64 KiB buffer allocation stopped after the first KO
  and replay (**512,516 / 503,786 calls**). Owned storage now uses the existing
  reusable kernel pool and is released separately from caller-supplied sample
  data. A focused test recreates aligned, zeroed buffers with the backing
  allocator exhausted.
- The fixed 16 MiB heap floor excluded free RAM between the loaded XBE and
  kernel data. The 1 MiB transition texture allocation failed at startup;
  its later use after Story dialogue produced `d3d-clear:render-target`
  (**1,035,265 / 1,046,527 calls**). Contiguous allocation now admits that
  page-aligned gap, bounded by the validated image extent and kernel data.
  The 64 MiB RAM size is unchanged. The heap test covers exhaustion, alignment,
  physical bounds and protection of both neighboring live regions.
- The additional memory exposed kernel ordinal **3**, `AvSetDisplayMode`
  (**197,121 calls**). Its six-argument bridge completes hardware scanout
  setup through the existing native D3D11 path. The calling convention is
  documented in [nxdk's kernel interface](https://github.com/XboxDev/nxdk/blob/master/lib/xboxkrnl/xboxkrnl.h).
  A focused check verifies completion, argument preservation and stack cleanup.

The repeated audio stops first differ at normalized kernel record **283**;
the repeated Story render-target stops first differ at **279**. Both differences
are fight-resource reads. Allocation diagnosis followed these comparisons;
no rendering acceptance check was relaxed. The corrected Story run reaches
Ayane combat without an allocation failure or invalid render target.

Health is unsigned 16-bit at `0x00484C68 + player*0x68`, with pending damage
in the next word and maximum health at `0x0048A4B0 + player*2`.
Game `0x000B91E0` subtracts damage and clamps lethal hits to zero. Story mode
2 intentionally selects infinite timer setting 5; Time Attack mode 3 selects
setting 3 and counts down from 60. The previous full-health claim was a visual
misreading, not a runtime defect.

Combat uses the verified 3925 programmable draw path and UI line lists;
see [doa3-d3d-draw.md](doa3-d3d-draw.md). Two invalid-transform draws are still
rejected during loading in the final Story run. Earlier cage-stage captures
sometimes showed large occluding geometry; other cage fights render normally.
Full material/lighting parity, unsupported declarations/pixel shaders,
cold-cache completion and attract mode remain unverified.

Python discovery passes (three tests, one skipped); Debug builds and all
**19 CTests pass**, including unpaced clock advancement. The cooperative
scheduler check also passes with `RECOMP_UNPACED=1`. The lifter is pinned
to `66b9b74`; its suite passes (605 tests, one skipped). The regenerated
snapshot has 7,069 / 7,146 functions with none failed, and 96 `_flags` fallback
branches remain (down from 106).

## Previous bring-up observations

### Screen regression bisect

The four Release builds used matching generated snapshots, identical warm
storage preparation, neutral input and the same RAM watch. At present 600:

| Runtime | Lifter / snapshot recipe | Capture |
|---|---|---|
| `c6dd4b4` | original merged recipe | legal notice |
| `2db3ae9` + `edff09d` (`257eb2b`) | cache-worker lift | identical legal pixels |
| `448af78` | same cache-worker lift | black, no font draws |
| `fab3550` | vblank/CRI manual bindings regenerated | black, no font draws |

The earliest behavioral divergence is the contiguous allocation reached from
`0x0006B850` through `0x001B4D80` and `0x00163E5B` (kernel ordinal 166).
The 1.25 MiB font resource fits before scheduling; adding five thread stacks
raises the heap cursor from `0x01486000` to `0x014DB000`, beyond the proposed
physical base `0x014D0000`. Allocation returns zero, the caller takes its error
return at `0x0006B88F`, and two font reads and their loader fibers never occur.
This precedes drawing: changing saved render state or cache flags is unnecessary.

The backing allocator still excluded a 1 MiB inline startup stack after
scheduled threads acquired their own stacks. `guest_heap.c` now protects only
the bootstrap TLS page at the top of RAM. Applying that change alone to
`448af78` restores the baseline's exact legal pixels at present 600. No RAM
expansion, overlapping allocation or guest-state override is involved.

Pacing is a separate issue. The 3925 Present body calls the explicit vblank
wait conditionally during a display-mode change; the game loop also has a
conditional wait, but the trace did not show that call every frame. CRI worker
waits were left runnable by the scheduler and could be selected again before
the refresh. Waiters now block on one shared deadline, idle scheduling uses a
high-resolution timer, and Present consumes an already completed explicit wait
instead of waiting twice. The refresh phase is retained across calls. The RAM
watch disables the fast memory path and slows legal drawing even in baseline
builds, so the final throughput runs omit it in both confirmations.

Before cooperative scheduling, lifter `edff09d` made the cache worker
`0x0009D440` lift by accepting `int3` after a no-return call.
`PsCreateSystemThreadEx` ran a new thread inline on the creator's stack, and
blocking waits returned at once (single-threaded mode).
The worker opens its `z:` cache copy and then never returns, so the creating
game task never resumes: a 25 s Debug run spun to 69,460,021 kernel calls with
the watchdog inside the worker's wait loop. This and the CRI worker wait below
both need guest threads scheduled cooperatively.

The corrected utility-drive mapping exposes a missing game-owned cache worker:
`indirect:0x0009D440`, after **2,417 kernel calls**. Neutral input and START/A
confirmation runs stopped there with identical sequences of 168 logged kernel
records (48 opens/creates, 17 reads). Both frame dumps at present 1 are black
720x480 images with identical hashes. No movie start was observed. The generated
snapshot and lifter pin remain unchanged.

The earlier "T: loop" diagnosis was incorrect. Game function `0x00080A00`
performs a bounded search over 19 slots, skipping two, through FindFirstFile
`0x001633B7`. Failed first queries close the handle at `0x00163492`, map the
status through `0x001650BA`, and return INVALID_HANDLE_VALUE; the game advances
to the next slot. The earlier recursive directory walks come from game
`0x0009C840` through CRI `0x00169660` / `0x0016D330` / `0x0016CE40`.
This is FindFirstFile enumeration, not an observed XFindFirstSaveGame retry.

A fresh baseline reached the red TECMO logo on black (present 1950), then the
watchdog sampled CRI `0x0016A4C0`, not directory enumeration. Its main-server
request flag at `0x00B24D3C` changed from zero to one and never cleared. The loop
repeatedly calls SetThreadPriority and ResumeThread while waiting for its
worker; the runtime does not schedule that worker. A run changing only the
empty-directory result reached the same wait. This later CRI scheduling gap
remains unresolved behind the newly exposed cache-worker stop.

The file model now distinguishes a first empty query (`STATUS_NO_SUCH_FILE`)
from exhaustion (`STATUS_NO_MORE_FILES`), with DOS errors 2 and 18 respectively.
Restarting an already queried handle does not make it a first query. The
semantics follow [ReactOS's initial-query handling](https://github.com/reactos/reactos/blob/master/drivers/filesystems/fastfat/dirctrl.c)
and [status mappings](https://github.com/reactos/reactos/blob/master/sdk/lib/rtl/error.c);
[nxdk's FindFirstFile](https://github.com/XboxDev/nxdk/blob/master/lib/winapi/findfile.c)
and DOA3's disassembly both close the handle on failure. The runtime still
passes other NTSTATUS values through; general DOS error conversion is unfinished.

DOA3's XMountUtilityDrive at `0x00163A3F` creates Z:'s symbolic link at
`0x00163B01`. The observed target is hard-disk partition 5. The runtime now
honors storage links to partitions 1 and 3-5, keeps utility writes separate
from the disc, retains the modeled cache dismount, and omits its storage
directory from disc-root enumeration. Separate partition backing also matches
[Cxbx-Reloaded's disk model](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded-legacy/blob/master/src/core/kernel/support/EmuDisk.cpp).
The first baseline/updated difference is the partition-5 root open becoming a
host directory instead of a pseudo handle. The first cache-file lookup then
reports missing rather than finding the disc file, activating the cache worker.
Between the two final runs there is no kernel-sequence difference; only the
configured input schedule differs, with no demonstrated pulse delivery before
the stop. Every run used isolated storage, an expected stop, a milestone log,
a visible minimized runner and a 55-second kill limit.

After the repeated stop, runtime changes stopped. A private check with lifter
`1077fe3` confirmed the missing-body hypothesis: game code registers
`0x0009D440` at `0x0009D6E5`, but discovery and the generated declarations omit
it. The worker has no return; it ends with a call to ExitThread `0x00164B6F`,
then an INT3 at `0x0009D6A9`. That wrapper calls PsTerminateSystemThread.
Callback discovery treats the INT3 as a reachable, unclosed exit instead of
recognizing the preceding non-returning call. This requires a lifter fix and
regeneration, not a runtime replacement for the game worker.

Verification: focused kernel-file checks cover first/subsequent/restarted
queries, DOS error mapping, utility writes and traversal rejection, and hidden
runtime storage. Python discovery passed (three tests, one skipped), the full
Debug runtime build passed, and CTest passed 14/14.

The runner must not start hidden: the presenter's first present checks that its
window is visible.

XPP input is replaced at the API level. These XAPILIB 3911 entry points
each matched one XbSymbolDatabase signature, and their `ret` sizes match the
DOAXBV adapters: `XInitDevices` `0x001E5D4C` (and its `jmp` thunk
`0x001E6953`), `XGetDevices` `0x001E6958`, `XGetDeviceChanges` `0x001E697A`,
`XInputOpen` `0x001E6E3A`, `XInputClose` `0x001E6EAF` (the second match,
`0x001E625A`, is not adjacent to the other entry points), `XInputGetCapabilities`
`0x001E6EBB`, `XInputGetState` `0x001E70AD`, `XInputSetState` `0x001E711E`. The
gamepad device type is `0x001E59E0`.

Stops in discovery order. A corrected model can expose an earlier execution
path; unresolved entries are marked below:

| Stop | kernel_calls | Fix |
|---|---|---|
| `indirect:0x0018e098` in memcpy | 8 | lifter `3764134`: arms after an inline jump table become in-function labels |
| `indirect:0x001985d0` in `_initterm` | 162 | lifter `939a6f4`: a gap alias's borrowed range no longer hides static callbacks; a callback may fall through into an alias start |
| `import:unknown` (ordinal 127) | 168 | runtime: `KeQueryPerformanceCounter`/`Frequency` bridges |
| `indirect:0x001e63d2` in XPP's device tables | 171 | runtime: XPP input entry points bound to `input_adapter.c` |
| `memory:0xfd001804`, miniport init in `CDevice::Init` | 177 | runtime: `d3d_miniport_adapter.c` (miniport init, channel and object creation, mode, tiles, pusher flush, KickOff) |
| `d3d-clear:model:2`, Init's own clear | 178 | runtime: frame model starts from the static device |
| `memory:0xfec0012c`, AC97 setup inside `DirectSoundCreate` | 254 | runtime: `dsound_api_adapter.c` |
| `indirect:0x0016c0b0`, a callback passed with `push imm` | 349 | lifter `04cbb08`: gap callbacks taken as immediates are recovered |
| watchdog, `ADXF_GetPtStat` spin | 816 | runtime: ADXM server pass per poll; lifter `4d276d3` so callers reach the wrapper |
| watchdog, ADXF read never completing | 137,958,756 (alertable waits returning at once) | runtime: I/O completion APCs |
| `indirect:0x0016abf0`, entry in a `.data` function table | 874 | seed list |
| watchdog, `ADXF_GetStat` spin | 1,803 | runtime: same server pass on `ADXF_GetStat` |
| `d3d-clear:presenter:5` | 1,918 | harness: the runner was started hidden |
| `indirect:0x00084340`, first fiber task (no `ret`) | 1,918 | runtime: fiber service at DOA3's XAPI; lifter `d2792f3`..`1077fe3`: callback recovery |
| save-space warning screen, watchdog | 130,134 | runtime: report 3 GiB free (8 GiB, low dword zero, showed the warning) |
| watchdog after TECMO logo, previously reported as a T: directory loop | 189,642,076 | Diagnosis corrected: CRI main-server suspension wait; still unresolved behind the cache-worker stop. Empty-directory status alone does not resolve it. |
| `indirect:0x0009D440`, cache-worker thread | 2,417 in both confirmation runs | lifter `edff09d`: callbacks may end in a no-return call padded with `int3` |
| `completed` immediately after thread creation | 2 | runtime: wait for guest threads when the XBE bootstrap returns |
| `memory:0xffffffe4` during first fiber switch | 1,572 | runtime: `TlsDataSize` includes the TLS pointer slot; correct FS stack top and test negative TLS indexing |
| black-frame Present loop, `watchdog:45000` | 90,197 / 90,065 | Unresolved: cache thread stays ready while zero-timeout polls and host-only vblank pacing never schedule it. The isolated vblank/yield check reproduces this limitation. |
| `indirect:0x0016A650`, CRI main-worker entry | 1,687 | Scheduling exposes an absent middleware callback; bind its lifecycle to the existing ADXM server model. |
| black presentation loop; cache destination opens but does not advance | 92,741 | Bind `BlockUntilVerticalBlank` to shared native pacing instead of the unsignaled hardware event. |
| concurrent cache copy and black presentation, `watchdog:45000` | 170,836 / 169,788 | Vblank scheduling and priority preemption work; matching kernel-log prefixes and black frames in neutral/START-A runs. Runtime changes frozen. |
| warm-cache hypothesis, `watchdog:45000` | 122,140 | Prepopulating the cache skips its worker but does not produce a logo, CRI request transition or SFD reads within 45 seconds. |
| legal screen disappears first at `448af78` | 31,796,786 at the bisect watchdog | Reclaim obsolete inline-stack reservation; the failed contiguous font allocation now succeeds. Block vblank waiters and share refresh pacing. |
| `missing-body:0x0017ebc0`, after legal notice and TECMO | 168,589 / 168,679 | Lifter `e6695a5` / `2675417`: fixed-point tail discovery includes late aliases and excludes unreachable branches. |
| `indirect:0x00176dc0`, address-taken direct thunk | 1,665 | Lifter `21bfc5f`: recognize direct thunks with validated returning destinations. |
| `missing-body:0x001771d2`, shared conditional return | 168,669 | Lifter `9b21c97`: conditional tails into existing bodies get aliases without splitting those bodies. |
| `indirect:0x001e3720`, after SFD reads begin | 223,984 / 214,956 | seed list: first return at instruction 315 exceeds the 64-instruction data-pointer probe |
| black movie frames, `watchdog:45000` | 886,621 / 770,937 with native conversion | DOA3 uses DrawVerticesUP with a linear BGRA texture, not an overlay. Select the quad's default diffuse alpha; its texture alpha is zero and the active GREATER/ref=1 alpha test rejected it. |
| visible movie, `watchdog:45000`; timing test `watchdog:53000` | 874,523 / 803,846; 1,172,127 | Repeated stop during continuing playback. First 241 normalized kernel records match; three additional SFD reads in the faster run. Extending only the watchdog advances the picture but does not reach title or movie end. |
| `missing-body:0x0001B000`, after START skips the intro | 467,479 / 465,582 | Lifter `f870248`: explicit seeds override unclaimed out-of-phase sweep instructions; `b8665cb`: reachable tail targets in gaps are decoded, so this seed is no longer required. |
| GPU fence wait through `0x001B1110`, `watchdog:45000` | 464,595 / 465,700 | Runtime `5ead25d`: InsertFence kicks before incrementing the counter; complete the recorded issued fence while keeping the unissued next value pending. All 249 normalized kernel records and present-2301 black frames match. |
| `indirect:0x000E00D0`, after the fence fix | 472,237 / 472,821 | Lifter `799afc8`..`c397950`: bounded callback CFG proof replaces the fixed instruction budget; the target is discovered without a seed. |
| `indirect:0x001A3BC0`, callback-discovery regression | 169 in both runs | Lifter `ea525ed`: table callbacks establish ownership before weak immediates; the suffix `0x001A4000` no longer cuts this initializer. All 19 normalized records match. |
| `indirect:0x00176FA0`, forwarding thunk | 168,669 / 168,679 | Lifter `81959a4`: prove direct tail dependencies in their own gaps without an instruction cap. All 212 normalized records match. |
| `indirect:0x001E3060`, shared Sofdec callback | 191,785 / 191,897 | Lifter `c397950`: retain callable shared bodies with their own register saves, reject owner epilogue suffixes, and distinguish external backward tails from local loops. All 213 normalized records match. |
| title and Mode Select, `watchdog:45000` | 504,074 / 504,595 | Callback gaps resolved. Title at present 2484 and Mode Select at 2606; all 278 normalized records match. Additional A at poll 2580 starts selection and changes resource reads at record 278. |
| Story Mode Character Select, `watchdog:53000` | 594,176 | Same input test with eight more seconds: character model, profile and portrait grid render at presents 2788 and 3094. No missing body, import or memory fault; combat not observed. |
| Story Mode fight, `watchdog:53000` | 509,687 / 512,654 | Earlier START and repeated A reach two fighters and health bars on naturally selected stages; no runtime change needed to enter combat. First resource-read difference at normalized record 290. |
| Title water draws declined as `doa3-layout` | 510,870 in the first shader run | Combat: map 3925 declaration/program/constant/viewport fields to the existing HLSL path. Native draw validation must allow program-owned transforms; queue acceptance alone does not prove rendering. |
| Character-select primitive 2 declined as `doa3-layout` | 423,305 | Combat: admit line lists in all three draw APIs and use native D3D11 line topology; synthetic pixel and odd-count checks. |
| Scripted combat, `watchdog:53000` | 423,305 / 420,184 | Earlier input route reaches both fighters by present 2748; movement, hits, combos and shadows render. Native 25-instruction water pipeline observed; round completion remains unverified. |
| Earlier movie skip and natural fights, `watchdog:53000` | 354,126 / 339,360 / 350,716 | START 1983, 2090, 2130 reaches combat. Cage-stage Bayman fight has large occluding geometry; cage-stage Tina and mirrored-room Zack/Christie fights render unobscured. Health was initially reported full; the later damage investigation below corrects that interpretation. Round behavior was unverified. |
| Reported full health / infinite Story timer | 345,480 / 342,738 / 348,488 | Damage investigation: no recomp fix required. Story health falls 300 to 177 and its bar depletes; infinity is selected by retail mode initialization. Time Attack counts down and both fighters take damage. Round completion remains unverified within the cap. |
| Paced Time Attack attacks, `watchdog:53000` | 342,518 | CPU health falls 240 to 104; no KO within the cap. Extend the analog schedule and add opt-in unpaced execution with shared guest time. |
| KO and replay, `dsound:create-buffer:alloc=65536` | 512,516 / 503,786 | Recycle owned DirectSound storage through the existing kernel pool; preserve caller-owned sample data. |
| Time Attack WINNER, stage two and YOU LOSE, `watchdog:53000` | 1,413,507 | Natural round and result flow with extended attacks; no missing body/import/fault. |
| Story wins and dialogue, `d3d-clear:render-target` | 1,035,265 / 1,046,527 | An earlier transition-texture allocation failed. Admit unused RAM between the validated XBE extent and kernel data to contiguous allocation. |
| `import:AvSetDisplayMode` after admitting image-gap RAM | 197,121 | Kernel ordinal 3: complete the six-argument scanout setup through native presentation. |
| Two Story wins, dialogue and third-fight Ayane combat, `watchdog:53000` | 1,260,247 | The transition now completes. Present 9729 shows active combat; ending/continue remain unverified. |
| Fighters cross stage walls; no danger-zone fall, `watchdog:53000` | 1,289,812 / 1,235,831 / 1,236,207 | Lifter `66b9b74`: cmp snapshots of different widths join where the condition agrees; boundary exemption and fall-state branches are restored |
| Walls hold; cliff fall reaches the lower tier, `watchdog:53000` | 1,196,524 / 1,477,422 | Wall penetration at most 0.07 units across four stages. |
| Title falls into the attract demo after lifter `68d7dbc`, `watchdog:53000` | 1,633,974 / 1,660,797 | Route change; no lifter fix. `0x0004FFC0` joins `or ecx` and `or edx` of `[0x004B8228]` before a `je`; older lifters made that branch never taken, so the title acted on a START that was not pressed. Add START at poll 2175 and move right and every later input 105 polls later (right at 2250). Time Attack reaches stage-two combat with walls holding (1,305,810 / 1,331,841). Lifter `fdb7dde` adds a regression test. |

The `_initterm` stop came from 25 `tail_jump_alias` entries in the 100 KB of
dynamic initializers after `0x001985A0`. None has an ordinary start, and each
claims the range up to `0x001B0DA0` (each lifts to about 38,000 lines).
`discover_static_indirect_targets()` skipped any table target inside such a
range. Raising the data-pointer probe cap (64 instructions) would also have
caught them, but adds 7 unaligned targets that look like data words.

## D3D8 3925 and DSOUND 3936 entry points

Find these in DOA3, in this order, before the presenter can show a frame. The
"first signature" column is the earliest XDK build for which
[XbSymbolDatabase](https://github.com/Cxbx-Reloaded/XbSymbolDatabase) has a
signature; 3911 means it exists in DOA3's library generation. DOAXBV addresses
are XDK 4928. DOA3 addresses come from
[doa3-d3d-symbols.md](doa3-d3d-symbols.md) and
[doa3-d3d-frame.md](doa3-d3d-frame.md), and for rows 6-11 from
[doa3-d3d-draw.md](doa3-d3d-draw.md); "bound" means the runner uses the
adapter for DOA3 today.

| # | Entry point | First signature | DOA3 | DOAXBV adapter (address) |
|---|---|---|---|---|
| 1 | `Direct3D_CreateDevice` | 3911 | 0x001B4EC0, stays generated; the GPU layer under `CDevice::Init` is replaced (`d3d_miniport_adapter.c`) | `d3d_creation_adapter.c` `recomp_d3d_create_device_adapter` (0x001E9100) |
| 2 | `CDevice::KickOff` (push-buffer kick-off) | 3911 | 0x001B88C0, bound to `d3d_miniport_adapter.c` | `d3d_creation_adapter.c` `recomp_d3d_kick_off_adapter` (0x001E9EB0) |
| 3 | push-buffer space request | 4034 as `D3D_MakeRequestedSpace` | `CDevice::MakeSpace` 0x001B8B00 (device in `ecx`, no stack args), stays generated; KickOff's model leaves DMA get equal to put | `d3d_creation_adapter.c` `recomp_d3d_make_requested_space_adapter` (0x001EA190) |
| 4 | `D3DDevice_Clear` | 3911 | 0x001B3390, bound | `d3d_frame_adapter.c` `recomp_d3d_clear_adapter` (0x001E72D0) |
| 5 | `D3DDevice_Present` | 3911; `D3DDevice_Swap` starts at 4034 | 0x001B5850, bound to `recomp_d3d_present_adapter` | `d3d_frame_adapter.c` `recomp_d3d_swap_adapter` (Swap, 0x001E8F30) |
| 6 | `D3DDevice_DrawIndexedVertices` | 3911 | 0x001B3940, bound | `d3d_draw_adapter.c` `recomp_d3d_draw_indexed_vertices_adapter` (0x001E78B0) |
| 7 | `D3DDevice_DrawVerticesUP` | 3911 | 0x001B3760, bound | `d3d_draw_adapter.c` `recomp_d3d_draw_vertices_up_adapter` (0x001E7750) |
| 8 | `D3DDevice_SetVertexShader` | 3911 | 0x001B45F0, bound | `d3d_vertex_shader_adapter.c` `recomp_d3d_set_vertex_shader_adapter` (0x001E7170) |
| 9 | `D3DDevice_SetTexture`, `D3DTexture_LockRect` | 3911 | SetTexture 0x001B1CC0, bound; LockRect 0x001B4B30, stays generated | `d3d_texture_adapter.c` set_texture (0x001E43F0), lock_rect (0x001E8090) |
| 10 | `D3DDevice_SetRenderState_*` (Simple, EdgeAntiAlias, CullMode, NormalizeNormals, TextureFactor, FillMode, ZEnable, StencilEnable, StencilFail, MultiSampleAntiAlias) | 3911 | 0x001B2390-0x001B32F0, bound | `d3d_render_state_adapter.c` (0x001E4D80-0x001E6510) |
| 11 | `D3DDevice_SetTile` | 3911 | 0x001B1F30, bound | `d3d_tile_adapter.c` `recomp_d3d_set_tile_adapter` (0x001E4930) |
| 12 | `D3DDevice_SetGammaRamp` | 3911 | 0x001B0E00, bound | `d3d_frame_adapter.c` `set_gamma_ramp` (0x001E3640) |
| 13 | `D3DDevice_Reset`, `D3DDevice_PersistDisplay` | 3911 | 0x001B1290, 0x001B2180, stay generated | `d3d_creation_adapter.c` reset (0x001E3B00), persist_display (0x001E4AE0) |
| 14 | `DirectSoundCreate`, `DirectSoundDoWork` | 3911 | 0x001C7FD9, 0x001C760D, bound to `dsound_api_adapter.c` | `dsound_service_adapter.c` create (0x001FA27C), do_work (0x001F90E0) |
| 15 | `CDirectSound` DownloadEffectsImage, SetMixBinHeadroom, CommitDeferredSettings, SetPosition, SetVelocity | 3911 | the `IDirectSound_*` wrappers game code calls (0x001C7392-0x001C7457, 0x001C7EA9) are bound | `dsound_service_adapter.c` (0x001F8F21, 0x001F8F48, 0x001F974F, 0x001F9DD4, 0x001F9E09) |
| 16 | `IDirectSoundBuffer` Play, Stop, StopEx, GetStatus, GetCurrentPosition, SetCurrentPosition, SetFrequency, Release, SetBufferData | 3911 | the `IDirectSoundBuffer_*` wrappers (0x001C6B7C, 0x001C7477-0x001C75DD, 0x001C7AFB-0x001C7B6F) are bound, plus Lock and SetLoopRegion | `dsound_service_adapter.c` buffer_* (0x001F8FD8-0x001F9E5E) |

Rows 1-7 are the minimum for a frame: a device, a working push buffer, a clear,
a present and the two draw calls DOAXBV uses. DOA3 also calls `DrawVertices`
(0x001B38A0, 9 game call sites), which has no DOAXBV adapter and is now bound
to the same draw path; it does not link `DrawIndexedVerticesUP`. Rows 1-3
needed DOA3-specific work, because DOA3's
CreateDevice builds its frame buffers inside the static device (`0x001C0800`)
and its KickOff and space routine differ from 4928 (see
[doa3-d3d-symbols.md](doa3-d3d-symbols.md)). Rows 8-13 make the frame correct.
Rows 14-16 are audio and do not block frames.
