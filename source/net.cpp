// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// Socket buffer sizes follow Sphaira's main.cpp, which measured them on the console.
#include "net.hpp"

#include <curl/curl.h>

namespace nxdock::net {
namespace {

bool g_socket = false;
bool g_curl = false;
int g_rx_max = 0;

} // namespace

bool IsAppletMode() {
    const AppletType type = appletGetAppletType();
    return type != AppletType_Application && type != AppletType_SystemApplication;
}

bool Init() {
    SocketInitConfig config = *socketGetDefaultInitConfig();
    if (IsAppletMode()) {
        config.tcp_tx_buf_size = 1024 * 64;
        config.tcp_rx_buf_size = 1024 * 64;
        config.tcp_tx_buf_max_size = 1024 * 256;
        config.tcp_rx_buf_max_size = 1024 * 256;
        config.sb_efficiency = 4;
    } else {
        config.tcp_tx_buf_size = 1024 * 64;
        config.tcp_rx_buf_size = 1024 * 64;
        config.tcp_tx_buf_max_size = 1024 * 1024 * 4;
        config.tcp_rx_buf_max_size = 1024 * 1024 * 4;
        config.sb_efficiency = 8;
    }
    config.num_bsd_sessions = 8; // room for several connections at once
    g_rx_max = int(config.tcp_rx_buf_max_size);
    g_socket = R_SUCCEEDED(socketInitialize(&config));
    if (!g_socket) {
        // fall back to the defaults rather than having no network at all
        g_socket = R_SUCCEEDED(socketInitializeDefault());
        g_rx_max = int(socketGetDefaultInitConfig()->tcp_rx_buf_max_size);
    }
    g_curl = g_socket && curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    return g_curl;
}

void Exit() {
    if (g_curl) {
        curl_global_cleanup();
        g_curl = false;
    }
    if (g_socket) {
        socketExit();
        g_socket = false;
    }
}

int ReceiveBuffer() {
    return g_rx_max;
}

Boost::Boost() {
    appletSetCpuBoostMode(ApmCpuBoostMode_FastLoad);
    appletSetAutoSleepDisabled(true);
    appletSetMediaPlaybackState(true); // the screen does not dim during a long transfer
}

Boost::~Boost() {
    appletSetMediaPlaybackState(false);
    appletSetAutoSleepDisabled(false);
    appletSetCpuBoostMode(ApmCpuBoostMode_Normal);
}

} // namespace nxdock::net
