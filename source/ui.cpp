// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "ui.hpp"

#include <cmath>
#include <cstdio>
#include <ctime>
#include "gfx.hpp"
#include "i18n.hpp"

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
           const char* format, bool installed, bool update, float radius) {
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

namespace {

// Brand mark: a tray with an arrow going in, as in the icon.
void BrandMark(NVGcontext* vg, const Theme& t, float x, float y, float s) {
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, s, s, s * 0.26f);
    nvgFillPaint(vg, nvgLinearGradient(vg, x, y, x + s, y + s, t.work, Hex(0x6a4ae0)));
    nvgFill(vg);
    nvgStrokeColor(vg, nvgRGB(255, 255, 255));
    nvgStrokeWidth(vg, s * 0.09f);
    nvgLineCap(vg, NVG_ROUND);
    nvgLineJoin(vg, NVG_ROUND);
    nvgBeginPath(vg);
    nvgMoveTo(vg, x + s * 0.26f, y + s * 0.55f);
    nvgLineTo(vg, x + s * 0.26f, y + s * 0.76f);
    nvgLineTo(vg, x + s * 0.74f, y + s * 0.76f);
    nvgLineTo(vg, x + s * 0.74f, y + s * 0.55f);
    nvgStroke(vg);
    nvgBeginPath(vg);
    nvgMoveTo(vg, x + s * 0.5f, y + s * 0.2f);
    nvgLineTo(vg, x + s * 0.5f, y + s * 0.6f);
    nvgMoveTo(vg, x + s * 0.36f, y + s * 0.47f);
    nvgLineTo(vg, x + s * 0.5f, y + s * 0.61f);
    nvgLineTo(vg, x + s * 0.64f, y + s * 0.47f);
    nvgStroke(vg);
}

} // namespace

void TopBar(NVGcontext* vg, const Theme& t, int tab, int queue_count, HitRect tabs_out[4]) {
    BrandMark(vg, t, 36, 19, 34);
    Text(vg, 80, 36, 24, t.text, "NXDock");

    const auto& s = Tr();
    const char* tabs[] = {s.library, s.queue, s.installed, s.settings};
    float x = 236;
    Text(vg, x - 10, 37, 22, t.muted, glyph::L, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    x += 14;
    for (int i = 0; i < 4; i++) {
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

    // Right side: free space on the SD card (sample value for now) and the clock.
    char clock[16];
    const time_t now = time(nullptr);
    const struct tm* lt = localtime(&now);
    std::snprintf(clock, sizeof(clock), "%02d:%02d", lt ? lt->tm_hour : 0, lt ? lt->tm_min : 0);
    Text(vg, 1244, 36, 17, t.text, clock, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    char sd[48];
    std::snprintf(sd, sizeof(sd), "SD  212 GB %s", s.free_space);
    Text(vg, 1180, 36, 16, t.muted, sd, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);

    nvgBeginPath(vg);
    nvgRect(vg, 0, 72, kWidth, 1);
    nvgFillColor(vg, t.line);
    nvgFill(vg);
}

void HintBar(NVGcontext* vg, const Theme& t, const char* left, std::initializer_list<Hint> hints,
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
        x -= lw + 8;
        const float gw = TextWidth(vg, 24, it->glyph);
        Text(vg, x, 693, 24, t.text, it->glyph, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        x -= gw;
        hits.push_back({x - 12, 664, right - x + 24, 56, it->button});
        x -= 26;
    }
}

} // namespace nxdock::ui
