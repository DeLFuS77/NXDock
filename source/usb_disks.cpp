// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "usb_disks.hpp"

#include <switch.h>
#include <usbhsfs.h>

namespace nxdock {
namespace {
bool g_started = false;
}

void UsbDisksStart() {
    if (!g_started) {
        g_started = R_SUCCEEDED(usbHsFsInitialize(0));
    }
}

void UsbDisksStop() {
    if (g_started) {
        usbHsFsExit();
        g_started = false;
    }
}

std::vector<UsbDisk> UsbDisks() {
    std::vector<UsbDisk> out;
    if (!g_started) return out;
    UsbHsFsDevice devices[8];
    const u32 n = usbHsFsListMountedDevices(devices, 8);
    for (u32 i = 0; i < n; i++) {
        UsbDisk d;
        d.mount = std::string(devices[i].name) + "/";
        d.label = devices[i].product_name[0] ? devices[i].product_name : devices[i].manufacturer;
        d.capacity = devices[i].capacity;
        out.push_back(d);
    }
    return out;
}

} // namespace nxdock
