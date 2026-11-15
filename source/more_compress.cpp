// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// Compress games (24A of the visual test of phase 24), from Space and dumps. On the left, what it
// does, and whether the console can start a compressed game (firmware 12, sigpatches); on the
// right, the installed games, each checked from the header of its program: what it saves is not
// measured before, the user chooses and sees it when it ends (14/11). A game is compressed only
// after a window with everything that changes; one compressed by NXDock can be undone (installed
// again from the library).
#include "app_shared.hpp"

#include "draw.hpp"
#include "history.hpp"
#include "i18n.hpp"
#include "i18n_more.hpp"
#include "i18n_tools.hpp"
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
    // every game is checked from the header of its program (fast); the list fills as it goes
    m_compressed_marks = Compressor::Marks();
    m_compress_started = false;
    m_compress_ask = 0;
    m_compress_return = false;
    m_compress_to_tiles = false;
}

bool App::CanCompress(const InstalledApp& a) const {
    // a game, with its game installed, as the list of Compress games takes them (CompressRows)
    if (a.archived || (a.app_id >> 56) != 0x01 || a.Bytes() < (100ull << 20)) return false;
    return std::any_of(a.contents.begin(), a.contents.end(), [](const InstalledApp::Content& c) { return c.type == NcmContentMetaType_Application; });
}

void App::CompressFromConsole(unsigned long long app) {
    // app 0: the whole list, from the menu of + of In the console
    const MoreScreen back = m_more;
    m_tab = Tab::More;
    if (!m_space) m_space = std::make_unique<Space>();
    m_more = MoreScreen::Space;
    OpenCompress();
    if (app) {
        m_compress_app = app;
        m_compress_all = true; // in sight whatever it is
    }
    m_compress_ask = app;
    m_compress_return = true;
    m_compress_back_more = back;
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

// ---- several at once: the marks and the queue (15/11, 1A-4A)

// What can go into the queue: a game the check found can be compressed, or one merged before whose
// newer update waits to be merged in the same way.
bool App::CompressCanQueue(const GameEstimate& e) {
    if (e.why == GameEstimate::Why::Stops || e.why == GameEstimate::Why::Redo) return false;
    const bool waiting = e.merge && e.merged && e.state == GameEstimate::State::Low;
    return e.state == GameEstimate::State::Possible || e.state == GameEstimate::State::Worth || waiting;
}

// A row can be marked when its game can go into the queue and is not there yet.
bool App::CompressMarkable(const Compressor::View& c, unsigned long long app) const {
    const auto it = c.games.find(app);
    if (it == c.games.end() || !CompressCanQueue(it->second)) return false;
    if (c.running && c.app == app) return false;
    return std::find(m_compress_queue.begin(), m_compress_queue.end(), app) == m_compress_queue.end();
}

// The games of the list that can be marked now, in its order (Mark all, Invert marks).
std::vector<unsigned long long> App::CompressMarkableApps() {
    std::vector<unsigned long long> out;
    if (!m_space || !m_compress) return out;
    const auto v = m_space->Snapshot();
    const auto c = m_compress->Snapshot();
    for (int r : CompressVisible(v, c)) {
        const auto app = v.apps[size_t(r)].app_id;
        if (CompressMarkable(c, app)) out.push_back(app);
    }
    return out;
}

std::string App::CompressName(const Space::View& v, unsigned long long app) const {
    for (const auto& a : v.apps) {
        if (a.app_id == app) return a.name;
    }
    return {};
}

int App::CompressImage(unsigned long long app) {
    // the cover of the library, or the icon the game has on the console, as In the console
    const Game* g = LibraryGame(app);
    if (g && !g->cover.empty()) {
        if (const int image = m_covers.Get(g->cover)) return image;
    }
    char path[24];
    std::snprintf(path, sizeof(path), "ns:%016llX", app);
    return m_covers.Get(path);
}

// The question before compressing: one game, or every one marked.
void App::AskCompress(const std::vector<unsigned long long>& apps) {
    const More& m = TrM();
    if (apps.empty()) return;
    if (!hosversionAtLeast(12, 0, 0)) {
        OpenDialog(DialogKind::CompressInfo);
        m_dialog.single = true;
        m_dialog.text = m.cz_fw_title;
        m_dialog.from = m.cz_fw_body;
        return;
    }
    if (!m_sigpatches.found) {
        // never without them: the game would not start
        OpenDialog(DialogKind::CompressInfo);
        m_dialog.single = true;
        m_dialog.text = m.cz_nosig_title;
        m_dialog.from = m.cz_nosig_body;
        return;
    }
    const auto c = m_compress->Snapshot();
    OpenDialog(DialogKind::CompressGo);
    m_dialog.ids = apps;
    if (apps.size() == 1) {
        const auto it = c.games.find(apps[0]);
        if (it != c.games.end()) m_dialog.size = it->second.saved;
        m_dialog.text = m_space ? CompressName(m_space->Snapshot(), apps[0]) : std::string{};
    }
}

void App::CompressEnqueue(const std::vector<unsigned long long>& apps) {
    if (!m_compress) return;
    const auto p = m_compress->Progress();
    const bool busy = p.running || !m_compress_queue.empty();
    if (!busy) {
        // a new round: what it does is counted from here
        m_compress_round = CompressRound{};
        m_compress_round.started = m_time;
        m_compress_card_at = m_time;
    }
    int added = 0;
    for (const auto app : apps) {
        if (p.running && p.app == app) continue;
        if (std::find(m_compress_queue.begin(), m_compress_queue.end(), app) != m_compress_queue.end()) continue;
        m_compress_queue.push_back(app);
        m_compress_picked.erase(app);
        added++;
    }
    m_compress_round.total += added;
    if (busy && added) {
        char text[120];
        std::snprintf(text, sizeof(text), TrM().cz_added_fmt, added);
        NoticeOk(text);
    }
    CompressStartNext();
}

// The next of the queue, once nothing else is at work (the check of the list, or installs).
void App::CompressStartNext() {
    if (!m_compress || m_compress_queue.empty()) return;
    const auto p = m_compress->Progress();
    if (p.running || p.analyzing || (m_queue && m_queue->Busy())) return;
    const unsigned long long app = m_compress_queue.front();
    if (!m_compress->StartCompress(app)) return;
    m_compress_queue.erase(m_compress_queue.begin());
    m_compress_round.game_at = m_time;
    m_compress_samples.clear();
    m_compress_sample_step = -1;
    m_compress_speed = 0;
    m_compress_frac = 0;
    m_compress_piece = -1;
}

// Every frame, on any screen: what ended, and the next one (the queue goes on when you leave).
void App::FollowCompressQueue() {
    if (!m_compress) return;
    const auto p = m_compress->Progress();
    if (p.finished != m_compress_finished) FollowCompress(p);
    if (p.running) FollowCompressSpeed(p);
    CompressStartNext();
    if (m_compress_summary_pending && m_dialog.kind == DialogKind::None && !m_act) {
        m_compress_summary_pending = false;
        OpenDialog(DialogKind::CompressDone);
        m_compress_summary_at = m_time;
    }
}

// How fast it goes, from what was read over the last four seconds of the same step, and how far
// the piece at work is with all its steps together (measuring, writing, the check).
void App::FollowCompressSpeed(const Compressor::View& c) {
    const int piece = int((c.app & 0xFFFFF) * 16 + unsigned(c.dlc));
    if (piece != m_compress_piece) {
        m_compress_piece = piece;
        m_compress_measuring = c.phase <= 1;
        m_compress_frac = 0;
    }
    const int step = piece * 4 + c.phase;
    if (step != m_compress_sample_step || (!m_compress_samples.empty() && c.done < m_compress_samples.back().second)) {
        m_compress_sample_step = step;
        m_compress_samples.clear();
    }
    if (m_compress_samples.empty() || m_compress_samples.back().second != c.done) m_compress_samples.push_back({m_time, c.done});
    while (m_compress_samples.size() > 2 && m_time - m_compress_samples.front().first > 4) m_compress_samples.erase(m_compress_samples.begin());
    if (m_compress_samples.size() >= 2) {
        const double span = m_compress_samples.back().first - m_compress_samples.front().first;
        if (span >= 1) {
            const double now = double(m_compress_samples.back().second - m_compress_samples.front().second) / span;
            m_compress_speed = m_compress_speed > 0 ? m_compress_speed * 0.7 + now * 0.3 : now;
        }
    }
    const float part = c.total ? std::clamp(float(double(c.done) / double(c.total)), 0.f, 1.f) : 0.f;
    float f;
    if (m_compress_measuring) f = c.phase <= 1 ? 0.45f * part : c.phase == 2 ? 0.45f + 0.45f * part : 0.9f + 0.1f * part;
    else f = c.phase <= 2 ? 0.88f * part : 0.88f + 0.12f * part;
    m_compress_frac = std::max(m_compress_frac, f);
}

void App::DrawCompress(NVGcontext* vg) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    const More& m = TrM();
    const auto v = m_space->Snapshot();
    const auto c = m_compress->Snapshot();
    const bool busy = c.running || !m_compress_queue.empty();
    const float lx = 48, lw = 440, rx = 516, rw = kWidth - 48 - rx;
    char text[300];

    if (busy) {
        // ---- the left: the game at work (2A)
        DrawCompressCard(vg, v, c, lx, lw);
    } else {
        // ---- the left: what it is, and whether the console can start a compressed game
        const float in = ui::EaseOut(ui::Appear(m_compress_opened, 0.35));
        nvgSave(vg);
        nvgTranslate(vg, -24 * (1 - in), 0);
        nvgGlobalAlpha(vg, in);
        ui::Text(vg, lx, 116, 26, t.text, m.cz_title);
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
    }

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
                (e.state != GameEstimate::State::Possible && e.state != GameEstimate::State::Worth && e.state != GameEstimate::State::Low &&
                 e.why != GameEstimate::Why::Redo)) {
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
    // taller while it works: the card on the left takes the place of the bar under it
    const float top = 92, ph = busy ? 558 : 460, bottom = top + ph;
    draw::Panel(vg, t, rx, top, rw, ph, 16);
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
            std::snprintf(text, sizeof(text), m.cz_checking_fmt, c.check_done, c.check_total);
            ui::Spinner(vg, rx + 30, 114, 8, t.work, m_time);
            ui::Text(vg, rx + 46, 114, 14, t.muted, text);
            const float frac = float(c.check_done) / float(std::max(1, c.check_total));
            ui::Progress(vg, t, rx + 22, 132, rw - 44, 6, frac, t.work, m_time);
        } else {
            if (busy) {
                const int at = int(m_compress_round.done.size() + m_compress_round.failed.size()) + (c.running ? 1 : 0);
                std::snprintf(text, sizeof(text), m.cz_working_fmt, std::max(1, at), std::max(at, m_compress_round.total));
                ui::Title(vg, rx + 22, 114, 15, t.text, text);
            } else if (!m_compress_picked.empty()) {
                unsigned long long bytes = 0;
                for (const auto& a : v.apps) {
                    if (m_compress_picked.count(a.app_id)) bytes += a.Bytes();
                }
                std::snprintf(text, sizeof(text), m.cz_marked_fmt, int(m_compress_picked.size()), draw::Size(bytes).c_str());
                ui::Title(vg, rx + 22, 114, 15, t.text, text);
            } else {
                std::snprintf(text, sizeof(text), m.cz_counts_fmt, can, already, cannot);
                ui::Text(vg, rx + 22, 114, 14, t.muted, text);
            }
            // the filter, which a finger can tap too (4A)
            const char* filter = m_compress_all ? m.cz_show_all : m.cz_only_some;
            const float fw = ui::TextWidth(vg, 14, filter);
            ui::Text(vg, rx + rw - 22, 114, 14, t.work, filter, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            AddHit(rx + rw - 34 - fw, 92, fw + 24, 46, HitKind::CompressFilter);
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
    // rows of 64: a cover, and the marks at the right once one is marked (1A)
    const float rh = 64, list_top = 148;
    const int visible = int((bottom - list_top - 4) / rh);
    const int first = std::clamp(m_compress_focus - visible / 2, 0, std::max(0, int(rows.size()) - visible));
    const bool marking = !m_compress_picked.empty();
    ListArea(rx, list_top, rw, visible * rh, rh);
    nvgSave(vg);
    nvgScissor(vg, rx, list_top - 2, rw, bottom - list_top);
    for (int r = first; r < int(rows.size()) && r < first + visible; r++) {
        const auto& a = v.apps[size_t(rows[size_t(r)])];
        const float ry = list_top + 2 + (r - first) * rh, bh = rh - 6;
        const float enter = ui::EaseOut(ui::Appear(m_compress_opened + 0.03 * (r - first), 0.3));
        nvgGlobalAlpha(vg, enter);
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
        // in this round: at work, waiting, done or failed
        const bool working = c.running && c.app == a.app_id;
        const auto queued = std::find(m_compress_queue.begin(), m_compress_queue.end(), a.app_id);
        const int queue_pos = queued == m_compress_queue.end() ? 0 : int(queued - m_compress_queue.begin()) + 1;
        unsigned long long done_saved = 0;
        bool done = false, failed = false;
        for (const auto& [app, saved] : m_compress_round.done) {
            if (app == a.app_id) {
                done = true;
                done_saved = saved;
            }
        }
        for (const auto& f : m_compress_round.failed) failed |= f.first == a.app_id;
        if (working) {
            // the row fills with blue as it goes, with a glowing line under it
            NVGcolor wash = t.work;
            wash.a = 0.08f;
            ui::FillRound(vg, rx + 8, ry, rw - 16, bh, 10, wash);
        }
        if (r == m_compress_focus) {
            ui::FillRound(vg, rx + 8, ry, rw - 16, bh, 10, t.panel2);
            ui::Focus(vg, t, rx + 8, ry, rw - 16, bh, 10, m_time);
        }
        if (working) {
            nvgSave(vg);
            nvgIntersectScissor(vg, rx + 8, ry, (rw - 16) * m_compress_frac, bh);
            NVGcolor wash = t.work;
            wash.a = 0.16f;
            ui::FillRound(vg, rx + 8, ry, rw - 16, bh, 10, wash);
            ui::FillRound(vg, rx + 8, ry + bh - 4, rw - 16, 4, 2, t.work);
            nvgRestore(vg);
        }
        AddHit(rx + 8, ry, rw - 16, bh, HitKind::CompressRow, r);
        // the cover, filling with blue while it is compressed (as in NXPress)
        const float cs = 44, cx = rx + 22, cy = ry + (bh - cs) / 2;
        ui::Cover(vg, t, cx, cy, cs, PlaceholderColor(a.name), Initial(a.name).c_str(), nullptr, false, false, 8, CompressImage(a.app_id));
        if (working) {
            nvgSave(vg);
            nvgIntersectScissor(vg, cx, cy + cs * (1 - m_compress_frac), cs, cs * m_compress_frac);
            NVGcolor fill = t.work;
            fill.a = 0.45f;
            ui::FillRound(vg, cx, cy, cs, cs, 8, fill);
            nvgRestore(vg);
        }
        const bool merged_ours = e.state == GameEstimate::State::Ours && IsMergedGame(a.app_id);
        // a newer update over a game NXDock merged: it waits to be merged in the same way
        const bool redo = e.why == GameEstimate::Why::Redo;
        // an official update over it: it does not start
        const bool stops = e.why == GameEstimate::Why::Stops;
        const bool waiting = e.merge && e.merged &&
                             (e.state == GameEstimate::State::Possible || e.state == GameEstimate::State::Worth || e.state == GameEstimate::State::Low || redo);
        const float nx = cx + cs + 14;
        nvgSave(vg);
        nvgIntersectScissor(vg, nx, ry, rw * 0.56f - (nx - rx) + 24, bh);
        ui::Text(vg, nx, ry + 18, 16, t.text, a.name.c_str());
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
            float x = ui::Text(vg, nx, ry + 38, 13, t.muted, sub.c_str());
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
        // the mark, at the right, while something is marked (1A); a finger taps it there
        float right = rx + rw - 24;
        if (marking) {
            if (CompressMarkable(c, a.app_id) || m_compress_picked.count(a.app_id)) {
                draw::TickBox(vg, t, rx + rw - 48, ry + bh / 2, m_compress_picked.count(a.app_id) > 0, "czpick:" + std::to_string(a.app_id));
            }
            right -= 40;
        }
        if (CompressMarkable(c, a.app_id)) AddHit(rx + rw - 84, ry, 76, bh, HitKind::CompressMark, r);
        const float mid = ry + bh / 2;
        if (working) {
            std::snprintf(text, sizeof(text), "%d %%", int(m_compress_frac * 100));
            ui::Title(vg, right, ry + 18, 17, t.work, text, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            ui::Text(vg, right, ry + 39, 13, t.muted, appui::SpeedText(m_compress_speed / double(1 << 20)).c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            continue;
        }
        if (queue_pos) {
            // waiting its turn, with its place
            std::snprintf(text, sizeof(text), m.cz_queued_fmt, queue_pos);
            const float w = ui::TextWidth(vg, 13, text) + 22;
            ui::StrokeRound(vg, right - w, mid - 12, w, 24, 12, t.work, 1.4f);
            ui::Text(vg, right - w / 2, mid, 13, t.work, text, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            continue;
        }
        if (done) {
            // done in this round: what it saved, in green, with a tick
            // the list is read again when the round ends: until then its size is the one before
            const unsigned long long was = a.Bytes();
            std::snprintf(text, sizeof(text), "\xE2\x88\x92%s \xC2\xB7 %d %%", draw::Size(done_saved).c_str(),
                          was ? int(double(done_saved) * 100 / double(was) + 0.5) : 0);
            ui::Title(vg, right, mid, 15, t.ok, text, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            const float tx = right - ui::TextWidth(vg, 15, text) - 16;
            nvgBeginPath(vg);
            nvgCircle(vg, tx, mid, 9);
            nvgFillColor(vg, t.ok);
            nvgFill(vg);
            ui::CheckMark(vg, tx, mid, 6, t.on_work, 2.f);
            continue;
        }
        if (failed) {
            // red only for this: it could not be done
            ui::Text(vg, right, mid, 15, t.err, m.cz_failed_row, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
            continue;
        }
        NVGcolor col = t.muted;
        switch (e.state) {
            case GameEstimate::State::Pending: std::snprintf(text, sizeof(text), "%s", m.cz_pending); break;
            case GameEstimate::State::Possible:
                if (waiting) {
                    std::snprintf(text, sizeof(text), "%s", m.cz_update_wait);
                    col = t.orange;
                } else {
                    std::snprintf(text, sizeof(text), "%s", m.cz_possible);
                    col = t.work;
                }
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
        const bool spin = known && e.state == GameEstimate::State::Pending && c.analyzing;
        if (spin) ui::Spinner(vg, right - 6, mid, 8, t.muted, m_time);
        ui::Text(vg, right - (spin ? 24 : 0), mid, 15, col, text, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        // the share it saves, as a small bar under the text
        if ((e.state == GameEstimate::State::Worth || e.state == GameEstimate::State::Low) && !waiting) {
            NVGcolor track = t.text;
            track.a = 0.08f;
            const float bw = rw * 0.26f, bx = right - bw;
            ui::FillRound(vg, bx, mid + 14, bw, 5, 2.5f, track);
            ui::FillRound(vg, bx, mid + 14, bw * float(std::clamp(e.gain, 0.0, 1.0)), 5, 2.5f, col);
        }
    }
    nvgGlobalAlpha(vg, 1.f);
    nvgRestore(vg);

    DrawListLetter(vg, rx + rw / 2, 322);
    std::vector<ui::Hint> hints;
    if (busy) {
        hints.push_back({ui::glyph::Y, m.mark, HidNpadButton_Y});
        hints.push_back({ui::glyph::Plus, TrT().fm_actions, HidNpadButton_Plus});
        hints.push_back({ui::glyph::X, s.cancel, HidNpadButton_X});
    } else if (!rows.empty()) {
        hints.push_back({ui::glyph::DPad, m.jump_letters, HidNpadButton_AnyRight});
        hints.push_back({ui::glyph::Y, m.mark, HidNpadButton_Y}); // as in the other lists (4A)
        const auto focused = c.games.find(m_compress_app);
        if (focused != c.games.end() && focused->second.why == GameEstimate::Why::Stops) {
            // the two ways out of a game that does not start with the update over it (2A)
            hints.push_back({ui::glyph::X, m.cz_remove_update, HidNpadButton_X});
            hints.push_back({ui::glyph::A, m.cz_fix_original, HidNpadButton_A});
        } else {
            hints.push_back({ui::glyph::A, m.open, HidNpadButton_A});
        }
        hints.push_back({ui::glyph::Plus, TrT().fm_actions, HidNpadButton_Plus}); // the same, as everywhere (1A)
    }
    hints.push_back({ui::glyph::B, s.back, HidNpadButton_B});
    ui::HintBar(vg, t, nullptr, hints, m_hint_rects);
}

// The card of the game at work, in the place of the left column (2A): its cover filling with
// blue, the share in big, the speed and the time left, the steps, the sizes and what comes next.
void App::DrawCompressCard(NVGcontext* vg, const Space::View& v, const Compressor::View& c, float lx, float lw) {
    const Theme& t = m_theme;
    const More& m = TrM();
    char text[200];
    const float in = ui::EaseOut(ui::Appear(m_compress_card_at, 0.4f));
    nvgSave(vg);
    nvgTranslate(vg, -24 * (1 - in), 0);
    nvgGlobalAlpha(vg, in);
    draw::Panel(vg, t, lx, 92, lw, 558, 16);
    const float pad = 24, x = lx + pad, w = lw - 2 * pad;
    if (!c.running) {
        // between two games, or waiting for the installs to end
        ui::Spinner(vg, lx + lw / 2, 300, 14, t.work, m_time);
        ui::Text(vg, lx + lw / 2, 340, 16, t.muted, (m_queue && m_queue->Busy()) ? m.cz_busy : m.cz_pending, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgRestore(vg);
        return;
    }
    const std::string name = CompressName(v, c.app);
    // the cover, with a soft blue light behind, filling from below
    const float cs = 120, cy = 116;
    NVGcolor glow = t.work;
    glow.a = 0.35f;
    NVGcolor clear = glow;
    clear.a = 0;
    nvgBeginPath(vg);
    nvgRect(vg, x - 30, cy - 30, cs + 60, cs + 60);
    nvgFillPaint(vg, nvgBoxGradient(vg, x, cy, cs, cs, 14, 30, glow, clear));
    nvgFill(vg);
    ui::Cover(vg, t, x, cy, cs, PlaceholderColor(name), Initial(name).c_str(), nullptr, false, false, 14, CompressImage(c.app));
    {
        nvgSave(vg);
        nvgIntersectScissor(vg, x, cy + cs * (1 - m_compress_frac), cs, cs * m_compress_frac);
        NVGcolor fill = t.work;
        fill.a = 0.45f;
        ui::FillRound(vg, x, cy, cs, cs, 14, fill);
        nvgRestore(vg);
    }
    // which game, its name and what goes with it
    const float tx = x + cs + 18, tw = x + w - tx;
    const int at = int(m_compress_round.done.size() + m_compress_round.failed.size()) + 1;
    std::snprintf(text, sizeof(text), m.cz_game_n_fmt, at, std::max(at, m_compress_round.total));
    ui::Text(vg, tx, cy + 12, 14, t.muted, text);
    nvgSave(vg);
    nvgIntersectScissor(vg, tx, cy + 26, tw, 64);
    const float nh = draw::Box(vg, tx, cy + 28, tw, 20, t.text, name.c_str());
    nvgRestore(vg);
    const float sy = cy + 28 + std::min(nh, 56.f) + 12;
    if (c.dlc > 0) std::snprintf(text, sizeof(text), m.cz_dlc_n_fmt, c.dlc, c.dlc_count);
    else if (c.dlc_count > 0) std::snprintf(text, sizeof(text), m.cz_with_dlc_fmt, c.dlc_count);
    else text[0] = 0;
    if (text[0]) ui::Text(vg, tx, sy, 14, c.dlc > 0 ? t.work : t.muted, text);

    // the share in big, the speed and the time left
    std::snprintf(text, sizeof(text), "%d %%", int(m_compress_frac * 100));
    ui::Title(vg, x, 290, 54, t.text, text);
    const double mbs = m_compress_speed / double(1 << 20);
    ui::Title(vg, x + w, 272, 22, t.work, appui::SpeedText(mbs).c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    const bool timed = m_compress_speed > 0 && m_time - m_compress_round.game_at > 3;
    if (timed) {
        // what is left of this step, and the steps after it read about as much again
        const double rest = double(c.total > c.done ? c.total - c.done : 0);
        const double after = double(c.after ? c.after : c.before * 7 / 10);
        double left = rest;
        if (m_compress_measuring && c.phase <= 1) left += double(c.total) + after;
        else if (c.phase <= 2) left += after;
        std::snprintf(text, sizeof(text), m.cz_left_fmt, appui::Duration(left / m_compress_speed).c_str());
    } else {
        std::snprintf(text, sizeof(text), "%s", m.cz_timing);
    }
    ui::Text(vg, x + w, 300, 15, t.muted, text, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    ui::Progress(vg, t, x, 324, w, 12, m_compress_frac, t.work, m_time);

    // the steps: the one at work in blue and breathing, those done with a tick
    {
        std::vector<const char*> steps;
        if (m_compress_measuring) steps.push_back(m.cz_step_measure);
        steps.push_back(m.cz_step_write);
        steps.push_back(m.cz_step_check);
        const int now = m_compress_measuring ? std::clamp(c.phase, 1, 3) - 1 : std::clamp(c.phase, 2, 3) - 2;
        float sx = x;
        for (int i = 0; i < int(steps.size()); i++) {
            const float pw = ui::TextWidth(vg, 13, steps[size_t(i)]) + 40;
            if (sx + pw > x + w + 1) break;
            NVGcolor bg = t.work;
            bg.a = 0.2f;
            ui::FillRound(vg, sx, 352, pw, 28, 14, i == now ? bg : t.panel2);
            if (i < now) {
                nvgBeginPath(vg);
                nvgCircle(vg, sx + 14, 366, 7);
                nvgFillColor(vg, t.ok);
                nvgFill(vg);
                ui::CheckMark(vg, sx + 14, 366, 5, t.on_work, 1.6f);
            } else {
                const float r = i == now && ui::Lively() ? 4 + float(std::sin(m_time * 6)) : 4;
                nvgBeginPath(vg);
                nvgCircle(vg, sx + 14, 366, r);
                nvgFillColor(vg, i == now ? t.work : t.muted);
                nvgFill(vg);
            }
            ui::Text(vg, sx + 26, 366, 13, i == now ? t.text : t.muted, steps[size_t(i)]);
            sx += pw + 6;
        }
    }

    // the figures: before, after, saved so far, time so far
    const auto figure = [&](float fx, float fy, const char* label, const std::string& value, NVGcolor col) {
        ui::Text(vg, fx, fy, 13, t.muted, label);
        ui::Title(vg, fx, fy + 24, 20, col, value.c_str());
    };
    const float c2 = x + w / 2 + 8;
    unsigned long long round_saved = 0;
    for (const auto& d : m_compress_round.done) round_saved += d.second;
    const bool after_known = c.after && c.before;
    figure(x, 408, m.cz_before, c.before ? draw::Size(c.before) : std::string("\xE2\x80\x94"), t.text);
    figure(c2, 408, c.after_exact ? m.cz_after : m.cz_after_estimate,
           after_known ? std::string(c.after_exact ? "" : "\xE2\x89\x88 ") + draw::Size(c.after) : std::string("\xE2\x80\x94"), t.text);
    // what this piece saves, as far as it was written, and the games done before it
    unsigned long long now_saved = 0;
    if (after_known && c.after < c.before) {
        const float written = m_compress_measuring ? std::clamp((m_compress_frac - 0.45f) / 0.45f, 0.f, 1.f) : std::clamp(m_compress_frac / 0.88f, 0.f, 1.f);
        now_saved = (unsigned long long)(double(c.before - c.after) * written);
    }
    figure(x, 466, m.cz_saved_so_far, "\xE2\x88\x92" + draw::Size(round_saved + now_saved), t.ok);
    figure(c2, 466, m.cz_elapsed, appui::Duration(m_time - m_compress_round.game_at), t.text);

    // what comes next in the queue, and the total of the round
    nvgBeginPath(vg);
    nvgRect(vg, x, 524, w, 1);
    nvgFillColor(vg, t.line);
    nvgFill(vg);
    if (!m_compress_queue.empty()) {
        const unsigned long long next = m_compress_queue.front();
        const std::string next_name = CompressName(v, next);
        ui::Text(vg, x, 546, 13, t.muted, m.cz_next);
        ui::Cover(vg, t, x, 562, 44, PlaceholderColor(next_name), Initial(next_name).c_str(), nullptr, false, false, 8, CompressImage(next));
        nvgSave(vg);
        nvgIntersectScissor(vg, x + 56, 560, w * 0.55f, 48);
        ui::Text(vg, x + 56, 576, 15, t.text, next_name.c_str());
        if (m_compress_queue.size() > 1) {
            std::snprintf(text, sizeof(text), m.cz_and_more_fmt, int(m_compress_queue.size()) - 1);
            ui::Text(vg, x + 56, 596, 13, t.muted, text);
        }
        nvgRestore(vg);
    }
    if (round_saved) {
        std::snprintf(text, sizeof(text), m.cz_total_fmt, draw::Size(round_saved).c_str());
        ui::Title(vg, x + w, 586, 14, t.ok, text, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    }
    nvgRestore(vg);
}

void App::FollowCompress(const Compressor::View& c) {
    if (c.finished == m_compress_finished) return;
    m_compress_finished = c.finished;
    const More& m = TrM();
    std::string name;
    if (m_space) name = CompressName(m_space->Snapshot(), c.app);
    const bool alone = m_compress_round.total <= 1 && m_compress_queue.empty();
    if (!c.error.empty()) {
        if (!m_compress_round.stopped) m_compress_round.failed.push_back({c.app, c.error});
        if (alone && !m_compress_round.stopped) {
            // one game alone: its error, as before
            OpenDialog(DialogKind::VaultError);
            m_dialog.single = true;
            m_dialog.from = c.error;
            m_dialog.text = m.cz_title;
            m_compress_round.failed.clear();
        }
    } else {
        m_compress_round.done.push_back({c.app, c.saved});
        // in the history, as an install of its own
        HistoryEntry entry;
        entry.when = std::time(nullptr);
        entry.game = name;
        entry.label = std::string(m.cz_hist_label) + " \xC2\xB7 -" + draw::Size(c.saved);
        entry.format = "LZ4";
        entry.result = HistoryEntry::Result::Installed;
        History(HistoryPath()).Append(entry);
    }
    if (!m_compress_queue.empty() && !m_compress_round.stopped) return; // the next one starts
    // the round ended: the summary (3A), the list read again
    m_compress_queue.clear();
    if (!m_compress_round.done.empty() || !m_compress_round.failed.empty()) {
        m_compress_summary = m_compress_round;
        m_compress_summary.started = m_time - m_compress_round.started; // how long it took
        m_compress_summary_pending = true;
        if (!m_compress_round.done.empty() && m_ui.sounds) m_sound.Play(synth::Effect::Finish);
    } else if (m_compress_round.stopped) {
        // one game alone stopped: said as before; a queue: the rest was not compressed either
        Notice(alone && !c.error.empty() ? UnpackError(c.error).title : std::string(m.cz_done_stopped));
    }
    m_compress_round = CompressRound{};
    m_compress_opened = m_time; // the left column comes back in
    m_compressed_marks = Compressor::Marks();
    if (m_space) m_space->StartScan();
}

// The summary of the round (3A): a green ring that draws itself, the total counting up, the covers
// coming in one after another with what each one saved, how long it took, and what failed.
void App::DrawCompressDone(NVGcontext* vg) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    const More& m = TrM();
    const CompressRound& r = m_compress_summary;
    const double since = m_time - m_compress_summary_at;
    const bool lively = ui::Lively();
    char text[300];
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, kWidth, kHeight);
    nvgFillColor(vg, nvgRGBAf(0, 0, 0, 0.55f * ui::EaseOut(ui::Appear(m_compress_summary_at, 0.2f))));
    nvgFill(vg);
    AddHit(0, 0, kWidth, kHeight, HitKind::DialogBack);
    const int lines = int(r.failed.size()) + (r.stopped ? 1 : 0);
    const float w = 720, h = 420 + std::min(lines, 3) * 26.f - (r.done.empty() ? 110.f : 0.f);
    const float x = (kWidth - w) / 2, y = std::max(80.f, (kHeight - 56 - h) / 2);
    const float pop = ui::EaseOutBack(ui::Appear(m_compress_summary_at, 0.4f));
    nvgSave(vg);
    nvgTranslate(vg, x + w / 2, y + h / 2);
    nvgScale(vg, 0.86f + 0.14f * pop, 0.86f + 0.14f * pop);
    nvgTranslate(vg, -(x + w / 2), -(y + h / 2));
    nvgGlobalAlpha(vg, std::min(1.f, pop * 1.4f));
    draw::Popup(vg, t, x, y, w, h, 20);
    AddHit(x, y, w, h, HitKind::Button);
    const float cx = x + w / 2;
    unsigned long long total = 0;
    for (const auto& d : r.done) total += d.second;
    const NVGcolor tone = r.done.empty() ? t.err : t.ok;
    // the ring, drawn round, then its tick
    {
        const float ry = y + 66, k = lively ? ui::EaseOut(float(std::clamp((since - 0.3) / 0.7, 0.0, 1.0))) : 1.f;
        NVGcolor halo = tone;
        halo.a = 0.35f * k;
        NVGcolor clear = tone;
        clear.a = 0;
        nvgBeginPath(vg);
        nvgCircle(vg, cx, ry, 70);
        nvgFillPaint(vg, nvgRadialGradient(vg, cx, ry, 20, 70, halo, clear));
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgArc(vg, cx, ry, 32, -NVG_PI / 2, -NVG_PI / 2 + 2 * NVG_PI * std::max(0.001f, k), NVG_CW);
        nvgStrokeColor(vg, tone);
        nvgStrokeWidth(vg, 6);
        nvgLineCap(vg, NVG_ROUND);
        nvgStroke(vg);
        if (k >= 1) {
            if (r.done.empty()) ui::Title(vg, cx, ry, 30, tone, "!", NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            else ui::CheckMark(vg, cx, ry, 22 * (lively ? ui::EaseOutBack(ui::Appear(m_compress_summary_at + 1.0, 0.3f)) : 1.f), tone, 4.f);
        }
        // a few sparks out of the ring
        if (lively && !r.done.empty() && since > 1.0 && since < 1.9) {
            const float u = float((since - 1.0) / 0.9);
            for (int i = 0; i < 12; i++) {
                const float a = float(i) / 12 * 2 * NVG_PI;
                const float d = 40 + ui::EaseOut(u) * 70;
                nvgBeginPath(vg);
                nvgCircle(vg, cx + std::cos(a) * d, ry + std::sin(a) * d, 3.5f * (1 - 0.6f * u));
                NVGcolor spark = i % 2 ? t.work : t.ok;
                spark.a = 1 - u;
                nvgFillColor(vg, spark);
                nvgFill(vg);
            }
        }
    }
    float cy = y + 134;
    if (r.done.empty()) std::snprintf(text, sizeof(text), "%s", m.cz_done_none);
    else if (r.done.size() == 1) std::snprintf(text, sizeof(text), "%s", m.cz_done_one);
    else std::snprintf(text, sizeof(text), m.cz_done_many_fmt, int(r.done.size()));
    ui::Title(vg, cx, cy, 22, t.text, text, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    cy += 26;
    if (!r.done.empty()) {
        // the total, counting up
        const float k = lively ? ui::EaseOut(float(std::clamp((since - 0.9) / 1.3, 0.0, 1.0))) : 1.f;
        const std::string sum = "\xE2\x88\x92" + draw::Size((unsigned long long)(double(total) * k));
        ui::Title(vg, cx, cy + 26, 44, t.ok, sum.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        cy += 66;
        // the covers, one after another, with what each saved (up to five)
        const int n = std::min<int>(5, int(r.done.size()));
        const float cs = 72, gap = 30, row = n * cs + (n - 1) * gap;
        const auto v = m_space ? m_space->Snapshot() : Space::View{};
        for (int i = 0; i < n; i++) {
            const float k2 = lively ? ui::EaseOutBack(ui::Appear(m_compress_summary_at + 1.4 + 0.18 * i, 0.4f)) : 1.f;
            const float gx = cx - row / 2 + i * (cs + gap);
            const std::string name = CompressName(v, r.done[size_t(i)].first);
            nvgSave(vg);
            nvgGlobalAlpha(vg, std::min(1.f, pop * 1.4f) * std::clamp(k2 * 2, 0.f, 1.f));
            nvgTranslate(vg, 0, 20 * (1 - k2));
            ui::Cover(vg, t, gx, cy, cs, PlaceholderColor(name), Initial(name).c_str(), nullptr, false, false, 10, CompressImage(r.done[size_t(i)].first));
            const std::string saved = "\xE2\x88\x92" + draw::Size(r.done[size_t(i)].second);
            ui::Text(vg, gx + cs / 2, cy + cs + 16, 14, t.ok, saved.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgRestore(vg);
        }
        if (int(r.done.size()) > n) {
            std::snprintf(text, sizeof(text), "+%d", int(r.done.size()) - n);
            ui::Text(vg, cx + row / 2 + 14, cy + cs / 2, 17, t.muted, text);
        }
        cy += cs + 44;
        std::snprintf(text, sizeof(text), m.cz_done_time_fmt, appui::Duration(r.started).c_str());
        ui::Text(vg, cx, cy, 15, t.muted, text, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        cy += 28;
    } else {
        cy += 10;
    }
    // what could not be done, in red; a stop, as it is
    const auto v = m_space ? m_space->Snapshot() : Space::View{};
    for (int i = 0; i < int(r.failed.size()) && i < 3; i++) {
        const ErrorText e = UnpackError(r.failed[size_t(i)].second);
        std::snprintf(text, sizeof(text), m.cz_done_failed_fmt, CompressName(v, r.failed[size_t(i)].first).c_str(),
                      (e.title.empty() ? e.what : e.title).c_str());
        nvgSave(vg);
        nvgIntersectScissor(vg, x + 24, cy - 13, w - 48, 26);
        ui::Text(vg, cx, cy, 15, t.err, text, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgRestore(vg);
        cy += 26;
    }
    if (r.stopped) {
        ui::Text(vg, cx, cy, 15, t.orange, m.cz_done_stopped, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    }
    // the button, breathing
    const float bw = std::max(160.f, ui::TextWidth(vg, 17, s.close) + 56), bh = 48;
    const float breath = lively ? 1 + 0.025f * float(std::sin(m_time * 4)) : 1.f;
    const float bx = cx - bw / 2, by = y + h - bh - 22;
    nvgSave(vg);
    nvgTranslate(vg, cx, by + bh / 2);
    nvgScale(vg, breath, breath);
    nvgTranslate(vg, -cx, -(by + bh / 2));
    ui::FillRound(vg, bx, by, bw, bh, bh / 2, t.work);
    ui::Title(vg, cx, by + bh / 2, 17, t.on_work, s.close, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgRestore(vg);
    AddHit(bx, by, bw, bh, HitKind::DialogButton, 1);
    nvgRestore(vg);
    ui::HintBar(vg, t, nullptr, {{ui::glyph::A, s.close, HidNpadButton_A}}, m_hint_rects);
}

void App::UpdateCompressDone(u64 down) {
    if (down & (HidNpadButton_A | HidNpadButton_B)) m_dialog.kind = DialogKind::None;
}

void App::UpdateCompress(u64 down, u64 held) {
    const auto v = m_space->Snapshot();
    const auto c = m_compress->Snapshot();
    const auto rows = CompressVisible(v, c);
    const More& m = TrM();
    const bool busy = c.running || !m_compress_queue.empty();
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
        m_space_mode = 0; // a compression that runs goes on, and its queue
        m_compress_ask = 0;
        if (m_compress_to_tiles) {
            m_compress_to_tiles = false;
            m_more = MoreScreen::Tiles;
        }
        if (m_compress_return) {
            // opened from In the console: back there, as it was
            m_compress_return = false;
            m_tab = Tab::Installed;
            m_more = m_compress_back_more;
        }
        return;
    }
    if ((down & HidNpadButton_Y) && !rows.empty()) {
        // Y marks, as in the other lists (4A)
        if (m_compress_picked.erase(m_compress_app)) return;
        if (CompressMarkable(c, m_compress_app)) m_compress_picked.insert(m_compress_app);
        else Notice(m.cz_cant_mark);
        return;
    }
    if (m_compress_ask && m_dialog.kind == DialogKind::None && !c.running) {
        // opened from the menu of + of a game: its question, once the check knows it
        const auto it = c.games.find(m_compress_ask);
        const bool known = it != c.games.end() && it->second.state != GameEstimate::State::Pending;
        const bool listed = std::any_of(rows.begin(), rows.end(), [&](int r) { return v.apps[size_t(r)].app_id == m_compress_ask; });
        if (known && listed && m_compress_app == m_compress_ask) {
            m_compress_ask = 0;
            down |= HidNpadButton_A;
        } else if (v.scanned && m_compress_started && !c.analyzing && (!listed || !known)) {
            m_compress_ask = 0; // nothing to ask: the row says why
        }
    }
    if (busy) {
        if (down & HidNpadButton_X) {
            if (m_compress_queue.empty()) {
                m_compress_round.stopped = true;
                m_compress->Stop();
            } else {
                // the rest of the queue goes too: asked first
                OpenDialog(DialogKind::CompressStop);
            }
            return;
        }
        // while it works, A only adds to the queue
        if ((down & HidNpadButton_A) && !rows.empty()) {
            if (CompressMarkable(c, m_compress_app)) AskCompress({m_compress_app});
            else Notice(m.cz_busy);
        }
        return;
    }
    if (!(down & (HidNpadButton_A | HidNpadButton_X)) || rows.empty()) return;
    const auto& a = v.apps[size_t(rows[size_t(std::clamp(m_compress_focus, 0, int(rows.size()) - 1))])];
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
    if (!known || e.state == GameEstimate::State::Pending) {
        // still being checked: it says so
        if (!c.analyzing) m_compress->StartAnalysis({a.app_id});
        Notice(m.cz_pending);
        return;
    }
    if (e.state == GameEstimate::State::Ours || e.why == GameEstimate::Why::Redo) {
        OpenDialog(DialogKind::CompressUndo);
        m_dialog.ids = {a.app_id};
        m_dialog.text = a.name;
        return;
    }
    // what can be compressed is compressed when chosen: what it saves shows when it ends
    if (!CompressCanQueue(e)) return;
    AskCompress({a.app_id});
}

// The questions of this screen (drawn by DrawDialog).
void App::CompressDialogText(std::string& title, std::string& body, std::string& note, std::string& primary) {
    const More& m = TrM();
    char text[400];
    switch (m_dialog.kind) {
            break;
        case DialogKind::CompressGo: {
            const bool busy = m_compress && (m_compress->Progress().running || !m_compress_queue.empty());
            if (m_dialog.ids.size() > 1 && m_compress) {
                // every one marked, in one question: their names, then what changes
                const auto c = m_compress->Snapshot();
                const auto v = m_space ? m_space->Snapshot() : Space::View{};
                std::snprintf(text, sizeof(text), m.cz_go_many_title_fmt, int(m_dialog.ids.size()));
                title = text;
                std::string names;
                bool merge = false;
                for (size_t i = 0; i < m_dialog.ids.size(); i++) {
                    const auto it = c.games.find(m_dialog.ids[i]);
                    merge |= it != c.games.end() && it->second.merge;
                    if (i >= 4) continue;
                    names += (i ? ", " : "") + CompressName(v, m_dialog.ids[i]);
                }
                if (m_dialog.ids.size() > 4) {
                    std::snprintf(text, sizeof(text), m.cz_and_more_fmt, int(m_dialog.ids.size()) - 4);
                    names += std::string(" ") + text;
                }
                body = names + "\n" + m.cz_go_many_body + "\n" + (merge ? std::string(m.cz_merge_risks) + "\n" : std::string{}) + m.cz_risks;
                std::snprintf(text, sizeof(text), m.cz_go_many_fmt, int(m_dialog.ids.size()));
                primary = busy ? m.cz_add_queue : text;
                break;
            }
            GameEstimate e;
            if (m_compress && !m_dialog.ids.empty()) {
                const auto c = m_compress->Snapshot();
                const auto it = c.games.find(m_dialog.ids[0]);
                if (it != c.games.end()) e = it->second;
            }
            std::string dlc;
            if (!e.dlcs.empty()) {
                std::snprintf(text, sizeof(text), m.cz_go_dlc_fmt, int(e.dlcs.size()));
                dlc = std::string(text) + "\n";
            }
            if (e.merge) {
                std::snprintf(text, sizeof(text), m.cz_go_merge_title_fmt, m_dialog.text.c_str());
                title = text;
                std::snprintf(text, sizeof(text), m.cz_go_merge_body_fmt, e.patch);
                body = std::string(text) + "\n" + m.cz_merge_risks + "\n" + dlc + m.cz_risks;
                primary = m.cz_merge_go;
            } else {
                std::snprintf(text, sizeof(text), m.cz_go_title_fmt, m_dialog.text.c_str());
                title = text;
                body = std::string(m.cz_go_body) + "\n" + dlc + m.cz_risks;
                primary = m.cz_go;
            }
            if (busy) primary = m.cz_add_queue; // it waits its turn
            break;
        }
        case DialogKind::CompressStop: {
            title = m.cz_stop;
            const auto v = m_space ? m_space->Snapshot() : Space::View{};
            const std::string name = m_compress ? CompressName(v, m_compress->Progress().app) : std::string{};
            std::snprintf(text, sizeof(text), m.cz_stop_body_fmt, name.c_str(), int(m_compress_queue.size()));
            body = text;
            primary = m.cz_stop;
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
        case DialogKind::CompressGo:
            // into the queue: it starts at once when nothing else is at work
            CompressEnqueue(m_dialog.ids);
            break;
        case DialogKind::CompressStop:
            m_compress_round.stopped = true;
            m_compress_queue.clear();
            if (m_compress) m_compress->Stop();
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
