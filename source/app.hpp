// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include <switch.h>
#include <vector>
#include "gfx.hpp"
#include "theme.hpp"
#include "ui.hpp"

namespace nxdock {

enum class Tab { Library, Queue, Installed, Settings };

class App {
public:
    bool Init();
    void Exit();
    // Returns false when the user asks to leave.
    bool Frame();

private:
    // Everything on screen that answers to touch, rebuilt every frame.
    enum class HitKind { Button, Tab, Chip, Cover, QueueStrip, ThemeRow };
    struct Hit {
        ui::HitRect rect;
        HitKind kind;
        int index;
    };

    void ReadTouch(u64& down);
    void Tap(float x, float y, u64& down);
    void Update(u64 down, u64 held);
    void Draw(NVGcontext* vg);
    void DrawLibrary(NVGcontext* vg);
    void DrawQueue(NVGcontext* vg);
    void DrawSettings(NVGcontext* vg);
    void DrawComingSoon(NVGcontext* vg);
    void DrawTouchFeedback(NVGcontext* vg);

    // Repeats a held direction like the HOME menu does.
    bool Repeat(u64 down, u64 held, u64 mask);
    // Library grid scrolling.
    void KeepFocusVisible();
    float MaxScroll() const;

    void AddHit(float x, float y, float w, float h, HitKind kind, int index = 0) {
        m_hits.push_back({{x, y, w, h, 0}, kind, index});
    }
    void AddHints(const std::vector<ui::HitRect>& rects);

    Gfx m_gfx;
    PadState m_pad{};
    Theme m_theme = DarkTheme();
    Tab m_tab = Tab::Library;
    int m_focus = 0;
    int m_filter = 0;
    double m_time = 0;
    double m_next_repeat = 0;
    u64 m_held_dir = 0;
    bool m_quit = false;

    // Library grid scroll, in pixels: drawn value eases toward the target.
    float m_scroll = 0, m_scroll_target = 0;

    // Touch state.
    std::vector<Hit> m_hits;
    bool m_touching = false;
    bool m_dragging = false;
    float m_touch_x0 = 0, m_touch_y0 = 0, m_touch_x = 0, m_touch_y = 0;
    float m_drag_scroll0 = 0;
    std::vector<ui::HitRect> m_hint_rects;
};

} // namespace nxdock
