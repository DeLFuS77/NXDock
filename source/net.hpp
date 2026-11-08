// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include <switch.h>
#include <string>

namespace nxdock::net {

// Opened as an application (from a game with R held, or a forwarder) NXDock has plenty of
// memory and takes large TCP buffers; as an applet (from the Album) it has to keep them small.
bool IsAppletMode();

// Sockets with buffers sized for the mode, and curl. Call once at start and at exit.
bool Init();
void Exit();

// The largest TCP receive buffer asked for in this mode.
int ReceiveBuffer();

// Keeps the CPU at its fast clock and the console awake while a transfer runs.
class Boost {
public:
    Boost();
    ~Boost();
    Boost(const Boost&) = delete;
    Boost& operator=(const Boost&) = delete;
};

} // namespace nxdock::net
