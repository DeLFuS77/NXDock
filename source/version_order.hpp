// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

// The order of NXDock's versions: 1.2.3, and betas such as 1.2.3-beta.4, which come after the
// version before them and before their own version (1.2.2 < 1.2.3-beta.1 < 1.2.3-beta.2 < 1.2.3).

#include <array>
#include <climits>
#include <cstdlib>
#include <string>

namespace nxdock::version {

// The three numbers and the number of the beta; INT_MAX in its place for a version.
inline std::array<int, 4> Parts(std::string v) {
    if (!v.empty() && (v[0] == 'v' || v[0] == 'V')) v.erase(0, 1);
    std::array<int, 4> out{0, 0, 0, INT_MAX};
    size_t i = 0;
    for (int k = 0; k < 3 && i < v.size(); k++) {
        if (v[i] < '0' || v[i] > '9') break;
        out[k] = std::atoi(v.c_str() + i);
        while (i < v.size() && v[i] >= '0' && v[i] <= '9') i++;
        if (i < v.size() && v[i] == '.') i++;
        else break;
    }
    const size_t beta = v.find("-beta.");
    if (beta != std::string::npos) out[3] = std::atoi(v.c_str() + beta + 6);
    else if (v.find('-') != std::string::npos) out[3] = 0; // another pre-release: before any beta
    return out;
}

inline bool IsBeta(const std::string& v) { return Parts(v)[3] != INT_MAX; }

// a newer than b
inline bool Newer(const std::string& a, const std::string& b) { return Parts(a) > Parts(b); }

} // namespace nxdock::version
