cmake_minimum_required(VERSION 3.22)

project(BusComp VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# JUCE dependency, pinned to a concrete stable release tag so that every
# build (local and CI) resolves to exactly the same JUCE revision.
# JUCE 8.0.10: https://github.com/juce-framework/JUCE/releases/tag/8.0.10
# JUCE is licensed under GPLv3/AGPLv3 or a commercial licence, see README.md.
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.10
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
# Global JUCE config: no web browser / curl dependency, deterministic VST3
# plugin UID across platforms, no splash screen. Must be set before JUCE's
# module targets are created via FetchContent.
add_compile_definitions(
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_HOST_CROSS_PLATFORM_UID=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
)

FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# BUS COMP - professional VCA-style bus compressor (VST3, Windows x64)
# ---------------------------------------------------------------------------
juce_add_plugin(BusComp
    COMPANY_NAME            "BusComp"
    BUNDLE_ID               "com.buscomp.buscomp"
    PLUGIN_MANUFACTURER_CODE Bscp
    PLUGIN_CODE             Bsc1
    PRODUCT_NAME            "BUS COMP"
    FORMATS                 VST3
    VST3_CATEGORIES         Dynamics
    VST3_MANUFACTURER       "BusComp"
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_SYNTH                FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(BusComp PRIVATE
    Source/ParameterDefs.cpp
    Source/CompressorDSP.cpp
    Source/SidechainFilter.cpp
    Source/GainReductionMeter.cpp
    Source/LevelMeter.cpp
    Source/PresetManager.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_link_libraries(BusComp PRIVATE
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_extra
)

# ---------------------------------------------------------------------------
# Automated DSP / parameter / state tests (console runner, also runs in CI)
# ---------------------------------------------------------------------------
juce_add_console_app(BusCompTests
    PRODUCT_NAME "BusCompTests"
)

target_sources(BusCompTests PRIVATE
    Tests/TestMain.cpp
    Tests/CompressorTests.cpp
    Tests/ParameterTests.cpp
    Tests/ProcessorStateTests.cpp
    Source/ParameterDefs.cpp
    Source/CompressorDSP.cpp
    Source/SidechainFilter.cpp
    Source/GainReductionMeter.cpp
    Source/LevelMeter.cpp
    Source/PresetManager.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_compile_definitions(BusCompTests PRIVATE
    JUCE_STANDALONE_APPLICATION=1
)

target_link_libraries(BusCompTests PRIVATE
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_extra
)

enable_testing()
add_test(NAME BusCompTests COMMAND BusCompTests)
