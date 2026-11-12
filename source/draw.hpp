// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

// Small drawing helpers shared by the screens of the More tab (more.cpp, vault.cpp, space.cpp…).
#include "gfx.hpp"
#include "theme.hpp"
#include "motion.hpp"
#include "ui.hpp"

#include <algorithm>
#include <cstdio>
#include <string>

namespace nxdock::draw {

inline void Panel(NVGcontext* vg, const Theme& t, float x, float y, float w, float h, float r) {
    ui::Glass(vg, t, x, y, w, h, r); // glass, with a soft shadow (12A)
}

// A menu that opens over the screen: glass too, but nearly solid, so what is behind does not
// get in the way of its rows.
inline void Popup(NVGcontext* vg, const Theme& t, float x, float y, float w, float h, float r) {
    ui::Glass(vg, t, x, y, w, h, r);
    NVGcolor solid = t.panel;
    solid.a = 0.92f;
    ui::FillRound(vg, x, y, w, h, r, solid);
}

inline float BoxHeight(NVGcontext* vg, float w, float size, const char* str) {
    nvgFontFace(vg, "ui");
    nvgFontSize(vg, size);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgTextLineHeight(vg, 1.3f);
    float b[4];
    nvgTextBoxBounds(vg, 0, 0, w, str, nullptr, b);
    return b[3] - b[1];
}

inline float Box(NVGcontext* vg, float x, float y, float w, float size, NVGcolor color, const char* str) {
    nvgFontFace(vg, "ui");
    nvgFontSize(vg, size);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgTextLineHeight(vg, 1.3f);
    nvgFillColor(vg, color);
    float b[4];
    nvgTextBoxBounds(vg, x, y, w, str, nullptr, b);
    nvgTextBox(vg, x, y, w, str, nullptr);
    return b[3] - b[1];
}

inline std::string Size(unsigned long long bytes) {
    char t[32];
    if (bytes >= 1024ULL * 1024 * 1024) {
        std::snprintf(t, sizeof(t), "%.1f GB", double(bytes) / (1024.0 * 1024 * 1024));
    } else if (bytes >= 1024ULL * 1024) {
        std::snprintf(t, sizeof(t), "%.0f MB", double(bytes) / (1024.0 * 1024));
    } else {
        std::snprintf(t, sizeof(t), "%.0f KB", double(bytes) / 1024.0);
    }
    return t;
}

// A section title, small and in capitals (as the visual tests show them).
inline void Section(NVGcontext* vg, const Theme& t, float x, float y, const char* text) {
    std::string up = text;
    for (auto& c : up) {
        if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    }
    ui::Text(vg, x, y, 13, t.muted, up.c_str());
}

// A folder (orange) or a file (outline), small, for lists.
inline void FolderIcon(NVGcontext* vg, const Theme& t, float x, float cy) {
    ui::FillRound(vg, x, cy - 6, 10, 4, 1.5f, t.orange);
    ui::FillRound(vg, x, cy - 4, 22, 14, 2.5f, t.orange);
}

inline void FileIcon(NVGcontext* vg, const Theme& t, float x, float cy) {
    ui::StrokeRound(vg, x + 3, cy - 9, 16, 19, 2.5f, t.muted, 1.6f);
    ui::FillRound(vg, x + 6, cy - 3, 10, 1.6f, 0.8f, t.muted);
    ui::FillRound(vg, x + 6, cy + 2, 10, 1.6f, 0.8f, t.muted);
}

// A tick box, filled in the blue of work when marked; with a key, it pops and its tick is drawn.
inline void TickBox(NVGcontext* vg, const Theme& t, float x, float cy, bool on, const std::string& key = {}) {
    const float k = key.empty() ? (on ? 1.f : 0.f) : ui::Spring(key, on);
    ui::StrokeRound(vg, x + 1, cy - 11, 22, 22, 6, t.muted, 2);
    if (k <= 0.01f) return;
    const float s = 24 * std::max(0.f, k);
    ui::FillRound(vg, x + 12 - s / 2, cy - s / 2, s, s, 6, t.work);
    nvgSave(vg);
    nvgGlobalAlpha(vg, std::min(1.f, k));
    ui::CheckMark(vg, x + 12, cy, 12 * std::min(1.2f, k), t.on_work, 2.4f);
    nvgRestore(vg);
}

// An on/off switch (10A); with a key its knob jumps over with a spring (4A).
inline void Toggle(NVGcontext* vg, const Theme& t, float x, float cy, bool on, const std::string& key = {}) {
    const float k = key.empty() ? (on ? 1.f : 0.f) : ui::Spring(key, on);
    const float f = std::clamp(k, 0.f, 1.f);
    ui::FillRound(vg, x, cy - 15, 52, 30, 15, t.panel2);
    ui::StrokeRound(vg, x + .5f, cy - 14.5f, 51, 29, 15, t.line, 1);
    NVGcolor fill = t.work;
    fill.a = f;
    ui::FillRound(vg, x, cy - 15, 52, 30, 15, fill);
    nvgBeginPath(vg);
    nvgCircle(vg, x + 15 + 22 * k, cy, 10);
    nvgFillColor(vg, nvgLerpRGBA(t.muted, t.on_work, f));
    nvgFill(vg);
}

} // namespace nxdock::draw
