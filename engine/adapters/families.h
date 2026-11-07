// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// families.h - the entry points of the three engine libraries (each in its own renamed namespace, engine/CMakeLists.txt)
#pragma once

#include "sound_engine.h"

namespace tw::engines
{
std::unique_ptr<SoundEngine> createTinesEngine (const Setup& setup, std::string* error);
int tinesPresetCount();
std::string tinesPresetId (int index);

std::unique_ptr<SoundEngine> createReedsEngine (const Setup& setup, std::string* error);
int reedsPresetCount();
std::string reedsPresetId (int index);

std::unique_ptr<SoundEngine> createSynthKeysEngine (const Setup& setup, std::string* error);
int synthKeysPresetCount();
std::string synthKeysPresetId (int index);
} // namespace tw::engines
