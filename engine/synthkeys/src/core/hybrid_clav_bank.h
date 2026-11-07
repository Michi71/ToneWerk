// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// hybrid_clav_bank.h - GENERATED FILE, DO NOT EDIT
//
// The embedded hybrid bank of Hohner Clavinet D6: bank version 7,
// 44100 Hz, keys 28..100, 10368464 bytes. Built from Clavinet_Lit.sf2 Realistic Clavinet (Lithalean/Mike77154, musical-artifacts)
// by pmpiano's tools/analysis/buildbank.py, packed to 4 velocity anchors by tools/samples/pack_bank.py; embedded here by
//   python3 tools/samples/embed_bank.py out/bank/clav.bank
// (image CRC-32 2ef67e42). The array is 16-byte aligned, as HybridBank::attach requires.
#pragma once

#include <cstddef>
#include <cstdint>

namespace pmp {

inline constexpr size_t kHybridClavBankSize = 10368464;
extern const uint8_t kHybridClavBank[kHybridClavBankSize];   // hybrid_clav_bank.cpp

} // namespace pmp
