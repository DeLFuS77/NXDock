// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

// Movement and the controller symbols (the animation pass, 10/11/2026, all its recommended
// options): how much moves (Settings › Animations), easing curves, rows and tiles that come in,
// a focus that glides, springy switches, the ABXY diamond, shoulder pills, the d-pad and the
// stick, and the slow aurora behind everything.

#include <nanovg.h>
#include <string>
#include "theme.hpp"

namespace nxdock::ui {

enum class MotionLevel { Full = 0, Gentle = 1, None = 2 };
void SetMotion(int level);
MotionLevel Motion();
inline bool Lively() { return Motion() == MotionLevel::Full; }
inline bool Still() { return Motion() == MotionLevel::None; }

// The clock of the frame (seconds), set once per frame.
void Tick(double now);
double Now();

// A button pressed this frame, for the pulse of its symbol in the hint bar.
void NotePress(unsigned long long down);
// 0..1: how much a symbol is pressed in now (0 when not).
float PressAmount(unsigned long long button);

// 0..1 over `dur` seconds after `since` (1 at once when nothing moves).
float Appear(double since, float dur);
float EaseOut(float x);     // cubic
float EaseOutBack(float x); // with a small overshoot (only when lively; else EaseOut)

// A value that glides to its target (the focus of a list).
struct Glide {
    float value = -1e9f;
    float Step(float target);
};

// A springy value 0..1 for a switch or a tick, kept by a name.
float Spring(const std::string& key, bool on);

// The symbol of a button, centred at (cx, cy), `size` like a font size: A/B/X/Y as the diamond
// of the four with the one lit, L/R/ZL/ZR as shoulder pills, the d-pad and the stick drawn, and
// the console font's glyph for the rest. `button` is its pad mask, for the press pulse. Returns
// its width.
float SymbolWidth(NVGcontext* vg, float size, const char* glyph);
void Symbol(NVGcontext* vg, const Theme& t, float cx, float cy, float size, const char* glyph, NVGcolor color, unsigned long long button);

namespace glyph {
inline constexpr const char* DPad = "\x01dpad";   // drawn: the cross
inline constexpr const char* Stick = "\x01stick"; // drawn: a stick that moves
} // namespace glyph

// The slow lights behind everything, blue and violet.
void Aurora(NVGcontext* vg, const Theme& t);
// The scene of the style, over the aurora and the light of the game in focus (runes; the moon,
// branches and embers; stars and the scan of the visor). Only full animations move it.
void Scene(NVGcontext* vg, const Theme& t);

} // namespace nxdock::ui
