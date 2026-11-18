// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// Settings: its rows, the help of the row in focus, the speed test, Wi-Fi and pairing with the library app.
#include "app_shared.hpp"

namespace nxdock {

using namespace appui;

namespace {

// the first row of each group
constexpr int kGroupStarts[] = {kRowLibrary, kRowDest, kRowSaveCopy, kRowFinish, kRowLanguage, kRowWizard, kRowSelfUpdate, kRowHome, kRowAddress};

constexpr int kGroups = int(std::size(kGroupStarts));

// What a row of Settings does, for the panel on the right while it is in focus (S2 A of 14/11);
// nullptr for the rows of the network, which keep the speed test there, and for Style, its cards.
const char* SettingHelp(int row) {
    const Tools& tt = TrT();
    switch (row) {
        case kRowUpdates: return tt.h_updates;
        case kRowVersionsList: return tt.h_versions;
        case kRowDest: return tt.h_dest;
        case kRowPick: return tt.h_pick;
        case kRowScreen: return tt.h_screen;
        case kRowSigpatch: return TrM().sg_help;
        case kRowSaveCopy: return tt.h_save_copy;
        case kRowSaveAuto: return tt.h_save_auto;
        case kRowFinish: return tt.h_finish;
        case kRowBattery: return tt.h_battery;
        case kRowNotifyDone: return tt.h_notify;
        case kRowLanguage: return tt.h_language;
        case kRowTheme: return tt.h_theme;
        case kRowSounds: return tt.h_sounds;
        case kRowAnim: return tt.h_anim;
        case kRowFocusView: return tt.h_focus_view;
        case kRowTouchRumble: return tt.h_touch_rumble;
        case kRowWizard: return tt.h_wizard;
        case kRowSelfUpdate: return tt.h_self_update;
        case kRowSelfNotice: return tt.h_self_notice;
        case kRowSelfBack: return tt.h_self_back;
        case kRowHome: return tt.h_home;
        case kRowHomeRemove: return tt.h_home_remove;
        default: return nullptr;
    }
}

// the levels of the notice of little battery; 0 is off
constexpr int kBatterySteps[] = {0, 10, 15, 20, 30};

std::string Clip(std::string text, size_t max) {
    if (text.size() > max) {
        text = text.substr(0, max - 1) + "…";
    }
    return text;
}

} // namespace

void App::CheckWifi() {
    if (!m_nifm || m_time - m_wifi_checked < 3) {
        return;
    }
    m_wifi_checked = m_time;
    NifmInternetConnectionType type{};
    u32 strength = 0;
    NifmInternetConnectionStatus status{};
    if (R_FAILED(nifmGetInternetConnectionStatus(&type, &strength, &status))) {
        // not connected to any network
        m_wifi = -1;
        m_wifi_ssid.clear();
        m_wifi_ip = 0;
        return;
    }
    m_wifi_cable = type == NifmInternetConnectionType_Ethernet;
    m_wifi = m_wifi_cable ? 3 : int(std::min(strength, 3u));
    m_wifi_ssid.clear();
    if (!m_wifi_cable) {
        auto profile = std::make_unique<NifmNetworkProfileData>();
        if (R_SUCCEEDED(nifmGetCurrentNetworkProfile(profile.get()))) {
            const auto& w = profile->wireless_setting_data;
            m_wifi_ssid.assign(w.ssid, strnlen(w.ssid, std::min<size_t>(w.ssid_len, sizeof(w.ssid))));
        }
    }
    u32 ip = 0;
    m_wifi_ip = R_SUCCEEDED(nifmGetCurrentIpAddress(&ip)) ? ip : 0;
}

void App::DrawWifiCard(NVGcontext* vg) {
    if (!m_wifi_open || m_wifi < -1) {
        return;
    }
    const Theme& t = m_theme;
    const auto& m = TrM();
    const float w = 330, y = 80;
    const float x = std::clamp(m_wifi_rect.x + m_wifi_rect.w / 2 - w / 2, 16.f, float(kWidth) - 16 - w);
    struct Row {
        const char* label;
        std::string value;
    };
    std::vector<Row> rows;
    const char* words[] = {m.wf_l0, m.wf_l1, m.wf_l2, m.wf_l3};
    if (m_wifi >= 0) {
        if (!m_wifi_cable) {
            rows.push_back({m.wf_network, m_wifi_ssid.empty() ? std::string("-") : m_wifi_ssid});
            char level[96];
            std::snprintf(level, sizeof(level), m.wf_level_fmt, words[std::clamp(m_wifi, 0, 3)], m_wifi);
            rows.push_back({m.wf_coverage, level});
        }
        if (m_wifi_ip) {
            char ip[24];
            std::snprintf(ip, sizeof(ip), "%u.%u.%u.%u", m_wifi_ip & 0xFF, (m_wifi_ip >> 8) & 0xFF, (m_wifi_ip >> 16) & 0xFF,
                          m_wifi_ip >> 24);
            rows.push_back({m.wf_console, ip});
        }
        const Config c = m_link.CurrentConfig();
        if (!c.url.empty()) {
            const size_t scheme = c.url.find("://");
            rows.push_back({m.wf_library, c.url.substr(scheme == std::string::npos ? 0 : scheme + 3)});
        }
    }
    const bool weak = m_wifi == 1 && !m_wifi_cable;
    const float h = m_wifi < 0 ? 110 : 72 + 30 * float(rows.size()) + (weak ? 46 : 0);
    m_wifi_card = {x, y, w, h, 0};
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y + 6, w, h, 14);
    nvgFillPaint(vg, nvgBoxGradient(vg, x, y + 6, w, h, 14, 24, nvgRGBAf(0, 0, 0, 0.35f), nvgRGBAf(0, 0, 0, 0)));
    nvgFill(vg);
    ui::FillRound(vg, x, y, w, h, 14, t.panel);
    ui::StrokeRound(vg, x + .5f, y + .5f, w - 1, h - 1, 14, t.line, 1);
    const char* title = m_wifi < 0 ? m.wf_none : m_wifi_cable ? m.wf_cable : m.wf_title;
    ui::Text(vg, x + 18, y + 26, 18, t.text, title);
    if (m_wifi < 0) {
        TextBox(vg, x + 18, y + 46, w - 36, 15, t.muted, m.wf_none_hint);
        return;
    }
    float ry = y + 44;
    if (!m_wifi_cable) {
        // the coverage as a bar, in the colour of the fan
        const std::array<float, 4> pct{0.08f, 0.35f, 0.68f, 0.96f};
        const NVGcolor lit = m_wifi <= 0 ? t.muted : m_wifi == 1 ? t.warn : t.work;
        ui::FillRound(vg, x + 18, ry, w - 36, 6, 3, t.panel2);
        ui::FillRound(vg, x + 18, ry, (w - 36) * pct[size_t(std::clamp(m_wifi, 0, 3))], 6, 3, lit);
        ry += 22;
    } else {
        ry += 8;
    }
    for (const auto& r : rows) {
        ui::Text(vg, x + 18, ry + 10, 15, t.muted, r.label);
        nvgSave(vg);
        nvgScissor(vg, x + 120, ry - 4, w - 138, 28);
        ui::Text(vg, x + w - 18, ry + 10, 15, t.text, r.value.c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgRestore(vg);
        ry += 30;
    }
    if (weak) {
        TextBox(vg, x + 18, ry + 2, w - 36, 14, t.warn, m.wf_weak);
    }
}

void App::ActivateSetting(int row) {
    const auto& s = Tr();
    const Config current = m_link.CurrentConfig();
    switch (row) {
        case kRowTheme:
            // light, dark, as the console
            ApplyTheme(((m_ui.theme_auto ? 2 : m_theme.dark ? 1 : 0) + 1) % 3);
            break;
        case kRowStyle:
            // the next style, its wave from its card
            SetStyle((m_ui.style + 1) % kStyles, -1, -1);
            break;
        case kRowLanguage:
            // the console's, then each language NXDock speaks
            m_ui.language = (m_ui.language + 2) % (LanguageCount() + 1) - 1;
            UseLanguage(m_ui.language);
            m_ui.Save();
            break;
        case kRowSounds:
            m_ui.sounds = !m_ui.sounds;
            m_ui.Save();
            break;
        case kRowWizard:
            OpenWizard();
            break;
        case kRowSelfUpdate:
            AskSelfUpdate();
            break;
        case kRowSelfNotice:
            m_ui.self_update_notice = !m_ui.self_update_notice;
            m_ui.Save();
            break;
        case kRowSelfBack:
            if (m_update_has_old) {
                OpenDialog(DialogKind::SelfBack);
            }
            break;
        case kRowHome:
            AskHomeShortcut(false);
            break;
        case kRowHomeRemove:
            AskHomeShortcut(true);
            break;
        case kRowScreen: {
            // normal, dimmed after a minute, off for a long queue (and dimmed for a short one)
            const int next = ((m_ui.screen_off_auto ? 2 : m_ui.saver ? 1 : 0) + 1) % 3;
            m_ui.saver = next >= 1;
            m_ui.screen_off_auto = next == 2;
            m_ui.Save();
            break;
        }
        case kRowDest:
            // the SD card, or asking each time; the internal memory is only ever chosen for one install
            m_ui.dest = m_ui.dest == 2 ? 0 : 2;
            m_ui.Save();
            break;
        case kRowPick:
            m_ui.pick = (m_ui.pick + 1) % 3;
            m_ui.Save();
            break;
        case kRowSigpatch:
            // the orange line of the page of a game when no sigpatches are found (5B)
            m_ui.sigpatch_notice = !m_ui.sigpatch_notice;
            m_ui.Save();
            break;
        case kRowFinish:
            m_ui.finish_effect = !m_ui.finish_effect;
            m_ui.Save();
            break;
        case kRowBattery: {
            int at = 0;
            for (int k = 1; k < 5; k++) {
                if (m_ui.battery_notice && m_ui.battery_level == kBatterySteps[k]) at = k;
            }
            const int next = (at + 1) % 5;
            m_ui.battery_notice = next != 0;
            if (next) m_ui.battery_level = kBatterySteps[next];
            m_ui.Save();
            break;
        }
        case kRowUpdates:
            m_ui.check_updates = !m_ui.check_updates;
            m_ui.Save();
            break;
        case kRowVersionsList:
            m_ui.versions_list = !m_ui.versions_list;
            m_ui.Save();
            break;
        case kRowSaveCopy:
            m_ui.save_copy = !m_ui.save_copy;
            m_ui.Save();
            break;
        case kRowSaveAuto: {
            // no, every 1, 3, 7, 14 or 30 days
            static constexpr int steps[] = {0, 1, 3, 7, 14, 30};
            int at = 0;
            for (int k = 0; k < 6; k++) {
                if (m_ui.save_auto_days == steps[k]) at = k;
            }
            m_ui.save_auto_days = steps[(at + 1) % 6];
            m_ui.Save();
            break;
        }
        case kRowNotifyDone:
            m_ui.notify_done = !m_ui.notify_done;
            m_ui.Save();
            break;
        case kRowAnim:
            m_ui.anim = (m_ui.anim + 1) % 3;
            ui::SetMotion(m_ui.anim);
            m_ui.Save();
            break;
        case kRowFocusView:
            // the game in focus: a band under the covers, or a panel on the right (2A, 2B)
            m_ui.focus_view = m_ui.focus_view == 1 ? 0 : 1;
            m_ui.Save();
            GridSize(m_ui.library_size, m_ui.focus_view == 1);
            m_scroll = m_scroll_target = 0;
            KeepFocusVisible();
            break;
        case kRowTouchRumble:
            m_ui.touch_rumble = !m_ui.touch_rumble;
            m_ui.Save();
            TouchTick(); // felt at once when it is turned on
            break;
        case kRowLibrary:
            // the pairing screen, or the library when linked
            m_tab = Tab::Library;
            if (!current.Linked()) {
                m_link.Search();
            }
            break;
        case kRowForget:
            if (current.Linked()) {
                // asked first: pairing again needs a new code
                OpenDialog(DialogKind::Forget);
            }
            break;
        case kRowAddress: {
            std::string url;
            if (AskText(s.address, !m_manual_url.empty() ? m_manual_url : current.url.empty() ? "http://" : current.url, false, url)) {
                m_manual_url = url;
            }
            break;
        }
        case kRowKey: {
            std::string key;
            const std::string url = !m_manual_url.empty() ? m_manual_url : current.url;
            if (!url.empty() && AskText(s.console_key, "", true, key)) {
                m_link.UseKey(url, key);
            }
            break;
        }
        case kRowShare:
            m_prefs.share_installed = m_prefs.share_installed == 1 ? 0 : 1;
            m_prefs.Save();
            if (m_prefs.share_installed == 0) {
                m_push->SendInventory("{\"off\":true}"); // the app forgets it
            }
            m_inventory_sent = ~0u;
            break;
        case kRowSpeed:
            if (m_speed.Running()) {
                m_speed.Cancel();
            } else if (current.Linked()) {
                m_speed_kept = false;
                m_speed.Start(current.url, current.key);
            }
            break;
    }
}

void App::ActivatePairRow(int row, const Link::View& v) {
    if (row < int(v.found.size())) {
        m_link.Choose(row);
        return;
    }
    std::string url;
    if (AskText(Tr().address, "http://", false, url)) {
        m_link.UseAddress(url);
    }
}

void App::UpdatePairing(u64 down, u64 held, const Link::View& v) {
    const int rows = int(v.found.size()) + 1; // the apps found and "Type the address"
    const u64 up = HidNpadButton_AnyUp, dn = HidNpadButton_AnyDown;
    if (Repeat(down, held, up) && m_pair_focus > 0) m_pair_focus--;
    if (Repeat(down, held, dn) && m_pair_focus < rows - 1) m_pair_focus++;
    m_pair_focus = std::clamp(m_pair_focus, 0, rows - 1);
    const bool busy = v.state == Link::State::Searching || v.state == Link::State::Reconnecting;
    if ((down & HidNpadButton_A) && !busy) {
        ActivatePairRow(m_pair_focus, v);
    }
    if (down & HidNpadButton_X) {
        // a new code from the same app, or a new look at the network
        if (v.chosen >= 0 && v.chosen < int(v.found.size()) && v.state != Link::State::Offline) {
            m_link.Choose(v.chosen);
        } else {
            m_link.Search();
        }
    }
    if (down & HidNpadButton_Y) {
        m_link.SetRemember(!v.remember);
    }
}

void App::DrawSettings(NVGcontext* vg) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    const More& m = TrM();
    const bool running = m_speed.Running();
    const Link::View link = m_link.Snapshot();
    const Config current = m_link.CurrentConfig();
    const bool linked = current.Linked();

    // the rows in their groups (the recommended option A of the visual test of 11/11)
    std::string library = s.not_linked;
    if (linked) {
        const std::string host = current.url.substr(current.url.find("://") == std::string::npos ? 0 : current.url.find("://") + 3);
        library = Clip((current.name.empty() ? link.app_name : current.name) + " · " + host, 36);
    }
    const std::string address = !m_manual_url.empty() ? Clip(m_manual_url, 32) : s.not_set;
    const char* picks[] = {s.wiz_pick_missing, s.wiz_pick_base, s.wiz_pick_base_update};
    const char* screens[] = {m.st_scr_normal, m.st_scr_dim, m.st_scr_off};
    char battery[64];
    if (m_ui.battery_notice) std::snprintf(battery, sizeof(battery), m.st_battery_fmt, m_ui.battery_level);
    else std::snprintf(battery, sizeof(battery), "%s", s.no_word);
    const std::string language = m_ui.language < 0 ? std::string(s.wiz_console_lang) + " · " + LanguageName(ConsoleLanguage())
                                                   : std::string(LanguageName(m_ui.language));
    const int theme = m_ui.theme_auto ? 2 : t.dark ? 1 : 0;
    const char* themes[] = {s.light, s.dark, s.wiz_theme_auto};
    struct Row { const char* label; std::string value; bool accent; };
    const auto yes = [&](bool on) { return std::string(on ? s.yes_word : s.no_word); };
    // how often the saves are copied by themselves (9A)
    char save_auto[64];
    if (m_ui.save_auto_days > 0) std::snprintf(save_auto, sizeof(save_auto), TrT().sv_auto_fmt, m_ui.save_auto_days);
    else std::snprintf(save_auto, sizeof(save_auto), "%s", s.no_word);
    // the version, and the newer one when it was found (5A)
    const Updater::View up = m_updater ? m_updater->Snapshot() : Updater::View{};
    const bool self_newer = up.stage == Updater::Stage::Newer;
    char self_update[160];
    if (self_newer) std::snprintf(self_update, sizeof(self_update), TrT().up_have_new_fmt, NXDOCK_VERSION, up.version.c_str());
    else std::snprintf(self_update, sizeof(self_update), TrT().up_have_fmt, NXDOCK_VERSION);
    const Row rows[kSettingRows] = {
        {s.library, library, linked},
        {s.forget, "", false},
        {s.share_row, yes(m_prefs.share_installed == 1), m_prefs.share_installed == 1},
        {m.st_updates, yes(m_ui.check_updates), m_ui.check_updates},
        {TrT().ms_row, yes(m_ui.versions_list), m_ui.versions_list},
        {s.speed_test, running ? s.cancel : s.start, linked},
        {m.st_dest, m_ui.dest == 2 ? s.wiz_ask_each : s.sd_card, true},
        {m.st_pick, picks[std::clamp(m_ui.pick, 0, 2)], true},
        {m.st_screen, screens[m_ui.screen_off_auto ? 2 : m_ui.saver ? 1 : 0], m_ui.saver || m_ui.screen_off_auto},
        {m.sg_row, yes(m_ui.sigpatch_notice), m_ui.sigpatch_notice},
        {m.sc_row, yes(m_ui.save_copy), m_ui.save_copy},
        {TrT().sv_auto_row, save_auto, m_ui.save_auto_days > 0},
        {m.st_finish, yes(m_ui.finish_effect), m_ui.finish_effect},
        {m.st_battery, battery, m_ui.battery_notice},
        {m.qw_notify_row, yes(m_ui.notify_done), m_ui.notify_done},
        {s.wiz_language, language, true},
        {s.theme, themes[theme], true},
        {m.style_row, StyleName(m_ui.style), true},
        {s.sounds_row, yes(m_ui.sounds), m_ui.sounds},
        {m.anim_row, m_ui.anim == 2 ? m.anim_none : m_ui.anim == 1 ? m.anim_soft : m.anim_full, m_ui.anim != 2},
        {m.st_focus_row, m_ui.focus_view == 1 ? m.st_focus_panel : m.st_focus_band, true},
        {m.st_touch_rumble, yes(m_ui.touch_rumble), m_ui.touch_rumble},
        {s.wiz_row, s.wiz_repeat, true},
        {TrT().up_row, self_update, self_newer},
        {TrT().up_notice_row, yes(m_ui.self_update_notice), m_ui.self_update_notice},
        {TrT().up_back_row, m_update_has_old ? TrT().up_back_kept : TrT().up_back_none, false},
        {m.hm_row, m_home.running_from ? m.hm_in_use : m_home.installed ? m.hm_on : s.no_word, m_home.installed},
        {m.hm_remove_row, "", false},
        {s.manual_address, address, !m_manual_url.empty()},
        {s.manual_key, linked ? s.key_saved : s.not_set, linked},
    };
    const char* groups[kGroups] = {m.st_g_library, m.st_g_install, TrT().st_g_saves, m.st_g_notices, m.st_g_look, m.st_g_wizard, TrT().st_g_nxdock, m.st_g_home, m.st_g_manual};
    const float x = 48, w = 540, h = 56, gap = 8, head = 34;
    // where each row goes: a title before the first of each group
    float top_of[kSettingRows];
    float at = 0;
    for (int i = 0; i < kSettingRows; i++) {
        if (std::find(std::begin(kGroupStarts), std::end(kGroupStarts), i) != std::end(kGroupStarts)) at += head;
        top_of[i] = at;
        at += h + gap;
    }
    // the rows scroll when they do not fit, keeping the one in focus (and its title) in sight
    const float view_top = 88, view_bottom = 652;
    const float list_h = at + 8 + 70;
    const bool first_of_group = std::find(std::begin(kGroupStarts), std::end(kGroupStarts), m_setting) != std::end(kGroupStarts);
    const float focus_top = top_of[m_setting] - (first_of_group ? head : 0);
    float target = m_settings_scroll;
    if (focus_top - target < 0) target = focus_top;
    if (top_of[m_setting] + h + 12 - target > view_bottom - view_top) target = top_of[m_setting] + h + 12 - (view_bottom - view_top);
    if (m_setting == kSettingRows - 1) target = std::max(target, list_h - (view_bottom - view_top));
    m_settings_scroll = std::clamp(target, 0.f, std::max(0.f, list_h - (view_bottom - view_top)));
    nvgSave(vg);
    nvgScissor(vg, 0, view_top - 8, 600, view_bottom - view_top + 8);
    const float y0 = view_top + 8 - m_settings_scroll;
    int group = 0;
    for (int i = 0; i < kSettingRows; i++) {
        const float y = y0 + top_of[i];
        if (group < kGroups && kGroupStarts[group] == i) {
            if (y > view_top - 8 && y - 26 < view_bottom) draw::Section(vg, t, x + 6, y - 13, groups[group]);
            group++;
        }
        // only the rows in sight are drawn (the GPU has room for so many strokes a frame)
        if (y + h + 12 < view_top - 8 || y - 12 > view_bottom) continue;
        const bool dim = ((i == kRowForget || i == kRowSpeed || i == kRowShare) && !linked) ||
                         (i == kRowHomeRemove && (!m_home.installed || m_home.running_from)) ||
                         (i == kRowHome && m_home.running_from) || (i == kRowSelfBack && !m_update_has_old);
        nvgGlobalAlpha(vg, dim ? 0.45f : 1.f);
        Panel(vg, t, x, y, w, h, 14);
        if (i == m_setting) {
            ui::Focus(vg, t, x, y, w, h, 14, m_time);
        }
        // only the part in sight can be tapped
        const float hit_top = std::max(y, view_top - 8), hit_bottom = std::min(y + h, view_bottom);
        if (hit_bottom > hit_top) {
            AddHit(x, hit_top, w, hit_bottom - hit_top, HitKind::SettingsRow, i);
        }
        const float vw = std::min(260.f, ui::TextWidth(vg, 16, rows[i].value.c_str()));
        nvgSave(vg);
        nvgIntersectScissor(vg, x + 16, y, w - 32 - vw - 14, h);
        ui::Text(vg, x + 22, y + h / 2, 18, i == kRowForget ? t.warn : t.text, rows[i].label);
        nvgRestore(vg);
        nvgSave(vg);
        nvgIntersectScissor(vg, x + w - 22 - vw, y, vw, h);
        ui::Text(vg, x + w - 22, y + h / 2, 16, rows[i].accent ? t.work : t.muted, rows[i].value.c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgRestore(vg);
        nvgGlobalAlpha(vg, 1.f);
    }
    const float end_y = y0 + at;
    if (linked && !link.enabled) {
        TextBox(vg, x, end_y + 4, w, 15, t.warn, s.link_off_warning);
    } else if (!linked) {
        TextBox(vg, x, end_y + 4, w, 15, t.muted, s.need_config);
    }
    nvgRestore(vg);

    // on the right: the cards of Style, the help of the row in focus (S2 A), or the speed test for
    // the rows of the network; a short fade when it changes
    if (m_setting != m_help_row) {
        m_help_prev = m_help_row;
        m_help_row = m_setting;
        m_help_at = m_time;
    }
    const auto panel_of = [](int row) { return row == kRowStyle ? 1 : SettingHelp(row) ? 2 : 0; };
    const auto draw_panel = [&](int row, float alpha) {
        const float px = 620, py = 96, pw = kWidth - 620 - 48, ph = 548;
        switch (panel_of(row)) {
            case 1: DrawStylePanel(vg, px, py, pw, ph); break;
            case 2: {
                // "Now: …" for a choice; the rows that do something say their state as it is, or nothing
                std::string pill = rows[row].value;
                const bool action = row == kRowWizard || row == kRowSelfUpdate || row == kRowSelfBack || row == kRowHome || row == kRowHomeRemove;
                if (row == kRowWizard) pill.clear();
                if (!action && !pill.empty()) {
                    char now[200];
                    std::snprintf(now, sizeof(now), TrT().st_now_fmt, pill.c_str());
                    pill = now;
                }
                DrawSettingHelp(vg, px, py, pw, ph, rows[row].label, pill.c_str(), rows[row].accent, SettingHelp(row), alpha);
                break;
            }
            default: DrawSpeedPanel(vg, px, py, pw, ph); break;
        }
    };
    const float change = ui::EaseOut(ui::Appear(m_help_at, 0.3f));
    const bool plain = m_help_prev < 0 || m_help_prev >= kSettingRows ||
                       (panel_of(m_help_prev) != 2 && panel_of(m_help_prev) == panel_of(m_setting));
    if (change >= 1 || plain) {
        draw_panel(m_setting, 1);
    } else {
        nvgSave(vg);
        nvgTranslate(vg, 0, -12 * change);
        nvgGlobalAlpha(vg, 1 - change);
        draw_panel(m_help_prev, 1 - change);
        nvgRestore(vg);
        nvgSave(vg);
        nvgTranslate(vg, 0, 14 * (1 - change));
        nvgGlobalAlpha(vg, change);
        draw_panel(m_setting, change);
        nvgRestore(vg);
        nvgGlobalAlpha(vg, 1);
    }

    // A changes a choice; the rows that open something say what they do
    const char* act = m.st_change;
    if (m_setting == kRowAddress || m_setting == kRowKey) act = s.edit;
    else if (m_setting == kRowLibrary || m_setting == kRowWizard) act = m.open;
    else if (m_setting == kRowForget) act = m.fg_go;
    else if (m_setting == kRowSpeed) act = running ? s.cancel : s.start;
    else if (m_setting == kRowHome) act = m_home.installed ? m.hm_update : m.hm_make;
    else if (m_setting == kRowSelfUpdate) act = m.open;
    else if (m_setting == kRowSelfBack) act = TrT().up_back_go;
    else if (m_setting == kRowHomeRemove) act = m.hm_remove_go;
    if (m_setting == kRowStyle) {
        ui::HintBar(vg, t, nullptr, {{ui::glyph::DPad, act, HidNpadButton_AnyRight}, {ui::glyph::B, s.back, HidNpadButton_B}}, m_hint_rects);
        return;
    }
    ui::HintBar(vg, t, nullptr,
                {{ui::glyph::A, act, HidNpadButton_A}, {ui::glyph::X, running ? s.cancel : s.speed_test, HidNpadButton_X},
                 {ui::glyph::B, s.back, HidNpadButton_B}},
                m_hint_rects);
}

void App::DrawNoNetwork(NVGcontext* vg, const char* retry) {
    const Theme& t = m_theme;
    const More& m = TrM();
    // the fan of the WiFi, empty, and what to do
    const float cx = kWidth / 2, cy = 270;
    NVGcolor c = t.muted;
    for (int i = 1; i <= 3; i++) {
        nvgBeginPath(vg);
        nvgArc(vg, cx, cy + 22, 14.f * float(i), -2.36f, -0.79f, NVG_CW);
        nvgStrokeColor(vg, c);
        nvgStrokeWidth(vg, 4);
        nvgLineCap(vg, NVG_ROUND);
        nvgStroke(vg);
    }
    nvgBeginPath(vg);
    nvgCircle(vg, cx, cy + 22, 4);
    nvgFillColor(vg, c);
    nvgFill(vg);
    ui::Text(vg, cx, cy + 74, 24, t.text, m.nn_title, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgSave(vg);
    nvgFontFace(vg, "ui");
    nvgFontSize(vg, 17);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
    nvgTextLineHeight(vg, 1.3f);
    nvgFillColor(vg, t.muted);
    nvgTextBox(vg, cx - 330, cy + 100, 660, m.wf_none_hint, nullptr);
    nvgRestore(vg);
    ui::HintBar(vg, t, nullptr, {{ui::glyph::X, retry, HidNpadButton_X}},
                m_hint_rects);
    HomeToLeave(vg);
}

void App::DrawPairing(NVGcontext* vg, const Link::View& v) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    using State = Link::State;
    if (m_wifi == -1 && v.state != State::Waiting) {
        // no network: nothing below can work, said in large
        DrawNoNetwork(vg, s.search_again);
        return;
    }
    const float top = 100, ph = 520;
    const float lx = 48, lw = 560, rx = 640, rw = kWidth - 640 - 48;

    // Reconnecting with a remembered library: just say so.
    if (v.state == State::Reconnecting) {
        ui::Spinner(vg, kWidth / 2, 330, 20, t.work, m_time);
        ui::Text(vg, kWidth / 2, 384, 20, t.muted, s.reconnecting, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        ui::HintBar(vg, t, nullptr, {}, m_hint_rects);
        return;
    }

    // Left: the libraries found, and typing an address.
    Panel(vg, t, lx, top, lw, ph, 16);
    ui::Text(vg, lx + 26, top + 36, 22, t.text, s.find_library);
    TextBox(vg, lx + 26, top + 56, lw - 52, 15, t.muted, s.found_hint);
    float y = top + 100;
    const int rows = int(v.found.size()) + 1;
    for (int i = 0; i < rows; i++) {
        const bool typed = i == int(v.found.size());
        const float rh = 64;
        ui::FillRound(vg, lx + 22, y, lw - 44, rh, 12, t.panel2);
        ui::StrokeRound(vg, lx + 22.5f, y + .5f, lw - 45, rh - 1, 12, t.line, 1);
        if (i == m_pair_focus) {
            ui::Focus(vg, t, lx + 22, y, lw - 44, rh, 12, m_time);
        }
        AddHit(lx + 22, y, lw - 44, rh, HitKind::PairRow, i);
        if (typed) {
            ui::Text(vg, lx + 44, y + 24, 17, t.text, s.type_address);
            ui::Text(vg, lx + 44, y + 46, 14, t.muted, s.type_address_hint);
        } else {
            const auto& lib = v.found[i];
            ui::Text(vg, lx + 44, y + 24, 17, t.text, lib.name.c_str());
            const size_t scheme = lib.url.find("://");
            const std::string where = lib.url.substr(scheme == std::string::npos ? 0 : scheme + 3) + "  ·  " + lib.version;
            ui::Text(vg, lx + 44, y + 46, 14, t.muted, where.c_str());
            if (i == v.chosen) {
                const float cw = ui::TextWidth(vg, 14, s.chosen);
                ui::Text(vg, lx + lw - 44, y + rh / 2, 14, t.ok, s.chosen, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
                nvgBeginPath(vg);
                nvgCircle(vg, lx + lw - 44 - cw - 14, y + rh / 2, 9);
                nvgFillColor(vg, t.ok);
                nvgFill(vg);
                ui::CheckMark(vg, lx + lw - 44 - cw - 14, y + rh / 2, 10, t.panel, 2.2f);
            }
        }
        y += rh + 10;
    }
    if (v.state == State::Searching) {
        ui::Spinner(vg, lx + 36, y + 18, 8, t.work, m_time);
        ui::Text(vg, lx + 54, y + 18, 15, t.muted, s.searching);
        ui::Progress(vg, t, lx + 26, y + 40, lw - 52, 5, v.progress, t.work, m_time);
    } else if (v.state == State::Choose && v.found.empty()) {
        TextBox(vg, lx + 26, y + 6, lw - 52, 15, t.warn, s.none_found);
    } else if (v.state == State::Offline) {
        CompactError(vg, t, lx + 26, y + 6, lw - 52, v.error, t.warn, TrM().pr_todo_search);
    }

    // Right: the code and what to do with it.
    Panel(vg, t, rx, top, rw, ph, 16);
    ui::Text(vg, rx + 26, top + 36, 22, t.text, s.approve_title);
    TextBox(vg, rx + 26, top + 56, rw - 52, 15, t.muted, s.approve_hint);

    const float cy = top + 118;
    const bool has_code = v.state == State::Waiting && !v.code.empty();
    // eight boxes in two groups, as the app shows the code
    const float bw = 50, bh = 66, bg = 8, dash = 22;
    const float total = 8 * bw + 6 * bg + dash + 2 * bg;
    float bx = rx + (rw - total) / 2;
    size_t ci = 0;
    for (int i = 0; i < 8; i++) {
        if (i == 4) {
            ui::Text(vg, bx + dash / 2, cy + bh / 2, 30, t.muted, "–", NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            bx += dash + bg;
        }
        ui::FillRound(vg, bx, cy, bw, bh, 10, t.panel2);
        ui::StrokeRound(vg, bx + .5f, cy + .5f, bw - 1, bh - 1, 10, has_code ? t.work : t.line, has_code ? 1.5f : 1);
        if (has_code) {
            while (ci < v.code.size() && v.code[ci] == '-') ci++;
            if (ci < v.code.size()) {
                const char c[2] = {v.code[ci++], 0};
                ui::Text(vg, bx + bw / 2, cy + bh / 2 + 2, 34, t.text, c, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            }
        }
        bx += bw + bg;
    }

    // The QR (4A): the address of the app chosen, so a phone opens the library; the app shows the
    // notice to pair on any of its pages. The steps go on its right.
    const float qy = cy + bh + 26, qs = 150;
    std::string chosen_url;
    if (v.chosen >= 0 && v.chosen < int(v.found.size())) {
        chosen_url = v.found[v.chosen].url;
    } else if (!v.url.empty()) {
        chosen_url = v.url;
    }
    if (!chosen_url.empty()) {
        ui::Qr(vg, rx + 26, qy, qs, chosen_url + "/");
        const float tx = rx + 26 + qs + 22, tw = rw - 52 - qs - 22;
        const size_t scheme = chosen_url.find("://");
        const std::string host = chosen_url.substr(scheme == std::string::npos ? 0 : scheme + 3);
        TextBox(vg, tx, qy + 18, tw, 15, t.muted, s.qr_scan);
        const float h1 = BoxHeight(vg, tw, 15, s.qr_scan);
        nvgSave(vg);
        nvgScissor(vg, tx, qy, tw, qs);
        ui::Text(vg, tx, qy + 18 + h1 + 16, 18, t.text, host.c_str());
        nvgRestore(vg);
        TextBox(vg, tx, qy + 18 + h1 + 34, tw, 15, t.muted, s.qr_notice);
    } else {
        ui::FillRound(vg, rx + 26, qy, qs, qs, 10, t.panel2);
        ui::StrokeRound(vg, rx + 26.5f, qy + .5f, qs - 1, qs - 1, 10, t.line, 1);
        ui::Text(vg, rx + 26 + qs / 2, qy + qs / 2, 22, t.muted, "QR", NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    }

    // What happens now.
    const float sy = qy + qs + 34;
    if (v.state == State::Waiting) {
        char line[160];
        std::snprintf(line, sizeof(line), s.waiting_accept, v.seconds_left / 60, v.seconds_left % 60);
        ui::Spinner(vg, rx + 36, sy, 8, t.work, m_time);
        ui::Text(vg, rx + 54, sy, 16, t.work, line);
    } else if (v.state == State::Asking) {
        ui::Spinner(vg, rx + 36, sy, 8, t.work, m_time);
    } else if (!v.error.empty() && v.state != State::Offline) {
        // what happened, and what to do now: another code
        CompactError(vg, t, rx + 26, sy - 12, rw - 52, v.error, v.state == State::Failed ? t.err : t.warn, TrM().pr_todo_code);
    }

    // Remember: on by default; off, the link lasts until NXDock closes.
    const float my = top + ph - 70;
    AddHit(rx + 16, my - 8, rw - 32, 52, HitKind::Remember);
    ui::FillRound(vg, rx + 26, my, 30, 30, 8, v.remember ? t.work : t.panel2);
    if (!v.remember) {
        ui::StrokeRound(vg, rx + 26.5f, my + .5f, 29, 29, 8, t.muted, 1.5f);
    } else {
        ui::CheckMark(vg, rx + 41, my + 15, 16, t.on_work, 2.6f);
    }
    ui::Text(vg, rx + 68, my + 15, 17, t.text, s.remember);
    ui::Text(vg, rx + rw - 26, my + 16, 22, t.muted, ui::glyph::Y, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);

    const bool code_shown = v.state == State::Waiting || v.state == State::Expired || v.state == State::Refused;
    ui::HintBar(vg, t, nullptr,
                {{ui::glyph::A, s.choose, HidNpadButton_A},
                 {ui::glyph::X, code_shown ? s.new_code : s.search_again, HidNpadButton_X},
                 {ui::glyph::Y, TrM().pr_remember, HidNpadButton_Y}},
                m_hint_rects);
}

void App::DrawSettingHelp(NVGcontext* vg, float x, float y, float w, float h, const char* name, const char* value, bool accent,
                          const char* help, float alpha) {
    const Theme& t = m_theme;
    nvgGlobalAlpha(vg, alpha);
    draw::Panel(vg, t, x, y, w, h, 16);
    const float px = x + 28, pw = w - 56;
    // the mark of help, then the name of the row
    NVGcolor ring = t.work;
    ring.a = 0.16f;
    nvgBeginPath(vg);
    nvgCircle(vg, px + 18, y + 42, 18);
    nvgFillColor(vg, ring);
    nvgFill(vg);
    ui::Text(vg, px + 18, y + 43, 20, t.work, "i", NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFontFace(vg, "ui");
    nvgFontSize(vg, 21);
    nvgTextLineHeight(vg, 1.3f);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    float b[4];
    nvgTextBoxBounds(vg, px + 50, y + 28, pw - 50, name, nullptr, b);
    nvgFillColor(vg, t.text);
    nvgTextBox(vg, px + 50, y + 28, pw - 50, name, nullptr);
    nvgTextBox(vg, px + 50.5f, y + 28, pw - 50, name, nullptr); // drawn twice, as titles are
    float cy = std::max(y + 66, b[3]) + 18;
    if (value && value[0]) {
        // what it is now, in a pill
        const char* now = value;
        const float nw = std::min(pw, ui::TextWidth(vg, 15, now) + 28);
        NVGcolor fill = accent ? t.work : t.muted;
        fill.a = 0.16f;
        ui::FillRound(vg, px, cy, nw, 30, 15, fill);
        nvgSave(vg);
        nvgIntersectScissor(vg, px, cy, nw, 30);
        ui::Text(vg, px + 14, cy + 15, 15, accent ? t.work : t.muted, now);
        nvgRestore(vg);
        cy += 52;
    }
    draw::Box(vg, px, cy, pw, 18, t.text, help);
    nvgGlobalAlpha(vg, 1);
}

void App::DrawSpeedPanel(NVGcontext* vg, float x, float y, float w, float h) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    const SpeedTest::View v = m_speed.Snapshot();
    Panel(vg, t, x, y, w, h, 16);
    const float px = x + 24, pw = w - 48;
    ui::Text(vg, px, y + 34, 21, t.text, s.speed_test);

    // The mode: an applet gets small buffers, which caps the speed.
    const bool applet = net::IsAppletMode();
    TextBox(vg, px, y + 58, pw, 14, applet ? t.warn : t.muted, applet ? s.applet_warning : s.app_mode);

    // One line per measure: label, bar and value. Bars share a scale so they compare at a glance.
    double top = std::max({v.net[0], v.net[1], v.net[2], v.sd, 20.0});
    if (v.live > top) top = v.live;
    const int best = (v.net[0] >= v.net[1] && v.net[0] >= v.net[2]) ? 0 : (v.net[1] >= v.net[2] ? 1 : 2);
    struct Line { const char* label; double value; SpeedTest::Stage stage; NVGcolor color; };
    const Line lines[] = {
        {s.conn1, v.net[0], SpeedTest::Stage::Net1, t.work},
        {s.conn2, v.net[1], SpeedTest::Stage::Net2, t.work},
        {s.conn4, v.net[2], SpeedTest::Stage::Net4, t.work},
        {s.sd_write, v.sd, SpeedTest::Stage::Sd, t.orange},
    };
    float ly = y + 128;
    for (int i = 0; i < 4; i++) {
        const auto& l = lines[i];
        const bool now = v.stage == l.stage;
        const double value = now ? v.live : l.value;
        ui::Text(vg, px, ly, 16, now ? t.text : t.muted, l.label);
        char text[48];
        if (value > 0) {
            std::snprintf(text, sizeof(text), "%.1f MB/s", value);
        } else {
            std::snprintf(text, sizeof(text), "%s", now ? "…" : s.waiting_test);
        }
        const float vw = ui::Text(vg, px + pw, ly, 16, value > 0 ? t.text : t.muted, text, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        (void)vw;
        if (i < 3 && v.stage == SpeedTest::Stage::Done && i == best && l.value > 0) {
            const float bw = ui::TextWidth(vg, 12, s.best) + 14;
            const float tx = px + ui::TextWidth(vg, 16, l.label) + 10;
            ui::FillRound(vg, tx, ly - 11, bw, 22, 6, t.ok);
            ui::Text(vg, tx + bw / 2, ly, 12, nvgRGB(255, 255, 255), s.best, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        }
        ui::Progress(vg, t, px, ly + 18, pw, 8, float(value / top), l.color, m_time, now);
        ly += 66;
    }

    // Progress of the stage running, or the error, or what the test does.
    const float by = y + h - 70;
    if (v.stage == SpeedTest::Stage::Failed) {
        CompactError(vg, t, px, by - 8, pw, v.error, t.err, TrM().sp_todo);
    } else if (m_speed.Running()) {
        ui::Progress(vg, t, px, by + 8, pw, 6, v.progress, t.muted, m_time, true);
    } else {
        TextBox(vg, px, by, pw, 14, t.muted, s.test_note);
    }
}

} // namespace nxdock
