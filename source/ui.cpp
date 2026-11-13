// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include "gfx.hpp"
#include "i18n.hpp"
#include "i18n_more.hpp"
#include "motion.hpp"

extern "C" {
#include <qrcodegen.h>
}

namespace nxdock::ui {

float Text(NVGcontext* vg, float x, float y, float size, NVGcolor color, const char* str, int align) {
    nvgFontFace(vg, "ui");
    nvgFontSize(vg, size);
    nvgTextAlign(vg, align);
    nvgFillColor(vg, color);
    return nvgText(vg, x, y, str, nullptr);
}

float TextWidth(NVGcontext* vg, float size, const char* str) {
    nvgFontFace(vg, "ui");
    nvgFontSize(vg, size);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    return nvgTextBounds(vg, 0, 0, str, nullptr, nullptr);
}

float Title(NVGcontext* vg, float x, float y, float size, NVGcolor color, const char* str, int align) {
    Text(vg, x, y, size, color, str, align);
    return Text(vg, x + (size >= 24 ? 0.7f : 0.5f), y, size, color, str, align);
}

namespace {

NVGcolor Alpha(NVGcolor c, float a) {
    c.a = a;
    return c;
}

// The marks of the corners of Huntress, a stroke `k` long at each corner, `d` outside the box.
void Corners(NVGcontext* vg, float x, float y, float w, float h, float d, float k, NVGcolor c, float width) {
    const float q[4][4] = {{x - d, y - d, 1, 1}, {x + w + d, y - d, -1, 1}, {x - d, y + h + d, 1, -1}, {x + w + d, y + h + d, -1, -1}};
    nvgBeginPath(vg);
    for (const auto& p : q) {
        nvgMoveTo(vg, p[0], p[1] + p[3] * k);
        nvgLineTo(vg, p[0], p[1]);
        nvgLineTo(vg, p[0] + p[2] * k, p[1]);
    }
    nvgStrokeColor(vg, c);
    nvgStrokeWidth(vg, width);
    nvgStroke(vg);
}

} // namespace

void Glass(NVGcontext* vg, const Theme& t, float x, float y, float w, float h, float r) {
    if (t.style == kStyleHuntress) r = std::min(r, 4.f); // armour: nearly square
    // the shadow, only outside the panel (it is a little see-through)
    nvgBeginPath(vg);
    nvgRect(vg, x - 16, y - 8, w + 32, h + 30);
    nvgRoundedRect(vg, x, y, w, h, r);
    nvgPathWinding(vg, NVG_HOLE);
    nvgFillPaint(vg, nvgBoxGradient(vg, x, y + 6, w, h, r + 4, 18, nvgRGBAf(0, 0, 0, t.dark ? 0.32f : 0.09f), nvgRGBAf(0, 0, 0, 0)));
    nvgFill(vg);
    NVGcolor fill = t.panel;
    fill.a = t.dark ? 0.80f : 0.82f;
    FillRound(vg, x, y, w, h, r, fill);
    // the frame of the style: RuneStone glows, Vampire has a double edge of its adornment,
    // Huntress marks its corners and has a line of light along the top
    if (t.style == kStyleRuneStone) {
        nvgBeginPath(vg);
        nvgRect(vg, x - 20, y - 20, w + 40, h + 40);
        nvgRoundedRect(vg, x, y, w, h, r);
        nvgPathWinding(vg, NVG_HOLE);
        nvgFillPaint(vg, nvgBoxGradient(vg, x, y, w, h, r + 2, 18, Alpha(t.work, t.dark ? 0.14f : 0.12f), Alpha(t.work, 0)));
        nvgFill(vg);
        StrokeRound(vg, x + .5f, y + .5f, w - 1, h - 1, r, nvgLerpRGBA(t.line, t.work, 0.4f), 1);
        return;
    }
    if (t.style == kStyleVampire) {
        StrokeRound(vg, x + .5f, y + .5f, w - 1, h - 1, r, Alpha(t.ornament, 0.6f), 1);
        if (h > 30) StrokeRound(vg, x + 4.5f, y + 4.5f, w - 9, h - 9, std::max(0.f, r - 3), Alpha(t.ornament, 0.22f), 1);
        return;
    }
    if (t.style == kStyleHuntress) {
        StrokeRound(vg, x + .5f, y + .5f, w - 1, h - 1, r, Alpha(t.ornament, 0.45f), 1);
        Corners(vg, x + 5, y + 5, w - 10, h - 10, 0, std::min(14.f, h / 4), t.work, 2.5f);
        nvgBeginPath(vg);
        nvgRect(vg, x + 24, y, w - 48, 1.5f);
        nvgFillPaint(vg, nvgLinearGradient(vg, x + 24, y, x + w / 2, y, Alpha(t.work, 0), Alpha(t.work, 0.7f)));
        nvgFill(vg);
        return;
    }
    StrokeRound(vg, x + .5f, y + .5f, w - 1, h - 1, r, t.line, 1);
    // the light along the top edge
    if (w > 2 * r) {
        nvgBeginPath(vg);
        nvgMoveTo(vg, x + r, y + 1.2f);
        nvgLineTo(vg, x + w - r, y + 1.2f);
        nvgStrokeColor(vg, nvgRGBAf(1, 1, 1, t.dark ? 0.08f : 0.9f));
        nvgStrokeWidth(vg, 1.2f);
        nvgStroke(vg);
    }
}

void FillRound(NVGcontext* vg, float x, float y, float w, float h, float r, NVGcolor c) {
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, w, h, r);
    nvgFillColor(vg, c);
    nvgFill(vg);
}

void StrokeRound(NVGcontext* vg, float x, float y, float w, float h, float r, NVGcolor c, float width) {
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, w, h, r);
    nvgStrokeColor(vg, c);
    nvgStrokeWidth(vg, width);
    nvgStroke(vg);
}

float Chip(NVGcontext* vg, const Theme& t, float x, float y, const char* label, bool on) {
    const float w = TextWidth(vg, 16, label) + 32;
    FillRound(vg, x, y, w, 34, 17, on ? t.work : t.panel2);
    if (!on) {
        StrokeRound(vg, x + .5f, y + .5f, w - 1, 33, 16.5f, t.line, 1);
    }
    Text(vg, x + w / 2, y + 17, 16, on ? t.on_work : t.muted, label, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    return w;
}

void Focus(NVGcontext* vg, const Theme& t, float x, float y, float w, float h, float r, double time) {
    const float pulse = 0.75f + 0.25f * float(std::sin(time * 3.0));
    if (t.style == kStyleHuntress) {
        // the visor locks on: a thin frame and its corners, which breathe, with a mark on top
        const float breathe = Lively() ? 0.5f + 0.5f * float(std::sin(time * 3.0)) : 0.6f;
        StrokeRound(vg, x - 4, y - 4, w + 8, h + 8, 4, Alpha(t.work, 0.5f), 1.5f);
        const float d = 9 + 2 * breathe, k = std::min(22.f, h / 2.5f);
        Corners(vg, x, y, w, h, d + 4, k + 4, Alpha(t.work, 0.25f), 8);
        Corners(vg, x, y, w, h, d, k, t.work, 4);
        FillRound(vg, x + w / 2 - 14, y - d - 2, 28, 3, 1, t.ornament);
        return;
    }
    NVGcolor glow = t.style == kStyleVampire ? t.ornament : t.work;
    // RuneStone shines more, breathing
    glow.a = (t.style == kStyleRuneStone ? 0.8f : 0.55f) * pulse;
    NVGcolor clear = t.work;
    clear.a = 0;
    NVGpaint shadow = nvgBoxGradient(vg, x - 4, y - 4, w + 8, h + 8, r + 4, 24, glow, clear);
    nvgBeginPath(vg);
    nvgRect(vg, x - 40, y - 40, w + 80, h + 80);
    nvgRoundedRect(vg, x - 7, y - 7, w + 14, h + 14, r + 7);
    nvgPathWinding(vg, NVG_HOLE);
    nvgFillPaint(vg, shadow);
    nvgFill(vg);
    StrokeRound(vg, x - 5, y - 5, w + 10, h + 10, r + 5, t.work, t.style == kStyleVampire ? 2.5f : 4);
}

float Marquee(NVGcontext* vg, float x, float y, float w, float size, NVGcolor color, const char* str, double time) {
    const float tw = TextWidth(vg, size, str);
    if (tw <= w) return Text(vg, x, y, size, color, str);
    const float travel = tw - w;
    const float speed = 40.f, pause = 1.6f; // pixels a second; seconds at each end
    const float run = travel / speed;
    const float period = 2 * (pause + run);
    float at = Still() ? 0.f : std::fmod(float(time), period);
    float offset;
    if (at < pause) offset = 0;
    else if ((at -= pause) < run) offset = at * speed;
    else if ((at -= run) < pause) offset = travel;
    else offset = travel - (at - pause) * speed;
    nvgSave(vg);
    nvgIntersectScissor(vg, x, y - size, w, size * 2);
    Text(vg, x - offset, y, size, color, str);
    nvgRestore(vg);
    return x + w;
}

void Progress(NVGcontext* vg, const Theme& t, float x, float y, float w, float h, float frac,
              NVGcolor color, double time, bool moving) {
    NVGcolor track = color;
    track.a = t.dark ? 0.18f : 0.16f;
    FillRound(vg, x, y, w, h, h / 2, track);
    const float fw = w * std::fmax(0.f, std::fmin(1.f, frac));
    if (fw <= 0) {
        return;
    }
    FillRound(vg, x, y, fw, h, h / 2, color);

    // Stripes at 45 degrees, sliding to the right.
    nvgSave(vg);
    nvgIntersectScissor(vg, x, y, fw, h);
    const float period = 22.f;
    const float shift = moving ? std::fmod(float(time) * period, period) : 0.f;
    nvgBeginPath(vg);
    for (float sx = x - h - period + shift; sx < x + fw; sx += period) {
        nvgMoveTo(vg, sx, y + h);
        nvgLineTo(vg, sx + period / 2, y + h);
        nvgLineTo(vg, sx + period / 2 + h, y);
        nvgLineTo(vg, sx + h, y);
        nvgClosePath(vg);
    }
    nvgFillColor(vg, nvgRGBAf(1, 1, 1, 0.22f));
    nvgFill(vg);
    nvgRestore(vg);
    if (moving && fw < w && !Still()) {
        // a soft light at the head of the bar
        NVGcolor glow = color;
        glow.a = 0.55f + 0.2f * float(std::sin(time * 4));
        NVGcolor clear = color;
        clear.a = 0;
        const float r = h * 1.6f;
        nvgBeginPath(vg);
        nvgCircle(vg, x + fw, y + h / 2, r);
        nvgFillPaint(vg, nvgRadialGradient(vg, x + fw, y + h / 2, h * 0.2f, r, glow, clear));
        nvgFill(vg);
    }
}

void CheckMark(NVGcontext* vg, float cx, float cy, float s, NVGcolor c, float width) {
    nvgBeginPath(vg);
    nvgMoveTo(vg, cx - s * 0.45f, cy + s * 0.02f);
    nvgLineTo(vg, cx - s * 0.12f, cy + s * 0.34f);
    nvgLineTo(vg, cx + s * 0.48f, cy - s * 0.32f);
    nvgStrokeColor(vg, c);
    nvgStrokeWidth(vg, width);
    nvgLineCap(vg, NVG_ROUND);
    nvgLineJoin(vg, NVG_ROUND);
    nvgStroke(vg);
}

void Cover(NVGcontext* vg, const Theme& t, float x, float y, float s, unsigned rgb, const char* initial,
           const char* format, bool installed, bool update, float radius, int image) {
    if (image > 0) {
        // the real cover, filling the square (a cover that is not square is cut at its sides)
        int iw = 1, ih = 1;
        nvgImageSize(vg, image, &iw, &ih);
        const float scale = s / float(std::min(iw, ih));
        const float dw = iw * scale, dh = ih * scale;
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, s, s, radius);
        nvgFillPaint(vg, nvgImagePattern(vg, x + (s - dw) / 2, y + (s - dh) / 2, dw, dh, 0, image, 1));
        nvgFill(vg);
    } else {
    const NVGcolor base = Hex(rgb);
    const NVGcolor light = nvgLerpRGBA(base, nvgRGB(255, 255, 255), 0.22f);
    const NVGcolor dark = nvgLerpRGBA(base, nvgRGB(0, 0, 0), 0.30f);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, s, s, radius);
    nvgFillPaint(vg, nvgLinearGradient(vg, x, y, x + s, y + s, light, dark));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, s, s, radius);
    nvgFillPaint(vg, nvgRadialGradient(vg, x + s * 0.3f, y + s * 0.25f, 0, s * 0.5f,
                                       nvgRGBAf(1, 1, 1, 0.28f), nvgRGBAf(1, 1, 1, 0)));
    nvgFill(vg);

    Text(vg, x + s / 2, y + s / 2 + 2, s * 0.42f, nvgRGBAf(1, 1, 1, 0.88f), initial,
         NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    }

    if (format && s >= 80) {
        const float fw = TextWidth(vg, 12, format) + 14;
        FillRound(vg, x + 8, y + 8, fw, 22, 6, nvgRGBAf(0, 0, 0, 0.55f));
        Text(vg, x + 8 + fw / 2, y + 19, 12, nvgRGB(255, 255, 255), format, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    }
    if (installed && s >= 80) {
        nvgBeginPath(vg);
        nvgCircle(vg, x + s - 21, y + 21, 13);
        nvgFillColor(vg, t.ok);
        nvgFill(vg);
        CheckMark(vg, x + s - 21, y + 21, 14, nvgRGB(255, 255, 255), 2.6f);
    }
    if (update && s >= 80) {
        const char* label = Tr().update_badge;
        const float uw = TextWidth(vg, 12, label) + 14;
        FillRound(vg, x + s - 8 - uw, y + s - 30, uw, 22, 6, Hex(0x22bfe3));
        Text(vg, x + s - 8 - uw / 2, y + s - 19, 12, Hex(0x082430), label, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    }
}

void Spinner(NVGcontext* vg, float cx, float cy, float r, NVGcolor c, double time) {
    NVGcolor faint = c;
    faint.a = 0.25f;
    nvgBeginPath(vg);
    nvgCircle(vg, cx, cy, r);
    nvgStrokeColor(vg, faint);
    nvgStrokeWidth(vg, 2.5f);
    nvgStroke(vg);
    const float a0 = float(time) * 6.f;
    nvgBeginPath(vg);
    nvgArc(vg, cx, cy, r, a0, a0 + 1.6f, NVG_CW);
    nvgStrokeColor(vg, c);
    nvgLineCap(vg, NVG_ROUND);
    nvgStroke(vg);
}

void Qr(NVGcontext* vg, float x, float y, float s, const std::string& text) {
    static std::string encoded;
    static std::vector<uint8_t> code;
    if (text != encoded) {
        encoded = text;
        code.assign(qrcodegen_BUFFER_LEN_MAX, 0);
        std::vector<uint8_t> temp(qrcodegen_BUFFER_LEN_MAX);
        if (!qrcodegen_encodeText(text.c_str(), temp.data(), code.data(), qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
                                  qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true)) {
            code.clear();
        }
    }
    FillRound(vg, x, y, s, s, 10, nvgRGB(255, 255, 255));
    if (code.empty()) {
        return;
    }
    // the modules with the quiet zone the standard asks for (here a little less: the tile is white)
    const int n = qrcodegen_getSize(code.data());
    const float margin = 10, cell = (s - 2 * margin) / float(n);
    nvgBeginPath(vg);
    for (int my = 0; my < n; my++) {
        for (int mx = 0; mx < n; mx++) {
            if (qrcodegen_getModule(code.data(), mx, my)) {
                // a hair wider than the cell, so no seam shows between dark modules
                nvgRect(vg, x + margin + mx * cell, y + margin + my * cell, cell + 0.3f, cell + 0.3f);
            }
        }
    }
    nvgFillColor(vg, nvgRGB(17, 17, 17));
    nvgFill(vg);
}

namespace {

// The dock of the icon (assets/icon.svg) seen from the front, from its own 256 px layout scaled by
// `k`: its back and slot (`back`), or its front with the step, the bar lit by `bar` and the light.
void DockPart(NVGcontext* vg, float x, float y, float k, bool back, float bar, float lamp) {
    const auto rect = [&](float rx, float ry, float rw, float rh, float r, NVGcolor c) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x + rx * k, y + ry * k, rw * k, rh * k, r * k);
        nvgFillColor(vg, c);
        nvgFill(vg);
    };
    if (back) {
        rect(30, 110, 196, 118, 9, Hex(0x2c2c30));
        rect(70, 111, 116, 7, 3.5f, Hex(0x0d0d10));
        return;
    }
    rect(30, 120, 196, 108, 9, Hex(0x232326));
    rect(38, 120, 180, 2, 1, Hex(0x6b6b72));
    rect(58, 196, 140, 12, 6, nvgRGBA(0, 0, 0, 90));
    const float lit = 98 * std::clamp(bar, 0.f, 1.f);
    if (lit > 1) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x + 58 * k, y + 196 * k, std::max(12.f, lit) * k, 12 * k, 6 * k);
        nvgFillPaint(vg, nvgLinearGradient(vg, x + 58 * k, y, x + 156 * k, y, Hex(0x29d0ff), Hex(0x7c5cff)));
        nvgFill(vg);
    }
    if (lamp > 0.01f) {
        nvgBeginPath(vg);
        nvgCircle(vg, x + 212 * k, y + 216 * k, std::max(1.f, 5 * k));
        nvgFillColor(vg, Hex(0x3ee87a, std::min(1.f, lamp)));
        nvgFill(vg);
    }
}

} // namespace

void Brand(NVGcontext* vg, float x, float y, float s, float card, float bar, float lamp) {
    const float k = s / 256.f;
    const auto rect = [&](float rx, float ry, float rw, float rh, float r, NVGcolor c) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x + rx * k, y + ry * k, rw * k, rh * k, r * k);
        nvgFillColor(vg, c);
        nvgFill(vg);
    };
    // the background
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, s, s, s * 0.22f);
    nvgFillPaint(vg, nvgLinearGradient(vg, x, y, x + s, y + s, Hex(0x4fc8f5), Hex(0x5a2fc0)));
    nvgFill(vg);
    // the game card, its lower part already in the dock; before that it falls in from above
    nvgSave(vg);
    nvgIntersectScissor(vg, x, y, s, 117 * k);
    const float drop = -130 * (1 - std::clamp(card, 0.f, 1.2f));
    nvgTranslate(vg, 0, drop * k);
    rect(96, 34, 64, 100, 8, Hex(0x45454b));
    rect(106, 42, 44, 3, 1.5f, Hex(0x1a1a1d));
    rect(102, 57, 52, 70, 5, Hex(0xe8202f));
    nvgStrokeColor(vg, nvgRGB(255, 255, 255));
    nvgStrokeWidth(vg, std::max(1.2f, 9 * k));
    nvgLineCap(vg, NVG_ROUND);
    nvgLineJoin(vg, NVG_ROUND);
    nvgBeginPath(vg);
    nvgMoveTo(vg, x + 128 * k, y + 70 * k);
    nvgLineTo(vg, x + 128 * k, y + 100 * k);
    nvgMoveTo(vg, x + 116 * k, y + 88 * k);
    nvgLineTo(vg, x + 128 * k, y + 100 * k);
    nvgLineTo(vg, x + 140 * k, y + 88 * k);
    nvgStroke(vg);
    nvgRestore(vg);
    DockPart(vg, x, y, k, true, 0, 0);
    DockPart(vg, x, y, k, false, bar, lamp);
}

void Dock(NVGcontext* vg, float x, float y, float s, float bar, void (*inside)(NVGcontext*, void*), void* data) {
    const float k = s / 256.f;
    DockPart(vg, x, y, k, true, 0, 0);
    if (inside) {
        // what falls in shows only above the slot
        nvgSave(vg);
        nvgIntersectScissor(vg, x - s, y - 2 * s, 3 * s, 2 * s + 117 * k);
        inside(vg, data);
        nvgRestore(vg);
    }
    DockPart(vg, x, y, k, false, bar, 1);
}

float Battery(NVGcontext* vg, const Theme& t, float right, float cy, int percent, bool charging) {
    percent = std::clamp(percent, 0, 100);
    // red is only for errors: amber when it gets low
    const NVGcolor color = percent < 20 ? t.warn : t.text;
    const NVGcolor outline = percent < 20 ? color : t.muted;
    const float bw = 30, bh = 15, bx = right - bw - 4, by = cy - bh / 2;
    StrokeRound(vg, bx, by, bw, bh, 4, outline, 2);
    FillRound(vg, bx + bw + 1, cy - 3, 3, 6, 1.5f, outline);
    const float fw = (bw - 6) * float(percent) / 100.f;
    if (fw > 0.5f) {
        FillRound(vg, bx + 3, by + 3, std::max(fw, 2.f), bh - 6, 1.5f, color);
    }
    if (charging) {
        // a bolt over the middle, outlined with the colour of the bar behind it
        const float mx = bx + bw / 2, my = cy;
        nvgBeginPath(vg);
        nvgMoveTo(vg, mx + 1.5f, my - 8.5f);
        nvgLineTo(vg, mx - 4.5f, my + 1.f);
        nvgLineTo(vg, mx - 0.5f, my + 1.f);
        nvgLineTo(vg, mx - 1.5f, my + 8.5f);
        nvgLineTo(vg, mx + 4.5f, my - 1.f);
        nvgLineTo(vg, mx + 0.5f, my - 1.f);
        nvgClosePath(vg);
        nvgStrokeColor(vg, t.bg);
        nvgStrokeWidth(vg, 2.5f);
        nvgStroke(vg);
        nvgFillColor(vg, color);
        nvgFill(vg);
    }
    char text[8];
    std::snprintf(text, sizeof(text), "%d %%", percent);
    const float tw = TextWidth(vg, 16, text);
    Text(vg, bx - 7, cy, 16, color, text, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    return bx - 7 - tw;
}

float Wifi(NVGcontext* vg, const Theme& t, float right, float cy, int strength) {
    // red stays for errors: no network is grey and crossed, a weak signal amber
    const NVGcolor lit = strength <= 0 ? t.muted : strength == 1 ? t.warn : t.work;
    NVGcolor dim = t.muted;
    dim.a = 0.28f;
    const float w = 26, cx = right - w / 2, base = cy + 8;
    const float radii[3] = {5.5f, 10.f, 14.5f};
    for (int i = 0; i < 3; i++) {
        nvgBeginPath(vg);
        nvgArc(vg, cx, base, radii[i], -NVG_PI * 0.78f, -NVG_PI * 0.22f, NVG_CW);
        nvgStrokeColor(vg, strength >= i + 1 ? lit : dim);
        nvgStrokeWidth(vg, 2.6f);
        nvgLineCap(vg, NVG_ROUND);
        nvgStroke(vg);
    }
    nvgBeginPath(vg);
    nvgCircle(vg, cx, base, 1.9f);
    nvgFillColor(vg, strength >= 1 ? lit : dim);
    nvgFill(vg);
    if (strength < 0) {
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx - 12, cy - 10);
        nvgLineTo(vg, cx + 12, cy + 10);
        nvgStrokeColor(vg, t.muted);
        nvgStrokeWidth(vg, 2.2f);
        nvgStroke(vg);
    }
    return right - w;
}

namespace {

// The icon of each tab (8A), 18 px, centred at (cx, cy): the library a grid, the shops a shop, the
// queue a numbered list, the installed games a tick in a circle, More three dots, Settings a gear.
void TabIcon(NVGcontext* vg, int tab, float cx, float cy, NVGcolor c) {
    nvgStrokeColor(vg, c);
    nvgFillColor(vg, c);
    nvgStrokeWidth(vg, 1.8f);
    nvgLineCap(vg, NVG_ROUND);
    nvgLineJoin(vg, NVG_ROUND);
    switch (tab) {
        case 0: // the library
            for (int i = 0; i < 4; i++) {
                nvgBeginPath(vg);
                nvgRoundedRect(vg, cx - 8 + (i % 2) * 9, cy - 8 + (i / 2) * 9, 7, 7, 1.8f);
                nvgFill(vg);
            }
            break;
        case 1: // a shop: its awning and its front
            nvgBeginPath(vg);
            nvgRoundedRect(vg, cx - 7, cy - 1, 14, 9, 1.5f);
            nvgStroke(vg);
            nvgBeginPath(vg);
            nvgMoveTo(vg, cx - 9, cy - 3);
            nvgLineTo(vg, cx - 7, cy - 8);
            nvgLineTo(vg, cx + 7, cy - 8);
            nvgLineTo(vg, cx + 9, cy - 3);
            nvgClosePath(vg);
            nvgFill(vg);
            nvgBeginPath(vg);
            nvgRect(vg, cx - 2, cy + 3, 4, 5);
            nvgFill(vg);
            break;
        case 2: // the queue
            for (int i = 0; i < 3; i++) {
                const float y = cy - 6 + i * 6;
                nvgBeginPath(vg);
                nvgCircle(vg, cx - 7, y, 1.6f);
                nvgFill(vg);
                nvgBeginPath(vg);
                nvgMoveTo(vg, cx - 3, y);
                nvgLineTo(vg, cx + 8, y);
                nvgStroke(vg);
            }
            break;
        case 3: // installed
            nvgBeginPath(vg);
            nvgCircle(vg, cx, cy, 8);
            nvgStroke(vg);
            CheckMark(vg, cx, cy + 0.5f, 9, c, 1.8f);
            break;
        case 4: // more
            for (int i = -1; i <= 1; i++) {
                nvgBeginPath(vg);
                nvgCircle(vg, cx + i * 6.5f, cy, 2);
                nvgFill(vg);
            }
            break;
        default: { // settings: a gear
            nvgSave(vg);
            nvgTranslate(vg, cx, cy);
            for (int i = 0; i < 8; i++) {
                nvgBeginPath(vg);
                nvgRoundedRect(vg, -1.8f, -8.6f, 3.6f, 4, 1);
                nvgFill(vg);
                nvgRotate(vg, NVG_PI / 4);
            }
            nvgRestore(vg);
            nvgBeginPath(vg);
            nvgCircle(vg, cx, cy, 5.6f);
            nvgStrokeWidth(vg, 2.6f);
            nvgStroke(vg);
            break;
        }
    }
}

} // namespace

float TopBar(NVGcontext* vg, const Theme& t, int tab, int queue_count, HitRect tabs_out[6], const char* sd_free, int battery,
             bool charging, int wifi, HitRect* wifi_hit, float sd_used) {
    // only the icon: the name of NXDock is on its opening and in the HOME menu (8A)
    Brand(vg, 36, 19, 34);
    const auto& s = Tr();
    const char* tabs[] = {s.library, TrM().t_shops, s.queue, s.installed, TrM().more_tab, s.settings};
    // Right side, in a capsule: the SD card, the WiFi, the battery and the clock (measured first).
    char clock[16];
    const time_t now = time(nullptr);
    const struct tm* lt = localtime(&now);
    std::snprintf(clock, sizeof(clock), "%02d:%02d", lt ? lt->tm_hour : 0, lt ? lt->tm_min : 0);
    char pct[8];
    std::snprintf(pct, sizeof(pct), "%d %%", std::clamp(battery, 0, 100));
    const float gap = 16, pad = 16;
    float inner = TextWidth(vg, 17, clock);
    if (battery >= 0) inner += gap + TextWidth(vg, 16, pct) + 7 + 34;
    if (wifi > -2) inner += gap + 26;
    const bool sd = sd_free && sd_free[0];
    if (sd) inner += gap + 12 + 7 + (sd_used >= 0 ? 38 + 7 : 0) + TextWidth(vg, 16, sd_free);
    const float cap_x = 1244 - inner - 2 * pad;
    // the tabs: when every name does not fit before the capsule (long languages), the tabs not
    // shown keep only their icon
    float names = 0;
    for (int i = 0; i < 6; i++) names += TextWidth(vg, 17, tabs[i]) + 58 + 4 + ((i == 2 && queue_count > 0) ? 32.f : 0.f);
    const bool compact = 114 + names + 30 > cap_x - 16;
    float x = 98;
    Text(vg, x, 37, 22, t.muted, glyph::L, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    x += 16;
    for (int i = 0; i < 6; i++) {
        const bool on = i == tab;
        const bool named = on || !compact;
        const float tw = named ? TextWidth(vg, 17, tabs[i]) : 0.f;
        const float badge = (i == 2 && queue_count > 0) ? 32.f : 0.f;
        const float w = named ? tw + 58 + badge : 46 + badge;
        if (on) {
            NVGcolor bgc = t.work;
            bgc.a = 0.16f;
            FillRound(vg, x, 18, w, 36, 18, bgc);
            // the line under the tab shown, with a little glow
            NVGcolor glow = t.work;
            glow.a = 0.45f;
            nvgBeginPath(vg);
            nvgRect(vg, x + 6, 44, w - 12, 20);
            nvgFillPaint(vg, nvgBoxGradient(vg, x + 18, 52, w - 36, 3, 1.5f, 8, glow, Hex(0, 0)));
            nvgFill(vg);
            FillRound(vg, x + 18, 52, w - 36, 3, 1.5f, t.work);
        }
        TabIcon(vg, i, x + (named ? 25 : 23), 36, on ? t.work : t.muted);
        if (named) Text(vg, x + 42, 36, 17, on ? t.text : t.muted, tabs[i]);
        if (badge > 0) {
            char n[8];
            std::snprintf(n, sizeof(n), "%d", queue_count);
            const float bx = named ? x + 42 + tw + 8 : x + 40;
            FillRound(vg, bx, 26, 24, 20, 10, t.work);
            Text(vg, bx + 12, 36, 13, t.on_work, n, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        }
        tabs_out[i] = {x - 2, 0, w + 4, 72, 0};
        x += w + 4;
    }
    Text(vg, x + 8, 37, 22, t.muted, glyph::R, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

    NVGcolor cap = t.panel2;
    cap.a = 0.8f;
    FillRound(vg, cap_x, 17, inner + 2 * pad, 38, 19, cap);
    StrokeRound(vg, cap_x + .5f, 17.5f, inner + 2 * pad - 1, 37, 18.5f, t.line, 1);
    float right = 1244 - pad;
    Text(vg, right, 36, 17, t.text, clock, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    right -= TextWidth(vg, 17, clock) + gap;
    if (battery >= 0) {
        right = Battery(vg, t, right, 36, battery, charging) - gap;
    }
    if (wifi > -2) {
        const float start = Wifi(vg, t, right, 36, wifi);
        if (wifi_hit) *wifi_hit = {start - 10, 0, right - start + 20, 72, 0};
        right = start - gap;
    }
    if (sd) {
        Text(vg, right, 36, 16, t.text, sd_free, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        right -= TextWidth(vg, 16, sd_free) + 7;
        if (sd_used >= 0) {
            // the part of the card in use
            NVGcolor track = t.work;
            track.a = 0.22f;
            FillRound(vg, right - 38, 32.5f, 38, 7, 3.5f, track);
            FillRound(vg, right - 38, 32.5f, std::max(7.f, 38 * std::clamp(sd_used, 0.f, 1.f)), 7, 3.5f,
                      sd_used > 0.95f ? t.warn : t.work);
            right -= 38 + 7;
        }
        // the card: its outline with the corner cut
        nvgBeginPath(vg);
        nvgMoveTo(vg, right - 12, 29);
        nvgLineTo(vg, right - 3, 29);
        nvgLineTo(vg, right, 32);
        nvgLineTo(vg, right, 44);
        nvgLineTo(vg, right - 12, 44);
        nvgClosePath(vg);
        nvgStrokeColor(vg, t.muted);
        nvgStrokeWidth(vg, 1.6f);
        nvgLineJoin(vg, NVG_ROUND);
        nvgStroke(vg);
    }

    nvgBeginPath(vg);
    nvgRect(vg, 0, 72, kWidth, 1);
    nvgFillColor(vg, t.line);
    nvgFill(vg);
    return cap_x;
}

void HintBar(NVGcontext* vg, const Theme& t, const char* left, std::initializer_list<Hint> hints,
             std::vector<HitRect>& hits) {
    HintBar(vg, t, left, std::vector<Hint>(hints), hits);
}

void HintBar(NVGcontext* vg, const Theme& t, const char* left, const std::vector<Hint>& hints,
             std::vector<HitRect>& hits) {
    FillRound(vg, 0, 664, kWidth, 56, 0, t.bg);
    nvgBeginPath(vg);
    nvgRect(vg, 0, 664, kWidth, 1);
    nvgFillColor(vg, t.line);
    nvgFill(vg);
    if (left) {
        Text(vg, 36, 692, 16, t.muted, left);
    }
    float x = 1244;
    for (auto it = hints.end(); it != hints.begin();) {
        --it;
        const float right = x;
        const float lw = TextWidth(vg, 16, it->label);
        Text(vg, x, 692, 16, t.muted, it->label, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        x -= lw + 10;
        const float gw = SymbolWidth(vg, 24, it->glyph);
        Symbol(vg, t, x - gw / 2, 692, 24, it->glyph, t.text, it->button);
        x -= gw;
        hits.push_back({x - 12, 664, right - x + 24, 56, it->button});
        x -= 26;
    }
}

} // namespace nxdock::ui
