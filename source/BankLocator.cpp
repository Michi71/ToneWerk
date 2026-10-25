// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
#include "BankLocator.h"

#include <juce_data_structures/juce_data_structures.h>

#include <iterator>

namespace tw
{
namespace
{
    const char* const kBankFiles[] = { "piano_hybrid.bank", "mark1_hybrid.bank", "mark2_hybrid.bank", "wurli_hybrid.bank",
        "clav_hybrid.bank", "pianet_hybrid.bank", "steinway_hybrid.bank", "grandpas_hybrid.bank", "cp80_hybrid.bank" };
    constexpr const char* kBankFolderKey = "bankFolder";

    juce::CriticalSection& settingsLock()
    {
        static juce::CriticalSection lock;
        return lock;
    }

    juce::PropertiesFile::Options settingsOptions()
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "ToneWerk";
        o.filenameSuffix = ".settings";
        o.folderName = "ToneWerk";
        o.osxLibrarySubFolder = "Application Support";
        o.processLock = nullptr;
        return o;
    }

    // ~/Library/Application Support (macOS), %APPDATA% (Windows), ~/.config (Linux)
    juce::File userDataRoot()
    {
#if JUCE_MAC
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Application Support");
#else
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
#endif
    }

    // /Library/Application Support (macOS), C:\ProgramData (Windows), /usr/local/share (Linux)
    juce::File commonDataRoot()
    {
#if JUCE_MAC
        return juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory).getChildFile ("Application Support");
#elif JUCE_LINUX || JUCE_BSD
        return juce::File ("/usr/local/share");
#else
        return juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory);
#endif
    }

    // banks/ inside the bundle of the plugin or app (VST3 on every platform, AU, the macOS app: Contents/Resources) or next to
    // the binary (the standalone app on Windows and Linux)
    void addBinaryFolders (juce::Array<juce::File>& out)
    {
        const auto binary = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
        const auto dir = binary.getParentDirectory(); // Contents/MacOS, Contents/x86_64-win, Contents/x86_64-linux, or the exe folder
        out.add (dir.getParentDirectory().getChildFile ("Resources").getChildFile ("banks"));
        out.add (dir.getChildFile ("banks"));
    }
} // namespace

juce::String bankFileName (int slot)
{
    return kBankFiles[juce::jlimit (0, int (std::size (kBankFiles)) - 1, slot)];
}

juce::File defaultUserBankFolder()
{
    return userDataRoot().getChildFile ("ToneWerk").getChildFile ("banks");
}

juce::File userBankFolder()
{
    const juce::ScopedLock sl (settingsLock());
    juce::PropertiesFile settings (settingsOptions());
    const auto path = settings.getValue (kBankFolderKey);
    return juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
}

void setUserBankFolder (const juce::File& folder)
{
    const juce::ScopedLock sl (settingsLock());
    juce::PropertiesFile settings (settingsOptions());
    if (folder == juce::File())
        settings.removeValue (kBankFolderKey);
    else
        settings.setValue (kBankFolderKey, folder.getFullPathName());
    settings.saveIfNeeded();
}

juce::Array<juce::File> bankSearchFolders()
{
    juce::Array<juce::File> folders;
    if (const auto chosen = userBankFolder(); chosen != juce::File())
        folders.add (chosen);
    if (const auto env = juce::SystemStats::getEnvironmentVariable ("TONEWERK_BANKS", {}); juce::File::isAbsolutePath (env))
        folders.add (juce::File (env));
    addBinaryFolders (folders);
    folders.add (defaultUserBankFolder());
    folders.add (commonDataRoot().getChildFile ("ToneWerk").getChildFile ("banks"));
#ifdef TONEWERK_DEV_BANKS_DIR
    folders.add (juce::File (TONEWERK_DEV_BANKS_DIR));
#endif
    return folders;
}

juce::File findBank (int slot)
{
    const auto name = bankFileName (slot);
    for (const auto& folder : bankSearchFolders())
    {
        const auto f = folder.getChildFile (name);
        // a Git LFS pointer (a checkout without LFS) is a text file of about 130 bytes
        if (f.existsAsFile() && f.getSize() > 4096)
            return f;
    }
    return {};
}
} // namespace tw
