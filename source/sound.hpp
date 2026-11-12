// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include <switch.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include "synth.hpp"

namespace nxdock {

// The sound of NXDock: the synth on a thread of its own, out through audout at 48 kHz.
// Without audout (or in an emulator without sound) everything here is quietly nothing.
class Sound {
public:
    ~Sound();
    void Start();
    void Stop();

    void Play(synth::Effect effect);
    void Music(bool on);

private:
    void Run();

    std::thread m_thread;
    std::atomic<bool> m_quit{false};
    bool m_started = false;
    // audout is opened on the sound's thread: it took a good part of the boot on the drawing one
    std::atomic<bool> m_audio_ok{false};
    std::mutex m_mutex;
    // asked from the interface, done on the sound's thread
    std::vector<synth::Effect> m_effects;
    int m_music = -1; // -1 nothing asked, 0 stop, 1 start
    synth::Synth m_synth;
};

} // namespace nxdock
