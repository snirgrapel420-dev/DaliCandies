# Architecture

## Threads
* **Audio thread** - only `CandyProcessor::processBlock`. It reads immutable objects published through atomics: the sound bank (engine), the sequencer pattern (`SeqPlayer::publish`, graveyard of 16), the capture buffer (raw atomic pointer), adaptive-mix parameters (atomics). No locks, no allocation (proved by a test that overrides global `operator new` and counts), no I/O.
* **Message thread** - all model edits (`Preset`, `CandyDNA`, parameters, taste), guarded by one `CriticalSection`, then publication of a new immutable bank / pattern. A 30 Hz timer coalesces rebuilds (parameter listeners only set atomic flags, they may run on any thread).
* **Worker thread** - one-thread `ThreadPool`: generation, mutation, analysis, preview render, export. Results return through a mailbox + `AsyncUpdater`. Jobs poll a cancel flag.
* Editor polls `takeUiFlags()` (accumulated `ChangeFlag` bits) at 30 Hz and refreshes only the visible page.

## Determinism
`Rng` (PCG32) with `hashCombine` / `derive`. A generated sound is a pure function of (settings, seed, sample rate used for the preview). Offline rendering is history-independent (test `renderer_is_repeatable_and_history_independent`). Engines process in 16-sample chunks so output does not depend on the host block size (tests compare 64..2048).

## Signal path
`CandyEngine` = up to 8 concurrent events (`kMaxVoices`) x up to 4 layers -> per-layer insert + sends -> shared delay / reverb / mod-FX -> master (width, low cut, gain, soft protector). Instrument: MIDI note -> event (note - 60 = pitch offset, MIDI channel -> sound slot) plus sequencer events. FX: output = in * dry + candy * wet * gain; analysis source = main input or sidechain bus.
Gain staging: generator targets about -6 dBFS peak (rejects sounds that would need more than +12 dB master + 6 dB layer gain).

## Sequencer
`planEvents` (8 modes) produces events in beats from loop start. `SeqPlayer::render` derives the window from host PPQ with continuity tracking, so each event fires exactly once for any block size, tempo change or loop. Probability and live-omit decisions are hashes of (event, loop index).

## Files
`.dalicandy` = JSON; `kPresetVersion = 2`; migration in `migratePresetVar`; parameter IDs are versioned (`ParameterID{id, 1}`) and must never be renamed. Plug-in state = APVTS XML + preset JSON + UI state.

## Kitchen layer (families, fusion, drift, listen)
* `Generator/Families.*`: six families map archetypes by category (`familyOfCategory`); `fusionName(a,b)` names the 15 pairs. `GenSettings` carries `familyA/familyB/spice/syncLength` (-1 = classic Category mode, so the old GENERATE page and all older tests are unchanged).
* `CandyGenerator::generateOne`: pick an archetype from family A (and B for a fusion) -> `buildRaw` (build + modulation + FX) -> `fuse` (layers of both, <= 4; routes re-targeted to the new layer slots; B's modulators re-allocated to free LFO / MSEG / S&H / chaos slots of the same kind, routes without a free slot are dropped; FX halfway) -> `applySpice` -> render / analyse / gain-stage -> `fitLength` for synced windows (rescales envelopes, sends and tails up to 5 passes; rejects if the audible length ends outside 50-130 % of the window).
* KEEP = `CandyGenerator::variations` -> `mutate(..., keepSkeleton=true)` (parameters, small pitch moves, seeds, LFO rates, route depths only).
* DRIFT lives in `CandyEngine::startVoice`: three mean-reverting random walks (pitch, length, 16 parameter offsets) advanced once per event with a private deterministic `Rng` that `reset()` re-seeds; 0 = off. Because it is advanced at event time inside the engine it is independent of the host block size (tested).
* Synced length: `CandyProcessor::syncWindowMs()` = beats x 60000 / host BPM (`hostBpm_` is published from `processBlock`). `ExportFormat::fitMs` makes the one-shot renderer cut (8 ms fade) or zero-pad to exactly that many samples.
* ROLL / LISTEN are processor methods (`rollWith(seed)`, `listen()`), results arrive through the same mailbox as every other job. LISTEN sets `listenPending_`; when the analysis arrives the pattern is placed from its suggestions and `generate()` runs with `kitchenSettings()` steering brightness to the quietest band.
* GUI: `UI/KitchenPage.cpp` (flavour matrix, plates, drag pads, on-screen keys via `CandyProcessor::keyboardState`, which `processBlock` merges into the MIDI buffer).
