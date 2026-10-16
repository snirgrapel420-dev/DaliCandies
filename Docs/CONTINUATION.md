# Continuation procedure

## A. Verify on Windows / Ableton Live (not done yet)
Shortcut: push this repository to GitHub; `.github/workflows/build.yml` builds `DaliCandy.vst3` on a Windows runner, runs the tests and pluginval, and offers the plug-in as a downloadable artifact. That workflow has never been run (written without GitHub access) and may need small fixes. Manual steps follow.

1. Install Visual Studio 2022 (Desktop C++), CMake >= 3.22, Git.
2. `git clone --depth 1 --branch 8.0.8 https://github.com/juce-framework/JUCE.git External/JUCE`
3. ```bat
   cmake -S . -B build -G "Visual Studio 17 2022" -A x64
   cmake --build build --config Release --target DaliCandy_VST3 DaliCandyTests
   build\DaliCandyTests_artefacts\Release\DaliCandyTests.exe
   ```
   (Do **not** pass `DALI_MINIMAL_LINUX`.) Expect `64 test cases run, 0 failed`. Any failure here is a real Windows finding - note it in `Docs/results/`.
4. Copy `DaliCandy.vst3` to `C:\Program Files\Common Files\VST3\`. Run pluginval (Windows build) at strictness 10; also run Steinberg's `validator.exe` (VST3 SDK `vst3validator`).
5. Ableton Live (11/12): rescan VST3; check -
   * Loaded on an **instrument track**: loads, plays MIDI on 16 channels, sequencer follows transport, tempo changes, loop / jump; output at 44.1 / 48 / 96 kHz and buffer 64..2048; freeze / flatten works.
   * Loaded as an **effect on an audio track** (this is the key unknown for the single-plug-in design: does Ableton list/allow a `Instrument|Synth|Fx` VST3 on audio tracks, and does it route audio to the input?), dry/wet, sidechain routing (Sidechain bus), Capture arms and records at the next bar line, Import WAV.
   * Save / reload a Live set: state restores (sound, pattern, frozen events, analysis-independent parameters).
   * GUI: open / close repeatedly, resize, HiDPI scaling, preset browser, file dialogs (export, import), **drag a waveform / DRAG WAV into an Ableton clip slot or arrangement**.
   * Automation of macros / sequencer parameters; no zipper noise.
   * If Ableton will not offer the plug-in in the place you need, build the legacy split plug-ins instead: `-DDALI_BUILD_INSTRUMENT=ON -DDALI_BUILD_FX=ON` (two separate VST3s from the same code).
6. Record the results (pass / fail / notes) in `Docs/results/windows-ableton.md` and update `KNOWN_LIMITATIONS.md`.

## B. Open work worth doing next
* Resolve the up-to-6 dB sample-rate level outlier in Resonator-type sounds (compare engine output per rate in `Tests/EngineTests.cpp`, tighten the export test from `peak <= 0 dB` to a level-match assertion).
* Run TSan, and ASan on `DaliCandyEditorShots` / the plug-in (ASan+UBSan on `DaliCandyTests` is done: `-DDALI_SANITIZE=ON`).
* Listen-through and hand-tuning of the factory bank; extend `Tools/BankTool.cpp` with curated settings.
* Undo / redo, HiDPI review, keyboard shortcuts, MPE.
* Hosts that provide no PPQ (some report none while stopped): confirm *Free run* behaviour.

## C. Regenerating things
* Factory bank: `DaliCandyBankTool` writes `Resources/FactoryPresets/*.dalicandy`; they are embedded via `juce_add_binary_data` at build time.
* Screenshots / smoke test: `DaliCandyEditorShots <dir>` under xvfb.
* Parameter IDs (`Source/Plugin/Params.h`) are a saved-state contract: add new ones, never rename or repurpose old ones.
