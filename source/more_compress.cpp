// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// Compress games (24A of the visual test of phase 24), experimental, from Space and dumps. Off
// until the user turns it on, after a window that says what it means (the focus on Cancel). On
// the left, what it does, and whether the console can start a compressed game (firmware 12,
// sigpatches); on the right, the installed games, each with what it would save, from a sample of
// its data. A game is compressed only after another window with everything that changes; one
// compressed by NXDock can be undone (installed again from the library).
#include "app.hpp"

#include "draw.hpp"
#include "history.hpp"
#include "i18n.hpp"
#include "i18n_more.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace nxdock {

void App::OpenCompress() {
    if (!m_ui.compress_on) {
        // the first time: what it is, the focus on Cancel
        m_dialog = Dialog{};
        m_dialog.kind = DialogKind::CompressIntro;
        return;
    }
    if (!m_compress) m_compress = std::make_unique<Compressor>();
    m_sigpatches = FindSigpatches("sdmc:");
    m_compress_opened = m_time;
    m_compress_focus = 0;
    m_space_mode = 3;
    // nothing is read now: a game is analysed when it is chosen (A), the others are not touched
    m_compressed_marks = Compressor::Marks();
}

// The installed games the list shows: those with their game, as the scan found them.
std::vector<int> App::CompressRows(const Space::View& v) const {
    std::vector<int> rows;
    for (int i = 0; i < int(v.apps.size()); i++) {
        const auto& a = v.apps[size_t(i)];
        if (a.archived) continue;
        const bool base = std::any_of(a.contents.begin(), a.contents.end(), [](const InstalledApp::Content& c) {
            return c.type == NcmContentMetaType_Application;
        });
        if (base) rows.push_back(i);
    }
    return rows;
}

void App::DrawCompress(NVGcontext* vg) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    const More& m = TrM();
    const auto v = m_space->Snapshot();
    const auto c = m_compress->Snapshot();
    FollowCompress(c);
    const float lx = 48, lw = 440, rx = 516, rw = kWidth - 48 - rx;
    const float in = ui::EaseOut(ui::Appear(m_compress_opened, 0.35));
    char text[300];

    // ---- the left: what it is, and whether the console can start a compressed game
    nvgSave(vg);
    nvgTranslate(vg, -24 * (1 - in), 0);
    nvgGlobalAlpha(vg, in);
    const float tw = ui::Text(vg, lx, 116, 26, t.text, m.cz_title) - lx;
    {
        // the tag that says it is experimental, in the orange of things to mind
        const float w = ui::TextWidth(vg, 13, m.cz_tag) + 16;
        ui::StrokeRound(vg, lx + tw + 12, 104, w, 24, 6, t.orange, 1.2f);
        ui::Text(vg, lx + tw + 12 + w / 2, 116, 13, t.orange, m.cz_tag, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    }
    float y = 146;
    y += draw::Box(vg, lx, y, lw, 15, t.muted, m.cz_explain) + 18;
    const bool fw = hosversionAtLeast(12, 0, 0);
    const auto line = [&](bool ok, const char* str) {
        nvgBeginPath(vg);
        nvgCircle(vg, lx + 9, y + 10, 9);
        nvgFillColor(vg, ok ? t.ok : t.err);
        nvgFill(vg);
        if (ok) ui::CheckMark(vg, lx + 9, y + 10, 6, t.on_work, 2.f);
        else ui::Text(vg, lx + 9, y + 10, 13, t.on_work, "!", NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        y += draw::Box(vg, lx + 28, y + 1, lw - 28, 15, ok ? t.text : t.err, str) + 12;
    };
    line(fw, fw ? m.cz_fw_ok : m.cz_fw_old);
    if (m_sigpatches.found) {
        std::snprintf(text, sizeof(text), m.cz_sig_ok_fmt, m_sigpatches.how.c_str());
        line(true, text);
    } else {
        line(false, m.cz_sig_missing);
    }
    nvgRestore(vg);

    // ---- the right: the games
    const auto rows = CompressRows(v);
    draw::Panel(vg, t, rx, 92, rw, 460, 16);
    if (rows.empty()) {
        ui::Text(vg, rx + rw / 2, 320, 17, t.muted, m.cz_no_games, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    }
    m_compress_focus = std::clamp(m_compress_focus, 0, std::max(0, int(rows.size()) - 1));
    const float rh = 56;
    const int visible = int((460 - 16) / rh);
    const int first = std::clamp(m_compress_focus - visible / 2, 0, std::max(0, int(rows.size()) - visible));
    nvgSave(vg);
    nvgScissor(vg, rx, 92, rw, 460);
    for (int r = first; r < int(rows.size()) && r < first + visible; r++) {
        const auto& a = v.apps[size_t(rows[size_t(r)])];
        const float ry = 100 + (r - first) * rh;
        const float enter = ui::EaseOut(ui::Appear(m_compress_opened + 0.03 * (r - first), 0.3));
        nvgGlobalAlpha(vg, enter);
        if (r == m_compress_focus) {
            ui::FillRound(vg, rx + 8, ry, rw - 16, rh - 6, 10, t.panel2);
            ui::StrokeRound(vg, rx + 8, ry, rw - 16, rh - 6, 10, t.work, 2.5f);
        }
        AddHit(rx + 8, ry, rw - 16, rh - 6, HitKind::CompressRow, r);
        nvgSave(vg);
        nvgScissor(vg, rx + 24, ry, rw * 0.5f, rh);
        ui::Text(vg, rx + 24, ry + 18, 16, t.text, a.name.c_str());
        ui::Text(vg, rx + 24, ry + 38, 13, t.muted, draw::Size(a.Bytes()).c_str());
        nvgRestore(vg);
        // what it would save, or what it is
        GameEstimate e;
        const auto it = c.games.find(a.app_id);
        bool known = it != c.games.end();
        if (known) e = it->second;
        for (const auto& mark : m_compressed_marks) {
            if (!known && mark.app_id == a.app_id) {
                e.state = GameEstimate::State::Ours;
                e.saved = mark.saved;
                known = true;
            }
        }
        NVGcolor col = t.muted;
        switch (e.state) {
            case GameEstimate::State::Pending: std::snprintf(text, sizeof(text), "%s", known ? m.cz_pending : m.cz_tap); break;
            case GameEstimate::State::Worth:
                std::snprintf(text, sizeof(text), m.cz_worth_fmt, int(e.gain * 100 + 0.5), draw::Size(e.saved).c_str());
                col = t.ok;
                break;
            case GameEstimate::State::Low: std::snprintf(text, sizeof(text), m.cz_low_fmt, int(e.gain * 100 + 0.5)); break;
            case GameEstimate::State::Already: std::snprintf(text, sizeof(text), "%s", m.cz_already); break;
            case GameEstimate::State::Ours:
                std::snprintf(text, sizeof(text), m.cz_ours_fmt, draw::Size(e.saved).c_str());
                col = t.work;
                break;
            default: std::snprintf(text, sizeof(text), "%s", m.cz_unsupported); break;
        }
        const bool spin = known && e.state == GameEstimate::State::Pending;
        if (spin) ui::Spinner(vg, rx + rw - 30, ry + 25, 8, t.muted, m_time);
        ui::Text(vg, rx + rw - (spin ? 48 : 24), ry + 25, 15, col, text, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        // the share it saves, as a small bar under the name
        if (e.state == GameEstimate::State::Worth || e.state == GameEstimate::State::Low) {
            NVGcolor track = t.text;
            track.a = 0.08f;
            const float bw = rw * 0.3f, bx = rx + rw - 24 - bw;
            ui::FillRound(vg, bx, ry + 38, bw, 5, 2.5f, track);
            ui::FillRound(vg, bx, ry + 38, bw * float(std::clamp(e.gain, 0.0, 1.0)), 5, 2.5f, col);
        }
        nvgGlobalAlpha(vg, 1.f);
    }
    nvgRestore(vg);

    // ---- the work going on
    if (c.running) {
        const char* what = c.phase <= 1 ? m.cz_phase1 : c.phase == 2 ? m.cz_phase2 : m.cz_phase3;
        draw::Panel(vg, t, lx, 566, kWidth - 96, 84, 12);
        std::string name;
        for (const auto& a : v.apps) {
            if (a.app_id == c.app) name = a.name;
        }
        ui::Text(vg, lx + 22, 590, 16, t.text, (std::string(what) + " \xC2\xB7 " + name).c_str());
        const float frac = c.total ? float(double(c.done) / double(c.total)) : 0.f;
        ui::Progress(vg, t, lx + 22, 614, kWidth - 140, 12, frac, t.work, m_time);
    }

    std::vector<ui::Hint> hints;
    if (c.running) {
        hints.push_back({ui::glyph::X, s.cancel, HidNpadButton_X});
    } else if (!rows.empty()) {
        hints.push_back({ui::glyph::A, m.open, HidNpadButton_A});
    }
    hints.push_back({ui::glyph::B, s.back, HidNpadButton_B});
    ui::HintBar(vg, t, nullptr, hints, m_hint_rects);
}

void App::FollowCompress(const Compressor::View& c) {
    if (c.finished == m_compress_finished) return;
    m_compress_finished = c.finished;
    const More& m = TrM();
    std::string name;
    if (m_space) {
        for (const auto& a : m_space->Snapshot().apps) {
            if (a.app_id == c.app) name = a.name;
        }
    }
    if (!c.error.empty()) {
        m_dialog = Dialog{};
        m_dialog.kind = DialogKind::VaultError;
        m_dialog.single = true;
        m_dialog.from = c.error;
        m_dialog.text = m.cz_title;
        return;
    }
    char text[160];
    std::snprintf(text, sizeof(text), m.cz_done_fmt, draw::Size(c.saved).c_str());
    Notice(text);
    if (m_ui.sounds) m_sound.Play(synth::Effect::Finish);
    // in the history, as an install of its own
    HistoryEntry entry;
    entry.when = std::time(nullptr);
    entry.game = name;
    entry.label = std::string(m.cz_hist_label) + " \xC2\xB7 -" + draw::Size(c.saved);
    entry.format = "LZ4";
    entry.result = HistoryEntry::Result::Installed;
    History(HistoryPath()).Append(entry);
    m_compressed_marks = Compressor::Marks();
    if (m_space) m_space->StartScan();
}

void App::UpdateCompress(u64 down, u64 held) {
    const auto v = m_space->Snapshot();
    const auto c = m_compress->Snapshot();
    const auto rows = CompressRows(v);
    if (Repeat(down, held, HidNpadButton_AnyUp) && m_compress_focus > 0) m_compress_focus--;
    if (Repeat(down, held, HidNpadButton_AnyDown) && m_compress_focus < int(rows.size()) - 1) m_compress_focus++;
    if (down & HidNpadButton_B) {
        m_space_mode = 0; // a compression that runs goes on
        return;
    }
    if (c.running) {
        if (down & HidNpadButton_X) m_compress->Stop();
        return;
    }
    if (!(down & HidNpadButton_A) || rows.empty()) return;
    const auto& a = v.apps[size_t(rows[size_t(std::clamp(m_compress_focus, 0, int(rows.size()) - 1))])];
    const More& m = TrM();
    GameEstimate e;
    const auto it = c.games.find(a.app_id);
    bool known = it != c.games.end();
    if (known) e = it->second;
    for (const auto& mark : m_compressed_marks) {
        if (!known && mark.app_id == a.app_id) {
            e.state = GameEstimate::State::Ours;
            known = true;
        }
    }
    if (!known) {
        // this game only: a sample of its data, read now
        if (!m_compress->StartAnalysis({a.app_id})) Notice(m.cz_pending);
        return;
    }
    if (e.state == GameEstimate::State::Ours) {
        m_dialog = Dialog{};
        m_dialog.kind = DialogKind::CompressUndo;
        m_dialog.ids = {a.app_id};
        m_dialog.text = a.name;
        return;
    }
    if (e.state != GameEstimate::State::Worth) return;
    if (!hosversionAtLeast(12, 0, 0)) {
        m_dialog = Dialog{};
        m_dialog.kind = DialogKind::CompressInfo;
        m_dialog.single = true;
        m_dialog.text = m.cz_fw_title;
        m_dialog.from = m.cz_fw_body;
        return;
    }
    if (!m_sigpatches.found) {
        // never without them: the game would not start
        m_dialog = Dialog{};
        m_dialog.kind = DialogKind::CompressInfo;
        m_dialog.single = true;
        m_dialog.text = m.cz_nosig_title;
        m_dialog.from = m.cz_nosig_body;
        return;
    }
    if (m_queue && m_queue->Busy()) {
        Notice(m.cz_busy);
        return;
    }
    m_dialog = Dialog{};
    m_dialog.kind = DialogKind::CompressGo;
    m_dialog.ids = {a.app_id};
    m_dialog.text = a.name;
    m_dialog.size = e.saved;
}

// The questions of this screen (drawn by DrawDialog).
void App::CompressDialogText(std::string& title, std::string& body, std::string& note, std::string& primary) {
    const More& m = TrM();
    char text[400];
    switch (m_dialog.kind) {
        case DialogKind::CompressIntro:
            title = m.cz_intro_title;
            body = m.cz_intro_body;
            primary = m.cz_turn_on;
            break;
        case DialogKind::CompressGo:
            std::snprintf(text, sizeof(text), m.cz_go_title_fmt, m_dialog.text.c_str());
            title = text;
            std::snprintf(text, sizeof(text), m.cz_go_body_fmt, draw::Size(m_dialog.size).c_str());
            body = std::string(text) + "\n" + m.cz_risks;
            primary = m.cz_go;
            break;
        case DialogKind::CompressUndo:
            std::snprintf(text, sizeof(text), m.cz_undo_title_fmt, m_dialog.text.c_str());
            title = text;
            body = m.cz_undo_body;
            primary = m.cz_undo_go;
            break;
        case DialogKind::CompressInfo:
            title = m_dialog.text;
            body = m_dialog.from;
            break;
        default: break;
    }
    (void)note;
}

void App::ConfirmCompressDialog() {
    const More& m = TrM();
    switch (m_dialog.kind) {
        case DialogKind::CompressIntro:
            m_ui.compress_on = true;
            m_ui.Save();
            m_dialog.kind = DialogKind::None;
            OpenCompress();
            break;
        case DialogKind::CompressGo:
            if (m_compress && !m_dialog.ids.empty()) m_compress->StartCompress(m_dialog.ids[0]);
            break;
        case DialogKind::CompressUndo: {
            // the original again, from the library: its game file, where the game is
            const u64 app = m_dialog.ids.empty() ? 0 : m_dialog.ids[0];
            for (const auto& g : m_games) {
                if (g.title_id != app) continue;
                for (const auto& f : g.files) {
                    if (f.kind != CatalogFile::Kind::Game) continue;
                    InstallJob job = JobFor(g, f);
                    job.nand = g.installed.base_nand;
                    AddJobs({job});
                    return;
                }
            }
            Notice(m.cz_undo_missing);
            break;
        }
        default: break;
    }
}

} // namespace nxdock
