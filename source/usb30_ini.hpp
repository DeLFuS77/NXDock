// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

// USB 3.0 in sdmc:/atmosphere/config/system_settings.ini (USB A of the visual test of 14/11):
// Atmosphère turns it on at the next start with "usb30_force_enabled = u8!0x1" under [usb]. Only
// the text is worked on here, so it is tested without a console (usb30.cpp reads and writes it).

#include <string>

namespace nxdock {

// What the file says: -1 nothing (no such line in [usb] that is not commented out), 0 off, 1 on.
int Usb30InIni(const std::string& ini);

// The file with usb30_force_enabled under [usb] set on or off. A line that is there (commented out
// or not) is changed where it is, never added twice; [usb] is added when missing; every other byte
// stays as it was, and new lines end as the file's own do (CRLF or LF).
std::string SetUsb30InIni(const std::string& ini, bool on);

} // namespace nxdock
