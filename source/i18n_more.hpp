// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include "i18n_table.hpp"

namespace nxdock {

// The texts of the second roadmap and after (clear errors, the More tab, compressing, In the
// console, the new look), in source/texts/more_*.inc; a text a language lacks is taken from English.
struct More {
#define TEXT NXDOCK_TEXT_FIELD
#include "texts/more.inc"
#undef TEXT
};

const More& TrM();

} // namespace nxdock
