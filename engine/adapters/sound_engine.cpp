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
        case Family::Hybrid:
            return createHybridEngine (setup, error);
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
        case Family::Hybrid:
            return hybridPresetCount();
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
        case Family::Hybrid:
            return hybridPresetId (index);
    }
    return {};
}
} // namespace tw::engines
