// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The vault of saves (6A of the visual test of phases 9 to 15): the games with a save of the user
// on the left, the copies in the library on the right. X copies now (with a note that can stay
// empty), A restores a copy (always with its question, Cancel in focus), R changes the user.
#include "app.hpp"

#include "draw.hpp"
#include "errors.hpp"
#include "i18n.hpp"
#include "i18n_more.hpp"

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
    m_vault_saves = m_vault_users.empty() ? std::vector<Vault::Save>{} : m_vault->Saves(m_vault_users[m_vault_user]);
    m_vault_focus = 0;
    m_vault_copy = 0;
    m_vault_side = 0;
    const Config c = m_link.CurrentConfig();
    if (c.Linked()) m_vault->StartList(c.url, c.key);
}

std::vector<LibrarySave> App::VaultCopies() {
    std::vector<LibrarySave> out;
    if (!m_vault || m_vault_focus >= int(m_vault_saves.size())) return out;
    const std::string tid = TitleHex(m_vault_saves[m_vault_focus].app_id);
    for (const auto& s : m_vault->Snapshot().library) {
        if (s.title_id == tid) out.push_back(s);
    }
    return out;
}

void App::FollowVault() {
    if (!m_vault) return;
    const auto v = m_vault->Snapshot();
    if (v.finished == m_vault_finished) return;
    m_vault_finished = v.finished;
    const More& m = TrM();
    if (v.error.empty()) {
        NoticeOk(v.last_restore ? m.restored : m.copied);
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

void App::DrawVault(NVGcontext* vg) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    const More& m = TrM();
    FollowVault();
    const auto v = m_vault ? m_vault->Snapshot() : Vault::View{};
    const bool linked = m_link.CurrentConfig().Linked();

    // the games with a save
    nvgBeginPath(vg);
    nvgRect(vg, kLeftW, 73, 1, 664 - 73);
    nvgFillColor(vg, t.line);
    nvgFill(vg);
    char head[160];
    std::snprintf(head, sizeof(head), m.saves_user_fmt,
                  m_vault_users.empty() ? "?" : m_vault_users[m_vault_user].name.c_str());
    draw::Section(vg, t, 30, 100, head);
    if (m_vault_saves.empty()) {
        draw::Box(vg, 30, 124, kLeftW - 60, 15, t.muted, m.no_saves);
    }
    const int rows = 8;
    m_vault_focus = std::clamp(m_vault_focus, 0, std::max(0, int(m_vault_saves.size()) - 1));
    const int first = std::max(0, m_vault_focus - rows + 1);
    if (!m_vault_saves.empty()) {
        const float fy = m_glide_list.Step(118 + (m_vault_focus - first) * 64.f);
        ui::FillRound(vg, 14, fy, kLeftW - 28, 58, 10, t.panel2);
        if (m_vault_side == 0) ui::Focus(vg, t, 14, fy, kLeftW - 28, 58, 10, m_time);
    }
    ListArea(14, 118, kLeftW - 28, rows * 64.f, 64);
    for (int r = 0; r < rows && first + r < int(m_vault_saves.size()); r++) {
        const int i = first + r;
        const auto& sv = m_vault_saves[i];
        const float y = 118 + r * 64;
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
        for (const auto& c : v.library) copies += c.title_id == tid;
        char count[32];
        if (copies) std::snprintf(count, sizeof(count), m.copies_fmt, copies);
        ui::Text(vg, kLeftW - 34, y + 29, 14, t.muted, copies ? count : "—", NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    }

    // the copies in the library
    const float rx = kLeftW + 30, rw = kWidth - rx - 40;
    draw::Section(vg, t, rx, 100, m.copies_lib);
    const auto copies = VaultCopies();
    if (!linked) {
        draw::Box(vg, rx, 124, rw, 15, t.muted, m.need_link);
    } else if (copies.empty() && !m_vault_saves.empty()) {
        draw::Box(vg, rx, 124, rw, 15, t.muted, m.no_copies);
    }
    m_vault_copy = std::clamp(m_vault_copy, 0, std::max(0, int(copies.size()) - 1));
    for (int i = 0; i < int(copies.size()) && i < 6; i++) {
        const auto& c = copies[i];
        const float y = 118 + i * 66;
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
        nvgSave(vg);
        nvgScissor(vg, rx + 48, y, rw - 260, 58);
        ui::Text(vg, rx + 48, y + 21, 16, c.note.empty() ? t.muted : t.text, c.note.empty() ? m.no_note : c.note.c_str());
        const std::string sub = When(c.when) + (c.console.empty() ? "" : " · " + c.console);
        ui::Text(vg, rx + 48, y + 41, 13, t.muted, sub.c_str());
        nvgRestore(vg);
        ui::Text(vg, rx + rw - 120, y + 29, 14, t.muted, VersionText(c.version).c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        ui::Text(vg, rx + rw - 18, y + 29, 14, t.muted, draw::Size(c.size).c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    }
    if (m_vault_side == 1 && !copies.empty()) ui::Focus(vg, t, rx, m_glide_side.Step(118 + m_vault_copy * 66.f), rw, 58, 10, m_time);
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
    if (linked && !m_vault_saves.empty()) hints.push_back({ui::glyph::X, m.copy_now, HidNpadButton_X});
    if (linked && !copies.empty()) hints.push_back({ui::glyph::A, m.restore, HidNpadButton_A});
    if (m_vault_users.size() > 1) hints.push_back({ui::glyph::ZR, m.user_word, HidNpadButton_ZR});
    hints.push_back({ui::glyph::B, s.back, HidNpadButton_B});
    ui::HintBar(vg, t, nullptr, hints, m_hint_rects);
}

void App::UpdateVault(u64 down, u64 held) {
    const More& m = TrM();
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
        const int n = int(VaultCopies().size());
        if (Repeat(down, held, HidNpadButton_AnyUp) && m_vault_copy > 0) m_vault_copy--;
        if (Repeat(down, held, HidNpadButton_AnyDown) && m_vault_copy < std::min(n, 6) - 1) m_vault_copy++;
    }
    if ((down & HidNpadButton_ZR) && m_vault_users.size() > 1) {
        m_vault_user = (m_vault_user + 1) % int(m_vault_users.size());
        m_vault_saves = m_vault->Saves(m_vault_users[m_vault_user]);
        m_vault_focus = m_vault_copy = 0;
    }
    const Config c = m_link.CurrentConfig();
    if (!c.Linked() || m_vault_saves.empty() || !m_vault) return;
    if (m_vault->Busy() && (down & (HidNpadButton_X | HidNpadButton_A))) {
        Notice(m.st_copying);
        return;
    }
    const auto& save = m_vault_saves[m_vault_focus];
    if (down & HidNpadButton_X) {
        std::string note;
        if (!AskNote(m.note_guide, note)) return;
        unsigned version = 0;
        if (m_queue) {
            const auto installed = m_queue->Installed();
            const auto it = installed.find(save.app_id);
            if (it != installed.end()) version = it->second.patch;
        }
        m_vault->StartBackup(c.url, c.key, m_vault_users[m_vault_user], save.app_id, version, note);
    }
    if ((down & HidNpadButton_A) && m_vault_side == 1) {
        const auto copies = VaultCopies();
        if (m_vault_copy >= int(copies.size())) return;
        // always with its question, Cancel in focus
        m_dialog = Dialog{};
        m_dialog.kind = DialogKind::VaultRestore;
        m_dialog.save = copies[m_vault_copy];
        m_dialog.game = m_vault_focus;
    }
}

} // namespace nxdock
