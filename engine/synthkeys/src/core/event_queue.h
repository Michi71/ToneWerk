// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Michi71
// event_queue.h - lock-free single-producer/single-consumer queue for MIDI-like events
//
// The MIDI/UI thread pushes, the audio thread pops before calling Engine::process().
#pragma once

#include <atomic>
#include <cstdint>

namespace pmp {

struct Event {
    enum Type : uint8_t { Midi, Volume };
    Type    type = Midi;
    uint8_t status = 0, data1 = 0, data2 = 0;
    float   value = 0.0f;
};

template <int Capacity = 1024>
class EventQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "capacity must be a power of two");

public:
    bool push(const Event& e) {  // producer thread
        const uint32_t w = write_.load(std::memory_order_relaxed);
        if (w - read_.load(std::memory_order_acquire) >= Capacity) return false;  // full
        buf_[w & (Capacity - 1)] = e;
        write_.store(w + 1, std::memory_order_release);
        return true;
    }
    bool pop(Event& e) {         // consumer thread
        const uint32_t r = read_.load(std::memory_order_relaxed);
        if (r == write_.load(std::memory_order_acquire)) return false;           // empty
        e = buf_[r & (Capacity - 1)];
        read_.store(r + 1, std::memory_order_release);
        return true;
    }

private:
    Event buf_[Capacity];
    alignas(64) std::atomic<uint32_t> write_{0};
    alignas(64) std::atomic<uint32_t> read_{0};
};

} // namespace pmp
