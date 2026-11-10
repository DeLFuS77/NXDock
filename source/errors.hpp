// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include <string>

namespace nxdock {

// An error as shown (1A): a short title, what happened, what to do, and the code to look it up.
struct ErrorText {
    std::string title, what, todo, code;
    bool Empty() const { return title.empty() && what.empty(); }
};

// What is known when an install fails, to fill in the figures.
struct ErrorContext {
    unsigned long long needed = 0; // bytes the install needs
    unsigned firmware = 0;          // the system version the content needs (cnmt form)
    unsigned console_firmware = 0;  // the console's, same form
    long long free_bytes = -1;      // free in the storage, -1 unknown
    bool nand = false;              // to the internal memory
    bool install = true;            // an install: "Nothing was installed." is added
};

// The code as the console's error viewer writes it: 2000 + module, then the description.
std::string ErrorCode(unsigned rc);

// The explanation of a result, in the console language.
ErrorText ExplainError(unsigned rc, const ErrorContext& ctx);

// An error is kept in a job and in the history as one line: the four parts with a unit
// separator between them. A line without one (older history) is only "what happened".
std::string PackError(const ErrorText& e);
ErrorText UnpackError(const std::string& line);

// A firmware in the form of the cnmt (major << 26 | minor << 20 | micro << 16) as 15.0.1.
std::string FirmwareString(unsigned version);

} // namespace nxdock
