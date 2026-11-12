// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once
#include <future>
#include <set>
#include <thread>

#include <switch.h>
#include <memory>
#include <vector>
#include "catalog.hpp"
#include "file_source.hpp"
#include "fileops.hpp"
#include "updater.hpp"
#include "versions_list.hpp"
#include "forwarder.hpp"
#include "config.hpp"
#include "covers.hpp"
#include "gfx.hpp"
#include "history.hpp"
#include "install_queue.hpp"
#include "link.hpp"
#include "precheck.hpp"
#include "mods.hpp"
#include "shop.hpp"
#include "space.hpp"
#include "sdcheck_nx.hpp"
#include "queue_report.hpp"
#include "resume.hpp"
#include "compress_nx.hpp"
#include "vault.hpp"
#include "push.hpp"
#include "sound.hpp"
#include "safety.hpp"
#include "speedtest.hpp"
#include "motion.hpp"
#include "theme.hpp"
#include "ui.hpp"

namespace nxdock {

// The colour of a cover without a picture and its initial (app.cpp).
unsigned PlaceholderColor(const std::string& key);
std::string Initial(const std::string& name);
// A version as people say it: "Update 3" (the number of the update), "No updates" for 0.
std::string VersionText(u32 version);
// An error where there is little room (the pairing, the speed test): its title in `color`, what to
// do (or `todo` when it says nothing) and the code, small. Returns the height used.
float CompactError(NVGcontext* vg, const Theme& t, float x, float y, float w, const std::string& packed, NVGcolor color, const char* todo);

enum class Tab { Library, Shops, Queue, Installed, More, Settings };

// The title ID and the version in a file name ("Game [0100…][v65536].nsz"); false without an ID.
bool TitleFromName(const std::string& name, u64& id, u32& version);
// What a title ID is: 0 a game, 1 its update, 2 a DLC.
int TitleKind(u64 id);
inline constexpr int kTabs = 6;

class App {
public:
    bool Init();
    void Exit();
    // Returns false when the user asks to leave.
    bool Frame();

private:
    // Everything on screen that answers to touch, rebuilt every frame.
    enum class HitKind { Button, Tab, Chip, Cover, QueueStrip, SettingsRow, PairRow, Remember, DetailRow, Dest, Install, QueueRow,
                         InstalledView, InstalledRow, ConsoleSize, ShopChip, FilesDest, DialogBack, DialogButton, MenuRow, MenuBack, WizItem, WizButton, WizMusic,
                         MoreTile, MorePlace, MoreRow, MoreMark, MoreSide, VaultGame, VaultCopy, SpaceAction, PickRow, ShopRow, ModRow, SdChoice, DialogLine, VersionChip, CompressRow, Wifi, SearchChip,
                         OpenBubble, QuickRow, QuickBack,
                         FilesMenuRow, FilesMenuBack, FilesView, FilesAfter, ConsoleMissing, MtpUnit, VaultKind, VaultMenuRow };
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
    // `ink`: its colour (the blue of work when none)
    void DrawTileIcon(NVGcontext* vg, int tile, float x, float cy, const NVGcolor* ink = nullptr);
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
    // the file list's tools (1A to 4A of the test of the new tools, more_files.cpp): every file or the
    // games only, marking folders and ranges, a menu of actions on −, a clipboard, the work of a
    // copy, a move or a delete on its own thread, and deleting files once they install well
    struct FilesRow {
        int action;          // FilesAction
        std::string label, right;
        bool enabled;
    };
    enum FilesAction { kFaInstall, kFaView, kFaCopy, kFaCut, kFaPaste, kFaForget, kFaRename, kFaFolder, kFaProps, kFaDelete };
    std::vector<int> FilesChosen() const;                 // the marked rows, or the one in focus
    std::vector<std::string> FilesChosenPaths() const;
    std::vector<FilesRow> FilesMenuRows();
    void DrawFilesMenu(NVGcontext* vg);
    void UpdateFilesMenu(u64 down, u64 held);
    void FilesDo(int action);
    void FilesMarkMove(int from, int to);                // a range marked while Y is held
    void StartFilesWork(int kind, std::vector<std::string> paths, const std::string& to);
    void FollowFilesWork();
    bool FilesWorkBusy() const { return m_fwork != nullptr; }
    void ReopenFolder(const std::string& focus_name = {});
    void LongPress(float x, float y);
    // the texts and choices of the questions of the new tools (more_files.cpp and the others)
    struct ToolLine {
        std::string text, right;
        bool warn = false;
    };
    struct ToolDialog {
        std::string title, body, note, primary, error, cancel;
        std::vector<ToolLine> lines;
        bool can = true, single = false;
        bool bar = false, running = false;
        float frac = 0;
        std::string bar_text;
    };
    bool ToolDialogText(ToolDialog& d);       // false: nothing to show (the question closes)
    bool ToolDialogInput(u64 down, u64 held); // true: the question took the input
    void ConfirmToolDialog();
    bool m_files_all = false;          // every file, not only the games
    bool m_files_menu = false;
    int m_files_menu_focus = 0;
    bool m_files_after = false;        // delete the files once they install well (off each time it opens)
    int m_mark_anchor = -1;            // Y held: where the range began, and what it sets
    char m_mark_value = 1;
    std::vector<std::string> m_clip;   // copied or cut, to paste
    bool m_clip_move = false;
    std::string m_clip_from;
    unsigned long long m_clip_bytes = 0;
    std::unique_ptr<fileops::Progress> m_fwork;
    std::thread m_fwork_thread;
    int m_fwork_kind = 0;              // 0 delete, 1 copy, 2 move
    std::string m_fwork_to, m_fwork_place;
    bool m_fwork_split = false;
    double m_fwork_at = 0;
    std::string m_files_refocus;       // the name to keep in focus when the folder is read again
    double m_touch_at = 0;             // when the finger came down, for a long press (m_long_fired is shared with the covers)
    void OpenFilesMenu();
    std::vector<fileops::GameFile> m_files_found; // the games under what is chosen, read when the menu opens
    fileops::Count m_files_count;                  // what is chosen holds, read then too
    ToolDialog m_tool;                             // a question of the new tools, prepared when it opens
    // NXDock updating itself (5A, more_update.cpp)
    void FollowSelfUpdate();
    void AskSelfUpdate();
    bool UpdaterDialogText(ToolDialog& d);
    bool UpdaterDialogInput(u64 down);
    void ConfirmUpdater();
    std::unique_ptr<Updater> m_updater;
    bool m_update_silent = false;   // the check at start: only a notice when a newer one is out
    bool m_update_has_old = false;  // NXDock.nro.old is there, to go back to
    double m_update_old_at = -100, m_update_done_at = -100;
    bool m_update_started = false;
    // what is missing for the games on the console (6A) and the details of a content (7A), more_console.cpp
    struct Missing {
        unsigned update = 0;     // a newer update than the one installed, 0 none
        bool update_lib = false; // the library has it (or one newer than what is installed)
        int dlc = 0, dlc_known = 0, dlc_lib = 0; // DLC not installed, all known, the missing ones the library has
        bool Any() const { return update || dlc; }
    };
    const Missing& MissingOf(unsigned long long app);
    void FollowMissing(const Space::View& v);
    Space::View ConsoleView(const Space::View& v); // only those missing something, when the filter is on
    void OpenContentDetails(const InstalledApp& a, const InstalledApp::Content& c);
    void ConsoleTicketBack();
    void ConsoleVersionReset();
    std::unique_ptr<VersionsList> m_versions;
    std::map<unsigned long long, Missing> m_missing;
    std::string m_missing_key;    // what the missing were worked out from
    bool m_console_missing = false; // the filter: only the games missing something
    // more folders over MTP (8A, more.cpp): the one in focus, and a switch turned
    int m_mtp_unit = 0;
    void ToggleMtpUnit(int unit);
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
    // more of the saves (9A): the kind, the copies of the SD card, the menu of −, the automatic copies
    struct VaultRow {
        bool sd = false;
        LibrarySave lib;
        Vault::SdCopy copy;
        long long When() const { return sd ? copy.when : lib.when; }
    };
    std::vector<VaultRow> VaultRows();
    void DrawVaultMenu(NVGcontext* vg);
    void VaultMenuDo(int row);
    void ConfirmSaveDelete();
    void FollowSaveAuto();
    int m_vault_kind = 0;
    bool m_vault_menu = false;
    int m_vault_menu_focus = 0;
    bool m_save_auto_started = false, m_vault_auto_running = false;
    // space and dumps (8A, more_space.cpp)
    unsigned long long QueueNeedsOnSd();
    void OpenSpace();
    bool m_space_dumps = false; // Space shows the dumps only (their own tile of More)
    void OpenPicker(int kind); // 0 archive (several), 1 move, 2 dump
    void OpenPlanner();
    void FollowSpace();
    void DrawSpace(NVGcontext* vg);
    void DrawPicker(NVGcontext* vg, const Space::View& v);
    void UpdateSpace(u64 down, u64 held);
    void SpaceDialogText(std::string& title, std::string& body, std::string& primary);
    void ConfirmSpaceDialog();
    std::unique_ptr<Space> m_space;
    // the check of the SD card (phase 17), a screen of Space and dumps (m_space_mode 2)
    void OpenSdCheck();
    void DrawSdCheck(NVGcontext* vg);
    void UpdateSdCheck(u64 down, u64 held);
    std::unique_ptr<SdChecker> m_sdcheck;
    int m_sd_choice = 0;
    bool m_sd_seen_done = true;
    double m_sd_opened = 0, m_sd_result_at = -100;
    long long m_sdc_free = -1, m_sdc_total = -1;
    // compress games, experimental (phase 24), a screen of Space and dumps (m_space_mode 3)
    // Long lists: left and right jump to the previous or next first letter; the letter shows big
    // for a moment.
    int ListJump(const std::vector<std::string>& names, int focus, int dir);
    void DrawListLetter(NVGcontext* vg, float cx, float cy);
    std::string m_list_letter;
    double m_list_letter_at = -100;
    void OpenCompress();
    std::vector<int> CompressRows(const Space::View& v) const;
    void DrawCompress(NVGcontext* vg);
    void UpdateCompress(u64 down, u64 held);
    void FollowCompress(const Compressor::View& c);
    void CompressDialogText(std::string& title, std::string& body, std::string& note, std::string& primary);
    void ConfirmCompressDialog();
    std::unique_ptr<Compressor> m_compress;
    Sigpatches m_sigpatches;
    bool m_prodkeys = false; // prod.keys on the SD card, for Compress games
    double m_compress_opened = 0;
    int m_compress_focus = 0, m_compress_finished = 0;
    unsigned long long m_compress_app = 0; // the game in focus: kept when the order changes
    bool m_compress_all = false;     // the list shows every game, or only those that can be compressed
    bool m_compress_started = false; // the check of every game began
    bool m_compress_told = false;    // the notice of an update not merged was shown (once)
    std::vector<int> CompressVisible(const Space::View& v, const Compressor::View& c) const;
    std::vector<CompressedGame> m_compressed_marks;
    bool m_marks_read = false;
    int m_space_focus = 0, m_space_mode = 0, m_pick_kind = 0, m_pick_focus = 0, m_pick_first = 0, m_space_finished = 0;
    std::vector<std::pair<int, int>> m_pick_rows; // the game, and its content (dump) or -1
    std::vector<char> m_pick_marks;
    void SortPickRows(const Space::View& v); // by name, the focus kept on its row, as names arrive
    std::string m_plan_head;
    // shops of the kind of Tinfoil and Ownfoil (9A, more_shop.cpp)
    void OpenShops();
    bool EditShop(ShopConfig& shop, bool fresh);
    void FollowShop();
    void LeaveShop();
    // the Shops tab: the list of shops, or the games of the one open with a pill for each above
    void EnterShopsTab();
    void LeaveShopsTab();
    void OpenShopAt(int index);
    void DrawShopChips(NVGcontext* vg);
    bool GridTab() const { return m_tab == Tab::Library || m_tab == Tab::Shops; }
    std::vector<Game> m_shop_cache; // the games of the shop open, kept while another tab is shown
    int m_shop_cached = -1;
    void DrawShops(NVGcontext* vg);
    void UpdateShops(u64 down, u64 held);
    std::vector<ShopConfig> m_shops;
    std::unique_ptr<ShopLoader> m_shop_loader;
    int m_shop_focus = 0, m_shop_opening = -1;
    bool m_shop_active = false;   // the library shows a shop
    std::string m_shop_name, m_shop_key, m_shop_url;
    // cheats and mods (10A, more_mods.cpp)
    void OpenMods();
    void OpenModsGame(unsigned long long app_id, const std::string& name);
    void DrawMods(NVGcontext* vg);
    void UpdateMods(u64 down, u64 held);
    struct Cheats {
        ~Cheats();
        std::thread thread;
        std::mutex mutex;
        bool done = false, found = false;
        std::vector<std::string> names;
        std::string build, text;
    };
    Cheats m_cheats;
    std::vector<ModPackage> m_mods;
    unsigned long long m_mods_app = 0;
    std::string m_mods_name;
    // the screen off while installing (11A, screen_off.cpp)
    void AskScreenOff();
    // the shortcut on the HOME menu (Settings): asked first, then made, updated or taken away
    void AskHomeShortcut(bool remove);
    void DoHomeShortcut(bool remove);
    void ScreenOn();
    bool FollowScreenOff(u64& down, bool new_touch);
    void DrawScreenOff(NVGcontext* vg);
    double m_screen_off_at = -100; // the countdown started
    // the animation pass: when the dialog, the notice, a list or a screen of More appeared
    double m_dialog_at = -100, m_notice_at = -100, m_more_at = -100, m_list_at = -100;
    ui::Glide m_glide_list, m_glide_side;
    int m_more_seen = -1; // the screen of More last drawn, to start its entrance
    std::vector<float> m_tile_lift; // how much each tile of More is lifted
    bool m_screen_off = false, m_lbl = false, m_screen_busy_before = false;
    unsigned m_mods_version = 0;
    int m_mods_game = -1, m_mods_pick = 0, m_mods_side = 0, m_mods_focus = 0;
    bool m_mods_from_detail = false; // opened from the page of a game: B goes back there
    bool m_detail_from_console = false; // the page of a game opened from In the console: B goes back there
    // saver mode: dark after a minute without input while installing
    void DrawSaver(NVGcontext* vg);
    double m_last_input = 0;
    bool m_saver_on = false;
    bool m_wake_touch = false;  // the touch that woke the screen does nothing until lifted
    bool m_updates_told = false;
    bool m_dest_touched = false; // the destination was chosen on the page, so "ask each time" does not ask

    void ReadTouch(u64& down);
    // true when the tap landed on something
    bool Tap(float x, float y, u64& down);
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
    double m_rstick_at = -100;    // the right stick last moved the library: its click is not the WiFi then
    std::string m_keep_key;      // the game to keep in focus when the view is rebuilt
    bool m_focus_restored = false;
    void FollowLink();
    void CheckSdSpace();
    void Notice(const std::string& text);
    void NoticeOk(const std::string& text); // in green, with a tick: something was done
    void DrawPairing(NVGcontext* vg, const Link::View& v);
    // Without network: said in large, in place of what needs it (the pairing, the library).
    void DrawNoNetwork(NVGcontext* vg, const char* retry);
    void UpdatePairing(u64 down, u64 held, const Link::View& v);
    void ActivatePairRow(int row, const Link::View& v);
    void DrawQueue(NVGcontext* vg);
    void UpdateQueue(u64 down, u64 held);
    void DrawQueueStrip(NVGcontext* vg, float y);
    float StripKey(NVGcontext* vg, float qy);
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
    void DrawConsole(NVGcontext* vg, const Space::View& v);
    void DrawConsolePage(NVGcontext* vg, const Space::View& v);
    void DrawConsoleGrid(NVGcontext* vg, const Space::View& v, bool library);
    void InstalledBadge(NVGcontext* vg, const Game& g, float x, float y, float s);
    // Where it installs, in large: the SD card and the internal memory side by side, each with its
    // free space; `kind` is the touch that chooses one (Dest for a game, FilesDest for the files).
    void DestCards(NVGcontext* vg, float x, float y, float w, float h, bool nand, unsigned long long need, HitKind kind);
    double m_installed_read_at = -100;
    // the files of the SD card or a USB disk, read before they are queued (PreflightFiles)
    std::future<std::vector<InstallJob>> m_preflight;
    bool m_preflight_dropped = false;
    void FollowPreflight(); // when the Library last read what is installed
    Tab m_tab_seen = Tab::Library; // the tab of the last frame: the Library reads what is installed again
    void UpdateConsole(u64 down, u64 held);
    void ReadConsole();
    const Game* LibraryGame(unsigned long long app) const;
    std::string ContentLabel(const InstalledApp::Content& c, unsigned long long app) const;
    void DrawHistory(NVGcontext* vg);
    void AskUndo(std::vector<int> jobs);
    // A question over everything, with Cancel in focus first.
    void DrawDialog(NVGcontext* vg);
    void UpdateDialog(u64 down, u64 held);
    void ConfirmDialog();
    // A third button some questions have between Cancel and the action (-1 none), and its use.
    int DialogExtra(u64& mask) const;
    void DialogExtraChosen();
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

    // The list on screen that a finger drags (set while drawing): each row dragged moves its focus
    // by one, as the pad does; taps keep working.
    void ListArea(float x, float y, float w, float h, float pitch) {
        m_list_area = {x, y, w, h, 0};
        m_list_pitch = pitch;
    }
    void ListDragStarts();
    ui::HitRect m_list_area{};
    float m_list_pitch = 0;
    bool m_list_drag = false;
    int m_drag_steps = 0, m_drag_pending = 0;

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
    int m_detail_first = 0;          // the first row of its list in sight
    std::vector<char> m_detail_pick;
    bool m_to_nand = false;
    std::string m_sd_free;
    s64 m_sd_free_bytes = -1;
    s64 m_nand_free_bytes = -1, m_nand_total_bytes = -1;
    double m_sd_checked = -100;
    std::string m_notice;
    double m_notice_until = 0;
    bool m_notice_ok = false;
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
    // the WiFi in the top bar (1A): its coverage read every few seconds; a tap or the right stick
    // opens a card with the network, the coverage and the addresses
    void CoverLoading(NVGcontext* vg, float x, float y, float size, const std::string& path);
    void CheckWifi();
    void DrawWifiCard(NVGcontext* vg);
    bool m_nifm = false;
    int m_wifi = -2; // 0..3, -1 without network, -2 not read yet
    bool m_wifi_cable = false;
    std::string m_wifi_ssid;
    u32 m_wifi_ip = 0;
    double m_wifi_checked = -100;
    bool m_wifi_open = false;
    ui::HitRect m_wifi_rect{}, m_wifi_card{};
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
    void SendRumble(bool on, float strength = 1.f);
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
    int m_installed_view = 0;       // 0: in the console, 1: this session, 2: the history
    // In the console: every game installed, and the page of one with what it has
    unsigned long long m_console_app = 0; // the game whose page is open (0: the list)
    int m_console_focus = 0, m_console_item = 0;
    std::vector<char> m_console_marks;
    std::vector<InstalledApp::Content> m_console_remove; // what the question takes off
    std::set<unsigned long long> m_console_picked;       // games marked with X, to take off together
    std::set<unsigned long long> m_console_removing;     // games being taken off: dimmed meanwhile
    std::vector<HistoryEntry> m_console_done;             // what goes in the history when it is done
    std::string m_console_done_text;                      // the green notice then
    void AskConsoleRemoveMany(const Space::View& v);
    void PickedText(const Space::View& v, char* out, size_t size);
    std::vector<ui::Hint> ConsoleHints();
    unsigned m_console_scanned_for = ~0u;                 // the version of the queue last read for
    int m_installed_focus = 0;
    std::vector<HistoryEntry> m_history;
    unsigned m_history_version = ~0u;
    // the question shown over everything, if any
    enum class DialogKind { None, Nand, Undo, ClearHistory, HistoryInfo, UpdateAll, Verify, Push, Share, Where, Risk, PreCheck, MtpNand, VaultRestore, VaultError, SaveCopyAsk, ModsAsk, Versions, Downgrade, WebCancel, Resume, CompressIntro, CompressGo, CompressUndo, CompressInfo, SpaceClean, SpaceArchive, SpaceMove, SpaceCard, SpaceDump, ShopRemove, ConsoleRemove, Reinstall, Preflight, DlcNeeds, QueueCancel, Quit, Forget, ResumeDiscard, HomeAdd, HomeRemove,
                            FilesDelete, FilesWork, FilesProps, SelfUpdate, SelfBack, ContentDetails, SaveDelete };
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
    void FollowQueueAsk(); // a copy of the saves that failed: go on? (18A)
    // the queue told to the library app (22A) and its end to the phone (21A)
    void FollowQueueWeb();
    std::string QueueJson();
    std::unique_ptr<QueueReport> m_report;
    double m_report_at = -100;
    bool m_report_busy = false;
    std::vector<int> m_report_jobs;
    std::vector<QueueCommand> m_web_cancels; // cancels the web asked for, asked about one by one
    // an install cut by the console going off, found when NXDock opens (23A)
    void CheckResume();
    bool m_resume_checked = false;
    std::unique_ptr<ResumeState> m_resume;
    // the versions of a game the library has, to go back to an older one (20A)
    void OpenVersions();
    std::vector<int> VersionRows(const Game& g) const; // its updates, the newest first
    unsigned long long m_mods_after = 0; // after its update ends, Cheats and mods opens for it (19A)
    std::string m_mods_after_name;
    // emuMMC and 90DNS, read once; the warning comes before the first install of a session
    std::unique_ptr<Safety> m_safety;
    forwarder::Status m_home; // where NXDock is and whether its HOME menu shortcut is there
    SystemInfo m_sysinfo;           // the firmware and Atmosphère, for the check before installing
    bool m_risk_ok = false;
    std::string m_update_hint;
    Dialog m_dialog;
    DialogKind m_dialog_drawn = DialogKind::None; // to start its entrance
    double m_time = 0;
    double m_next_repeat = 0;
    double m_held_since = 0;
    u64 m_held_dir = 0;
    bool m_quit = false;

    // Library grid scroll, in pixels: drawn value eases toward the target.
    float m_scroll = 0, m_scroll_target = 0;

    // The new look (11/11, all the recommended options of the visual test)
    // 1A: the cover of the game in focus, made small and soft, behind everything
    void DrawBackdrop(NVGcontext* vg, const Game& g, bool at_once);
    int m_bd_img[3] = {0, 0, 0};         // three images: the one shown, the one fading out, a spare
    bool m_bd_has[3] = {false, false, false};
    unsigned m_bd_color[3] = {0, 0, 0}; // the colour of a cover without a picture
    int m_bd_front = -1, m_bd_back = -1;
    std::string m_bd_key, m_bd_want;     // the cover shown, and the one asked for
    double m_bd_want_at = -100, m_bd_at = -100;
    // 2A / 2B: the game in focus, in a band under the covers or in a panel on the right
    void DrawFocusBand(NVGcontext* vg, const Game& g, bool pill);
    void DrawFocusPanel(NVGcontext* vg, const Game& g, bool pill);
    void FocusPills(NVGcontext* vg, const Game& g, float x, float y, float right, bool wrap);
    // 3A: the queue at work, as a floating pill (nothing when the queue is empty)
    bool QueueAtWork() const;
    void DrawQueuePill(NVGcontext* vg, float x, float y, float w);
    double m_pill_at = -100;
    bool m_pill_shown = false;
    // 4A: the opening (the icon assembles itself, then the covers come in)
    void DrawBoot(NVGcontext* vg);
    double m_boot_at = -100;
    // 9A: the covers come in as a wave when the filter changes; the filter pill glides
    double m_wave_at = -100;
    ui::Glide m_chip_x, m_chip_w;
    // 10A: the last game installed falls into the dock
    std::string m_finish_cover, m_finish_key, m_finish_name;
    // 14A: the shape of the library while it is read the first time
    void DrawSkeleton(NVGcontext* vg, const char* line, float frac, bool known);
    // 15A: what a finger presses sinks and a wave comes out of the finger
    ui::HitRect m_press_rect{};
    HitKind m_press_kind = HitKind::Button;
    int m_press_cover = -1;          // the cover pressed, in the view
    bool m_press_live = false;       // still under the finger and not dragged
    double m_press_at = -100, m_release_at = -100;
    // 16A: the grid goes on after the finger with its speed, and gives a little at its ends
    float m_fling_v = 0, m_drag_v = 0, m_touch_y_prev = 0, m_touch_x_prev = 0, m_drag_vx = 0;
    double m_touch_t_prev = 0, m_scroll_seen_at = -100;
    bool m_fling_end_told = false;
    float m_list_fling = 0, m_list_fling_d = 0, m_list_fling_pitch = 0;
    // 17A: a very short vibration when a touch does something
    void TouchTick();
    double m_tick_off_at = -1;
    // 18A: a cover held opens its quick menu
    std::vector<int> QuickRows() const; // 0 install to the SD card, 1 its page, 2 check, 3 cheats and mods
    void OpenQuick(int view_index);
    void UpdateQuick(u64 down, u64 held);
    void ActivateQuick(int row);
    void DrawQuick(NVGcontext* vg);
    int m_quick = -1, m_quick_focus = 0;
    double m_quick_at = -100;
    bool m_long_fired = false;
    // 19A: a cover tapped once shows a bubble that opens it
    bool m_touch_focus = false;
    double m_touch_focus_at = -100;
    // 20A: sliding sideways changes the tab (or goes back from a page)
    bool InSubScreen() const;
    bool m_swipe = false;
    float m_swipe_dx = 0, m_swipe_back = 0, m_tab_from = 0, m_tab_from_next = 0;
    double m_swipe_back_at = -100;
    // 21A: pulling the library down reads it again
    bool m_refreshing = false;
    double m_refresh_at = -100;
    s64 m_sd_total_bytes = -1;

    // Touch state.
    std::vector<Hit> m_hits;
    bool m_touching = false;
    bool m_dragging = false;
    float m_touch_x0 = 0, m_touch_y0 = 0, m_touch_x = 0, m_touch_y = 0;
    float m_drag_scroll0 = 0;
    std::vector<ui::HitRect> m_hint_rects;
};

} // namespace nxdock
