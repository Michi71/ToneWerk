// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// hybrid_splendid_bank.h - GENERATED FILE, DO NOT EDIT
//
// The embedded hybrid bank of the splendid grand piano: bank version 7,
// 22050 Hz, keys 21..108, 5974304 bytes. Built from the public-domain AKAI
// Steinway recordings by pmpiano's tools/analysis/buildbank.py, packed to 3 velocity anchors by tools/samples/pack_bank.py; embedded here by
//   python3 tools/samples/embed_bank.py out/bank/splendid.bank
// (image CRC-32 b00748e0). The array is 16-byte aligned, as HybridBank::attach requires.
#pragma once

#include <cstddef>
#include <cstdint>

namespace pmp {

inline constexpr size_t kHybridSplendidBankSize = 5974304;
extern const uint8_t kHybridSplendidBank[kHybridSplendidBankSize];   // hybrid_splendid_bank.cpp

} // namespace pmp
