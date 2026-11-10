// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "space.hpp"

#include <algorithm>
#include <numeric>

namespace nxdock {

std::vector<int> PlanArchive(const std::vector<InstalledApp>& apps, unsigned long long needed, bool nand) {
    std::vector<int> order(apps.size());
    std::iota(order.begin(), order.end(), 0);
    const auto bytes = [&](int i) { return nand ? apps[size_t(i)].nand_bytes : apps[size_t(i)].sd_bytes; };
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return bytes(a) > bytes(b); });
    std::vector<int> out;
    unsigned long long freed = 0;
    for (const int i : order) {
        if (freed >= needed) break;
        if (apps[size_t(i)].archived || bytes(i) == 0) continue;
        out.push_back(i);
        freed += bytes(i);
    }
    // the largest alone may be enough: then not a list of many small ones before it
    if (freed < needed) return {};
    return out;
}

} // namespace nxdock
