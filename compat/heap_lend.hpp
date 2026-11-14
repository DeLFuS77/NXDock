// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The allocator of zstd for the stream of an NCZ. An NSZ made with a long window (128 MiB, the
// most libzstd takes by default) needs that window in one block. Opened from the Album, NXDock
// has a heap of about 260 MiB that its screens break into pieces, and no block that big is left;
// DBI installs such files there, so NXDock must too.
//
// From the Album the loader keeps about 134 MiB of the applet memory for other applets (its
// applet_heap_reservation_size), which an install does not open. When a block of
// 16 MiB or more is not in the heap, the process heap grows past its end for the time zstd holds
// the block (svcSetHeapSize: the new memory follows the heap, so it is one block), and shrinks
// back as soon as zstd frees it.
#pragma once

#include <cstddef>

namespace nxdock::heap {

void* Alloc(void* opaque, size_t size);
void Free(void* opaque, void* address);

} // namespace nxdock::heap
