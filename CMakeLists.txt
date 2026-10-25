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

option(TONEWERK_DEV_BANKS "Let the build find the banks in its source tree (banks/)" ON)
option(TONEWERK_BUNDLE_BANKS "Copy banks/*.bank into the built plugin bundles and next to the standalone app" OFF)

set(CMAKE_POLICY_DEFAULT_CMP0141 NEW CACHE STRING "" FORCE)
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded CACHE STRING "" FORCE)

project(${PROJECT_NAME} VERSION ${CURRENT_VERSION})

include(PamplejuceLog)
include(PamplejuceFunctions)

add_subdirectory(JUCE)
add_subdirectory(modules/clap-juce-extensions EXCLUDE_FROM_ALL)

# the pmpiano engine (engine/UPSTREAM.md)
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
if(TONEWERK_DEV_BANKS)
  target_compile_definitions(SharedCode INTERFACE TONEWERK_DEV_BANKS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/banks")
endif()

target_link_libraries(SharedCode
    INTERFACE
    pmpcore
    juce_audio_utils
    juce_audio_processors
    juce_dsp
    juce_gui_basics
    juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

target_link_libraries("${PROJECT_NAME}" PRIVATE SharedCode)

# banks next to the binaries for a distribution (source/BankLocator.cpp searches there)
if(TONEWERK_BUNDLE_BANKS)
  file(GLOB BankFiles "${CMAKE_CURRENT_SOURCE_DIR}/banks/*.bank")
  foreach(format ${FORMATS} CLAP)
    set(target "${PROJECT_NAME}_${format}")
    if(NOT TARGET ${target})
      continue()
    endif()
    get_target_property(artefact ${target} JUCE_PLUGIN_ARTEFACT_FILE)
    if(format STREQUAL "VST3" OR APPLE)
      set(dest "${artefact}/Contents/Resources/banks")          # a bundle (VST3 on every platform, AU, CLAP and the app on macOS)
    else()
      set(dest "$<TARGET_FILE_DIR:${target}>/banks")            # the folder of the standalone executable or the .clap file
    endif()
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${dest}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${BankFiles} "${dest}"
        VERBATIM)
  endforeach()
endif()

pamplejuce_add_tests()
target_compile_definitions(Tests PRIVATE TONEWERK_TEST_BANKS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/banks")

include(GitHubENV)
