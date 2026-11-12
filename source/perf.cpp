// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "perf.hpp"

#ifdef NXDOCK_PERF

#include <malloc.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace nxdock::perf {
namespace {

const u64 g_start = armGetSystemTick();
std::FILE* g_log = nullptr;
std::mutex g_mutex;

std::FILE* Log() {
    if (!g_log) {
        g_log = std::fopen("sdmc:/switch/NXDock/perf.log", "a");
        if (g_log) std::fprintf(g_log, "\n=== NXDock perf, " __DATE__ " " __TIME__ " ===\n");
    }
    return g_log;
}

// one section of the script (or of the time without a script): every frame of it
struct Section {
    std::string label = "free";
    std::vector<float> period, stage[kStages];
    int uploads = 0;
    u64 frames = 0;
};
Section g_sec;
u64 g_frame_at = 0, g_lap_at = 0;
bool g_first_frame = true;

struct Sum {
    int n = 0;
    double total = 0, max = 0;
};
std::map<std::string, Sum> g_took;

float Pct(std::vector<float> v, float p) {
    if (v.empty()) return 0;
    const size_t i = std::min(v.size() - 1, size_t(p * float(v.size() - 1) + 0.5f));
    std::nth_element(v.begin(), v.begin() + long(i), v.end());
    return v[i];
}

float Avg(const std::vector<float>& v) {
    double s = 0;
    for (float x : v) s += x;
    return v.empty() ? 0.f : float(s / double(v.size()));
}

void Flush() {
    std::FILE* f = Log();
    if (!f || g_sec.frames < 2) return;
    const auto& p = g_sec.period;
    int slow = 0;
    for (float x : p) slow += x > 18.f;
    u64 used = 0;
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    const struct mallinfo mi = mallinfo();
    std::fprintf(f, "[%s] frames %llu  period avg %.2f p50 %.2f p95 %.2f p99 %.2f max %.2f ms  slow(>18ms) %d (%.1f%%)\n",
                 g_sec.label.c_str(), (unsigned long long)g_sec.frames, Avg(p), Pct(p, .5f), Pct(p, .95f), Pct(p, .99f),
                 p.empty() ? 0.f : *std::max_element(p.begin(), p.end()), slow, p.empty() ? 0.0 : 100.0 * slow / double(p.size()));
    static const char* names[kStages] = {"update", "acquire", "upload", "draw", "present"};
    std::fprintf(f, "   cpu ms (avg/p95/max):");
    for (int i = 0; i < kStages; i++) {
        const auto& s = g_sec.stage[i];
        std::fprintf(f, " %s %.2f/%.2f/%.2f", names[i], Avg(s), Pct(s, .95f), s.empty() ? 0.f : *std::max_element(s.begin(), s.end()));
    }
    std::fprintf(f, "\n   uploads %d  heap in use %.1f MB  process %.1f MB\n", g_sec.uploads, mi.uordblks / 1048576.0, used / 1048576.0);
    {
        std::lock_guard lock(g_mutex);
        for (const auto& [name, s] : g_took) {
            std::fprintf(f, "   %s: %d, avg %.2f ms, max %.2f ms, total %.0f ms\n", name.c_str(), s.n, s.total / s.n, s.max, s.total);
        }
        g_took.clear();
    }
    std::fflush(f);
}

// the script: one step a line
struct Step {
    enum Kind { Label, Wait, Hold, Drag, Quit } kind = Wait;
    std::string label;
    u64 buttons = 0;
    int frames = 0;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};
std::vector<Step> g_steps;
size_t g_step = 0;
int g_step_frame = 0;
bool g_script_read = false;
HidTouchScreenState g_touch{};
bool g_touch_on = false;

u64 Buttons(const char* text) {
    static const std::pair<const char*, u64> names[] = {
        {"A", HidNpadButton_A}, {"B", HidNpadButton_B}, {"X", HidNpadButton_X}, {"Y", HidNpadButton_Y},
        {"L", HidNpadButton_L}, {"R", HidNpadButton_R}, {"ZL", HidNpadButton_ZL}, {"ZR", HidNpadButton_ZR},
        {"PLUS", HidNpadButton_Plus}, {"MINUS", HidNpadButton_Minus}, {"UP", HidNpadButton_Up},
        {"DOWN", HidNpadButton_Down}, {"LEFT", HidNpadButton_Left}, {"RIGHT", HidNpadButton_Right},
        {"NONE", 0}};
    u64 out = 0;
    std::string all = text;
    size_t at = 0;
    while (at <= all.size()) {
        const size_t plus = all.find('+', at);
        const std::string one = all.substr(at, plus == std::string::npos ? std::string::npos : plus - at);
        for (const auto& [n, b] : names) {
            if (one == n) out |= b;
        }
        if (plus == std::string::npos) break;
        at = plus + 1;
    }
    return out;
}

void ReadScript() {
    g_script_read = true;
    std::FILE* f = std::fopen("sdmc:/switch/NXDock/perf-script.txt", "r");
    if (!f) return;
    char line[256];
    while (std::fgets(line, sizeof(line), f)) {
        Step s;
        char word[32] = {}, arg[128] = {};
        if (std::sscanf(line, "%31s", word) != 1 || word[0] == '#') continue;
        if (!std::strcmp(word, "label") && std::sscanf(line, "%*s %127s", arg) == 1) {
            s.kind = Step::Label;
            s.label = arg;
        } else if (!std::strcmp(word, "wait") && std::sscanf(line, "%*s %d", &s.frames) == 1) {
            s.kind = Step::Wait;
        } else if (!std::strcmp(word, "hold") && std::sscanf(line, "%*s %127s %d", arg, &s.frames) == 2) {
            s.kind = Step::Hold;
            s.buttons = Buttons(arg);
        } else if (!std::strcmp(word, "tap") && std::sscanf(line, "%*s %127s", arg) == 1) {
            // pressed two frames, let go four
            s.kind = Step::Hold;
            s.buttons = Buttons(arg);
            s.frames = 2;
            g_steps.push_back(s);
            s = Step{};
            s.kind = Step::Wait;
            s.frames = 4;
        } else if (!std::strcmp(word, "drag") && std::sscanf(line, "%*s %f %f %f %f %d", &s.x0, &s.y0, &s.x1, &s.y1, &s.frames) == 5) {
            s.kind = Step::Drag;
        } else if (!std::strcmp(word, "quit")) {
            s.kind = Step::Quit;
        } else {
            continue;
        }
        g_steps.push_back(s);
    }
    std::fclose(f);
    if (auto* log = Log()) std::fprintf(log, "script: %zu steps\n", g_steps.size());
}

} // namespace

void Mark(const char* what) {
    if (auto* f = Log()) {
        std::fprintf(f, "boot %-28s %8.1f ms\n", what, Ms(armGetSystemTick() - g_start));
        std::fflush(f);
    }
}

void FrameBegin() {
    const u64 now = armGetSystemTick();
    if (g_first_frame) {
        g_first_frame = false;
        Mark("first frame begins");
    } else {
        g_sec.period.push_back(float(Ms(now - g_frame_at)));
        g_sec.frames++;
        if (g_sec.frames >= 600 && g_steps.empty()) {
            // without a script, a summary every ten seconds
            Flush();
            g_sec = Section{};
        }
    }
    g_frame_at = g_lap_at = now;
}

void Lap(Stage stage) {
    const u64 now = armGetSystemTick();
    g_sec.stage[stage].push_back(float(Ms(now - g_lap_at)));
    g_lap_at = now;
}

void Count(int n) {
    g_sec.uploads += n;
}

void Took(const char* what, double ms) {
    std::lock_guard lock(g_mutex);
    auto& s = g_took[what];
    s.n++;
    s.total += ms;
    s.max = std::max(s.max, ms);
}

void Script(u64& held, bool& quit) {
    if (!g_script_read) ReadScript();
    g_touch_on = false;
    while (g_step < g_steps.size()) {
        Step& s = g_steps[g_step];
        if (s.kind == Step::Label) {
            Flush();
            g_sec = Section{};
            g_sec.label = s.label;
            g_step++;
            continue;
        }
        if (s.kind == Step::Quit) {
            Flush();
            quit = true;
            g_step++;
            return;
        }
        if (g_step_frame >= s.frames) {
            g_step++;
            g_step_frame = 0;
            continue;
        }
        if (s.kind == Step::Hold) held |= s.buttons;
        if (s.kind == Step::Drag) {
            // the finger moves evenly, and is lifted on the last frame of the step
            if (g_step_frame < s.frames - 1) {
                const float k = s.frames > 2 ? float(g_step_frame) / float(s.frames - 2) : 1.f;
                g_touch = HidTouchScreenState{};
                g_touch.count = 1;
                g_touch.touches[0].x = u32(s.x0 + (s.x1 - s.x0) * k);
                g_touch.touches[0].y = u32(s.y0 + (s.y1 - s.y0) * k);
                g_touch_on = true;
            }
        }
        g_step_frame++;
        return;
    }
}

bool ScriptTouch(HidTouchScreenState& state) {
    if (!g_touch_on) return false;
    state = g_touch;
    return true;
}

void ReadTouch(HidTouchScreenState& state) {
    if (!ScriptTouch(state)) hidGetTouchScreenStates(&state, 1);
}

} // namespace nxdock::perf

#endif
