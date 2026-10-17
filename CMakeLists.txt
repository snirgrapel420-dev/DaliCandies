cmake_minimum_required(VERSION 3.22)
project(DaliCandy VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# JUCE: use -DDALI_JUCE_DIR=<path> or the copy in External/JUCE
# (git clone --depth 1 --branch 8.0.8 https://github.com/juce-framework/JUCE.git External/JUCE)
# ---------------------------------------------------------------------------
set(DALI_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/External/JUCE" CACHE PATH "Path to JUCE")
add_subdirectory(${DALI_JUCE_DIR} JUCE EXCLUDE_FROM_ALL)

option(DALI_SANITIZE "Build Dali targets with AddressSanitizer + UBSan (GCC/Clang)" OFF)
option(DALI_BUILD_TESTS "Build the DaliCandyTests console runner" ON)
option(DALI_BUILD_PLUGIN "Build the unified DaliCandy plugin (instrument + effect in ONE VST3)" ON)
option(DALI_BUILD_FX "Also build the legacy split DaliCandy FX plugin" OFF)
option(DALI_BUILD_INSTRUMENT "Also build the legacy split DaliCandy Instrument plugin" OFF)

# Common definitions. On minimal Linux machines (no ALSA / curl / Xrandr headers)
# set the corresponding JUCE_* flags to 0; they are harmless on Windows/macOS.
set(DALI_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

option(DALI_MINIMAL_LINUX "Disable optional Linux libs (Xrandr/Xinerama/Xcursor/ALSA/JACK) for minimal build machines" OFF)
if(DALI_MINIMAL_LINUX AND UNIX AND NOT APPLE)
    list(APPEND DALI_DEFS JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0 JUCE_USE_XRENDER=0 JUCE_ALSA=0 JUCE_JACK=0)
endif()

# ---- shared sources ---------------------------------------------------------
file(GLOB_RECURSE DALI_CORE_SOURCES CONFIGURE_DEPENDS
    Source/Core/*.cpp Source/DSP/*.cpp Source/Analysis/*.cpp Source/Generator/*.cpp
    Source/Sequencer/*.cpp Source/Rendering/*.cpp Source/Presets/*.cpp)
file(GLOB DALI_PLUGIN_SOURCES CONFIGURE_DEPENDS Source/Plugin/*.cpp)
file(GLOB DALI_UI_SOURCES CONFIGURE_DEPENDS Source/UI/*.cpp)

set(DALI_JUCE_LIBS
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_dsp
    juce::juce_gui_basics)

# Factory presets are embedded into the binaries (regenerate with DaliCandyBankTool)
file(GLOB DALI_FACTORY_PRESETS CONFIGURE_DEPENDS Resources/FactoryPresets/*.dalicandy)
juce_add_binary_data(DaliCandyFactoryData NAMESPACE DaliFactory HEADER_NAME FactoryData.h SOURCES ${DALI_FACTORY_PRESETS})
set_target_properties(DaliCandyFactoryData PROPERTIES POSITION_INDEPENDENT_CODE ON)

function(dali_configure_target tgt)
    target_include_directories(${tgt} PRIVATE Source)
    target_compile_definitions(${tgt} PRIVATE ${DALI_DEFS} ${ARGN})
    if(tgt MATCHES "Tests|BankTool")
        target_compile_definitions(${tgt} PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)   # test runner pumps the message loop
    else()
        target_compile_definitions(${tgt} PRIVATE JUCE_MODAL_LOOPS_PERMITTED=0)
    endif()
    target_compile_features(${tgt} PRIVATE cxx_std_20)
    target_link_libraries(${tgt} PRIVATE ${DALI_JUCE_LIBS}
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)
    if(DALI_SANITIZE AND NOT MSVC)
        target_compile_options(${tgt} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
        target_link_options(${tgt} PRIVATE -fsanitize=address,undefined)
    endif()
    if(MSVC)
        target_compile_options(${tgt} PRIVATE /W4 /wd4100 /wd4244 /wd4267 /wd4458 /wd4456 /fp:precise)
    else()
        target_compile_options(${tgt} PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare)
    endif()
endfunction()

# ---- plugins ----------------------------------------------------------------
# The shipping product: ONE plug-in. It is registered as an EFFECT so that hosts (Ableton) give it the audio of the track it sits on;
# that audio is what it listens to. MIDI is still accepted where the host sends it; use "DaliCandy Instrument" for a MIDI-only instrument track.
if(DALI_BUILD_PLUGIN)
    juce_add_plugin(DaliCandy
        COMPANY_NAME "Dali Audio"
        PLUGIN_MANUFACTURER_CODE Dali
        PLUGIN_CODE DaCa
        FORMATS VST3
        BUNDLE_ID com.daliaudio.dalicandy
        PRODUCT_NAME "DaliCandy"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        VST3_CATEGORIES Fx
        COPY_PLUGIN_AFTER_BUILD FALSE)
    target_sources(DaliCandy PRIVATE ${DALI_CORE_SOURCES} ${DALI_PLUGIN_SOURCES} ${DALI_UI_SOURCES})
    dali_configure_target(DaliCandy DALI_IS_FX=1 DALI_WITH_EDITOR=1)
    target_link_libraries(DaliCandy PRIVATE DaliCandyFactoryData)
endif()

if(DALI_BUILD_INSTRUMENT)
    juce_add_plugin(DaliCandyInstrument
        COMPANY_NAME "Dali Audio"
        PLUGIN_MANUFACTURER_CODE Dali
        PLUGIN_CODE DcIn
        FORMATS VST3
        BUNDLE_ID com.daliaudio.dalicandy.instrument
        PRODUCT_NAME "DaliCandy Instrument"
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        VST3_CATEGORIES Instrument Synth
        COPY_PLUGIN_AFTER_BUILD FALSE)
    target_sources(DaliCandyInstrument PRIVATE ${DALI_CORE_SOURCES} ${DALI_PLUGIN_SOURCES} ${DALI_UI_SOURCES})
    dali_configure_target(DaliCandyInstrument DALI_IS_FX=0 DALI_WITH_EDITOR=1)
    target_link_libraries(DaliCandyInstrument PRIVATE DaliCandyFactoryData)
endif()

if(DALI_BUILD_FX)
    juce_add_plugin(DaliCandyFX
        COMPANY_NAME "Dali Audio"
        PLUGIN_MANUFACTURER_CODE Dali
        PLUGIN_CODE DcFx
        FORMATS VST3
        BUNDLE_ID com.daliaudio.dalicandy.fx
        PRODUCT_NAME "DaliCandy FX"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        VST3_CATEGORIES Fx
        COPY_PLUGIN_AFTER_BUILD FALSE)
    target_sources(DaliCandyFX PRIVATE ${DALI_CORE_SOURCES} ${DALI_PLUGIN_SOURCES} ${DALI_UI_SOURCES})
    dali_configure_target(DaliCandyFX DALI_IS_FX=1 DALI_WITH_EDITOR=1)
    target_link_libraries(DaliCandyFX PRIVATE DaliCandyFactoryData)
endif()

# ---- tools (factory bank generator) -------------------------------------------
option(DALI_BUILD_TOOLS "Build DaliCandyBankTool (regenerates Resources/FactoryPresets)" ON)
if(DALI_BUILD_TOOLS)
    juce_add_console_app(DaliCandyBankTool PRODUCT_NAME "DaliCandyBankTool")
    target_sources(DaliCandyBankTool PRIVATE Tools/BankTool.cpp ${DALI_CORE_SOURCES})
    dali_configure_target(DaliCandyBankTool DALI_IS_FX=0 DALI_WITH_EDITOR=0)
endif()

# Headless GUI smoke test + screenshots (run under xvfb on Linux)
if(DALI_BUILD_TOOLS)
    juce_add_console_app(DaliCandyEditorShots PRODUCT_NAME "DaliCandyEditorShots")
    juce_generate_juce_header(DaliCandyEditorShots)
    target_sources(DaliCandyEditorShots PRIVATE Tools/EditorShots.cpp ${DALI_CORE_SOURCES} ${DALI_PLUGIN_SOURCES} ${DALI_UI_SOURCES})
    dali_configure_target(DaliCandyEditorShots DALI_IS_FX=0 DALI_WITH_EDITOR=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(DaliCandyEditorShots PRIVATE DaliCandyFactoryData)
endif()

# ---- tests ------------------------------------------------------------------
if(DALI_BUILD_TESTS)
    file(GLOB DALI_TEST_SOURCES CONFIGURE_DEPENDS Tests/*.cpp)
    juce_add_console_app(DaliCandyTests PRODUCT_NAME "DaliCandyTests")
    juce_generate_juce_header(DaliCandyTests)
    target_sources(DaliCandyTests PRIVATE ${DALI_TEST_SOURCES} ${DALI_CORE_SOURCES} ${DALI_PLUGIN_SOURCES})
    dali_configure_target(DaliCandyTests DALI_IS_FX=0 DALI_WITH_EDITOR=0)
    target_link_libraries(DaliCandyTests PRIVATE DaliCandyFactoryData)
endif()
