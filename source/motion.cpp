// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "motion.hpp"

#include "gfx.hpp"
#include "ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <vector>

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
    NVGcolor blue = t.work, violet = t.glow2;
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

// ---- the scenes of the styles (Settings › Look), behind everything and under the top bar ----
namespace {

// the lines of runes of a tile of 180, as the pattern of RuneStone in NXPress
struct Pt {
    float x, y;
};
const Pt kRune0[] = {{0, 40}, {52, 40}, {62, 30}, {118, 30}, {128, 40}, {180, 40}};
const Pt kRune1[] = {{0, 130}, {34, 130}, {44, 140}, {100, 140}, {110, 130}, {180, 130}};
const Pt kRune2[] = {{150, 0}, {150, 20}, {140, 30}, {140, 70}, {150, 80}, {150, 180}};
struct Line {
    const Pt* p;
    int n;
};
const Line kRunes[] = {{kRune0, 6}, {kRune1, 6}, {kRune2, 6}};

Pt Along(const Line& l, float f) {
    float len = 0;
    for (int i = 1; i < l.n; i++) len += std::hypot(l.p[i].x - l.p[i - 1].x, l.p[i].y - l.p[i - 1].y);
    float at = f * len;
    for (int i = 1; i < l.n; i++) {
        const float d = std::hypot(l.p[i].x - l.p[i - 1].x, l.p[i].y - l.p[i - 1].y);
        if (at <= d && d > 0) {
            const float k = at / d;
            return {l.p[i - 1].x + (l.p[i].x - l.p[i - 1].x) * k, l.p[i - 1].y + (l.p[i].y - l.p[i - 1].y) * k};
        }
        at -= d;
    }
    return l.p[l.n - 1];
}

NVGcolor Alpha(NVGcolor c, float a) {
    c.a = a;
    return c;
}

void Glow(NVGcontext* vg, float x, float y, float r, NVGcolor c, float inner = 0) {
    nvgBeginPath(vg);
    nvgRect(vg, x - r, y - r, 2 * r, 2 * r);
    nvgFillPaint(vg, nvgRadialGradient(vg, x, y, inner, r, c, Alpha(c, 0)));
    nvgFill(vg);
}

void RuneStone(NVGcontext* vg, const Theme& t, float tt, bool move) {
    // each part of a path costs a command of the GPU: the lines of runes go on from tile to tile
    // as one line each (a row of the two across, a column of the one down)
    nvgBeginPath(vg);
    for (int ty = 0; ty < 4; ty++) {
        const float oy = 60 + ty * 180.f;
        for (int k = 0; k < 2; k++) {
            const Line& l = kRunes[k];
            nvgMoveTo(vg, l.p[0].x, oy + l.p[0].y);
            for (int tx = 0; tx < 8; tx++) {
                for (int i = 1; i < l.n; i++) nvgLineTo(vg, tx * 180.f + l.p[i].x, oy + l.p[i].y);
            }
        }
    }
    for (int tx = 0; tx < 8; tx++) {
        const Line& l = kRunes[2];
        nvgMoveTo(vg, tx * 180.f + l.p[0].x, 60 + l.p[0].y);
        for (int ty = 0; ty < 4; ty++) {
            for (int i = 1; i < l.n; i++) nvgLineTo(vg, tx * 180.f + l.p[i].x, 60 + ty * 180.f + l.p[i].y);
        }
    }
    nvgStrokeColor(vg, Alpha(t.work, t.dark ? 0.09f : 0.13f));
    nvgStrokeWidth(vg, 1.2f);
    nvgStroke(vg);
    // a stone on each tile, where the lines turn
    nvgBeginPath(vg);
    for (int ty = 0; ty < 4; ty++) {
        for (int tx = 0; tx < 8; tx++) nvgCircle(vg, tx * 180.f + 62, 60 + ty * 180.f + 30, 3.5f);
    }
    nvgFillColor(vg, Alpha(t.work, (t.dark ? 0.16f : 0.2f) + (move ? 0.08f * std::sin(tt * 1.3f) : 0.f)));
    nvgFill(vg);
    if (!move) return;
    // a light runs along a line of runes, one after another
    for (int k = 0; k < 5; k++) {
        const float per = 4.2f, at = tt + k * 0.84f, ph = std::fmod(at, per) / per;
        const int cycle = int(at / per);
        const int tx = (k * 3 + cycle * 2) % 7, ty = k % 3;
        const Pt p = Along(kRunes[k % 3], ph);
        Glow(vg, tx * 180.f + p.x, 60 + ty * 180.f + p.y, 26, Alpha(t.work, 0.55f * std::sin(ph * 3.14159f)));
    }
}

struct Branch {
    float x0, y0, x1, y1;
};
void Grow(std::vector<Branch>* out, float x, float y, float ang, float len, int d) {
    if (d > 3) return;
    const float x2 = x + std::cos(ang) * len, y2 = y + std::sin(ang) * len;
    out[d].push_back({x, y, x2, y2});
    Grow(out, x2, y2, ang - 0.45f, len * 0.7f, d + 1);
    Grow(out, x2, y2, ang + 0.35f, len * 0.62f, d + 1);
}
// the bare branches of Vampire, by depth (made once: they do not move)
const std::vector<Branch>* Branches() {
    static std::vector<Branch> depth[4];
    static bool made = false;
    if (!made) {
        made = true;
        Grow(depth, 0, 664, -0.9f, 120, 0);
        Grow(depth, kWidth, 664, -2.3f, 110, 0);
    }
    return depth;
}

void Vampire(NVGcontext* vg, const Theme& t, float tt) {
    // the moon, with its halo and two shadows
    const float mx = 1080, my = 180;
    nvgBeginPath(vg);
    nvgRect(vg, mx - 260, my - 260, 520, 520);
    nvgFillPaint(vg, nvgRadialGradient(vg, mx, my, 40, 260, Alpha(t.work, t.dark ? 0.16f : 0.25f), Alpha(t.work, 0)));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgCircle(vg, mx, my, 62);
    nvgFillColor(vg, t.dark ? Alpha(t.work, 0.22f) : nvgRGBAf(1, 1, 1, 0.7f));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgCircle(vg, mx - 18, my - 10, 12);
    nvgCircle(vg, mx + 20, my + 16, 8);
    nvgFillColor(vg, Alpha(t.bg, 0.12f));
    nvgFill(vg);
    // clouds drift across it
    for (int k = 0; k < 3; k++) {
        const float cx = std::fmod(k * 470.f + tt * 9, kWidth + 400) - 200, cy = 140 + k * 70.f;
        const NVGcolor c = t.dark ? Alpha(t.muted, 0.08f) : nvgRGBAf(1, 1, 1, 0.35f);
        nvgBeginPath(vg);
        nvgEllipse(vg, cx, cy, 170, 40);
        nvgFillPaint(vg, nvgRadialGradient(vg, cx, cy, 0, 160, c, Alpha(c, 0)));
        nvgFill(vg);
    }
    // bare branches in the corners
    const std::vector<Branch>* depth = Branches();
    nvgLineCap(vg, NVG_ROUND);
    for (int d = 0; d < 4; d++) {
        nvgBeginPath(vg);
        for (const Branch& b : depth[d]) {
            nvgMoveTo(vg, b.x0, b.y0);
            nvgLineTo(vg, b.x1, b.y1);
        }
        nvgStrokeColor(vg, t.dark ? nvgRGBAf(0.01f, 0.012f, 0.03f, 0.9f) : Alpha(t.muted, 0.35f));
        nvgStrokeWidth(vg, 9 * std::pow(0.63f, float(d)));
        nvgStroke(vg);
    }
    nvgLineCap(vg, NVG_BUTT);
    // embers rise
    for (int i = 0; i < 22; i++) {
        const float x0 = float((i * 211 + 40) % int(kWidth)), start = std::fmod(i * 0.618f, 1.f);
        const float r = 1.2f + float(i % 3) * 0.8f, speed = 0.05f + float(i % 5) * 0.012f;
        const float f = std::fmod(start + tt * speed, 1.f), y = 664 - f * 560, x = x0 + 20 * std::sin(tt * 0.8f + x0);
        const float a = std::sin(f * 3.14159f) * (t.dark ? 0.8f : 0.55f);
        Glow(vg, x, y, r * 3, Alpha(t.ornament, a), r * 0.7f);
    }
}

void Huntress(NVGcontext* vg, const Theme& t, float tt, bool move) {
    // stars that twinkle, in four groups
    for (int g = 0; g < 4; g++) {
        nvgBeginPath(vg);
        for (int i = g; i < 64; i += 4) {
            const float s = 0.6f + float(i % 4) * 0.4f;
            nvgRect(vg, float((i * 733) % int(kWidth)), float(74 + (i * 389) % 590), s, s);
        }
        const float tw = 0.45f + 0.55f * (0.5f + 0.5f * std::sin(tt * 1.6f + float(g) * 1.57f));
        nvgFillColor(vg, t.dark ? nvgRGBAf(0.91f, 0.96f, 1.f, 0.6f * tw) : Alpha(t.muted, 0.35f * tw));
        nvgFill(vg);
    }
    // the visor scans: a band of light goes down every few seconds
    if (move) {
        const float f = std::fmod(tt, 6.5f) / 6.5f, y = 73 + f * 640;
        nvgBeginPath(vg);
        nvgRect(vg, 0, y - 60, kWidth, 62);
        nvgFillPaint(vg, nvgLinearGradient(vg, 0, y - 60, 0, y + 2, Alpha(t.work, 0), Alpha(t.work, t.dark ? 0.2f : 0.26f)));
        nvgFill(vg);
    }
    // the corners of the visor
    const float k = 26, m = 14;
    const float c[4][4] = {{m, 86, 1, 1}, {kWidth - m, 86, -1, 1}, {m, 650, 1, -1}, {kWidth - m, 650, -1, -1}};
    nvgBeginPath(vg);
    for (const auto& q : c) {
        nvgMoveTo(vg, q[0], q[1] + q[3] * k);
        nvgLineTo(vg, q[0], q[1]);
        nvgLineTo(vg, q[0] + q[2] * k, q[1]);
    }
    nvgStrokeColor(vg, Alpha(t.ornament, 0.55f));
    nvgStrokeWidth(vg, 3);
    nvgStroke(vg);
}

} // namespace

void Scene(NVGcontext* vg, const Theme& t) {
    if (t.style == kStyleNXDock) return;
    // with gentle animations or none the scene stays, but nothing in it moves
    const bool move = Lively();
    const float tt = move ? float(g_now) : 0.f;
    nvgSave(vg);
    nvgIntersectScissor(vg, 0, 73, kWidth, 664 - 73);
    if (t.style == kStyleRuneStone) RuneStone(vg, t, tt, move);
    else if (t.style == kStyleVampire) Vampire(vg, t, tt);
    else if (t.style == kStyleHuntress) Huntress(vg, t, tt, move);
    nvgRestore(vg);
}

} // namespace nxdock::ui
