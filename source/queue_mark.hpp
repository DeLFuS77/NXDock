// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// What the queue holds of a game of the library, for the mark on its cover (2A): installing,
// waiting, or nothing; and its place in the queue (1 is the one installing).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nxdock {

struct QueueMark {
    enum class State { None, Waiting, Installing };
    State state = State::None;
    int place = 0; // among the jobs installing and waiting, in their order; 0 when not there
};

// The game a title belongs to: an update or a DLC to its game.
inline uint64_t QueueGameOf(uint64_t tid) {
    if ((tid & 0xFFF) == 0x800) return tid ^ 0x800;
    if ((tid & 0xFFF) != 0) return (tid - 0x1000) & ~0xFFFull;
    return tid;
}

// A job is of a game when its title belongs to the game, or, without a title, when it was queued
// from the game (its colour key is the key of the game). The one installing wins over one waiting.
template <class Job>
QueueMark QueueMarkOf(const std::vector<Job>& jobs, uint64_t title_id, const std::string& key) {
    QueueMark out;
    // the one installing is first, then those waiting in the order they were added
    int place = 0;
    for (const auto& j : jobs) place += j.state == Job::State::Running;
    for (const auto& j : jobs) {
        const bool running = j.state == Job::State::Running, waiting = j.state == Job::State::Waiting;
        if (!running && !waiting) continue;
        if (waiting) place++;
        const bool same = (j.title_id != 0 && title_id != 0) ? QueueGameOf(j.title_id) == QueueGameOf(title_id)
                                                             : (!key.empty() && j.color_key == key);
        if (!same) continue;
        if (running) {
            out.state = QueueMark::State::Installing;
            out.place = 1;
        } else if (out.state == QueueMark::State::None) {
            out.state = QueueMark::State::Waiting;
            out.place = place;
        }
    }
    return out;
}

} // namespace nxdock
