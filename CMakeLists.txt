cmake_minimum_required(VERSION 3.22)

# Universal arch + minimum macOS version are pinned here (not in a build
# script) so every configure path - build_and_run_mac.sh, generate_xcode.sh,
# release_mac.sh - gets the same binary shape. See SnorkelAudioStandards
# build-scripts.md "Standards" for why this can't live in a script flag.
if (APPLE)
    # No FORCE: this is a default, not a stomp. generate_compile_commands.sh
    # intentionally passes a single-arch -DCMAKE_OSX_ARCHITECTURES=arm64 for
    # its side build (clangd-setup.md), which a cache variable already
    # populated on the command line takes priority over.
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "")
endif()

project(Current VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.14
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

# Desktop dev loop only for now (VST3 + AU + Standalone). AUv3/iPad packaging
# is real work (screen-size AUM tags, PRE_BUILD plist patching, per
# resize-scaling.md §1.6) that has nothing to test against until a later
# phase actually puts something on the canvas worth hearing.
if (APPLE)
    set(CURRENT_FORMATS VST3 AU Standalone)
else()
    set(CURRENT_FORMATS VST3 Standalone)
endif()

juce_add_plugin(Current
    COMPANY_NAME "Snorkel Audio"
    BUNDLE_ID "com.snorkelaudio.current"
    PLUGIN_MANUFACTURER_CODE Snrk
    PLUGIN_CODE Crnt
    FORMATS ${CURRENT_FORMATS}
    PRODUCT_NAME "Current"
    COPY_PLUGIN_AFTER_BUILD TRUE
    IS_SYNTH FALSE
    IS_MIDI_EFFECT TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
)

target_sources(Current
    PRIVATE
        source/PluginProcessor.cpp
        source/PluginEditor.cpp
        source/UI/PluginLookAndFeel.cpp
        source/UI/InlineDialog.cpp
        source/UI/GlobalSettingsBar.cpp
        source/Canvas/CanvasComponent.cpp
        source/Canvas/ModuleComponent.cpp
        source/Canvas/ModulePalette.cpp
)

target_compile_definitions(Current
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(Current
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
