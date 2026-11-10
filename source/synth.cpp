// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "synth.hpp"

#include <algorithm>
#include <cmath>

namespace nxdock::synth {

constexpr float kMusicGain = 8.f;
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSilent = 0.0001; // where Web Audio's exponential ramps start and end

double Note(int midi) {
    return 440.0 * std::pow(2.0, (midi - 69) / 12.0);
}

int64_t Samples(double seconds) {
    return int64_t(std::llround(seconds * kRate));
}

// The music of the wizard, as in the library app (14-wizard.js): D major 9, B minor 9, G major 9,
// A 6 sus, four bars that turn round and round, at 72 beats a minute in 16 steps a bar.
constexpr int kChords[4][5] = {{50, 57, 61, 64, 66}, {47, 54, 57, 61, 62}, {43, 50, 54, 57, 59}, {45, 52, 59, 62, 66}};
// where the bass plays in a bar, and for how many steps
constexpr int kBass[4][2] = {{0, 5}, {6, 2}, {8, 5}, {14, 2}};
// the short chords, ahead of the beat
constexpr int kStabs[] = {3, 6, 10, 13};
// the very soft wooden rim
constexpr int kClicks[] = {0, 3, 6, 10, 12};
constexpr double kStep = 60.0 / 72.0 / 4.0;

template <size_t N>
bool In(const int (&list)[N], int value) {
    return std::find(std::begin(list), std::end(list), value) != std::end(list);
}

} // namespace

void Synth::Biquad::LowPass(double freq, double q_db) {
    // as Web Audio's BiquadFilterNode "lowpass": its Q is in decibels
    const double w0 = 2 * kPi * freq / kRate;
    const double alpha = std::sin(w0) / (2 * std::pow(10.0, q_db / 20));
    const double a0 = 1 + alpha, c = std::cos(w0);
    b0 = (1 - c) / 2 / a0;
    b1 = (1 - c) / a0;
    b2 = (1 - c) / 2 / a0;
    a1 = -2 * c / a0;
    a2 = (1 - alpha) / a0;
}

void Synth::Biquad::BandPass(double freq, double q) {
    // as Web Audio's "bandpass": the peak stays at 0 dB
    const double w0 = 2 * kPi * freq / kRate;
    const double alpha = std::sin(w0) / (2 * q);
    const double a0 = 1 + alpha, c = std::cos(w0);
    b0 = alpha / a0;
    b1 = 0;
    b2 = -alpha / a0;
    a1 = -2 * c / a0;
    a2 = (1 - alpha) / a0;
}

float Synth::Biquad::Run(float x) {
    const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1;
    x1 = x;
    y2 = y1;
    y1 = y;
    return float(y);
}

Synth::Synth() {
    m_soft.LowPass(2400, 1);
    // a tenth of a second of white noise, as the wizard makes for its click; always the same
    m_noise.resize(kRate / 10);
    unsigned seed = 0x2545F491u;
    for (auto& s : m_noise) {
        seed = seed * 1664525u + 1013904223u;
        s = float(seed >> 8) / float(1u << 24) * 2.f - 1.f;
    }
    m_voices.reserve(64);
}

void Synth::Add(double freq, double start, double length, Wave wave, double volume, double attack, Bus bus,
                double freq_end, double band) {
    Voice v;
    v.wave = wave;
    v.bus = bus;
    v.freq = freq;
    v.freq_end = freq_end;
    v.volume = volume;
    v.start = Samples(start);
    v.attack = std::max<int64_t>(0, Samples(attack));
    v.end = std::max(v.start + v.attack + 1, Samples(start + length));
    // exponential ramps as Web Audio's: from silent up to the volume, then down to silent
    v.gain = v.attack > 0 ? kSilent : volume;
    v.rise = v.attack > 0 ? std::pow(volume / kSilent, 1.0 / double(v.attack)) : 1;
    v.fall = std::pow(kSilent / volume, 1.0 / double(v.end - v.start - v.attack));
    v.glide = freq_end > 0 ? std::pow(freq_end / freq, 1.0 / double(v.end - v.start)) : 1;
    if (wave == Wave::Noise) {
        v.filter.BandPass(band, band > 2000 ? 2 : 1);
    }
    m_voices.push_back(v);
}

void Synth::Click(double start) {
    // the wooden click: a very short burst of noise through a narrow band at 1900 Hz
    Add(0, start, 0.045, Wave::Noise, 0.02, 0, Bus::MusicDry, 0, 1900);
}

void Synth::Play(Effect effect) {
    const double t = double(m_now) / kRate + 0.005;
    const auto wood = [&](double f, double at, double length, double volume) {
        // a soft mallet: the note and a quiet, short overtone near four times above, as a marimba
        Add(f, t + at, length, Wave::Sine, volume, 0.003, Bus::Fx);
        Add(f * 3.93, t + at, length * 0.3, Wave::Sine, volume * 0.16, 0.002, Bus::Fx);
    };
    const auto bubble = [&](double f0, double f1, double at, double length, double volume) {
        Add(f0, t + at, length, Wave::Sine, volume, 0.005, Bus::Fx, f1);
    };
    const auto paper = [&](double band, double at, double volume) {
        Add(0, t + at, 0.03, Wave::Noise, volume, 0, Bus::Fx, 0, band);
    };
    switch (m_set) {
        case Set::Wood:
            switch (effect) {
                case Effect::Move: wood(1046.50, 0, 0.07, 0.035); break;               // C6
                case Effect::Select: wood(659.26, 0, 0.14, 0.055); wood(987.77, 0.055, 0.18, 0.05); break; // E5 B5
                case Effect::Back: wood(987.77, 0, 0.14, 0.045); wood(659.26, 0.055, 0.18, 0.045); break;
                case Effect::Finish: {
                    // D major going up, low and warm: D4 F#4 A4 D5, the last one longer
                    const double notes[] = {293.66, 369.99, 440.00, 587.33};
                    for (int i = 0; i < 4; i++) wood(notes[i], i * 0.09, i == 3 ? 0.9 : 0.5, 0.06);
                    break;
                }
            }
            break;
        case Set::Bubble:
            switch (effect) {
                case Effect::Move: bubble(600, 900, 0, 0.06, 0.04); break;
                case Effect::Select: bubble(500, 1000, 0, 0.12, 0.055); break;
                case Effect::Back: bubble(900, 450, 0, 0.12, 0.05); break;
                case Effect::Finish:
                    bubble(400, 800, 0, 0.25, 0.05);
                    bubble(500, 1000, 0.12, 0.25, 0.05);
                    bubble(600, 1200, 0.24, 0.4, 0.05);
                    break;
            }
            break;
        case Set::Paper:
            switch (effect) {
                case Effect::Move: paper(2500, 0, 0.05); break;
                case Effect::Select: paper(1800, 0, 0.07); paper(2600, 0.05, 0.06); break;
                case Effect::Back: paper(2600, 0, 0.06); paper(1800, 0.05, 0.06); break;
                case Effect::Finish:
                    for (int i = 0; i < 4; i++) paper(1600 + i * 400, i * 0.08, 0.06);
                    Add(293.66, t + 0.3, 0.6, Wave::Sine, 0.04, 0.01, Bus::Fx);
                    break;
            }
            break;
    }
}

void Synth::MusicStart() {
    if (m_music && !m_stopping) {
        return;
    }
    if (!m_music) {
        m_out = kSilent;
        m_next_step = m_now + Samples(0.3);
        m_count = 0;
    }
    m_music = true;
    m_stopping = false;
    // rises over five seconds
    m_out_target = 0.7;
    m_out_ramp_end = m_now + Samples(5);
    m_out_step = std::pow(m_out_target / std::max(m_out, kSilent), 1.0 / double(m_out_ramp_end - m_now));
}

void Synth::MusicStop() {
    if (!m_music || m_stopping) {
        return;
    }
    // fades out in 1.2 s, then is gone
    m_stopping = true;
    m_out_target = kSilent;
    m_out_ramp_end = m_now + Samples(1.2);
    m_out_step = std::pow(kSilent / std::max(m_out, kSilent), 1.0 / double(m_out_ramp_end - m_now));
}

void Synth::Schedule(int64_t until) {
    if (!m_music || m_stopping) {
        return;
    }
    while (m_next_step < until) {
        const double at = double(m_next_step) / kRate;
        const int* chord = kChords[(m_count / 16) % 4];
        const int step = m_count % 16;
        for (const auto& b : kBass) {
            if (b[0] == step) {
                Add(Note(chord[0] - 12), at, std::max(b[1] * kStep, 0.5) + 0.3, Wave::Sine, 0.05, 0.04, Bus::Music);
            }
        }
        if (In(kStabs, step)) {
            for (int i = 1; i < 5; i++) {
                Add(Note(chord[i]), at, 0.55, Wave::Triangle, 0.02, 0.02, Bus::Music);
            }
        }
        if (In(kClicks, step)) {
            Click(at);
        }
        m_next_step += Samples(kStep);
        m_count++;
    }
}

void Synth::Render(float* out, int frames) {
    Schedule(m_now + frames);
    for (int i = 0; i < frames; i++) {
        const int64_t now = m_now + i;
        float fx = 0, wet = 0, dry = 0;
        for (auto& v : m_voices) {
            if (now < v.start || now >= v.end) {
                continue;
            }
            float s;
            if (v.wave == Wave::Noise) {
                s = v.filter.Run(m_noise[v.noise++ % m_noise.size()]);
            } else {
                const double p = v.phase - std::floor(v.phase);
                s = v.wave == Wave::Sine ? float(std::sin(2 * kPi * p)) : float(p < 0.5 ? 4 * p - 1 : 3 - 4 * p);
                v.phase += v.freq / kRate;
                v.freq *= v.glide;
            }
            s *= float(v.gain);
            v.gain *= (now - v.start < v.attack) ? v.rise : v.fall;
            switch (v.bus) {
                case Bus::Fx: fx += s; break;
                case Bus::Music: wet += s; break;
                case Bus::MusicDry: dry += s; break;
            }
        }
        float music = 0;
        if (m_music) {
            if (now < m_out_ramp_end) {
                m_out *= m_out_step;
            }
            // as the wizard of the library app makes it, it is too quiet for the console's speakers
            // (peaks at -24 dBFS): raised to peaks of about -6 dBFS (24A)
            music = (m_soft.Run(wet) + dry) * float(m_out) * kMusicGain;
        }
        float v = fx + music;
        // a soft limit, never a hard clip: above 0.8 it bends gently towards 1
        if (std::fabs(v) > 0.8f) v = std::copysign(0.8f + 0.2f * std::tanh((std::fabs(v) - 0.8f) / 0.2f), v);
        out[i] = v;
    }
    m_now += frames;
    // what has ended goes; the music goes whole once faded
    if (m_music && m_stopping && m_now >= m_out_ramp_end) {
        m_music = m_stopping = false;
        m_voices.erase(std::remove_if(m_voices.begin(), m_voices.end(), [](const Voice& v) { return v.bus != Bus::Fx; }),
                       m_voices.end());
    }
    m_voices.erase(std::remove_if(m_voices.begin(), m_voices.end(), [&](const Voice& v) { return v.end <= m_now; }),
                   m_voices.end());
}

} // namespace nxdock::synth
