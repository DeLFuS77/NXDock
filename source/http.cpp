// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "tls_pin.hpp"
#include "http.hpp"

#include <curl/curl.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace nxdock::http {
namespace {

constexpr size_t kMaxBody = 64 * 1024;

struct Sink {
    std::string* body;
    size_t max;
};

size_t OnBody(char* data, size_t size, size_t count, void* user) {
    auto* sink = static_cast<Sink*>(user);
    const size_t n = size * count;
    if (sink->body->size() + n > sink->max) {
        return 0;
    }
    sink->body->append(data, n);
    return n;
}

Response Request(const std::string& url, const std::string* json, const std::string& key, long timeout_ms, size_t max_bytes = kMaxBody) {
    Response r;
    Sink sink{&r.body, max_bytes};
    CURL* curl = curl_easy_init();
    if (!curl) {
        r.error = "curl";
        return r;
    }
    curl_slist* headers = nullptr;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    tls::Apply(curl, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, OnBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, std::min(timeout_ms, 3000L));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "NXDock/" NXDOCK_VERSION);
    if (!key.empty()) {
        curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
        curl_easy_setopt(curl, CURLOPT_USERNAME, "nxdock");
        curl_easy_setopt(curl, CURLOPT_PASSWORD, key.c_str());
    }
    headers = curl_slist_append(headers, "Accept: application/json");
    if (json) {
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json->c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, long(json->size()));
    }
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    const CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &r.status);
    } else {
        r.error = curl_easy_strerror(res);
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return r;
}

} // namespace

Response Get(const std::string& url, const std::string& key, long timeout_ms) {
    return Request(url, nullptr, key, timeout_ms);
}

Response GetLarge(const std::string& url, const std::string& key, long timeout_ms, size_t max_bytes) {
    return Request(url, nullptr, key, timeout_ms, max_bytes);
}

Response PostJson(const std::string& url, const std::string& json, const std::string& key, long timeout_ms) {
    return Request(url, &json, key, timeout_ms);
}

} // namespace nxdock::http
