// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include <cstddef>
#include <cstring>

namespace nxdock {

// Of a library file: an NSP/NSZ (PFS0) or an XCI/XCZ (game card image), from its first bytes.
// The app names a file by what it holds, but an XCZ it repacks comes as an NSP/NSZ: the bytes
// decide, not the name.
enum class ContainerKind { Unknown, Nsp, Xci };

constexpr size_t kContainerHeadSize = 0x1104;

inline ContainerKind DetectContainer(const unsigned char* head, size_t size) {
    if (size >= 4 && !std::memcmp(head, "PFS0", 4)) {
        return ContainerKind::Nsp;
    }
    // the card header "HEAD" at 0x100, or at 0x1100 when the key area comes first
    if (size >= 0x104 && !std::memcmp(head + 0x100, "HEAD", 4)) {
        return ContainerKind::Xci;
    }
    if (size >= 0x1104 && !std::memcmp(head + 0x1100, "HEAD", 4)) {
        return ContainerKind::Xci;
    }
    return ContainerKind::Unknown;
}

} // namespace nxdock
