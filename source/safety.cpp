// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "safety.hpp"

#ifdef __SWITCH__
#include <switch.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace nxdock {

bool IsBlockedAddress(unsigned char a, unsigned char b, unsigned char c, unsigned char d) {
    if (a == 127 || a == 0) {
        return true; // a block: nowhere
    }
    // 90DNS answers the connection test of the console with its own server, and the rest nowhere
    return (a == 95 && b == 216 && c == 149 && d == 205) || (a == 163 && b == 172 && c == 141 && d == 219) ||
           (a == 207 && b == 246 && c == 121 && d == 77);
}

#ifdef __SWITCH__

Safety::Safety() {
    m_thread = std::thread([this] { Run(); });
}

Safety::~Safety() {
    m_thread.join();
}

void Safety::Run() {
    // Atmosphère: the configuration item of exosphere that says the emuMMC type (0: none)
    if (R_SUCCEEDED(splInitialize())) {
        u64 type = 0;
        if (R_SUCCEEDED(splGetConfig(SplConfigItem(65007), &type))) {
            m_emummc = type != 0 ? Answer::Yes : Answer::No;
        }
        splExit();
    }
    // a few names the console asks for; all must lead nowhere
    static const char* kNames[] = {"conntest.nintendowifi.net", "ctest.cdn.nintendo.net", "atum.hac.lp1.d4c.nintendo.net",
                                   "sun.hac.lp1.d4c.nintendo.net"};
    bool reachable = false, answered = false;
    for (const char* name : kNames) {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        addrinfo* res = nullptr;
        if (getaddrinfo(name, nullptr, &hints, &res) != 0 || !res) {
            answered = true; // no address: blocked
            continue;
        }
        answered = true;
        for (addrinfo* r = res; r; r = r->ai_next) {
            const auto* in = reinterpret_cast<const sockaddr_in*>(r->ai_addr);
            const auto* ip = reinterpret_cast<const unsigned char*>(&in->sin_addr.s_addr);
            if (!IsBlockedAddress(ip[0], ip[1], ip[2], ip[3])) {
                reachable = true;
            }
        }
        freeaddrinfo(res);
    }
    if (answered) {
        m_blocked = reachable ? Answer::No : Answer::Yes;
    }
    m_done = true;
}

#endif

} // namespace nxdock
