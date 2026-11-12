// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include <string>
#include <vector>

namespace nxdock {

// USB disks plugged into the console (FAT32 and exFAT, with libusbhsfs), mounted as ums0:/…
// Started when the file list opens and kept until NXDock closes.
struct UsbDisk {
    std::string mount; // "ums0:/"
    std::string label; // the maker and product, as the disk says
    unsigned long long capacity = 0;
    bool fat32 = false; // FAT12/16/32: no file of 4 GB or more
};

void UsbDisksStart();
void UsbDisksStop();
std::vector<UsbDisk> UsbDisks();

} // namespace nxdock
