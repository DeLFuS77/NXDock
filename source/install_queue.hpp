// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <switch.h>
#include "catalog.hpp"
#include "container_kind.hpp"
#include "verify.hpp"

namespace sphaira::ui {
class ProgressBox;
}

namespace nxdock {

// One file of the library to install: a game, an update or a DLC.
struct InstallJob {
    enum class State { Waiting, Running, Done, Failed, Cancelled };
    // where the file comes from: the library app, the SD card or a USB disk, the computer over
    // the USB cable (NS-USBloader), or the computer over MTP
    enum class Origin { Library, File, Usb, Mtp };

    int id = 0;
    std::string game, label, cover, color_key; // as shown: the game, what of it, its picture
    std::string file_id, url, key;             // where it comes from (url is the whole address)
    std::string format;
    Origin origin = Origin::Library;
    std::vector<std::string> parts; // File: the parts of the file in order; Usb and Mtp: its name
    CatalogFile::Kind kind = CatalogFile::Kind::Game;
    u64 title_id = 0;
    u64 size = 0;
    bool nand = false; // to the internal memory (asked first, on the page of the game)
    u32 required_system = 0; // the firmware it needs, from the library (cnmt form), 0 unknown
    bool checked = false;     // the check before installing was shown for it (2A)
    bool firmware_ok = false; // the user chose to install it although the firmware is older
    std::vector<NcmContentId> repair; // a repair: these damaged parts go, and only what is missing comes
    State state = State::Waiting;
    std::string error;  // why it failed, as shown
    double seconds = 0; // how long it took
    u64 written = 0;    // bytes installed
    u64 saved = 0;      // bytes not downloaded: the console had them already (16A)
};

// What NXDock installed since it was opened, for "Undo what was installed in this session".
struct SessionItem {
    enum class State { Installed, Removing, Removed, Failed };

    int job = 0;
    std::string game, label, format, cover, color_key;
    bool nand = false;
    u64 size = 0;                        // bytes it took
    long long when = 0;                  // when it was installed, seconds since 1970
    std::vector<NcmContentMetaKey> keys; // what it registered
    State state = State::Installed;
    std::string error;                   // why it could not be removed
};

// The install queue: one job at a time, on its own thread, while the interface goes on.
// Every job is all or nothing (see yati.cpp); a cancelled or failed one leaves nothing behind.
class InstallQueue {
public:
    // What the running job does now, for the four stages of the queue screen.
    struct Live {
        bool running = false;
        int job = 0;
        int phase = 0;         // ui::ProgressBox::Phase
        u64 net = 0;           // bytes from the network
        u64 source_size = 0;   // bytes of the file in the library
        u64 ready = 0;         // bytes decompressed (or passed on) and hashed
        u64 written = 0;       // bytes written to the SD card
        u64 total = 0;         // bytes the install writes, once known
        u32 checked = 0;       // NCAs whose SHA-256 matched
        u32 ncas = 0;          // NCAs to install
        u64 saved = 0;         // bytes the console had already (16A)
        int threads = -1;      // decompression threads: 3 for NCZBLOCK, 1 solid, 0 none, -1 unknown
        int connections = 0;
        double net_rate = 0, ready_rate = 0, write_rate = 0; // MB/s
        double elapsed = 0;
    };

    InstallQueue();
    ~InstallQueue();

    // How many connections each download uses (from the speed test), and the download-only check
    // for an emulator, set from the config.
    void SetOptions(int connections, bool download_test);

    // Adds a job; false if the same file is already waiting or installing.
    bool Add(InstallJob job);
    // Takes a waiting job out, or cancels the one installing.
    void Remove(int id);
    void Clear(); // the finished ones
    // Puts a failed or cancelled job back in the queue.
    void Retry(int id);

    std::vector<InstallJob> Jobs();
    Live GetLive();
    bool Busy();
    int Waiting();
    // Changes when a job ends: the interface reads what is installed again then.
    unsigned Version() const { return m_version.load(); }
    std::map<u64, InstalledState> Installed();

    // What was installed in this session, the latest first.
    std::vector<SessionItem> Session();
    // Removes what was installed in this session (those of jobs, or all when empty), on the
    // queue's thread; false while something installs or waits.
    bool StartUndo(std::vector<int> jobs = {});
    bool Undoing();

    // Checks every part of a game on the queue's thread; false while something installs.
    struct VerifyView {
        bool running = false, done = false;
        u64 bytes_done = 0, bytes = 0;
        u32 parts_done = 0, parts = 0;
        Result rc = 0;
        std::vector<VerifiedContent> result;
    };
    bool StartVerify(u64 app_id);
    VerifyView GetVerify();
    void CancelVerify();

private:
    void Run();
    void RunJob(InstallJob& job);
    void RunUndo();
    void RunVerify();

    std::thread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_quit = false;
    std::vector<InstallJob> m_jobs;
    int m_next_id = 1;
    int m_connections = 4;
    bool m_download_test = false;
    std::atomic<unsigned> m_version{0};
    std::map<u64, InstalledState> m_installed;
    std::vector<SessionItem> m_session;
    std::vector<int> m_undo_jobs;
    bool m_undo = false, m_undoing = false;
    u64 m_verify_app = 0;
    bool m_verify = false, m_verifying = false, m_verify_done = false;
    Result m_verify_rc = 0;
    VerifyProgress m_verify_progress;
    std::vector<VerifiedContent> m_verify_result;

    // the running job
    sphaira::ui::ProgressBox* m_box = nullptr;
    int m_running = 0;
    u64 m_source_size = 0;
    double m_started = 0;
    // rates, sampled by GetLive
    double m_sample_t = 0;
    u64 m_sample_net = 0, m_sample_ready = 0, m_sample_written = 0;
    double m_rate_net = 0, m_rate_ready = 0, m_rate_written = 0;
};

// The install or check was cancelled by the user.
bool IsCancelled(unsigned rc);

// The message shown for a failed install, in the console language.
// Packed as errors.hpp says (PackError): title, what happened, what to do and the code.
std::string InstallErrorText(unsigned rc, unsigned long long needed, unsigned firmware, long long free_bytes, bool nand = false,
                             bool install = true);

} // namespace nxdock
