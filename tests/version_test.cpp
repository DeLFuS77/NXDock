// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
// The order of versions and betas, as updating itself compares them.
#include "version_order.hpp"

#include <cstdio>

namespace v = nxdock::version;

static int failed = 0;
static void Check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL %s\n", what);
        failed++;
    }
}

int main() {
    Check(v::Newer("0.9.1", "0.9.0"), "a patch is newer");
    Check(v::Newer("1.0.0", "0.9.9"), "a major is newer");
    Check(v::Newer("0.10.0", "0.9.0"), "numbers, not letters");
    Check(!v::Newer("0.9.0", "0.9.0"), "the same is not newer");
    Check(v::Newer("0.9.0-beta.1", "0.8.5"), "a beta comes after the version before it");
    Check(v::Newer("0.9.0", "0.9.0-beta.3"), "a version comes after its betas");
    Check(v::Newer("0.9.0-beta.2", "0.9.0-beta.1"), "betas in order");
    Check(v::Newer("0.9.0-beta.10", "0.9.0-beta.9"), "beta numbers, not letters");
    Check(v::Newer("1.0.0-beta.1", "0.9.0"), "the next beta after a version");
    Check(!v::Newer("0.9.0-beta.1", "0.9.0"), "a beta is not newer than its version");
    Check(v::Newer("v0.9.1", "0.9.0"), "a v before the numbers");
    Check(v::IsBeta("0.9.0-beta.1") && !v::IsBeta("0.9.0"), "which is a beta");
    if (failed) return 1;
    std::printf("version_test: ok\n");
    return 0;
}
