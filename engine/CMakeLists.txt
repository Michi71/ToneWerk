# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Michi71
# The pmpiano engine core (engine/UPSTREAM.md): no dependencies, no fast math (key_voice.cpp refuses it).
option(TONEWERK_FLOAT "Single-precision real-time path of the engine (PMP_FLOAT)" OFF)

add_library(pmpcore STATIC
  src/core/params.cpp
  src/core/tine_designer.cpp
  src/core/key_voice.cpp
  src/core/engine.cpp
  src/core/mk8_fx.cpp
  src/core/piano_hybrid.cpp)
target_include_directories(pmpcore PUBLIC src)
target_compile_features(pmpcore PUBLIC cxx_std_17)
# SYSTEM: the plugin builds with JUCE's recommended warnings (-Wsign-conversion, -Wfloat-equal, ...), which the engine's
# headers are not written for; pmpiano builds them with -Wall -Wextra -Wpedantic
set_target_properties(pmpcore PROPERTIES POSITION_INDEPENDENT_CODE ON SYSTEM ON)
if(TONEWERK_FLOAT)
  target_compile_definitions(pmpcore PUBLIC PMP_FLOAT)
endif()
if(WIN32)
  target_compile_definitions(pmpcore PRIVATE _CRT_SECURE_NO_WARNINGS)   # fopen, getenv
endif()
if(MSVC)
  target_compile_options(pmpcore PRIVATE /fp:precise)
else()
  target_compile_options(pmpcore PRIVATE -fno-fast-math $<$<NOT:$<CONFIG:Debug>>:-O2>)
  # x86-64 without AVX: the four-lane double vectors of key_voice.cpp are passed between inline functions with internal
  # linkage only, so the AVX calling convention note does not apply
  target_compile_options(pmpcore PRIVATE $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-Wno-psabi>)
endif()
