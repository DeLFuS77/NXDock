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
    // NXDock updating itself (5A)
    const char *st_g_nxdock, *up_row, *up_have_fmt, *up_have_new_fmt, *up_notice_row, *up_back_row, *up_back_kept, *up_back_none, *up_checking, *up_title_fmt, *up_body_fmt, *up_note_fmt, *up_go, *up_later, *up_latest_t, *up_latest_fmt, *up_downloading, *up_done, *up_failed_t, *up_err_network, *up_err_none, *up_err_keys, *up_err_sig, *up_err_sum, *up_err_write, *up_busy, *up_out_fmt, *up_back_title, *up_back_body, *up_back_go, *up_back_failed, *up_wiz;
    // what is missing for the games on the console (6A)
    const char *ms_filter_fmt, *ms_dlc_fmt, *ms_card, *ms_update_fmt, *ms_update, *ms_dlc, *ms_dlc_of_fmt, *ms_in_lib, *ms_some_lib_fmt, *ms_not_lib, *ms_list, *ms_list_fmt, *ms_list_none, *ms_nothing, *ms_install_hint, *ms_row, *ms_hint_filter, *ms_hint_all;
    // the details of a content, its ticket and the version the console asks for (7A)
    const char *cd_title_fmt, *cd_id, *cd_version, *cd_where, *cd_ticket, *cd_t_common, *cd_t_personal, *cd_t_missing, *cd_t_none, *cd_keys, *cd_keys_fmt, *cd_keys_one_fmt, *cd_firmware, *cd_firmware_fmt, *cd_needs_app, *cd_asks, *cd_asks_fmt, *cd_ticket_go, *cd_version_go, *cd_no_lib, *cd_version_done, *cd_version_fail, *cd_ticket_label, *cd_hint, *cd_missing_note;
    // more folders for the computer over MTP (8A)
    const char *mu_title, *mu_installed, *mu_installed_sub, *mu_saves, *mu_saves_sub, *mu_album, *mu_album_sub, *mu_usb, *mu_usb_sub, *mu_usb_none, *mu_device, *mu_note;
};

const Tools& TrT();

} // namespace nxdock
