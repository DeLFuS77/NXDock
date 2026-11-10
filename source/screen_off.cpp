// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The screen off while installing (11A of the visual test of phases 9 to 15): asked for with −
// on the queue or the tile in More (or, when turned on in Settings, when a long queue starts),
// a notice counts three seconds and the backlight goes off. Any button or touch lights it
// again; the end of the queue lights it by itself. When docked, only the console's own screen
// goes off.
#include "app.hpp"

#include "draw.hpp"
#include "i18n.hpp"
#include "i18n_more.hpp"

#include <cstdio>

namespace nxdock {
namespace {

constexpr double kCount = 3.0;
constexpr unsigned long long kLongQueue = 2ull * 1024 * 1024 * 1024;

} // namespace

void App::AskScreenOff() {
    if (!m_queue || !m_queue->Busy()) {
        Notice(TrM().so_nothing);
        return;
    }
    m_screen_off_at = m_time;
}

void App::ScreenOn() {
    if (m_screen_off && m_lbl) lblSwitchBacklightOn(200'000'000ull);
    m_screen_off = false;
    m_screen_off_at = -100;
}

bool App::FollowScreenOff(u64& down, bool new_touch) {
    const bool input = down || new_touch;
    // a long queue starting, when asked for in Settings
    const bool busy = m_queue && m_queue->Busy();
    if (busy && !m_screen_busy_before && m_ui.screen_off_auto) {
        unsigned long long bytes = 0;
        for (const auto& j : m_queue->Jobs()) {
            if (j.state == InstallJob::State::Waiting || j.state == InstallJob::State::Running) bytes += j.size;
        }
        if (bytes >= kLongQueue) m_screen_off_at = m_time;
    }
    m_screen_busy_before = busy;
    if (m_screen_off) {
        // lit again by any input (which does nothing else), and at the end of the queue
        if (input || !busy) {
            ScreenOn();
            m_wake_touch = new_touch;
            down = 0;
            return true;
        }
        return false;
    }
    if (m_screen_off_at < 0) return false;
    if (input && m_time - m_screen_off_at > 0.3) {
        // the countdown stopped
        m_screen_off_at = -100;
        m_wake_touch = new_touch;
        down = 0;
        return true;
    }
    if (m_time - m_screen_off_at >= kCount) {
        if (!m_lbl) m_lbl = R_SUCCEEDED(lblInitialize());
        if (m_lbl) lblSwitchBacklightOff(500'000'000ull);
        m_screen_off = true;
    }
    return false;
}

void App::DrawScreenOff(NVGcontext* vg) {
    if (m_screen_off) {
        // without the backlight service, black is the next best
        if (!m_lbl) {
            nvgBeginPath(vg);
            nvgRect(vg, 0, 0, kWidth, kHeight);
            nvgFillColor(vg, nvgRGBAf(0, 0, 0, 1));
            nvgFill(vg);
        }
        return;
    }
    if (m_screen_off_at < 0) return;
    const Theme& t = m_theme;
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, kWidth, kHeight);
    nvgFillColor(vg, nvgRGBAf(0, 0, 0, 0.6f));
    nvgFill(vg);
    const float w = 640, h = 260, x = (kWidth - w) / 2, y = (kHeight - h) / 2;
    draw::Panel(vg, t, x, y, w, h, 18);
    const int left = int(kCount - (m_time - m_screen_off_at)) + 1;
    char n[8];
    std::snprintf(n, sizeof(n), "%d", std::max(1, std::min(3, left)));
    const float frac = float(std::min(1.0, (m_time - m_screen_off_at) / kCount));
    NVGcolor track = t.work;
    track.a = 0.18f;
    nvgBeginPath(vg);
    nvgCircle(vg, kWidth / 2, y + 74, 52);
    nvgStrokeColor(vg, track);
    nvgStrokeWidth(vg, 8);
    nvgStroke(vg);
    nvgBeginPath(vg);
    nvgArc(vg, kWidth / 2, y + 74, 52, -1.5708f, -1.5708f + 6.2832f * (1 - frac), NVG_CW);
    nvgStrokeColor(vg, t.work);
    nvgLineCap(vg, NVG_ROUND);
    nvgStroke(vg);
    ui::Text(vg, kWidth / 2, y + 74, 52, t.work, n, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFontFace(vg, "ui");
    nvgFontSize(vg, 17);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
    nvgTextLineHeight(vg, 1.35f);
    nvgFillColor(vg, t.muted);
    nvgTextBox(vg, x + 40, y + 140, w - 80, TrM().so_body, nullptr);
}

} // namespace nxdock
