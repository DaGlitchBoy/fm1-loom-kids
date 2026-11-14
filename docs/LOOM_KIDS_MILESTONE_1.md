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
3. SEQ opens Steps; HOME returns to Stage; SEL returns to Studio Steps.

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
| SEL | Studio for complex notes, recording, sounds, save, FX, envelopes, LFO, mixer, scenes and song |

The two rows retain all eight colored, numbered track marks; the first four have stronger underlines.
Selected track, mute marks, transport, current and queued patterns, bar, notes and playhead come directly
from the application model. A drum cell shows the selected hit, so other hits survive removing it.
A melodic insertion uses the one note shown by K3; existing chords remain editable in Studio.
Dim cells lie beyond the current pattern length; adding a step extends it through the existing helper.
No screen or encoder enters auxiliary tracks accidentally; Studio retains them all.
Other buttons and SELECT rotation have no Kids action. There are no hold gestures or hidden alternate modes.

## Implementation

- `app/kids.c`: isolated emulator input and eight signature-cached drawing bands, at most 36 pixels high.
  Uses existing canvas, fonts and procedural rectangles; no new framebuffer, assets, heap allocation,
  sequencer state, or idle redraw loop.
- `app/ui.c`: guarded entry, input/render dispatch, initialization and redraw invalidation.
- `app/pages.c`: emulator-only entry label on Debug; hardware footer unchanged.
- `app/host.c`: assertions for Kids mode, selected bar and band-draw count.
- `app/web/emu/page.html`: visible emulator entry/control instructions.
- `tests/scenarios/kids.xy`: real button/encoder scenario, automatically discovered by the existing runner.
- This report.

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
transport, returning with a key held, reboot to Studio, and no additional Kids drawing during one
second idle (eight bands initially, still eight afterwards).

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

The first post-change run exposed clipping when Studio had large type enabled. Kids now explicitly uses
its fixed small font; the expanded Kids scenario and the original large-type layout sweep both pass.
The final `tests/run_tests.sh` run passed all 91 checks (baseline: 90), with the three skips listed above.
`git diff --check` passed.

GNU `size` on the native host executable (bytes; includes the entire simulator):

| Section | Baseline | Milestone 1 | Difference |
| --- | ---: | ---: | ---: |
| text | 1,119,709 | 1,126,701 | +6,992 |
| data | 26,620 | 26,620 | 0 |
| bss | 3,431,992 | 3,432,056 | +64 |
| total | 4,578,321 | 4,585,377 | +7,056 |

The complete Kids scenario simulated 8,190 ms with 318 blits across Kids and Studio; the host reported
render time of 0.3% of real time on its CPU. The idle assertion specifically measures Kids band draws,
not elapsed wall-clock performance. No new heap allocations or framebuffer were introduced.
The large-type sweep simulated 160,939 ms with 3,331 blits and no layout findings.

## Limits and next milestone

This is an opt-in emulator preview. Web source is integrated but its WASM build has not been validated
without Emscripten. Kids uses fixed small type and does not yet implement Studio's large-type or full
spoken-page navigation. It inserts single melodic notes; use Studio for chords and advanced editing.
Hardware SRAM/stack usage, firmware size and on-device render speed were not measured. Host measurements
are not estimates of device performance. Hiding screens does not remove their code or memory.

Next: instrument categories plus a small set of clearly named sound macros, reusing existing preset and
parameter APIs. Afterwards address envelopes/LFO/FX/mixer, then recording and scene interactions.

## Child-safety requirement (added during review)

Kids mode must support experimentation without damaging saved work or exposing destructive/system actions.
Acceptance for that stronger safety boundary:

- Kids edits must not automatically overwrite a saved project; an adult must explicitly approve saving.
- Deleting/replacing projects or presets, resetting settings, device management and installation tools must
  remain outside Kids access.
- Studio must have an explicit adult-controlled entry, rather than a secret gesture that a child can discover.
- Ordinary note, pattern and sound experiments should be undoable, with an obvious way to return to a safe state.
- Existing projects and advanced capabilities must remain available to the adult; safety must not delete them.

The current vertical slice is **not yet child-safe under these criteria**: SEL directly opens Studio and
existing autosave still applies to Kids edits. Emulator-only compilation prevents physical hardware access
by this implementation, but does not by itself protect saved emulator projects. These additional protections are prerequisites for treating the preview as ready for unsupervised use
by a child; they are not implemented in this commit.
