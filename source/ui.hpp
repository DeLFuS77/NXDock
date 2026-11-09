// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include <switch.h>
#include <nanovg.h>
#include <initializer_list>
#include <vector>
#include "theme.hpp"

namespace nxdock::ui {

// Button glyphs from the console's NintendoExt font.
namespace glyph {
inline constexpr const char* A = "\xEE\x83\xA0";     // U+E0E0
inline constexpr const char* B = "\xEE\x83\xA1";     // U+E0E1
inline constexpr const char* X = "\xEE\x83\xA2";     // U+E0E2
inline constexpr const char* Y = "\xEE\x83\xA3";     // U+E0E3
inline constexpr const char* L = "\xEE\x83\xA4";     // U+E0E4
inline constexpr const char* R = "\xEE\x83\xA5";     // U+E0E5
inline constexpr const char* ZL = "\xEE\x83\xA6";    // U+E0E6
inline constexpr const char* ZR = "\xEE\x83\xA7";    // U+E0E7
inline constexpr const char* Plus = "\xEE\x83\xAF";  // U+E0EF
inline constexpr const char* Minus = "\xEE\x83\xB0"; // U+E0F0
} // namespace glyph

// A button in the hint bar. Tapping it does the same as pressing `button`.
struct Hint {
    const char* glyph;
    const char* label;
    u64 button;
};

// A place on screen that answers to touch, in 1280x720 coordinates.
struct HitRect {
    float x, y, w, h;
    u64 button; // pressed when tapped (0: the caller decides)
    bool Contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

float Text(NVGcontext* vg, float x, float y, float size, NVGcolor color, const char* str,
           int align = NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
float TextWidth(NVGcontext* vg, float size, const char* str);

void FillRound(NVGcontext* vg, float x, float y, float w, float h, float r, NVGcolor c);
void StrokeRound(NVGcontext* vg, float x, float y, float w, float h, float r, NVGcolor c, float width);

// Pill with text; returns its width.
float Chip(NVGcontext* vg, const Theme& t, float x, float y, const char* label, bool on);

// The focus ring with a soft glow that breathes.
void Focus(NVGcontext* vg, const Theme& t, float x, float y, float w, float h, float r, double time);

// A bar of work in progress: fill with moving stripes (still when done).
void Progress(NVGcontext* vg, const Theme& t, float x, float y, float w, float h, float frac,
              NVGcolor color, double time, bool moving = true);

// Sample cover: colour gradient with an initial; badges for format, installed and update.
void Cover(NVGcontext* vg, const Theme& t, float x, float y, float s, unsigned rgb, const char* initial,
           const char* format, bool installed, bool update, float radius = 10.f, int image = 0);

void Spinner(NVGcontext* vg, float cx, float cy, float r, NVGcolor c, double time);
void CheckMark(NVGcontext* vg, float cx, float cy, float s, NVGcolor c, float width);

// Draws the bar on top; `tabs` receives the area of each tab (the full bar height, easy to tap).
void TopBar(NVGcontext* vg, const Theme& t, int tab, int queue_count, HitRect tabs[4], const char* sd_free);
// Draws the hint bar; each hint adds its area to `hits` (the full bar height).
void HintBar(NVGcontext* vg, const Theme& t, const char* left, std::initializer_list<Hint> hints,
             std::vector<HitRect>& hits);

} // namespace nxdock::ui
