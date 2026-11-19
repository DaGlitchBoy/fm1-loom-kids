# Codex task — LION KIDS milestone 1: functional child-friendly emulator UI

**Repository:** https://github.com/vmphilemon-glitch/fm1-loom-kids
**Upstream reference:** https://github.com/charlesvestal/fm1-loom
**Preferred model:** Astra Low / Standard when available.
**Safety boundary:** NEVER flash, install firmware, run USB/MIDI device writers, modify the bootloader or hardware recovery. Only host/web-emulator work.

## Goal

Implement, compile and test an isolated first child-friendly UI vertical slice of LOOM-8. Make real interactive code changes. Do not merely produce a design proposal, demo picture, or mockup.

### Before editing

- Check the repository's checked-out commit and read `AGENTS.md`, `README.md`, `BUILDING.md`, `docs/MANUAL.md`, `app/ui.c`, `app/pages.c`, and existing graphics/input code. Actual paths and architecture take precedence over assumptions.
- Initialize submodules recursively if necessary.
- Establish baseline host build and regression status. Use `./build.sh --host`, `tests/run_tests.sh`, and optionally `./build.sh --web` if tooling exists. Report unavailable tools transparently.
- Work in a fresh isolated feature branch, e.g. `feat/loom-kids-ui`. Do not merge automatically.

### Child-friendly design

- Treat this as a small real groovebox, not a toy. Reduce navigation and text; use a restrained palette, icons and clear status feedback.
- Keep eight track identities, each with a stable color and simple icon/shape; initially emphasize four, but keep all eight visibly accessible.
- Prefer on-device procedural graphics and existing primitives, no new bitmap assets or animated textures.
- Persistent, truthful legend for current physical knob/button actions, aligned with actual FM1 hardware.
- Never silently overload a button with several hidden navigation actions. If complex editing is not yet adapted, provide an obvious path to original Studio UI.
- Render all musical state directly from existing application model; do not duplicate sequencer engine state or introduce heavy allocations/redraws.

### Implement in this task

1. **Isolated UI entry/fallback:** a reviewable switch between original and child-friendly UI, enabled safely in host/web emulator. No critical hidden gestures.
2. **Stage/Home:** eight colored/marked track identities, selected track, live transport status, current pattern and clear available interactions, all based on real state.
3. **Sequencer:** a clean 16-step view with moving playhead and obvious bar navigation across all 64 steps/patterns. Step toggles must call existing sequencer APIs and preserve drum/melodic semantics, with visible fallback for complex note editing.
4. **Control legend:** screen labels/icons for the four live rotary controls matching what they actually do on that screen.
5. **Tests:** at least one repeatable host test/scenario for navigating Stage->Sequencer, switching tracks, setting/removing a step, and returning to original UI. Include screenshots if supported.

### Explicitly defer

Do not redesign FX, ENV, LFO, instrument macros, presets, mixer, arrangement, recording engine, or performance routing in milestone one. Preserve access through original UI. No destructive simplification, save format changes or DSP modifications.

### Deliverables / acceptance

- Real code implementing the vertical slice in emulator, not just plans.
- Existing baseline build/tests remain functional or exact limitations documented.
- No hardware-specific write operations.
- Brief implementation report: commit, source files, tests executed/results, missing dependencies, memory/firmware-size evidence if available, screenshots and interaction notes.
- Propose the next narrowly scoped task: instrument categories + simple macros, followed by envelope/LFO/FX/mixer and recording/scene design.
