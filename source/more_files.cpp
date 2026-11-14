// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The tools of the file list (1A to 4A of the visual test of the new tools, 11/11): every file or
// the games only, files and folders marked (Y, a range while Y is held, ZL for all), a menu of
// actions on − or a long press (install what is inside, copy, cut, paste, rename, a new folder,
// the properties, delete), a clipboard shown on top, the work of a copy, a move or a delete on a
// thread of its own with its progress, and the folders NXDock never deletes, renames or moves.
#include "app.hpp"

#include "draw.hpp"
#include "i18n.hpp"
#include "i18n_more.hpp"
#include "i18n_tools.hpp"
#include "usb_disks.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <sys/stat.h>
#include <sys/statvfs.h>

namespace nxdock {
namespace {

constexpr float kMenuW = 400, kMenuRow = 50;

bool AskName(const char* guide, const std::string& initial, std::string& out) {
    SwkbdConfig kbd;
    if (R_FAILED(swkbdCreate(&kbd, 0))) return false;
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetGuideText(&kbd, guide);
    swkbdConfigSetStringLenMax(&kbd, 255);
    swkbdConfigSetInitialText(&kbd, initial.c_str());
    char text[512] = {};
    const Result rc = swkbdShow(&kbd, text, sizeof(text));
    swkbdClose(&kbd);
    if (R_FAILED(rc) || !text[0]) return false;
    out = text;
    return true;
}

// The free bytes of the device of a path; -1 when it cannot be read.
long long FreeOn(const std::string& path) {
    struct statvfs st{};
    const std::string dev = fileops::DeviceOf(path) + "/";
    if (statvfs(dev.c_str(), &st) != 0) return -1;
    return (long long)st.f_bavail * (long long)st.f_frsize;
}

// A short time, as "4 min" or "35 s".
std::string Left(double seconds) {
    char text[32];
    if (seconds >= 90) std::snprintf(text, sizeof(text), "%d min", int(seconds / 60 + 0.5));
    else std::snprintf(text, sizeof(text), "%d s", std::max(1, int(seconds)));
    return text;
}

// The pictures of the menu, in the blue of work, by the action (App::FilesAction): an arrow down,
// a list, two sheets, scissors, a clipboard, a cross, a text cursor, a folder with a plus, an i, a bin.
void ActionIcon(NVGcontext* vg, NVGcolor c, int action, float x, float cy) {
    nvgStrokeColor(vg, c);
    nvgFillColor(vg, c);
    nvgStrokeWidth(vg, 2.2f);
    nvgLineCap(vg, NVG_ROUND);
    switch (action) {
        case 0: // install
            nvgBeginPath(vg);
            nvgMoveTo(vg, x + 10, cy - 9);
            nvgLineTo(vg, x + 10, cy + 5);
            nvgMoveTo(vg, x + 4, cy);
            nvgLineTo(vg, x + 10, cy + 6);
            nvgLineTo(vg, x + 16, cy);
            nvgMoveTo(vg, x + 2, cy + 10);
            nvgLineTo(vg, x + 18, cy + 10);
            nvgStroke(vg);
            break;
        case 1: // the view
            ui::StrokeRound(vg, x + 1, cy - 9, 18, 18, 3, c, 2.2f);
            nvgBeginPath(vg);
            nvgMoveTo(vg, x + 5, cy - 2);
            nvgLineTo(vg, x + 15, cy - 2);
            nvgMoveTo(vg, x + 5, cy + 3);
            nvgLineTo(vg, x + 15, cy + 3);
            nvgStroke(vg);
            break;
        case 2: // copy
            ui::StrokeRound(vg, x + 6, cy - 6, 12, 15, 2, c, 2.2f);
            ui::StrokeRound(vg, x + 1, cy - 10, 12, 15, 2, c, 2.2f);
            break;
        case 3: // cut
            nvgBeginPath(vg);
            nvgCircle(vg, x + 5, cy + 6, 4);
            nvgCircle(vg, x + 15, cy + 6, 4);
            nvgMoveTo(vg, x + 7, cy + 3);
            nvgLineTo(vg, x + 16, cy - 10);
            nvgMoveTo(vg, x + 13, cy + 3);
            nvgLineTo(vg, x + 4, cy - 10);
            nvgStroke(vg);
            break;
        case 4: // paste
            ui::StrokeRound(vg, x + 2, cy - 8, 16, 19, 3, c, 2.2f);
            ui::FillRound(vg, x + 6, cy - 11, 8, 5, 2, c);
            break;
        case 5: // forget
            nvgBeginPath(vg);
            nvgMoveTo(vg, x + 4, cy - 6);
            nvgLineTo(vg, x + 16, cy + 6);
            nvgMoveTo(vg, x + 16, cy - 6);
            nvgLineTo(vg, x + 4, cy + 6);
            nvgStroke(vg);
            break;
        case 6: // rename
            nvgBeginPath(vg);
            nvgMoveTo(vg, x + 10, cy - 9);
            nvgLineTo(vg, x + 10, cy + 9);
            nvgMoveTo(vg, x + 6, cy - 9);
            nvgLineTo(vg, x + 14, cy - 9);
            nvgMoveTo(vg, x + 6, cy + 9);
            nvgLineTo(vg, x + 14, cy + 9);
            nvgStroke(vg);
            break;
        case 7: // a new folder
            nvgBeginPath(vg);
            nvgMoveTo(vg, x, cy - 7);
            nvgLineTo(vg, x + 7, cy - 7);
            nvgLineTo(vg, x + 9, cy - 4);
            nvgLineTo(vg, x + 20, cy - 4);
            nvgLineTo(vg, x + 20, cy + 8);
            nvgLineTo(vg, x, cy + 8);
            nvgClosePath(vg);
            nvgStroke(vg);
            nvgBeginPath(vg);
            nvgMoveTo(vg, x + 10, cy - 1);
            nvgLineTo(vg, x + 10, cy + 6);
            nvgMoveTo(vg, x + 6.5f, cy + 2.5f);
            nvgLineTo(vg, x + 13.5f, cy + 2.5f);
            nvgStroke(vg);
            break;
        case 8: // properties
            nvgBeginPath(vg);
            nvgCircle(vg, x + 10, cy, 9);
            nvgStroke(vg);
            ui::FillRound(vg, x + 9, cy - 1, 2.2f, 6, 1, c);
            ui::FillRound(vg, x + 9, cy - 5.5f, 2.2f, 2.2f, 1, c);
            break;
        default: // delete: a bin
            nvgBeginPath(vg);
            nvgMoveTo(vg, x + 1, cy - 6);
            nvgLineTo(vg, x + 19, cy - 6);
            nvgMoveTo(vg, x + 7, cy - 6);
            nvgLineTo(vg, x + 8, cy - 9);
            nvgLineTo(vg, x + 12, cy - 9);
            nvgLineTo(vg, x + 13, cy - 6);
            nvgMoveTo(vg, x + 4, cy - 4);
            nvgLineTo(vg, x + 5, cy + 9);
            nvgLineTo(vg, x + 15, cy + 9);
            nvgLineTo(vg, x + 16, cy - 4);
            nvgStroke(vg);
            break;
    }
}

bool IsFolder(const std::string& path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

} // namespace

// ---- what is chosen ----

std::vector<int> App::FilesChosen() const {
    std::vector<int> out;
    for (size_t i = 0; i < m_entries.size() && i < m_marked.size(); i++) {
        if (m_marked[i] && m_entries[i].name != "..") out.push_back(int(i));
    }
    if (out.empty() && m_file_focus >= 0 && m_file_focus < int(m_entries.size()) && m_entries[size_t(m_file_focus)].name != "..") {
        out.push_back(m_file_focus);
    }
    return out;
}

std::vector<std::string> App::FilesChosenPaths() const {
    std::vector<std::string> out;
    for (const int i : FilesChosen()) {
        const auto& e = m_entries[size_t(i)];
        if (!IsFolder(e.path) && e.parts.size() > 1) {
            // name.nsp.00, .01…: every part
            out.insert(out.end(), e.parts.begin(), e.parts.end());
        } else {
            out.push_back(e.path);
        }
    }
    return out;
}

void App::ReopenFolder(const std::string& focus_name) {
    const std::string keep = !focus_name.empty() ? focus_name
                             : m_file_focus >= 0 && m_file_focus < int(m_entries.size()) ? m_entries[size_t(m_file_focus)].name
                                                                                          : std::string{};
    const int first = m_file_first;
    OpenFolder(m_folder, m_folder_root);
    for (size_t i = 0; i < m_entries.size(); i++) {
        if (m_entries[i].name == keep) {
            m_file_focus = int(i);
            break;
        }
    }
    m_file_first = std::min(first, m_file_focus);
    m_list_at = -100; // no entrance again
}

void App::FilesMarkMove(int from, int to) {
    if (m_mark_anchor < 0 || from == to) return;
    const int a = std::min(m_mark_anchor, to), b = std::max(m_mark_anchor, to);
    for (int i = a; i <= b && i < int(m_marked.size()); i++) {
        if (m_entries[size_t(i)].name != "..") m_marked[size_t(i)] = m_mark_value;
    }
}

// ---- the menu of actions (1A) ----

void App::OpenFilesMenu() {
    const auto paths = FilesChosenPaths();
    m_files_found = fileops::GamesUnder(paths);
    // a game file chosen on its own counts too
    for (const int i : FilesChosen()) {
        const auto& e = m_entries[size_t(i)];
        if (!e.dir && !e.format.empty()) m_files_found.push_back({e.name, e.parts, e.size, e.format});
    }
    m_files_count = fileops::CountAll(paths);
    m_files_menu = true;
    m_files_menu_focus = 0;
    m_list_at = m_time; // its rows come in
}

std::vector<App::FilesRow> App::FilesMenuRows() {
    const Tools& tt = TrT();
    std::vector<FilesRow> rows;
    const auto chosen = FilesChosen();
    const auto paths = FilesChosenPaths();
    const fileops::Guard guard = fileops::GuardOfAll(paths);
    const bool locked = guard == fileops::Guard::System || guard == fileops::Guard::Self;
    // a folder where things can be written: not inside Nintendo/, emuMMC/ or bootloader/
    const bool writable = fileops::GuardOf(fileops::Join(m_folder, "x")) != fileops::Guard::System;
    if (!m_files_found.empty()) {
        char text[48];
        std::snprintf(text, sizeof(text), tt.fm_games_fmt, int(m_files_found.size()));
        rows.push_back({kFaInstall, tt.fm_install_inside, text, true});
    }
    rows.push_back({kFaView, m_files_all ? tt.fm_show_games : tt.fm_show_all, {}, true});
    rows.push_back({kFaCopy, tt.fm_copy, chosen.empty() ? tt.fw_none : "", !chosen.empty()});
    rows.push_back({kFaCut, tt.fm_cut, chosen.empty() ? tt.fw_none : locked ? tt.fw_system : "", !chosen.empty() && !locked});
    rows.push_back({kFaPaste, tt.fm_paste, m_clip.empty() ? tt.fw_clip : !writable ? tt.fw_here : "", !m_clip.empty() && writable});
    if (!m_clip.empty()) rows.push_back({kFaForget, tt.fm_forget, {}, true});
    const bool one = chosen.size() == 1 && paths.size() == 1;
    rows.push_back({kFaRename, tt.fm_rename, chosen.empty() ? tt.fw_none : !one ? tt.fw_one : locked ? tt.fw_system : "", one && !locked});
    rows.push_back({kFaFolder, tt.fm_new_folder, writable ? "" : tt.fw_here, writable});
    rows.push_back({kFaProps, tt.fm_props, chosen.empty() ? tt.fw_none : "", !chosen.empty()});
    rows.push_back({kFaDelete, tt.fm_delete, chosen.empty() ? tt.fw_none : locked ? tt.fw_system : "", !chosen.empty() && !locked});
    return rows;
}

void App::DrawFilesMenu(NVGcontext* vg) {
    const Theme& t = m_theme;
    const Tools& tt = TrT();
    const auto rows = FilesMenuRows();
    m_files_menu_focus = std::clamp(m_files_menu_focus, 0, std::max(0, int(rows.size()) - 1));
    // a tap anywhere else closes it
    AddHit(0, 73, kWidth, kHeight - 73 - 56, HitKind::FilesMenuBack);
    // above the bar, growing out of the + that opened it (1A)
    const float h = 52 + float(rows.size()) * kMenuRow + 14, x = kWidth - 24 - kMenuW, y = std::max(84.f, kHeight - 56 - 12 - h);
    nvgSave(vg);
    const float alpha = GrowFromPlus(vg, m_list_at, x, y, kMenuW, h);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x - 8, y + 6, kMenuW + 16, h + 16, 20);
    nvgFillPaint(vg, nvgBoxGradient(vg, x, y + 10, kMenuW, h, 16, 36, nvgRGBAf(0, 0, 0, 0.45f), nvgRGBAf(0, 0, 0, 0)));
    nvgFill(vg);
    draw::Popup(vg, t, x, y, kMenuW, h, 16);
    AddHit(x, y, kMenuW, h, HitKind::Button); // the panel itself does nothing
    const auto chosen = FilesChosen();
    char head[160];
    std::snprintf(head, sizeof(head), tt.fm_title_fmt, int(chosen.size()), draw::Size(m_files_count.bytes).c_str());
    ui::Text(vg, x + 22, y + 28, 15, t.muted, head);
    for (size_t i = 0; i < rows.size(); i++) {
        const auto& r = rows[i];
        const float ry = y + 50 + float(i) * kMenuRow, rc = ry + (kMenuRow - 4) / 2;
        nvgSave(vg);
        const float ra = RowComesIn(vg, m_list_at, int(i), alpha);
        if (int(i) == m_files_menu_focus) {
            ui::FillRound(vg, x + 8, ry, kMenuW - 16, kMenuRow - 4, 10, t.panel2);
            ui::Focus(vg, t, x + 8, ry, kMenuW - 16, kMenuRow - 4, 10, m_time);
        }
        AddHit(x + 8, ry, kMenuW - 16, kMenuRow, HitKind::FilesMenuRow, r.action);
        nvgGlobalAlpha(vg, (r.enabled ? 1.f : 0.45f) * ra);
        ActionIcon(vg, r.enabled ? (r.action == kFaDelete ? t.warn : t.work) : t.muted, r.action, x + 24, rc);
        ui::Text(vg, x + 58, rc, 17, r.enabled ? t.text : t.muted, r.label.c_str());
        if (!r.right.empty()) ui::Text(vg, x + kMenuW - 24, rc, 14, t.muted, r.right.c_str(), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgRestore(vg);
    }
    nvgRestore(vg);
    std::vector<ui::HitRect> rects;
    ui::HintBar(vg, t, nullptr, {{ui::glyph::A, Tr().choose, HidNpadButton_A}, {ui::glyph::B, Tr().close, HidNpadButton_B}}, rects);
    AddHints(rects);
}

void App::UpdateFilesMenu(u64 down, u64 held) {
    const auto rows = FilesMenuRows();
    const int n = std::max(1, int(rows.size()));
    if (Repeat(down, held, HidNpadButton_AnyUp)) m_files_menu_focus = (m_files_menu_focus + n - 1) % n;
    if (Repeat(down, held, HidNpadButton_AnyDown)) m_files_menu_focus = (m_files_menu_focus + 1) % n;
    if (down & (HidNpadButton_B | HidNpadButton_Plus | HidNpadButton_Minus)) {
        m_files_menu = false;
        return;
    }
    if ((down & HidNpadButton_A) && m_files_menu_focus < int(rows.size())) FilesDo(rows[size_t(m_files_menu_focus)].action);
}

void App::LongPress(float x, float y) {
    if (m_tab != Tab::More || m_more != MoreScreen::Files || m_files_menu || m_fwork) return;
    for (auto it = m_hits.rbegin(); it != m_hits.rend(); ++it) {
        if (!it->rect.Contains(x, y)) continue;
        if (it->kind == HitKind::MoreRow || it->kind == HitKind::MoreMark) {
            m_files_side = false;
            m_file_focus = it->index;
            OpenFilesMenu();
        }
        return;
    }
}

// ---- the actions ----

void App::FilesDo(int action) {
    const Tools& tt = TrT();
    const auto rows = FilesMenuRows();
    const auto row = std::find_if(rows.begin(), rows.end(), [&](const FilesRow& r) { return r.action == action; });
    if (row == rows.end()) return;
    if (!row->enabled) {
        // why not: a folder of the system by its name, else what the row says
        for (const auto& p : FilesChosenPaths()) {
            const auto g = fileops::GuardOf(p);
            if (g == fileops::Guard::System || g == fileops::Guard::Self) {
                char text[300];
                std::snprintf(text, sizeof(text), g == fileops::Guard::Self ? tt.fd_self_fmt : tt.fd_locked_fmt, fileops::NameOf(p).c_str());
                if (action == kFaCut || action == kFaRename || action == kFaDelete) {
                    Notice(text);
                    return;
                }
            }
        }
        if (!row->right.empty()) Notice(row->right);
        return;
    }
    const auto paths = FilesChosenPaths();
    switch (action) {
        case kFaInstall: {
            std::vector<InstallJob> jobs;
            for (const auto& g : m_files_found) {
                InstallJob job;
                job.game = g.name;
                job.label = g.parts.size() > 1 ? std::string(TrM().t_files) + " · " + std::to_string(g.parts.size()) : TrM().t_files;
                job.format = g.format;
                job.origin = InstallJob::Origin::File;
                job.resumable = true;
                job.parts = g.parts;
                job.file_id = "file:" + g.parts.front();
                job.size = g.size;
                job.nand = m_files_nand;
                job.color_key = g.name;
                job.delete_after = m_files_after;
                jobs.push_back(std::move(job));
            }
            m_files_menu = false;
            std::fill(m_marked.begin(), m_marked.end(), 0);
            if (!jobs.empty()) QueueLocal(std::move(jobs));
            m_files_nand = false; // the SD card again, always
            break;
        }
        case kFaView:
            m_files_all = !m_files_all;
            m_files_menu = false;
            ReopenFolder();
            break;
        case kFaCopy:
        case kFaCut:
            m_clip = paths;
            m_clip_move = action == kFaCut;
            m_clip_from = m_folder;
            m_clip_bytes = m_files_count.bytes;
            std::fill(m_marked.begin(), m_marked.end(), 0);
            m_files_menu = false;
            break;
        case kFaForget:
            m_clip.clear();
            m_files_menu = false;
            break;
        case kFaPaste: {
            m_files_menu = false;
            for (const auto& p : m_clip) {
                if (fileops::Within(m_folder, p)) {
                    Notice(tt.fx_into_itself);
                    return;
                }
            }
            // the room: a move on the same device takes none
            const bool rename = m_clip_move && !m_clip.empty() && fileops::DeviceOf(m_clip.front()) == fileops::DeviceOf(m_folder);
            if (!rename) {
                const long long room = FreeOn(m_folder);
                if (room >= 0 && (unsigned long long)room < m_clip_bytes + (8ull << 20)) {
                    char text[200];
                    std::snprintf(text, sizeof(text), tt.fx_no_room_fmt, draw::Size(m_clip_bytes).c_str(),
                                  draw::Size((unsigned long long)room).c_str());
                    Notice(text);
                    return;
                }
            }
            auto clip = m_clip;
            const bool move = m_clip_move;
            if (move) m_clip.clear(); // what was cut goes where it is pasted
            StartFilesWork(move ? 2 : 1, std::move(clip), m_folder);
            break;
        }
        case kFaRename: {
            m_files_menu = false;
            const std::string old = fileops::NameOf(paths.front());
            std::string name;
            if (!AskName(tt.fn_rename_guide, old, name) || name == old) break;
            if (fileops::Rename(paths.front(), name)) {
                NoticeOk(tt.fn_renamed);
                ReopenFolder(name);
            } else {
                Notice(tt.fn_bad);
            }
            break;
        }
        case kFaFolder: {
            m_files_menu = false;
            std::string name;
            if (!AskName(tt.fn_folder_guide, "", name)) break;
            if (fileops::MakeFolder(m_folder, name)) {
                NoticeOk(tt.fn_made);
                ReopenFolder(name);
            } else {
                Notice(tt.fn_bad);
            }
            break;
        }
        case kFaProps: {
            m_files_menu = false;
            m_tool = ToolDialog{};
            const auto chosen = FilesChosen();
            char text[200];
            if (chosen.size() == 1) {
                const auto& e = m_entries[size_t(chosen.front())];
                m_tool.title = e.name;
                struct stat st{};
                stat(e.path.c_str(), &st);
                const bool folder = S_ISDIR(st.st_mode) && e.dir;
                std::string kind = folder ? tt.fp_folder : tt.fp_file;
                if (!e.dir && !e.format.empty()) {
                    std::string fmt = e.format;
                    for (auto& c : fmt) c = char(std::toupper((unsigned char)c));
                    std::snprintf(text, sizeof(text), tt.fp_game_fmt, fmt.c_str());
                    kind = text;
                }
                m_tool.lines.push_back({tt.fp_kind, kind});
                m_tool.lines.push_back({tt.fp_size, draw::Size(m_files_count.bytes)});
                if (folder) {
                    std::snprintf(text, sizeof(text), tt.fp_holds_fmt, m_files_count.files,
                                  m_files_count.folders > 0 ? m_files_count.folders - 1 : 0ull);
                    m_tool.lines.push_back({tt.fp_holds, text});
                }
                if (st.st_mtime > 0) {
                    const std::time_t when = st.st_mtime;
                    std::tm tm{};
                    localtime_r(&when, &tm);
                    std::strftime(text, sizeof(text), "%d/%m/%Y %H:%M", &tm);
                    m_tool.lines.push_back({tt.fp_changed, text});
                }
            } else {
                std::snprintf(text, sizeof(text), tt.fm_title_fmt, int(chosen.size()), draw::Size(m_files_count.bytes).c_str());
                m_tool.title = text;
                std::snprintf(text, sizeof(text), tt.fp_holds_fmt, m_files_count.files, m_files_count.folders);
                m_tool.lines.push_back({tt.fp_holds, text});
            }
            m_tool.lines.push_back({tt.fp_where, m_folder});
            m_tool.single = true;
            m_dialog = Dialog{};
            m_dialog.kind = DialogKind::FilesProps;
            break;
        }
        case kFaDelete: {
            m_files_menu = false;
            m_tool = ToolDialog{};
            const auto chosen = FilesChosen();
            char text[300];
            if (chosen.size() == 1) {
                std::snprintf(text, sizeof(text), tt.fd_title_one_fmt, m_entries[size_t(chosen.front())].name.c_str());
            } else {
                std::snprintf(text, sizeof(text), tt.fd_title_many_fmt, int(chosen.size()));
            }
            m_tool.title = text;
            std::snprintf(text, sizeof(text), tt.fd_body_fmt, draw::Size(m_files_count.bytes).c_str());
            m_tool.body = text;
            for (const int i : chosen) {
                const auto& e = m_entries[size_t(i)];
                std::vector<std::string> one;
                if (!IsFolder(e.path) && e.parts.size() > 1) one = e.parts;
                else one.push_back(e.path);
                const auto c = fileops::CountAll(one);
                std::snprintf(text, sizeof(text), tt.fd_files_fmt, c.files, draw::Size(c.bytes).c_str());
                m_tool.lines.push_back({e.name, text, fileops::GuardOfAll(one) == fileops::Guard::Careful});
            }
            if (fileops::GuardOfAll(paths) == fileops::Guard::Careful) m_tool.note = tt.fd_careful;
            std::snprintf(text, sizeof(text), tt.fd_go_fmt, draw::Size(m_files_count.bytes).c_str());
            m_tool.primary = text;
            m_dialog = Dialog{};
            m_dialog.kind = DialogKind::FilesDelete;
            m_dialog.push = paths;
            break;
        }
        default: break;
    }
}

// ---- the work, on a thread of its own ----

void App::StartFilesWork(int kind, std::vector<std::string> paths, const std::string& to) {
    if (m_fwork || paths.empty()) return;
    m_fwork = std::make_unique<fileops::Progress>();
    m_fwork_kind = kind;
    m_fwork_to = to;
    m_fwork_at = m_time;
    // where it goes, as the places say it; a FAT32 USB disk splits files of 4 GB or more
    m_fwork_place = Tr().sd_card;
    m_fwork_split = false;
    const std::string dev = fileops::DeviceOf(kind == 0 ? paths.front() : to);
    for (const auto& d : UsbDisks()) {
        if (fileops::DeviceOf(d.mount) == dev) {
            m_fwork_place = std::string(TrM().usb_disk) + (d.label.empty() ? "" : " · " + d.label);
            m_fwork_split = d.fat32 && kind != 0;
        }
    }
    appletSetAutoSleepDisabled(true);
    appletSetMediaPlaybackState(true);
    fileops::Progress* p = m_fwork.get();
    const bool split = m_fwork_split;
    m_fwork_thread = std::thread([p, kind, paths = std::move(paths), to, split] {
        if (kind == 0) fileops::DeleteAll(paths, *p);
        else fileops::CopyAll(paths, to, kind == 2, split, *p);
    });
    m_dialog = Dialog{};
    m_dialog.kind = DialogKind::FilesWork;
}

void App::FollowFilesWork() {
    if (!m_fwork || !m_fwork->done) return;
    if (m_fwork_thread.joinable()) m_fwork_thread.join();
    const Tools& tt = TrT();
    fileops::Progress& p = *m_fwork;
    char text[300];
    std::string made;
    {
        std::lock_guard lock(p.mutex);
        if (!p.made.empty()) made = p.made.front();
        if (p.failed) {
            std::snprintf(text, sizeof(text), tt.fx_failed_fmt, fileops::NameOf(p.error).c_str());
            Notice(text);
        }
    }
    if (p.failed) {
        // said above
    } else if (p.cancel) {
        Notice(tt.fx_stopped);
    } else if (m_fwork_kind == 0) {
        std::snprintf(text, sizeof(text), tt.fx_done_delete_fmt, draw::Size(p.bytes_done).c_str());
        NoticeOk(text);
    } else {
        std::snprintf(text, sizeof(text), m_fwork_kind == 2 ? tt.fx_done_move_fmt : tt.fx_done_copy_fmt, (unsigned long long)p.files_done,
                      draw::Size(p.bytes_done).c_str());
        NoticeOk(text);
    }
    const int kind = m_fwork_kind;
    m_fwork.reset();
    if (!m_queue || !m_queue->Busy()) {
        appletSetMediaPlaybackState(false);
        appletSetAutoSleepDisabled(false);
    }
    if (m_dialog.kind == DialogKind::FilesWork) m_dialog = Dialog{};
    std::fill(m_marked.begin(), m_marked.end(), 0);
    m_sd_checked = -100;
    if (m_tab == Tab::More && m_more == MoreScreen::Files) ReopenFolder(kind == 0 ? std::string{} : made);
}

// ---- the questions ----

bool App::ToolDialogText(ToolDialog& d) {
    const Tools& tt = TrT();
    switch (m_dialog.kind) {
        case DialogKind::FilesDelete:
        case DialogKind::FilesProps:
        case DialogKind::ContentDetails:
        case DialogKind::SaveDelete: {
            const std::string cancel = d.cancel;
            d = m_tool;
            if (d.cancel.empty()) d.cancel = cancel;
            return true;
        }
        case DialogKind::FilesWork: {
            if (!m_fwork) return false;
            fileops::Progress& p = *m_fwork;
            char text[300];
            if (m_fwork_kind == 0) {
                d.title = tt.fx_deleting;
            } else {
                std::snprintf(text, sizeof(text), m_fwork_kind == 2 ? tt.fx_moving_fmt : tt.fx_copying_fmt, m_fwork_place.c_str());
                d.title = text;
            }
            d.body = p.Current();
            if (d.body.empty()) d.body = " ";
            const unsigned long long done = p.bytes_done, total = p.bytes_total;
            d.bar = true;
            d.running = !p.done;
            d.frac = total ? float(double(done) / double(total)) : 0.f;
            std::snprintf(text, sizeof(text), tt.fx_progress_fmt, draw::Size(done).c_str(), draw::Size(total).c_str(),
                          (unsigned long long)p.files_done, (unsigned long long)p.files_total);
            d.bar_text = text;
            const double secs = m_time - m_fwork_at;
            if (m_fwork_kind != 0 && secs > 2 && done > 0 && total > done) {
                const double speed = double(done) / secs;
                std::snprintf(text, sizeof(text), tt.fx_speed_fmt, draw::Size((unsigned long long)speed).c_str(),
                              Left(double(total - done) / speed).c_str());
                d.bar_text += "   ·   ";
                d.bar_text += text;
            }
            d.note = m_fwork_split ? std::string(tt.fx_fat32) + "\n" + tt.fx_awake : std::string(tt.fx_awake);
            d.single = true;
            d.cancel = tt.fx_stop;
            return true;
        }
        default: return UpdaterDialogText(d);
    }
}

bool App::ToolDialogInput(u64 down, u64 /*held*/) {
    if (m_dialog.kind == DialogKind::FilesWork) {
        // B (or A on Stop) stops it; the question closes when the work has stopped
        if ((down & (HidNpadButton_B | HidNpadButton_A)) && m_fwork) m_fwork->cancel = true;
        return true;
    }
    return UpdaterDialogInput(down);
}

void App::ConfirmToolDialog() {
    switch (m_dialog.kind) {
        case DialogKind::FilesDelete: {
            auto paths = std::move(m_dialog.push);
            // nothing of the system, whatever changed meanwhile
            const auto g = fileops::GuardOfAll(paths);
            if (g == fileops::Guard::System || g == fileops::Guard::Self) break;
            StartFilesWork(0, std::move(paths), {});
            break;
        }
        case DialogKind::ContentDetails: ConsoleVersionReset(); break;
        case DialogKind::SaveDelete: ConfirmSaveDelete(); break;
        default: ConfirmUpdater(); break;
    }
}

} // namespace nxdock
