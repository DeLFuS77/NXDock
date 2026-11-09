// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include <switch.h>

namespace nxdock {

// Takes one content NXDock installed off the console, as Sphaira deletes one: its NCAs and its
// entry in the ncm database of its storage, then the record of its game, which keeps the rest
// (what was there before). Tickets stay: they are tiny and other content may need them.
// Result_GameRunning when the content belongs to the game NXDock was opened from.
Result RemoveInstalled(const NcmContentMetaKey& key, NcmStorageId storage);

// The program NXDock runs in: the game it was opened from with R, or the Album.
u64 OwnProgramId();

// Free bytes of a storage, from ncm; -1 when it does not answer.
s64 StorageFreeSpace(NcmStorageId storage);

} // namespace nxdock
