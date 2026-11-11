// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

namespace nxdock {

// The texts of the second roadmap (clear errors, installing without a server, saves, space,
// shops, cheats and mods, screen off), apart from i18n.cpp. Every language is written with
// designated initialisers; a text a language lacks is taken from English.
struct More {
    // clear errors (phase 9): what happened and what to do
    const char *what_happened, *what_to_do, *code_line;
    const char *e_cancelled_t, *e_cancelled_w, *e_cancelled_d;
    const char *e_space_t, *e_space_w, *e_space_d;
    const char *e_space_nand_t, *e_space_nand_w, *e_space_nand_d;
    const char *e_firmware_t, *e_firmware_w, *e_firmware_d;
    const char *e_network_t, *e_network_w, *e_network_d;
    const char *e_refused_t, *e_refused_w, *e_refused_d;
    const char *e_changed_t, *e_changed_w, *e_changed_d;
    const char *e_sha_t, *e_sha_w, *e_sha_d;
    const char *e_ncz_t, *e_ncz_w, *e_ncz_d;
    const char *e_ticket_t, *e_ticket_w, *e_ticket_d;
    const char *e_personal_t, *e_personal_w, *e_personal_d;
    const char *e_keys_t, *e_keys_w, *e_keys_d;
    const char *e_memory_t, *e_memory_w, *e_memory_d;
    const char *e_experimental_t, *e_experimental_w, *e_experimental_d;
    const char *e_container_t, *e_container_w, *e_container_d;
    const char *e_running_t, *e_running_w, *e_running_d;
    const char *e_storage_t, *e_storage_w, *e_storage_d;
    const char *e_other_t, *e_other_w, *e_other_d;
    const char *nothing_installed;
    // the check before installing (phase 9)
    const char *pc_title, *pc_firmware, *pc_keys, *pc_ams;
    const char *pc_fw_ok, *pc_fw_low, *pc_keys_ok, *pc_keys_low, *pc_ams_ok, *pc_ams_low, *pc_ams_none, *pc_unknown;
    const char *pc_note, *pc_go, *pc_and_more;
    // the More tab and installing without a server (phase 10)
    const char *more_tab, *sect_noserver, *sect_console, *open;
    const char *t_files, *t_files_sub, *t_mtp, *t_mtp_sub, *t_usb, *t_usb_sub, *t_shops, *t_shops_sub;
    const char *t_saves, *t_saves_sub, *t_space, *t_space_sub, *t_mods, *t_mods_sub, *t_screen, *t_screen_sub;
    const char *places, *usb_disk, *no_usb, *mark, *install_marked, *sd_or_internal, *marked_fmt, *left_after_fmt;
    const char *empty_folder, *split_fmt;
    const char *mtp_title, *mtp_hint, *mtp_install_sd, *mtp_install_sd_sub, *mtp_install_nand, *mtp_install_nand_sub;
    const char *mtp_sd_card, *mtp_sd_card_sub, *arriving, *connected_usb, *not_connected, *waiting_files, *stop_back;
    const char *allow_nand, *nand_allowed, *mtp_failed, *mtp_to_sd, *mtp_to_nand, *count_fmt, *nand_q_title, *nand_q_body;
    const char *usb_title, *usb_steps, *usb_from, *usb_failed, *wait_usb;
    // the vault of saves (phase 11)
    const char *saves_user_fmt, *copies_fmt, *copies_lib, *copy_now, *restore, *user_word, *restore_note, *note_guide;
    const char *no_saves, *no_copies, *need_link, *st_copying, *st_sending, *st_receiving, *st_restoring, *copied, *restored;
    const char *restore_title, *restore_body, *restore_go, *before_restore, *no_note;
    const char *v_copy_failed, *v_copy_failed_d, *v_no_save, *v_no_save_d, *v_restore_failed, *v_restore_failed_d;
    // space and dumps (phase 12)
    const char *sp_sd_fmt, *sp_nand_fmt, *a_clean, *a_clean_sub_fmt, *a_archive, *a_archive_sub, *a_move, *a_move_sub, *a_plan, *a_plan_sub_fmt, *a_plan_fits, *a_card, *a_card_sub, *a_card_none, *a_dump, *a_dump_sub, *choose_word, *dumps_note, *scanning, *clean_title, *clean_body_fmt, *clean_go, *archive_title_fmt, *archive_body_fmt, *archive_go, *move_title_fmt, *move_body_fmt, *move_go, *plan_head_fmt, *card_title, *card_body, *dump_title_fmt, *dump_body, *dump_go, *st_cleaning, *st_archiving, *st_moving, *st_dumping, *done_clean, *done_archive, *done_move, *done_dump, *s_archive_failed, *s_archive_failed_d, *s_move_failed, *s_move_failed_d, *s_dump_failed, *s_dump_failed_d, *s_no_card, *s_no_card_d, *d_uploads_off, *d_uploads_off_d, *d_library_full, *d_library_full_d, *archived_tag, *game_word;
    // shops (phase 13)
    const char *sh_add, *sh_add_sub, *sh_none, *sh_guide_url, *sh_guide_name, *sh_guide_user, *sh_guide_pass, *sh_remove, *sh_remove_title, *sh_remove_body, *sh_loading, *sh_badge, *sh_back, *sh_edit, *e_shop_enc_t, *e_shop_enc_w, *e_shop_enc_d, *e_shop_bad_t, *e_shop_bad_w, *e_shop_bad_d, *e_shop_auth_t, *e_shop_auth_d, *e_shop_empty, *sh_note;
    // cheats and mods (phase 14)
    const char *cm_pick, *cm_cheats_head_fmt, *cm_cheats_lib, *cm_cheats_count_fmt, *cm_no_cheats, *cm_loading, *cm_mods_head, *cm_no_mods, *cm_mod_files_fmt, *cm_note, *cm_toggle, *cm_conflict_t, *cm_conflict_d, *cm_failed_t, *cm_failed_d, *cm_put, *cm_removed;
    // the screen off (phase 15)
    const char *so_body, *so_button, *so_auto_row, *so_nothing;
    // when a game was last played (the planner)
    const char *idle_days_fmt, *idle_today, *idle_never;
    const char *no_installed;
    const char *anim_row, *anim_full, *anim_soft, *anim_none;
    // the first-run wizard, updated for More, the animations and the screen off
    const char *wiz_screen_off, *wiz_no_app, *tour_more, *tour_count4;
    // only what is missing is installed (phase 16)
    const char *saved_fmt;
    // the check of the SD card (phase 17)
    const char *sd_action, *sd_action_sub, *sd_running_fmt, *sd_title, *sd_explain, *sd_risk, *sd_quick, *sd_quick_sub, *sd_full,
        *sd_full_sub, *sd_start, *sd_stop, *sd_again, *sd_writing, *sd_reading, *sd_deleting, *sd_write_speed, *sd_read_speed,
        *sd_left, *sd_keep_on, *sd_free_fmt, *sd_key_free, *sd_key_written, *sd_key_ok, *sd_key_bad, *sd_good_t, *sd_good_d,
        *sd_fake_t, *sd_fake_d, *sd_bad_t, *sd_bad_d, *sd_full_t, *sd_full_d, *sd_failed_t, *sd_failed_d, *sd_stopped_t,
        *sd_stopped, *sd_speeds_fmt;
    // the saves copied before a change to a game (phase 18)
    const char *sc_row, *sc_note_update, *sc_note_repair, *sc_note_remove, *sc_note_back, *sc_copying, *sc_ask_title, *sc_ask_body,
        *sc_go_on, *sc_go_remove;
    // cheats and mods that an update breaks (phase 19)
    const char *mr_title, *mr_cheats_fmt, *mr_patches_fmt, *mr_exefs, *mr_body, *mr_go, *mr_not_now;
    // another version of a game (phase 20)
    const char *ver_hint, *ver_title, *ver_installed, *ver_back_fmt, *ver_install_fmt, *ver_note, *dg_title_fmt, *dg_body_fmt, *dg_go;
    // the queue on the web and its end on the phone (phases 21 and 22)
    const char *qw_notify_row, *qw_paused, *qw_paused_web, *qw_resumed, *qw_pause, *qw_go_on, *qw_cancel_title, *qw_cancel_body, *qw_cancel_go,
        *qw_keep;
    // going on after the console went off (phase 23)
    const char *rs_title, *rs_parts_fmt, *rs_waiting_fmt, *rs_note, *rs_go, *rs_discard, *rs_discarded;
    // optimize space: what Clean leftovers takes (phase 24)
    const char *cl_sub_fmt, *cl_body_fmt;
    // compress games, experimental (phase 24)
    const char *cz_action, *cz_action_sub, *cz_title, *cz_tag, *cz_intro_title, *cz_intro_body, *cz_turn_on, *cz_row, *cz_fw_ok, *cz_fw_old, *cz_sig_ok_fmt, *cz_sig_missing, *cz_explain, *cz_pending, *cz_worth_fmt, *cz_low_fmt, *cz_already, *cz_ours_fmt, *cz_unsupported, *cz_no_games, *cz_go_title_fmt, *cz_go_body_fmt, *cz_risks, *cz_go, *cz_nosig_title, *cz_nosig_body, *cz_fw_title, *cz_fw_body, *cz_undo_title_fmt, *cz_undo_body, *cz_undo_go, *cz_undo_missing, *cz_phase1, *cz_phase2, *cz_phase3, *cz_done_fmt, *cz_hist_label, *cz_badge, *cz_busy, *cz_failed_t, *cz_failed_d, *cz_unsupported_t, *cz_unsupported_d, *cz_stopped_t, *cz_stopped_d, *cz_low_t, *cz_low_d, *cz_room_t, *cz_room_d;
    const char *cz_tap, *cz_why_sparse, *cz_why_key, *cz_why_missing, *cz_why_update, *cz_base_t, *cz_base_d, *qm_tag, *qm_merging, *qm_playable, *qm_read, *qm_pack, *qm_check, *qm_installed, *qm_note, *qm_merged_fmt, *qm_at_end, *cz_keys_ok, *cz_keys_missing, *cz_keys_t, *cz_keys_d, *sp_reading_fmt, *sp_names_fmt, *jump_letters;
    const char *cz_possible, *cz_checking_fmt, *cz_measuring_fmt, *cz_counts_fmt, *cz_only_some, *cz_show_all, *cz_none_possible;
    // merge and compress, and the DLC with their game (25F: 1A, 4A)
    const char *cz_merge_tag, *cz_merge_sub_fmt, *cz_merge_worth_fmt, *cz_merged_fmt, *cz_merged_sub, *cz_update_wait, *cz_update_tag, *cz_update_sub_fmt, *cz_dlc_fmt, *cz_go_merge_title_fmt, *cz_go_merge_body_fmt, *cz_merge_risks, *cz_go_dlc_fmt, *cz_merge_go, *cz_update_notice_fmt, *cz_undo_merged_body;
    // a game merged when Nintendo had compressed it: undone before a newer update is merged
    const char *cz_why_redo, *cz_redo_t, *cz_redo_d, *cz_undo_redo_body;
    // the certificate of an https shop changed since it was first opened
    const char *e_shop_key_t, *e_shop_key_w, *e_shop_key_d;
    // the question of the internal memory: the SD card instead
    const char *nand_sd_hint;
    // In the console, of the Installed tab: every game installed, and taking contents off
    const char *ic_console, *ic_reading, *ic_empty, *ic_not_lib, *ic_dlc_needs_tag, *ic_dlc_fmt, *ic_archived_sub, *ic_names_fmt, *ic_open, *ic_meta_fmt, *ic_archived_note, *ic_needs_fmt, *ic_with_game, *ic_marked_fmt, *ic_mark, *ic_remove_marked, *ic_from_library, *ic_removing, *ic_not_in_view, *ic_nothing, *ic_update_word, *ic_dlc_word, *ic_rm_title_fmt, *ic_rm_body_fmt, *ic_rm_go, *ic_done_remove, *ic_rm_failed_t, *ic_rm_failed_d, *ic_rm_running_t, *ic_rm_running_d;
    // installing what is installed already, the same version
    const char *ri_title_one, *ri_title_fmt, *ri_body, *ri_go, *ri_skip;
    // the sizes of In the console
    const char *ic_size, *ic_size_list, *ic_size_mid, *ic_size_big;
    // taking several games off together
    const char *ic_rm_many_fmt, *ic_with_fmt, *ic_whole_game, *ic_done_many_fmt, *ic_done_one_fmt, *ic_picked_fmt, *ic_rm_picked_fmt;
    // the Library: what is installed, and the size of its covers
    const char *lb_installed, *lb_installed_tag, *lb_needs_update, *lb_needs_dlc, *lb_size, *lb_size_big, *lb_size_small;
    const char *lb_checking;
    // where it installs, in large
    const char *dc_free_fmt, *dc_left_fmt, *dc_install_sd_fmt, *dc_install_nand_fmt, *dc_switch, *dc_once, *marked_short_fmt;
    // what files need, read before they are queued
    const char *pf_title, *pf_body, *dn_title, *dn_body, *dn_line_fmt, *dn_go, *dn_skip;
    // a large library: its catalog prepared, the list kept on the SD card
    const char *cat_preparing_fmt, *cat_preparing, *cat_stale;
    // the WiFi in the top bar and its card
    const char *wf_title, *wf_cable, *wf_none, *wf_none_hint, *wf_network, *wf_coverage, *wf_level_fmt, *wf_l0, *wf_l1, *wf_l2, *wf_l3, *wf_console, *wf_library, *wf_weak;
    // questions before cancelling, leaving and forgetting; the hints of a shop, pairing and MTP
    const char *qc_title, *qc_body, *quit_title, *quit_body, *quit_go, *quit_stay, *fg_title, *fg_body, *fg_go, *sh_shop_hint, *sh_shops_hint, *pr_remember, *nand_sd_button, *mtp_sd_only;
    // versions as people say them, an empty library, no network
    const char *ver_update_fmt, *ver_none, *lb_empty_t, *lb_empty, *lb_reload, *nn_title;
    // shorter and plainer texts
    const char *qs_left_fmt, *pr_todo_code, *pr_todo_search, *cz_saves_fmt;
    // Settings in groups, the question of going on, the errors of the library and the pairing, the dumps
    const char *st_g_library, *st_g_install, *st_g_notices, *st_g_look, *st_g_wizard, *st_g_manual, *st_change, *st_updates, *st_dest, *st_pick, *st_screen, *st_scr_normal, *st_scr_dim, *st_scr_off, *st_finish, *st_battery, *st_battery_fmt, *rs_later, *rd_title, *rd_body_fmt, *rd_go, *le_reach_t, *le_reach_w, *le_reach_d, *le_key_t, *le_key_w, *le_key_d, *le_off_t, *le_off_w, *le_off_d, *le_server_t, *le_server_w, *le_server_d, *le_stop_t, *le_stop_w, *le_stop_d, *pr_code_failed, *sp_todo, *cat_stale_short, *t_dumps, *t_dumps_sub;
};

const More& TrM();

} // namespace nxdock
