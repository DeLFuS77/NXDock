// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "app.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include "i18n.hpp"

namespace nxdock {
namespace {

// Sample library until the app's catalogue arrives (I1.2). Names are made up.
struct SampleGame {
    const char* name;
    const char* initial;
    unsigned color;
    const char* format;
    const char* size;
    bool installed;
    bool update;
};

constexpr SampleGame kGames[] = {
    {"Lighthouse Tale", "L", 0xc8743a, "NSZ", "6.8 GB", true, true},
    {"Nebula Racers", "N", 0x2e7d6b, "XCZ", "7.0 GB", false, false},
    {"Midnight Rhythm", "M", 0x6a4ae0, "NSZ", "3.2 GB", false, false},
    {"Frozen Peaks", "F", 0x3a6fc8, "NSP", "1.9 GB", true, false},
    {"Secret Garden", "S", 0xb8406e, "NSZ", "2.4 GB", true, false},
    {"Ancient Woods", "A", 0x8a8f2a, "XCI", "11.2 GB", false, false},
    {"Robot Workshop", "R", 0x5a5f6e, "NSZ", "900 MB", false, false},
    {"Golden Cup", "G", 0xc49a2a, "NSZ", "4.6 GB", true, true},
    {"Tide Runner", "T", 0x2a8fb0, "NSP", "2.1 GB", false, false},
    {"Ember Knights", "E", 0x9a3a3a, "XCZ", "8.3 GB", false, false},
    {"Beetle Quest", "B", 0x4a7a3a, "NSZ", "1.2 GB", true, false},
    {"Moon Station", "M", 0x7a4ab0, "NSZ", "5.5 GB", false, false},
    {"Paper Pilots", "P", 0x2f8a8a, "NSZ", "700 MB", false, false},
    {"Quiet Harbour", "Q", 0x8a5a3a, "NSP", "3.9 GB", false, false},
    {"Crystal Caves", "C", 0x3a8ac8, "NSZ", "2.7 GB", false, false},
    {"Desert Drift", "D", 0xb88a3a, "XCZ", "6.1 GB", true, false},
    {"Owl Academy", "O", 0x6a5a9a, "NSZ", "1.4 GB", false, false},
    {"Pixel Picnic", "P", 0xc85a7a, "NSP", "450 MB", false, false},
    {"Storm Sailors", "S", 0x3a5a8a, "NSZ", "9.0 GB", false, true},
    {"Velvet Thief", "V", 0x8a3a6a, "NSZ", "3.3 GB", false, false},
    {"Winter Market", "W", 0x5a8aa0, "NSP", "1.1 GB", true, false},
};
constexpr int kGameCount = sizeof(kGames) / sizeof(kGames[0]);

// Library grid layout.
constexpr int kColumns = 7;
constexpr float kGridX = 36, kGridTop = 150, kGridGap = 20;
constexpr float kCoverSize = (kWidth - 2 * kGridX - kGridGap * (kColumns - 1)) / kColumns;
constexpr float kRowPitch = kCoverSize + kGridGap;
constexpr float kGridViewH = 2 * kRowPitch - kGridGap; // two full rows in sight
constexpr float kGridMargin = 10;                      // room for the focus glow, less than the gap

// Touch: how far a finger moves before a tap turns into a drag.
constexpr float kDragSlop = 14;

double Now() {
    return armTicksToNs(armGetSystemTick()) / 1e9;
}

void Background(NVGcontext* vg, const Theme& t) {
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, kWidth, kHeight);
    nvgFillColor(vg, t.bg);
    nvgFill(vg);
}

void Panel(NVGcontext* vg, const Theme& t, float x, float y, float w, float h, float r) {
    ui::FillRound(vg, x, y, w, h, r, t.panel);
    ui::StrokeRound(vg, x + .5f, y + .5f, w - 1, h - 1, r, t.line, 1);
}

} // namespace

bool App::Init() {
    InitStrings();
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&m_pad);
    hidInitializeTouchScreen();
    if (!m_gfx.Init()) {
        return false;
    }
    m_time = Now();
    return true;
}

void App::Exit() {
    m_gfx.Exit();
}

bool App::Repeat(u64 down, u64 held, u64 mask) {
    if (down & mask) {
        m_held_dir = down & mask;
        m_next_repeat = m_time + 0.35;
        return true;
    }
    if ((held & m_held_dir) && (m_held_dir & mask) && m_time >= m_next_repeat) {
        m_next_repeat = m_time + 0.09;
        return true;
    }
    return false;
}

float App::MaxScroll() const {
    const int rows = (kGameCount + kColumns - 1) / kColumns;
    return std::max(0.f, rows * kRowPitch - kGridGap - kGridViewH);
}

void App::KeepFocusVisible() {
    const float row_top = (m_focus / kColumns) * kRowPitch;
    if (row_top < m_scroll_target) {
        m_scroll_target = row_top;
    } else if (row_top + kCoverSize > m_scroll_target + kGridViewH) {
        m_scroll_target = row_top + kCoverSize - kGridViewH;
    }
    m_scroll_target = std::clamp(m_scroll_target, 0.f, MaxScroll());
}

void App::ReadTouch(u64& down) {
    HidTouchScreenState state{};
    hidGetTouchScreenStates(&state, 1);
    if (state.count > 0) {
        const float x = state.touches[0].x, y = state.touches[0].y;
        if (!m_touching) {
            m_touching = true;
            m_dragging = false;
            m_touch_x0 = x;
            m_touch_y0 = y;
            m_drag_scroll0 = m_scroll;
        }
        m_touch_x = x;
        m_touch_y = y;
        const bool in_grid = m_tab == Tab::Library && m_touch_y0 >= kGridTop - 10 && m_touch_y0 < kGridTop + kGridViewH;
        if (!m_dragging && std::fabs(y - m_touch_y0) > kDragSlop && in_grid) {
            m_dragging = true;
        }
        if (m_dragging) {
            // The list follows the finger.
            m_scroll = m_scroll_target = std::clamp(m_drag_scroll0 - (y - m_touch_y0), 0.f, MaxScroll());
        }
    } else if (m_touching) {
        m_touching = false;
        const bool moved = std::fabs(m_touch_x - m_touch_x0) > kDragSlop || std::fabs(m_touch_y - m_touch_y0) > kDragSlop;
        if (!m_dragging && !moved) {
            Tap(m_touch_x, m_touch_y, down);
        }
        m_dragging = false;
    }
}

void App::Tap(float x, float y, u64& down) {
    // Last drawn on top wins: walk backwards.
    for (auto it = m_hits.rbegin(); it != m_hits.rend(); ++it) {
        if (!it->rect.Contains(x, y)) {
            continue;
        }
        switch (it->kind) {
            case HitKind::Button: down |= it->rect.button; break;
            case HitKind::Tab: m_tab = Tab(it->index); break;
            case HitKind::Chip: m_filter = it->index; break;
            case HitKind::Cover:
                // First tap marks it, a second tap opens it (as A does).
                if (m_focus == it->index) {
                    down |= HidNpadButton_A;
                } else {
                    m_focus = it->index;
                    KeepFocusVisible();
                }
                break;
            case HitKind::QueueStrip: m_tab = Tab::Queue; break;
            case HitKind::ThemeRow: down |= HidNpadButton_A; break;
        }
        return;
    }
}

void App::Update(u64 down, u64 held) {
    if (down & HidNpadButton_Plus) {
        m_quit = true;
        return;
    }
    if (down & (HidNpadButton_L | HidNpadButton_ZL)) {
        m_tab = Tab((int(m_tab) + 3) % 4);
    }
    if (down & (HidNpadButton_R | HidNpadButton_ZR)) {
        m_tab = Tab((int(m_tab) + 1) % 4);
    }

    const u64 left = HidNpadButton_AnyLeft, right = HidNpadButton_AnyRight;
    const u64 up = HidNpadButton_AnyUp, dn = HidNpadButton_AnyDown;

    switch (m_tab) {
        case Tab::Library: {
            const int before = m_focus;
            if (Repeat(down, held, left) && m_focus % kColumns > 0) m_focus--;
            if (Repeat(down, held, right) && m_focus % kColumns < kColumns - 1 && m_focus + 1 < kGameCount) m_focus++;
            if (Repeat(down, held, up) && m_focus >= kColumns) m_focus -= kColumns;
            if (Repeat(down, held, dn) && m_focus + kColumns < kGameCount) m_focus += kColumns;
            if (m_focus != before) KeepFocusVisible();
            if (down & HidNpadButton_Y) m_filter = (m_filter + 1) % 4;
            if (down & HidNpadButton_X) m_tab = Tab::Queue;
            break;
        }
        case Tab::Settings:
            if (down & HidNpadButton_A) {
                m_theme = m_theme.dark ? LightTheme() : DarkTheme();
            }
            break;
        default:
            break;
    }
    if ((down & HidNpadButton_B) && m_tab != Tab::Library) {
        m_tab = Tab::Library;
    }
}

bool App::Frame() {
    if (!appletMainLoop() || m_quit) {
        return false;
    }
    const double now = Now();
    const float dt = float(std::min(0.1, now - m_time));
    m_time = now;

    padUpdate(&m_pad);
    u64 down = padGetButtonsDown(&m_pad);
    ReadTouch(down);
    Update(down, padGetButtons(&m_pad));

    // Ease the grid toward where it should be (frame-rate independent).
    if (!m_dragging) {
        m_scroll += (m_scroll_target - m_scroll) * (1.f - std::exp(-dt * 14.f));
        if (std::fabs(m_scroll_target - m_scroll) < 0.5f) m_scroll = m_scroll_target;
    }

    m_gfx.CheckResize();
    NVGcontext* vg = m_gfx.BeginFrame();
    Draw(vg);
    m_gfx.EndFrame();
    return !m_quit;
}

void App::AddHints(const std::vector<ui::HitRect>& rects) {
    for (const auto& r : rects) {
        m_hits.push_back({r, HitKind::Button, 0});
    }
}

void App::Draw(NVGcontext* vg) {
    m_hits.clear();
    m_hint_rects.clear();
    Background(vg, m_theme);
    ui::HitRect tabs[4];
    ui::TopBar(vg, m_theme, int(m_tab), 3, tabs);
    for (int i = 0; i < 4; i++) {
        m_hits.push_back({tabs[i], HitKind::Tab, i});
    }
    switch (m_tab) {
        case Tab::Library: DrawLibrary(vg); break;
        case Tab::Queue: DrawQueue(vg); break;
        case Tab::Settings: DrawSettings(vg); break;
        default: DrawComingSoon(vg); break;
    }
    AddHints(m_hint_rects);
    DrawTouchFeedback(vg);
}

void App::DrawTouchFeedback(NVGcontext* vg) {
    // While a finger rests on something, light it up so it is clear what will be pressed.
    if (!m_touching || m_dragging) {
        return;
    }
    for (auto it = m_hits.rbegin(); it != m_hits.rend(); ++it) {
        if (it->rect.Contains(m_touch_x, m_touch_y)) {
            NVGcolor c = m_theme.work;
            c.a = 0.18f;
            ui::FillRound(vg, it->rect.x, it->rect.y, it->rect.w, it->rect.h, 10, c);
            return;
        }
    }
}

void App::DrawLibrary(NVGcontext* vg) {
    const Theme& t = m_theme;
    const auto& s = Tr();

    // Filters.
    char all[32], ni[48], wu[48], dlc[32];
    std::snprintf(all, sizeof(all), "%s · 148", s.all);
    std::snprintf(ni, sizeof(ni), "%s · 37", s.not_installed);
    std::snprintf(wu, sizeof(wu), "%s · 6", s.with_update);
    std::snprintf(dlc, sizeof(dlc), "%s · 41", s.dlc);
    float x = kGridX;
    const char* chips[] = {all, ni, wu, dlc};
    for (int i = 0; i < 4; i++) {
        const float w = ui::Chip(vg, t, x, 92, chips[i], i == m_filter);
        AddHit(x - 4, 82, w + 8, 54, HitKind::Chip, i); // taller than drawn: easy to hit
        x += w + 10;
    }

    // Grid of covers, clipped to its viewport and scrolled.
    nvgSave(vg);
    nvgScissor(vg, 0, kGridTop - kGridMargin, kWidth, kGridViewH + 2 * kGridMargin);
    for (int i = 0; i < kGameCount; i++) {
        const float cx = kGridX + (i % kColumns) * (kCoverSize + kGridGap);
        const float cy = kGridTop + (i / kColumns) * kRowPitch - m_scroll;
        if (cy + kCoverSize < kGridTop - kGridMargin || cy > kGridTop + kGridViewH + kGridMargin) {
            continue;
        }
        if (i == m_focus) {
            ui::Focus(vg, t, cx, cy, kCoverSize, kCoverSize, 10, m_time);
        }
        const auto& g = kGames[i];
        ui::Cover(vg, t, cx, cy, kCoverSize, g.color, g.initial, g.format, g.installed, g.update);
        // Only the visible part of a cover can be tapped.
        const float top = std::max(cy, kGridTop - kGridMargin), bottom = std::min(cy + kCoverSize, kGridTop + kGridViewH + kGridMargin);
        if (bottom > top) {
            AddHit(cx - kGridGap / 2, top, kCoverSize + kGridGap, bottom - top, HitKind::Cover, i);
        }
    }
    nvgRestore(vg);

    // Scroll indicator when there are more rows than fit.
    if (MaxScroll() > 0) {
        const float track_h = kGridViewH;
        const float thumb_h = track_h * kGridViewH / (kGridViewH + MaxScroll());
        const float thumb_y = kGridTop + (track_h - thumb_h) * (m_scroll / MaxScroll());
        NVGcolor c = t.muted;
        c.a = 0.45f;
        ui::FillRound(vg, kWidth - 16, thumb_y, 4, thumb_h, 2, c);
    }

    // Name of the game in focus.
    const auto& g = kGames[m_focus];
    const float ny = kGridTop + kGridViewH + 22;
    const float nw = ui::Text(vg, kGridX, ny, 22, t.text, g.name) - kGridX;
    char facts[96];
    std::snprintf(facts, sizeof(facts), "%s · %s", g.format, g.size);
    ui::Text(vg, kGridX + nw + 16, ny + 1, 16, t.muted, facts);

    // Queue strip, always in sight; tapping it opens the queue.
    const float qy = 556;
    Panel(vg, t, kGridX, qy, kWidth - 2 * kGridX, 54, 12);
    AddHit(kGridX, qy, kWidth - 2 * kGridX, 54, HitKind::QueueStrip);
    ui::Cover(vg, t, 54, qy + 10, 34, 0x2e7d6b, "N", nullptr, false, false, 7);
    char now[96];
    std::snprintf(now, sizeof(now), "%s Nebula Racers", s.installing);
    const float tw = ui::TextWidth(vg, 16, now);
    ui::Text(vg, 104, qy + 27, 16, t.text, now);
    char more[48];
    std::snprintf(more, sizeof(more), " · 2 %s", s.more_in_queue);
    ui::Text(vg, 104 + tw, qy + 27, 16, t.muted, more);
    const float frac = float(std::fmod(m_time * 0.02, 1.0));
    ui::Progress(vg, t, 520, qy + 23, 400, 8, frac, t.work, m_time);
    char pct[64];
    std::snprintf(pct, sizeof(pct), "%d %% · 86 MB/s", int(frac * 100));
    ui::Text(vg, 940, qy + 27, 16, t.muted, pct);
    ui::Text(vg, 1222, qy + 28, 24, t.text, ui::glyph::X, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

    ui::Text(vg, kGridX, 638, 14, t.muted, s.preview_note);

    ui::HintBar(vg, t, "192.168.1.10",
                {{ui::glyph::A, s.view, HidNpadButton_A}, {ui::glyph::X, s.queue, HidNpadButton_X},
                 {ui::glyph::Y, s.search, HidNpadButton_Y}, {ui::glyph::Plus, s.exit, HidNpadButton_Plus}},
                m_hint_rects);
}

void App::DrawQueue(NVGcontext* vg) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    const float frac = float(std::fmod(m_time * 0.02, 1.0));

    // The game being installed, with its four stages.
    const float px = 48, py = 98, pw = kWidth - 96, ph = 236;
    Panel(vg, t, px, py, pw, ph, 16);
    ui::Cover(vg, t, px + 24, py + 24, 120, 0x2e7d6b, "N", nullptr, false, false, 10);
    const float cx = px + 168;
    const float nw = ui::Text(vg, cx, py + 40, 24, t.text, "Nebula Racers") - cx;
    ui::Text(vg, cx + nw + 14, py + 41, 16, t.muted, s.base_update_sd);
    char pct[16];
    std::snprintf(pct, sizeof(pct), "%d %%", int(frac * 100));
    ui::Text(vg, px + pw - 24, py + 40, 26, t.text, pct, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    char eta[96];
    const int left_s = int((1 - frac) * 240);
    std::snprintf(eta, sizeof(eta), "%.1f / 7.0 GB · %s %d min %02d s", frac * 7.0, s.left, left_s / 60, left_s % 60);
    ui::Text(vg, cx, py + 70, 16, t.muted, eta);
    ui::Progress(vg, t, cx, py + 88, pw - 168 - 24, 12, frac, t.work, m_time);

    struct Lane { const char* title; NVGcolor color; const char* value; const char* unit; float meter; };
    char v_net[16], v_dec[16], v_wr[16], v_hash[16], dec_title[64], hash_unit[32];
    const float wob = float(std::sin(m_time * 1.7));
    std::snprintf(v_net, sizeof(v_net), "%d", 86 + int(wob * 6));
    std::snprintf(v_dec, sizeof(v_dec), "%d", 142 + int(wob * 9));
    std::snprintf(v_wr, sizeof(v_wr), "%d", 91 + int(wob * 4));
    const int parts = std::min(1 + int(frac * 5), 5);
    std::snprintf(v_hash, sizeof(v_hash), "%d", parts);
    std::snprintf(dec_title, sizeof(dec_title), "%s · 3 %s", s.decompress, s.threads);
    std::snprintf(hash_unit, sizeof(hash_unit), "%s 5", s.parts_of);
    const Lane lanes[] = {
        {s.network, t.work, v_net, "MB/s", 0.72f + wob * 0.05f},
        {dec_title, t.violet, v_dec, "MB/s", -1},
        {s.write_sd, t.orange, v_wr, "MB/s", 0.64f - wob * 0.05f},
        {s.check_sha, t.ok, v_hash, hash_unit, parts / 5.f},
    };
    const float lw = (pw - 168 - 24 - 3 * 14) / 4;
    for (int i = 0; i < 4; i++) {
        const float lx = cx + i * (lw + 14), ly = py + 118;
        ui::FillRound(vg, lx, ly, lw, 92, 12, t.panel2);
        ui::StrokeRound(vg, lx + .5f, ly + .5f, lw - 1, 91, 12, t.line, 1);
        nvgBeginPath(vg);
        nvgCircle(vg, lx + 18, ly + 20, 5);
        nvgFillColor(vg, lanes[i].color);
        nvgFill(vg);
        ui::Text(vg, lx + 30, ly + 20, 14, t.muted, lanes[i].title);
        const float vw = ui::Text(vg, lx + 14, ly + 48, 22, t.text, lanes[i].value) - (lx + 14);
        ui::Text(vg, lx + 14 + vw + 6, ly + 50, 14, t.muted, lanes[i].unit);
        if (lanes[i].meter >= 0) {
            NVGcolor track = t.text;
            track.a = 0.10f;
            ui::FillRound(vg, lx + 14, ly + 70, lw - 28, 6, 3, track);
            ui::FillRound(vg, lx + 14, ly + 70, (lw - 28) * lanes[i].meter, 6, 3, lanes[i].color);
        } else {
            // One segment per decompression thread; the fourth core is kept free.
            const float sw = (lw - 28 - 12) / 4;
            for (int k = 0; k < 4; k++) {
                NVGcolor c = lanes[i].color;
                c.a = k < 3 ? 1.f : 0.25f;
                ui::FillRound(vg, lx + 14 + k * (sw + 4), ly + 70, sw, 6, 3, c);
            }
        }
    }

    // Waiting and done.
    float y = py + ph + 26;
    ui::Text(vg, 62, y, 13, t.muted, s.in_queue);
    y += 22;
    struct Row { const char* name; const char* initial; unsigned color; const char* info; };
    const Row rows[] = {
        {"Midnight Rhythm", "M", 0x6a4ae0, "XCZ · 3.2 GB · SD"},
        {"Secret Garden · DLC", "S", 0xb8406e, "NSZ · 380 MB · SD"},
    };
    for (const auto& r : rows) {
        nvgGlobalAlpha(vg, 0.55f);
        ui::Cover(vg, t, 62, y, 44, r.color, r.initial, nullptr, false, false, 8);
        ui::Text(vg, 122, y + 22, 17, t.text, r.name);
        ui::Text(vg, 1010, y + 22, 15, t.muted, r.info, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        ui::Text(vg, 1218, y + 22, 15, t.muted, s.waiting, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgGlobalAlpha(vg, 1.f);
        y += 54;
    }
    y += 12;
    ui::Text(vg, 62, y + 8, 13, t.muted, s.done);
    y += 30;
    ui::Cover(vg, t, 62, y, 44, 0x3a6fc8, "F", nullptr, false, false, 8);
    ui::Text(vg, 122, y + 22, 17, t.text, "Frozen Peaks");
    ui::Text(vg, 1010, y + 22, 15, t.muted, "NSP · 1.9 GB · SD · 42 s", NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    const float cw = ui::TextWidth(vg, 15, s.checked);
    ui::Text(vg, 1218, y + 22, 15, t.ok, s.checked, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    nvgBeginPath(vg);
    nvgCircle(vg, 1218 - cw - 14, y + 22, 9);
    nvgFillColor(vg, t.ok);
    nvgFill(vg);
    ui::CheckMark(vg, 1218 - cw - 14, y + 22, 10, t.panel, 2.2f);

    ui::HintBar(vg, t, s.no_sleep,
                {{ui::glyph::Y, s.pause, HidNpadButton_Y}, {ui::glyph::X, s.remove, HidNpadButton_X},
                 {ui::glyph::B, s.back, HidNpadButton_B}},
                m_hint_rects);
}

void App::DrawSettings(NVGcontext* vg) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    const float x = 48, y = 110, w = 560;
    Panel(vg, t, x, y, w, 76, 14);
    ui::Focus(vg, t, x, y, w, 76, 14, m_time);
    AddHit(x, y, w, 76, HitKind::ThemeRow);
    ui::Text(vg, x + 24, y + 38, 19, t.text, s.theme);
    ui::Text(vg, x + w - 24, y + 38, 19, t.work, t.dark ? s.dark : s.light, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    ui::Text(vg, x, y + 112, 15, t.muted, s.preview_note);
    ui::HintBar(vg, t, nullptr, {{ui::glyph::A, s.change, HidNpadButton_A}, {ui::glyph::B, s.back, HidNpadButton_B}},
                m_hint_rects);
}

void App::DrawComingSoon(NVGcontext* vg) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    ui::Spinner(vg, kWidth / 2, 330, 18, t.work, m_time);
    ui::Text(vg, kWidth / 2, 384, 20, t.muted, s.coming_soon, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    ui::HintBar(vg, t, nullptr, {{ui::glyph::B, s.back, HidNpadButton_B}}, m_hint_rects);
}

} // namespace nxdock
