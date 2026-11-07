# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
cmake_minimum_required(VERSION 3.25)

# Pamplejuce helpers in /cmake (git submodule sudara/cmake-includes)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(PamplejuceVersion)
include(CPM)
include(PamplejuceMacOS)
# the libc++ of current SDKs no longer supports the template's 10.14
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
include(JUCEDefaults)

set(PROJECT_NAME "ToneWerk")
set(PRODUCT_NAME "ToneWerk")
set(COMPANY_NAME "Michi71")
set(BUNDLE_ID "de.michi71.tonewerk")
set(FORMATS Standalone AU VST3)

set(CMAKE_POLICY_DEFAULT_CMP0141 NEW CACHE STRING "" FORCE)
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded CACHE STRING "" FORCE)

project(${PROJECT_NAME} VERSION ${CURRENT_VERSION})

include(PamplejuceLog)
include(PamplejuceFunctions)

add_subdirectory(JUCE)
add_subdirectory(modules/clap-juce-extensions EXCLUDE_FROM_ALL)

# the sound engines pmtines, pmreeds, synthkeys and the MK8 effect chain (engine/UPSTREAM.md)
add_subdirectory(engine)

juce_add_plugin("${PROJECT_NAME}"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icon.png"
    COMPANY_NAME "${COMPANY_NAME}"
    BUNDLE_ID "${BUNDLE_ID}"
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE Mi71
    PLUGIN_CODE TnWk
    FORMATS "${FORMATS}"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    PRODUCT_NAME "${PRODUCT_NAME}")

clap_juce_extensions_plugin(TARGET "${PROJECT_NAME}"
    CLAP_ID "${BUNDLE_ID}"
    CLAP_FEATURES instrument synthesizer stereo)

# plugin code shared by the plugin targets and the tests (no fast math: the engine headers are inlined here too)
add_library(SharedCode INTERFACE)
target_compile_features(SharedCode INTERFACE cxx_std_20)
if(MSVC)
  target_compile_options(SharedCode INTERFACE /Zc:__cplusplus)
endif()

file(GLOB_RECURSE SourceFiles CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/source/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/source/*.h")
target_sources(SharedCode INTERFACE ${SourceFiles})

pamplejuce_xcode_prettify()

target_compile_definitions(SharedCode
    INTERFACE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    CMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
    VERSION="${CURRENT_VERSION}"
    PRODUCT_NAME_WITHOUT_VERSION="ToneWerk")

target_link_libraries(SharedCode
    INTERFACE
    soundengines
    mk8fx
    juce_audio_utils
    juce_audio_processors
    juce_dsp
    juce_gui_basics
    juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

target_link_libraries("${PROJECT_NAME}" PRIVATE SharedCode)

pamplejuce_add_tests()

include(GitHubENV)
