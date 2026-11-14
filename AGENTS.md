# LOOM Kids development guidance

Project: a child-friendly M-VAVE FM1 groovebox UI based on Charles Vestal's LOOM-8.
Upstream: https://github.com/charlesvestal/fm1-loom

## Non-negotiable safety

- Never flash, erase, install on, or communicate with a physical FM1. Do not invoke `fm1_install.py`, `fm1_resources.py`, USB/MIDI device-writing utilities, or recovery tools.
- Start with host and web emulators. Do not modify the bootloader, flash layout, install package, or `base/` submodules in milestone one.
- Do not modify DSP, audio interrupts, synthesis, sequencer timing, or project/preset serialization in milestone one.
- Preserve eight tracks, 64-step patterns, recording, effects, envelopes, LFO, mixer, scenes, song arrangement, presets, and Studio access. Hide complexity rather than deleting functionality.
- Keep original LOOM-8 UI as a reachable fallback; isolate new UI in well-scoped code.
- Avoid added full-screen framebuffers, bitmap animations, animation frameworks, idle redraw loops, large allocations, and any allocations inside redraw paths. Prefer existing graphics primitives and event-driven partial rendering.
- Do not assume that hiding screens saves significant RAM. Measure memory, firmware size, host regressions and (if testable) render performance.
- Work only on an isolated feature branch. Never push to upstream or merge without review.
- If dependencies are unavailable, clearly say which tests/builds could not run. Do not claim success without evidence.

## Product and interaction goals

- Audience: a five-year-old learning real music production, not a toddler game. Playful but restrained visual style, strong contrast, consistent track colors/icons, low text density, and understandable controls.
- Keep eight musical tracks; emphasize four starting tracks without hiding the others behind obscure actions.
- Use direct physical-key interactions with visible modes and knob legends. Avoid secret long presses and nesting multiple menus behind a single button.
- The musical workflow must remain real and functional: play, record, add/remove steps, select sounds, loop, mix, arrange, save and perform. Advanced features can live in the original UI until redesigned.
- Account for the FM1's actual controls and tiny 240x240 display. Confirm physical mappings from code/manual rather than assumptions.

## Work procedure

1. Read `README.md`, `BUILDING.md`, `docs/MANUAL.md`, `app/ui.c`, `app/pages.c`, graphics, and input code before making changes. Record the source commit.
2. Check submodules with `git submodule update --init --recursive` and run baseline `./build.sh --host` and `tests/run_tests.sh` where dependencies permit; use `./build.sh --web` when Emscripten is available.
3. Follow `TASK_FOR_CODEX.md` for milestone one. Make functional UI code changes, not mockups or a plan alone.
4. Add a repeatable emulator/host scenario, preserve original behavior, document tests and failures, and attach screenshots or paths if supported.
5. Summarize the exact files changed, code and test evidence, known limitations, size/memory readings if available, and a narrowly scoped next milestone.

Keep original copyright/license notices. Consult `LICENSING.md` before redistribution; the combined LOOM-8 project states GPL-3.0-only.
