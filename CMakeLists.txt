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
# Current — Phase 2 canvas skeleton. A MIDI-effect plugin: VST3 + Standalone on
# every desktop OS, plus an AU on macOS.
#
# NEEDS_MIDI_INPUT/OUTPUT + IS_MIDI_EFFECT mark it as a MIDI processor.
# makeBusesProperties in the processor still exposes a stereo bus it never fills,
# because some VST3 hosts (Live) reject an effect with no audio bus.
#
# The macOS AU is where IS_MIDI_EFFECT earns its keep: with that flag the AU
# registers as an 'aumi' MIDI processor — the only plugin type Logic Pro /
# GarageBand surface in their MIDI-FX insert slot (no DAW gives a VST3 such a
# slot). AU rides this one target rather than a separate one as in Little Arp
# Monster: LAM splits so its VST3 (IS_MIDI_EFFECT FALSE) keeps a stereo bus while
# its AU (TRUE) sheds it at compile time — the shed being what makes an 'aumi'
# AU pass auval. Current instead sheds the AU's buses at runtime in
# makeBusesProperties (via getPluginLoadedAs), so all three formats share one
# target and one plugin code with no auval conflict (hosts disambiguate by
# format; the .component / .vst3 live in separate folders — nothing collides).
# AU is macOS-only; Linux/Windows stay VST3 + Standalone.
# ---------------------------------------------------------------------------
set(CURRENT_FORMATS VST3 Standalone)
if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    list(APPEND CURRENT_FORMATS AU)
endif()

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
    source/Engine.cpp
    source/MainView.cpp
    source/MenuBar.cpp
    source/StepperControl.cpp
    source/Canvas.cpp
    source/ModuleComponent.cpp
    source/PaletteBar.cpp
    source/InlineDialog.cpp
    source/ModuleWindow.cpp
    source/ProgressionStepList.cpp
    source/RhythmizeStepGrid.cpp
    source/SettingsView.cpp
    source/MessageStrip.cpp
    source/HelpText.cpp
    source/AuditionSynth.cpp
)

target_include_directories(Current PRIVATE include)

# Per-control help descriptions for the bottom help bar (messaging-area spec):
# resources/help.json is compiled into the binary and lazy-parsed by HelpText.
juce_add_binary_data(CurrentHelpData SOURCES resources/help.json)

juce_generate_juce_header(Current)

target_compile_definitions(Current PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Current PRIVATE
    CurrentHelpData
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

# ---------------------------------------------------------------------------
# Headless engine smoke test (off by default). Enable with
# -DCURRENT_BUILD_TESTS=ON to build + run it; it links only the engine and the
# JUCE modules it needs, no plugin wrappers.
# ---------------------------------------------------------------------------
option(CURRENT_BUILD_TESTS "Build the headless engine smoke test" OFF)
if(CURRENT_BUILD_TESTS)
    juce_add_console_app(current_engine_test)
    target_sources(current_engine_test PRIVATE
        tools/engine_smoketest.cpp
        source/Engine.cpp
    )
    target_include_directories(current_engine_test PRIVATE include)
    target_compile_definitions(current_engine_test PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )
    target_link_libraries(current_engine_test PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_basics
        juce::juce_core
        juce::juce_recommended_config_flags
    )

    # End-to-end audition-path test: links the full processor (and, because
    # createEditor drags it in, the editor sources), no plugin wrappers.
    # Current's makeBusesProperties is runtime-only (no JucePlugin_* macros),
    # so the plugin sources compile fine in a console app.
    juce_add_console_app(current_audition_test)
    target_sources(current_audition_test PRIVATE
        tools/audition_smoketest.cpp
        source/PluginProcessor.cpp
        source/PluginEditor.cpp
        source/Engine.cpp
        source/MainView.cpp
        source/MenuBar.cpp
        source/StepperControl.cpp
        source/Canvas.cpp
        source/ModuleComponent.cpp
        source/PaletteBar.cpp
        source/InlineDialog.cpp
        source/ModuleWindow.cpp
        source/ProgressionStepList.cpp
        source/RhythmizeStepGrid.cpp
        source/SettingsView.cpp
        source/MessageStrip.cpp
        source/HelpText.cpp
        source/AuditionSynth.cpp
    )
    target_include_directories(current_audition_test PRIVATE include)
    target_compile_definitions(current_audition_test PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )
    target_link_libraries(current_audition_test PRIVATE
        CurrentHelpData
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_gui_basics
        juce::juce_recommended_config_flags
    )
endif()
