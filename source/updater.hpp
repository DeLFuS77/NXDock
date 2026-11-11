// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// NXDock updating itself (5A of the visual test of the new tools): the newest release on GitHub,
// its NXDock.nro downloaded, SHA256SUMS.txt checked with the minisign signature of the project
// (the keys of release.pub, built in) and with "NXDock <version>" as its trusted comment, then the
// .nro checked against its SHA-256 and put in place of the one running, which is kept as
// NXDock.nro.old to go back to. Nothing is changed when anything does not match.
#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace nxdock {

class Updater {
public:
    enum class Stage { Idle, Checking, Newer, Latest, Downloading, Done, Failed };
    enum class Error { None, Network, NoRelease, NoKeys, Signature, Sum, Write };
    struct View {
        Stage stage = Stage::Idle;
        Error error = Error::None;
        std::string version; // the newest one, as its tag says it ("0.9.1")
        std::string notes;   // its first lines of news, one per line
        unsigned long long size = 0, done = 0;
    };

    ~Updater();
    // Asks GitHub for the newest release, on a thread of its own.
    void Check();
    // Downloads the newer one found, checks it and puts it in place of `nro`, on a thread.
    void Install(const std::string& nro);
    View Snapshot();

    // Whether this build has the keys of the releases (release.pub had them when it was built).
    static bool HasKeys();
    // Whether version `a` ("1.2.3", a "v" before it is fine) is newer than `b`.
    static bool Newer(const std::string& a, const std::string& b);
    // The copy kept from before the last update, and putting it back in place (swapped).
    static bool HasOld(const std::string& nro);
    static bool RollBack(const std::string& nro);
    // The news of a release body: its first lines of a list, without marks, up to `lines` of them.
    static std::string Notes(const std::string& body, int lines = 4);
    // The SHA-256 of a file named in a SHA256SUMS.txt, in hex; empty when it is not there.
    static std::string SumOf(const std::string& sums, const std::string& name);

private:
    void Join();
    void Set(Stage stage, Error error = Error::None);
    std::mutex m_mutex;
    std::thread m_thread;
    View m_view;
    std::string m_nro_url, m_sums_url, m_sig_url;
    std::atomic<unsigned long long> m_done{0};
};

} // namespace nxdock
