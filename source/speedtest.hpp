// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace nxdock {

// Measures the network alone (the app's /install/speedtest, nothing written) with 1, 2 and 4
// connections at once, and then the SD card alone (a temporary file, removed after).
// Runs on its own thread; the interface reads a snapshot every frame.
class SpeedTest {
public:
    enum class Stage { Idle, Net1, Net2, Net4, Sd, Done, Failed };

    struct View {
        Stage stage = Stage::Idle;
        float progress = 0;     // of the current stage
        double live = 0;        // MB/s right now
        double net[3] = {0, 0, 0}; // 1, 2 and 4 connections
        double sd = 0;
        std::string error;
    };

    ~SpeedTest();
    void Start(const std::string& url, const std::string& key);
    void Cancel();
    bool Running() const { return m_running.load(); }
    View Snapshot();

private:
    void Run(std::string url, std::string key);
    double NetStage(const std::string& url, const std::string& key, int connections, Stage stage, int slot);
    double SdStage();
    void Set(Stage stage, float progress, double live);
    void Fail(const std::string& error);

    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_cancel{false};
    std::mutex m_mutex;
    View m_view;
};

} // namespace nxdock
