// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The dialogs over any screen: drawn, moved through and confirmed.
#include "app_shared.hpp"

namespace nxdock {

using namespace appui;

void App::DrawDialog(NVGcontext* vg) {
    if (m_dialog.kind == DialogKind::Usb3) {
        DrawUsb3Sheet(vg); // a sheet of its own, wider (more_usb3.cpp)
        return;
    }
    const Theme& t = m_theme;
    const auto& s = Tr();
    // everything behind goes dark; a tap there is Cancel
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, kWidth, kHeight);
    nvgFillColor(vg, nvgRGBAf(0, 0, 0, 0.55f * ui::EaseOut(ui::Appear(m_dialog_at, 0.2f))));
    nvgFill(vg);
    AddHit(0, 0, kWidth, kHeight, HitKind::DialogBack);

    struct Line {
        std::string text, tag, right;
        NVGcolor color;
        int mark = 0; // 1: fine, 2: a warning (the check before installing)
        bool focus = false; // the row in focus (the versions of a game, 20A)
        int hit = -1;       // a row that answers to touch
        int tick = -1;      // a box to tick before it: 0 off, 1 on (15A of groups 3 and 4)
    };
    std::string title, body, note, primary, error;
    std::vector<Line> lines;
    bool can = true, bar = false;
    bool verify_bar = false, verify_running = false;
    bool tick = false; // ConsoleRemove: the box of the saves (3A)
    bool tick2 = false; // ConsoleRemove: the box of its mods and its tickets (7A)
    std::string cancel_label = s.cancel;
    float verify_frac = 0;
    std::string verify_text;
    u64 size = 0;
    switch (m_dialog.kind) {
        case DialogKind::Nand: {
            title = s.nand_title;
            body = s.nand_body;
            for (const auto& j : m_dialog.jobs) {
                size += j.size;
                lines.push_back({j.label, "", AsciiUpper(j.format) + " · " + SizeText(j.size), t.text});
            }
            primary = s.nand_confirm;
            bar = m_nand_total_bytes > 0 && m_nand_free_bytes >= 0; // unknown: no bar
            if (m_nand_free_bytes >= 0 && size + kNandMargin > u64(m_nand_free_bytes)) {
                can = false;
                char text[128];
                std::snprintf(text, sizeof(text), s.nand_no_room, SizeText(size + kNandMargin - u64(m_nand_free_bytes)).c_str());
                error = text;
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
            title = m_dialog.undo.empty() ? s.undo_title : s.undo_one_title;
            for (const auto& i : chosen) {
                size += i.size;
                lines.push_back({i.game + " · " + i.label, WhereText(i.nand), SizeText(i.size), t.text});
            }
            note = s.undo_note;
            char text[96];
            std::snprintf(text, sizeof(text), s.undo_confirm, int(chosen.size()), SizeText(size).c_str());
            primary = text;
            can = !chosen.empty();
            break;
        }
        case DialogKind::UpdateAll: {
            for (const auto& j : m_dialog.jobs) {
                size += j.size;
                lines.push_back({j.game + " · " + j.label, WhereText(j.nand), SizeText(j.size), t.text});
            }
            char text[96];
            std::snprintf(text, sizeof(text), s.update_all_title, int(m_dialog.jobs.size()));
            title = text;
            note = s.update_all_note;
            std::snprintf(text, sizeof(text), s.update_all_confirm, int(m_dialog.jobs.size()), SizeText(size).c_str());
            primary = text;
            break;
        }
        case DialogKind::Verify: {
            if (m_dialog.game < 0 || m_dialog.game >= int(m_games.size())) {
                m_dialog.kind = DialogKind::None;
                return;
            }
            const Game& g = m_games[m_dialog.game];
            char text[160];
            std::snprintf(text, sizeof(text), s.verify_title, g.name.c_str());
            title = text;
            const auto v = m_queue->GetVerify();
            verify_bar = true;
            verify_frac = v.bytes ? float(double(v.bytes_done) / double(v.bytes)) : 0.f;
            verify_running = v.running;
            std::snprintf(text, sizeof(text), s.verify_progress, v.parts_done, v.parts, SizeText(v.bytes_done).c_str(), SizeText(v.bytes).c_str());
            verify_text = text;
            m_dialog.single = true;
            if (v.done) {
                if (R_FAILED(v.rc) && !IsCancelled(v.rc)) {
                    error = InstallErrorText(v.rc, 0, 0, -1, false, false);
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
                    lines.push_back({ContentLabel(g, c.key), WhereText(c.nand), right, c.bad.empty() ? t.ok : t.err});
                }
                std::vector<InstallJob> repairs;
                if (bad && RepairJobs(v, repairs)) {
                    u64 bytes = 0;
                    for (const auto& j : repairs) bytes += j.size;
                    std::snprintf(text, sizeof(text), s.repair_confirm, SizeText(bytes).c_str());
                    primary = text;
                    note = s.repair_note;
                    m_dialog.single = false;
                } else if (bad) {
                    error = s.repair_missing;
                } else if (error.empty()) {
                    note = s.verify_all_ok;
                }
            }
            break;
        }
        case DialogKind::Push: {
            for (const auto& j : m_dialog.jobs) {
                size += j.size;
                lines.push_back({j.game + " · " + j.label, "", AsciiUpper(j.format) + " · " + SizeText(j.size), t.text});
            }
            char text[160];
            std::snprintf(text, sizeof(text), s.push_title, int(m_dialog.jobs.size()));
            title = text;
            std::snprintf(text, sizeof(text), s.push_from, m_dialog.from.c_str());
            body = text;
            std::snprintf(text, sizeof(text), s.push_install, SizeText(size).c_str());
            primary = text;
            cancel_label = s.not_now;
            break;
        }
        case DialogKind::Risk: {
            title = s.risk_title;
            // the reasons, then what is recommended
            if (m_safety->EmuMmc() != Safety::Answer::Yes) {
                body += std::string("• ") + s.risk_sysmmc + "\n";
            }
            if (m_safety->NintendoBlocked() == Safety::Answer::No) {
                body += std::string("• ") + s.risk_online + "\n";
            }
            note = s.risk_body;
            primary = s.risk_go;
            break;
        }
        case DialogKind::VaultRestore: {
            const More& m = TrM();
            title = m.restore_title;
            const std::string game = m_dialog.game >= 0 && m_dialog.game < int(m_vault_saves.size()) ? m_vault_saves[m_dialog.game].name : "";
            char text[512];
            std::snprintf(text, sizeof(text), m.restore_body, game.c_str(), m_dialog.save.note.empty() ? m.no_note : m_dialog.save.note.c_str(),
                          WhenText(m_dialog.save.when).c_str());
            body = text;
            primary = m.restore_go;
            break;
        }
        case DialogKind::SpaceClean:
        case DialogKind::SpaceArchive:
        case DialogKind::SpaceMove:
        case DialogKind::SpaceCard:
        case DialogKind::SpaceDump:
            SpaceDialogText(title, body, primary);
            break;
        case DialogKind::Preflight:
            title = TrM().pf_title;
            body = TrM().pf_body;
            cancel_label = s.cancel;
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
                lines.push_back({j.game + " · " + j.label, "", right, t.orange});
            }
            title = m.dn_title;
            body = m.dn_body;
            primary = m.dn_go;
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
                lines.push_back({j.game + " · " + j.label, "", why, t.orange});
            }
            title = only_in_game ? m.e_ingame_t : m.e_stops_t;
            body = only_in_game ? std::string(m.e_ingame_w) + " " + m.e_ingame_d : std::string(m.us_body);
            if (m_dialog.single && !only_in_game) body += std::string("\n") + m.us_no_original;
            cancel_label = m.us_leave;
            primary = m.us_go;
            break;
        }
        case DialogKind::Reinstall: {
            const More& m = TrM();
            int count = 0;
            for (const auto& j : m_dialog.jobs) {
                if (j.reinstall_ok) continue;
                count++;
                lines.push_back({j.game + " · " + j.label, "", AsciiUpper(j.format) + " · " + SizeText(j.size), t.text});
            }
            char text[200];
            std::snprintf(text, sizeof(text), count == 1 ? m.ri_title_one : m.ri_title_fmt, count);
            title = text;
            body = m.ri_body;
            primary = m.ri_go;
            break;
        }
        case DialogKind::ConsoleRemove: {
            const More& m = TrM();
            char text[300];
            if (!m_dialog.ids.empty()) {
                // several games, each whole
                std::snprintf(text, sizeof(text), m.ic_rm_many_fmt, int(m_dialog.ids.size()));
                title = text;
                const auto v = m_space ? m_space->Snapshot() : Space::View{};
                for (const auto app : m_dialog.ids) {
                    for (const auto& a : v.apps) {
                        if (a.app_id != app) continue;
                        int updates = 0, dlc = 0;
                        for (const auto& c : a.contents) {
                            updates += c.type == NcmContentMetaType_Patch;
                            dlc += c.type == NcmContentMetaType_AddOnContent;
                        }
                        std::string what = a.name;
                        if (updates || dlc) {
                            std::snprintf(text, sizeof(text), m.ic_with_fmt, updates, dlc);
                            what += std::string(" · ") + text;
                        }
                        size += a.Bytes();
                        lines.push_back({what, "", SizeText(a.Bytes()), t.text});
                    }
                }
            } else {
                std::snprintf(text, sizeof(text), m.ic_rm_title_fmt, m_dialog.text.c_str());
                title = text;
            }
            for (const auto& c : m_console_remove) {
                if (!m_dialog.ids.empty()) break;
                size += c.bytes;
                lines.push_back({ContentLabel(c, m_console_app), c.nand ? s.internal_short : "SD", SizeText(c.bytes), t.text});
            }
            std::snprintf(text, sizeof(text), m.ic_rm_body_fmt, SizeText(size).c_str());
            body = text;
            // the saves can be copied first, when the game itself goes (3A, 14/11)
            tick = ConsoleRemoveTakesGame();
            primary = tick && m_dialog.keep_saves ? m.ic_rm_copy_go : m.ic_rm_go;
            // its mods and its tickets, only when it has any (7A)
            tick2 = tick && (m_dialog.extra_mods > 0 || m_dialog.extra_common + m_dialog.extra_personal > 0);
            break;
        }
        case DialogKind::Play: {
            const More& m = TrM();
            char text[300];
            std::snprintf(text, sizeof(text), m.pl_title_fmt, m_dialog.text.c_str());
            title = text;
            body = m.pl_body;
            primary = m.pl_play;
            break;
        }
        case DialogKind::UpdateHere: {
            // the updates and DLC of a folder or a shop for the games installed (15A)
            const More& m = TrM();
            char text[300];
            if (m_here_waiting) {
                title = m.uh_row;
                body = m_here_where + "\n" + m.uh_looking;
                verify_bar = verify_running = true;
                verify_text = m.fs_comparing;
                can = false;
                break;
            }
            const int n = int(m_dialog.jobs.size());
            if (n == 0) {
                title = m.uh_row;
                body = m_dialog.text + "\n" + m.uh_none;
            } else {
                std::snprintf(text, sizeof(text), m.uh_found_fmt, n);
                title = text;
                body = m_dialog.text;
            }
            if (m_dialog.size > 0) {
                std::snprintf(text, sizeof(text), m.uh_left_fmt, int(m_dialog.size));
                body += std::string("\n") + text;
            }
            m_dialog.marks.resize(size_t(n), 1);
            m_dialog.entry = std::clamp(m_dialog.entry, 0, std::max(0, n - 1));
            // a window of five around the focus, each with its box
            const int first = std::clamp(m_dialog.entry - 2, 0, std::max(0, n - 5));
            int marked = 0;
            for (int i = 0; i < n; i++) {
                if (!m_dialog.marks[size_t(i)]) continue;
                marked++;
                size += m_dialog.jobs[size_t(i)].size;
            }
            for (int r = first; r < n && r < first + 5; r++) {
                const InstallJob& j = m_dialog.jobs[size_t(r)];
                Line l{j.game, m.uh_new, AsciiUpper(j.format) + " \xC2\xB7 " + SizeText(j.size), t.text};
                l.focus = r == m_dialog.entry;
                l.hit = r;
                l.tick = m_dialog.marks[size_t(r)] ? 1 : 0;
                lines.push_back(l);
            }
            std::snprintf(text, sizeof(text), m.uh_add_fmt, marked, SizeText(size).c_str());
            primary = text;
            can = marked > 0;
            break;
        }
        case DialogKind::PlayRandom: {
            // a game at random (21A): its name, how much it was played, and another one on Y
            const More& m = TrM();
            char text[300];
            std::snprintf(text, sizeof(text), m.rg_title_fmt, m_dialog.text.c_str());
            title = text;
            body = m_dialog.from;
            primary = m.pl_play;
            break;
        }
        case DialogKind::OpenNro: {
            // a .nro of the explorer (22A): NXDock closes; what the queue has stops, and can go on
            const More& m = TrM();
            char text[300];
            std::snprintf(text, sizeof(text), m.pl_title_fmt, m_nro_name.c_str());
            title = text;
            body = m.nro_close;
            const auto jobs = m_queue ? m_queue->Jobs() : std::vector<InstallJob>{};
            int pending = 0, lost = 0;
            for (const auto& j : jobs) {
                if (j.state != InstallJob::State::Waiting && j.state != InstallJob::State::Running) continue;
                pending++;
                lost += !j.resumable;
            }
            if (pending) {
                std::snprintf(text, sizeof(text), m.nro_queue_fmt, pending);
                lines.push_back({text, "", "", t.orange, 2});
                if (pending > lost) lines.push_back({m.nro_go_on, "", "", t.text});
                if (lost) lines.push_back({m.nro_lost, "", "", t.text});
            }
            primary = m.open;
            break;
        }
        case DialogKind::TicketsDelete: {
            const More& m = TrM();
            char text[200];
            std::snprintf(text, sizeof(text), m.tk_del_title_fmt, int(m_ticket_delete.size()));
            title = text;
            for (const auto& id : m_ticket_delete) {
                for (const auto& r : m_tickets) {
                    if (std::memcmp(r.id.rights, id.rights, sizeof(id.rights)) || r.id.personal != id.personal) continue;
                    lines.push_back({(r.name.empty() ? std::string(m.tk_no_name) : r.name) + " \xC2\xB7 " + (id.personal ? m.tk_personal : m.tk_common), "",
                                     r.orphan ? m.tk_no_game : "", r.orphan ? t.orange : t.muted});
                    break;
                }
            }
            body = m.tk_del_body;
            primary = m.tk_del_go;
            break;
        }
        case DialogKind::ShopRemove: {
            const More& m = TrM();
            char text[200];
            std::snprintf(text, sizeof(text), m.sh_remove_title,
                          m_dialog.entry >= 0 && m_dialog.entry < int(m_shops.size()) ? m_shops[size_t(m_dialog.entry)].name.c_str() : "");
            title = text;
            body = m.sh_remove_body;
            primary = m.sh_remove;
            break;
        }
        case DialogKind::VaultError:
            title = m_dialog.text.empty() ? TrM().t_saves : m_dialog.text;
            error = m_dialog.from;
            break;
        case DialogKind::CompressIntro:
        case DialogKind::CompressGo:
        case DialogKind::CompressUndo:
        case DialogKind::CompressInfo:
        case DialogKind::CompressStopsOriginal:
        case DialogKind::CompressStopsRemove:
            CompressDialogText(title, body, note, primary);
            break;
        case DialogKind::Versions: {
            const More& m = TrM();
            const Game& g = m_games[std::clamp(m_dialog.game, 0, int(m_games.size()) - 1)];
            const auto rows = VersionRows(g);
            m_dialog.entry = std::clamp(m_dialog.entry, 0, std::max(0, int(rows.size()) - 1));
            title = m.ver_title;
            body = g.name;
            // a window of five around the focus
            const int first = std::clamp(m_dialog.entry - 2, 0, std::max(0, int(rows.size()) - 5));
            for (int r = first; r < int(rows.size()) && r < first + 5; r++) {
                const CatalogFile& f = g.files[rows[r]];
                const bool here = f.version == g.installed.patch;
                Line l{VersionText(f.version), here ? s.installed_tag : "",
                       AsciiUpper(f.format) + " \xC2\xB7 " + SizeText(f.size), t.text};
                l.focus = r == m_dialog.entry;
                l.hit = r;
                lines.push_back(l);
            }
            const CatalogFile& chosen = g.files[rows[size_t(m_dialog.entry)]];
            char text[96];
            if (chosen.version == g.installed.patch) {
                primary = m.ver_installed;
                can = false;
            } else if (chosen.version < g.installed.patch) {
                std::snprintf(text, sizeof(text), m.ver_back_fmt, VersionText(chosen.version).c_str());
                primary = text;
                note = m.ver_note;
            } else {
                std::snprintf(text, sizeof(text), m.ver_install_fmt, VersionText(chosen.version).c_str());
                primary = text;
            }
            break;
        }
        case DialogKind::Downgrade: {
            const More& m = TrM();
            char text[200];
            std::snprintf(text, sizeof(text), m.dg_title_fmt, m_dialog.jobs.empty() ? "" : VersionText(m_dialog.jobs[0].version).c_str());
            title = text;
            std::snprintf(text, sizeof(text), m.dg_body_fmt, VersionText(u32(m_dialog.size)).c_str(),
                          m_dialog.jobs.empty() ? "" : VersionText(m_dialog.jobs[0].version).c_str());
            body = text;
            note = m.ver_note;
            primary = m.dg_go;
            break;
        }
        case DialogKind::Resume: {
            const More& m = TrM();
            title = m_resume && KeptForLater(*m_resume) ? m.rs_left_title : m.rs_title;
            if (m_resume) {
                const auto& f = m_resume->job.fields;
                const auto get = [&f](const char* k) {
                    auto it = f.find(k);
                    return it == f.end() ? std::string() : it->second;
                };
                body = get("game") + (get("label").empty() ? "" : " \xC2\xB7 " + get("label"));
                long long bytes = 0;
                const int parts = ResumeChecked(*m_resume, &bytes);
                char text[300];
                std::snprintf(text, sizeof(text), m.rs_parts_fmt, parts, SizeText(u64(bytes)).c_str());
                if (parts > 0) body += std::string("\n") + text;
                if (!m_resume->waiting.empty()) {
                    std::snprintf(text, sizeof(text), m.rs_waiting_fmt, int(m_resume->waiting.size()));
                    body += std::string("\n") + text;
                }
            }
            note = m.rs_note;
            primary = m.rs_go;
            cancel_label = m.rs_later;
            break;
        }
        case DialogKind::QueueCancel: {
            const More& m = TrM();
            char text[300];
            std::snprintf(text, sizeof(text), m.qc_title, m_dialog.text.c_str());
            title = text;
            body = m.qc_body;
            primary = m.qw_cancel_go;
            cancel_label = m.qw_keep;
            break;
        }
        case DialogKind::Quit:
            title = TrM().quit_title;
            body = TrM().quit_body;
            primary = TrM().quit_go;
            cancel_label = TrM().quit_stay;
            break;
        case DialogKind::Forget:
            title = TrM().fg_title;
            body = TrM().fg_body;
            primary = TrM().fg_go;
            break;
        case DialogKind::HomeAdd:
            title = m_home.installed ? TrM().hm_update_title : TrM().hm_add_title;
            body = TrM().hm_add_body;
            primary = m_home.installed ? TrM().hm_update : TrM().hm_make;
            break;
        case DialogKind::HomeRemove:
            title = TrM().hm_remove_title;
            body = TrM().hm_remove_body;
            primary = TrM().hm_remove_go;
            break;
        case DialogKind::ResumeDiscard: {
            const More& m = TrM();
            title = m.rd_title;
            long long bytes = 0;
            const int parts = m_resume ? ResumeChecked(*m_resume, &bytes) : 0;
            char text[300];
            std::snprintf(text, sizeof(text), m.rd_body_fmt, parts, SizeText(u64(bytes)).c_str());
            body = text;
            primary = m.rd_go;
            cancel_label = s.back;
            break;
        }
        case DialogKind::WebCancel: {
            const More& m = TrM();
            char text[300];
            std::snprintf(text, sizeof(text), m.qw_cancel_title, m_dialog.text.c_str());
            title = text;
            body = m.qw_cancel_body;
            primary = m.qw_cancel_go;
            cancel_label = m.qw_keep;
            break;
        }
        case DialogKind::ModsAsk: {
            const More& m = TrM();
            title = m.mr_title;
            body = m_dialog.text + "\n" + m_dialog.from + "\n" + m.mr_body;
            primary = m.mr_go;
            cancel_label = m.mr_not_now;
            break;
        }
        case DialogKind::SaveCopyAsk: {
            const More& m = TrM();
            char text[300];
            std::snprintf(text, sizeof(text), m.sc_ask_title, m_dialog.text.c_str());
            title = text;
            body = m.sc_ask_body;
            error = m_dialog.from;
            primary = m_dialog.entry == 2 ? m.sc_go_remove : m.sc_go_on;
            break;
        }
        case DialogKind::MtpNand: {
            const More& m = TrM();
            title = m.nand_q_title;
            body = m.nand_q_body;
            primary = m.allow_nand;
            break;
        }
        case DialogKind::PreCheck: {
            const More& m = TrM();
            title = m.pc_title;
            if (!m_dialog.jobs.empty()) {
                const auto& j0 = m_dialog.jobs.front();
                body = j0.game + " \xC2\xB7 " + j0.label;
                if (m_dialog.jobs.size() > 1) {
                    char text[256];
                    std::snprintf(text, sizeof(text), m.pc_and_more, body.c_str(), int(m_dialog.jobs.size()) - 1);
                    body = text;
                }
            }
            const auto add = [&](const char* name, const PreCheck::Line& l) {
                const int mark = l.state == PreCheck::State::Fine ? 1 : l.state == PreCheck::State::Warn ? 2 : 0;
                lines.push_back({name, "", l.right, mark == 2 ? t.warn : t.muted, mark});
            };
            add(m.pc_firmware, m_dialog.check.firmware);
            add(m.pc_keys, m_dialog.check.keys);
            add(m.pc_ams, m_dialog.check.atmosphere);
            note = m.pc_note;
            primary = m.pc_go;
            break;
        }
        case DialogKind::Share:
            title = s.share_title;
            body = s.share_body;
            primary = s.share_yes;
            cancel_label = s.no_word;
            break;
        case DialogKind::Where:
            title = s.where_title;
            body = s.where_body;
            for (const auto& j : m_dialog.jobs) {
                size += j.size;
                lines.push_back({j.label, "", AsciiUpper(j.format) + " · " + SizeText(j.size), t.text});
            }
            primary = s.sd_card;
            break;
        case DialogKind::ClearHistory:
            title = s.clear_title;
            body = s.clear_body;
            primary = s.clear_confirm;
            break;
        case DialogKind::HistoryInfo: {
            if (m_dialog.entry < 0 || m_dialog.entry >= int(m_history.size())) {
                m_dialog.kind = DialogKind::None;
                return;
            }
            const HistoryEntry& e = m_history[m_dialog.entry];
            title = e.game;
            lines.push_back({e.label + " · " + AsciiUpper(e.format), "", e.nand ? s.internal : s.sd_card, t.text});
            lines.push_back({s.col_date, "", WhenText(e.when), t.text});
            lines.push_back({s.col_size, "", SizeText(e.size), t.text});
            if (e.written) lines.push_back({s.col_written, "", SizeText(e.written), t.text});
            if (e.seconds > 0) lines.push_back({s.col_time, "", Duration(e.seconds), t.text});
            if (e.result == HistoryEntry::Result::Installed) lines.push_back({s.col_speed, "", SpeedText(e.Speed()), t.text});
            lines.push_back({s.col_result, "", ResultText(e.result), ResultColor(t, e.result)});
            if (e.result == HistoryEntry::Result::Failed) error = e.error;
            break;
        }
        case DialogKind::Usb3Reboot: {
            // USB 3.0 written (USB A): reboot now or later
            const Tools& tt = TrT();
            title = m_usb3_written_on ? tt.u3_done_on : tt.u3_done_off;
            body = m_usb3_written_on ? tt.u3_until : tt.u3_until_off;
            if (m_usb3_backed) lines.push_back({tt.u3_copy_saved, "", "", t.text, 1});
            lines.push_back({m_usb3_written_on ? tt.u3_line_on : tt.u3_line_off, "", "", t.text, 1});
            note = tt.u3_waits;
            primary = tt.u3_reboot_now;
            cancel_label = tt.u3_later;
            break;
        }
        case DialogKind::None: return;
        default: {
            ToolDialog d;
            d.cancel = s.cancel;
            if (!ToolDialogText(d)) {
                m_dialog.kind = DialogKind::None;
                return;
            }
            title = d.title;
            body = d.body;
            note = d.note;
            primary = d.primary;
            error = d.error;
            cancel_label = d.cancel;
            can = d.can;
            m_dialog.single = d.single;
            verify_bar = d.bar;
            verify_running = d.running;
            verify_frac = d.frac;
            verify_text = d.bar_text;
            for (const auto& l : d.lines) lines.push_back({l.text, "", l.right, l.warn ? t.warn : t.text, l.warn ? 2 : 0});
            break;
        }
    }

    // the layout, from the top
    const float w = 740, pad = 32, iw = w - 2 * pad;
    const float tick_h = (tick ? 84.f + 14 : 0.f) + (tick2 ? 84.f + 14 : 0.f);
    const int shown = std::min<int>(int(lines.size()), m_dialog.kind == DialogKind::HistoryInfo ? 8 : 5);
    const bool more = int(lines.size()) > shown;
    // rows a finger chooses are 48 px high; the others 40
    const float lp = std::any_of(lines.begin(), lines.end(), [](const Line& l) { return l.hit >= 0; }) ? 48.f : 40.f;
    float h = pad + 40;
    const float body_h = body.empty() ? 0 : BoxHeight(vg, iw, 17, body.c_str()) + 16;
    const float note_h = note.empty() ? 0 : BoxHeight(vg, iw, 15, note.c_str()) + 14;
    const float list_h = lines.empty() ? 0 : shown * lp + (more ? 30 : 0) + 12 + 16;
    const float err_h = error.empty() ? 0 : ErrorCard(vg, t, 0, 0, iw, error, false) + 14;
    h += body_h + list_h + (bar ? 64 : 0) + (verify_bar ? 54 : 0) + err_h + note_h + tick_h + 60 + pad - 8;
    const float x = (kWidth - w) / 2, y = std::max(80.f, (kHeight - 56 - h) / 2);
    // it grows out of the middle with a soft spring
    const float pop = ui::EaseOutBack(ui::Appear(m_dialog_at, 0.32f));
    nvgSave(vg);
    nvgTranslate(vg, x + w / 2, y + h / 2);
    nvgScale(vg, 0.88f + 0.12f * pop, 0.88f + 0.12f * pop);
    nvgTranslate(vg, -(x + w / 2), -(y + h / 2));
    nvgGlobalAlpha(vg, std::min(1.f, pop * 1.4f));
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x - 10, y + 6, w + 20, h + 20, 22);
    nvgFillPaint(vg, nvgBoxGradient(vg, x, y + 10, w, h, 18, 40, nvgRGBAf(0, 0, 0, 0.5f), nvgRGBAf(0, 0, 0, 0)));
    nvgFill(vg);
    draw::Popup(vg, t, x, y, w, h, 18); // nearly solid: what is behind does not show through
    // a tap on the panel itself does nothing
    AddHit(x, y, w, h, HitKind::Button);

    float cy = y + pad;
    if (ui::TextWidth(vg, 26, title.c_str()) > iw) {
        // cut with an ellipsis, on a whole character
        const std::string dots = "\xE2\x80\xA6";
        while (!title.empty() && ui::TextWidth(vg, 26, (title + dots).c_str()) > iw) {
            size_t cut = title.size() - 1;
            while (cut > 0 && (static_cast<unsigned char>(title[cut]) & 0xC0) == 0x80) cut--;
            title.erase(cut);
        }
        title += dots;
    }
    nvgSave(vg);
    nvgScissor(vg, x + pad, cy - 4, iw, 44);
    ui::Text(vg, x + pad, cy + 16, 26, t.text, title.c_str());
    nvgRestore(vg);
    cy += 44;
    if (!body.empty()) {
        TextBox(vg, x + pad, cy, iw, 17, t.muted, body.c_str());
        cy += body_h;
    }
    if (!lines.empty()) {
        const float lh = shown * lp + (more ? 30 : 0) + 12;
        ui::FillRound(vg, x + pad, cy, iw, lh, 12, t.panel2);
        ui::StrokeRound(vg, x + pad + .5f, cy + .5f, iw - 1, lh - 1, 12, t.line, 1);
        for (int i = 0; i < shown; i++) {
            const Line& l = lines[i];
            const float ly = cy + 6 + i * lp + lp / 2;
            if (l.focus) {
                ui::FillRound(vg, x + pad + 4, ly - lp / 2 + 2, iw - 8, lp - 4, 8, t.panel);
                ui::StrokeRound(vg, x + pad + 4, ly - lp / 2 + 2, iw - 8, lp - 4, 8, t.work, 2.5f);
            }
            if (l.hit >= 0) AddHit(x + pad, ly - lp / 2, iw, lp, HitKind::DialogLine, l.hit);
            if (i > 0) {
                nvgBeginPath(vg);
                nvgRect(vg, x + pad + 14, ly - lp / 2, iw - 28, 1);
                nvgFillColor(vg, t.line);
                nvgFill(vg);
            }
            const float rw = ui::TextWidth(vg, 15, l.right.c_str());
            float tx = x + pad + 16;
            if (l.mark) {
                // a filled tick, or a warning triangle in amber
                const float mx = tx + 10;
                if (l.mark == 1) {
                    nvgBeginPath(vg);
                    nvgCircle(vg, mx, ly, 10);
                    nvgFillColor(vg, t.ok);
                    nvgFill(vg);
                    ui::CheckMark(vg, mx, ly, 10, t.panel, 2.2f);
                } else {
                    nvgBeginPath(vg);
                    nvgMoveTo(vg, mx, ly - 10);
                    nvgLineTo(vg, mx + 11, ly + 9);
                    nvgLineTo(vg, mx - 11, ly + 9);
                    nvgClosePath(vg);
                    nvgFillColor(vg, t.warn);
                    nvgFill(vg);
                    ui::Text(vg, mx, ly + 2, 13, t.panel, "!", NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
                }
                tx += 30;
            }
            if (l.tick >= 0) {
                draw::TickBox(vg, t, tx, ly, l.tick > 0, "dialog-tick:" + std::to_string(l.hit));
                tx += 36;
            }
            nvgSave(vg);
            nvgScissor(vg, tx, ly - lp / 2, iw - 32 - rw - 140, lp);
            tx = ui::Text(vg, tx, ly, 17, m_dialog.kind == DialogKind::HistoryInfo ? t.muted : t.text, l.text.c_str());
            const bool colored = m_dialog.kind == DialogKind::HistoryInfo || m_dialog.kind == DialogKind::Verify ||
                                 m_dialog.kind == DialogKind::PreCheck || m_dialog.kind == DialogKind::TicketsDelete;
            nvgRestore(vg);
            if (!l.tag.empty()) {
                const float tw = ui::TextWidth(vg, 13, l.tag.c_str()) + 16;
                const float tagx = std::min(tx + 12, x + pad + iw - 16 - rw - 16 - tw);
                ui::StrokeRound(vg, tagx, ly - 11, tw, 22, 6, t.line, 1);
                ui::Text(vg, tagx + tw / 2, ly, 13, t.muted, l.tag.c_str(), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            }
            ui::Text(vg, x + pad + iw - 16, ly, 15, colored ? l.color : t.muted, l.right.c_str(),
                     NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        }
        if (more) {
            char text[48];
            std::snprintf(text, sizeof(text), s.more_count, int(lines.size()) - shown);
            ui::Text(vg, x + pad + 16, cy + 6 + shown * lp + 14, 15, t.muted, text);
        }
        cy += lh + 16;
    }
    if (bar) {
        // the internal memory: used, this install, free after
        const float bw = iw, bh = 18;
        ui::FillRound(vg, x + pad, cy, bw, bh, 9, t.panel2);
        if (m_nand_total_bytes > 0 && m_nand_free_bytes >= 0) {
            const double total = double(m_nand_total_bytes);
            const double used = std::max(0.0, total - double(m_nand_free_bytes));
            const float uw = float(used / total) * bw;
            const float tw = std::min(bw - uw, float(double(size) / total) * bw);
            nvgSave(vg);
            nvgIntersectScissor(vg, x + pad, cy, bw, bh);
            NVGcolor used_c = t.muted;
            used_c.a = 0.45f;
            ui::FillRound(vg, x + pad, cy, uw + 9, bh, 9, used_c);
            nvgBeginPath(vg);
            nvgRect(vg, x + pad + uw, cy, tw, bh);
            nvgFillColor(vg, can ? t.work : t.err);
            nvgFill(vg);
            nvgRestore(vg);
            ui::StrokeRound(vg, x + pad + .5f, cy + .5f, bw - 1, bh - 1, 9, t.line, 1);
            char a[64], b[64], c[96];
            std::snprintf(a, sizeof(a), s.used_fmt, SizeText(u64(used)).c_str());
            std::snprintf(b, sizeof(b), s.will_take_fmt, SizeText(size).c_str());
            const u64 after = u64(m_nand_free_bytes) > size ? u64(m_nand_free_bytes) - size : 0;
            std::snprintf(c, sizeof(c), s.free_after_fmt, SizeText(after).c_str(), SizeText(u64(m_nand_total_bytes)).c_str());
            float lx = x + pad;
            const float ly = cy + bh + 20;
            ui::FillRound(vg, lx, ly - 6, 12, 12, 3, used_c);
            lx = ui::Text(vg, lx + 18, ly, 15, t.muted, a) + 22;
            ui::FillRound(vg, lx, ly - 6, 12, 12, 3, can ? t.work : t.err);
            lx = ui::Text(vg, lx + 18, ly, 15, t.muted, b) + 22;
            ui::FillRound(vg, lx, ly - 6, 12, 12, 3, t.panel2);
            ui::StrokeRound(vg, lx + .5f, ly - 5.5f, 11, 11, 3, t.line, 1);
            ui::Text(vg, lx + 18, ly, 15, t.muted, c);
        } else {
            ui::Text(vg, x + pad, cy + bh / 2, 15, t.muted, TrM().pc_unknown);
        }
        cy += 64;
    }
    if (verify_bar) {
        ui::Progress(vg, t, x + pad, cy + 4, iw, 10, verify_running ? verify_frac : 1.f, t.work, m_time, verify_running);
        ui::Text(vg, x + pad, cy + 34, 15, t.muted, verify_text.c_str());
        cy += 54;
    }
    if (!error.empty()) {
        ErrorCard(vg, t, x + pad, cy, iw, error, true);
        cy += err_h;
    }
    if (!note.empty()) {
        TextBox(vg, x + pad, cy, iw, 15, t.muted, note.c_str());
        cy += note_h;
    }
    if (tick) {
        // a big box to tick, on by default: the saves copied before (3A); a tap or Y changes it
        const bool on = m_dialog.keep_saves;
        const More& m = TrM();
        const float th = 84;
        NVGcolor fill = t.work;
        fill.a = on ? 0.12f : 0.04f;
        ui::FillRound(vg, x + pad, cy, iw, th, 14, fill);
        NVGcolor edge = t.work;
        edge.a = 0.6f;
        ui::StrokeRound(vg, x + pad + 1, cy + 1, iw - 2, th - 2, 14, on ? edge : t.line, 2);
        draw::TickBox(vg, t, x + pad + 22, cy + th / 2, on, "keep-saves");
        ui::Text(vg, x + pad + 62, cy + th / 2 - 13, 18, t.text, m.ic_rm_keep);
        ui::Text(vg, x + pad + 62, cy + th / 2 + 14, 14, t.muted, m_link.CurrentConfig().Linked() ? m.ic_rm_keep_lib : m.ic_rm_keep_sd);
        ui::Symbol(vg, t, x + pad + iw - 30, cy + th / 2, 24, ui::glyph::Y, t.muted, HidNpadButton_Y);
        AddHit(x + pad, cy, iw, th, HitKind::DialogTick);
        cy += th + 14;
    }
    if (tick2) {
        // a second box, off by default: its mods and its tickets go too (7A); a tap or X changes it
        const bool on = m_dialog.remove_extra;
        const More& m = TrM();
        const float th = 84;
        // it comes in a little after the question
        const float in = ui::EaseOut(ui::Appear(m_dialog_at + 0.3, 0.3f));
        nvgSave(vg);
        nvgTranslate(vg, 0, (1 - in) * 10);
        nvgGlobalAlpha(vg, in * std::min(1.f, pop * 1.4f));
        NVGcolor fill = t.work;
        fill.a = on ? 0.12f : 0.04f;
        ui::FillRound(vg, x + pad, cy, iw, th, 14, fill);
        NVGcolor edge = t.work;
        edge.a = 0.6f;
        ui::StrokeRound(vg, x + pad + 1, cy + 1, iw - 2, th - 2, 14, on ? edge : t.line, 2);
        draw::TickBox(vg, t, x + pad + 22, cy + th / 2, on, "remove-extra");
        ui::Text(vg, x + pad + 62, cy + th / 2 - 13, 18, t.text, m.rx_title);
        // what was found: "Mods: 2 folders · common ticket"
        std::string found;
        char text[96];
        if (m_dialog.extra_mods > 0) {
            if (m_dialog.extra_mods == 1) std::snprintf(text, sizeof(text), "%s", m.rx_mods_one);
            else std::snprintf(text, sizeof(text), m.rx_mods_fmt, m_dialog.extra_mods);
            found = text;
        }
        const int tickets = m_dialog.extra_common + m_dialog.extra_personal;
        if (tickets > 0) {
            if (tickets == 1) std::snprintf(text, sizeof(text), "%s", m_dialog.extra_personal ? m.rx_ticket_personal : m.rx_ticket_common);
            else std::snprintf(text, sizeof(text), m.rx_tickets_fmt, tickets);
            found += (found.empty() ? "" : " \xC2\xB7 ") + std::string(text);
        }
        ui::Text(vg, x + pad + 62, cy + th / 2 + 14, 14, t.muted, found.c_str());
        ui::Symbol(vg, t, x + pad + iw - 30, cy + th / 2, 24, ui::glyph::X, t.muted, HidNpadButton_X);
        nvgRestore(vg);
        AddHit(x + pad, cy, iw, th, HitKind::DialogTickExtra);
        cy += th + 14;
    }

    // the buttons: Cancel (or Close) first in focus, the action on the right
    const bool info = m_dialog.single;
    if ((!can || info) && m_dialog.focus == 1) m_dialog.focus = 0;
    const float bh = 52, by = y + h - pad - bh + 8;
    float bx = x + w - pad;
    struct Button { const char* glyph; std::string label; bool primary; int index; u64 mask; };
    std::vector<Button> buttons;
    buttons.push_back({ui::glyph::B, info ? s.close : cancel_label, false, 0, HidNpadButton_B});
    u64 extra_mask = 0;
    const int extra = info ? -1 : DialogExtra(extra_mask);
    if (extra >= 0) {
        // a third choice, between the two: the internal memory (Where), the SD card instead (Nand),
        // leaving some out (DlcNeeds, Reinstall), discarding (Resume)
        const More& m = TrM();
        const char* label = m_dialog.kind == DialogKind::ContentDetails ? TrT().cd_ticket_go
                            : m_dialog.kind == DialogKind::PlayRandom ? m.rg_another
                            : m_dialog.kind == DialogKind::Where     ? s.internal
                            : m_dialog.kind == DialogKind::Nand      ? m.nand_sd_button
                            : m_dialog.kind == DialogKind::DlcNeeds  ? m.dn_skip
                            : m_dialog.kind == DialogKind::Reinstall ? m.ri_skip
                                                                     : m.rs_discard;
        buttons.push_back({extra_mask == HidNpadButton_Y ? ui::glyph::Y : ui::glyph::X, label, false, extra, extra_mask});
    }
    if (!info) buttons.push_back({ui::glyph::A, primary, true, 1, HidNpadButton_A});
    // three long labels that do not fit get a smaller size
    float all = 14 * float(buttons.size() - 1);
    for (const auto& b : buttons) all += ui::TextWidth(vg, 18, b.label.c_str()) + 78;
    const bool small = all > iw;
    for (auto it = buttons.rbegin(); it != buttons.rend(); ++it) {
        const float bw = small ? ui::TextWidth(vg, 15, it->label.c_str()) + 62 : ui::TextWidth(vg, 18, it->label.c_str()) + 78;
        bx -= bw;
        const bool enabled = !it->primary || can;
        nvgGlobalAlpha(vg, (enabled ? 1.f : 0.4f) * std::min(1.f, pop * 1.4f));
        if (m_dialog.focus == it->index) {
            ui::Focus(vg, t, bx, by, bw, bh, 12, m_time);
        }
        if (it->primary) {
            ui::FillRound(vg, bx, by, bw, bh, 12, t.work);
        } else {
            ui::FillRound(vg, bx, by, bw, bh, 12, t.panel2);
            ui::StrokeRound(vg, bx + 1, by + 1, bw - 2, bh - 2, 12, t.line, 2);
        }
        const NVGcolor fg = it->primary ? t.on_work : t.text;
        ui::Symbol(vg, t, bx + (small ? 27 : 34), by + bh / 2, small ? 21 : 24, it->glyph, fg, it->mask);
        ui::Text(vg, bx + (small ? 45 : 56), by + bh / 2, small ? 15 : 18, fg, it->label.c_str());
        nvgGlobalAlpha(vg, 1.f);
        if (enabled) {
            AddHit(bx, by, bw, bh, HitKind::DialogButton, it->index);
        }
        bx -= 14;
    }

    nvgGlobalAlpha(vg, 1.f);
    nvgRestore(vg);
    // its own hint bar, over the one of the screen
    std::vector<ui::HitRect> rects;
    ui::HintBar(vg, t, nullptr, {{ui::glyph::A, s.choose, HidNpadButton_A}, {ui::glyph::B, info ? s.close : cancel_label.c_str(), HidNpadButton_B}}, rects);
    AddHints(rects);
}

void App::UpdateDialog(u64 down, u64 held) {
    if (m_dialog.kind == DialogKind::Usb3) {
        UpdateUsb3Sheet(down, held);
        return;
    }
    if (ToolDialogInput(down, held)) return;
    if (m_dialog.kind == DialogKind::ConsoleRemove && (down & HidNpadButton_Y) && ConsoleRemoveTakesGame()) {
        m_dialog.keep_saves = !m_dialog.keep_saves; // the box of the saves (3A)
        return;
    }
    if (m_dialog.kind == DialogKind::ConsoleRemove && (down & HidNpadButton_X) && ConsoleRemoveTakesGame() &&
        (m_dialog.extra_mods > 0 || m_dialog.extra_common + m_dialog.extra_personal > 0)) {
        m_dialog.remove_extra = !m_dialog.remove_extra; // the box of its mods and its tickets (7A)
        return;
    }
    const bool info = m_dialog.single;
    if (m_dialog.kind == DialogKind::Verify && (down & (HidNpadButton_B | HidNpadButton_A)) && m_queue->GetVerify().running) {
        // stops the check; the question closes
        m_queue->CancelVerify();
        m_dialog.kind = DialogKind::None;
        return;
    }
    if (m_dialog.kind == DialogKind::UpdateHere && !m_dialog.jobs.empty()) {
        // the list of 15A: up and down move, Y ticks or unticks, A adds what is ticked
        const int n = int(m_dialog.jobs.size());
        m_dialog.marks.resize(size_t(n), 1);
        if (Repeat(down, held, HidNpadButton_AnyUp) && m_dialog.entry > 0) m_dialog.entry--;
        if (Repeat(down, held, HidNpadButton_AnyDown) && m_dialog.entry < n - 1) m_dialog.entry++;
        if (down & HidNpadButton_Y) {
            auto& mark = m_dialog.marks[size_t(std::clamp(m_dialog.entry, 0, n - 1))];
            mark = !mark;
            return;
        }
    }
    if (m_dialog.kind == DialogKind::Versions) {
        const Game& g = m_games[std::clamp(m_dialog.game, 0, int(m_games.size()) - 1)];
        const int n = int(VersionRows(g).size());
        if (Repeat(down, held, HidNpadButton_AnyUp) && m_dialog.entry > 0) {
            m_dialog.entry--;
            m_dialog.focus = 1;
        }
        if (Repeat(down, held, HidNpadButton_AnyDown) && m_dialog.entry < n - 1) {
            m_dialog.entry++;
            m_dialog.focus = 1;
        }
    }
    u64 extra_mask = 0;
    const int extra = info ? -1 : DialogExtra(extra_mask);
    if (extra >= 0) {
        // Cancel, the third choice, the action, from left to right
        const int order[] = {0, extra, 1};
        int pos = m_dialog.focus == extra ? 1 : m_dialog.focus == 1 ? 2 : 0;
        if (Repeat(down, held, HidNpadButton_AnyLeft) && pos > 0) pos--;
        if (Repeat(down, held, HidNpadButton_AnyRight) && pos < 2) pos++;
        m_dialog.focus = order[pos];
        if ((down & extra_mask) || ((down & HidNpadButton_A) && m_dialog.focus == extra)) {
            DialogExtraChosen();
            return;
        }
    } else if (!info) {
        if (Repeat(down, held, HidNpadButton_AnyLeft)) m_dialog.focus = 0;
        if (Repeat(down, held, HidNpadButton_AnyRight)) m_dialog.focus = 1; // a disabled action falls back when drawn
    }
    if (m_dialog.kind == DialogKind::Preflight && (down & HidNpadButton_B)) {
        // not waited for: what it reads is dropped
        m_preflight_dropped = true;
        m_dialog = Dialog{};
        return;
    }
    const bool declined = (down & HidNpadButton_B) || ((down & HidNpadButton_A) && m_dialog.focus == 0);
    if (declined) {
        if (m_dialog.kind == DialogKind::Resume) {
            // decided later: the journal stays, and it is asked again the next time NXDock opens
            m_resume.reset();
        } else if (m_dialog.kind == DialogKind::ResumeDiscard) {
            // back to the question of going on
            m_dialog.kind = DialogKind::Resume;
            m_dialog.focus = 1;
            return;
        } else if (m_dialog.kind == DialogKind::SaveCopyAsk || m_dialog.kind == DialogKind::ModsAsk) {
            m_queue->Answer(false);
        } else if (m_dialog.kind == DialogKind::Usb3Reboot) {
            // later: the chip stays orange until the console reboots
            NoticeOk(m_usb3_written_on ? TrT().u3_toast : TrT().u3_toast_off);
        } else if (m_dialog.kind == DialogKind::Push) {
            for (const auto& id : m_dialog.push) m_push->Answer(id, false);
        } else if (m_dialog.kind == DialogKind::Share) {
            m_prefs.share_installed = 0;
            m_prefs.Save();
        } else if (m_dialog.kind == DialogKind::UpdateStops) {
            // those updates stay out; the rest goes on
            const auto checks = m_dialog.checks;
            std::vector<InstallJob> rest;
            for (size_t i = 0; i < checks.size(); i++) {
                if (!StopsTheGame(checks[i].over)) rest.push_back(std::move(m_dialog.jobs[i]));
            }
            m_dialog = Dialog{};
            if (!rest.empty()) AddJobs(std::move(rest));
            return;
        }
        m_dialog.kind = DialogKind::None;
        return;
    }
    if (down & HidNpadButton_A) {
        const DialogKind before = m_dialog.kind;
        if (m_dialog.focus == 1 && !info) {
            ConfirmDialog();
        }
        if (m_dialog.kind == before) {
            m_dialog.kind = DialogKind::None; // unless the choice led to another question
        }
    }
}

int App::DialogExtra(u64& mask) const {
    switch (m_dialog.kind) {
        case DialogKind::Where: mask = HidNpadButton_X; return 2;
        case DialogKind::Nand: mask = HidNpadButton_Y; return 3;
        case DialogKind::DlcNeeds:
        case DialogKind::Reinstall:
        case DialogKind::Resume: mask = HidNpadButton_X; return 2;
        case DialogKind::ContentDetails: mask = HidNpadButton_X; return 2;
        case DialogKind::PlayRandom: mask = HidNpadButton_Y; return 2; // another one (21A)
        default: mask = 0; return -1;
    }
}

void App::DialogExtraChosen() {
    if (m_dialog.kind == DialogKind::PlayRandom) {
        m_dialog = Dialog{};
        RollRandom(); // the dice again
        return;
    }
    if (m_dialog.kind == DialogKind::ContentDetails) {
        ConsoleTicketBack();
        return;
    }
    if (m_dialog.kind == DialogKind::Resume) {
        // discarding is asked once more, Back in focus
        m_dialog.kind = DialogKind::ResumeDiscard;
        m_dialog.focus = 0;
        return;
    }
    if (m_dialog.kind == DialogKind::Where) {
        auto jobs = std::move(m_dialog.jobs);
        for (auto& j : jobs) j.nand = true;
        m_dialog = Dialog{};
        m_dialog.kind = DialogKind::Nand; // never to the internal memory without its question
        m_dialog.jobs = std::move(jobs);
        return;
    }
    if (m_dialog.kind == DialogKind::Nand) {
        // to the SD card instead, the default
        auto jobs = std::move(m_dialog.jobs);
        for (auto& j : jobs) j.nand = false;
        m_dialog = Dialog{};
        AddJobs(std::move(jobs));
        return;
    }
    if (m_dialog.kind == DialogKind::DlcNeeds) {
        // those DLC stay out; the rest goes on
        std::vector<InstallJob> rest;
        const auto installed = m_queue ? m_queue->Installed() : std::map<u64, InstalledState>{};
        for (auto& j : m_dialog.jobs) {
            u64 id = j.title_id;
            u32 v = 0;
            if (j.origin != InstallJob::Origin::Library) TitleFromName(j.game + " " + j.label, id, v);
            bool behind = false;
            if (j.dlc_needs && id && TitleKind(id) == 2) {
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
                behind = j.dlc_needs > have;
            }
            if (!behind) {
                j.dlc_ok = true;
                rest.push_back(std::move(j));
            }
        }
        m_dialog = Dialog{};
        if (!rest.empty()) AddJobs(std::move(rest));
        return;
    }
    if (m_dialog.kind == DialogKind::Reinstall) {
        // skipped: those installed stay out, the rest goes on
        std::vector<InstallJob> rest;
        for (auto& j : m_dialog.jobs) {
            if (j.reinstall_ok) rest.push_back(std::move(j));
        }
        m_dialog = Dialog{};
        if (!rest.empty()) AddJobs(std::move(rest));
        return;
    }
}

void App::ConfirmDialog() {
    switch (m_dialog.kind) {
        case DialogKind::Nand: {
            // checked again at the moment of the choice: nothing goes there when it does not fit
            u64 size = 0;
            for (const auto& j : m_dialog.jobs) size += j.size;
            if (m_nand_free_bytes >= 0 && size + kNandMargin > u64(m_nand_free_bytes)) {
                return;
            }
            AddJobs(std::move(m_dialog.jobs));
            break;
        }
        case DialogKind::UpdateAll:
            AddJobs(std::move(m_dialog.jobs));
            break;
        case DialogKind::SaveCopyAsk:
            m_queue->Answer(true);
            break;
        case DialogKind::Usb3Reboot: RebootWhenIdle(); break; // waits for the queue
        case DialogKind::Versions: {
            const Game& g = m_games[std::clamp(m_dialog.game, 0, int(m_games.size()) - 1)];
            const auto rows = VersionRows(g);
            if (rows.empty()) break;
            const CatalogFile& f = g.files[rows[size_t(std::clamp(m_dialog.entry, 0, int(rows.size()) - 1))]];
            if (f.version == g.installed.patch) break;
            InstallJob job = JobFor(g, f);
            job.nand = g.installed.base_nand; // where the game is, as its update goes there too
            if (f.version > g.installed.patch) {
                AddJobs({job}); // a newer one: a plain update
                break;
            }
            // an older one: asked once more, with what it means for the save
            job.downgrade = true;
            const u32 now = g.installed.patch;
            OpenDialog(DialogKind::Downgrade);
            m_dialog.jobs = {job};
            m_dialog.size = now;
            return;
        }
        case DialogKind::Downgrade:
            AddJobs(std::move(m_dialog.jobs));
            break;
        case DialogKind::WebCancel:
        case DialogKind::QueueCancel:
            m_queue->Remove(m_dialog.entry); // a waiting one goes; the one installing is cancelled
            break;
        case DialogKind::Quit:
            m_quit = true;
            break;
        case DialogKind::Play:
            m_dialog = Dialog{};
            PlayConsoleGame(true);
            return;
        case DialogKind::PlayRandom: {
            // the game chosen opens as from its page (2A of group 2)
            const unsigned long long app = m_dialog.ids.empty() ? 0 : m_dialog.ids.front();
            m_dialog = Dialog{};
            if (!app) return;
            m_console_app = app;
            m_console_item = 0;
            m_console_btn = 0;
            m_console_marks.clear();
            PlayConsoleGame(true);
            return;
        }
        case DialogKind::OpenNro:
            m_dialog = Dialog{};
            OpenNro();
            return;
        case DialogKind::UpdateHere: {
            // what is ticked goes to the queue, to the SD card
            std::vector<InstallJob> jobs;
            for (size_t i = 0; i < m_dialog.jobs.size(); i++) {
                if (i < m_dialog.marks.size() && m_dialog.marks[i]) jobs.push_back(std::move(m_dialog.jobs[i]));
            }
            m_dialog = Dialog{};
            if (jobs.empty()) return;
            char text[160];
            std::snprintf(text, sizeof(text), TrM().uh_added_fmt, int(jobs.size()));
            AddJobs(std::move(jobs));
            if (m_dialog.kind == DialogKind::None) NoticeOk(text);
            return;
        }
        case DialogKind::TicketsDelete:
            // they wait while something installs: an install writes tickets
            if ((m_queue && (m_queue->Busy() || m_queue->Undoing())) || !m_space || !m_space->StartDeleteTickets(m_ticket_delete)) {
                Notice(TrM().cz_busy);
            } else {
                std::fill(m_ticket_marks.begin(), m_ticket_marks.end(), 0);
            }
            break;
        case DialogKind::Forget:
            m_speed.Cancel();
            m_link.Forget();
            m_tab = Tab::Library;
            break;
        case DialogKind::HomeAdd:
        case DialogKind::HomeRemove:
            DoHomeShortcut(m_dialog.kind == DialogKind::HomeRemove);
            break;
        case DialogKind::CompressIntro:
        case DialogKind::CompressGo:
        case DialogKind::CompressUndo:
        case DialogKind::CompressStopsOriginal:
        case DialogKind::CompressStopsRemove:
            ConfirmCompressDialog();
            break;
        case DialogKind::Resume:
            if (m_resume) AddJobs(ResumeJobs(*m_resume, m_link.CurrentConfig().key));
            m_resume.reset();
            break;
        case DialogKind::ResumeDiscard:
            // every placeholder of the journal goes, and the journal
            if (m_resume) DiscardResume(*m_resume);
            m_resume.reset();
            NoticeOk(TrM().rs_discarded);
            break;
        case DialogKind::ModsAsk:
            // the update goes on; when it ends, Cheats and mods opens for the new version
            m_mods_after = m_dialog.ids.empty() ? 0 : m_dialog.ids[0];
            m_mods_after_name = m_dialog.text;
            m_queue->Answer(true);
            break;
        case DialogKind::Where:
            for (auto& j : m_dialog.jobs) j.nand = false;
            AddJobs(std::move(m_dialog.jobs));
            break;
        case DialogKind::Push:
            for (const auto& id : m_dialog.push) m_push->Answer(id, true);
            AddJobs(std::move(m_dialog.jobs));
            break;
        case DialogKind::Risk:
            m_risk_ok = true;
            AddJobs(std::move(m_dialog.jobs));
            if (m_after_risk != MoreScreen::Tiles) {
                const MoreScreen screen = m_after_risk;
                m_after_risk = MoreScreen::Tiles;
                if (m_dialog.kind == DialogKind::Risk) m_dialog.kind = DialogKind::None;
                OpenMoreScreen(screen);
            }
            break;
        case DialogKind::MtpNand:
            MtpAllowNand(true);
            break;
        case DialogKind::SpaceClean:
        case DialogKind::SpaceArchive:
        case DialogKind::SpaceMove:
        case DialogKind::SpaceCard:
        case DialogKind::SpaceDump:
            ConfirmSpaceDialog();
            break;
        case DialogKind::UpdateStops: {
            // the original games first (the queue puts them before their updates); an update
            // whose game has no original in the library, or is in the game already, stays out
            auto originals = OriginalsFor(m_dialog.jobs);
            std::set<u64> have;
            for (const auto& o : originals) have.insert(o.title_id);
            const auto checks = UpdatesThatStop(m_dialog.jobs);
            std::vector<InstallJob> jobs = std::move(originals);
            for (size_t i = 0; i < checks.size(); i++) {
                if (StopsTheGame(checks[i].over) && !(OriginalHelps(checks[i].over) && have.count(checks[i].app))) continue;
                jobs.push_back(std::move(m_dialog.jobs[i]));
            }
            m_dialog = Dialog{};
            AddJobs(std::move(jobs));
            return;
        }
        case DialogKind::DlcNeeds: {
            auto jobs = std::move(m_dialog.jobs);
            for (auto& j : jobs) j.dlc_ok = true;
            m_dialog = Dialog{};
            AddJobs(std::move(jobs));
            return;
        }
        case DialogKind::Reinstall: {
            auto jobs = std::move(m_dialog.jobs);
            for (auto& j : jobs) j.reinstall_ok = true;
            m_dialog = Dialog{};
            AddJobs(std::move(jobs));
            return;
        }
        case DialogKind::ConsoleRemove: {
            // the saves first, when asked (3A): to the library when one is linked, else to the SD
            // card; a copy that fails stops the removal of that game, and nothing of it goes
            std::function<std::string(unsigned long long)> before;
            if (m_dialog.keep_saves && ConsoleRemoveTakesGame()) {
                const Config c = m_link.CurrentConfig();
                const std::string url = c.Linked() ? c.url : std::string{}, key = c.Linked() ? c.key : std::string{};
                const std::string note = TrM().sc_note_remove;
                std::map<unsigned long long, unsigned> versions; // the version installed, kept with the copy
                if (m_space) {
                    for (const auto& a : m_space->Snapshot().apps) {
                        for (const auto& ct : a.contents) {
                            if (ct.type == NcmContentMetaType_Patch) versions[a.app_id] = std::max(versions[a.app_id], unsigned(ct.version));
                        }
                    }
                }
                before = [url, key, note, versions](unsigned long long app) {
                    const auto it = versions.find(app);
                    return CopySavesBefore(url, key, app, it == versions.end() ? 0 : it->second, note, {});
                };
            }
            // then its mods and its tickets, when asked (7A); the tickets are saved to the SD card first
            std::function<void(unsigned long long)> after;
            if (m_dialog.remove_extra && ConsoleRemoveTakesGame()) after = RemoveGameExtras;
            // taking contents off while the queue installs could take those of the install going on
            if (m_queue && (m_queue->Busy() || m_queue->Undoing())) {
                Notice(TrM().cz_busy);
            } else if (!m_dialog.ids.empty()) {
                // several games, whole: what the console has of each
                const auto v = m_space ? m_space->Snapshot() : Space::View{};
                std::vector<std::pair<unsigned long long, std::vector<InstalledApp::Content>>> games;
                u64 bytes = 0;
                m_console_done.clear();
                for (const auto app : m_dialog.ids) {
                    for (const auto& a : v.apps) {
                        if (a.app_id != app || a.contents.empty()) continue;
                        games.push_back({app, a.contents});
                        bytes += a.Bytes();
                        HistoryEntry e;
                        e.when = std::time(nullptr);
                        e.game = a.name;
                        e.label = TrM().ic_whole_game;
                        e.size = a.Bytes();
                        e.nand = a.nand_bytes > 0 && a.sd_bytes == 0;
                        e.result = HistoryEntry::Result::Removed;
                        m_console_done.push_back(e);
                    }
                }
                char text[200];
                std::snprintf(text, sizeof(text), TrM().ic_done_many_fmt, int(games.size()), SizeText(bytes).c_str());
                m_console_done_text = text;
                if (m_space && m_space->StartRemove(games, before, after)) {
                    m_console_removing.clear();
                    for (const auto& g : games) m_console_removing.insert(g.first);
                    m_console_picked.clear();
                } else {
                    m_console_done.clear();
                    Notice(TrM().cz_busy);
                }
            } else if (m_space && m_space->StartRemove(m_console_app, m_console_remove, before, after)) {
                // one game, or a part of it
                u64 bytes = 0;
                bool base = false;
                m_console_done.clear();
                for (const auto& c : m_console_remove) {
                    bytes += c.bytes;
                    base = base || c.type == NcmContentMetaType_Application;
                    HistoryEntry e;
                    e.when = std::time(nullptr);
                    e.game = m_dialog.text;
                    e.label = ContentLabel(c, m_console_app);
                    e.size = c.bytes;
                    e.nand = c.nand;
                    e.result = HistoryEntry::Result::Removed;
                    m_console_done.push_back(e);
                }
                char text[300];
                std::snprintf(text, sizeof(text), TrM().ic_done_one_fmt, m_dialog.text.c_str(), SizeText(bytes).c_str());
                m_console_done_text = text;
                m_console_removing.clear();
                if (base) m_console_removing.insert(m_console_app);
                m_console_marks.clear();
            } else {
                Notice(TrM().cz_busy);
            }
            break;
        }
        case DialogKind::ShopRemove:
            if (m_dialog.entry >= 0 && m_dialog.entry < int(m_shops.size())) {
                m_shops.erase(m_shops.begin() + m_dialog.entry);
                SaveShops(ShopsPath(), m_shops);
            }
            break;
        case DialogKind::VaultRestore: {
            const Config c = m_link.CurrentConfig();
            if (!m_vault || m_dialog.game < 0 || m_dialog.game >= int(m_vault_saves.size())) break;
            const Vault::User user = m_vault_users.empty() ? Vault::User{} : m_vault_users[m_vault_user];
            if (!m_dialog.text.empty()) {
                // a copy of the SD card (9A)
                m_vault->StartRestoreSd(user, m_vault_kind, m_vault_saves[m_dialog.game].app_id, m_dialog.text, TrM().before_restore);
            } else if (c.Linked()) {
                m_vault->StartRestore(c.url, c.key, user, m_vault_saves[m_dialog.game].app_id, m_dialog.save, TrM().before_restore, m_vault_kind);
            }
            break;
        }
        case DialogKind::PreCheck:
            // the user goes on: what needs a newer firmware is installed all the same
            for (auto& j : m_dialog.jobs) j.firmware_ok = true;
            AddJobs(std::move(m_dialog.jobs));
            break;
        case DialogKind::Share:
            m_prefs.share_installed = 1;
            m_prefs.Save();
            m_inventory_sent = ~0u;
            break;
        case DialogKind::Verify: {
            std::vector<InstallJob> repairs;
            if (RepairJobs(m_queue->GetVerify(), repairs)) {
                AddJobs(std::move(repairs));
            }
            break;
        }
        case DialogKind::Undo:
            if (!m_queue->StartUndo(m_dialog.undo)) {
                Notice(Tr().undo_wait_queue);
            }
            break;
        case DialogKind::ClearHistory:
            History(HistoryPath()).Clear();
            m_history.clear();
            m_history_version = ~0u;
            m_installed_focus = 0;
            break;
        default: ConfirmToolDialog(); break;
    }
}

} // namespace nxdock
