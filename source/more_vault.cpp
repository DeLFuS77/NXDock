// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The vault of saves (6A of the visual test of phases 9 to 15, and 9A of the test of the new tools):
// the kind of save on top (a user's, the console's, BCAT; ZL changes it, ZR the user), the games
// with a save on the left, the copies on the right: those of the library and, without a library,
// those of the SD card. X copies now (with a note that can stay empty), A restores a copy (always
// with its question, Cancel in focus), − has Copy all and Delete the save (a copy first). Copies can
// also be made by themselves every so many days, when NXDock opens.
#include "app.hpp"

#include "draw.hpp"
#include "errors.hpp"
#include "i18n.hpp"
#include "i18n_more.hpp"
#include "i18n_tools.hpp"

#include <algorithm>
#include <ctime>

namespace nxdock {
namespace {

constexpr float kLeftW = 430;

std::string When(long long when) {
    const time_t t = time_t(when);
    const struct tm* lt = localtime(&t);
    if (!lt) return "";
    char text[64];
    std::snprintf(text, sizeof(text), "%02d/%02d/%04d %02d:%02d", lt->tm_mday, lt->tm_mon + 1, lt->tm_year + 1900, lt->tm_hour, lt->tm_min);
    return text;
}

// The keyboard of the console for the note: true unless cancelled (an empty note is fine).
bool AskNote(const char* guide, std::string& out) {
    SwkbdConfig kbd;
    if (R_FAILED(swkbdCreate(&kbd, 0))) return false;
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetGuideText(&kbd, guide);
    swkbdConfigSetStringLenMax(&kbd, 200);
    char text[256] = {};
    const Result rc = swkbdShow(&kbd, text, sizeof(text));
    swkbdClose(&kbd);
    if (R_FAILED(rc)) return false;
    out = text;
    return true;
}

} // namespace

void App::OpenVault() {
    if (!m_vault) m_vault = std::make_unique<Vault>();
    m_vault_users = m_vault->Users();
    m_vault_user = std::clamp(m_vault_user, 0, std::max(0, int(m_vault_users.size()) - 1));
    if (m_vault_kind == 0 && m_vault_users.empty()) {
        m_vault_saves.clear();
    } else {
        m_vault_saves = m_vault->Saves(m_vault_users.empty() ? Vault::User{} : m_vault_users[size_t(m_vault_user)], m_vault_kind);
    }
    m_vault_focus = 0;
    m_vault_copy = 0;
    m_vault_side = 0;
    m_vault_menu = false;
    const Config c = m_link.CurrentConfig();
    if (c.Linked()) m_vault->StartList(c.url, c.key);
}

std::vector<LibrarySave> App::VaultCopies() {
    std::vector<LibrarySave> out;
    if (!m_vault || m_vault_focus >= int(m_vault_saves.size())) return out;
    const std::string tid = TitleHex(m_vault_saves[m_vault_focus].app_id);
    for (const auto& s : m_vault->Snapshot().library) {
        if (s.title_id == tid && Vault::KindOf(s) == m_vault_kind) out.push_back(s);
    }
    return out;
}

std::vector<App::VaultRow> App::VaultRows() {
    std::vector<VaultRow> rows;
    for (const auto& l : VaultCopies()) {
        VaultRow r;
        r.lib = l;
        rows.push_back(r);
    }
    if (m_vault_focus < int(m_vault_saves.size())) {
        for (const auto& c : Vault::SdCopies(m_vault_saves[size_t(m_vault_focus)].app_id, m_vault_kind)) {
            // a user's copies of this user only
            if (m_vault_kind == 0 && !m_vault_users.empty() && !c.who.empty() && c.who != m_vault_users[size_t(m_vault_user)].name) continue;
            VaultRow r;
            r.sd = true;
            r.copy = c;
            rows.push_back(r);
        }
    }
    std::stable_sort(rows.begin(), rows.end(), [](const VaultRow& a, const VaultRow& b) { return a.When() > b.When(); });
    return rows;
}

void App::FollowVault() {
    if (!m_vault) return;
    const auto v = m_vault->Snapshot();
    if (v.finished == m_vault_finished) return;
    m_vault_finished = v.finished;
    const More& m = TrM();
    const Tools& tt = TrT();
    char text[120];
    if (m_vault_auto_running) {
        // the automatic copies: only a notice
        m_vault_auto_running = false;
        std::snprintf(text, sizeof(text), tt.sv_auto_done_fmt, v.copied);
        if (v.copied) NoticeOk(text);
    } else if (v.error.empty()) {
        if (v.copied) {
            std::snprintf(text, sizeof(text), tt.sv_all_done_fmt, v.copied);
            NoticeOk(text);
        } else {
            NoticeOk(v.last_delete ? tt.sv_deleted : v.last_restore ? m.restored : m.copied);
        }
        if (v.last_delete && m_tab == Tab::More && m_more == MoreScreen::Saves) {
            // the save went: the list again
            const int focus = m_vault_focus;
            OpenVault();
            m_vault_focus = std::clamp(focus, 0, std::max(0, int(m_vault_saves.size()) - 1));
        }
    } else {
        // the card of the error, as in the queue
        m_dialog = Dialog{};
        m_dialog.kind = DialogKind::VaultError;
        m_dialog.single = true;
        m_dialog.from = v.error;
    }
    // the copies of the library again
    const Config c = m_link.CurrentConfig();
    if (c.Linked()) m_vault->StartList(c.url, c.key);
}

// Copies of every save of the users by themselves, every so many days, when NXDock opens (9A).
void App::FollowSaveAuto() {
    if (m_save_auto_started || !m_ui.wizard_done || m_wizard || m_time < 10) return;
    m_save_auto_started = true;
    const long long now = std::time(nullptr);
    if (m_ui.save_auto_days <= 0 || now - m_ui.save_auto_last < (long long)m_ui.save_auto_days * 86400) return;
    if (!m_vault) m_vault = std::make_unique<Vault>();
    if (m_vault->Busy()) return;
    std::vector<Vault::Item> items;
    const auto installed = m_queue ? m_queue->Installed() : std::map<u64, InstalledState>{};
    for (const auto& u : m_vault->Users()) {
        for (const auto& sv : m_vault->Saves(u, 0)) {
            Vault::Item it;
            it.user = u;
            it.app_id = sv.app_id;
            const auto f = installed.find(sv.app_id);
            if (f != installed.end()) it.version = f->second.patch;
            items.push_back(it);
        }
    }
    const Config c = m_link.CurrentConfig();
    if (!items.empty() && m_vault->StartBackupAll(c.Linked() ? c.url : std::string{}, c.key, std::move(items), TrT().sv_auto_note, true)) {
        m_vault_auto_running = true;
        m_ui.save_auto_last = now;
        m_ui.Save();
    }
}

void App::DrawVault(NVGcontext* vg) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    const More& m = TrM();
    const Tools& tt = TrT();
    FollowVault();
    const auto v = m_vault ? m_vault->Snapshot() : Vault::View{};
    const bool linked = m_link.CurrentConfig().Linked();

    // the kind of save, on top of the games (9A)
    {
        const char* kinds[3] = {tt.sv_user, tt.sv_device, tt.sv_bcat};
        float x = 30;
        for (int k = 0; k < 3; k++) {
            std::string label = kinds[k];
            if (k == 0 && !m_vault_users.empty()) label += " · " + m_vault_users[size_t(m_vault_user)].name;
            const float w = ui::TextWidth(vg, 14, label.c_str()) + 26;
            const bool on = k == m_vault_kind;
            if (on) ui::FillRound(vg, x, 86, w, 28, 14, t.work);
            else ui::StrokeRound(vg, x, 86, w, 28, 14, t.line, 1.f);
            ui::Text(vg, x + w / 2, 100, 14, on ? t.on_work : t.muted, label.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            AddHit(x, 76, w, 48, HitKind::VaultKind, k);
            x += w + 8;
        }
    }

    // the games with a save
    nvgBeginPath(vg);
    nvgRect(vg, kLeftW, 73, 1, 664 - 73);
    nvgFillColor(vg, t.line);
    nvgFill(vg);
    if (m_vault_saves.empty()) {
        draw::Box(vg, 30, 134, kLeftW - 60, 15, t.muted, m_vault_kind == 0 ? m.no_saves : tt.sv_none_kind);
    }
    const int rows = 8;
    const float top = 128;
    m_vault_focus = std::clamp(m_vault_focus, 0, std::max(0, int(m_vault_saves.size()) - 1));
    const int first = std::max(0, m_vault_focus - rows + 1);
    if (!m_vault_saves.empty()) {
        const float fy = m_glide_list.Step(top + (m_vault_focus - first) * 64.f);
        ui::FillRound(vg, 14, fy, kLeftW - 28, 58, 10, t.panel2);
        if (m_vault_side == 0) ui::Focus(vg, t, 14, fy, kLeftW - 28, 58, 10, m_time);
    }
    ListArea(14, top, kLeftW - 28, rows * 64.f, 64);
    for (int r = 0; r < rows && first + r < int(m_vault_saves.size()); r++) {
        const int i = first + r;
        const auto& sv = m_vault_saves[i];
        const float y = top + r * 64;
        AddHit(14, y, kLeftW - 28, 58, HitKind::VaultGame, i);
        // the cover of the library when the game is there
        int image = 0;
        std::string key = sv.name;
        for (const auto& g : m_games) {
            if ((g.title_id & ~0xFFFull) == (sv.app_id & ~0xFFFull)) {
                image = m_covers.Get(g.cover);
                key = g.key;
                break;
            }
        }
        ui::Cover(vg, t, 26, y + 5, 48, PlaceholderColor(key), Initial(sv.name).c_str(), nullptr, false, false, 8, image);
        nvgSave(vg);
        nvgScissor(vg, 88, y, kLeftW - 200, 58);
        ui::Text(vg, 88, y + 22, 16, t.text, sv.name.c_str());
        ui::Text(vg, 88, y + 42, 13, t.muted, draw::Size(sv.size).c_str());
        nvgRestore(vg);
        int copies = 0;
        const std::string tid = TitleHex(sv.app_id);
        for (const auto& c : v.library) copies += c.title_id == tid && Vault::KindOf(c) == m_vault_kind;
        char count[32];
        if (copies) std::snprintf(count, sizeof(count), m.copies_fmt, copies);
        ui::Text(vg, kLeftW - 34, y + 29, 14, t.muted, copies ? count : "—", NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    }

    // the copies: of the library, and of the SD card
    const float rx = kLeftW + 30, rw = kWidth - rx - 40;
    draw::Section(vg, t, rx, 100, linked ? m.copies_lib : tt.sv_copies);
    const auto copies = VaultRows();
    if (!linked) {
        draw::Box(vg, rx, 124, rw, 15, t.muted, tt.sv_sd_note);
    } else if (copies.empty() && !m_vault_saves.empty()) {
        draw::Box(vg, rx, 124, rw, 15, t.muted, m.no_copies);
    }
    const float cy0 = linked ? 118 : 160;
    m_vault_copy = std::clamp(m_vault_copy, 0, std::max(0, int(copies.size()) - 1));
    const int shown = linked ? 6 : 5;
    for (int i = 0; i < int(copies.size()) && i < shown; i++) {
        const auto& c = copies[size_t(i)];
        const float y = cy0 + i * 66;
        draw::Panel(vg, t, rx, y, rw, 58, 10);
        AddHit(rx, y, rw, 58, HitKind::VaultCopy, i);
        // a little clock
        nvgBeginPath(vg);
        nvgCircle(vg, rx + 26, y + 29, 9);
        nvgStrokeColor(vg, t.work);
        nvgStrokeWidth(vg, 2);
        nvgStroke(vg);
        ui::FillRound(vg, rx + 25, y + 22, 2, 8, 1, t.work);
        ui::FillRound(vg, rx + 25, y + 28, 6, 2, 1, t.work);
        const std::string note = c.sd ? c.copy.note : c.lib.note;
        const std::string console = c.sd ? std::string{} : c.lib.console;
        nvgSave(vg);
        nvgScissor(vg, rx + 48, y, rw - 330, 58);
        ui::Text(vg, rx + 48, y + 21, 16, note.empty() ? t.muted : t.text, note.empty() ? m.no_note : note.c_str());
        const std::string sub = When(c.When()) + (console.empty() ? "" : " · " + console);
        ui::Text(vg, rx + 48, y + 41, 13, t.muted, sub.c_str());
        nvgRestore(vg);
        // where it is kept
        const char* where = c.sd ? tt.sv_sd : tt.sv_lib;
        const float pw = ui::TextWidth(vg, 12, where) + 16;
        ui::StrokeRound(vg, rx + rw - 250 - pw, y + 20, pw, 18, 9, c.sd ? t.cyan : t.muted, 1.f);
        ui::Text(vg, rx + rw - 250 - pw / 2, y + 29, 12, c.sd ? t.cyan : t.muted, where, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        ui::Text(vg, rx + rw - 120, y + 29, 14, t.muted, VersionText(c.sd ? c.copy.version : c.lib.version).c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        ui::Text(vg, rx + rw - 18, y + 29, 14, t.muted, draw::Size(c.sd ? c.copy.size : c.lib.size).c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    }
    if (m_vault_side == 1 && !copies.empty()) ui::Focus(vg, t, rx, m_glide_side.Step(cy0 + m_vault_copy * 66.f), rw, 58, 10, m_time);
    draw::Box(vg, rx, 520, rw, 14, t.muted, m.restore_note);

    // the work going on
    if (v.stage != Vault::Stage::Idle) {
        const char* what = v.stage == Vault::Stage::Sending     ? m.st_sending
                           : v.stage == Vault::Stage::Receiving ? m.st_receiving
                           : v.stage == Vault::Stage::Restoring ? m.st_restoring
                                                                : m.st_copying;
        draw::Panel(vg, t, rx, 566, rw, 74, 12);
        ui::Text(vg, rx + 20, 590, 15, t.text, what);
        const float frac = v.total ? float(double(v.done) / double(v.total)) : 0.f;
        ui::Progress(vg, t, rx + 20, 612, rw - 40, 10, frac, t.work, m_time);
    }

    std::vector<ui::Hint> hints;
    if (!m_vault_saves.empty()) hints.push_back({ui::glyph::X, m.copy_now, HidNpadButton_X});
    if (!copies.empty()) hints.push_back({ui::glyph::A, m.restore, HidNpadButton_A});
    if (!m_vault_saves.empty()) hints.push_back({ui::glyph::Minus, tt.sv_more, HidNpadButton_Minus});
    hints.push_back({ui::glyph::ZL, tt.sv_kind, HidNpadButton_ZL});
    if (m_vault_users.size() > 1 && m_vault_kind == 0) hints.push_back({ui::glyph::ZR, m.user_word, HidNpadButton_ZR});
    hints.push_back({ui::glyph::B, s.back, HidNpadButton_B});
    ui::HintBar(vg, t, nullptr, hints, m_hint_rects);
    if (m_vault_menu) DrawVaultMenu(vg);
}

// The menu of −: Copy all, Delete the save.
void App::DrawVaultMenu(NVGcontext* vg) {
    const Theme& t = m_theme;
    const Tools& tt = TrT();
    const float w = 360, row = 50, x = kLeftW - 40, y = 160;
    const float h = 50 + 2 * row + 12;
    AddHit(0, 73, kWidth, kHeight - 73 - 56, HitKind::FilesMenuBack);
    const float pop = ui::EaseOutBack(ui::Appear(m_list_at, 0.28f));
    nvgSave(vg);
    nvgGlobalAlpha(vg, std::min(1.f, pop * 1.4f));
    draw::Panel(vg, t, x, y, w, h, 16);
    AddHit(x, y, w, h, HitKind::Button);
    const std::string game = m_vault_focus < int(m_vault_saves.size()) ? m_vault_saves[size_t(m_vault_focus)].name : "";
    nvgSave(vg);
    nvgScissor(vg, x + 18, y + 8, w - 36, 36);
    ui::Text(vg, x + 22, y + 28, 15, t.muted, game.c_str());
    nvgRestore(vg);
    char count[48];
    std::snprintf(count, sizeof(count), tt.sv_games_fmt, int(m_vault_saves.size()));
    const char* labels[2] = {tt.sv_copy_all, tt.sv_delete};
    const char* rights[2] = {count, ""};
    for (int i = 0; i < 2; i++) {
        const float ry = y + 50 + i * row;
        if (i == m_vault_menu_focus) {
            ui::FillRound(vg, x + 8, ry, w - 16, row - 4, 10, t.panel2);
            ui::Focus(vg, t, x + 8, ry, w - 16, row - 4, 10, m_time);
        }
        AddHit(x + 8, ry, w - 16, row, HitKind::VaultMenuRow, i);
        ui::Text(vg, x + 24, ry + (row - 4) / 2, 17, i == 1 ? t.warn : t.text, labels[i]);
        if (rights[i][0]) ui::Text(vg, x + w - 24, ry + (row - 4) / 2, 14, t.muted, rights[i], NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    }
    nvgRestore(vg);
    std::vector<ui::HitRect> rects;
    ui::HintBar(vg, t, nullptr, {{ui::glyph::A, Tr().choose, HidNpadButton_A}, {ui::glyph::B, Tr().close, HidNpadButton_B}}, rects);
    AddHints(rects);
}

void App::VaultMenuDo(int row) {
    m_vault_menu = false;
    if (!m_vault || m_vault_saves.empty()) return;
    if (m_vault->Busy()) {
        Notice(TrM().st_copying);
        return;
    }
    const Config c = m_link.CurrentConfig();
    const std::string url = c.Linked() ? c.url : std::string{};
    const Vault::User user = m_vault_users.empty() ? Vault::User{} : m_vault_users[size_t(m_vault_user)];
    const auto installed = m_queue ? m_queue->Installed() : std::map<u64, InstalledState>{};
    const auto version = [&](unsigned long long app) {
        const auto it = installed.find(app);
        return it != installed.end() ? unsigned(it->second.patch) : 0u;
    };
    if (row == 0) {
        // every save of this kind (and user), one after the other
        std::vector<Vault::Item> items;
        for (const auto& sv : m_vault_saves) items.push_back({user, m_vault_kind, sv.app_id, version(sv.app_id)});
        m_vault->StartBackupAll(url, c.key, std::move(items), TrT().sv_all_note, false);
        return;
    }
    // deleting: always after a question, Cancel in focus; a copy first
    const auto& sv = m_vault_saves[size_t(m_vault_focus)];
    m_tool = ToolDialog{};
    char text[300];
    std::snprintf(text, sizeof(text), TrT().sv_del_title_fmt, sv.name.c_str());
    m_tool.title = text;
    m_tool.body = TrT().sv_del_body;
    m_tool.primary = TrT().sv_del_go;
    m_dialog = Dialog{};
    m_dialog.kind = DialogKind::SaveDelete;
    m_dialog.ids = {sv.app_id, version(sv.app_id)};
}

void App::ConfirmSaveDelete() {
    if (!m_vault || m_dialog.ids.size() < 2) return;
    const Config c = m_link.CurrentConfig();
    const Vault::User user = m_vault_users.empty() ? Vault::User{} : m_vault_users[size_t(m_vault_user)];
    m_vault->StartDelete(c.Linked() ? c.url : std::string{}, c.key, user, m_vault_kind, m_dialog.ids[0], unsigned(m_dialog.ids[1]),
                         TrT().sv_del_note);
}

void App::UpdateVault(u64 down, u64 held) {
    const More& m = TrM();
    if (m_vault_menu) {
        if (Repeat(down, held, HidNpadButton_AnyUp) || Repeat(down, held, HidNpadButton_AnyDown)) m_vault_menu_focus = 1 - m_vault_menu_focus;
        if (down & (HidNpadButton_B | HidNpadButton_Minus)) m_vault_menu = false;
        else if (down & HidNpadButton_A) VaultMenuDo(m_vault_menu_focus);
        return;
    }
    if (down & HidNpadButton_B) {
        if (m_vault_side == 1) {
            m_vault_side = 0;
        } else {
            m_more = MoreScreen::Tiles;
        }
        return;
    }
    if (Repeat(down, held, HidNpadButton_AnyLeft)) m_vault_side = 0;
    if (Repeat(down, held, HidNpadButton_AnyRight)) m_vault_side = 1;
    if (m_vault_side == 0) {
        const int before = m_vault_focus;
        if (Repeat(down, held, HidNpadButton_AnyUp) && m_vault_focus > 0) m_vault_focus--;
        if (Repeat(down, held, HidNpadButton_AnyDown) && m_vault_focus < int(m_vault_saves.size()) - 1) m_vault_focus++;
        if (before != m_vault_focus) m_vault_copy = 0;
    } else {
        const int n = int(VaultRows().size());
        if (Repeat(down, held, HidNpadButton_AnyUp) && m_vault_copy > 0) m_vault_copy--;
        if (Repeat(down, held, HidNpadButton_AnyDown) && m_vault_copy < std::min(n, 6) - 1) m_vault_copy++;
    }
    if (down & HidNpadButton_ZL) {
        // the kind of save: a user's, the console's, BCAT
        m_vault_kind = (m_vault_kind + 1) % 3;
        OpenVault();
        return;
    }
    if ((down & HidNpadButton_ZR) && m_vault_users.size() > 1 && m_vault_kind == 0) {
        m_vault_user = (m_vault_user + 1) % int(m_vault_users.size());
        m_vault_saves = m_vault->Saves(m_vault_users[size_t(m_vault_user)], m_vault_kind);
        m_vault_focus = m_vault_copy = 0;
    }
    const Config c = m_link.CurrentConfig();
    if (m_vault_saves.empty() || !m_vault) return;
    if (down & HidNpadButton_Minus) {
        m_vault_menu = true;
        m_vault_menu_focus = 0;
        m_list_at = m_time;
        return;
    }
    if (m_vault->Busy() && (down & (HidNpadButton_X | HidNpadButton_A))) {
        Notice(m.st_copying);
        return;
    }
    const auto& save = m_vault_saves[size_t(m_vault_focus)];
    const Vault::User user = m_vault_users.empty() ? Vault::User{} : m_vault_users[size_t(m_vault_user)];
    if (down & HidNpadButton_X) {
        std::string note;
        if (!AskNote(m.note_guide, note)) return;
        unsigned version = 0;
        if (m_queue) {
            const auto installed = m_queue->Installed();
            const auto it = installed.find(save.app_id);
            if (it != installed.end()) version = it->second.patch;
        }
        // to the library when linked, else to the SD card (9A)
        m_vault->StartBackup(c.Linked() ? c.url : std::string{}, c.key, user, save.app_id, version, note, m_vault_kind);
    }
    if ((down & HidNpadButton_A) && m_vault_side == 1) {
        const auto rows = VaultRows();
        if (m_vault_copy >= int(rows.size())) return;
        const auto& r = rows[size_t(m_vault_copy)];
        // always with its question, Cancel in focus; a copy of the SD card says its path
        m_dialog = Dialog{};
        m_dialog.kind = DialogKind::VaultRestore;
        if (r.sd) {
            m_dialog.save.note = r.copy.note;
            m_dialog.save.when = r.copy.when;
            m_dialog.save.version = r.copy.version;
            m_dialog.text = r.copy.path;
        } else {
            m_dialog.save = r.lib;
        }
        m_dialog.game = m_vault_focus;
    }
}

} // namespace nxdock
