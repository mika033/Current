cmake_minimum_required(VERSION 3.22)

set(PLUGIN_VERSION "0.1.0")

set(COMPANY_NAME "Snorkel Audio")
set(PLUGIN_MANUFACTURER_CODE "Snrk")
set(PLUGIN_BUNDLE_ID_BASE "com.SnorkelAudio.Current")

project(Current VERSION ${PLUGIN_VERSION})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# On desktop macOS ship a Universal binary (mirrors Little Arp Monster). Ignored
# on Linux/Windows and on iOS, where the arch is driven by the SDK.
if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS universal binary" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "macOS minimum" FORCE)
endif()

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.12
    GIT_SHALLOW TRUE
    GIT_SUBMODULES ""
)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(JUCE)

if(MSVC)
    add_compile_options(/MP)
endif()

# ---------------------------------------------------------------------------
# Current — Phase 1 canvas skeleton. A MIDI-effect plugin: VST3 + Standalone on
# desktop. macOS would also ship an AU MIDI-FX variant like Little Arp Monster,
# but Phase 1 is developed and build-tested on Linux, where only VST3 +
# Standalone exist.
#
# NEEDS_MIDI_INPUT/OUTPUT + IS_MIDI_EFFECT mark it as a MIDI processor.
# makeBusesProperties in the processor still exposes a stereo bus it never fills,
# because some VST3 hosts (Live) reject an effect with no audio bus.
# ---------------------------------------------------------------------------
set(CURRENT_FORMATS VST3 Standalone)

juce_add_plugin(Current
    COMPANY_NAME "${COMPANY_NAME}"
    BUNDLE_ID "${PLUGIN_BUNDLE_ID_BASE}"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    PLUGIN_MANUFACTURER_CODE ${PLUGIN_MANUFACTURER_CODE}
    PLUGIN_CODE Curr
    FORMATS ${CURRENT_FORMATS}
    PRODUCT_NAME "Current"
)

target_sources(Current PRIVATE
    source/PluginProcessor.cpp
    source/PluginEditor.cpp
    source/MainView.cpp
    source/MenuBar.cpp
    source/Canvas.cpp
    source/ModuleComponent.cpp
    source/PaletteBar.cpp
    source/InlineDialog.cpp
)

target_include_directories(Current PRIVATE include)

juce_generate_juce_header(Current)

target_compile_definitions(Current PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Current PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
