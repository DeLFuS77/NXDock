// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include "mtp_proto.hpp"
#include "space.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace nxdock {

// Where the folder of the games installed reads them from (the Installed tab's list, on its thread).
void MtpSetInstalled(std::function<std::vector<InstalledApp>()> installed);
// The folders of 8A after the usual ones, as `units` says (UiPrefs::mtp_units): 1 the games
// installed, 2 the saves, 4 the Album, 8 a USB disk. All read only.
void MtpAddMore(std::vector<std::shared_ptr<mtp::Storage>>& storages, int units);
// The Album unmounted and the zips of the saves taken away.
void MtpMoreStop();

} // namespace nxdock
