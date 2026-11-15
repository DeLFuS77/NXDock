// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The app: start and exit, a frame, the screens drawn in turn, the end of a queue, notices.
#include "app_shared.hpp"

namespace nxdock {

using namespace appui;

namespace {

void Background(NVGcontext* vg, const Theme& t) {
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, kWidth, kHeight);
    nvgFillColor(vg, t.bg);
    nvgFill(vg);
}

// The cover that falls into the dock at the end of a queue (10A), drawn by ui::Dock between its
// back and its front.
struct Falling {
    const Theme* theme;
    float x, y, s, angle;
    unsigned color;
    const char* initial;
    int image;
};

void DrawFalling(NVGcontext* vg, void* data) {
    const auto& f = *static_cast<const Falling*>(data);
    nvgSave(vg);
    nvgTranslate(vg, f.x + f.s / 2, f.y + f.s / 2);
    nvgRotate(vg, f.angle);
    nvgTranslate(vg, -f.s / 2, -f.s / 2);
    nvgBeginPath(vg);
    nvgRect(vg, -16, -6, f.s + 32, f.s + 30);
    nvgFillPaint(vg, nvgBoxGradient(vg, 2, 8, f.s - 4, f.s, 10, 16, nvgRGBAf(0, 0, 0, 0.4f), nvgRGBAf(0, 0, 0, 0)));
    nvgFill(vg);
    ui::Cover(vg, *f.theme, 0, 0, f.s, f.color, f.initial, nullptr, false, false, 10, f.image);
    nvgRestore(vg);
}

} // namespace

// The first letter of a name (one UTF-8 character), for a cover not loaded yet.
std::string Initial(const std::string& name) {
    if (name.empty()) {
        return "?";
    }
    size_t len = 1;
    const unsigned char c = static_cast<unsigned char>(name[0]);
    if (c >= 0xF0) len = 4;
    else if (c >= 0xE0) len = 3;
    else if (c >= 0xC0) len = 2;
    return name.substr(0, std::min(len, name.size()));
}

std::string VersionText(u32 version) {
    const More& m = TrM();
    if (version == 0) return m.ver_none;
    char text[96];
    // the number of the update (version 196608 is the third); an odd one is shown as it is
    std::snprintf(text, sizeof(text), m.ver_update_fmt, (version >> 16) ? version >> 16 : version);
    return text;
}

// A colour for the placeholder of a cover, the same for a game every time.
unsigned PlaceholderColor(const std::string& key) {
    static constexpr unsigned kColors[] = {0xc8743a, 0x2e7d6b, 0x6a4ae0, 0x3a6fc8, 0xb8406e, 0x8a8f2a, 0x2a8fb0,
                                           0x9a3a3a, 0x4a7a3a, 0x7a4ab0, 0x2f8a8a, 0x8a5a3a};
    return kColors[std::hash<std::string>{}(key) % (sizeof(kColors) / sizeof(kColors[0]))];
}

float CompactError(NVGcontext* vg, const Theme& t, float x, float y, float w, const std::string& packed, NVGcolor color, const char* todo) {
    const ErrorText e = UnpackError(packed);
    const std::string head = e.title.empty() ? e.what : e.title;
    const std::string next = e.todo.empty() ? std::string(todo ? todo : "") : e.todo;
    float h = 0;
    if (!head.empty()) {
        TextBox(vg, x, y, w, 15, color, head.c_str());
        h += BoxHeight(vg, w, 15, head.c_str()) + 2;
    }
    if (!next.empty()) {
        TextBox(vg, x, y + h, w, 14, t.muted, next.c_str());
        h += BoxHeight(vg, w, 14, next.c_str()) + 2;
    }
    if (!e.code.empty()) {
        char line[96];
        std::snprintf(line, sizeof(line), TrM().code_line, e.code.c_str());
        ui::Text(vg, x, y + h + 7, 12, t.muted, line);
        h += 16;
    }
    return h;
}

bool App::Init() {
    perf::Mark("main");
    InitStrings();
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&m_pad);
    hidInitializeTouchScreen();
    net::Init();
    perf::Mark("net ready");
    const Config config = Config::Load();
    m_theme = config.light ? LightTheme() : DarkTheme();
    if (!m_gfx.Init()) {
        return false;
    }
    perf::Mark("graphics and fonts ready");
    // remembered: connects by itself; else looks for the app on the network
    m_link.Start(config);
    m_prefs = InstallPrefs::Load();
    m_ui = UiPrefs::Load();
    ui::SetMotion(m_ui.anim);
    if (m_ui.language >= 0) {
        UseLanguage(m_ui.language); // chosen in the wizard; else the console's
    }
    if (m_ui.theme_auto) {
        // the theme follows the console's
        ColorSetId color = ColorSetId_Light;
        if (R_SUCCEEDED(setsysInitialize())) {
            if (R_FAILED(setsysGetColorSetId(&color))) color = ColorSetId_Light;
            setsysExit();
        }
        m_theme = color == ColorSetId_Dark ? DarkTheme() : LightTheme();
    }
    m_theme = StyleTheme(m_ui.style, !m_theme.dark);
    m_psm = R_SUCCEEDED(psmInitialize());
    m_nifm = R_SUCCEEDED(nifmInitialize(NifmServiceType_User));
    perf::Mark("settings and services");
    m_sound.Start();
    perf::Mark("sound");
    InitRumble();
    perf::Mark("rumble");
    m_queue = std::make_unique<InstallQueue>();
    m_queue->SetOptions(m_prefs.connections, m_prefs.download_test);
    m_report = std::make_unique<QueueReport>();
    // cheats and mods that an update would break (19A): the builds of both versions, from the
    // library app (its database, or the file of the update), against what is on the SD card
    m_queue->SetModsCheck([this](u64 app, u32 from, u32 to) -> std::string {
        const Config c = m_link.CurrentConfig();
        if (!c.Linked()) return {};
        const auto build = [&c, app](u32 version) -> std::string {
            char hex[24];
            std::snprintf(hex, sizeof(hex), "%016llX", (unsigned long long)app);
            const auto r = http::Get(c.url + "/install/cheats/" + hex + "?version=" + std::to_string(version), c.key, 15000);
            if (r.status != 200 && r.status != 404) return {};
            yyjson_doc* doc = yyjson_read(r.body.data(), r.body.size(), 0);
            if (!doc) return {};
            const char* b = yyjson_get_str(yyjson_obj_get(yyjson_doc_get_root(doc), "build_id"));
            std::string out = b ? b : "";
            yyjson_doc_free(doc);
            return out;
        };
        const std::string to_build = build(to);
        if (to_build.empty()) return {};
        const auto risk = ModsAtRiskFor("sdmc:/atmosphere", app, build(from), to_build);
        if (!risk.Any()) return {};
        const More& m = TrM();
        std::string what;
        char line[200];
        if (risk.cheats) {
            std::snprintf(line, sizeof(line), m.mr_cheats_fmt, risk.cheats);
            what += std::string("\xE2\x80\xA2 ") + line + "\n";
        }
        if (risk.patches) {
            std::snprintf(line, sizeof(line), m.mr_patches_fmt, risk.patches);
            what += std::string("\xE2\x80\xA2 ") + line + "\n";
        }
        if (risk.exefs) what += std::string("\xE2\x80\xA2 ") + m.mr_exefs + "\n";
        return what;
    });
    // the saves before an update, a repair, a removal or going back (18A), when it is on: to the
    // library, or to the SD card when none is linked; on the queue's thread
    m_queue->SetSaveCopy([this](u64 app, int why, u32 to, u32 current, std::function<void(u64, u64)> progress) -> std::string {
        if (!m_ui.save_copy) return {};
        Config c = m_link.CurrentConfig();
        if (!c.Linked()) c.url.clear(); // CopySavesBefore copies to the SD card
        const More& m = TrM();
        char note[160];
        switch (why) {
            case 0: std::snprintf(note, sizeof(note), m.sc_note_update, VersionText(to).c_str()); break;
            case 1: std::snprintf(note, sizeof(note), "%s", m.sc_note_repair); break;
            case 3: std::snprintf(note, sizeof(note), m.sc_note_back, VersionText(to).c_str()); break;
            default: std::snprintf(note, sizeof(note), "%s", m.sc_note_remove); break;
        }
        return CopySavesBefore(c.url, c.key, app, current, note, progress);
    });
    perf::Mark("install queue");
    m_push = std::make_unique<Push>();
    m_safety = std::make_unique<Safety>();
    m_sysinfo = ReadSystemInfo();
    perf::Mark("push, safety, system info");
    m_time = Now();
    m_last_input = m_time;
    m_home = forwarder::Query();
    perf::Mark("HOME shortcut looked at");
    if (!m_ui.wizard_done) {
        OpenWizard(); // only the first time; repeated from Settings
    } else if (net::IsAppletMode() && !m_home.installed && m_home.nro_found) {
        Notice(TrM().hm_applet_tip);
        m_notice_until = m_time + 8;
    }
    if (m_ui.wizard_done && !ui::Still()) {
        // the opening (4A): the icon assembles itself, then the covers come in
        m_boot_at = m_time;
        m_wave_at = m_time + (ui::Lively() ? 1.25 : 0.5);
    }
    return true;
}

void App::AskHomeShortcut(bool remove) {
    m_home = forwarder::Query();
    const More& m = TrM();
    if (m_home.running_from) {
        Notice(m.hm_in_use_note);
        return;
    }
    if (remove && !m_home.installed) {
        return;
    }
    if (!remove && !m_home.nro_found) {
        Notice(m.hm_no_nro);
        return;
    }
    m_dialog = Dialog{};
    m_dialog.kind = remove ? DialogKind::HomeRemove : DialogKind::HomeAdd;
}

void App::DoHomeShortcut(bool remove) {
    const More& m = TrM();
    const bool was = m_home.installed;
    const Result rc = remove ? forwarder::Remove() : forwarder::Install();
    m_home = forwarder::Query();
    if (R_SUCCEEDED(rc)) {
        NoticeOk(remove ? m.hm_done_remove : was ? m.hm_done_update : m.hm_done_add);
        return;
    }
    if (rc == forwarder::kResultNoNro) {
        Notice(m.hm_no_nro);
    } else if (rc == forwarder::kResultRunning) {
        Notice(m.hm_in_use_note);
    } else {
        char text[200];
        std::snprintf(text, sizeof(text), m.hm_failed_fmt, unsigned(rc));
        Notice(text);
    }
}

void App::Exit() {
    // the game in focus, to find it there the next time
    const std::string focused = FocusedKey();
    if (!focused.empty() && focused != m_ui.focus) {
        m_ui.focus = focused;
        m_ui.Save();
    }
    // the backlight on again before leaving
    ScreenOn();
    if (m_space_thread.joinable()) m_space_thread.join();
    if (m_lbl) lblExit();
    // a check or a download of a new version is let go
    m_updater.reset();
    // a copy, a move or a delete of the file list stops first (what was done stays done)
    if (m_fwork) {
        m_fwork->cancel = true;
        if (m_fwork_thread.joinable()) m_fwork_thread.join();
        m_fwork.reset();
    }
    // what arrives from the computer stops first, so a job reading it ends
    MtpStop();
    UsbLink().Cancel();
    // a job running is cancelled and cleaned up before the network goes; leaving for another app
    // (22A), what was installing and waiting is written down to go on the next time
    const auto kept = m_keep_queue && m_queue ? m_queue->Jobs() : std::vector<InstallJob>{};
    m_queue.reset();
    if (!kept.empty()) KeepForLater(kept);
    UsbLink().Stop();
    UsbDisksStop();
    m_push.reset();
    m_sound.Stop();
    m_speed.Cancel();
    m_link.Stop();
    m_covers.Stop();
    m_covers.Free(m_gfx.vg());
    m_gfx.Exit();
    if (m_psm) {
        psmExit();
    }
    if (m_nifm) {
        nifmExit();
    }
    net::Exit();
}

void App::CheckBattery() {
    if (!m_psm || m_time - m_battery_checked < 5) {
        return;
    }
    m_battery_checked = m_time;
    u32 percent = 0;
    PsmChargerType charger = PsmChargerType_Unconnected;
    if (R_SUCCEEDED(psmGetBatteryChargePercentage(&percent))) {
        m_battery = int(std::min(percent, 100u));
    }
    if (R_SUCCEEDED(psmGetChargerType(&charger))) {
        m_charging = charger != PsmChargerType_Unconnected;
    }
}

// A cover still on its way (as chosen, A): a small blue circle turning at its corner, over the placeholder.
float App::GrowFromPlus(NVGcontext* vg, double at, float x, float y, float w, float h) {
    // the menus of + (1A) grow out of its symbol in the bar below, or the corner of the panel
    float ox = x + w, oy = y + h;
    for (const auto& r : m_hint_rects) {
        if (r.button == HidNpadButton_Plus) {
            ox = r.x + 16;
            oy = r.y + r.h / 2;
        }
    }
    const float pop = ui::EaseOutBack(ui::Appear(at, 0.28f));
    const float scale = 0.55f + 0.45f * pop;
    nvgTranslate(vg, ox, oy);
    nvgScale(vg, scale, scale);
    nvgTranslate(vg, -ox, -oy);
    const float alpha = std::clamp(pop * 1.4f, 0.f, 1.f);
    nvgGlobalAlpha(vg, alpha);
    return alpha;
}

float App::RowComesIn(NVGcontext* vg, double at, int row, float alpha) {
    // the rows of a menu come in one after another, from below
    const float k = ui::EaseOut(ui::Appear(at + 0.06 + 0.025 * row, 0.22f));
    nvgTranslate(vg, 0, (1 - k) * 14);
    nvgGlobalAlpha(vg, alpha * k);
    return alpha * k;
}

void App::HomeToLeave(NVGcontext* vg) {
    // + no longer leaves (3A): the HOME button does, said where nothing else can be done
    ui::Text(vg, kWidth / 2, kHeight - 56 - 26, 15, m_theme.muted, TrM().home_exit, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
}

int App::CoverOf(const Game& g) {
    const int image = m_covers.Get(g.cover);
    if (image || !g.installed.base || !g.title_id || !m_covers.Missing(g.cover)) return image;
    // the library has no cover for it (or not yet one it could fetch): the icon the game has on
    // the console, rather than only its first letter
    char path[24];
    std::snprintf(path, sizeof(path), "ns:%016llX", static_cast<unsigned long long>(g.title_id));
    return m_covers.Get(path);
}

void App::CoverLoading(NVGcontext* vg, float x, float y, float size, const std::string& path) {
    if (size < 30 || !m_covers.Loading(path)) return;
    const float r = std::clamp(size * 0.06f, 5.f, 11.f);
    const float m = std::max(5.f, size * 0.05f);
    const float cx = x + size - m - r - 3, cy = y + size - m - r - 3;
    nvgBeginPath(vg);
    nvgCircle(vg, cx, cy, r + 4);
    nvgFillColor(vg, nvgRGBAf(0, 0, 0, 0.35f));
    nvgFill(vg);
    ui::Spinner(vg, cx, cy, r, m_theme.work, m_time);
}

bool App::Repeat(u64 down, u64 held, u64 mask) {
    if (down & mask) {
        m_held_dir = down & mask;
        m_held_since = m_time;
        m_next_repeat = m_time + 0.35;
        return true;
    }
    if ((held & m_held_dir) && (m_held_dir & mask) && m_time >= m_next_repeat) {
        // held for more than a second, it goes twice as fast
        m_next_repeat = m_time + (m_time - m_held_since > 1.35 ? 0.045 : 0.09);
        return true;
    }
    return false;
}

void App::Update(u64 down, u64 held) {
    if (m_launch_at > 0) {
        // a game asked to open (2A): NXDock closes once its line has been seen, and does nothing else
        if (m_time - m_launch_at > 0.8) m_quit = true;
        return;
    }
    if (m_dice_at > 0) return; // the dice roll (21A), a moment
    if (m_dialog.kind != DialogKind::None) {
        UpdateDialog(down, held);
        return;
    }
    // the WiFi card: the right stick opens and closes it, B or A closes it
    if (down & HidNpadButton_StickR) {
        // on the grid the right stick jumps by letter: a click while it is pushed, or just after a
        // jump, is part of that and does not open the WiFi
        const u64 pushed = HidNpadButton_StickRUp | HidNpadButton_StickRDown | HidNpadButton_StickRLeft | HidNpadButton_StickRRight;
        const bool jumping = GridTab() && !m_detail && !m_wifi_open && ((held & pushed) || m_time - m_rstick_at < 0.3 || m_time - m_jump_at < 0.3);
        if (!jumping) {
            m_wifi_open = !m_wifi_open && m_wifi > -2;
            return;
        }
        down &= ~u64(HidNpadButton_StickR);
    }
    if (m_wifi_open) {
        if (down & (HidNpadButton_B | HidNpadButton_A)) m_wifi_open = false;
        return;
    }
    if (m_wizard) {
        UpdateWizard(down, held);
        return;
    }
    // + opens the actions of the screen, − too (1A, 14/11); the library, Files and Saves have their own
    {
        const ActScreen act = CurrentActScreen();
        if (m_act && act != ActParent(m_act_screen)) m_act = false;
        if (m_act) {
            UpdateActions(down, held);
            return;
        }
        if (act != ActScreen::None && (down & (HidNpadButton_Plus | HidNpadButton_Minus))) {
            OpenActions(act);
            return;
        }
    }
    if (m_detail && GridTab()) {
        if (m_detail_game < 0 || m_detail_game >= int(m_games.size())) {
            m_detail = false;
        } else if (down & (HidNpadButton_L | HidNpadButton_R)) {
            // as everywhere, L and R change the tab; the page is there again when coming back
            m_tab = Tab((int(m_tab) + ((down & HidNpadButton_R) ? 1 : kTabs - 1)) % kTabs);
            return;
        } else {
            UpdateDetail(down, held);
            return;
        }
    }
    if (m_quick >= 0) {
        // the quick menu of a cover held (18A)
        if (GridTab() && !m_detail && m_quick < int(m_view.size())) {
            UpdateQuick(down, held);
            return;
        }
        m_quick = -1;
    }
    if (m_menu) {
        if (GridTab()) {
            UpdateMenu(down, held);
            return;
        }
        m_menu = false;
    }
    // L and R change the tab from anywhere, as on the HOME menu; a tool of More stays open, and is
    // there again when coming back
    const bool in_tool = (m_tab == Tab::More && m_more != MoreScreen::Tiles) || // B leaves the tool first
                         (m_tab == Tab::Installed && m_installed_view == 0 && m_console_app != 0) || // or the page of a game
                         (m_tab == Tab::Shops && m_shop_active); // or the shop open: the list of shops again
    if (down & HidNpadButton_L) {
        m_tab = Tab((int(m_tab) + kTabs - 1) % kTabs);
    }
    if (down & HidNpadButton_R) {
        m_tab = Tab((int(m_tab) + 1) % kTabs);
    }

    const u64 up = HidNpadButton_AnyUp, dn = HidNpadButton_AnyDown;

    switch (m_tab) {
        case Tab::Library: {
            const Link::View link = m_link.Snapshot();
            if (link.state != Link::State::Connected) {
                UpdatePairing(down, held, link);
                break;
            }
            UpdateLibrary(down, held);
            break;
        }
        case Tab::Shops:
            // the list of shops, or the games of the one open (as the library)
            if (m_shop_active) UpdateLibrary(down, held);
            else UpdateShops(down, held);
            break;
        case Tab::Queue:
            UpdateQueue(down, held);
            break;
        case Tab::Installed:
            UpdateInstalled(down, held);
            break;
        case Tab::More:
            UpdateMore(down, held);
            break;
        case Tab::Settings:
            if (m_setting == kRowStyle) {
                // left and right go through the styles, as their cards are laid out
                if (Repeat(down, held, HidNpadButton_AnyLeft)) SetStyle((m_ui.style + kStyles - 1) % kStyles, -1, -1);
                if (Repeat(down, held, HidNpadButton_AnyRight)) SetStyle((m_ui.style + 1) % kStyles, -1, -1);
            }
            if (Repeat(down, held, up) && m_setting > 0) m_setting--;
            if (Repeat(down, held, dn) && m_setting < kSettingRows - 1) m_setting++;
            if (down & HidNpadButton_A) ActivateSetting(m_setting);
            if (down & HidNpadButton_X) ActivateSetting(kRowSpeed);
            break;
        default:
            break;
    }
    if ((down & HidNpadButton_B) && m_tab != Tab::Library && !in_tool) {
        m_tab = Tab::Library;
    }
}

bool App::Frame() {
    if (!appletMainLoop() || m_quit) {
        return false;
    }
    perf::FrameBegin();
    const double now = Now();
    const float dt = float(std::min(0.1, now - m_time));
    m_time = now;

    padUpdate(&m_pad);
    u64 down = padGetButtonsDown(&m_pad);
    u64 held = padGetButtons(&m_pad);
#ifdef NXDOCK_PERF
    {
        u64 scripted = 0;
        perf::Script(scripted, m_quit);
        down |= scripted & ~m_perf_held;
        held |= scripted;
        m_perf_held = scripted;
    }
#endif
    // the clock of the animations, and the buttons whose symbols pulse
    ui::Tick(m_time);
    ui::NotePress(down);
    if (m_dialog.kind != m_dialog_drawn) {
        m_dialog_drawn = m_dialog.kind;
        m_dialog_at = m_time;
    }
    HidTouchScreenState touch{};
    perf::ReadTouch(touch);
    const bool new_touch = touch.count > 0 && !m_touching && !m_wake_touch;
    if (down || held || touch.count > 0) {
        m_last_input = m_time;
    }
    if (FollowScreenOff(down, new_touch)) {
        // the input that lit the screen, or stopped the countdown, does nothing else
    } else if (m_boot_at > 0 && m_time - m_boot_at < 1.5 && (down || new_touch)) {
        // the opening is skipped by any button or touch, which does nothing else (4A)
        m_boot_at = -100;
        m_wave_at = -100;
        m_wake_touch = new_touch;
        down = 0;
    } else if ((m_saver_on || TourActive()) && (down || new_touch)) {
        // the input that lights the screen again, or ends the walk, does nothing else
        m_saver_on = false;
        if (TourActive()) EndTour();
        m_wake_touch = new_touch;
        down = 0;
    }
    const u64 focus_before = FocusSignature();
    { perf::Timer t_("frame: ReadTouch"); ReadTouch(down); }
    // a list being dragged: one row a frame, as a press of the pad
    if (m_drag_pending > 0 && !(down & HidNpadButton_AnyDown)) {
        down |= HidNpadButton_Down;
        m_drag_pending--;
    } else if (m_drag_pending < 0 && !(down & HidNpadButton_AnyUp)) {
        down |= HidNpadButton_Up;
        m_drag_pending++;
    }
    if (m_dialog.kind != DialogKind::None || m_menu || m_act || m_quick >= 0) {
        m_drag_pending = 0;
        m_list_fling = 0;
    }
    GridSize(m_ui.library_size, m_ui.focus_view == 1);
    SeeQueue();
    { perf::Timer t_("frame: Update"); Update(down, held); }
    { perf::Timer t_("frame: FollowPreflight"); FollowPreflight(); }
    // the game card coming and going (14A), the updates looked for in a folder or a shop (15A)
    FollowCard();
    FollowUpdateHere();
    if (m_tab != m_tab_seen) {
        // the Library again: what the console has now, also what DBI or the HOME menu changed
        if (m_tab_seen == Tab::Shops) LeaveShopsTab();
        if (m_tab == Tab::Shops) EnterShopsTab();
        // on a thread of its own, at most every few seconds: going through the tab never waits for it
        if (m_tab == Tab::Library && m_queue && !m_queue->Busy() && m_time - m_installed_read_at > 4) {
            m_installed_read_at = m_time;
            m_queue->ReadInstalledAgain();
        }
        m_tab_seen = m_tab;
    }
    { perf::Timer t_("frame: PlaySounds"); PlaySounds(down, focus_before); }
    { perf::Timer t_("frame: FollowTour"); FollowTour(); }
    // saver mode: dark after a minute without input while something installs
    if (m_queue && !m_queue->Busy()) {
        m_saver_on = false;
    } else if (m_ui.saver && !m_wizard && m_time - m_last_input > 60) {
        m_saver_on = true;
    }
    { perf::Timer t_("frame: FollowLink"); FollowLink(); }
    if (m_refreshing && m_catalog.GetState() != Catalog::State::Loading && m_time - m_refresh_at > 0.4) {
        // the library read again by pulling it down (21A): said in green, and the grid goes back up
        m_refreshing = false;
        if (m_catalog.GetState() == Catalog::State::Ready) {
            char text[160];
            std::snprintf(text, sizeof(text), TrM().pr_done_fmt, int(m_games.size()));
            NoticeOk(text);
        }
        if (!m_dragging) m_scroll_target = std::clamp(m_scroll_target, 0.f, MaxScroll());
    }
    if (m_tick_off_at > 0 && m_time >= m_tick_off_at) {
        // the end of the short vibration of a touch (17A)
        m_tick_off_at = -1;
        if (m_rumble_step < 0) SendRumble(false);
    }
    { perf::Timer t_("frame: FollowQueue"); FollowQueue(); }
    FollowCompressQueue(); // Compress games goes on on any screen (15/11)
    { perf::Timer t_("frame: FollowFilesWork"); FollowFilesWork(); }
    { perf::Timer t_("frame: FollowSelfUpdate"); FollowSelfUpdate(); }
    { perf::Timer t_("frame: FollowSaveAuto"); FollowSaveAuto(); }
    FollowReboot(); // "Reboot now" of USB 3.0 waits for the queue
    { perf::Timer t_("frame: FollowFinish"); FollowFinish(); }
    { perf::Timer t_("frame: FollowUsb"); FollowUsb(); }
    { perf::Timer t_("frame: FollowPush"); FollowPush(); }
    { perf::Timer t_("frame: FollowQueueAsk"); FollowQueueAsk(); }
    { perf::Timer t_("frame: FollowQueueWeb"); FollowQueueWeb(); }
    { perf::Timer t_("frame: CheckSdSpace"); CheckSdSpace(); }
    { perf::Timer t_("frame: CheckBattery"); CheckBattery(); }
    { perf::Timer t_("frame: CheckWifi"); CheckWifi(); }

    // The grid goes on after the finger with its speed, slowing down; past an end it stops quickly
    // and comes back (16A)
    if (!m_dragging && m_fling_v != 0) {
        const float max = MaxScroll(), top = m_refreshing ? -46.f : 0.f;
        m_scroll = std::clamp(m_scroll + m_fling_v * dt, top - 90.f, max + 90.f);
        const bool out = m_scroll < top || m_scroll > max;
        m_fling_v *= std::exp(-dt * (out ? 18.f : 3.f));
        if (out && !m_fling_end_told) {
            m_fling_end_told = true;
            TouchTick(); // the end, felt
        }
        m_scroll_target = m_scroll;
        m_scroll_seen_at = m_time;
        if (std::fabs(m_fling_v) < 30 || !GridTab() || m_detail) {
            m_fling_v = 0;
            m_scroll_target = std::clamp(m_scroll, top, max);
        }
    }
    // a list let go with speed goes on a few rows
    if (m_list_fling != 0 && !m_touching) {
        m_list_fling_d += m_list_fling * dt;
        m_list_fling *= std::exp(-dt * 5.f);
        const int steps = m_list_fling_pitch > 0 ? int(m_list_fling_d / m_list_fling_pitch) : 0;
        if (steps) {
            m_drag_pending += steps;
            m_list_fling_d -= float(steps) * m_list_fling_pitch;
        }
        if (std::fabs(m_list_fling) < 150) m_list_fling = 0;
    }

    // Ease the grid toward where it should be (frame-rate independent).
    if (!m_dragging) {
        m_scroll += (m_scroll_target - m_scroll) * (1.f - std::exp(-dt * 14.f));
        if (std::fabs(m_scroll_target - m_scroll) < 0.5f) m_scroll = m_scroll_target;
    }

    m_gfx.CheckResize();
    perf::Lap(perf::kUpdate);
    NVGcontext* vg = m_gfx.BeginFrame();
    perf::Lap(perf::kAcquire);
    SeeQueue(); // what Update changed in the queue, drawn in this frame
    perf::Count(m_covers.Upload(vg));
    perf::Lap(perf::kUpload);
    if (ScreenDark()) {
        DrawDark(vg); // nothing of the screen can be seen: the install keeps the CPU
    } else {
        Draw(vg);
    }
    perf::Lap(perf::kDraw);
    m_gfx.EndFrame();
    perf::Lap(perf::kPresent);
    return !m_quit;
}

u64 App::FocusSignature() const {
    // every place a focus can be, mixed into one number
    u64 h = 1469598103934665603ull;
    const auto mix = [&](long long v) { h = (h ^ u64(v)) * 1099511628211ull; };
    mix(int(m_tab));
    mix(m_detail);
    mix(m_focus);
    mix(m_filter);
    mix(m_setting);
    mix(m_pair_focus);
    mix(m_detail_focus);
    mix(m_to_nand);
    mix(m_queue_focus);
    mix(m_installed_view);
    mix(m_installed_focus);
    mix(int(m_console_app & 0xFFFFFFF));
    mix(m_console_focus);
    mix(m_console_item);
    mix(m_ui.console_size);
    mix(int(std::count(m_console_marks.begin(), m_console_marks.end(), 1)));
    mix(int(m_dialog.kind));
    mix(m_dialog.focus);
    mix(m_menu);
    mix(m_act);
    mix(m_act_focus);
    mix(m_dialog.keep_saves);
    mix(m_menu_focus);
    mix(m_ui.sort);
    mix(m_wizard);
    mix(m_wiz_step);
    mix(m_wiz_group);
    mix(m_wiz_item);
    return h;
}

void App::PlaySounds(u64 down, u64 before) {
    if (!m_ui.sounds) {
        return;
    }
    if (down & HidNpadButton_A) {
        m_sound.Play(synth::Effect::Select);
    } else if (down & HidNpadButton_B) {
        m_sound.Play(synth::Effect::Back);
    } else if (FocusSignature() != before) {
        m_sound.Play(synth::Effect::Move);
    }
}

void App::AddHints(const std::vector<ui::HitRect>& rects) {
    for (const auto& r : rects) {
        m_hits.push_back({r, HitKind::Button, 0});
    }
}

void App::Draw(NVGcontext* vg) {
    m_hits.clear();
    m_hint_rects.clear();
    m_list_pitch = 0; // set again by the list drawn, if any
    const bool low = m_queue && m_queue->Busy() && m_battery >= 0 && m_battery < 20 && !m_charging;
    {
        perf::Timer t_("draw: background and aurora");
        StepStyle();
        Background(vg, m_theme);
        // the slow lights behind, paused while installing on little battery
        if (!low) ui::Aurora(vg, m_theme);
    }
    // the light of the game in focus (1A), behind the library and the page of a game; paused, as
    // the aurora, while installing on little battery
    if (!m_wizard && GridTab() && !low) {
        const bool shown = m_tab == Tab::Shops ? m_shop_active : m_link.CurrentState() == Link::State::Connected;
        if (shown && m_detail && m_detail_game >= 0 && m_detail_game < int(m_games.size())) {
            perf::Timer t_("draw: backdrop");
            DrawBackdrop(vg, m_games[m_detail_game], true);
        } else if (shown && !m_detail && !m_view.empty()) {
            perf::Timer t_("draw: backdrop");
            DrawBackdrop(vg, m_games[m_view[std::clamp(m_focus, 0, int(m_view.size()) - 1)]], false);
        }
    }
    // the scene of the style, over the light of the game, paused as the aurora
    if (!low) ui::Scene(vg, m_theme);
    if (m_wizard) {
        DrawWizard(vg);
        AddHints(m_hint_rects);
        if (m_time < m_notice_until && !m_notice.empty()) {
            const float w = ui::TextWidth(vg, 16, m_notice.c_str()) + 48;
            ui::FillRound(vg, (kWidth - w) / 2, 600, w, 44, 22, m_theme.panel2);
            ui::Text(vg, kWidth / 2, 622, 16, m_theme.text, m_notice.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        }
        DrawTouchFeedback(vg);
        return;
    }
    ui::HitRect tabs[kTabs];
    int queued = 0;
    if (m_queue) {
        queued = m_queue->Waiting() + (m_queue->GetLive().running ? 1 : 0);
    }
    const float sd_used = m_sd_total_bytes > 0 && m_sd_free_bytes >= 0 ? 1.f - float(double(m_sd_free_bytes) / double(m_sd_total_bytes)) : -1.f;
    perf::Timer t_top("draw: top bar");
    // the light of the tab slides from the one before (2A); still, it is there at once
    const bool changing = m_tab != m_shown_tab;
    const int from_tab = changing ? int(m_shown_tab) : m_tab_before;
    const float slide = changing ? 0.f : ui::EaseOut(ui::Appear(m_tab_at, 0.26f));
    const float capsule = ui::TopBar(vg, m_theme, int(m_tab), queued, tabs, m_sd_free.c_str(), m_battery, m_charging, m_wifi, &m_wifi_rect, sd_used,
                                     from_tab, slide);
    if (m_wifi > -2) m_hits.push_back({m_wifi_rect, HitKind::Wifi, 0});
    if (m_shop_active && m_tab == Tab::Library) {
        // which shop the library shows
        char badge[160];
        std::snprintf(badge, sizeof(badge), TrM().sh_badge, m_shop_name.c_str());
        const float bx = tabs[kTabs - 1].x + tabs[kTabs - 1].w + 30;
        const float bw = std::min({260.f, ui::TextWidth(vg, 14, badge) + 28, capsule - 12 - bx});
        NVGcolor bg = m_theme.work;
        bg.a = 0.16f;
        ui::FillRound(vg, bx, 22, bw, 28, 14, bg);
        nvgSave(vg);
        nvgScissor(vg, bx + 8, 22, bw - 16, 28);
        ui::Text(vg, bx + 14, 36, 14, m_theme.text, badge);
        nvgRestore(vg);
    }
    for (int i = 0; i < kTabs; i++) {
        m_hits.push_back({tabs[i], HitKind::Tab, i});
        m_tab_rects[i] = tabs[i];
    }
    if (m_tab != m_shown_tab) {
        // the content comes in from the side of the new tab (L wraps from the first to the last);
        // slid by a finger, from where the finger left the old one (20A)
        const int from = int(m_shown_tab), to = int(m_tab);
        m_tab_dir = ((to - from + kTabs) % kTabs) <= 2 ? 1.f : -1.f;
        m_tab_before = from;
        m_shown_tab = m_tab;
        m_tab_at = m_time;
        m_tab_from = m_tab_from_next;
        m_tab_from_next = 0;
        m_quick = -1;
    }
    if (m_detail != m_shown_detail) {
        m_shown_detail = m_detail;
        m_detail_at = m_time;
    }
    const float tab_e = float(std::min(1.0, (m_time - m_tab_at) / 0.22));
    nvgSave(vg);
    float shift = 0;
    if (tab_e < 1 && !ui::Still()) {
        const float k = 1 - tab_e;
        shift = m_tab_from != 0 ? m_tab_from * k * k * k : 30 * m_tab_dir * k * k;
    } else {
        m_tab_from = 0;
    }
    // a finger sliding sideways moves the content; let go too soon, it springs back (20A)
    shift += m_swipe_dx;
    if (m_swipe_back != 0) {
        const float back = 1 - ui::EaseOut(ui::Appear(m_swipe_back_at, 0.22f));
        shift += m_swipe_back * back;
        if (back <= 0) m_swipe_back = 0;
    }
    if (shift != 0) nvgTranslate(vg, shift, 0);
    t_top.Stop();
    perf::Timer t_tab("draw: the tab");
    switch (m_tab) {
        case Tab::Library: {
            const Link::View link = m_link.Snapshot();
            if (link.state == Link::State::Connected) {
                if (m_detail) {
                    DrawDetail(vg);
                } else {
                    DrawLibrary(vg);
                }
            } else {
                DrawPairing(vg, link);
            }
            break;
        }
        case Tab::Shops:
            FollowShop();
            if (!m_shop_active) DrawShops(vg);
            else if (m_detail) DrawDetail(vg);
            else DrawLibrary(vg);
            break;
        case Tab::Queue: DrawQueue(vg); break;
        case Tab::Installed: DrawInstalled(vg); break;
        case Tab::More: DrawMore(vg); break;
        case Tab::Settings: DrawSettings(vg); break;
    }
    nvgRestore(vg);
    t_tab.Stop();
    perf::Timer t_over("draw: over the tab");
    DrawTransitions(vg);
    DrawJumpLetter(vg);
    AddHints(m_hint_rects);
    if (m_menu && GridTab() && !m_detail) {
        DrawMenu(vg);
    }
    if (m_quick >= 0 && GridTab() && !m_detail) {
        DrawQuick(vg);
    }
    if (m_act && CurrentActScreen() == ActParent(m_act_screen)) {
        DrawActions(vg);
    }
    if (m_dialog.kind != DialogKind::None && m_dice_at < 0) {
        DrawDialog(vg);
    }
    DrawDice(vg); // a game at random (21A): the question waits for the dice
    if (m_time < m_notice_until && !m_notice.empty()) {
        // a short notice above the hint bar
        const float mark = m_notice_ok ? 28.f : 0.f; // something done: a green tick before it
        const float w = ui::TextWidth(vg, 16, m_notice.c_str()) + 48 + mark;
        // it comes up from below with a little spring
        const float rise = ui::EaseOutBack(ui::Appear(m_notice_at, 0.35f));
        const float x = (kWidth - w) / 2, y = 600 + (1 - rise) * 34;
        nvgGlobalAlpha(vg, float(std::min(1.0, (m_notice_until - m_time) * 3)) * std::min(1.f, rise * 1.5f));
        NVGcolor fill = m_theme.panel2;
        if (m_notice_ok) {
            fill = m_theme.ok;
            fill.a = 0.18f;
            ui::FillRound(vg, x, y, w, 44, 22, m_theme.panel2);
        }
        ui::FillRound(vg, x, y, w, 44, 22, fill);
        ui::StrokeRound(vg, x + .5f, y + .5f, w - 1, 43, 22, m_notice_ok ? m_theme.ok : m_theme.work, 1.5f);
        if (m_notice_ok) ui::CheckMark(vg, x + 30, y + 22, 14, m_theme.ok, 2.6f);
        ui::Text(vg, x + (w + mark) / 2, y + 22, 16, m_theme.text, m_notice.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgGlobalAlpha(vg, 1.f);
    }
    DrawFinish(vg);
    DrawTour(vg);
    DrawWifiCard(vg);
    DrawBoot(vg);
    DrawStyleWave(vg);
    DrawTouchFeedback(vg);
    DrawSaver(vg);
    DrawScreenOff(vg);
    DrawLaunch(vg);
}

void App::DrawTransitions(NVGcontext* vg) {
    // what is new appears out of the background: a veil of it that clears (the top bar stays)
    const auto veil = [&](float amount) {
        NVGcolor c = m_theme.bg;
        c.a = amount;
        nvgBeginPath(vg);
        nvgRect(vg, 0, 73, kWidth, kHeight - 73);
        nvgFillColor(vg, c);
        nvgFill(vg);
    };
    const float tab_e = float(std::min(1.0, (m_time - m_tab_at) / 0.22));
    if (tab_e < 1) {
        veil((m_tab_from != 0 ? 0.45f : 0.9f) * (1 - tab_e)); // slid by a finger: it was already moving
    }
    const double age = m_time - m_detail_at;
    if (!GridTab() || age >= 0.3 || m_detail_game < 0 || m_detail_game >= int(m_games.size())) {
        return;
    }
    // the page of a game grows out of its cover, and goes back into it
    const float p = float(age / 0.3), e = 1 - (1 - p) * (1 - p) * (1 - p);
    veil((ui::Lively() ? 0.4f : 0.85f) * (1 - e)); // lively, its parts come in one after another (7A)
    const float gx = kGridX + (m_focus % g_columns) * (g_cover + kGridGap);
    const float gy = kGridTop + (m_focus / g_columns) * g_row_pitch - m_scroll;
    const float dx = 48, dy = 72 + 34, ds = 330;
    const float k = m_detail ? e : 1 - e; // 0: in the grid, 1: on the page
    const float x = gx + (dx - gx) * k, y = gy + (dy - gy) * k, s = g_cover + (ds - g_cover) * k;
    const Game& g = m_games[m_detail_game];
    nvgBeginPath(vg);
    nvgRect(vg, x - 30, y - 10, s + 60, s + 60);
    nvgFillPaint(vg, nvgBoxGradient(vg, x + 6, y + 12, s - 12, s, 14, 30, nvgRGBAf(0, 0, 0, 0.4f), nvgRGBAf(0, 0, 0, 0)));
    nvgFill(vg);
    ui::Cover(vg, m_theme, x, y, s, PlaceholderColor(g.key), Initial(g.name).c_str(), nullptr, false, false, 10 + 4 * k,
              CoverOf(g));
    CoverLoading(vg, x, y, s, g.cover);
}

void App::FollowFinish() {
    // two pulses of 70 ms, 120 ms apart
    if (m_rumble_step >= 0 && m_time >= m_rumble_next) {
        static constexpr double kGaps[] = {0.07, 0.12, 0.07};
        const bool on = m_rumble_step % 2 == 0;
        SendRumble(on);
        if (m_rumble_step < 3) {
            m_rumble_next = m_time + kGaps[m_rumble_step];
            m_rumble_step++;
        } else {
            m_rumble_step = -1;
        }
    }
    if (!m_queue) {
        return;
    }
    const bool busy = m_queue->Busy();
    if (!busy && m_queue_was_busy) {
        // the queue has just ended: what this batch did
        for (const auto& j : m_queue->Jobs()) {
            const bool ended = j.state == InstallJob::State::Done || j.state == InstallJob::State::Failed;
            if (!ended || std::find(m_counted_jobs.begin(), m_counted_jobs.end(), j.id) != m_counted_jobs.end()) {
                continue;
            }
            m_counted_jobs.push_back(j.id);
            if (j.state == InstallJob::State::Done) {
                m_batch_done++;
                m_batch_bytes += j.size;
                // the last game installed falls into the dock (10A)
                m_finish_cover = j.cover;
                m_finish_key = j.color_key;
                m_finish_name = j.game;
            } else {
                m_batch_failed++;
            }
        }
        if (m_batch_done > 0) {
            const auto& s = Tr();
            char text[160];
            if (m_batch_failed > 0) {
                std::snprintf(text, sizeof(text), s.finish_some_failed, m_batch_done, m_batch_failed);
            } else {
                std::snprintf(text, sizeof(text), s.finish_all, m_batch_done, SizeText(m_batch_bytes).c_str());
            }
            if (m_batch_failed > 0) Notice(text);
            else NoticeOk(text);
            m_notice_until = m_time + 5;
            if (m_ui.finish_effect && m_batch_failed == 0 && ui::Lively()) {
                // the notice comes up once the game is in the dock
                m_notice_at = m_time + 1.0;
                m_notice_until = m_time + 6;
            }
            if (m_ui.finish_effect) {
                m_finish_at = m_time;
                m_rumble_step = 0;
                m_rumble_next = m_time;
                if (m_ui.sounds) {
                    m_sound.Play(synth::Effect::Finish);
                }
            }
        }
        m_batch_done = m_batch_failed = 0;
        m_batch_bytes = 0;
    }
    m_queue_was_busy = busy;
}

void App::DrawFinish(NVGcontext* vg) {
    // 10A: the cover of the last game installed falls into the slot of the dock (the drawing of the
    // icon), its bar lights, blue and violet sparks fly, and the green notice comes up. Gentle
    // animations show the game in the dock without the fall; with none, it shows still. The
    // buttons keep working all along.
    const double t = m_time - m_finish_at;
    const double total = 1.9;
    if (t < 0 || t > total) {
        return;
    }
    const Theme& th = m_theme;
    // the glow of the edges, as before
    const float glow = float(t < 0.25 ? t / 0.25 : std::max(0.0, 1 - (t - 0.25) / 1.15));
    NVGcolor edge = th.work;
    edge.a = 0.5f * glow;
    NVGcolor clear = th.work;
    clear.a = 0;
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, kWidth, kHeight);
    nvgFillPaint(vg, nvgBoxGradient(vg, 60, 60, kWidth - 120, kHeight - 120, 40, 220, clear, edge));
    nvgFill(vg);

    const bool lively = ui::Lively();
    // everything in and out: a light dark veil behind the dock
    const float shown = float(t < 0.15 ? t / 0.15 : t > 1.5 ? std::max(0.0, 1 - (t - 1.5) / 0.4) : 1.0);
    nvgBeginPath(vg);
    nvgRect(vg, 0, 73, kWidth, 664 - 73);
    nvgFillColor(vg, nvgRGBAf(0, 0, 0, 0.35f * shown));
    nvgFill(vg);
    nvgGlobalAlpha(vg, shown);
    const float ds = 256, dx = (kWidth - ds) / 2, dy = 196;
    // the light under the dock
    NVGcolor halo = th.work;
    halo.a = 0.3f;
    nvgBeginPath(vg);
    nvgCircle(vg, dx + ds / 2, dy + 170, 220);
    nvgFillPaint(vg, nvgRadialGradient(vg, dx + ds / 2, dy + 170, 40, 220, halo, clear));
    nvgFill(vg);
    // the fall: faster and faster, from above and a little turned, into the slot
    const float p = lively ? float(std::clamp((t - 0.1) / 0.55, 0.0, 1.0)) : 1.f;
    const float q = p * p;
    const float cs = 90 * (1.25f - 0.25f * q);
    const std::string initial = Initial(m_finish_name);
    Falling f{&th, dx + ds / 2 - cs / 2, dy - 170 + (90 + 170 - 0) * q, cs, -0.07f * (1 - q), PlaceholderColor(m_finish_key), initial.c_str(),
              m_covers.Get(m_finish_cover)};
    const float bar = lively ? ui::EaseOut(float(std::clamp((t - 0.6) / 0.4, 0.0, 1.0))) : 1.f;
    ui::Dock(vg, dx, dy, ds, bar, DrawFalling, &f);
    // the sparks, out of the slot
    if (lively && t > 0.62 && t < 1.35) {
        const float u = float((t - 0.62) / 0.7);
        for (int i = 0; i < 14; i++) {
            const float a = float(i) / 14 * 2 * NVG_PI + 0.2f;
            const float d = ui::EaseOut(u) * (110 + float(i % 3) * 40);
            nvgBeginPath(vg);
            nvgCircle(vg, dx + ds / 2 + std::cos(a) * d, dy + 114 + std::sin(a) * d * 0.7f, 5 * (1 - 0.7f * u));
            nvgFillColor(vg, Hex(i % 2 ? 0x7c5cff : 0x3db7ea, 1 - u));
            nvgFill(vg);
        }
    }
    nvgGlobalAlpha(vg, 1.f);
}

void App::Notice(const std::string& text) {
    m_notice = text;
    m_notice_at = m_time;
    m_notice_until = m_time + 3.5;
    m_notice_ok = false;
}

void App::NoticeOk(const std::string& text) {
    Notice(text);
    m_notice_ok = true;
    m_notice_until = m_time + 5;
}

void App::CheckSdSpace() {
    if (m_space_running && m_space_ready) {
        // what the thread read: the same as was read here before
        m_space_thread.join();
        m_space_running = false;
        m_space_ready = false;
        const SpaceRead& r = m_space_read;
        if (r.sd_free >= 0) {
            m_sd_free_bytes = r.sd_free;
            m_sd_free = SizeText(u64(r.sd_free));
        }
        if (r.sd_total > 0) m_sd_total_bytes = r.sd_total;
        m_nand_free_bytes = r.nand_free;
        m_nand_total_bytes = r.nand_total;
    }
    if (m_space_running || m_time - m_sd_checked < 10) {
        return;
    }
    m_sd_checked = m_time;
    m_space_running = true;
    // the values it starts from are kept when a read fails, as before
    m_space_read = SpaceRead{-1, -1, m_nand_free_bytes, m_nand_total_bytes};
    m_space_thread = std::thread([this] {
        SpaceRead& r = m_space_read;
        FsFileSystem fs;
        if (R_SUCCEEDED(fsOpenSdCardFileSystem(&fs))) {
            char root[FS_MAX_PATH] = "/";
            s64 free_bytes = 0, total_bytes = 0;
            if (R_SUCCEEDED(fsFsGetFreeSpace(&fs, root, &free_bytes))) r.sd_free = free_bytes;
            if (R_SUCCEEDED(fsFsGetTotalSpace(&fs, root, &total_bytes)) && total_bytes > 0) r.sd_total = total_bytes;
            fsFsClose(&fs);
        }
        StorageSpace(NcmStorageId_BuiltInUser, r.nand_free, r.nand_total);
        m_space_ready = true;
    });
}

// Starts the catalog and the covers of the library linked, and takes a new catalog when it comes.
void App::FollowLink() {
    const Link::View link = m_link.Snapshot();
    if (link.state == Link::State::Connected) {
        const Config c = m_link.CurrentConfig();
        if (c.url != m_loaded_url) {
            m_loaded_url = c.url;
            m_covers.Stop();
            m_covers.Free(m_gfx.vg());
            m_covers.Start(c.url, c.key);
            m_catalog.Load(c.url, c.key);
        }
    }
    if (!m_shop_active && m_catalog.Version() != m_games_version) {
        m_games_version = m_catalog.Version();
        // the same game stays in focus; the first time, the one in focus when NXDock was closed
        m_keep_key = m_focus_restored ? FocusedKey() : m_ui.focus;
        {
            perf::Timer timer("app: take the catalog");
            m_games = m_catalog.TakeGames();
            m_games_epoch++;
        }
        if (!m_games.empty()) {
            if (!m_focus_restored) perf::Mark("first list of games taken");
            m_focus_restored = true;
        }
        RebuildView();
    }
    // once at start (the wizard's "look for updates"): how many updates the library has for what
    // is installed; − Options installs them
    if (!m_updates_told && m_focus_restored && !m_wizard && m_tab == Tab::Library) {
        m_updates_told = true;
        const size_t updates = m_ui.check_updates ? PendingUpdates().size() : 0;
        if (updates > 0) {
            char text[200];
            std::snprintf(text, sizeof(text), Tr().updates_found, int(updates));
            Notice(text);
            m_notice_until = m_time + 6;
        }
    }
}

std::string App::FocusedKey() const {
    if (m_focus < 0 || m_focus >= int(m_view.size()) || m_view[m_focus] >= int(m_games.size())) {
        return "";
    }
    return m_games[m_view[m_focus]].key;
}

void App::DrawBoot(NVGcontext* vg) {
    // 4A: the icon assembles itself (the card falls into the dock, the bar lights, the lamp comes
    // on) with the name under it, then it flies to its place in the top bar while the library
    // shows; gentle animations only fade the icon. Any button or touch skips it (Frame).
    if (m_boot_at < 0 || m_wizard) return;
    const double a = m_time - m_boot_at;
    const bool lively = ui::Lively();
    if (a < 0 || a > (lively ? 1.5 : 0.7)) return;
    const Theme& t = m_theme;
    float veil, x = 540, y = 200, s = 200, card = 1, bar = 1, lamp = 1, name = 0;
    if (lively) {
        veil = a < 1.05 ? 1.f : float(std::max(0.0, 1 - (a - 1.05) / 0.35));
        card = ui::EaseOutBack(float(std::clamp((a - 0.15) / 0.5, 0.0, 1.0)));
        bar = ui::EaseOut(float(std::clamp((a - 0.55) / 0.45, 0.0, 1.0)));
        lamp = float(std::clamp((a - 0.95) / 0.2, 0.0, 1.0));
        name = float(std::clamp((a - 0.35) / 0.3, 0.0, 1.0)) * float(std::clamp((1.1 - a) / 0.2, 0.0, 1.0));
        const float m = float(std::clamp((a - 1.05) / 0.45, 0.0, 1.0));
        const float e = m < 0.5f ? 4 * m * m * m : 1 - std::pow(-2 * m + 2, 3.f) / 2;
        x += (36 - x) * e;
        y += (19 - y) * e;
        s += (34 - s) * e;
    } else {
        veil = a < 0.35 ? 1.f : float(std::max(0.0, 1 - (a - 0.35) / 0.35));
        name = veil;
    }
    NVGcolor bg = t.bg;
    bg.a = veil;
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, kWidth, kHeight);
    nvgFillColor(vg, bg);
    nvgFill(vg);
    nvgGlobalAlpha(vg, lively ? 1.f : veil);
    // a soft light under the icon while it is in the middle; the other styles have their own touch
    if (veil > 0.01f && lively && t.style != kStyleNXDock) {
        DrawBootStyle(vg, a, x + s / 2, y + s / 2, s, veil);
    } else if (veil > 0.01f) {
        NVGcolor glow = t.work;
        glow.a = 0.25f * veil;
        nvgBeginPath(vg);
        nvgCircle(vg, x + s / 2, y + s / 2, s);
        nvgFillPaint(vg, nvgRadialGradient(vg, x + s / 2, y + s / 2, s * 0.3f, s, glow, Hex(0, 0)));
        nvgFill(vg);
    }
    ui::Brand(vg, x, y, s, card, bar, lamp);
    if (name > 0.01f) {
        nvgGlobalAlpha(vg, name);
        if (t.style == kStyleHuntress) nvgTextLetterSpacing(vg, 6);
        ui::Title(vg, kWidth / 2, 448, 36, t.text, t.style == kStyleHuntress ? "NXDOCK" : "NXDock", NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgTextLetterSpacing(vg, 0);
    }
    nvgGlobalAlpha(vg, 1.f);
}

void App::DrawSkeleton(NVGcontext* vg, const char* line, float frac, bool known) {
    // 14A: the shape of the library while it is read, a light running over it, and below how far
    const Theme& t = m_theme;
    NVGcolor ghost = nvgLerpRGBA(t.panel, t.muted, 0.16f);
    ghost.a = 0.9f;
    const float widths[] = {110, 140, 120, 170, 80};
    float x = kGridX;
    for (float w : widths) {
        NVGcolor c = ghost;
        c.a = 0.55f;
        ui::FillRound(vg, x, 92, w, 34, 17, c);
        x += w + 10;
    }
    const int rows = int((g_view_h + kGridGap) / g_row_pitch);
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < g_columns; c++) {
            const float cx = kGridX + c * (g_cover + kGridGap), cy = kGridTop + r * g_row_pitch;
            ui::FillRound(vg, cx, cy, g_cover, g_cover, 10, ghost);
            if (ui::Still()) continue;
            // the light: a soft band that crosses each cover, one after another
            const float p = float(std::fmod(m_time * 0.7 + (r + c) * 0.08, 1.6)) - 0.3f;
            const float gx = cx + p * g_cover * 1.4f, bw = g_cover * 0.35f;
            nvgSave(vg);
            nvgIntersectScissor(vg, cx, cy, g_cover, g_cover);
            const NVGcolor clear = nvgRGBAf(1, 1, 1, 0), light = nvgRGBAf(1, 1, 1, t.dark ? 0.08f : 0.55f);
            nvgBeginPath(vg);
            nvgRoundedRect(vg, cx, cy, g_cover, g_cover, 10);
            nvgFillPaint(vg, nvgLinearGradient(vg, gx - bw, cy, gx, cy, clear, light));
            nvgFill(vg);
            nvgBeginPath(vg);
            nvgRect(vg, gx, cy, bw, g_cover);
            nvgFillPaint(vg, nvgLinearGradient(vg, gx, cy, gx + bw, cy, light, clear));
            nvgFill(vg);
            nvgRestore(vg);
        }
    }
    const float by = kGridTop + g_view_h + 16;
    NVGcolor c = ghost;
    c.a = 0.55f;
    ui::FillRound(vg, kGridX, by, 360, 30, 8, c);
    ui::Text(vg, kGridX, by + 58, 16, t.muted, line);
    ui::Progress(vg, t, kGridX, by + 76, 460, 6, known ? frac : 0.f, t.work, m_time, true);
}

bool App::InSubScreen() const {
    // a page or a tool open in a tab: sliding to the right goes back, as B (20A)
    return (GridTab() && m_detail) || (m_tab == Tab::More && m_more != MoreScreen::Tiles) ||
           (m_tab == Tab::Installed && m_installed_view == 0 && m_console_app != 0) || (m_tab == Tab::Shops && m_shop_active);
}

void App::DrawSaver(NVGcontext* vg) {
    if (!m_saver_on) {
        return;
    }
    // the screen goes dark; only a thin line of the install and how to light it again remain
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, kWidth, kHeight);
    nvgFillColor(vg, nvgRGBAf(0, 0, 0, 0.88f));
    nvgFill(vg);
    const auto live = m_queue->GetLive();
    NVGcolor dim = m_theme.work;
    dim.a = 0.6f;
    if (live.running) {
        ui::Progress(vg, m_theme, kWidth / 2 - 200, 352, 400, 6, JobFraction(live), dim, m_time);
    }
    ui::Text(vg, kWidth / 2, 384, 16, nvgRGBAf(1, 1, 1, 0.45f), Tr().saver_text, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
}

} // namespace nxdock
