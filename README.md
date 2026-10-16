# DaliCandy - ear-candy generator (Dali Audio)

**One** VST3 plug-in, `DaliCandy.vst3`, that is both:

* an **instrument** - MIDI in, stereo out (MIDI channel 1-8 selects the sound slot, note = pitch offset), plus the host-synced sequencer; and
* an **effect** - stereo audio input (optional) with dry/wet, optional sidechain, live and captured analysis of the incoming audio.

It behaves as an effect while its main audio input is connected and as an instrument when the host switches that input off (as most hosts do on instrument tracks). The plug-in declares the VST3 categories `Instrument|Synth|Fx`; **how a particular host lists a plug-in that is both (instrument list, effect list or both) is decided by the host and was not tested** - see `Docs/KNOWN_LIMITATIONS.md`.

It contains: 6 synthesis engines (MicroSynth, Resonator, Granular, Glitch, Spectral/STFT, Noise & Motion), up to 4 layers per sound, an intelligent generate pipeline (34 archetypes -> render -> analyse -> reject -> score -> diverse selection of 8 candidates), Candy DNA with deterministic seeds (Mutate / Lock / Evolve / Breed / Recall), a 32-route modulation matrix (4 LFO, 4 MSEG, 2 S&H, 2 chaos, random walk, step, follower, transient, velocity, note, wheel, aftertouch, 4 macros), 8 sequencer modes (sample-accurate, host-synced), track analysis with an opportunity map and editable placement suggestions, a Negative Space engine, masking awareness, versioned presets (56 factory presets embedded), local taste learning, a simple **Kitchen** main screen (six sound families, 15 fusions, ROLL / COOK / KEEP / SPICE / DRIFT / LISTEN, tempo-synced lengths from 1/16 to 4 bars, drag-out) and the original 5 advanced pages behind an ADVANCED button, all in a dark neon GUI.

> **Status in one paragraph.** Everything in this repository was built and tested on **Linux x86_64 (GCC 13)** only. The plug-in builds, passes **pluginval 1.0.4 at strictness 10 with GUI tests** (under xvfb), and 76 unit/integration test cases (also clean under ASan + UBSan). **The Windows build and Ableton Live have not been built, loaded or tested** - see `Docs/CONTINUATION.md` for the exact procedure. Read `Docs/KNOWN_LIMITATIONS.md` before relying on anything.

## Documentation

| File | Content |
|---|---|
| `Docs/INSTALL.md` | Build and install (Linux verified; Windows/macOS procedure, untested) |
| `Docs/USER_GUIDE.md` | What every page does, workflow, drag-and-drop, export |
| `Docs/ARCHITECTURE.md` | Threading model, real-time rules, module map, file formats |
| `Docs/TEST_RESULTS.md` | What was run, with real results and logs in `Docs/results/` |
| `Docs/KNOWN_LIMITATIONS.md` | Honest list of gaps and unverified areas |
| `Docs/CONTINUATION.md` | How to continue: Windows/Ableton verification checklist, open work |
| `Docs/screenshots/` | Headless renders: the Kitchen (page 1) and the five advanced pages |

## Quick start (Linux)

```sh
git clone --depth 1 --branch 8.0.8 https://github.com/juce-framework/JUCE.git External/JUCE
CXXFLAGS="-DJUCE_USE_XRANDR=0 -DJUCE_USE_XINERAMA=0 -DJUCE_USE_XCURSOR=0 -DJUCE_USE_XRENDER=0 -DJUCE_USE_CURL=0 -DJUCE_WEB_BROWSER=0 -DJUCE_ALSA=0 -DJUCE_JACK=0" cmake -S . -B build -G Ninja -DDALI_MINIMAL_LINUX=ON
cmake --build build --target DaliCandy_VST3 DaliCandyTests
xvfb-run -a build/DaliCandyTests_artefacts/Release/DaliCandyTests          # tests
cp -r "build/DaliCandy_artefacts/Release/VST3/DaliCandy.vst3" ~/.vst3/
```

On a Linux machine without the Xrandr/Xinerama/Xcursor/ALSA dev headers the `CXXFLAGS=...` environment variable is required (the `-DCMAKE_CXX_FLAGS` form does not reach JUCE's helper tool `juceaide`, which is built at configure time and does not see `DALI_MINIMAL_LINUX`). With the full dev packages installed, plain `cmake -S . -B build` is enough.

## Layout

```
Source/Core        data model (CandyDNA, SeqConfig, GenSettings), RNG, DSP utils, JSON
Source/DSP         6 engines, modulation engine, effects, voice manager (CandyEngine)
Source/Analysis    features, negative space, masking, live + track analysis
Source/Generator   archetypes, generator pipeline, taste model
Source/Sequencer   8 planning modes + sample-accurate host-synced player
Source/Rendering   offline renderer, WAV/MIDI exporter, drag-and-drop temp files
Source/Presets     versioned preset format, preset manager
Source/Plugin      parameters, processor (shared by both plug-ins), factory bank
Source/UI          neon look and feel, widgets, five pages, editor
Resources/FactoryPresets   56 .dalicandy presets (regenerate with DaliCandyBankTool)
Tests, Tools       test runner, bank tool, headless GUI smoke test / screenshot tool
```
