// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// hybrid_upright_bank.h - GENERATED FILE, DO NOT EDIT
//
// The embedded hybrid bank of Upright Piano KW: bank version 7,
// 22050 Hz, keys 21..108, 4730416 bytes. Built from Kawai upright, Gonzalo / Roberto, FreePats (CC0)
// by pmpiano's tools/analysis/buildbank.py, packed to 2 velocity anchors by tools/samples/pack_bank.py; embedded here by
//   python3 tools/samples/embed_bank.py out/bank/upright.bank
// (image CRC-32 350743e5). The array is 16-byte aligned, as HybridBank::attach requires.
#pragma once

#include <cstddef>
#include <cstdint>

namespace pmp {

inline constexpr size_t kHybridUprightBankSize = 4730416;
extern const uint8_t kHybridUprightBank[kHybridUprightBankSize];   // hybrid_upright_bank.cpp

} // namespace pmp
