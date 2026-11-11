// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The first-run wizard (6A of the visual test of phase 6), as the library app's: a picture on the
// left, the step on the right, dots and Back / Next, and its soft bossa that can be turned off.
// Shown only the first time; every step can be skipped; it is repeated from Settings.
//  1. welcome: the language (the console's, or another) and the theme;
//  2. the library: the usual pairing, then whether to share what is installed;
//  3. where to install (the internal memory always asks, every time) and what a page ticks;
//  4. notices: the end of the queue, sounds, little battery, updates at start;
//  5. saver mode, and the speed test if wanted;
//  6. done, and a walk of ten seconds through Library, Queue and Installed.
#include "app.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include "i18n.hpp"
#include "i18n_more.hpp"

namespace nxdock {
namespace {

constexpr int kSteps = 6;
constexpr float kArtW = 400;                       // the picture on the left
constexpr float kBodyX = kArtW + 52, kBodyW = kWidth - kBodyX - 52;
constexpr float kTileH = 56, kTileGap = 10, kToggleH = 52;
constexpr float kButtonsY = 584, kButtonH = 52;
constexpr double kTourStop = 3.3;                  // each tab of the tour, in seconds

void Panel(NVGcontext* vg, const Theme& t, float x, float y, float w, float h, float r) {
    ui::FillRound(vg, x, y, w, h, r, t.panel);
    ui::StrokeRound(vg, x + .5f, y + .5f, w - 1, h - 1, r, t.line, 1);
}

float TextBox(NVGcontext* vg, float x, float y, float w, float size, NVGcolor color, const char* str) {
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

// The pictures of the steps, drawn: a sparkle, a link, an SD card, a bell, a moon, a tick.
void Sparkle(NVGcontext* vg, float cx, float cy, float r) {
    nvgBeginPath(vg);
    for (int i = 0; i < 8; i++) {
        const float a = float(i) * 3.14159265f / 4 - 3.14159265f / 2;
        const float d = i % 2 == 0 ? r : r * 0.28f;
        const float x = cx + std::cos(a) * d, y = cy + std::sin(a) * d;
        if (i == 0) nvgMoveTo(vg, x, y);
        else nvgLineTo(vg, x, y);
    }
    nvgClosePath(vg);
    nvgFill(vg);
}

void StepPicture(NVGcontext* vg, int step, float cx, float cy, double time) {
    const NVGcolor white = nvgRGB(255, 255, 255);
    nvgFillColor(vg, white);
    nvgStrokeColor(vg, white);
    nvgStrokeWidth(vg, 7);
    nvgLineCap(vg, NVG_ROUND);
    nvgLineJoin(vg, NVG_ROUND);
    switch (step) {
        case 0: {
            const float twinkle = 0.85f + 0.15f * float(std::sin(time * 2.2));
            Sparkle(vg, cx - 8, cy + 6, 46 * twinkle);
            Sparkle(vg, cx + 38, cy - 34, 18 / twinkle);
            Sparkle(vg, cx + 34, cy + 40, 11);
            break;
        }
        case 1: {
            // three nodes linked: the console, the library, a phone
            const float pts[3][2] = {{cx - 40, cy + 26}, {cx + 40, cy + 26}, {cx, cy - 38}};
            nvgBeginPath(vg);
            nvgMoveTo(vg, pts[0][0], pts[0][1]);
            nvgLineTo(vg, pts[1][0], pts[1][1]);
            nvgLineTo(vg, pts[2][0], pts[2][1]);
            nvgClosePath(vg);
            nvgStrokeWidth(vg, 5);
            nvgStroke(vg);
            for (const auto& p : pts) {
                nvgBeginPath(vg);
                nvgCircle(vg, p[0], p[1], 15);
                nvgFill(vg);
            }
            break;
        }
        case 2: {
            // an SD card: a cut corner and its contacts
            const float w = 64, h = 84, x = cx - w / 2, y = cy - h / 2;
            nvgBeginPath(vg);
            nvgMoveTo(vg, x + 18, y);
            nvgLineTo(vg, x + w - 8, y);
            nvgQuadTo(vg, x + w, y, x + w, y + 8);
            nvgLineTo(vg, x + w, y + h - 8);
            nvgQuadTo(vg, x + w, y + h, x + w - 8, y + h);
            nvgLineTo(vg, x + 8, y + h);
            nvgQuadTo(vg, x, y + h, x, y + h - 8);
            nvgLineTo(vg, x, y + 18);
            nvgClosePath(vg);
            nvgFill(vg);
            nvgFillColor(vg, nvgRGBAf(0.25f, 0.2f, 0.6f, 0.55f));
            for (int i = 0; i < 4; i++) {
                ui::FillRound(vg, x + 16 + i * 11, y + 10, 6, 18, 2, nvgRGBAf(0.25f, 0.2f, 0.6f, 0.55f));
            }
            break;
        }
        case 3: {
            // a bell that swings a little
            nvgSave(vg);
            nvgTranslate(vg, cx, cy - 40);
            nvgRotate(vg, 0.12f * float(std::sin(time * 3)));
            nvgBeginPath(vg);
            nvgMoveTo(vg, -38, 52);
            nvgBezierTo(vg, -30, 40, -30, 30, -30, 14);
            nvgBezierTo(vg, -30, -8, -14, -16, 0, -16);
            nvgBezierTo(vg, 14, -16, 30, -8, 30, 14);
            nvgBezierTo(vg, 30, 30, 30, 40, 38, 52);
            nvgClosePath(vg);
            nvgFill(vg);
            nvgBeginPath(vg);
            nvgCircle(vg, 0, 62, 9);
            nvgFill(vg);
            nvgBeginPath(vg);
            nvgCircle(vg, 0, -20, 6);
            nvgFill(vg);
            nvgRestore(vg);
            break;
        }
        case 4: {
            // a crescent moon: a circle less another one, by the two arcs where they meet
            const float r1 = 44, r2 = 38, ox = 26, oy = -20;
            const float d = std::sqrt(ox * ox + oy * oy);
            const float a = (r1 * r1 - r2 * r2 + d * d) / (2 * d);
            const float h = std::sqrt(std::max(0.f, r1 * r1 - a * a));
            const float ux = ox / d, uy = oy / d;
            const float px = a * ux, py = a * uy;
            const float p1x = px - h * uy, p1y = py + h * ux, p2x = px + h * uy, p2y = py - h * ux;
            const float a1 = std::atan2(p1y, p1x), a2 = std::atan2(p2y, p2x);
            const float b1 = std::atan2(p1y - oy, p1x - ox), b2 = std::atan2(p2y - oy, p2x - ox);
            nvgBeginPath(vg);
            nvgArc(vg, cx, cy, r1, a1, a2, NVG_CW);
            nvgArc(vg, cx + ox, cy + oy, r2, b2, b1, NVG_CCW);
            nvgClosePath(vg);
            nvgFill(vg);
            Sparkle(vg, cx + 40, cy - 46, 9);
            Sparkle(vg, cx + 54, cy + 4, 6);
            break;
        }
        default:
            ui::CheckMark(vg, cx, cy + 4, 88, white, 13);
            break;
    }
}

// A small music note, for the button of the music.
void NoteIcon(NVGcontext* vg, float x, float y, NVGcolor c) {
    nvgBeginPath(vg);
    nvgEllipse(vg, x - 3, y + 6, 5, 4);
    nvgFillColor(vg, c);
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgMoveTo(vg, x + 1.5f, y + 6);
    nvgLineTo(vg, x + 1.5f, y - 9);
    nvgLineTo(vg, x + 8, y - 5);
    nvgStrokeColor(vg, c);
    nvgStrokeWidth(vg, 2.2f);
    nvgStroke(vg);
}

const int kBatteryLevels[] = {10, 15, 20, 30};

} // namespace

void App::OpenWizard() {
    m_wizard = true;
    m_wiz_step = 0;
    m_wiz_group = 0;
    m_wiz_item = 0;
    m_wiz_step_at = m_time;
    m_menu = false;
    m_detail = false;
    if (m_ui.wizard_music) {
        m_sound.Music(true);
    }
}

void App::CloseWizard(bool tour) {
    m_wizard = false;
    m_sound.Music(false);
    m_ui.wizard_done = true;
    m_ui.Save();
    m_tab = Tab::Library;
    if (tour) {
        m_tour_at = m_time;
    }
}

void App::ApplyTheme(int mode) {
    // 0 light, 1 dark, 2 as the console
    bool light = mode == 0;
    if (mode == 2) {
        ColorSetId color = ColorSetId_Light;
        if (R_SUCCEEDED(setsysInitialize())) {
            if (R_FAILED(setsysGetColorSetId(&color))) color = ColorSetId_Light;
            setsysExit();
        }
        light = color != ColorSetId_Dark;
    }
    m_ui.theme_auto = mode == 2;
    m_ui.Save();
    m_theme = light ? LightTheme() : DarkTheme();
    m_link.SaveTheme(light);
}

std::vector<App::WizGroup> App::WizardGroups() {
    const auto& s = Tr();
    std::vector<WizGroup> groups;
    const auto toggle = [](const char* label, bool on) { return WizItem{label, "", on, WizKind::Toggle}; };
    switch (m_wiz_step) {
        case 0: {
            WizGroup lang{s.wiz_language, {}, 6}; // two rows, so the theme and the animations fit below
            lang.items.push_back({s.wiz_console_lang, LanguageName(ConsoleLanguage()), m_ui.language < 0, WizKind::Choice});
            for (int i = 0; i < LanguageCount(); i++) {
                lang.items.push_back({LanguageName(i), "", m_ui.language == i, WizKind::Choice});
            }
            groups.push_back(std::move(lang));
            const int theme = m_ui.theme_auto ? 2 : m_theme.dark ? 1 : 0;
            groups.push_back({s.wiz_theme, {{s.light, "", theme == 0, WizKind::Choice}, {s.dark, "", theme == 1, WizKind::Choice},
                                            {s.wiz_theme_auto, "", theme == 2, WizKind::Choice}}, 3});
            const More& m = TrM();
            groups.push_back({m.anim_row, {{m.anim_full, "", m_ui.anim == 0, WizKind::Choice}, {m.anim_soft, "", m_ui.anim == 1, WizKind::Choice},
                                           {m.anim_none, "", m_ui.anim == 2, WizKind::Choice}}, 3});
            break;
        }
        case 1:
            if (m_link.Snapshot().state == Link::State::Connected) {
                groups.push_back({s.wiz_share_q, {{s.share_yes, "", m_prefs.share_installed == 1, WizKind::Choice},
                                                  {s.no_word, "", m_prefs.share_installed == 0, WizKind::Choice}}, 2});
            }
            break;
        case 2:
            groups.push_back({"", {{s.sd_card, s.wiz_recommended, m_ui.dest != 2, WizKind::Choice},
                                   {s.wiz_ask_each, "", m_ui.dest == 2, WizKind::Choice}}, 2});
            groups.push_back({s.wiz_pick_label, {{s.wiz_pick_missing, s.wiz_pick_missing_sub, m_ui.pick == 0, WizKind::Choice},
                                                 {s.wiz_pick_base, "", m_ui.pick == 1, WizKind::Choice},
                                                 {s.wiz_pick_base_update, "", m_ui.pick == 2, WizKind::Choice}}, 3});
            break;
        case 3: {
            groups.push_back({"", {toggle(s.wiz_finish_toggle, m_ui.finish_effect), toggle(s.sounds_row, m_ui.sounds),
                                   toggle(s.wiz_battery_toggle, m_ui.battery_notice)}, 1});
            WizGroup level{s.wiz_battery_from, {}, 4};
            for (int l : kBatteryLevels) {
                char text[16];
                std::snprintf(text, sizeof(text), "%d %%", l);
                level.items.push_back({text, "", m_ui.battery_level == l, WizKind::Choice});
            }
            level.small = true;
            groups.push_back(std::move(level));
            groups.push_back({"", {toggle(s.wiz_updates_toggle, m_ui.check_updates)}, 1});
            break;
        }
        case 4:
            groups.push_back({"", {toggle(s.wiz_saver_toggle, m_ui.saver)}, 1});
            groups.push_back({"", {toggle(TrM().wiz_screen_off, m_ui.screen_off_auto)}, 1});
            groups.push_back({"", {{s.wiz_speed_row, m_speed.Running() ? s.cancel : s.start, false, WizKind::Action}}, 1});
            break;
        default: break;
    }
    // Back (or Skip on the first step) and Next (or Start on the last one), at the bottom
    groups.push_back({"", {{m_wiz_step == 0 ? s.wiz_skip : s.back, "", false, WizKind::Button},
                           {m_wiz_step == kSteps - 1 ? s.wiz_start : s.wiz_next, "", true, WizKind::Button}}, 2});
    return groups;
}

void App::WizardGo(int step) {
    m_wiz_step = std::clamp(step, 0, kSteps - 1);
    m_wiz_step_at = m_time;
    // the focus on the first choice of the step, or on Next when it has none
    const auto groups = WizardGroups();
    m_wiz_group = groups.size() > 1 ? 0 : int(groups.size()) - 1;
    m_wiz_item = groups.size() > 1 ? 0 : 1;
}

void App::ActivateWizard(int group, int item) {
    auto groups = WizardGroups();
    if (group < 0 || group >= int(groups.size())) {
        return;
    }
    if (group == int(groups.size()) - 1) {
        // the buttons
        if (item == 0) {
            if (m_wiz_step == 0) CloseWizard(false); // Skip: the wizard ends here
            else WizardGo(m_wiz_step - 1);
        } else if (m_wiz_step == kSteps - 1) {
            CloseWizard(true);
        } else {
            WizardGo(m_wiz_step + 1);
        }
        return;
    }
    switch (m_wiz_step) {
        case 0:
            if (group == 0) {
                m_ui.language = item == 0 ? -1 : item - 1;
                UseLanguage(m_ui.language);
                m_ui.Save();
            } else if (group == 1) {
                ApplyTheme(item);
            } else {
                // the change shows at once
                m_ui.anim = std::clamp(item, 0, 2);
                ui::SetMotion(m_ui.anim);
                m_ui.Save();
            }
            break;
        case 1:
            m_prefs.share_installed = item == 0 ? 1 : 0;
            m_prefs.Save();
            if (m_prefs.share_installed == 0) {
                m_push->SendInventory("{\"off\":true}");
            }
            m_inventory_sent = ~0u;
            break;
        case 2:
            if (group == 0) m_ui.dest = item == 1 ? 2 : 0;
            else m_ui.pick = item;
            m_ui.Save();
            break;
        case 3:
            if (group == 0) {
                if (item == 0) m_ui.finish_effect = !m_ui.finish_effect;
                if (item == 1) m_ui.sounds = !m_ui.sounds;
                if (item == 2) m_ui.battery_notice = !m_ui.battery_notice;
            } else if (group == 1) {
                m_ui.battery_level = kBatteryLevels[std::clamp(item, 0, 3)];
                m_ui.battery_notice = true;
            } else {
                m_ui.check_updates = !m_ui.check_updates;
            }
            m_ui.Save();
            break;
        case 4:
            if (group == 0) {
                m_ui.saver = !m_ui.saver;
                m_ui.Save();
            } else if (group == 1) {
                m_ui.screen_off_auto = !m_ui.screen_off_auto;
                m_ui.Save();
            } else if (m_speed.Running()) {
                m_speed.Cancel();
            } else {
                const Config c = m_link.CurrentConfig();
                if (c.Linked()) {
                    m_speed_kept = false;
                    m_speed.Start(c.url, c.key);
                }
            }
            break;
        default: break;
    }
}

void App::UpdateWizard(u64 down, u64 held) {
    if (down & HidNpadButton_Minus) {
        m_ui.wizard_music = !m_ui.wizard_music;
        m_ui.Save();
        m_sound.Music(m_ui.wizard_music);
    }
    if (down & HidNpadButton_R) {
        ActivateWizard(int(WizardGroups().size()) - 1, 1); // Next (or Start)
        return;
    }
    if ((down & (HidNpadButton_L | HidNpadButton_B)) && m_wiz_step > 0) {
        WizardGo(m_wiz_step - 1);
        return;
    }
    // the library, not connected yet: the pairing screen as it always is
    if (m_wiz_step == 1 && m_link.Snapshot().state != Link::State::Connected) {
        UpdatePairing(down, held, m_link.Snapshot());
        return;
    }
    const auto groups = WizardGroups();
    m_wiz_group = std::clamp(m_wiz_group, 0, int(groups.size()) - 1);
    const int n = int(groups[m_wiz_group].items.size());
    const int cols = groups[m_wiz_group].cols;
    m_wiz_item = std::clamp(m_wiz_item, 0, n - 1);
    if (Repeat(down, held, HidNpadButton_AnyLeft) && m_wiz_item % cols > 0) m_wiz_item--;
    if (Repeat(down, held, HidNpadButton_AnyRight) && m_wiz_item % cols < cols - 1 && m_wiz_item + 1 < n) m_wiz_item++;
    if (Repeat(down, held, HidNpadButton_AnyUp)) {
        if (m_wiz_item - cols >= 0) {
            m_wiz_item -= cols;
        } else if (m_wiz_group > 0) {
            m_wiz_group--;
            const auto& g = groups[m_wiz_group];
            const int lines = (int(g.items.size()) + g.cols - 1) / g.cols;
            m_wiz_item = std::min(int(g.items.size()) - 1, (lines - 1) * g.cols + std::min(m_wiz_item % cols, g.cols - 1));
        }
    }
    if (Repeat(down, held, HidNpadButton_AnyDown)) {
        if (m_wiz_item + cols < n) {
            m_wiz_item += cols;
        } else if (m_wiz_group < int(groups.size()) - 1) {
            m_wiz_group++;
            const bool buttons = m_wiz_group == int(groups.size()) - 1;
            m_wiz_item = buttons ? 1 : std::min(int(groups[m_wiz_group].items.size()) - 1, m_wiz_item % cols);
        }
    }
    if (down & HidNpadButton_A) {
        ActivateWizard(m_wiz_group, m_wiz_item);
    }
}

void App::DrawWizard(NVGcontext* vg) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    const Link::View link = m_link.Snapshot();
    const bool pairing = m_wiz_step == 1 && link.state != Link::State::Connected;
    char step_text[48];
    std::snprintf(step_text, sizeof(step_text), s.wiz_step, m_wiz_step + 1, kSteps);
    const char* titles[] = {s.wiz_welcome_title, s.wiz_connect_title, s.wiz_where_title, s.wiz_notices_title, s.wiz_saver_title, s.wiz_done_title};
    const char* bodies[] = {s.wiz_welcome_body, s.wiz_connect_body, s.wiz_where_body, s.wiz_notices_body, s.wiz_saver_body, s.wiz_done_body};
    // a new step comes in softly
    const float in = float(std::min(1.0, (m_time - m_wiz_step_at) / 0.25));

    // the music: a button on the picture (or on top while pairing), and − anywhere
    const auto music_width = [&]() {
        return 44 + ui::TextWidth(vg, 16, s.wiz_music) + 10 + ui::TextWidth(vg, 20, ui::glyph::Minus) + 16;
    };
    const auto music_button = [&](float x, float y) {
        // a pill: a note (crossed out when off), the word, and the button that changes it
        const bool on = m_ui.wizard_music;
        const float w = music_width(), h = 36;
        ui::FillRound(vg, x, y, w, h, h / 2, on ? t.work : t.panel2);
        if (!on) ui::StrokeRound(vg, x + .5f, y + .5f, w - 1, h - 1, h / 2, t.line, 1);
        const NVGcolor fg = on ? t.on_work : t.muted;
        NoteIcon(vg, x + 22, y + h / 2, fg);
        if (!on) {
            nvgBeginPath(vg);
            nvgMoveTo(vg, x + 13, y + h / 2 + 10);
            nvgLineTo(vg, x + 33, y + h / 2 - 10);
            nvgStrokeColor(vg, fg);
            nvgStrokeWidth(vg, 2);
            nvgStroke(vg);
        }
        const float tx = ui::Text(vg, x + 44, y + h / 2, 16, fg, s.wiz_music);
        ui::Text(vg, tx + 10, y + h / 2 + 1, 20, fg, ui::glyph::Minus);
        AddHit(x - 6, y - 6, w + 12, h + 12, HitKind::WizMusic);
    };

    if (pairing) {
        // the step on top, then the pairing screen as it always is
        ui::Text(vg, 48, 30, 14, t.muted, step_text);
        ui::Text(vg, 48, 60, 26, t.text, titles[1]);
        music_button(kWidth - 48 - music_width(), 30);
        {
            // without the library app: what to do instead
            const float nx = 440, nw = kWidth - 48 - music_width() - 24 - nx, ny = 22;
            if (nw > 200) {
                NVGcolor edge = t.work;
                edge.a = 0.55f;
                ui::StrokeRound(vg, nx, ny, nw, 50, 10, edge, 1.2f);
                nvgSave(vg);
                nvgScissor(vg, nx + 8, ny + 4, nw - 16, 42);
                TextBox(vg, nx + 14, ny + 7, nw - 28, 13, t.muted, TrM().wiz_no_app);
                nvgRestore(vg);
            }
        }
        DrawPairing(vg, link);
        m_hint_rects.clear();
        const bool code_shown = link.state == Link::State::Waiting || link.state == Link::State::Expired || link.state == Link::State::Refused;
        ui::HintBar(vg, t, nullptr,
                    {{ui::glyph::A, s.choose, HidNpadButton_A},
                     {ui::glyph::X, code_shown ? s.new_code : s.search_again, HidNpadButton_X},
                     {ui::glyph::L, s.back, HidNpadButton_L},
                     {ui::glyph::R, s.wiz_skip, HidNpadButton_R}},
                    m_hint_rects);
        return;
    }

    // the picture: a soft gradient, an orbit of dots, the blob with the picture of the step
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, kArtW, 664);
    nvgFillPaint(vg, nvgLinearGradient(vg, 0, 0, kArtW, 664, nvgLerpRGBA(t.bg, t.work, 0.22f), nvgLerpRGBA(t.bg, Hex(0x6a4ae0), 0.26f)));
    nvgFill(vg);
    NVGcolor glow = t.work;
    glow.a = 0.35f;
    NVGcolor clear = t.work;
    clear.a = 0;
    const float acx = kArtW / 2, acy = 320;
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, kArtW, 664);
    nvgFillPaint(vg, nvgRadialGradient(vg, acx - 30, acy - 40, 20, 260, glow, clear));
    nvgFill(vg);
    for (int i = 0; i < 36; i++) {
        const float a = float(i) * 6.2831853f / 36 + float(m_time * 0.15);
        nvgBeginPath(vg);
        nvgCircle(vg, acx + std::cos(a) * 150, acy + std::sin(a) * 150, i % 3 == 0 ? 2.6f : 1.6f);
        nvgFillColor(vg, nvgRGBAf(1, 1, 1, 0.35f));
        nvgFill(vg);
    }
    const float bob = 6 * float(std::sin(m_time * 1.6));
    const float bs = 190 * (0.92f + 0.08f * in);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, acx - bs / 2 - 6, acy - bs / 2 + 22 + bob, bs + 12, bs, bs * 0.46f);
    nvgFillPaint(vg, nvgBoxGradient(vg, acx - bs / 2, acy - bs / 2 + 26 + bob, bs, bs, bs * 0.46f, 40, nvgRGBAf(0, 0, 0, 0.35f), nvgRGBAf(0, 0, 0, 0)));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, acx - bs / 2, acy - bs / 2 + bob, bs, bs, bs * 0.46f);
    nvgFillPaint(vg, nvgLinearGradient(vg, acx - bs / 2, acy - bs / 2, acx + bs / 2, acy + bs / 2, t.work, Hex(0x6a4ae0)));
    nvgFill(vg);
    nvgGlobalAlpha(vg, in);
    StepPicture(vg, m_wiz_step, acx, acy + bob, m_time);
    nvgGlobalAlpha(vg, 1.f);
    music_button(24, 22);

    // the step
    nvgGlobalAlpha(vg, in);
    const float slide = 24 * (1 - in) * (1 - in);
    float y = 64;
    ui::Text(vg, kBodyX + slide, y, 15, t.muted, step_text);
    y += 40;
    nvgSave(vg);
    nvgScissor(vg, kBodyX, y - 26, kBodyW, 52);
    ui::Text(vg, kBodyX + slide, y, 32, t.text, titles[m_wiz_step]);
    nvgRestore(vg);
    y += 30;
    y += TextBox(vg, kBodyX + slide, y, kBodyW, 17, t.muted, bodies[m_wiz_step]) + 18;
    if (m_wiz_step == 1) {
        // connected: to which library, with a tick
        const Config c = m_link.CurrentConfig();
        const size_t scheme = c.url.find("://");
        const std::string host = c.url.substr(scheme == std::string::npos ? 0 : scheme + 3);
        char text[200];
        std::snprintf(text, sizeof(text), s.wiz_connected, (c.name.empty() ? link.app_name : c.name).c_str());
        ui::FillRound(vg, kBodyX, y, kBodyW, 52, 12, t.panel2);
        nvgBeginPath(vg);
        nvgCircle(vg, kBodyX + 28, y + 26, 11);
        nvgFillColor(vg, t.ok);
        nvgFill(vg);
        ui::CheckMark(vg, kBodyX + 28, y + 26, 12, t.panel, 2.4f);
        const float tw = ui::Text(vg, kBodyX + 50, y + 26, 17, t.text, text) - (kBodyX + 50);
        ui::Text(vg, kBodyX + 50 + tw + 14, y + 27, 15, t.muted, host.c_str());
        y += 52 + 22;
    }

    const auto groups = WizardGroups();
    m_wiz_group = std::clamp(m_wiz_group, 0, int(groups.size()) - 1);
    for (int gi = 0; gi < int(groups.size()) - 1; gi++) {
        const WizGroup& g = groups[gi];
        float gx = kBodyX;
        if (!g.title.empty()) {
            if (g.small) {
                // a short label on the left of small tiles, on one line
                ui::Text(vg, kBodyX + 18, y + kTileH / 2, 16, t.muted, g.title.c_str());
                gx = kBodyX + 18 + ui::TextWidth(vg, 16, g.title.c_str()) + 18;
            } else {
                y += TextBox(vg, kBodyX, y, kBodyW, 15, t.muted, g.title.c_str()) + 10;
            }
        }
        const bool toggles = !g.items.empty() && (g.items[0].kind == WizKind::Toggle || g.items[0].kind == WizKind::Action);
        const float ih = toggles ? kToggleH : kTileH;
        const float iw = g.small ? 96 : (kBodyW - (g.cols - 1) * kTileGap) / g.cols;
        for (int ii = 0; ii < int(g.items.size()); ii++) {
            const WizItem& it = g.items[ii];
            const float x = gx + (ii % g.cols) * (iw + kTileGap);
            const float iy = y + (ii / g.cols) * (ih + (toggles ? 8 : kTileGap));
            const bool focus = gi == m_wiz_group && ii == m_wiz_item;
            const bool dim = it.kind == WizKind::Action && !m_link.CurrentConfig().Linked();
            nvgGlobalAlpha(vg, in * (dim ? 0.45f : 1.f));
            NVGcolor fill = t.panel2;
            if (it.kind == WizKind::Choice && it.on) fill = nvgLerpRGBA(t.panel2, t.work, 0.18f);
            ui::FillRound(vg, x, iy, iw, ih, 12, fill);
            ui::StrokeRound(vg, x + .5f, iy + .5f, iw - 1, ih - 1, 12, it.kind == WizKind::Choice && it.on ? t.work : t.line, it.on ? 1.5f : 1);
            if (focus) ui::Focus(vg, t, x, iy, iw, ih, 12, m_time);
            AddHit(x, iy, iw, ih, HitKind::WizItem, gi * 100 + ii);
            nvgSave(vg);
            nvgIntersectScissor(vg, x, iy, iw - (toggles ? 90 : 8), ih);
            if (it.kind == WizKind::Choice) {
                // a label longer than its tile gets a little smaller, never cut
                const float room = iw - 18 - 10;
                const float lw = ui::TextWidth(vg, 17, it.label.c_str());
                const float fs = lw > room ? std::max(13.f, 17 * room / lw) : 17.f;
                if (it.sub.empty()) {
                    ui::Text(vg, x + 18, iy + ih / 2, fs, t.text, it.label.c_str());
                } else {
                    ui::Text(vg, x + 18, iy + 19, fs, t.text, it.label.c_str());
                    ui::Text(vg, x + 18, iy + 40, 13, t.muted, it.sub.c_str());
                }
            } else {
                ui::Text(vg, x + 18, iy + ih / 2, 17, t.text, it.label.c_str());
            }
            nvgRestore(vg);
            if (it.kind == WizKind::Toggle) {
                // a switch on the right
                const float sw = 46, sh = 26, sx = x + iw - 18 - sw, sy = iy + (ih - sh) / 2;
                ui::FillRound(vg, sx, sy, sw, sh, sh / 2, it.on ? t.work : t.line);
                nvgBeginPath(vg);
                nvgCircle(vg, it.on ? sx + sw - 13 : sx + 13, sy + 13, 10);
                nvgFillColor(vg, nvgRGB(255, 255, 255));
                nvgFill(vg);
            } else if (it.kind == WizKind::Action) {
                ui::Text(vg, x + iw - 18, iy + ih / 2, 15, t.work, it.sub.c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            }
            nvgGlobalAlpha(vg, 1.f);
        }
        const int lines = (int(g.items.size()) + g.cols - 1) / g.cols;
        y += lines * (ih + (toggles ? 8 : kTileGap)) + (toggles ? 4 : 10);
    }
    nvgGlobalAlpha(vg, in);
    if (m_wiz_step == 3) {
        // fixed, only said: what the web sends always asks first
        y += TextBox(vg, kBodyX + 4, y + 4, kBodyW - 8, 15, t.muted, s.wiz_push_info) + 8;
    } else if (m_wiz_step == 4) {
        const SpeedTest::View v = m_speed.Snapshot();
        if (!m_link.CurrentConfig().Linked()) {
            TextBox(vg, kBodyX + 4, y + 4, kBodyW - 8, 15, t.muted, s.wiz_speed_need);
        } else if (m_speed.Running()) {
            ui::Progress(vg, t, kBodyX, y + 10, kBodyW, 8, v.progress, t.work, m_time);
        } else if (v.stage == SpeedTest::Stage::Done) {
            char text[200];
            std::snprintf(text, sizeof(text), "%s %.1f MB/s  ·  %s %.1f  ·  %s %.1f  ·  %s %.1f MB/s", s.conn1, v.net[0], s.conn2, v.net[1],
                          s.conn4, v.net[2], s.sd_write, v.sd);
            TextBox(vg, kBodyX + 4, y + 4, kBodyW - 8, 15, t.text, text);
        } else if (v.stage == SpeedTest::Stage::Failed) {
            TextBox(vg, kBodyX + 4, y + 4, kBodyW - 8, 15, t.err, v.error.c_str());
        }
    }
    nvgGlobalAlpha(vg, 1.f);

    // the dots of the steps, and the buttons
    for (int i = 0; i < kSteps; i++) {
        const float dx = kBodyX + i * 20 + (i > m_wiz_step ? 18 : 0);
        if (i == m_wiz_step) {
            ui::FillRound(vg, dx, kButtonsY + kButtonH / 2 - 5, 28, 10, 5, t.work);
        } else {
            nvgBeginPath(vg);
            nvgCircle(vg, dx + 5, kButtonsY + kButtonH / 2, 5);
            nvgFillColor(vg, i < m_wiz_step ? nvgLerpRGBA(t.line, t.work, 0.5f) : t.line);
            nvgFill(vg);
        }
    }
    const WizGroup& buttons = groups.back();
    float bx = kWidth - 52;
    for (int i = 1; i >= 0; i--) {
        const WizItem& b = buttons.items[i];
        const char* glyph = i == 1 ? ui::glyph::R : (m_wiz_step == 0 ? nullptr : ui::glyph::L);
        const float lw = ui::TextWidth(vg, 18, b.label.c_str());
        const float bw = lw + (glyph ? 78 : 48);
        bx -= bw;
        const bool focus = m_wiz_group == int(groups.size()) - 1 && m_wiz_item == i;
        if (focus) ui::Focus(vg, t, bx, kButtonsY, bw, kButtonH, 12, m_time);
        if (b.on) {
            ui::FillRound(vg, bx, kButtonsY, bw, kButtonH, 12, t.work);
        } else {
            ui::FillRound(vg, bx, kButtonsY, bw, kButtonH, 12, t.panel2);
            ui::StrokeRound(vg, bx + 1, kButtonsY + 1, bw - 2, kButtonH - 2, 12, t.line, 2);
        }
        const NVGcolor fg = b.on ? t.on_work : t.text;
        if (glyph) {
            ui::Text(vg, bx + 22, kButtonsY + kButtonH / 2 + 1, 24, fg, glyph, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
            ui::Text(vg, bx + 56, kButtonsY + kButtonH / 2, 18, fg, b.label.c_str());
        } else {
            ui::Text(vg, bx + bw / 2, kButtonsY + kButtonH / 2, 18, fg, b.label.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        }
        AddHit(bx, kButtonsY, bw, kButtonH, HitKind::WizButton, i);
        bx -= 14;
    }

    std::vector<ui::Hint> hints = {{ui::glyph::A, s.choose, HidNpadButton_A}};
    if (m_wiz_step > 0) hints.push_back({ui::glyph::L, s.back, HidNpadButton_L});
    hints.push_back({ui::glyph::R, m_wiz_step == kSteps - 1 ? s.wiz_start : s.wiz_next, HidNpadButton_R});
    hints.push_back({ui::glyph::Minus, s.wiz_music, HidNpadButton_Minus});
    ui::HintBar(vg, t, nullptr, hints, m_hint_rects);
}

bool App::TourActive() const {
    return m_time - m_tour_at < 4 * kTourStop;
}

void App::FollowTour() {
    if (!TourActive()) {
        if (m_tour_at > 0) {
            m_tour_at = -100;
            m_tab = Tab::Library;
        }
        return;
    }
    static constexpr Tab kStops[] = {Tab::Library, Tab::Queue, Tab::Installed, Tab::More};
    m_tab = kStops[std::clamp(int((m_time - m_tour_at) / kTourStop), 0, 3)];
    if (m_tab == Tab::More) m_more = MoreScreen::Tiles;
}

void App::EndTour() {
    m_tour_at = -100;
    m_tab = Tab::Library;
}

void App::DrawTour(NVGcontext* vg) {
    if (!TourActive()) {
        return;
    }
    const Theme& t = m_theme;
    const auto& s = Tr();
    const int stop = std::clamp(int((m_time - m_tour_at) / kTourStop), 0, 3);
    const double in_stop = m_time - m_tour_at - stop * kTourStop;
    static constexpr int kStopTab[] = {int(Tab::Library), int(Tab::Queue), int(Tab::Installed), int(Tab::More)};
    const ui::HitRect r = m_tab_rects[kStopTab[stop]];
    const float x = r.x + 2, y = 16, w = r.w - 4, h = 40;
    // everything goes dark but the tab, which glows
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, kWidth, kHeight);
    nvgRoundedRect(vg, x, y, w, h, 20);
    nvgPathWinding(vg, NVG_HOLE);
    nvgFillColor(vg, nvgRGBAf(0, 0, 0, 0.6f));
    nvgFill(vg);
    const float pulse = 0.7f + 0.3f * float(std::sin(m_time * 4));
    NVGcolor ring = t.work;
    ring.a = pulse;
    ui::StrokeRound(vg, x - 3, y - 3, w + 6, h + 6, 22, ring, 3);
    // the bubble under it, with what the tab is for
    const char* texts[] = {s.tour_library, s.tour_queue, s.tour_installed, TrM().tour_more};
    char count[64];
    std::snprintf(count, sizeof(count), TrM().tour_count4, stop + 1);
    const float bw = 440, bx = std::clamp(x + w / 2 - bw / 2, 24.f, kWidth - 24 - bw), by = 82;
    nvgFontFace(vg, "ui");
    nvgFontSize(vg, 17);
    nvgTextLineHeight(vg, 1.3f);
    float b[4];
    nvgTextBoxBounds(vg, bx + 20, by + 18, bw - 40, texts[stop], nullptr, b);
    const float bh = (b[3] - b[1]) + 18 + 16 + 30;
    nvgGlobalAlpha(vg, float(std::min(1.0, in_stop / 0.2)));
    Panel(vg, t, bx, by, bw, bh, 14);
    ui::StrokeRound(vg, bx + .5f, by + .5f, bw - 1, bh - 1, 14, t.work, 1.5f);
    TextBox(vg, bx + 20, by + 18, bw - 40, 17, t.text, texts[stop]);
    ui::Text(vg, bx + 20, by + bh - 22, 14, t.muted, count);
    // how long this stop lasts
    ui::FillRound(vg, bx + 14, by + bh - 6, (bw - 28) * float(std::min(1.0, in_stop / kTourStop)), 3, 1.5f, t.work);
    nvgGlobalAlpha(vg, 1.f);
}

} // namespace nxdock
