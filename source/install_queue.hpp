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
#include "catalog.hpp"

namespace sphaira::ui {
class ProgressBox;
}

namespace nxdock {

// One file of the library to install: a game, an update or a DLC.
struct InstallJob {
    enum class State { Waiting, Running, Done, Failed, Cancelled };

    int id = 0;
    std::string game, label, cover, color_key; // as shown: the game, what of it, its picture
    std::string file_id, url, key;             // where it comes from (url is the whole address)
    std::string format;
    CatalogFile::Kind kind = CatalogFile::Kind::Game;
    u64 title_id = 0;
    u64 size = 0;
    State state = State::Waiting;
    std::string error;  // why it failed, as shown
    double seconds = 0; // how long it took
    u64 written = 0;    // bytes installed
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

private:
    void Run();
    void RunJob(InstallJob& job);

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

// The message shown for a failed install, in the console language.
std::string InstallErrorText(unsigned rc, unsigned long long needed, unsigned firmware, long long free_bytes);

} // namespace nxdock
