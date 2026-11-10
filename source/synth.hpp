// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include <cstdint>
#include <vector>

namespace nxdock::synth {

// Everything NXDock plays is made here while it plays: no recorded sound is shipped. The voices
// follow the Web Audio nodes of the library app's wizard (an oscillator through a gain that
// rises and falls exponentially, a low-pass of 2400 Hz, a click of filtered noise), so the
// music sounds as it does in the browser. Plain C++, so it is also rendered and checked on the
// computer (tests/sound_test.cpp writes the WAV files of the listening test).

constexpr int kRate = 48000;

enum class Effect { Move, Select, Back, Finish };

// The sets of interface sounds of the listening test; Wood is the one chosen.
enum class Set { Wood, Bubble, Paper };

class Synth {
public:
    Synth();

    // Mono samples, about -1..1, `frames` of them, going on from the last call.
    void Render(float* out, int frames);

    void Play(Effect effect);
    // The wizard's music: a slow bossa that rises in 5 s and fades in 1.2 s when stopped.
    void MusicStart();
    void MusicStop();
    bool MusicPlaying() const { return m_music; }
    void SetSet(Set set) { m_set = set; }

private:
    enum class Wave { Sine, Triangle, Noise };
    enum class Bus { Fx, Music, MusicDry };
    struct Biquad {
        double b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        void LowPass(double freq, double q_db);
        void BandPass(double freq, double q);
        float Run(float x);
    };
    struct Voice {
        Wave wave = Wave::Sine;
        Bus bus = Bus::Fx;
        double freq = 440, freq_end = 0; // freq_end: a glide to it over the attack and decay, 0 none
        double phase = 0;
        int64_t start = 0, attack = 1, end = 1; // in samples, from the start of the synth
        double volume = 0, gain = 0, rise = 1, fall = 1, glide = 1;
        unsigned noise = 0; // where in the noise it reads
        Biquad filter;      // noise only: its band
    };

    // A voice as voice() of the wizard: frequency, start and length in seconds, wave, volume,
    // attack in seconds.
    void Add(double freq, double start, double length, Wave wave, double volume, double attack, Bus bus,
             double freq_end = 0, double band = 0);
    void Click(double start);
    void Schedule(int64_t until);

    std::vector<Voice> m_voices;
    std::vector<float> m_noise;
    int64_t m_now = 0; // samples rendered
    Set m_set = Set::Wood;
    Biquad m_soft; // the music's low-pass

    // the music
    bool m_music = false, m_stopping = false;
    double m_out = 0, m_out_target = 0, m_out_step = 1; // its gain, ramping exponentially
    int64_t m_out_ramp_end = 0;
    int64_t m_next_step = 0;
    int m_count = 0;
};

} // namespace nxdock::synth
