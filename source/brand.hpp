// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

// The name and the icon of NXDock are for its official versions only (TRADEMARKS.md): the workflow
// of its repository builds them with the CMake option NXDOCK_OFFICIAL, which defines
// NXDOCK_OFFICIAL=1. A program built from the code without it is called "NXDock (unofficial
// build)" and has a plain icon (assets/unofficial.jpg). The names here are the ones CMakeLists.txt
// gives the NACP.

namespace nxdock::brand {

#if defined(NXDOCK_OFFICIAL) && NXDOCK_OFFICIAL
// built by the workflow of the repository, with the name and the icon of NXDock
inline constexpr bool kOfficial = true;
inline constexpr char kName[] = "NXDock";
// the name of the process of the HOME menu shortcut, in the 15 letters its NPDM holds
inline constexpr char kShortName[] = "NXDock";
#else
inline constexpr bool kOfficial = false;
inline constexpr char kName[] = "NXDock (unofficial build)";
inline constexpr char kShortName[] = "NXDock (unoff.)";
#endif

static_assert(sizeof(kShortName) <= 16, "the NPDM holds 15 letters and the end of the name");

} // namespace nxdock::brand
