// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "config.hpp"
#include "discovery.hpp"

namespace nxdock {

// The link with the library app, so nobody types an address or a key:
//  - remembered: at start NXDock checks the app it was paired with, and finds it again by its id
//    if its address changed;
//  - the first time: it looks for the app on the network, picks it when there is only one, asks
//    to be paired and shows the code; once the code is accepted in the app, the key comes by
//    itself and is kept on the SD card when "Remember" is on.
// The work runs on its own thread; the interface reads a snapshot every frame.
class Link {
public:
    enum class State {
        Idle,
        Reconnecting, // checking the app it was paired with
        Searching,    // asking the network
        Choose,       // several apps found, or none: the user picks or types an address
        Asking,       // asking the chosen app to be paired
        Waiting,      // the code is shown, waiting for Accept in the app
        Connected,
        Refused,      // refused in the app
        Expired,      // nobody accepted in time
        Offline,      // paired, but the app does not answer now
        Failed,
    };

    struct View {
        State state = State::Idle;
        float progress = 0;
        std::vector<Library> found;
        int chosen = -1;
        std::string code;     // as shown: ABCD-EFGH
        int seconds_left = 0; // until the code runs out
        std::string url, name, app_name;
        bool remember = true;
        bool enabled = true; // the app installs over the network now
        std::string error;
    };

    ~Link();
    // Starts with what the SD card holds: reconnects when paired, else looks for the app.
    void Start(const Config& config);
    void Search();
    void Choose(int index);
    // An address typed by hand (an app outside home, or behind a proxy).
    void UseAddress(const std::string& url);
    // An address and a key typed by hand, as before pairing existed.
    void UseKey(const std::string& url, const std::string& key);
    void Forget();
    void SetRemember(bool remember);
    void Stop();

    View Snapshot();
    Config CurrentConfig();
    void SaveTheme(bool light);

private:
    void Run(void (Link::*job)());
    void JobReconnect();
    void JobSearch();
    void JobPair();
    // With https: keeps the key of the app the first time (see tls_pin.hpp).
    void TakePin(const std::string& url);
    void JobAddress();
    void Set(State state);
    void Fail(State state, const std::string& error);
    bool CheckKey(const std::string& url, const std::string& key, bool& enabled, std::string& error);

    std::thread m_thread;
    std::atomic<bool> m_cancel{false};
    std::mutex m_mutex;
    View m_view;
    Config m_config;      // what is on the SD card
    Config m_session;     // the link in use (also when not remembered)
    Library m_target;     // the app being paired
    std::string m_typed;  // an address typed by hand
};

} // namespace nxdock
