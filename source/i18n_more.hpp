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
};

const More& TrM();

} // namespace nxdock
