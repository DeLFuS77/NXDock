// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace nxdock::http {

struct Response {
    long status = 0;   // 0: no answer
    std::string body;
    std::string error; // why there was no answer
    long retry_after = 0; // seconds the app asks to wait (Retry-After), 0 when not said
};

// The origin of an address, "scheme://host:port" in lower case with the port of the scheme when
// none is given; empty when it is not http or https, or carries a user ("http://a@b/"): a key
// is sent only to the origin it belongs to.
std::string Origin(const std::string& url);
bool SameOrigin(const std::string& a, const std::string& b);

// A short request to the library app; the key, when given, goes as the password of basic
// authentication, as DBI sends it. Bodies are small JSON answers, cut at 64 KB.
Response Get(const std::string& url, const std::string& key = "", long timeout_ms = 4000);
// A larger answer: the catalog, a cover. Cut at max_bytes.
// The answer may come compressed with gzip (curl inflates it; max_bytes counts the inflated body).
// `cancel`, when given, stops the transfer as soon as it turns true.
Response GetLarge(const std::string& url, const std::string& key, long timeout_ms, size_t max_bytes,
                  const std::atomic<bool>* cancel = nullptr);
Response PostJson(const std::string& url, const std::string& json, const std::string& key = "", long timeout_ms = 4000);

// Large bodies, sent or received in pieces, for saves and dumps. No time limit while bytes keep
// moving; a minute without any stops it. `progress` gets the bytes so far and returns false to
// stop (a cancel).
// Sends `size` bytes that `read(buffer, max)` gives (0 at the end, -1 on error), with POST or PUT.
Response Send(const std::string& method, const std::string& url, const std::string& key, uint64_t size,
              const std::function<long(char*, size_t)>& read, const std::function<bool(uint64_t)>& progress = {});
// Gives the body of a GET to `write` (false stops), up to `max_bytes`.
Response Receive(const std::string& url, const std::string& key, uint64_t max_bytes,
                 const std::function<bool(const char*, size_t)>& write, const std::function<bool(uint64_t)>& progress = {});

// Reads a field of a flat JSON object: a string, a number or a boolean. Enough for the
// answers of the app, which never nest the fields NXDock reads.
std::string JsonString(const std::string& json, const std::string& field);
long long JsonNumber(const std::string& json, const std::string& field, long long fallback = 0);
bool JsonBool(const std::string& json, const std::string& field, bool fallback = false);
std::string JsonQuote(const std::string& text);

// The user and password of basic authentication for a key: the console key goes as the password
// of the user "nxdock"; a shop's key is "user", a unit separator, then "password".
void SplitKey(const std::string& key, std::string& user, std::string& pass);

} // namespace nxdock::http
