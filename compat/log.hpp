// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// NXDock's stand-in for Sphaira's log.hpp: the yati core writes its notes through log_write,
// which goes to sdmc:/switch/NXDock/install.log while an install runs (see install_log.cpp).
// Usable from C (nxdumptool) and C++.
#pragma once

#include <stdarg.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void log_write(const char* s, ...) __attribute__((format(printf, 1, 2)));
void log_write_arg(const char* s, va_list* v);

#ifdef __cplusplus
}
#endif
