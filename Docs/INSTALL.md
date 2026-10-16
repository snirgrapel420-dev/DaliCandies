# Install

## What was actually verified
Linux x86_64, GCC 13.3, CMake + Ninja, JUCE 8.0.8, VST3. Nothing else.

## Requirements
* CMake >= 3.22, a C++20 compiler (GCC 11+/Clang 14+/MSVC 2022), Ninja (optional)
* JUCE 8.0.8 in `External/JUCE` (`git clone --depth 1 --branch 8.0.8 https://github.com/juce-framework/JUCE.git External/JUCE`) or `-DDALI_JUCE_DIR=<path>`
* Linux: `libfreetype-dev libx11-dev libasound2-dev` (ALSA only needed unless `-DDALI_MINIMAL_LINUX=ON`), `xvfb` for headless GUI tests

## Build
```sh
CXXFLAGS="-DJUCE_USE_XRANDR=0 -DJUCE_USE_XINERAMA=0 -DJUCE_USE_XCURSOR=0 -DJUCE_USE_XRENDER=0 -DJUCE_USE_CURL=0 -DJUCE_WEB_BROWSER=0 -DJUCE_ALSA=0 -DJUCE_JACK=0" cmake -S . -B build -G Ninja -DDALI_MINIMAL_LINUX=ON   # Linux without Xrandr/ALSA/curl headers only; otherwise plain cmake -S . -B build
cmake --build build --target DaliCandy_VST3
```
Options: `DALI_BUILD_PLUGIN` (the single unified plug-in, ON), `DALI_SANITIZE` (ASan+UBSan, GCC/Clang), `DALI_BUILD_TESTS`, `DALI_BUILD_TOOLS` (ON), and `DALI_BUILD_INSTRUMENT` / `DALI_BUILD_FX` (OFF: the old two-plug-in split, kept only as a fallback).
The bundle appears in `build/DaliCandy_artefacts/Release/VST3/DaliCandy.vst3`.

## Install
* **Linux (verified to build and validate):** copy `DaliCandy.vst3` to `~/.vst3/` (or `/usr/lib/vst3`).
* **Windows (NOT verified):** copy to `C:\Program Files\Common Files\VST3\`, then rescan in Ableton Live (Preferences > Plug-ins > VST3). See `CONTINUATION.md`.
* **macOS (NOT verified, not a requested target):** `~/Library/Audio/Plug-Ins/VST3/`.

## User data
Presets: `~/.config/Dali Audio/DaliCandy/` (Linux; the platform's application-data folder elsewhere): `Presets/*.dalicandy`, `Presets/library.json` (favourites, ratings), `taste.json` (local taste model; never uploaded). Set `DALICANDY_DATA_DIR` to relocate (used by tests).

## Tests
```sh
cmake --build build --target DaliCandyTests DaliCandyEditorShots
xvfb-run -a build/DaliCandyTests_artefacts/Release/DaliCandyTests [name-filter]
xvfb-run -a build/DaliCandyEditorShots_artefacts/Release/DaliCandyEditorShots shots
pluginval --strictness-level 10 --validate-in-process "build/DaliCandy_artefacts/Release/VST3/DaliCandy.vst3"
```
