// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

namespace nxdock {

struct Strings {
    const char *library, *queue, *installed, *settings;
    const char *free_space, *all, *not_installed, *with_update, *dlc;
    const char *search, *view, *exit, *back;
    const char *installing, *more_in_queue, *left;
    const char *network, *decompress, *threads, *write_sd, *check_sha, *parts_of;
    const char *in_queue, *waiting, *done, *checked, *no_sleep, *pause, *remove;
    const char *base_update_sd;
    const char *theme, *dark, *light, *change;
    const char *coming_soon, *preview_note;
    const char *update_badge;
    // settings and the speed test
    const char *address, *console_key, *key_saved, *not_set, *speed_test, *start, *cancel;
    const char *conn1, *conn2, *conn4, *sd_write, *waiting_test;
    const char *applet_warning, *app_mode, *need_config, *test_note, *best;
    const char *err_key, *err_off, *err_forbidden, *err_connect, *err_sd;
    const char *edit;
    // finding the app and pairing
    const char *find_library, *found_hint, *searching, *type_address, *type_address_hint, *chosen;
    const char *approve_title, *approve_hint, *waiting_accept, *remember, *search_again, *new_code;
    const char *reconnecting, *not_linked, *forget, *link_off_warning, *choose, *none_found;
    const char *link_not_app, *link_offline, *link_busy, *link_expired, *link_refused, *connected_to;
    const char *manual_address, *manual_key;
    // the real library and the page of a game
    const char *loading_library, *empty_filter, *retry, *queue_empty, *queue_empty_hint, *filter;
    const char *version, *format, *will_take, *firmware, *on_console, *not_on_console, *or_newer;
    const char *base_game, *update_word, *dlc_prefix, *install_to, *sd_card, *internal, *install_size;
    const char *install_soon, *space_line, *new_tag, *installed_tag, *select, *search_hint;
    // installing
    const char *err_cancelled, *err_space, *err_firmware, *err_network, *err_refused, *err_changed;
    const char *err_sha, *err_ncz, *err_ticket, *err_memory, *err_other, *err_experimental;
    const char *added_to_queue, *already_in_queue, *internal_soon, *xci_soon, *needs_firmware, *nothing_picked;
    const char *installing_name, *in_queue_count, *preparing, *registering, *not_compressed;
    const char *failed, *cancelled, *thread_one, *clear_done, *download_test, *queue_done_hint, *leave_while_installing;
    const char *err_container, *err_game_running, *err_space_nand;
    const char *nand_title, *nand_body, *used_fmt, *will_take_fmt, *free_after_fmt, *nand_no_room, *nand_confirm, *space_line_nand, *write_nand, *internal_short, *this_session, *history, *view_switch, *session_empty, *session_empty_hint, *undo_all, *remove_this, *freed_sd, *freed_nand, *undo_bar, *undo_title, *undo_one_title, *undo_note, *undo_confirm, *removing, *removed, *remove_failed, *undo_wait_queue, *more_count, *hist_installed, *hist_total, *hist_avg, *hist_best, *col_date, *col_game, *col_where, *col_size, *col_time, *col_speed, *col_result, *today, *yesterday, *res_installed, *history_empty, *clear_history, *clear_title, *clear_body, *clear_confirm, *details, *close, *col_written;
    const char *update_all, *update_all_title, *update_all_note, *update_all_confirm, *verify, *verify_title, *verify_progress, *verify_fine, *verify_bad, *verify_all_ok, *repair_confirm, *repair_note, *repair_missing, *verify_wait, *repair_tag;
    const char *push_title, *push_from, *push_install, *not_now, *share_title, *share_body, *share_yes, *no_word, *share_row, *yes_word;
    const char *battery_low;
};

// Picks the language of the console: English, Spanish, French, German, Italian, Portuguese,
// Dutch, Russian, Japanese, Korean or Chinese, as the library app; English for any other.
const Strings& Tr();
void InitStrings();

} // namespace nxdock
