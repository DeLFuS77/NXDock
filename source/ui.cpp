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
    NVGcolor glow = t.work;
    glow.a = 0.55f * pulse;
    NVGcolor clear = t.work;
    clear.a = 0;
    NVGpaint shadow = nvgBoxGradient(vg, x - 4, y - 4, w + 8, h + 8, r + 4, 24, glow, clear);
    nvgBeginPath(vg);
    nvgRect(vg, x - 40, y - 40, w + 80, h + 80);
    nvgRoundedRect(vg, x - 7, y - 7, w + 14, h + 14, r + 7);
    nvgPathWinding(vg, NVG_HOLE);
    nvgFillPaint(vg, shadow);
    nvgFill(vg);
    StrokeRound(vg, x - 5, y - 5, w + 10, h + 10, r + 5, t.work, 4);
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

// Brand mark, as the icon (assets/icon.svg): the dock of the console seen from the front, a game card
// going into its slot with an arrow on its label, a progress bar and the light on its front, on the
// blue and violet of NXDock. Drawn from the icon's own 256 px layout, scaled to `s`.
void BrandMark(NVGcontext* vg, const Theme& /*t*/, float x, float y, float s) {
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
    // the game card, its lower part already in the dock
    nvgSave(vg);
    nvgIntersectScissor(vg, x, y, s, 117 * k);
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
    // the dock: its back, the slot, its front with the step, the bar and the light
    rect(30, 110, 196, 118, 9, Hex(0x2c2c30));
    rect(70, 111, 116, 7, 3.5f, Hex(0x0d0d10));
    rect(30, 120, 196, 108, 9, Hex(0x232326));
    rect(38, 120, 180, 2, 1, Hex(0x6b6b72));
    rect(58, 196, 140, 12, 6, nvgRGBA(0, 0, 0, 90));
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x + 58 * k, y + 196 * k, 98 * k, 12 * k, 6 * k);
    nvgFillPaint(vg, nvgLinearGradient(vg, x + 58 * k, y, x + 156 * k, y, Hex(0x29d0ff), Hex(0x7c5cff)));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgCircle(vg, x + 212 * k, y + 216 * k, std::max(1.f, 5 * k));
    nvgFillColor(vg, Hex(0x3ee87a));
    nvgFill(vg);
}

} // namespace

float Battery(NVGcontext* vg, const Theme& t, float right, float cy, int percent, bool charging) {
    percent = std::clamp(percent, 0, 100);
    // red only when it is really low, as an error would be; amber a little before
    const NVGcolor color = percent < 10 ? t.err : percent < 20 ? t.warn : t.text;
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

void TopBar(NVGcontext* vg, const Theme& t, int tab, int queue_count, HitRect tabs_out[5], const char* sd_free, int battery,
            bool charging) {
    BrandMark(vg, t, 36, 19, 34);
    Text(vg, 80, 36, 24, t.text, "NXDock");

    const auto& s = Tr();
    const char* tabs[] = {s.library, s.queue, s.installed, TrM().more_tab, s.settings};
    float x = 236;
    Text(vg, x - 10, 37, 22, t.muted, glyph::L, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    x += 14;
    for (int i = 0; i < 5; i++) {
        const float tw = TextWidth(vg, 17, tabs[i]);
        const float badge = (i == 1 && queue_count > 0) ? 32.f : 0.f;
        const float w = tw + 32 + badge;
        if (i == tab) {
            NVGcolor bgc = t.work;
            bgc.a = 0.16f;
            FillRound(vg, x, 18, w, 36, 18, bgc);
        }
        Text(vg, x + 16, 36, 17, i == tab ? t.text : t.muted, tabs[i]);
        if (badge > 0) {
            char n[8];
            std::snprintf(n, sizeof(n), "%d", queue_count);
            FillRound(vg, x + 16 + tw + 8, 26, 24, 20, 10, t.work);
            Text(vg, x + 16 + tw + 20, 36, 13, t.on_work, n, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        }
        tabs_out[i] = {x - 3, 0, w + 6, 72, 0};
        x += w + 6;
    }
    Text(vg, x + 6, 37, 22, t.muted, glyph::R, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

    // Right side: free space on the SD card, the battery and the clock.
    char clock[16];
    const time_t now = time(nullptr);
    const struct tm* lt = localtime(&now);
    std::snprintf(clock, sizeof(clock), "%02d:%02d", lt ? lt->tm_hour : 0, lt ? lt->tm_min : 0);
    Text(vg, 1244, 36, 17, t.text, clock, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    float right = 1244 - TextWidth(vg, 17, clock) - 20;
    if (battery >= 0) {
        right = Battery(vg, t, right, 36, battery, charging) - 22;
    }
    if (sd_free && sd_free[0]) {
        char sd[64];
        std::snprintf(sd, sizeof(sd), "SD  %s %s", sd_free, s.free_space);
        Text(vg, right, 36, 16, t.muted, sd, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    }

    nvgBeginPath(vg);
    nvgRect(vg, 0, 72, kWidth, 1);
    nvgFillColor(vg, t.line);
    nvgFill(vg);
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
