# Test results (real runs, Linux x86_64, GCC 13, Release build)

Raw logs: `Docs/results/`. Date of the final run: 2026-10-16.

| What | How | Result |
|---|---|---|
| Unit / integration tests | `DaliCandyTests` (76 cases, 706,663 checks) | **76 passed, 0 failed checks** (`unit-tests.log`) |
| Same tests under AddressSanitizer + UBSan + LeakSanitizer | `-DDALI_SANITIZE=ON` build, `detect_leaks=1` | **76 passed, 0 sanitizer reports** (`asan-ubsan-tests.log`; run on the build just before a one-line change to how presets restore the family parameters, which the normal build and its tests do include); the allocation-counting test is skipped there because it replaces `operator new` |
| pluginval 1.0.4, the single `DaliCandy.vst3` | strictness 10, in-process, GUI tests on, xvfb | **SUCCESS** (`pluginval-DaliCandy.log`) |
| Headless editor smoke test (unified plug-in) | `DaliCandyEditorShots`: generate, import track, create editor, show the Kitchen and the 5 advanced pages, edit, resize 1040x700..1900x1200 | **PASSED**, 6 screenshots (`Docs/screenshots/unified_page*.png`, `editor-smoke.log`) |

No sanitizer run was done on the plug-in binary or the editor itself (only the test runner, which exercises the processor and engine code but not the GUI pages).

Covered by the unit tests (new for the Kitchen: 6 families partition all archetypes; all 15 fusions produce valid, loudness-staged, deterministic sounds with layers from both families; SPICE raises modulation; KEEP preserves engines / envelopes / levels and changes parameters; synced windows 1/16, 1/4 and 1 bar are fitted to 50-130 % of the target (the test requires at least two thirds of single attempts to be accepted; one run accepted 22 of 27; the batch generator retries); DRIFT: off = identical repeats, on = evolving repeats, reproducible and independent of block size; ROLL ignores the user's choice and is reproducible for a seed; the dragged WAV is exactly the window length; LISTEN steers toward the quietest band, places events and generates): the unified plug-in used as effect (input passes through, candy on top) and as instrument (input off, MIDI plays, no pass-through); engine output at 44.1 / 48 / 88.2 / 96 kHz and several block sizes; block-size independence (engine, processor, sequencer, 64..2048 samples); sample-accurate event starts; each event fires exactly once; tempo change / transport jump handling; **zero heap allocations in processBlock**; state save / load round trip and corrupt-state handling; concurrent edits while audio runs; generation determinism, diversity (>= 8 candidates), cancellation; mutate / regenerate keep locks; breed / recall / taste bias; preset round trip, v1 migration, newer-version rejection, library lifecycle; WAV export for 16 / 24 / 32f at all four rates, normalisation never clips, layers / dry-wet / batch / MIDI export, drag-and-drop temp files are real and cleaned; FX dry / wet / sidechain / pass-through; capture, analysis, suggestions, placement.

Performance (test `processor_cpu_load_is_reasonable`, 48 kHz / 512, dense pattern, single core, 2-core shared container): mean 2.8-3.4 % of the block budget over several runs. The *worst single block* varied between about 10 % and 48 % from run to run on this shared VM, so it is not a reliable figure; I did not establish whether those spikes are scheduler noise or real peaks (e.g. event starts). Treat CPU numbers as indicative only.

## Not run
* pluginval's Steinberg `vst3 validator` step was skipped (validator path not set); no separate Steinberg validator run was done.
* Windows build, Ableton Live, macOS, other hosts: **not run**.
* No interactive (human) mouse testing of the GUI, no real DAW drag-and-drop target.
* No sanitizer or valgrind pass over the built plug-in binary / GUI.
