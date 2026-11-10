// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "motion.hpp"

#include "gfx.hpp"
#include "ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace nxdock::ui {
namespace {

MotionLevel g_level = MotionLevel::Full;
double g_now = 0;
std::map<unsigned long long, double> g_pressed;
struct SpringState {
    float pos = -1, vel = 0;
};
std::map<std::string, SpringState> g_springs;

bool Is(const char* a, const char* b) {
    return a && b && std::strcmp(a, b) == 0;
}

} // namespace

void SetMotion(int level) {
    g_level = level == 2 ? MotionLevel::None : level == 1 ? MotionLevel::Gentle : MotionLevel::Full;
}

MotionLevel Motion() {
    return g_level;
}

void Tick(double now) {
    g_now = now;
}

double Now() {
    return g_now;
}

void NotePress(unsigned long long down) {
    for (int i = 0; i < 64; i++) {
        const unsigned long long b = 1ull << i;
        if (down & b) g_pressed[b] = g_now;
    }
}

float PressAmount(unsigned long long button) {
    if (Still() || !button) return 0;
    const auto it = g_pressed.find(button);
    if (it == g_pressed.end()) return 0;
    const float s = float(g_now - it->second);
    if (s < 0 || s > 0.28f) return 0;
    // down quickly, back with a little spring
    return s < 0.07f ? s / 0.07f : std::max(0.f, 1.f - (s - 0.07f) / 0.21f);
}

float Appear(double since, float dur) {
    if (Still()) return 1;
    const float x = float((g_now - since) / dur);
    return std::clamp(x, 0.f, 1.f);
}

float EaseOut(float x) {
    x = std::clamp(x, 0.f, 1.f);
    const float k = 1 - x;
    return 1 - k * k * k;
}

float EaseOutBack(float x) {
    if (!Lively()) return EaseOut(x);
    x = std::clamp(x, 0.f, 1.f);
    const float c1 = 1.70158f, c3 = c1 + 1;
    return 1 + c3 * std::pow(x - 1, 3.f) + c1 * std::pow(x - 1, 2.f);
}

float Glide::Step(float target) {
    if (Still() || value < -1e8f) {
        value = target;
        return value;
    }
    value += (target - value) * 0.28f;
    if (std::fabs(target - value) < 0.5f) value = target;
    return value;
}

float Spring(const std::string& key, bool on) {
    auto& s = g_springs[key];
    const float target = on ? 1.f : 0.f;
    if (s.pos < 0 || Still()) {
        s.pos = target;
        s.vel = 0;
        return s.pos;
    }
    if (Lively()) {
        s.vel += (target - s.pos) * 0.32f;
        s.vel *= 0.62f;
        s.pos += s.vel;
    } else {
        s.pos += (target - s.pos) * 0.3f;
    }
    if (std::fabs(target - s.pos) < 0.002f && std::fabs(s.vel) < 0.002f) s.pos = target;
    return s.pos;
}

float SymbolWidth(NVGcontext* vg, float size, const char* g) {
    if (Is(g, glyph::A) || Is(g, glyph::B) || Is(g, glyph::X) || Is(g, glyph::Y)) return size;
    if (Is(g, glyph::L) || Is(g, glyph::R)) return size * 0.95f;
    if (Is(g, glyph::ZL) || Is(g, glyph::ZR)) return size * 1.25f;
    if (Is(g, glyph::DPad) || Is(g, glyph::Stick)) return size * 0.85f;
    return TextWidth(vg, size, g);
}

void Symbol(NVGcontext* vg, const Theme& t, float cx, float cy, float size, const char* g, NVGcolor color, unsigned long long button) {
    const float press = PressAmount(button);
    const float s = size * (1 - 0.16f * press);
    // the face buttons: where it is on the controller
    int lit = -1;
    if (Is(g, glyph::X)) lit = 0;
    else if (Is(g, glyph::A)) lit = 1;
    else if (Is(g, glyph::B)) lit = 2;
    else if (Is(g, glyph::Y)) lit = 3;
    if (lit >= 0) {
        const float d = s * 0.3f, r = s * 0.17f;
        const float ox[4] = {0, d, 0, -d}, oy[4] = {-d, 0, d, 0};
        for (int i = 0; i < 4; i++) {
            NVGcolor c = color;
            if (i != lit) c.a *= 0.32f;
            if (i == lit && press > 0) {
                NVGcolor glow = t.work;
                glow.a = 0.6f * press;
                NVGcolor clear = glow;
                clear.a = 0;
                nvgBeginPath(vg);
                nvgCircle(vg, cx + ox[i], cy + oy[i], r * 3);
                nvgFillPaint(vg, nvgRadialGradient(vg, cx + ox[i], cy + oy[i], r, r * 3, glow, clear));
                nvgFill(vg);
            }
            nvgBeginPath(vg);
            nvgCircle(vg, cx + ox[i], cy + oy[i], i == lit ? r * 1.15f : r);
            nvgFillColor(vg, c);
            nvgFill(vg);
        }
        return;
    }
    // the shoulders: a pill with a rounded top
    const bool zl = Is(g, glyph::ZL) || Is(g, glyph::ZR);
    if (zl || Is(g, glyph::L) || Is(g, glyph::R)) {
        const float w = SymbolWidth(vg, s, g), h = s * 0.68f;
        const float x = cx - w / 2, y = cy - h / 2;
        nvgBeginPath(vg);
        nvgRoundedRectVarying(vg, x, y, w, h, h * 0.5f, h * 0.5f, h * 0.2f, h * 0.2f);
        nvgFillColor(vg, color);
        nvgFill(vg);
        const char* label = Is(g, glyph::ZL) ? "ZL" : Is(g, glyph::ZR) ? "ZR" : Is(g, glyph::L) ? "L" : "R";
        Text(vg, cx, cy + 0.5f, s * 0.46f, t.bg, label, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        return;
    }
    if (Is(g, glyph::DPad)) {
        const float a = s * 0.42f, b = s * 0.15f;
        FillRound(vg, cx - b, cy - a, b * 2, a * 2, 2, color);
        FillRound(vg, cx - a, cy - b, a * 2, b * 2, 2, color);
        return;
    }
    if (Is(g, glyph::Stick)) {
        const float r = s * 0.4f;
        nvgBeginPath(vg);
        nvgCircle(vg, cx, cy, r);
        nvgStrokeColor(vg, color);
        nvgStrokeWidth(vg, 2);
        nvgStroke(vg);
        const float wobble = Lively() ? float(std::sin(g_now * 3)) * r * 0.3f : 0.f;
        nvgBeginPath(vg);
        nvgCircle(vg, cx, cy + wobble, r * 0.45f);
        nvgFillColor(vg, color);
        nvgFill(vg);
        return;
    }
    nvgSave(vg);
    nvgTranslate(vg, cx, cy);
    nvgScale(vg, 1 - 0.16f * press, 1 - 0.16f * press);
    Text(vg, 0, 1, size, color, g, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgRestore(vg);
}

void Aurora(NVGcontext* vg, const Theme& t) {
    if (!Lively()) return;
    const float tt = float(g_now);
    struct Light {
        float x, y, r, speed;
        NVGcolor c;
    };
    NVGcolor blue = t.work, violet = t.violet;
    blue.a = t.dark ? 0.10f : 0.07f;
    violet.a = t.dark ? 0.09f : 0.06f;
    const Light lights[2] = {{260, 220, 520, 0.11f, blue}, {1020, 520, 470, 0.08f, violet}};
    for (int i = 0; i < 2; i++) {
        const auto& l = lights[i];
        const float x = l.x + 50 * std::sin(tt * l.speed + float(i)), y = l.y + 30 * std::cos(tt * l.speed * 1.3f + float(i));
        NVGcolor clear = l.c;
        clear.a = 0;
        nvgBeginPath(vg);
        nvgRect(vg, 0, 73, kWidth, 664 - 73);
        nvgFillPaint(vg, nvgRadialGradient(vg, x, y, 0, l.r, l.c, clear));
        nvgFill(vg);
    }
}

} // namespace nxdock::ui
