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
#include "uninstall.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>

namespace nxdock {

namespace {

// The game of a DLC: its ID with the last three digits at 000 and the 1 before them taken away.
unsigned long long DlcGame(unsigned long long id) { return (id & ~0xFFFull) ^ 0x1000; }

bool IsDlc(unsigned long long id) { return (id & 0xFFF) != 0; }

// What the DLC of a game that NXDock compressed saved, from the marks.
unsigned long long DlcSaved(const std::vector<CompressedGame>& marks, unsigned long long app) {
    unsigned long long saved = 0;
    for (const auto& m : marks) {
        if (IsDlc(m.app_id) && DlcGame(m.app_id) == app) saved += m.saved;
    }
    return saved;
}

} // namespace

void App::OpenCompress() {
    if (!m_ui.compress_on) {
        // the first time: what it is, the focus on Cancel
        m_dialog = Dialog{};
        m_dialog.kind = DialogKind::CompressIntro;
        return;
    }
    if (!m_compress) m_compress = std::make_unique<Compressor>();
    m_sigpatches = FindSigpatches("sdmc:");
    m_prodkeys = false;
    for (const char* path : {"sdmc:/switch/prod.keys", "sdmc:/prod.keys"}) {
        if (FILE* f = std::fopen(path, "r")) {
            std::fclose(f);
            m_prodkeys = true;
        }
    }
    m_compress_opened = m_time;
    m_compress_focus = 0;
    m_space_mode = 3;
    // every game is checked from the header of its program (fast), then only those that can be
    // compressed are measured; the list fills as it goes
    m_compressed_marks = Compressor::Marks();
    m_compress_started = false;
}

// The rows of the list: only those that can be compressed (or are being checked), or every game.
std::vector<int> App::CompressVisible(const Space::View& v, const Compressor::View& c) const {
    auto rows = CompressRows(v);
    if (m_compress_all) return rows;
    std::erase_if(rows, [&](int r) {
        const u64 app = v.apps[size_t(r)].app_id;
        for (const auto& mark : m_compressed_marks) {
            if (mark.app_id == app) return false;
        }
        const auto it = c.games.find(app);
        if (it == c.games.end()) return false;
        const auto st = it->second.state;
        // one to undo and merge again stays in sight
        if (it->second.why == GameEstimate::Why::Redo) return false;
        // one that does not start with the update over it stays in sight too
        if (it->second.why == GameEstimate::Why::Stops) return false;
        return st == GameEstimate::State::Already || st == GameEstimate::State::Unsupported;
    });
    return rows;
}

// The installed games the list shows: those with their game, as the scan found them.
std::vector<int> App::CompressRows(const Space::View& v) const {
    std::vector<int> rows;
    for (int i = 0; i < int(v.apps.size()); i++) {
        const auto& a = v.apps[size_t(i)];
        if (a.archived) continue;
        // games only: not the forwarders and homebrew installed as titles (their IDs do not start
        // with 01, as those of games do), nor the small applications, where nothing is gained
        if ((a.app_id >> 56) != 0x01 || a.Bytes() < (100ull << 20)) continue;
        const bool base = std::any_of(a.contents.begin(), a.contents.end(), [](const InstalledApp::Content& c) {
            return c.type == NcmContentMetaType_Application;
        });
        if (base) rows.push_back(i);
    }
    // always by name, as a person reads it (the names arrive after the scan)
    std::stable_sort(rows.begin(), rows.end(), [&v](int a, int b) {
        const auto& x = v.apps[size_t(a)].name;
        const auto& y = v.apps[size_t(b)].name;
        const size_t n = std::min(x.size(), y.size());
        for (size_t i = 0; i < n; i++) {
            const int p = std::tolower((unsigned char)x[i]), q = std::tolower((unsigned char)y[i]);
            if (p != q) return p < q;
        }
        return x.size() < y.size();
    });
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
    // the keys of the tickets come from the user's prod.keys, read only here
    line(m_prodkeys, m_prodkeys ? m.cz_keys_ok : m.cz_keys_missing);
    nvgRestore(vg);

    // ---- the right: the games, checked as soon as the console is read
    if (v.scanned && !m_compress_started && !c.running) {
        std::vector<unsigned long long> apps;
        for (int r : CompressRows(v)) apps.push_back(v.apps[size_t(r)].app_id);
        m_compress_started = m_compress->StartAnalysis(apps);
    }
    const auto rows = CompressVisible(v, c);
    if (!m_compress_told && !c.analyzing && v.scanned && m_compress_started) {
        // a game that does not start with the update over it is told first
        for (const auto& [app, e] : c.games) {
            if (e.why != GameEstimate::Why::Stops) continue;
            for (const auto& a : v.apps) {
                if (a.app_id != app) continue;
                std::snprintf(text, sizeof(text), m.cz_stops_notice_fmt, a.name.c_str());
                Notice(text);
                m_compress_told = true;
            }
            if (m_compress_told) break;
        }
    }
    if (!m_compress_told && !c.analyzing && v.scanned && m_compress_started) {
        for (const auto& [app, e] : c.games) {
            if (!e.merge || !e.merged ||
                (e.state != GameEstimate::State::Worth && e.state != GameEstimate::State::Low && e.why != GameEstimate::Why::Redo)) {
                continue;
            }
            for (const auto& a : v.apps) {
                if (a.app_id != app) continue;
                std::snprintf(text, sizeof(text), m.cz_update_notice_fmt, a.name.c_str());
                Notice(text);
                m_compress_told = true;
            }
            if (m_compress_told) break;
        }
    }
    draw::Panel(vg, t, rx, 92, rw, 460, 16);
    if (v.scanned) {
        // what the check found so far, and how far it got
        int can = 0, already = 0, cannot = 0;
        for (const auto& [app, e] : c.games) {
            if (e.state == GameEstimate::State::Possible || e.state == GameEstimate::State::Worth || e.state == GameEstimate::State::Low ||
                e.state == GameEstimate::State::Ours) {
                can++;
            } else if (e.state == GameEstimate::State::Already) {
                already++;
            } else if (e.state == GameEstimate::State::Unsupported) {
                cannot++;
            }
        }
        if (c.analyzing) {
            const bool measuring = c.check_done >= c.check_total && c.measure_total > 0;
            std::snprintf(text, sizeof(text), measuring ? m.cz_measuring_fmt : m.cz_checking_fmt, measuring ? c.measure_done : c.check_done,
                          measuring ? c.measure_total : c.check_total);
            ui::Spinner(vg, rx + 30, 114, 8, t.work, m_time);
            ui::Text(vg, rx + 46, 114, 14, t.muted, text);
            const float frac = measuring ? float(c.measure_done) / float(std::max(1, c.measure_total))
                                         : float(c.check_done) / float(std::max(1, c.check_total));
            ui::Progress(vg, t, rx + 22, 132, rw - 44, 6, frac, t.work, m_time);
        } else {
            std::snprintf(text, sizeof(text), m.cz_counts_fmt, can, already, cannot);
            ui::Text(vg, rx + 22, 114, 14, t.muted, text);
            ui::Text(vg, rx + rw - 22, 114, 14, t.work, m_compress_all ? m.cz_show_all : m.cz_only_some, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        }
        if (!c.analyzing && rows.empty() && !m_compress_all) {
            draw::Box(vg, rx + 40, 260, rw - 80, 16, t.muted, m.cz_none_possible);
        }
    }
    if (!v.scanned) {
        // the console is still being read: how far, and the list when it ends
        ui::Spinner(vg, rx + rw / 2, 280, 14, t.work, m_time);
        if (v.total) {
            std::snprintf(text, sizeof(text), m.sp_reading_fmt, int(v.done), int(v.total));
            ui::Text(vg, rx + rw / 2, 320, 17, t.muted, text, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            ui::Progress(vg, t, rx + 60, 344, rw - 120, 10, float(double(v.done) / double(v.total)), t.work, m_time);
        } else {
            ui::Text(vg, rx + rw / 2, 320, 17, t.muted, m.scanning, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        }
    } else if (rows.empty()) {
        ui::Text(vg, rx + rw / 2, 320, 17, t.muted, m.cz_no_games, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    }
    for (int r = 0; r < int(rows.size()); r++) {
        if (m_compress_app && v.apps[size_t(rows[size_t(r)])].app_id == m_compress_app) m_compress_focus = r;
    }
    m_compress_focus = std::clamp(m_compress_focus, 0, std::max(0, int(rows.size()) - 1));
    if (!rows.empty()) m_compress_app = v.apps[size_t(rows[size_t(m_compress_focus)])].app_id;
    const float rh = 56;
    const int visible = int((552 - 148 - 8) / rh);
    const int first = std::clamp(m_compress_focus - visible / 2, 0, std::max(0, int(rows.size()) - visible));
    nvgSave(vg);
    nvgScissor(vg, rx, 146, rw, 404);
    for (int r = first; r < int(rows.size()) && r < first + visible; r++) {
        const auto& a = v.apps[size_t(rows[size_t(r)])];
        const float ry = 150 + (r - first) * rh;
        const float enter = ui::EaseOut(ui::Appear(m_compress_opened + 0.03 * (r - first), 0.3));
        nvgGlobalAlpha(vg, enter);
        if (r == m_compress_focus) {
            ui::FillRound(vg, rx + 8, ry, rw - 16, rh - 6, 10, t.panel2);
            ui::StrokeRound(vg, rx + 8, ry, rw - 16, rh - 6, 10, t.work, 2.5f);
        }
        AddHit(rx + 8, ry, rw - 16, rh - 6, HitKind::CompressRow, r);
        // what it would save, or what it is
        GameEstimate e;
        const auto it = c.games.find(a.app_id);
        bool known = it != c.games.end();
        if (known) e = it->second;
        for (const auto& mark : m_compressed_marks) {
            if (!known && mark.app_id == a.app_id) {
                e.state = GameEstimate::State::Ours;
                e.saved = mark.saved + DlcSaved(m_compressed_marks, a.app_id);
                known = true;
            }
        }
        const bool merged_ours = e.state == GameEstimate::State::Ours && IsMergedGame(a.app_id);
        // a newer update over a game NXDock merged: it waits to be merged in the same way
        const bool redo = e.why == GameEstimate::Why::Redo;
        // an official update over it: it does not start
        const bool stops = e.why == GameEstimate::Why::Stops;
        const bool waiting = e.merge && e.merged && (e.state == GameEstimate::State::Worth || e.state == GameEstimate::State::Low || redo);
        nvgSave(vg);
        nvgScissor(vg, rx + 24, ry, rw * 0.56f, rh);
        ui::Text(vg, rx + 24, ry + 18, 16, t.text, a.name.c_str());
        {
            // under the name: its size, then the game with its update (and a tag), then its DLC
            std::string sub = draw::Size(a.Bytes());
            const char* tag = nullptr;
            NVGcolor tag_col = t.violet;
            if (stops) {
                std::snprintf(text, sizeof(text), m.cz_stops_sub_fmt, VersionText(e.patch).c_str());
                sub += std::string(" \xC2\xB7 ") + text;
                tag = m.cz_stops_tag;
                tag_col = t.orange;
            } else if (waiting) {
                std::snprintf(text, sizeof(text), m.cz_update_sub_fmt, VersionText(e.patch).c_str());
                sub += std::string(" \xC2\xB7 ") + text;
                tag = m.cz_update_tag;
                tag_col = t.orange;
            } else if (e.merge) {
                std::snprintf(text, sizeof(text), m.cz_merge_sub_fmt, VersionText(e.patch).c_str());
                sub += std::string(" \xC2\xB7 ") + text;
                tag = m.cz_merge_tag;
            } else if (merged_ours) {
                sub += std::string(" \xC2\xB7 ") + m.cz_merged_sub;
            }
            float x = ui::Text(vg, rx + 24, ry + 38, 13, t.muted, sub.c_str());
            if (tag) {
                const float w = ui::TextWidth(vg, 11, tag) + 12;
                ui::StrokeRound(vg, x + 8, ry + 30, w, 16, 8, tag_col, 1.f);
                ui::Text(vg, x + 8 + w / 2, ry + 38, 11, tag_col, tag, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
                x += 8 + w;
            }
            if (!e.dlcs.empty()) {
                std::snprintf(text, sizeof(text), m.cz_dlc_fmt, int(e.dlcs.size()));
                ui::Text(vg, x + 8, ry + 38, 13, t.muted, text);
            }
        }
        nvgRestore(vg);
        NVGcolor col = t.muted;
        switch (e.state) {
            case GameEstimate::State::Pending: std::snprintf(text, sizeof(text), "%s", m.cz_pending); break;
            case GameEstimate::State::Possible:
                std::snprintf(text, sizeof(text), "%s", m.cz_possible);
                col = t.work;
                break;
            case GameEstimate::State::Worth:
                if (waiting) {
                    std::snprintf(text, sizeof(text), "%s", m.cz_update_wait);
                    col = t.orange;
                } else if (e.merge) {
                    std::snprintf(text, sizeof(text), m.cz_merge_worth_fmt, draw::Size(e.saved).c_str());
                    col = t.ok;
                } else {
                    std::snprintf(text, sizeof(text), m.cz_worth_fmt, int(e.gain * 100 + 0.5), draw::Size(e.saved).c_str());
                    col = t.ok;
                }
                break;
            case GameEstimate::State::Low:
                if (waiting) {
                    // a newer update is merged even when it saves little: it cannot stay over it
                    std::snprintf(text, sizeof(text), "%s", m.cz_update_wait);
                    col = t.orange;
                } else {
                    std::snprintf(text, sizeof(text), m.cz_low_fmt, int(e.gain * 100 + 0.5));
                }
                break;
            case GameEstimate::State::Already: std::snprintf(text, sizeof(text), "%s", m.cz_already); break;
            case GameEstimate::State::Ours:
                std::snprintf(text, sizeof(text), merged_ours ? m.cz_merged_fmt : m.cz_ours_fmt, draw::Size(e.saved).c_str());
                col = t.work;
                break;
            default:
                if (stops) {
                    std::snprintf(text, sizeof(text), "%s", m.cz_why_stops);
                    col = t.orange;
                    break;
                }
                if (redo) {
                    std::snprintf(text, sizeof(text), "%s", m.cz_why_redo);
                    col = t.orange;
                    break;
                }
                std::snprintf(text, sizeof(text), "%s",
                              e.why == GameEstimate::Why::Sparse ? m.cz_why_sparse
                              : e.why == GameEstimate::Why::Key  ? m.cz_why_key
                              : e.why == GameEstimate::Why::NotFound ? m.cz_why_missing
                              : e.why == GameEstimate::Why::Update ? m.cz_why_update
                                                                 : m.cz_unsupported);
                break;
        }
        const bool spin = known && (e.state == GameEstimate::State::Pending || e.state == GameEstimate::State::Possible) && c.analyzing;
        if (spin) ui::Spinner(vg, rx + rw - 30, ry + 25, 8, t.muted, m_time);
        ui::Text(vg, rx + rw - (spin ? 48 : 24), ry + 25, 15, col, text, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        // the share it saves, as a small bar under the name
        if ((e.state == GameEstimate::State::Worth || e.state == GameEstimate::State::Low) && !waiting) {
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

    DrawListLetter(vg, rx + rw / 2, 322);
    std::vector<ui::Hint> hints;
    if (c.running) {
        hints.push_back({ui::glyph::X, s.cancel, HidNpadButton_X});
    } else if (!rows.empty()) {
        hints.push_back({ui::glyph::DPad, m.jump_letters, HidNpadButton_AnyRight});
        hints.push_back({ui::glyph::Y, m_compress_all ? m.cz_only_some : m.cz_show_all, HidNpadButton_Y});
        const auto focused = c.games.find(m_compress_app);
        if (focused != c.games.end() && focused->second.why == GameEstimate::Why::Stops) {
            // the two ways out of a game that does not start with the update over it (2A)
            hints.push_back({ui::glyph::X, m.cz_remove_update, HidNpadButton_X});
            hints.push_back({ui::glyph::A, m.cz_fix_original, HidNpadButton_A});
        } else {
            hints.push_back({ui::glyph::A, m.open, HidNpadButton_A});
        }
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
    NoticeOk(text);
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
    const auto rows = CompressVisible(v, c);
    if (down & HidNpadButton_Y) {
        m_compress_all = !m_compress_all; // every game, or only those that can be compressed
        return;
    }
    if (Repeat(down, held, HidNpadButton_AnyUp) && m_compress_focus > 0) m_compress_focus--;
    if (Repeat(down, held, HidNpadButton_AnyDown) && m_compress_focus < int(rows.size()) - 1) m_compress_focus++;
    // by first letter, for long lists
    const bool left = Repeat(down, held, HidNpadButton_AnyLeft), right = Repeat(down, held, HidNpadButton_AnyRight);
    if ((left || right) && !rows.empty()) {
        std::vector<std::string> names;
        for (int r : rows) names.push_back(v.apps[size_t(r)].name);
        m_compress_focus = ListJump(names, m_compress_focus, right ? 1 : -1);
    }
    if (!rows.empty()) {
        m_compress_focus = std::clamp(m_compress_focus, 0, int(rows.size()) - 1);
        m_compress_app = v.apps[size_t(rows[size_t(m_compress_focus)])].app_id;
    }
    if (down & HidNpadButton_B) {
        m_space_mode = 0; // a compression that runs goes on
        return;
    }
    if (c.running) {
        if (down & HidNpadButton_X) m_compress->Stop();
        return;
    }
    if (!(down & (HidNpadButton_A | HidNpadButton_X)) || rows.empty()) return;
    const auto& a = v.apps[size_t(rows[size_t(std::clamp(m_compress_focus, 0, int(rows.size()) - 1))])];
    const More& m = TrM();
    GameEstimate e;
    const auto it = c.games.find(a.app_id);
    bool known = it != c.games.end();
    if (known) e = it->second;
    if (e.why == GameEstimate::Why::Stops) {
        // X: the update goes; A: the original from the library goes under it
        if (m_queue && m_queue->Busy()) {
            Notice(m.cz_busy);
            return;
        }
        m_dialog = Dialog{};
        m_dialog.kind = (down & HidNpadButton_X) ? DialogKind::CompressStopsRemove : DialogKind::CompressStopsOriginal;
        m_dialog.ids = {a.app_id};
        m_dialog.text = a.name;
        if (m_dialog.kind == DialogKind::CompressStopsOriginal) {
            bool in_library = false;
            for (const auto& g : m_games) in_library |= g.title_id == a.app_id && g.Base();
            // nothing to install: it says so, and the other way out
            m_dialog.single = !in_library;
        }
        return;
    }
    if (!(down & HidNpadButton_A)) return;
    for (const auto& mark : m_compressed_marks) {
        if (!known && mark.app_id == a.app_id) {
            e.state = GameEstimate::State::Ours;
            known = true;
        }
    }
    if (!known || e.state == GameEstimate::State::Pending || e.state == GameEstimate::State::Possible) {
        // still being checked: it says so
        if (!c.analyzing && !m_compress->StartAnalysis({a.app_id})) Notice(m.cz_pending);
        else Notice(m.cz_pending);
        return;
    }
    if (e.state == GameEstimate::State::Ours || e.why == GameEstimate::Why::Redo) {
        m_dialog = Dialog{};
        m_dialog.kind = DialogKind::CompressUndo;
        m_dialog.ids = {a.app_id};
        m_dialog.text = a.name;
        return;
    }
    const bool waiting = e.merge && e.merged && e.state == GameEstimate::State::Low;
    if (e.state != GameEstimate::State::Worth && !waiting) return;
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
        case DialogKind::CompressGo: {
            GameEstimate e;
            if (m_compress && !m_dialog.ids.empty()) {
                const auto c = m_compress->Snapshot();
                const auto it = c.games.find(m_dialog.ids[0]);
                if (it != c.games.end()) e = it->second;
            }
            std::string dlc;
            if (!e.dlcs.empty()) {
                std::snprintf(text, sizeof(text), m.cz_go_dlc_fmt, int(e.dlcs.size()), draw::Size(e.dlc_saved).c_str());
                dlc = std::string(text) + "\n";
            }
            if (e.merge) {
                std::snprintf(text, sizeof(text), m.cz_go_merge_title_fmt, m_dialog.text.c_str());
                title = text;
                std::snprintf(text, sizeof(text), m.cz_go_merge_body_fmt, e.patch, draw::Size(m_dialog.size).c_str());
                body = std::string(text) + "\n" + m.cz_merge_risks + "\n" + dlc + m.cz_risks;
                primary = m.cz_merge_go;
            } else {
                std::snprintf(text, sizeof(text), m.cz_go_title_fmt, m_dialog.text.c_str());
                title = text;
                std::snprintf(text, sizeof(text), m.cz_go_body_fmt, draw::Size(m_dialog.size).c_str());
                body = std::string(text) + "\n" + dlc + m.cz_risks;
                primary = m.cz_go;
            }
            break;
        }
        case DialogKind::CompressUndo:
            std::snprintf(text, sizeof(text), m.cz_undo_title_fmt, m_dialog.text.c_str());
            title = text;
            body = m_dialog.ids.empty() || !IsMergedGame(m_dialog.ids[0]) ? m.cz_undo_body
                   : CanMergeAgain(m_dialog.ids[0])                      ? m.cz_undo_merged_body
                                                                         : m.cz_undo_redo_body;
            primary = m.cz_undo_go;
            break;
        case DialogKind::CompressInfo:
            title = m_dialog.text;
            body = m_dialog.from;
            break;
        case DialogKind::CompressStopsOriginal:
            std::snprintf(text, sizeof(text), m.cz_stops_orig_title_fmt, m_dialog.text.c_str());
            title = text;
            body = m_dialog.single ? m.cz_stops_no_original : m.cz_stops_orig_body;
            primary = m.cz_fix_original;
            break;
        case DialogKind::CompressStopsRemove:
            std::snprintf(text, sizeof(text), m.cz_stops_rm_title_fmt, m_dialog.text.c_str());
            title = text;
            body = m.cz_stops_rm_body;
            primary = m.cz_remove_update;
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
            const bool merged = IsMergedGame(app);
            for (const auto& g : m_games) {
                if (g.title_id != app) continue;
                const CatalogFile* base = g.Base();
                if (!base) break;
                std::vector<InstallJob> jobs;
                InstallJob job = JobFor(g, *base);
                job.nand = g.installed.base_nand;
                // a merged game is an Application of the version of its update: the original
                // takes its place
                if (merged) job.downgrade = true;
                jobs.push_back(job);
                if (merged) {
                    // the update it was merged with (the version it holds), or the newest
                    const CatalogFile* update = nullptr;
                    for (const auto& f : g.files) {
                        if (f.kind == CatalogFile::Kind::Update && f.version == g.installed.patch) update = &f;
                    }
                    if (!update) update = g.NewestUpdate();
                    if (update) {
                        InstallJob uj = JobFor(g, *update);
                        uj.nand = g.installed.base_nand;
                        uj.downgrade = true;
                        jobs.push_back(uj);
                    }
                }
                // its DLC NXDock compressed
                for (const auto& mark : m_compressed_marks) {
                    if (!IsDlc(mark.app_id) || DlcGame(mark.app_id) != app) continue;
                    for (const auto& f : g.files) {
                        if (f.kind == CatalogFile::Kind::Dlc && f.title_id == mark.app_id) jobs.push_back(JobFor(g, f));
                    }
                }
                AddJobs(jobs);
                // the original comes back: it is not merged again by itself, and can be merged anew
                if (merged) ForgetMerged(app);
                return;
            }
            Notice(m.cz_undo_missing);
            break;
        }
        case DialogKind::CompressStopsOriginal: {
            // the original under the update: it takes the place of the one compressed or optimized
            const u64 app = m_dialog.ids.empty() ? 0 : m_dialog.ids[0];
            const BaseGame base = BaseGameOf(app);
            for (const auto& g : m_games) {
                if (g.title_id != app || !g.Base()) continue;
                InstallJob job = JobFor(g, *g.Base());
                job.nand = g.installed.base_nand;
                job.reinstall_ok = true;
                if (base.version > 0) job.downgrade = true;
                AddJobs({job});
                return;
            }
            Notice(m.cz_undo_missing);
            break;
        }
        case DialogKind::CompressStopsRemove: {
            // every update of the game, on both storages: the game starts again at its version
            const u64 app = m_dialog.ids.empty() ? 0 : m_dialog.ids[0];
            bool ok = true, any = false;
            if (m_space) {
                for (const auto& a : m_space->Snapshot().apps) {
                    if (a.app_id != app) continue;
                    for (const auto& content : a.contents) {
                        if (content.type != NcmContentMetaType_Patch) continue;
                        NcmContentMetaKey key{};
                        key.id = content.id;
                        key.version = content.version;
                        key.type = NcmContentMetaType_Patch;
                        key.install_type = NcmContentInstallType_Full;
                        any = true;
                        if (R_FAILED(RemoveInstalled(key, content.nand ? NcmStorageId_BuiltInUser : NcmStorageId_SdCard))) ok = false;
                    }
                }
            }
            if (ok && any) NoticeOk(m.cz_stops_rm_done);
            else Notice(m.cz_stops_rm_failed);
            // read again: the game and the list as they are now
            m_compressed_marks = Compressor::Marks();
            m_compress = std::make_unique<Compressor>();
            m_compress_started = false;
            if (m_space) m_space->StartScan();
            break;
        }
        default: break;
    }
}

} // namespace nxdock
