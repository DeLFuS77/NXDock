// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The texts of the tools of the study of 11/11: the file list's actions, deleting files after their
// install, NXDock updating itself, what is missing for the games on the console, the details of a
// content, more folders over MTP and more of the saves. Kept apart from i18n_more so each part can
// grow on its own; a language that lacks a text takes the English one.
#pragma once

namespace nxdock {

struct Tools {
    // the file list: the view, the menu of actions and why an action cannot be used now
    const char *fv_games, *fv_all, *fm_actions, *fm_all, *fm_title_fmt, *fm_install_inside, *fm_games_fmt, *fm_copy, *fm_cut,
        *fm_paste, *fm_rename, *fm_new_folder, *fm_props, *fm_delete, *fm_forget, *fm_show_all, *fm_show_games, *fw_none, *fw_clip,
        *fw_one, *fw_system, *fw_here, *fw_no_games, *fc_copy_fmt, *fc_move_fmt;
    // deleting
    const char *fd_title_one_fmt, *fd_title_many_fmt, *fd_body_fmt, *fd_go_fmt, *fd_careful, *fd_files_fmt, *fd_locked_fmt,
        *fd_self_fmt;
    // the work going on, and how it ended
    const char *fx_deleting, *fx_copying_fmt, *fx_moving_fmt, *fx_progress_fmt, *fx_speed_fmt, *fx_fat32, *fx_stop, *fx_awake,
        *fx_done_delete_fmt, *fx_done_copy_fmt, *fx_done_move_fmt, *fx_failed_fmt, *fx_stopped, *fx_into_itself, *fx_no_room_fmt;
    // a name, a new folder, the properties
    const char *fn_rename_guide, *fn_folder_guide, *fn_bad, *fn_renamed, *fn_made, *fp_kind, *fp_folder, *fp_file, *fp_size,
        *fp_holds, *fp_holds_fmt, *fp_where, *fp_changed, *fp_game_fmt;
    // deleting the files once they install well
    const char *fa_switch, *fa_sub, *fa_tag, *fa_done_fmt;
};

const Tools& TrT();

} // namespace nxdock
