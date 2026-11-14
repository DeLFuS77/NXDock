// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include <switch.h>
#include <nanovg.h>
#include <initializer_list>
#include <string>
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
// A title: the console's font has one weight, so it is drawn twice, half a pixel apart, to stand
// out from the text around it (11A). The sizes of the interface: 13 · 15 · 17 · 22 · 30 · 36.
float Title(NVGcontext* vg, float x, float y, float size, NVGcolor color, const char* str,
            int align = NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);

// A panel of glass (12A): a little see-through (the lights behind show), a thin light along its
// top edge and a soft shadow below.
void Glass(NVGcontext* vg, const Theme& t, float x, float y, float w, float h, float r);

// The mark of NXDock, as its icon. `card` is how far the game card has gone into the dock (1:
// all the way, as on the icon), `bar` how much of its light bar is lit and `lamp` its light, for
// the opening presentation (4A); all 1 draws the icon as it is.
void Brand(NVGcontext* vg, float x, float y, float s, float card = 1, float bar = 1, float lamp = 1);
// The dock alone (no background) in a square of side `s`, with what falls into its slot drawn by
// `inside` between its back and its front, cut at the slot (10A); `bar` lights its bar.
void Dock(NVGcontext* vg, float x, float y, float s, float bar, void (*inside)(NVGcontext*, void*), void* data);

void FillRound(NVGcontext* vg, float x, float y, float w, float h, float r, NVGcolor c);
void StrokeRound(NVGcontext* vg, float x, float y, float w, float h, float r, NVGcolor c, float width);

// Pill with text; returns its width.
float Chip(NVGcontext* vg, const Theme& t, float x, float y, const char* label, bool on);

// The focus ring with a soft glow that breathes.
void Focus(NVGcontext* vg, const Theme& t, float x, float y, float w, float h, float r, double time);

// A line of text in `w`: when it does not fit, it glides left and right, with a pause at each end,
// to show it whole (still, cut at the start, with the animations off). Gives where it ends.
float Marquee(NVGcontext* vg, float x, float y, float w, float size, NVGcolor color, const char* str, double time);

// A bar of work in progress: fill with moving stripes (still when done).
void Progress(NVGcontext* vg, const Theme& t, float x, float y, float w, float h, float frac,
              NVGcolor color, double time, bool moving = true);

// Sample cover: colour gradient with an initial; badges for format, installed and update.
void Cover(NVGcontext* vg, const Theme& t, float x, float y, float s, unsigned rgb, const char* initial,
           const char* format, bool installed, bool update, float radius = 10.f, int image = 0);

void Spinner(NVGcontext* vg, float cx, float cy, float r, NVGcolor c, double time);
void CheckMark(NVGcontext* vg, float cx, float cy, float s, NVGcolor c, float width);

// Draws the bar on top; `tabs` receives the area of each tab (the full bar height, easy to tap).
// A QR code of `text`, dark on a white tile of size s (also in the dark theme: phones read it
// better). Encoded once and kept while the text stays the same.
void Qr(NVGcontext* vg, float x, float y, float s, const std::string& text);

// battery: the charge in %, -1 when unknown (then nothing is drawn).
// wifi: the coverage 0..3, -1 without network, -2 not known yet (nothing drawn); its area goes
// to `wifi_hit` (taller than drawn: easy to tap).
// Each tab has its icon and its name, the one shown with a blue line under it (8A); on the right a
// capsule holds the SD card (a little bar of the part in use), the WiFi, the battery and the
// clock. `sd_used` is the part of the card in use (0..1), -1 when not known. Returns where the
// capsule starts.
// Every tab shows its icon and its name (2A); the light of the tab shown slides from `from_tab`
// as `slide` goes from 0 to 1.
float TopBar(NVGcontext* vg, const Theme& t, int tab, int queue_count, HitRect tabs[6], const char* sd_free,
            int battery = -1, bool charging = false, int wifi = -2, HitRect* wifi_hit = nullptr, float sd_used = -1,
            int from_tab = -1, float slide = 1);

// The WiFi as the fan of the HOME menu (1A): three arcs lit by the coverage, blue with signal,
// amber with one bar, grey and crossed without network. Drawn to the left of `right`; returns
// where it starts.
float Wifi(NVGcontext* vg, const Theme& t, float right, float cy, int strength);

// The battery with its %: filled by the level, amber below 20 %, red only below 10 %, a bolt
// while charging. Drawn to the left of `right`; returns where it starts.
float Battery(NVGcontext* vg, const Theme& t, float right, float cy, int percent, bool charging);
// Draws the hint bar; each hint adds its area to `hits` (the full bar height).
void HintBar(NVGcontext* vg, const Theme& t, const char* left, std::initializer_list<Hint> hints,
             std::vector<HitRect>& hits);
void HintBar(NVGcontext* vg, const Theme& t, const char* left, const std::vector<Hint>& hints,
             std::vector<HitRect>& hits);

} // namespace nxdock::ui
