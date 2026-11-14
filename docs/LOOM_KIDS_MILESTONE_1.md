# LOOM Kids milestone 1

Source branch: `setup/loom-kids-codex`, commit
`475e3f5a5f03b76cb325ff09ef927afae13996e0`.
Feature branch: `feat/loom-kids-ui`.
Submodules initialized recursively, unchanged: `base` at `c8e05997ce970875038156d2bbd69e712eac1543`,
`base/bsp` at `18eadadb8c364abd026accd2b1ea1c14ca347f06`.

## Try it

Build with `./build.sh --host`. The default remains Studio. In the emulator:

1. Tap GLO, turn SELECT six clicks to Debug. The footer says **HOME Kids Stage**.
2. Tap HOME to enter Stage. Recording and active modal editing must finish first.
3. On first entry, an adult uses K1–K4 to choose four PIN digits, presses SEL, enters the same digits again,
   and presses SEL to start Kids. Set all four digits; there is no default PIN. HOME cancels setup.
4. SEQ opens Steps; HOME returns to Stage. SEL opens the adult PIN prompt, not Studio directly.
   Enter the PIN with K1–K4 and SEL to unlock; HOME cancels. Incorrect entries impose a three-second retry delay.
5. Kids edits an **unsaved working copy**. Unlocking preserves that copy without saving it. In Studio,
   SAVE chooses a slot; replacing existing work requires the normal explicit confirmation. HOME cancels.

Both Kids screens use the same four knobs, with persistent labels and current values:

| Control | Action |
| --- | --- |
| K1 / ALGORITHM | Select track 1–8, clamped at the ends |
| K2 / OCT− / OCT+ | Select bar 1–4, including steps beyond the current pattern length |
| K3 | Select drum hit 1–11, or the single MIDI note to insert |
| K4 | Tempo, 1 BPM per detent, through the existing tempo helper |
| PRESETS | Pattern, through the original queued-pattern helper |
| PLAY | Existing transport start/stop; recording stays in Studio |
| White keys on Steps | Toggle the corresponding step in the selected bar |
| Black keys on drum Steps | Select the drum hit |
| SEL | Adult PIN unlock for Studio: complex notes, recording, sounds, save, FX, envelopes, LFO, mixer, scenes and song |

The two rows retain all eight colored, numbered track marks; the first four have stronger underlines.
Selected track, mute marks, transport, current and queued patterns, bar, notes and playhead come directly
from the application model. A drum cell shows the selected hit, so other hits survive removing it.
A melodic insertion uses the one note shown by K3; existing chords remain editable in Studio.
Dim cells lie beyond the current pattern length; adding a step extends it through the existing helper.
No screen or encoder enters auxiliary tracks accidentally; Studio retains them all.
Other buttons and SELECT rotation have no Kids action. There are no hold gestures or hidden alternate modes.

## Implementation

- `app/kids.c`: isolated emulator input, adult session PIN setup/unlock and unsaved working-copy detachment;
  eight signature-cached drawing bands, at most 36 pixels high.
  Uses existing canvas, fonts and procedural rectangles; no new framebuffer, assets, heap allocation,
  sequencer state, or idle redraw loop.
- `app/ui.c`: guarded entry, input/render dispatch, initialization and redraw invalidation.
- `app/pages.c`: emulator-only entry label on Debug; hardware footer unchanged.
- `app/host.c`: assertions for Kids mode, selected bar, band-draw count, PIN gate, lock and saved-slot association.
- `app/web/emu/page.html`: visible emulator entry/control instructions.
- `tests/scenarios/kids.xy`: real button/encoder scenario, automatically discovered by the existing runner.
- `tests/scenarios/kids_safety.xy`, `tests/host/kids_safety_test.py`, `tests/run_tests.sh`: saved-object
  byte comparisons, adult PIN/overwrite flows, and lock retention over simulated reboot.
- `AGENTS.md` and this report: child-safety requirements and session boundaries.

All new behavior is guarded by `PLAT_HOST` / `PLAT_WEB`. No changes to DSP, audio interrupts, sequencer
timing, serialization, bootloader, flash layout, install package, or submodules. No firmware build,
physical-device communication, installation, flashing, or recovery command was performed.

## Verification

Baseline `./build.sh --host` and `tests/run_tests.sh` passed. Node is available. The runner skipped:

- Web/WASM build and emulator tests: Emscripten (`emcc`) unavailable.
- Update-package tests: no `app/build/loom.fwsc`; no firmware package built for this task.
- DSP fingerprint comparison: no pre-existing reference. DSP sources were unchanged; synth/Braids/drum/FX
  and sequencer regression tests ran.

The new scenario verifies Stage → Steps → Stage → Studio, all eight tracks, drum hit preservation,
melodic add/remove, step 64 add/remove, bar limits, pattern switching and queued-pattern playback,
transport, unlocking with a key held, reboot after adult unlock, and no additional Kids drawing during one
second idle (32 band draws after PIN setup, still 32 afterwards).

The safety scenario snapshots the entire simulated store. Byte-for-byte comparisons require it to remain
identical after Kids editing and ignored buttons/holds, adult unlock followed by an idle wait, and a
cancelled overwrite. Only an explicit adult-confirmed save may change it. A locked simulated reboot
must restore saved notes, remain locked, and leave the stored objects identical to the adult's save.
The gate tests reject an unset default PIN, a wrong PIN and a rapid retry after cancelling/reopening.

Repeat with:

```sh
./build.sh --host
mkdir -p app/build/scenarios/kids
app/build/host/loom_host tests/scenarios/kids.xy app/build/scenarios/kids
tests/run_tests.sh
```

Screenshots are generated in `app/build/scenarios/kids/`: `kids_stage.png`, `kids_bar4.png`,
`kids_playing.png`, `kids_home.png`, `kids_studio.png`, `kids_queued.png`, and `kids_large_setting.png`.
Stage and playback captures were visually inspected at 240×240; the host layout audit also checks them.
The safety scenario adds `app/build/scenarios/kids_safety/protected_stage.png` and `adult_wrong_pin.png`,
both visually inspected. Full-suite output is retained in `app/build/tests/milestone1-safety.log`.

The first post-change run exposed clipping when Studio had large type enabled. Kids now explicitly uses
its fixed small font; the expanded Kids scenario and the original large-type layout sweep both pass.
The initial milestone passed 91 checks (baseline: 90). The final child-safety extension passed all
93 checks in `tests/run_tests.sh`, including both scenarios, byte-for-byte store comparisons, and the
normal/large-type layout sweeps. `./build.sh --host` and `git diff --check` passed. The same three
dependency/artifact skips apply.

GNU `size` on the native host executable (bytes; includes the entire simulator):

| Section | Baseline | Milestone 1 | Difference |
| --- | ---: | ---: | ---: |
| text | 1,119,709 | 1,129,725 | +10,016 |
| data | 26,620 | 26,652 | +32 |
| bss | 3,431,992 | 3,432,088 | +96 |
| total | 4,578,321 | 4,588,465 | +10,144 |

The targeted safety scenario simulated 23,465 ms with 289 blits and reported render time of 0.3% of real
time on the host CPU. The idle assertion measures Kids band draws, not elapsed wall-clock performance.
There are no new heap allocations or framebuffer. PIN state is a few session bytes, not a copied project.

## Limits and next milestone

This is an opt-in emulator preview. Web source is integrated but its WASM build has not been validated
without Emscripten. Kids uses fixed small type and does not yet implement Studio's large-type or full
spoken-page navigation. It inserts single melodic notes; use Studio for chords and advanced editing.
Hardware SRAM/stack usage, firmware size and on-device render speed were not measured. Host measurements
are not estimates of device performance. Hiding screens does not remove their code or memory.

Next: instrument categories plus a small set of clearly named sound macros, reusing existing preset and
parameter APIs. Afterwards address envelopes/LFO/FX/mixer, then recording and scene interactions.

## Child-safety extension

Follow-up to `19d49fe` on the same feature branch, explicitly requested by the user:

- Kids must not overwrite saved work: `kids_detach()` uses the existing no-slot state (`proj_slot = NPROJ`)
  without changing the model or save format. Autosave also explicitly refuses all Kids/PIN screens.
- Detachment remains after adult unlock. Studio's existing SAVE flow asks before replacing any existing
  slot because the copy has no saved-slot association. Choosing cancel writes nothing.
- Adult setup requires two matching four-digit entries. SEL in Kids opens a credential gate; wrong PINs,
  holding buttons, or cancelling do not unlock. Cancel/reopen does not bypass the retry delay.
- Session PIN/lock state is separate from projects and survives a simulated reboot in the running emulator.
  A reboot reloads saved music, detaches it again, and returns to locked Stage.
- The original Studio remains available after adult unlock; no advanced musical feature is removed.

Boundary: this protects the active emulator UI session, not against a person who can close/reload the
application, use developer tools or execute host scripts. Ending the host/browser instance ends the PIN
session and discards unsaved edits; a fresh launch starts in Studio for adult setup. PINs are not written
to project files or browser storage. An adult should set up each new session before handing it over.
Web/WASM execution remains unverified without Emscripten; native-host gate and persistence checks run.
A durable parental-control boundary across browser restarts would require a separate design.

Further child-focused improvements, beyond these two protections: visible undo/redo and an adult-approved
restore-to-start action, then instrument categories and simple macros. The UI does not claim that a child
can never disrupt playback or change the temporary music; those experiments cannot overwrite saved work
without leaving the session boundary or an adult explicitly approving a save.
