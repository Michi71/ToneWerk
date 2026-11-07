// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
#include "families.h"

namespace tw::engines
{
std::unique_ptr<SoundEngine> createEngine (Family family, const Setup& setup, std::string* error)
{
    switch (family)
    {
        case Family::Tines:
            return createTinesEngine (setup, error);
        case Family::Reeds:
            return createReedsEngine (setup, error);
        case Family::SynthKeys:
            return createSynthKeysEngine (setup, error);
    }
    return nullptr;
}

int presetCount (Family family)
{
    switch (family)
    {
        case Family::Tines:
            return tinesPresetCount();
        case Family::Reeds:
            return reedsPresetCount();
        case Family::SynthKeys:
            return synthKeysPresetCount();
    }
    return 0;
}

std::string presetId (Family family, int index)
{
    switch (family)
    {
        case Family::Tines:
            return tinesPresetId (index);
        case Family::Reeds:
            return reedsPresetId (index);
        case Family::SynthKeys:
            return synthKeysPresetId (index);
    }
    return {};
}
} // namespace tw::engines
