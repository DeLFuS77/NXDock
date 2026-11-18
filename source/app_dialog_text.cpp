// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The text of each dialog, by area: its title, body, rows, note and buttons. DrawDialog
// (app_dialog.cpp) draws them all in the same card.
#include "app_shared.hpp"
#include "dialog_fit.hpp"

namespace nxdock {

using namespace appui;
using Line = App::DialogLine;

// The text of a dialog of installing: where, the queue, versions, checking, what the web sends, going on.
App::Fill App::DialogInstallText(DialogContent& dc) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    switch (m_dialog.kind) {
        case DialogKind::Nand: {
            dc.title = s.nand_title;
            dc.body = s.nand_body;
            for (const auto& j : m_dialog.jobs) {
                dc.size += j.size;
                dc.lines.push_back({j.label, "", AsciiUpper(j.format) + " · " + SizeText(j.size), t.text});
            }
            dc.primary = s.nand_confirm;
            dc.bar = m_nand_total_bytes > 0 && m_nand_free_bytes >= 0; // unknown: no bar
            if (m_nand_free_bytes >= 0 && dc.size + kNandMargin > u64(m_nand_free_bytes)) {
                dc.can = false;
                char text[128];
                std::snprintf(text, sizeof(text), s.nand_no_room, SizeText(dc.size + kNandMargin - u64(m_nand_free_bytes)).c_str());
                dc.error = text;
            }
            break;
        }
        case DialogKind::Undo: {
            const auto items = m_queue->Session();
            std::vector<SessionItem> chosen;
            for (const auto& i : items) {
                const bool wanted = m_dialog.undo.empty() || std::find(m_dialog.undo.begin(), m_dialog.undo.end(), i.job) != m_dialog.undo.end();
                if (wanted && Undoable(i)) chosen.push_back(i);
            }
            dc.title = m_dialog.undo.empty() ? s.undo_title : s.undo_one_title;
            for (const auto& i : chosen) {
                dc.size += i.size;
                dc.lines.push_back({i.game + " · " + i.label, WhereText(i.nand), SizeText(i.size), t.text});
            }
            dc.note = s.undo_note;
            char text[96];
            std::snprintf(text, sizeof(text), s.undo_confirm, int(chosen.size()), SizeText(dc.size).c_str());
            dc.primary = text;
            dc.can = !chosen.empty();
            break;
        }
        case DialogKind::UpdateAll: {
            for (const auto& j : m_dialog.jobs) {
                dc.size += j.size;
                dc.lines.push_back({j.game + " · " + j.label, WhereText(j.nand), SizeText(j.size), t.text});
            }
            char text[96];
            std::snprintf(text, sizeof(text), s.update_all_title, int(m_dialog.jobs.size()));
            dc.title = text;
            dc.note = s.update_all_note;
            std::snprintf(text, sizeof(text), s.update_all_confirm, int(m_dialog.jobs.size()), SizeText(dc.size).c_str());
            dc.primary = text;
            break;
        }
        case DialogKind::Verify: {
            if (m_dialog.game < 0 || m_dialog.game >= int(m_games.size())) {
                m_dialog.kind = DialogKind::None;
                return Fill::Stop;
            }
            const Game& g = m_games[m_dialog.game];
            char text[160];
            std::snprintf(text, sizeof(text), s.verify_title, g.name.c_str());
            dc.title = text;
            const auto v = m_queue->GetVerify();
            dc.verify_bar = true;
            dc.verify_frac = v.bytes ? float(double(v.bytes_done) / double(v.bytes)) : 0.f;
            dc.verify_running = v.running;
            std::snprintf(text, sizeof(text), s.verify_progress, v.parts_done, v.parts, SizeText(v.bytes_done).c_str(), SizeText(v.bytes).c_str());
            dc.verify_text = text;
            m_dialog.single = true;
            if (v.done) {
                if (R_FAILED(v.rc) && !IsCancelled(v.rc)) {
                    dc.error = InstallErrorText(v.rc, 0, 0, -1, false, false);
                }
                bool bad = false;
                for (const auto& c : v.result) {
                    std::string right;
                    if (c.bad.empty()) {
                        right = s.verify_fine;
                    } else {
                        char b[96];
                        std::snprintf(b, sizeof(b), s.verify_bad, u32(c.bad.size()), c.parts);
                        right = b;
                        bad = true;
                    }
                    dc.lines.push_back({ContentLabel(g, c.key), WhereText(c.nand), right, c.bad.empty() ? t.ok : t.err});
                }
                std::vector<InstallJob> repairs;
                if (bad && RepairJobs(v, repairs)) {
                    u64 bytes = 0;
                    for (const auto& j : repairs) bytes += j.size;
                    std::snprintf(text, sizeof(text), s.repair_confirm, SizeText(bytes).c_str());
                    dc.primary = text;
                    dc.note = s.repair_note;
                    m_dialog.single = false;
                } else if (bad) {
                    dc.error = s.repair_missing;
                } else if (dc.error.empty()) {
                    dc.note = s.verify_all_ok;
                }
            }
            break;
        }
        case DialogKind::Push: {
            for (const auto& j : m_dialog.jobs) {
                dc.size += j.size;
                dc.lines.push_back({j.game + " · " + j.label, "", AsciiUpper(j.format) + " · " + SizeText(j.size), t.text});
            }
            char text[160];
            std::snprintf(text, sizeof(text), s.push_title, int(m_dialog.jobs.size()));
            dc.title = text;
            std::snprintf(text, sizeof(text), s.push_from, m_dialog.from.c_str());
            dc.body = text;
            std::snprintf(text, sizeof(text), s.push_install, SizeText(dc.size).c_str());
            dc.primary = text;
            dc.cancel_label = s.not_now;
            break;
        }
        case DialogKind::SigpatchInfo: {
            // the orange line of the page of a game (5B): what they are, where they go, the wiki
            const More& m = TrM();
            dc.title = m.sg_info_title;
            dc.body = m.sg_info_body;
            dc.qr = "https://github.com/DeLFuS77/NXDock/wiki/FAQ";
            dc.qr_text = m.sg_info_qr;
            dc.note = m.sg_info_where;
            m_dialog.single = true;
            break;
        }
        case DialogKind::Risk: {
            dc.title = s.risk_title;
            // the reasons, then what is recommended
            if (m_safety->EmuMmc() != Safety::Answer::Yes) {
                dc.body += std::string("• ") + s.risk_sysmmc + "\n";
            }
            if (m_safety->NintendoBlocked() == Safety::Answer::No) {
                dc.body += std::string("• ") + s.risk_online + "\n";
            }
            dc.note = s.risk_body;
            dc.primary = s.risk_go;
            break;
        }
        case DialogKind::Preflight:
            dc.title = TrM().pf_title;
            dc.body = TrM().pf_body;
            dc.cancel_label = s.cancel;
            break;
        case DialogKind::DlcNeeds: {
            const More& m = TrM();
            const auto installed = m_queue ? m_queue->Installed() : std::map<u64, InstalledState>{};
            for (const auto& j : m_dialog.jobs) {
                u64 id = j.title_id;
                u32 v = 0;
                if (j.origin != InstallJob::Origin::Library) TitleFromName(j.game + " " + j.label, id, v);
                if (!j.dlc_needs || !id || TitleKind(id) != 2) continue;
                const u64 app = (id - 0x1000) & ~u64(0xFFF);
                u32 have = 0;
                const auto it = installed.find(app);
                if (it != installed.end()) have = it->second.patch;
                for (const auto& o : m_dialog.jobs) {
                    u64 oid = o.title_id;
                    u32 ov = o.version;
                    if (o.origin != InstallJob::Origin::Library) TitleFromName(o.game + " " + o.label, oid, ov);
                    if (oid && TitleKind(oid) == 1 && (oid & ~u64(0xFFF)) == app) have = std::max(have, ov);
                }
                if (j.dlc_needs <= have) continue;
                char right[200];
                std::snprintf(right, sizeof(right), m.dn_line_fmt, VersionText(j.dlc_needs).c_str(), VersionText(have).c_str());
                dc.lines.push_back({j.game + " · " + j.label, "", right, t.orange});
            }
            dc.title = m.dn_title;
            dc.body = m.dn_body;
            dc.primary = m.dn_go;
            break;
        }
        case DialogKind::UpdateStops: {
            const More& m = TrM();
            bool only_in_game = true;
            const auto& checks = m_dialog.checks;
            for (size_t i = 0; i < checks.size(); i++) {
                if (!StopsTheGame(checks[i].over)) continue;
                const auto& j = m_dialog.jobs[i];
                only_in_game &= checks[i].over == UpdateOver::InGame;
                const char* why = checks[i].over == UpdateOver::InGame ? m.us_ingame : checks[i].base.compressed ? m.us_compressed : m.us_optimized;
                dc.lines.push_back({j.game + " · " + j.label, "", why, t.orange});
            }
            dc.title = only_in_game ? m.e_ingame_t : m.e_stops_t;
            dc.body = only_in_game ? std::string(m.e_ingame_w) + " " + m.e_ingame_d : std::string(m.us_body);
            if (m_dialog.single && !only_in_game) dc.body += std::string("\n") + m.us_no_original;
            dc.cancel_label = m.us_leave;
            dc.primary = m.us_go;
            break;
        }
        case DialogKind::Reinstall: {
            const More& m = TrM();
            int count = 0;
            for (const auto& j : m_dialog.jobs) {
                if (j.reinstall_ok) continue;
                count++;
                dc.lines.push_back({j.game + " · " + j.label, "", AsciiUpper(j.format) + " · " + SizeText(j.size), t.text});
            }
            char text[200];
            std::snprintf(text, sizeof(text), count == 1 ? m.ri_title_one : m.ri_title_fmt, count);
            dc.title = text;
            dc.body = m.ri_body;
            dc.primary = m.ri_go;
            break;
        }
        case DialogKind::UpdateHere: {
            // the updates and DLC of a folder or a shop for the games installed (15A)
            const More& m = TrM();
            char text[300];
            if (m_here_waiting) {
                dc.title = m.uh_row;
                dc.body = m_here_where + "\n" + m.uh_looking;
                dc.verify_bar = dc.verify_running = true;
                dc.verify_text = m.fs_comparing;
                dc.can = false;
                break;
            }
            const int n = int(m_dialog.jobs.size());
            if (n == 0) {
                dc.title = m.uh_row;
                dc.body = m_dialog.text + "\n" + m.uh_none;
            } else {
                std::snprintf(text, sizeof(text), m.uh_found_fmt, n);
                dc.title = text;
                dc.body = m_dialog.text;
            }
            if (m_dialog.size > 0) {
                std::snprintf(text, sizeof(text), m.uh_left_fmt, int(m_dialog.size));
                dc.body += std::string("\n") + text;
            }
            m_dialog.marks.resize(size_t(n), 1);
            m_dialog.entry = std::clamp(m_dialog.entry, 0, std::max(0, n - 1));
            // a window of five around the focus, each with its box
            const int first = std::clamp(m_dialog.entry - 2, 0, std::max(0, n - 5));
            int marked = 0;
            for (int i = 0; i < n; i++) {
                if (!m_dialog.marks[size_t(i)]) continue;
                marked++;
                dc.size += m_dialog.jobs[size_t(i)].size;
            }
            for (int r = first; r < n && r < first + 5; r++) {
                const InstallJob& j = m_dialog.jobs[size_t(r)];
                Line l{j.game, m.uh_new, AsciiUpper(j.format) + " \xC2\xB7 " + SizeText(j.size), t.text};
                l.focus = r == m_dialog.entry;
                l.hit = r;
                l.tick = m_dialog.marks[size_t(r)] ? 1 : 0;
                dc.lines.push_back(l);
            }
            std::snprintf(text, sizeof(text), m.uh_add_fmt, marked, SizeText(dc.size).c_str());
            dc.primary = text;
            dc.can = marked > 0;
            break;
        }
        case DialogKind::Versions: {
            const More& m = TrM();
            const Game& g = m_games[std::clamp(m_dialog.game, 0, int(m_games.size()) - 1)];
            const auto rows = VersionRows(g);
            m_dialog.entry = std::clamp(m_dialog.entry, 0, std::max(0, int(rows.size()) - 1));
            dc.title = m.ver_title;
            dc.body = g.name;
            // a window of five around the focus
            const int first = std::clamp(m_dialog.entry - 2, 0, std::max(0, int(rows.size()) - 5));
            for (int r = first; r < int(rows.size()) && r < first + 5; r++) {
                const CatalogFile& f = g.files[rows[r]];
                const bool here = f.version == g.installed.patch;
                Line l{VersionText(f.version), here ? s.installed_tag : "",
                       AsciiUpper(f.format) + " \xC2\xB7 " + SizeText(f.size), t.text};
                l.focus = r == m_dialog.entry;
                l.hit = r;
                dc.lines.push_back(l);
            }
            const CatalogFile& chosen = g.files[rows[size_t(m_dialog.entry)]];
            char text[96];
            if (chosen.version == g.installed.patch) {
                dc.primary = m.ver_installed;
                dc.can = false;
            } else if (chosen.version < g.installed.patch) {
                std::snprintf(text, sizeof(text), m.ver_back_fmt, VersionText(chosen.version).c_str());
                dc.primary = text;
                dc.note = m.ver_note;
            } else {
                std::snprintf(text, sizeof(text), m.ver_install_fmt, VersionText(chosen.version).c_str());
                dc.primary = text;
            }
            break;
        }
        case DialogKind::Downgrade: {
            const More& m = TrM();
            char text[200];
            std::snprintf(text, sizeof(text), m.dg_title_fmt, m_dialog.jobs.empty() ? "" : VersionText(m_dialog.jobs[0].version).c_str());
            dc.title = text;
            std::snprintf(text, sizeof(text), m.dg_body_fmt, VersionText(u32(m_dialog.size)).c_str(),
                          m_dialog.jobs.empty() ? "" : VersionText(m_dialog.jobs[0].version).c_str());
            dc.body = text;
            dc.note = m.ver_note;
            dc.primary = m.dg_go;
            break;
        }
        case DialogKind::Resume: {
            const More& m = TrM();
            dc.title = m_resume && KeptForLater(*m_resume) ? m.rs_left_title : m.rs_title;
            if (m_resume) {
                const auto& f = m_resume->job.fields;
                const auto get = [&f](const char* k) {
                    auto it = f.find(k);
                    return it == f.end() ? std::string() : it->second;
                };
                dc.body = get("game") + (get("label").empty() ? "" : " \xC2\xB7 " + get("label"));
                long long bytes = 0;
                const int parts = ResumeChecked(*m_resume, &bytes);
                char text[300];
                std::snprintf(text, sizeof(text), m.rs_parts_fmt, parts, SizeText(u64(bytes)).c_str());
                if (parts > 0) dc.body += std::string("\n") + text;
                if (!m_resume->waiting.empty()) {
                    std::snprintf(text, sizeof(text), m.rs_waiting_fmt, int(m_resume->waiting.size()));
                    dc.body += std::string("\n") + text;
                }
            }
            dc.note = m.rs_note;
            dc.primary = m.rs_go;
            dc.cancel_label = m.rs_later;
            break;
        }
        case DialogKind::QueueCancel: {
            const More& m = TrM();
            char text[300];
            std::snprintf(text, sizeof(text), m.qc_title, m_dialog.text.c_str());
            dc.title = text;
            dc.body = m.qc_body;
            dc.primary = m.qw_cancel_go;
            dc.cancel_label = m.qw_keep;
            break;
        }
        case DialogKind::ResumeDiscard: {
            const More& m = TrM();
            dc.title = m.rd_title;
            long long bytes = 0;
            const int parts = m_resume ? ResumeChecked(*m_resume, &bytes) : 0;
            char text[300];
            std::snprintf(text, sizeof(text), m.rd_body_fmt, parts, SizeText(u64(bytes)).c_str());
            dc.body = text;
            dc.primary = m.rd_go;
            dc.cancel_label = s.back;
            break;
        }
        case DialogKind::WebCancel: {
            const More& m = TrM();
            char text[300];
            std::snprintf(text, sizeof(text), m.qw_cancel_title, m_dialog.text.c_str());
            dc.title = text;
            dc.body = m.qw_cancel_body;
            dc.primary = m.qw_cancel_go;
            dc.cancel_label = m.qw_keep;
            break;
        }
        case DialogKind::MtpNand: {
            const More& m = TrM();
            dc.title = m.nand_q_title;
            dc.body = m.nand_q_body;
            dc.primary = m.allow_nand;
            break;
        }
        case DialogKind::PreCheck: {
            const More& m = TrM();
            dc.title = m.pc_title;
            if (!m_dialog.jobs.empty()) {
                const auto& j0 = m_dialog.jobs.front();
                dc.body = j0.game + " \xC2\xB7 " + j0.label;
                if (m_dialog.jobs.size() > 1) {
                    char text[256];
                    std::snprintf(text, sizeof(text), m.pc_and_more, dc.body.c_str(), int(m_dialog.jobs.size()) - 1);
                    dc.body = text;
                }
            }
            const auto add = [&](const char* name, const PreCheck::Line& l) {
                const int mark = l.state == PreCheck::State::Fine ? 1 : l.state == PreCheck::State::Warn ? 2 : 0;
                dc.lines.push_back({name, "", l.right, mark == 2 ? t.warn : t.muted, mark});
            };
            add(m.pc_firmware, m_dialog.check.firmware);
            add(m.pc_keys, m_dialog.check.keys);
            add(m.pc_ams, m_dialog.check.atmosphere);
            dc.note = m.pc_note;
            dc.primary = m.pc_go;
            break;
        }
        case DialogKind::Share:
            dc.title = s.share_title;
            dc.body = s.share_body;
            dc.primary = s.share_yes;
            dc.cancel_label = s.no_word;
            break;
        case DialogKind::Where:
            dc.title = s.where_title;
            dc.body = s.where_body;
            for (const auto& j : m_dialog.jobs) {
                dc.size += j.size;
                dc.lines.push_back({j.label, "", AsciiUpper(j.format) + " · " + SizeText(j.size), t.text});
            }
            dc.primary = s.sd_card;
            break;
        default: return Fill::NotMine;
    }
    return Fill::Draw;
}

// The text of a dialog of In the console: taking a game off, playing, a .nro, the tickets, mods and saves.
App::Fill App::DialogConsoleText(DialogContent& dc) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    switch (m_dialog.kind) {
        case DialogKind::ConsoleRemove: {
            const More& m = TrM();
            char text[300];
            if (!m_dialog.ids.empty()) {
                // several games, each whole
                std::snprintf(text, sizeof(text), m.ic_rm_many_fmt, int(m_dialog.ids.size()));
                dc.title = text;
                const auto v = m_space ? m_space->Snapshot() : Space::View{};
                for (const auto app : m_dialog.ids) {
                    for (const auto& a : v.apps) {
                        if (a.app_id != app) continue;
                        int updates = 0, dlc = 0;
                        for (const auto& c : a.contents) {
                            updates += c.type == NcmContentMetaType_Patch;
                            dlc += c.type == NcmContentMetaType_AddOnContent;
                        }
                        // "1 update, 2 DLC": the singular for one, nothing for none
                        std::string what = a.name;
                        const std::string with = CountsText(updates, dlc, m.ic_upd_one, m.ic_upd_fmt, m.ic_dlc_one, m.ic_dlc_fmt, m.ic_list_sep);
                        if (!with.empty()) what += " \xC2\xB7 " + with;
                        dc.size += a.Bytes();
                        dc.lines.push_back({what, "", SizeText(a.Bytes()), t.text});
                    }
                }
            } else {
                std::snprintf(text, sizeof(text), m.ic_rm_title_fmt, m_dialog.text.c_str());
                dc.title = text;
            }
            for (const auto& c : m_console_remove) {
                if (!m_dialog.ids.empty()) break;
                dc.size += c.bytes;
                dc.lines.push_back({ContentLabel(c, m_console_app), c.nand ? s.internal_short : "SD", SizeText(c.bytes), t.text});
            }
            std::snprintf(text, sizeof(text), m.ic_rm_body_fmt, SizeText(dc.size).c_str());
            dc.body = text;
            // the saves can be copied first, when the game itself goes (3A, 14/11)
            dc.tick = ConsoleRemoveTakesGame();
            dc.primary = dc.tick && m_dialog.keep_saves ? m.ic_rm_copy_go : m.ic_rm_go;
            // its mods and its tickets, only when it has any (7A)
            dc.tick2 = dc.tick && (m_dialog.extra_mods > 0 || m_dialog.extra_common + m_dialog.extra_personal > 0);
            break;
        }
        case DialogKind::RemoveStopped:
            // Cancel while games were taken off: how many went, which were not touched
            dc.title = TrM().ic_rm_stopped_t;
            dc.body = m_dialog.text;
            break;
        case DialogKind::Play: {
            const More& m = TrM();
            char text[300];
            std::snprintf(text, sizeof(text), m.pl_title_fmt, m_dialog.text.c_str());
            dc.title = text;
            dc.body = m.pl_body;
            dc.primary = m.pl_play;
            break;
        }
        case DialogKind::PlayRandom: {
            // a game at random (21A): its name, how much it was played, and another one on Y
            const More& m = TrM();
            char text[300];
            std::snprintf(text, sizeof(text), m.rg_title_fmt, m_dialog.text.c_str());
            dc.title = text;
            dc.body = m_dialog.from;
            dc.primary = m.pl_play;
            break;
        }
        case DialogKind::OpenNro: {
            // a .nro of the explorer (22A): NXDock closes; what the queue has stops, and can go on
            const More& m = TrM();
            char text[300];
            std::snprintf(text, sizeof(text), m.pl_title_fmt, m_nro_name.c_str());
            dc.title = text;
            dc.body = m.nro_close;
            const auto jobs = m_queue ? m_queue->Jobs() : std::vector<InstallJob>{};
            int pending = 0, lost = 0;
            for (const auto& j : jobs) {
                if (j.state != InstallJob::State::Waiting && j.state != InstallJob::State::Running) continue;
                pending++;
                lost += !j.resumable;
            }
            if (pending) {
                std::snprintf(text, sizeof(text), m.nro_queue_fmt, pending);
                dc.lines.push_back({text, "", "", t.orange, 2});
                if (pending > lost) dc.lines.push_back({m.nro_go_on, "", "", t.text});
                if (lost) dc.lines.push_back({m.nro_lost, "", "", t.text});
            }
            dc.primary = m.open;
            break;
        }
        case DialogKind::TicketsDelete: {
            const More& m = TrM();
            char text[200];
            std::snprintf(text, sizeof(text), m.tk_del_title_fmt, int(m_ticket_delete.size()));
            dc.title = text;
            for (const auto& id : m_ticket_delete) {
                for (const auto& r : m_tickets) {
                    if (std::memcmp(r.id.rights, id.rights, sizeof(id.rights)) || r.id.personal != id.personal) continue;
                    dc.lines.push_back({(r.name.empty() ? std::string(m.tk_no_name) : r.name) + " \xC2\xB7 " + (id.personal ? m.tk_personal : m.tk_common), "",
                                     r.orphan ? m.tk_no_game : "", r.orphan ? t.orange : t.muted});
                    break;
                }
            }
            dc.body = m.tk_del_body;
            dc.primary = m.tk_del_go;
            break;
        }
        case DialogKind::ModsAsk: {
            const More& m = TrM();
            dc.title = m.mr_title;
            dc.body = m_dialog.text + "\n" + m_dialog.from + "\n" + m.mr_body;
            dc.primary = m.mr_go;
            dc.cancel_label = m.mr_not_now;
            break;
        }
        case DialogKind::SaveCopyAsk: {
            const More& m = TrM();
            char text[300];
            std::snprintf(text, sizeof(text), m.sc_ask_title, m_dialog.text.c_str());
            dc.title = text;
            dc.body = m.sc_ask_body;
            dc.error = m_dialog.from;
            dc.primary = m_dialog.entry == 2 ? m.sc_go_remove : m.sc_go_on;
            break;
        }
        default: return Fill::NotMine;
    }
    return Fill::Draw;
}

// The text of a dialog of the More tab: the vault, space, shops, compressing, USB 3.0.
App::Fill App::DialogMoreText(DialogContent& dc) {
    const Theme& t = m_theme;
    switch (m_dialog.kind) {
        case DialogKind::VaultRestore: {
            const More& m = TrM();
            dc.title = m.restore_title;
            const std::string game = m_dialog.game >= 0 && m_dialog.game < int(m_vault_saves.size()) ? m_vault_saves[m_dialog.game].name : "";
            char text[512];
            std::snprintf(text, sizeof(text), m.restore_body, game.c_str(), m_dialog.save.note.empty() ? m.no_note : m_dialog.save.note.c_str(),
                          WhenText(m_dialog.save.when).c_str());
            dc.body = text;
            dc.primary = m.restore_go;
            break;
        }
        case DialogKind::SpaceClean:
        case DialogKind::SpaceArchive:
        case DialogKind::SpaceMove:
        case DialogKind::SpaceCard:
        case DialogKind::SpaceDump:
            SpaceDialogText(dc.title, dc.body, dc.primary);
            break;
        case DialogKind::ShopRemove: {
            const More& m = TrM();
            char text[200];
            std::snprintf(text, sizeof(text), m.sh_remove_title,
                          m_dialog.entry >= 0 && m_dialog.entry < int(m_shops.size()) ? m_shops[size_t(m_dialog.entry)].name.c_str() : "");
            dc.title = text;
            dc.body = m.sh_remove_body;
            dc.primary = m.sh_remove;
            break;
        }
        case DialogKind::VaultError:
            dc.title = m_dialog.text.empty() ? TrM().t_saves : m_dialog.text;
            dc.error = m_dialog.from;
            if (m_dialog.report) {
                // a game that could not be compressed (4A): Close, and a report for the library
                // when one is paired
                m_dialog.single = !CanSendReport();
                dc.primary = TrM().rp_button;
                dc.cancel_label = Tr().close;
            }
            break;
        case DialogKind::CompressGo:
        case DialogKind::CompressStop:
        case DialogKind::CompressUndo:
        case DialogKind::CompressInfo:
        case DialogKind::CompressStopsOriginal:
        case DialogKind::CompressStopsRemove:
            CompressDialogText(dc.title, dc.body, dc.note, dc.primary);
            break;
        case DialogKind::Usb3Reboot: {
            // USB 3.0 written (USB A): reboot now or later
            const Tools& tt = TrT();
            dc.title = m_usb3_written_on ? tt.u3_done_on : tt.u3_done_off;
            dc.body = m_usb3_written_on ? tt.u3_until : tt.u3_until_off;
            if (m_usb3_backed) dc.lines.push_back({tt.u3_copy_saved, "", "", t.text, 1});
            dc.lines.push_back({m_usb3_written_on ? tt.u3_line_on : tt.u3_line_off, "", "", t.text, 1});
            dc.note = tt.u3_waits;
            dc.primary = tt.u3_reboot_now;
            dc.cancel_label = tt.u3_later;
            break;
        }
        default: return Fill::NotMine;
    }
    return Fill::Draw;
}

// The text of a dialog of the app: leaving, forgetting the library, the HOME menu, the history.
App::Fill App::DialogAppText(DialogContent& dc) {
    const Theme& t = m_theme;
    const auto& s = Tr();
    switch (m_dialog.kind) {
        case DialogKind::Quit:
            dc.title = TrM().quit_title;
            dc.body = TrM().quit_body;
            dc.primary = TrM().quit_go;
            dc.cancel_label = TrM().quit_stay;
            break;
        case DialogKind::Forget:
            dc.title = TrM().fg_title;
            dc.body = TrM().fg_body;
            dc.primary = TrM().fg_go;
            break;
        case DialogKind::HomeAdd:
            dc.title = m_home.installed ? TrM().hm_update_title : TrM().hm_add_title;
            dc.body = TrM().hm_add_body;
            dc.primary = m_home.installed ? TrM().hm_update : TrM().hm_make;
            break;
        case DialogKind::HomeRemove:
            dc.title = TrM().hm_remove_title;
            dc.body = TrM().hm_remove_body;
            dc.primary = TrM().hm_remove_go;
            break;
        case DialogKind::ClearHistory:
            dc.title = s.clear_title;
            dc.body = s.clear_body;
            dc.primary = s.clear_confirm;
            break;
        case DialogKind::HistoryInfo: {
            if (m_dialog.entry < 0 || m_dialog.entry >= int(m_history.size())) {
                m_dialog.kind = DialogKind::None;
                return Fill::Stop;
            }
            const HistoryEntry& e = m_history[m_dialog.entry];
            dc.title = e.game;
            dc.lines.push_back({e.label + " · " + AsciiUpper(e.format), "", e.nand ? s.internal : s.sd_card, t.text});
            dc.lines.push_back({s.col_date, "", WhenText(e.when), t.text});
            dc.lines.push_back({s.col_size, "", SizeText(e.size), t.text});
            if (e.written) dc.lines.push_back({s.col_written, "", SizeText(e.written), t.text});
            if (e.seconds > 0) dc.lines.push_back({s.col_time, "", Duration(e.seconds), t.text});
            if (e.result == HistoryEntry::Result::Installed) dc.lines.push_back({s.col_speed, "", SpeedText(e.Speed()), t.text});
            dc.lines.push_back({s.col_result, "", ResultText(e.result), ResultColor(t, e.result)});
            if (e.result == HistoryEntry::Result::Failed) dc.error = e.error;
            break;
        }
        default: return Fill::NotMine;
    }
    return Fill::Draw;
}

} // namespace nxdock
