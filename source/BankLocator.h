// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// BankLocator.h - where the hybrid banks of the engine are found (docs/BANKS.md)
#pragma once

#include <juce_core/juce_core.h>

namespace tw
{
// file name of the bank in an engine slot (pmp::Engine::kHybridSlots): 0 piano_hybrid.bank, 1 mark1_hybrid.bank, ...
juce::String bankFileName (int slot);

// The folders searched for a bank, in this order: the folder chosen in the plugin (settings), $TONEWERK_BANKS, banks/ in the
// plugin bundle's Resources or next to the executable, the user's and the common application data folder (ToneWerk/banks),
// the source tree of a development build.
juce::Array<juce::File> bankSearchFolders();

// the bank of a slot, or an invalid File when no folder has it
juce::File findBank (int slot);

// the folder chosen by the user (kept in the settings file, shared by all plugin instances); File() clears it
juce::File userBankFolder();
void setUserBankFolder (const juce::File& folder);

// where the user's application data folder for banks is (shown in the UI as the place to put banks)
juce::File defaultUserBankFolder();
} // namespace tw
