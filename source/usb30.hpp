// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

// USB 3.0 for MTP and NS-USBloader (USB A of the visual test of 14/11): the setting in use, what
// system_settings.ini says (it changes at once, the setting only after a reboot), the speed of the
// cable, writing the file with a copy kept first, and the reboot.

namespace nxdock {

struct Usb30State {
    int active = -1; // the setting in use (setsys): -1 unknown, 0 off, 1 on
    int file = -1;   // what system_settings.ini says: -1 nothing, 0 off, 1 on
    bool FileOn() const { return file == 1; }
    bool ActiveOn() const { return active == 1; }
};
Usb30State ReadUsb30();

// The speed the cable runs at now: 0 not known, 2 USB 2.0 (or slower), 3 USB 3.0. Only while MTP or
// the USB of Tinfoil holds the cable (both run on usb:ds).
int UsbSpeedNow();

// system_settings.ini with the setting on or off: a copy (system_settings.ini.nxdock-bak) first
// when the file is there, then the new file written beside it and put in its place. `backed`:
// the copy was made. False when nothing could be written; the file is then as it was.
bool WriteUsb30(bool on, bool& backed);

// Reboots the console; false when it could not be asked.
bool RebootConsole();

} // namespace nxdock
