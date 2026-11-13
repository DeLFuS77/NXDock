// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The styles of Settings › Look, as the colours of NXPress (all the recommended options of the
// visual test of 13/11): NXDock, RuneStone, Vampire and Huntress, each in light and in dark.
// Chosen on cards beside the row, with a finger or left and right; the colours flow into the new
// style and a wave of light leaves the card; each has its scene behind (motion.cpp), its frames
// (ui.cpp) and its own touch in the opening.
#include "app.hpp"

#include "draw.hpp"
#include "i18n.hpp"
#include "i18n_more.hpp"

#include <algorithm>
#include <cmath>

namespace nxdock {
namespace {

NVGcolor Alpha(NVGcolor c, float a) {
    c.a = a;
    return c;
}

// the cards of the panel, two by two
struct CardBox {
    float x, y, w, h;
};
CardBox StyleCard(float px, float py, float pw, int i) {
    const float gap = 16, w = (pw - 48 - gap) / 2, h = 212;
    return {px + 24 + float(i % 2) * (w + gap), py + 96 + float(i / 2) * (h + 18), w, h};
}
constexpr float kPanelX = 620, kPanelY = 96, kPanelW = kWidth - 620 - 48;

// The library as it looks in a style, drawn small with few strokes (the GPU has room for a
// limited number each frame): its background and a sign of its scene, the bar on top, covers,
// the focus, the panel of a game with its progress and its button.
void Miniature(NVGcontext* vg, const Theme& t, float x, float y, float w, float h, double time) {
    nvgSave(vg);
    nvgTranslate(vg, x, y);
    nvgScale(vg, w / kWidth, h / kHeight);
    nvgIntersectScissor(vg, 0, 0, kWidth, kHeight);
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, kWidth, kHeight);
    nvgFillPaint(vg, nvgRadialGradient(vg, 300, 260, 60, 700, nvgLerpRGBA(t.bg, t.work, t.dark ? 0.12f : 0.08f), t.bg));
    nvgFill(vg);
    // the sign of its scene
    if (t.style == kStyleRuneStone) {
        nvgBeginPath(vg);
        for (int k = 0; k < 4; k++) {
            const float oy = 120 + float(k) * 140;
            nvgMoveTo(vg, 0, oy);
            nvgLineTo(vg, 300, oy);
            nvgLineTo(vg, 330, oy - 30);
            nvgLineTo(vg, 700, oy - 30);
            nvgLineTo(vg, 730, oy);
            nvgLineTo(vg, kWidth, oy);
        }
        nvgStrokeColor(vg, Alpha(t.work, t.dark ? 0.28f : 0.3f));
        nvgStrokeWidth(vg, 6);
        nvgStroke(vg);
    } else if (t.style == kStyleVampire) {
        nvgBeginPath(vg);
        nvgCircle(vg, 1080, 200, 210);
        nvgFillPaint(vg, nvgRadialGradient(vg, 1080, 200, 90, 210, t.dark ? Alpha(t.work, 0.45f) : nvgRGBAf(1, 1, 1, 0.9f), Alpha(t.work, 0)));
        nvgFill(vg);
    } else if (t.style == kStyleHuntress) {
        nvgBeginPath(vg);
        for (int i = 0; i < 16; i++) nvgRect(vg, float((i * 733) % int(kWidth)), float(80 + (i * 389) % 580), 9, 9);
        nvgFillColor(vg, t.dark ? nvgRGBAf(0.91f, 0.96f, 1.f, 0.6f) : Alpha(t.muted, 0.5f));
        nvgFill(vg);
    } else {
        nvgBeginPath(vg);
        nvgCircle(vg, 1000, 520, 420);
        nvgFillPaint(vg, nvgRadialGradient(vg, 1000, 520, 0, 420, Alpha(t.glow2, t.dark ? 0.3f : 0.2f), Alpha(t.glow2, 0)));
        nvgFill(vg);
    }
    ui::FillRound(vg, 40, 18, 230, 36, 18, t.panel2);
    ui::FillRound(vg, 60, 50, 190, 4, 2, t.work);
    const float r = t.style == kStyleHuntress ? 4.f : 12.f;
    // the covers, all in one stroke of the colours of the style
    nvgBeginPath(vg);
    for (int i = 0; i < 8; i++) nvgRoundedRect(vg, 50 + float(i % 4) * 205, 110 + float(i / 4) * 270, 180, 240, r);
    nvgFillPaint(vg, nvgLinearGradient(vg, 0, 110, 0, 620, nvgLerpRGBA(t.panel2, t.work, 0.45f), nvgLerpRGBA(t.panel2, t.glow2, 0.35f)));
    nvgFill(vg);
    ui::Focus(vg, t, 255, 110, 180, 240, r, time);
    ui::Glass(vg, t, 884, 110, 360, 540, 18);
    ui::FillRound(vg, 914, 470, 300, 14, 7, t.line);
    ui::FillRound(vg, 914, 470, 190, 14, 7, t.work);
    ui::FillRound(vg, 914, 560, 300, 60, t.style == kStyleHuntress ? 4.f : 20.f, t.work);
    nvgRestore(vg);
}

} // namespace

const char* StyleName(int style) {
    const More& m = TrM();
    switch (style) {
        case kStyleRuneStone: return m.style_rune;
        case kStyleVampire: return m.style_vamp;
        case kStyleHuntress: return m.style_hunt;
        default: return "NXDock";
    }
}

void App::SetStyle(int style, float cx, float cy) {
    if (style < 0 || style >= kStyles || style == m_ui.style) return;
    m_style_from = m_theme; // also halfway through another change
    m_style_to = StyleTheme(style, !m_theme.dark);
    m_ui.style = style;
    m_ui.Save();
    if (cx < 0) {
        // with the controller: the wave leaves the card of the new style
        const CardBox c = StyleCard(kPanelX, kPanelY, kPanelW, style);
        cx = c.x + c.w / 2;
        cy = c.y + c.h / 2;
    }
    m_style_x = cx;
    m_style_y = cy;
    m_style_at = m_time;
    StepStyle();
}

void App::StepStyle() {
    if (m_style_at < 0) return;
    // 0.6 s with full animations, 0.3 s gentle, at once with none
    const double dur = ui::Still() ? 0 : ui::Lively() ? 0.6 : 0.3;
    const double a = m_time - m_style_at;
    if (a >= dur) {
        m_theme = m_style_to;
        if (a > 0.8) m_style_at = -100; // the wave is over too
        return;
    }
    const float k = float(a / dur);
    m_theme = MixTheme(m_style_from, m_style_to, k < 0.5f ? 4 * k * k * k : 1 - std::pow(-2 * k + 2, 3.f) / 2);
}

void App::DrawStyleWave(NVGcontext* vg) {
    if (m_style_at < 0 || !ui::Lively()) return;
    const float r = float(std::clamp((m_time - m_style_at) / 0.8, 0.0, 1.0));
    if (r >= 1) return;
    const float radius = 60 + 1500 * ui::EaseOut(r);
    const NVGcolor c = m_style_to.work;
    // a soft light just inside the ring, and the ring
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, kWidth, kHeight);
    nvgFillPaint(vg, nvgRadialGradient(vg, m_style_x, m_style_y, radius * 0.85f, radius, Alpha(c, 0), Alpha(c, 0.12f * (1 - r))));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgCircle(vg, m_style_x, m_style_y, radius);
    nvgStrokeColor(vg, Alpha(c, 0.55f * (1 - r)));
    nvgStrokeWidth(vg, 40 * (1 - r) + 2);
    nvgStroke(vg);
}

void App::DrawStylePanel(NVGcontext* vg, float x, float y, float w, float h) {
    const Theme& t = m_theme;
    const More& m = TrM();
    draw::Panel(vg, t, x, y, w, h, 18);
    ui::Title(vg, x + 26, y + 36, 24, t.text, m.style_row);
    draw::Box(vg, x + 26, y + 56, w - 52, 15, t.muted, m.style_hint);
    for (int i = 0; i < kStyles; i++) {
        const CardBox c = StyleCard(x, y, w, i);
        const Theme look = StyleTheme(i, !t.dark);
        const bool on = m_ui.style == i;
        ui::FillRound(vg, c.x, c.y, c.w, c.h, t.style == kStyleHuntress ? 4.f : 14.f, t.panel2);
        const float mw = c.w - 24, mh = mw * 9 / 16;
        Miniature(vg, look, c.x + 12, c.y + 12, mw, mh, m_time);
        ui::Text(vg, c.x + 14, c.y + mh + 38, 20, t.text, StyleName(i));
        // its colours: background, panel, main colour and adornment
        const NVGcolor sw[] = {look.bg, look.panel2, look.work, look.ornament};
        nvgBeginPath(vg);
        for (int k = 0; k < 4; k++) nvgCircle(vg, c.x + c.w - 24 - float(k) * 22, c.y + mh + 38, 8.5f);
        nvgFillColor(vg, Alpha(t.muted, 0.6f));
        nvgFill(vg);
        for (int k = 0; k < 4; k++) {
            nvgBeginPath(vg);
            nvgCircle(vg, c.x + c.w - 24 - float(k) * 22, c.y + mh + 38, 7.5f);
            nvgFillColor(vg, sw[k]);
            nvgFill(vg);
        }
        if (on) {
            ui::Focus(vg, t, c.x, c.y, c.w, c.h, 14, m_time);
            nvgBeginPath(vg);
            nvgCircle(vg, c.x + c.w - 22, c.y + 24, 14);
            nvgFillColor(vg, t.work);
            nvgFill(vg);
            ui::CheckMark(vg, c.x + c.w - 22, c.y + 24.5f, 10, t.on_work, 2.6f);
        }
        AddHit(c.x, c.y, c.w, c.h, HitKind::StyleCard, i);
    }
}

void App::DrawBootStyle(NVGcontext* vg, double a, float cx, float cy, float s, float veil) {
    // the touch of each style in the opening, under the icon
    const Theme& t = m_theme;
    if (t.style == kStyleVampire) {
        // the moon rises behind the icon, embers float up
        const float mr = ui::EaseOut(float(std::clamp(a / 0.9, 0.0, 1.0))), my = 620 - 320 * mr;
        nvgBeginPath(vg);
        nvgRect(vg, 0, 0, kWidth, kHeight);
        nvgFillPaint(vg, nvgRadialGradient(vg, 640, my, 60, 330, Alpha(t.work, 0.3f * veil), Alpha(t.work, 0)));
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgCircle(vg, 640, my, 150);
        nvgFillColor(vg, t.dark ? Alpha(t.work, 0.25f * veil) : nvgRGBAf(1, 1, 1, 0.8f * veil));
        nvgFill(vg);
        for (int k = 0; k < 14; k++) {
            const float f = float(std::clamp(a / 1.1 - k * 0.03, 0.0, 1.0));
            const float ex = 400 + float(k) * 37 + 15 * std::sin(float(a) * 4 + float(k)), ey = 700 - f * (300 + float(k) * 18);
            nvgBeginPath(vg);
            nvgCircle(vg, ex, ey, 2 + float(k % 3));
            nvgFillColor(vg, Alpha(t.ornament, std::sin(f * 3.14159f) * veil));
            nvgFill(vg);
        }
    } else if (t.style == kStyleRuneStone) {
        // lines of runes run from the edges to the icon, which lights up
        const float ends[][2] = {{0, 150}, {0, 420}, {kWidth, 130}, {kWidth, 470}, {330, kHeight}, {950, kHeight}, {640, 73}};
        nvgLineCap(vg, NVG_ROUND);
        for (int k = 0; k < 7; k++) {
            const float ex = ends[k][0], ey = ends[k][1], mx = ex + (cx - ex) * 0.5f;
            const float pts[4][2] = {{ex, ey}, {mx, ey}, {mx + (cx - mx) * 0.4f, cy}, {cx, cy}};
            const float f = ui::EaseOut(float(std::clamp((a - k * 0.05) / 0.75, 0.0, 1.0))) * 3;
            nvgBeginPath(vg);
            nvgMoveTo(vg, ex, ey);
            float hx = ex, hy = ey;
            for (int i = 1; i < 4; i++) {
                const float part = std::clamp(f - float(i - 1), 0.f, 1.f);
                if (part <= 0) break;
                hx = pts[i - 1][0] + (pts[i][0] - pts[i - 1][0]) * part;
                hy = pts[i - 1][1] + (pts[i][1] - pts[i - 1][1]) * part;
                nvgLineTo(vg, hx, hy);
            }
            nvgStrokeColor(vg, Alpha(t.work, 0.8f * veil));
            nvgStrokeWidth(vg, 2);
            nvgStroke(vg);
            nvgBeginPath(vg);
            nvgRect(vg, hx - 16, hy - 16, 32, 32);
            nvgFillPaint(vg, nvgRadialGradient(vg, hx, hy, 2, 16, Alpha(t.work, veil), Alpha(t.work, 0)));
            nvgFill(vg);
        }
        nvgLineCap(vg, NVG_BUTT);
        const float fl = float(std::clamp((a - 0.8) / 0.3, 0.0, 1.0));
        if (fl > 0 && fl < 1) {
            nvgBeginPath(vg);
            nvgCircle(vg, cx, cy, s * 0.55f + 200 * fl);
            nvgStrokeColor(vg, Alpha(t.work, (1 - fl) * 0.7f));
            nvgStrokeWidth(vg, 6 * (1 - fl) + 1);
            nvgStroke(vg);
        }
    } else if (t.style == kStyleHuntress) {
        // the corners of the visor close on the icon and a scan lights it
        const float k = ui::EaseOut(float(std::clamp((a - 0.1) / 0.6, 0.0, 1.0))), d = 300 - 280 * k, len = 40;
        const float q[4][4] = {{cx - s / 2 - d, cy - s / 2 - d, 1, 1}, {cx + s / 2 + d, cy - s / 2 - d, -1, 1},
                               {cx - s / 2 - d, cy + s / 2 + d, 1, -1}, {cx + s / 2 + d, cy + s / 2 + d, -1, -1}};
        nvgBeginPath(vg);
        for (const auto& p : q) {
            nvgMoveTo(vg, p[0], p[1] + p[3] * len);
            nvgLineTo(vg, p[0], p[1]);
            nvgLineTo(vg, p[0] + p[2] * len, p[1]);
        }
        nvgStrokeColor(vg, Alpha(t.work, veil));
        nvgStrokeWidth(vg, 4);
        nvgStroke(vg);
        ui::FillRound(vg, cx - 30, cy - s / 2 - d - 10, 60, 4, 1, Alpha(t.ornament, veil));
        const float sc = float(std::clamp((a - 0.3) / 0.6, 0.0, 1.0));
        if (sc > 0 && sc < 1) {
            const float sy = 130 + 360 * sc;
            nvgBeginPath(vg);
            nvgRect(vg, 0, sy - 80, kWidth, 80);
            nvgFillPaint(vg, nvgLinearGradient(vg, 0, sy - 80, 0, sy, Alpha(t.work, 0), Alpha(t.work, 0.45f * veil)));
            nvgFill(vg);
            ui::FillRound(vg, 0, sy - 1, kWidth, 2, 0, Alpha(t.work, 0.9f * veil));
        }
    }
}

} // namespace nxdock
