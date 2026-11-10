// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// Renders the sounds of NXDock with the same synth the console runs, checks them, and writes them
// as WAV files when a folder is given (for the listening test): sound_test [folder]
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "synth.hpp"

using namespace nxdock::synth;

namespace {

int g_failed = 0;

void Check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        g_failed++;
    }
}

void WriteWav(const std::string& path, const std::vector<float>& samples) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        Check(false, path.c_str());
        return;
    }
    const unsigned data = unsigned(samples.size() * 2), rate = kRate, byte_rate = kRate * 2;
    const unsigned riff = 36 + data, fmt = 16;
    const unsigned short pcm = 1, channels = 1, align = 2, bits = 16;
    std::fwrite("RIFF", 1, 4, f);
    std::fwrite(&riff, 4, 1, f);
    std::fwrite("WAVEfmt ", 1, 8, f);
    std::fwrite(&fmt, 4, 1, f);
    std::fwrite(&pcm, 2, 1, f);
    std::fwrite(&channels, 2, 1, f);
    std::fwrite(&rate, 4, 1, f);
    std::fwrite(&byte_rate, 4, 1, f);
    std::fwrite(&align, 2, 1, f);
    std::fwrite(&bits, 2, 1, f);
    std::fwrite("data", 1, 4, f);
    std::fwrite(&data, 4, 1, f);
    for (float s : samples) {
        const float c = s > 1 ? 1 : s < -1 ? -1 : s;
        const short v = short(std::lrint(c * 32767));
        std::fwrite(&v, 2, 1, f);
    }
    std::fclose(f);
}

// Renders in the blocks the console uses.
std::vector<float> Render(Synth& synth, double seconds) {
    std::vector<float> out(size_t(seconds * kRate));
    for (size_t at = 0; at < out.size(); at += 960) {
        synth.Render(out.data() + at, int(std::min<size_t>(960, out.size() - at)));
    }
    return out;
}

float Peak(const std::vector<float>& s) {
    float p = 0;
    for (float v : s) p = std::fmax(p, std::fabs(v));
    return p;
}

} // namespace

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "";
    const char* sets[] = {"wood", "bubble", "paper"};
    const char* effects[] = {"move", "select", "back", "finish"};
    for (int s = 0; s < 3; s++) {
        for (int e = 0; e < 4; e++) {
            Synth synth;
            synth.SetSet(Set(s));
            synth.Play(Effect(e));
            const auto out = Render(synth, e == 3 ? 1.6 : 0.5);
            const float peak = Peak(out);
            Check(peak > 0.005f && peak < 0.5f, (std::string(sets[s]) + " " + effects[e] + " is soft and heard").c_str());
            // it ends: the last tenth of a second is silent
            float tail = 0;
            for (size_t i = out.size() - kRate / 10; i < out.size(); i++) tail = std::fmax(tail, std::fabs(out[i]));
            Check(tail < 0.001f, (std::string(sets[s]) + " " + effects[e] + " ends").c_str());
            if (!dir.empty()) WriteWav(dir + "/" + sets[s] + "-" + effects[e] + ".wav", out);
        }
    }
    // the music: it rises, plays its four bars, and fades away once stopped
    {
        Synth synth;
        synth.MusicStart();
        auto out = Render(synth, 14.5);
        Check(synth.MusicPlaying(), "the music plays");
        synth.MusicStop();
        const auto end = Render(synth, 2.5);
        Check(!synth.MusicPlaying(), "the music stops after its fade");
        out.insert(out.end(), end.begin(), end.end());
        const float peak = Peak(out);
        Check(peak > 0.02f && peak < 0.6f, "the music is soft");
        float first = 0;
        for (size_t i = 0; i < size_t(kRate) * 1; i++) first = std::fmax(first, std::fabs(out[i]));
        Check(first < peak * 0.2f, "the music rises slowly");
        float tail = 0;
        for (size_t i = out.size() - kRate / 2; i < out.size(); i++) tail = std::fmax(tail, std::fabs(out[i]));
        Check(tail < 0.0005f, "the music is silent after the fade");
        std::printf("music peak %.3f\n", peak);
        if (!dir.empty()) WriteWav(dir + "/music.wav", out);
    }
    // the same every time
    {
        Synth a, b;
        a.MusicStart();
        b.MusicStart();
        Check(Render(a, 3) == Render(b, 3), "the music is the same every time");
    }
    if (g_failed) {
        std::printf("%d failed\n", g_failed);
        return 1;
    }
    std::printf("sound: all passed\n");
    return 0;
}
