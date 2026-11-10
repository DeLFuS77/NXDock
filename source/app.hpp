// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include <switch.h>
#include <memory>
#include <vector>
#include "catalog.hpp"
#include "file_source.hpp"
#include "config.hpp"
#include "covers.hpp"
#include "gfx.hpp"
#include "history.hpp"
#include "install_queue.hpp"
#include "link.hpp"
#include "precheck.hpp"
#include "shop.hpp"
#include "space.hpp"
#include "vault.hpp"
#include "push.hpp"
#include "sound.hpp"
#include "safety.hpp"
#include "speedtest.hpp"
#include "theme.hpp"
#include "ui.hpp"

namespace nxdock {

// The colour of a cover without a picture and its initial (app.cpp).
unsigned PlaceholderColor(const std::string& key);
std::string Initial(const std::string& name);

enum class Tab { Library, Queue, Installed, More, Settings };
inline constexpr int kTabs = 5;

class App {
public:
    bool Init();
    void Exit();
    // Returns false when the user asks to leave.
    bool Frame();

private:
    // Everything on screen that answers to touch, rebuilt every frame.
    enum class HitKind { Button, Tab, Chip, Cover, QueueStrip, SettingsRow, PairRow, Remember, DetailRow, Dest, Install, QueueRow,
                         InstalledView, InstalledRow, DialogBack, DialogButton, MenuRow, MenuBack, WizItem, WizButton, WizMusic,
                         MoreTile, MorePlace, MoreRow, MoreMark, MoreSide, VaultGame, VaultCopy, SpaceAction, PickRow, ShopRow };
    struct Hit {
        ui::HitRect rect;
        HitKind kind;
        int index;
    };

    // The first-run wizard (wizard.cpp) and the walk through the tabs after it.
    enum class WizKind { Choice, Toggle, Action, Button };
    struct WizItem {
        std::string label, sub;
        bool on;
        WizKind kind;
    };
    struct WizGroup {
        std::string title;
        std::vector<WizItem> items;
        int cols;
        bool small = false; // short tiles after their title, on one line
    };
    void OpenWizard();
    void CloseWizard(bool tour);
    void ApplyTheme(int mode); // 0 light, 1 dark, 2 as the console
    std::vector<WizGroup> WizardGroups();
    void WizardGo(int step);
    void ActivateWizard(int group, int item);
    void UpdateWizard(u64 down, u64 held);
    void DrawWizard(NVGcontext* vg);
    bool TourActive() const;
    void FollowTour();
    void EndTour();
    void DrawTour(NVGcontext* vg);
    bool m_wizard = false;
    int m_wiz_step = 0, m_wiz_group = 0, m_wiz_item = 0;
    double m_wiz_step_at = 0;
    double m_tour_at = -100;
    ui::HitRect m_tab_rects[kTabs]{};

    // The More tab (3A, more.cpp): a tile for each tool and their screens.
    enum class MoreScreen { Tiles, Files, Mtp, Usb, Shops, Saves, Space, Mods };
    void OpenMoreTile(int tile);
    void OpenMoreScreen(MoreScreen screen);
    void LeaveMoreScreen();
    void DrawMore(NVGcontext* vg);
    void UpdateMore(u64 down, u64 held);
    void DrawTiles(NVGcontext* vg);
    void DrawTileIcon(NVGcontext* vg, int tile, float x, float cy);
    void UpdateTiles(u64 down, u64 held);
    // the SD card and USB disks (4A)
    void FollowPlaces();
    void OpenFolder(const std::string& folder, bool root);
    void InstallMarkedFiles();
    void QueueLocal(std::vector<InstallJob> jobs);
    void DrawFiles(NVGcontext* vg);
    void UpdateFiles(u64 down, u64 held);
    // MTP and NS-USBloader (5A)
    void FollowUsb();
    void DrawArrival(NVGcontext* vg);
    void UpdateArrival(u64 down, u64 held);
    MoreScreen m_more = MoreScreen::Tiles;
    MoreScreen m_after_risk = MoreScreen::Tiles; // a screen to open once the warning is accepted
    int m_tile = 0;
    std::vector<std::string> m_place_roots, m_place_names;
    int m_place = 0;
    bool m_files_side = false;       // the focus on the places
    std::string m_folder;
    bool m_folder_root = true;
    std::vector<LocalEntry> m_entries;
    std::vector<char> m_marked;
    int m_file_focus = 0, m_file_first = 0;
    bool m_files_nand = false;       // where files go: R
    double m_places_checked = -100;
    bool m_usb_active = false;       // a list of the computer is being installed
    std::string m_more_error;
    // the vault of saves (6A, more_vault.cpp)
    void OpenVault();
    std::vector<LibrarySave> VaultCopies();
    void FollowVault();
    void DrawVault(NVGcontext* vg);
    void UpdateVault(u64 down, u64 held);
    std::unique_ptr<Vault> m_vault;
    std::vector<Vault::User> m_vault_users;
    std::vector<Vault::Save> m_vault_saves;
    int m_vault_user = 0, m_vault_focus = 0, m_vault_copy = 0, m_vault_side = 0, m_vault_finished = 0;
    // space and dumps (8A, more_space.cpp)
    unsigned long long QueueNeedsOnSd();
    void OpenSpace();
    void OpenPicker(int kind); // 0 archive (several), 1 move, 2 dump
    void OpenPlanner();
    void FollowSpace();
    void DrawSpace(NVGcontext* vg);
    void DrawPicker(NVGcontext* vg, const Space::View& v);
    void UpdateSpace(u64 down, u64 held);
    void SpaceDialogText(std::string& title, std::string& body, std::string& primary);
    void ConfirmSpaceDialog();
    std::unique_ptr<Space> m_space;
    int m_space_focus = 0, m_space_mode = 0, m_pick_kind = 0, m_pick_focus = 0, m_pick_first = 0, m_space_finished = 0;
    std::vector<std::pair<int, int>> m_pick_rows; // the game, and its content (dump) or -1
    std::vector<char> m_pick_marks;
    std::string m_plan_head;
    // shops of the kind of Tinfoil and Ownfoil (9A, more_shop.cpp)
    void OpenShops();
    bool EditShop(ShopConfig& shop, bool fresh);
    void FollowShop();
    void LeaveShop();
    void DrawShops(NVGcontext* vg);
    void UpdateShops(u64 down, u64 held);
    std::vector<ShopConfig> m_shops;
    std::unique_ptr<ShopLoader> m_shop_loader;
    int m_shop_focus = 0, m_shop_opening = -1;
    bool m_shop_active = false;   // the library shows a shop
    std::string m_shop_name, m_shop_key;
    // saver mode: dark after a minute without input while installing
    void DrawSaver(NVGcontext* vg);
    double m_last_input = 0;
    bool m_saver_on = false;
    bool m_wake_touch = false;  // the touch that woke the screen does nothing until lifted
    bool m_updates_told = false;
    bool m_dest_touched = false; // the destination was chosen on the page, so "ask each time" does not ask

    void ReadTouch(u64& down);
    void Tap(float x, float y, u64& down);
    void Update(u64 down, u64 held);
    void Draw(NVGcontext* vg);
    void DrawLibrary(NVGcontext* vg);
    void UpdateLibrary(u64 down, u64 held);
    void DrawDetail(NVGcontext* vg);
    void UpdateDetail(u64 down, u64 held);
    void OpenDetail(int view_index);
    void RebuildView();
    std::string FocusedKey() const;
    // moving fast in a long library (5A): the A-Z bar, the right stick by letter, a big letter
    // while jumping, and the Options menu on − (the order, Update all)
    bool AzBarShown() const;
    void JumpToLetter(int slot, int dir); // dir 0: to `slot`; -1 / 1: the letter before / after
    void DrawAzBar(NVGcontext* vg);
    void DrawJumpLetter(NVGcontext* vg);
    void DrawMenu(NVGcontext* vg);
    void UpdateMenu(u64 down, u64 held);
    void ActivateMenu(int row);
    void AskUpdateAll();
    bool m_menu = false;
    int m_menu_focus = 0;
    bool m_az_touch = false;
    int m_jump_letter = -1;
    double m_jump_at = -100;
    std::string m_keep_key;      // the game to keep in focus when the view is rebuilt
    bool m_focus_restored = false;
    void FollowLink();
    void CheckSdSpace();
    void Notice(const std::string& text);
    void DrawPairing(NVGcontext* vg, const Link::View& v);
    void UpdatePairing(u64 down, u64 held, const Link::View& v);
    void ActivatePairRow(int row, const Link::View& v);
    void DrawQueue(NVGcontext* vg);
    void UpdateQueue(u64 down, u64 held);
    void DrawQueueStrip(NVGcontext* vg, float y);
    // The marked files of the page of a game go to the queue; to the internal memory, only after
    // the question (1A).
    void EnqueuePicked();
    void AddJobs(std::vector<InstallJob> jobs);
    // A job for a file of a game of the library.
    InstallJob JobFor(const Game& g, const CatalogFile& f);
    // The newest update of every installed game that has a newer one in the library.
    std::vector<InstallJob> PendingUpdates();
    // The repairs a finished check asks for; false when the library lacks a version.
    bool RepairJobs(const InstallQueue::VerifyView& v, std::vector<InstallJob>& out);
    std::string ContentLabel(const Game& g, const NcmContentMetaKey& key) const;
    // The Installed tab (2A, 3A): what this session installed, and the history.
    void DrawInstalled(NVGcontext* vg);
    void UpdateInstalled(u64 down, u64 held);
    void DrawSession(NVGcontext* vg);
    void DrawHistory(NVGcontext* vg);
    void AskUndo(std::vector<int> jobs);
    // A question over everything, with Cancel in focus first.
    void DrawDialog(NVGcontext* vg);
    void UpdateDialog(u64 down, u64 held);
    void ConfirmDialog();
    // Applies what a finished job installed, and the speed test to the downloads.
    void FollowQueue();
    void DrawSettings(NVGcontext* vg);
    void DrawSpeedPanel(NVGcontext* vg, float x, float y, float w, float h);
    void ActivateSetting(int row);
    void DrawTouchFeedback(NVGcontext* vg);

    // Repeats a held direction like the HOME menu does.
    bool Repeat(u64 down, u64 held, u64 mask);
    // Library grid scrolling.
    void KeepFocusVisible();
    float MaxScroll() const;

    void AddHit(float x, float y, float w, float h, HitKind kind, int index = 0) {
        m_hits.push_back({{x, y, w, h, 0}, kind, index});
    }
    void AddHints(const std::vector<ui::HitRect>& rects);

    Gfx m_gfx;
    PadState m_pad{};
    Theme m_theme = DarkTheme();
    Tab m_tab = Tab::Library;
    int m_focus = 0;
    int m_filter = 0;
    int m_setting = 0; // row in focus in Settings
    Link m_link;
    Catalog m_catalog;
    Covers m_covers;
    std::vector<Game> m_games;
    std::vector<int> m_view;        // games shown: the filter and the search applied
    unsigned m_games_version = 0;
    std::string m_loaded_url;       // the library the catalog and covers come from
    std::string m_search;
    // the page of a game
    bool m_detail = false;
    int m_detail_game = -1;
    int m_detail_focus = 0;
    std::vector<char> m_detail_pick;
    bool m_to_nand = false;
    std::string m_sd_free;
    s64 m_sd_free_bytes = -1;
    s64 m_nand_free_bytes = -1, m_nand_total_bytes = -1;
    double m_sd_checked = -100;
    std::string m_notice;
    double m_notice_until = 0;
    int m_pair_focus = 0; // row in focus on the pairing screen
    std::string m_manual_url; // typed in Settings, until the key is typed too
    SpeedTest m_speed;
    bool m_speed_kept = false;      // the result of the last test went to install.ini
    InstallPrefs m_prefs;
    UiPrefs m_ui;
    Sound m_sound;
    // what is in focus anywhere, to hear a move when it changes
    u64 FocusSignature() const;
    void PlaySounds(u64 down, u64 before);
    float m_settings_scroll = 0;
    // the battery in the top bar, read every few seconds
    void CheckBattery();
    bool m_psm = false;
    int m_battery = -1;
    bool m_charging = false;
    double m_battery_checked = -100;
    // the cover in focus lifts and leans a little (1A); tabs slide in and the page of a game grows
    // out of its cover (1T-A)
    void DrawLiftedCover(NVGcontext* vg, const Game& g, float x, float y, float s);
    void DrawTransitions(NVGcontext* vg);
    int m_lift_focus = -1;
    double m_lift_at = -100;
    float m_lift_dx = 0, m_lift_dy = 0;
    Tab m_shown_tab = Tab::Library;
    double m_tab_at = -100;
    float m_tab_dir = 1;
    bool m_shown_detail = false;
    double m_detail_at = -100;
    // when the queue ends (2A): a soft blue glow with a tick, two gentle pulses of HD rumble, a sound
    void FollowFinish();
    void DrawFinish(NVGcontext* vg);
    void InitRumble();
    void SendRumble(bool on);
    bool m_queue_was_busy = false;
    std::vector<int> m_counted_jobs;   // finished jobs already counted in a batch
    int m_batch_done = 0, m_batch_failed = 0;
    u64 m_batch_bytes = 0;
    double m_finish_at = -100;
    HidVibrationDeviceHandle m_vib_handheld[2]{}, m_vib_dual[2]{}, m_vib_full[1]{};
    bool m_vib_ok[3] = {false, false, false};
    int m_rumble_step = -1;            // the pulse playing, -1 none
    double m_rumble_next = 0;
    std::unique_ptr<InstallQueue> m_queue;
    unsigned m_queue_version = 0;
    int m_queue_focus = 0;          // row in focus on the queue screen
    double m_quit_asked = -100;     // + while installing asks first
    // the Installed tab
    int m_installed_view = 0;       // 0: this session, 1: the history
    int m_installed_focus = 0;
    std::vector<HistoryEntry> m_history;
    unsigned m_history_version = ~0u;
    // the question shown over everything, if any
    enum class DialogKind { None, Nand, Undo, ClearHistory, HistoryInfo, UpdateAll, Verify, Push, Share, Where, Risk, PreCheck, MtpNand, VaultRestore, VaultError, SpaceClean, SpaceArchive, SpaceMove, SpaceCard, SpaceDump, ShopRemove };
    struct Dialog {
        DialogKind kind = DialogKind::None;
        int focus = 0;                  // 0: Cancel (or Close), 1: the action
        std::vector<InstallJob> jobs;   // Nand: what goes to the internal memory
        std::vector<int> undo;          // Undo: the jobs whose install goes; empty: all
        int entry = -1;                 // HistoryInfo: the row of the history
        int game = -1;                  // Verify: the game checked
        bool single = false;            // only Close (or Cancel): no action
        std::vector<std::string> push;  // Push: the ids of what the web sent, to answer
        std::string from;               // Push: who sent it
        PreCheck check;                 // PreCheck: the firmware, the keys and Atmosphère
        LibrarySave save;               // VaultRestore: the copy to bring back
        std::vector<unsigned long long> ids; // Space*: the games, or the content to dump
        std::string text;               // Space*: a name or a size to show; VaultError: its title
        bool to_nand = false;           // SpaceMove: the way it goes
        unsigned long long size = 0;    // SpaceMove: how much moves
    };
    // "Send to console" and the inventory (only when the user said yes)
    std::unique_ptr<Push> m_push;
    std::vector<PushItem> m_pushed;     // arrived, not asked about yet
    unsigned m_inventory_sent = ~0u;    // the queue version the last inventory was read at
    std::string m_inventory_url;
    void FollowPush();
    // emuMMC and 90DNS, read once; the warning comes before the first install of a session
    std::unique_ptr<Safety> m_safety;
    SystemInfo m_sysinfo;           // the firmware and Atmosphère, for the check before installing
    bool m_risk_ok = false;
    std::string m_update_hint;
    Dialog m_dialog;
    double m_time = 0;
    double m_next_repeat = 0;
    double m_held_since = 0;
    u64 m_held_dir = 0;
    bool m_quit = false;

    // Library grid scroll, in pixels: drawn value eases toward the target.
    float m_scroll = 0, m_scroll_target = 0;

    // Touch state.
    std::vector<Hit> m_hits;
    bool m_touching = false;
    bool m_dragging = false;
    float m_touch_x0 = 0, m_touch_y0 = 0, m_touch_x = 0, m_touch_y = 0;
    float m_drag_scroll0 = 0;
    std::vector<ui::HitRect> m_hint_rects;
};

} // namespace nxdock
