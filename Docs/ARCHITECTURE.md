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
