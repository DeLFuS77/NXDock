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
};

// Picks English or Spanish from the console language; more languages later.
const Strings& Tr();
void InitStrings();

} // namespace nxdock
