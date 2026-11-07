// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
#include "MidiLearn.h"

namespace tw
{
namespace
{
    const juce::Identifier ccProperty ("cc");
    const juce::Identifier parameterProperty ("param");
} // namespace

MidiLearn::MidiLearn (juce::AudioProcessorValueTreeState& state) : state_ (state)
{
    for (auto& v : values_)
        v.store (-1);
    state_.state.addListener (this);
    rebuild();
}

MidiLearn::~MidiLearn() { state_.state.removeListener (this); }

juce::ValueTree MidiLearn::tree (bool create)
{
    auto t = state_.state.getChildWithName (kTreeType);
    if (!t.isValid() && create)
    {
        t = juce::ValueTree (kTreeType);
        state_.state.appendChild (t, nullptr);
    }
    return t;
}

// the lookup tables from the tree; any thread that changes the tree (no audio thread does)
void MidiLearn::rebuild()
{
    std::array<juce::RangedAudioParameter*, kControllers> next {};
    const auto t = state_.state.getChildWithName (kTreeType);
    for (const auto& m : t)
    {
        const int cc = m.getProperty (ccProperty, -1);
        if (m.hasType (kMapType) && learnable (cc))
            next[size_t (cc)] = state_.getParameter (m.getProperty (parameterProperty).toString());
    }
    for (size_t cc = 0; cc < next.size(); ++cc)
        parameters_[cc].store (next[cc]);
    sendChangeMessage();
}

void MidiLearn::valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier&)
{
    if (t.hasType (kMapType) || t.hasType (kTreeType))
        rebuild();
}

void MidiLearn::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child)
{
    if (child.hasType (kMapType) || child.hasType (kTreeType))
        rebuild();
}

void MidiLearn::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int)
{
    if (child.hasType (kMapType) || child.hasType (kTreeType))
        rebuild();
}

// --- message thread -------------------------------------------------------------------------------------------------------

void MidiLearn::beginLearn (const juce::String& parameterId)
{
    learnedCc_.store (-1);
    learnId_ = parameterId;
    learningActive_.store (parameterId.isNotEmpty());
    sendChangeMessage();
}

void MidiLearn::cancelLearn()
{
    learningActive_.store (false);
    learnedCc_.store (-1);
    learnId_ = {};
    sendChangeMessage();
}

void MidiLearn::assign (int cc, const juce::String& parameterId)
{
    if (!learnable (cc) || state_.getParameter (parameterId) == nullptr)
        return;
    auto t = tree (true);
    for (int i = t.getNumChildren(); --i >= 0;)
    {
        const auto m = t.getChild (i);
        if (int (m.getProperty (ccProperty, -1)) == cc || m.getProperty (parameterProperty).toString() == parameterId)
            t.removeChild (i, nullptr);
    }
    t.appendChild (juce::ValueTree (kMapType, { { ccProperty, cc }, { parameterProperty, parameterId } }), nullptr);
}

void MidiLearn::forget (const juce::String& parameterId)
{
    auto t = tree (false);
    for (int i = t.getNumChildren(); --i >= 0;)
        if (t.getChild (i).getProperty (parameterProperty).toString() == parameterId)
            t.removeChild (i, nullptr);
}

void MidiLearn::clear()
{
    if (auto t = tree (false); t.isValid())
        t.removeAllChildren (nullptr);
}

int MidiLearn::ccFor (const juce::String& parameterId) const
{
    auto* p = state_.getParameter (parameterId);
    if (p == nullptr)
        return -1;
    for (int cc = 0; cc < kControllers; ++cc)
        if (parameters_[size_t (cc)].load() == p)
            return cc;
    return -1;
}

juce::String MidiLearn::parameterFor (int cc) const
{
    if (cc < 0 || cc >= kControllers)
        return {};
    auto* p = parameters_[size_t (cc)].load();
    return p != nullptr ? p->getParameterID() : juce::String();
}

int MidiLearn::size() const
{
    int n = 0;
    for (const auto& p : parameters_)
        n += p.load() != nullptr ? 1 : 0;
    return n;
}

void MidiLearn::dispatch()
{
    if (learningActive_.load())
        if (const int cc = learnedCc_.exchange (-1); cc >= 0)
        {
            const auto id = learnId_;
            cancelLearn();
            assign (cc, id);
        }
    if (!anyValue_.exchange (false))
        return;
    for (size_t cc = 0; cc < values_.size(); ++cc)
        if (const int v = values_[cc].exchange (-1); v >= 0)
            if (auto* p = parameters_[cc].load())
                p->setValueNotifyingHost (float (v) / 127.0f);
}

// --- audio thread ---------------------------------------------------------------------------------------------------------

bool MidiLearn::handleController (int cc, int value) noexcept
{
    if (!learnable (cc))
        return false;
    if (learningActive_.load())
    {
        int none = -1;
        learnedCc_.compare_exchange_strong (none, cc); // the first CC wins
        return true;
    }
    if (parameters_[size_t (cc)].load() == nullptr)
        return false;
    values_[size_t (cc)].store (juce::jlimit (0, 127, value));
    anyValue_.store (true);
    return true;
}
} // namespace tw
