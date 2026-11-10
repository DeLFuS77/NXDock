// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "sound.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <malloc.h>

namespace nxdock {
namespace {

// 20 ms a buffer and three of them in turn: a sound starts about 40 ms after it is asked for.
constexpr int kFrames = 960;
constexpr int kBuffers = 3;
constexpr size_t kBytes = kFrames * 2 * sizeof(s16);
constexpr size_t kAligned = (kBytes + 0xFFF) & ~size_t(0xFFF);

} // namespace

Sound::~Sound() {
    Stop();
}

void Sound::Start() {
    if (m_started) {
        return;
    }
    if (R_FAILED(audoutInitialize())) {
        return;
    }
    if (R_FAILED(audoutStartAudioOut())) {
        audoutExit();
        return;
    }
    m_started = true;
    m_quit = false;
    m_thread = std::thread(&Sound::Run, this);
}

void Sound::Stop() {
    if (!m_started) {
        return;
    }
    m_quit = true;
    if (m_thread.joinable()) {
        m_thread.join();
    }
    audoutExit();
    m_started = false;
}

void Sound::Play(synth::Effect effect) {
    if (!m_started) {
        return;
    }
    std::lock_guard lock(m_mutex);
    if (m_effects.size() < 8) {
        m_effects.push_back(effect);
    }
}

void Sound::Music(bool on) {
    if (!m_started) {
        return;
    }
    std::lock_guard lock(m_mutex);
    m_music = on ? 1 : 0;
}

void Sound::Run() {
    AudioOutBuffer buffers[kBuffers]{};
    void* memory[kBuffers]{};
    for (int i = 0; i < kBuffers; i++) {
        memory[i] = memalign(0x1000, kAligned);
        if (!memory[i]) {
            for (int j = 0; j < i; j++) std::free(memory[j]);
            audoutStopAudioOut();
            return;
        }
        std::memset(memory[i], 0, kAligned);
        buffers[i].buffer = memory[i];
        buffers[i].buffer_size = kAligned;
        buffers[i].data_size = kBytes;
        buffers[i].data_offset = 0;
        audoutAppendAudioOutBuffer(&buffers[i]);
    }
    std::vector<float> mono(kFrames);
    while (!m_quit) {
        AudioOutBuffer* released = nullptr;
        u32 count = 0;
        if (R_FAILED(audoutWaitPlayFinish(&released, &count, 100'000'000ull)) || !released || !count) {
            continue;
        }
        {
            std::lock_guard lock(m_mutex);
            for (const auto e : m_effects) m_synth.Play(e);
            m_effects.clear();
            if (m_music == 1) m_synth.MusicStart();
            if (m_music == 0) m_synth.MusicStop();
            m_music = -1;
        }
        m_synth.Render(mono.data(), kFrames);
        s16* out = static_cast<s16*>(released->buffer);
        for (int i = 0; i < kFrames; i++) {
            const float v = std::clamp(mono[i], -1.f, 1.f) * 32767.f;
            out[2 * i] = out[2 * i + 1] = s16(std::lrint(v));
        }
        armDCacheFlush(released->buffer, kBytes);
        audoutAppendAudioOutBuffer(released);
    }
    audoutStopAudioOut();
    for (auto* m : memory) std::free(m);
}

} // namespace nxdock
