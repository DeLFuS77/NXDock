// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace nxdock {

// A library app found on the network.
struct Library {
    std::string id;      // the same every time, whatever its address
    std::string name;
    std::string version;
    std::string url;     // http://address:port
    bool enabled = false; // installs over the network now
};

// The answer of /install/hello, read; false when it is not the library app.
bool ReadHello(const std::string& body, const std::string& url, Library& out);

// Asks the local network where the library app is: a UDP message first (an answer comes in a
// moment when the app can hear it), then, when nobody answered, its usual ports on every address
// of the network, which also finds an app in Docker. `progress` goes from 0 to 1.
std::vector<Library> Discover(const std::atomic<bool>& cancel, const std::function<void(float)>& progress);

// The address of the console on the network, as text ("" without network).
std::string ConsoleAddress();

} // namespace nxdock
