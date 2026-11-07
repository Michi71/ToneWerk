# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
# The sound engines (engine/UPSTREAM.md): pmtines, pmreeds and synthkeys, and the MK8 effect chain of pmpiano. No dependencies,
# no fast math (key_voice.cpp refuses it).
#
# All three engines call themselves pmp::Engine. Each is built as its own static library with the namespace renamed by the
# preprocessor (-Dpmp=pmp_tines, ...), so their symbols cannot collide and the copied sources stay untouched; the plugin
# reaches them only through adapters/sound_engine.h. The effect chain keeps the name pmp.
option(TONEWERK_FLOAT "Single-precision real-time path of the engines (PMP_FLOAT)" OFF)

function(tonewerk_engine_options target)
  # SYSTEM: the plugin builds with JUCE's recommended warnings (-Wsign-conversion, -Wfloat-equal, ...), which the engines'
  # headers are not written for; their own repositories build them with -Wall -Wextra -Wpedantic
  set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON SYSTEM ON)
  target_compile_features(${target} PUBLIC cxx_std_17)
  if(TONEWERK_FLOAT)
    target_compile_definitions(${target} PUBLIC PMP_FLOAT)
  endif()
  if(WIN32)
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)   # fopen, getenv
  endif()
  if(MSVC)
    target_compile_options(${target} PRIVATE /fp:precise /bigobj)
  else()
    target_compile_options(${target} PRIVATE -fno-fast-math $<$<NOT:$<CONFIG:Debug>>:-O2>)
    # x86-64 without AVX: the four-lane double vectors of key_voice.cpp are passed between inline functions with internal
    # linkage only, so the AVX calling convention note does not apply
    target_compile_options(${target} PRIVATE $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-Wno-psabi>)
  endif()
endfunction()

# --- the MK8 preamp and effect chain (pmpiano src/core, namespace pmp) --------------------------------------------------
add_library(mk8fx STATIC fx/src/core/mk8_fx.cpp)
target_include_directories(mk8fx PUBLIC fx/src)
tonewerk_engine_options(mk8fx)

# --- pmtines: the Rhodes ---------------------------------------------------------------------------------------------------
add_library(tines STATIC
  tines/src/core/params.cpp
  tines/src/core/tine_designer.cpp
  tines/src/core/key_voice.cpp
  tines/src/core/engine.cpp
  adapters/tines_engine.cpp)
target_include_directories(tines PRIVATE tines/src PUBLIC adapters)
target_compile_definitions(tines PRIVATE pmp=pmp_tines)
tonewerk_engine_options(tines)

# --- pmreeds: Wurlitzer and Pianet -----------------------------------------------------------------------------------------
add_library(reeds STATIC
  reeds/src/core/params.cpp
  reeds/src/core/reed_designer.cpp
  reeds/src/core/key_voice.cpp
  reeds/src/core/engine.cpp
  adapters/reeds_engine.cpp)
target_include_directories(reeds PRIVATE reeds/src PUBLIC adapters)
target_compile_definitions(reeds PRIVATE pmp=pmp_reeds)
tonewerk_engine_options(reeds)

# --- synthkeys: the hybrid voices, their banks compiled in -----------------------------------------------------------------
# synthkeys embeds its banks as generated sources (30-110 MB of text each); ToneWerk keeps the images (synthkeys/banks) and
# generates the same sources at build time with a small host tool
add_executable(tonewerk_embed_bank tools/embed_bank.cpp)
set(SYNTHKEYS_BANKS splendid:Splendid clav:Clav upright:Upright salamander:Salamander)
set(SYNTHKEYS_BANK_SOURCES)
foreach(entry ${SYNTHKEYS_BANKS})
  string(REPLACE ":" ";" parts "${entry}")
  list(GET parts 0 file)
  list(GET parts 1 name)
  string(TOLOWER "${name}" lower)
  set(image "${CMAKE_CURRENT_SOURCE_DIR}/synthkeys/banks/${file}.bank")
  set(source "${CMAKE_CURRENT_BINARY_DIR}/synthkeys/hybrid_${lower}_bank.cpp")
  add_custom_command(OUTPUT "${source}"
    COMMAND tonewerk_embed_bank "${image}" "core/hybrid_${lower}_bank.h" "kHybrid${name}Bank" "${source}"
    DEPENDS tonewerk_embed_bank "${image}"
    COMMENT "Embedding the synthkeys bank ${file}.bank"
    VERBATIM)
  list(APPEND SYNTHKEYS_BANK_SOURCES "${source}")
endforeach()

# The bank arrays (up to 18 M elements) cost the compiler memory: 3.2 GB for the Salamander with clang. They compile one at a
# time (a Ninja job pool; other generators ignore it) and unoptimised: they only need to be laid out.
set_property(GLOBAL APPEND PROPERTY JOB_POOLS tonewerk_banks=1)
add_library(synthkeys_banks OBJECT ${SYNTHKEYS_BANK_SOURCES})
target_include_directories(synthkeys_banks PRIVATE synthkeys/src)
target_compile_definitions(synthkeys_banks PRIVATE pmp=pmp_synthkeys)
tonewerk_engine_options(synthkeys_banks)
set_target_properties(synthkeys_banks PROPERTIES JOB_POOL_COMPILE tonewerk_banks)
if(MSVC)
  target_compile_options(synthkeys_banks PRIVATE /Od)
else()
  target_compile_options(synthkeys_banks PRIVATE -O0)
endif()

add_library(synthkeys STATIC
  synthkeys/src/core/params.cpp
  synthkeys/src/core/engine.cpp
  synthkeys/src/core/hybrid_bank.cpp
  synthkeys/src/core/hybrid_player.cpp
  $<TARGET_OBJECTS:synthkeys_banks>
  adapters/synthkeys_engine.cpp)
target_include_directories(synthkeys PRIVATE synthkeys/src PUBLIC adapters)
target_compile_definitions(synthkeys PRIVATE pmp=pmp_synthkeys)
tonewerk_engine_options(synthkeys)

# --- the interface the plugin uses -------------------------------------------------------------------------------------------
add_library(soundengines STATIC adapters/sound_engine.cpp)
target_include_directories(soundengines PUBLIC adapters)
target_link_libraries(soundengines PUBLIC tines reeds synthkeys)
tonewerk_engine_options(soundengines)
