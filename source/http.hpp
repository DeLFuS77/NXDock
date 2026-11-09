// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include <cstddef>
#include <string>

namespace nxdock::http {

struct Response {
    long status = 0;   // 0: no answer
    std::string body;
    std::string error; // why there was no answer
};

// A short request to the library app; the key, when given, goes as the password of basic
// authentication, as DBI sends it. Bodies are small JSON answers, cut at 64 KB.
Response Get(const std::string& url, const std::string& key = "", long timeout_ms = 4000);
// A larger answer: the catalog, a cover. Cut at max_bytes.
Response GetLarge(const std::string& url, const std::string& key, long timeout_ms, size_t max_bytes);
Response PostJson(const std::string& url, const std::string& json, const std::string& key = "", long timeout_ms = 4000);

// Reads a field of a flat JSON object: a string, a number or a boolean. Enough for the
// answers of the app, which never nest the fields NXDock reads.
std::string JsonString(const std::string& json, const std::string& field);
long long JsonNumber(const std::string& json, const std::string& field, long long fallback = 0);
bool JsonBool(const std::string& json, const std::string& field, bool fallback = false);
std::string JsonQuote(const std::string& text);

} // namespace nxdock::http
