// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The texts of the tools of the study of 11/11: the file list's actions, deleting files after their
// install, NXDock updating itself, what is missing for the games on the console, the details of a
// content, more folders over MTP, more of the saves, USB 3.0 and the help of Settings, in
// source/texts/tools_*.inc; a language that lacks a text takes the English one.
#pragma once

#include "i18n_table.hpp"

namespace nxdock {

struct Tools {
#define TEXT NXDOCK_TEXT_FIELD
#include "texts/tools.inc"
#undef TEXT
};

const Tools& TrT();

} // namespace nxdock
