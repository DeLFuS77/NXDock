// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "tls_pin.hpp"
#include "http.hpp"

#include <curl/curl.h>
#include <algorithm>
#include <cctype>
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
        static thread_local std::string user, pass;
        SplitKey(key, user, pass);
        curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
        curl_easy_setopt(curl, CURLOPT_USERNAME, user.c_str());
        curl_easy_setopt(curl, CURLOPT_PASSWORD, pass.c_str());
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

struct Stream {
    const std::function<long(char*, size_t)>* read = nullptr;
    const std::function<bool(const char*, size_t)>* write = nullptr;
    const std::function<bool(uint64_t)>* progress = nullptr;
    uint64_t done = 0, max = 0;
    std::string* body = nullptr; // a short answer to a send
};

size_t OnRead(char* data, size_t size, size_t count, void* user) {
    auto* s = static_cast<Stream*>(user);
    const long n = (*s->read)(data, size * count);
    if (n < 0) return CURL_READFUNC_ABORT;
    s->done += uint64_t(n);
    if (s->progress && *s->progress && !(*s->progress)(s->done)) return CURL_READFUNC_ABORT;
    return size_t(n);
}

size_t OnWrite(char* data, size_t size, size_t count, void* user) {
    auto* s = static_cast<Stream*>(user);
    const size_t n = size * count;
    if (s->body) {
        if (s->body->size() + n <= kMaxBody) s->body->append(data, n);
        return n;
    }
    if (s->done + n > s->max || !(*s->write)(data, n)) return 0;
    s->done += n;
    if (s->progress && *s->progress && !(*s->progress)(s->done)) return 0;
    return n;
}

CURL* Prepare(const std::string& url, const std::string& key) {
    CURL* curl = curl_easy_init();
    if (!curl) return nullptr;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    tls::Apply(curl, url);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "NXDock/" NXDOCK_VERSION);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 512L * 1024);
    if (!key.empty()) {
        static thread_local std::string user, pass;
        SplitKey(key, user, pass);
        curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
        curl_easy_setopt(curl, CURLOPT_USERNAME, user.c_str());
        curl_easy_setopt(curl, CURLOPT_PASSWORD, pass.c_str());
    }
    return curl;
}

Response Finish(CURL* curl, CURLcode res) {
    Response r;
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &r.status);
    } else {
        r.error = curl_easy_strerror(res);
    }
    curl_easy_cleanup(curl);
    return r;
}

} // namespace

void SplitKey(const std::string& key, std::string& user, std::string& pass) {
    const auto sep = key.find('');
    if (sep == std::string::npos) {
        user = "nxdock";
        pass = key;
    } else {
        user = key.substr(0, sep);
        pass = key.substr(sep + 1);
    }
}

Response Send(const std::string& method, const std::string& url, const std::string& key, uint64_t size,
              const std::function<long(char*, size_t)>& read, const std::function<bool(uint64_t)>& progress) {
    CURL* curl = Prepare(url, key);
    if (!curl) return {0, {}, "curl"};
    std::string body;
    Stream s;
    s.read = &read;
    s.progress = &progress;
    s.body = &body;
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, OnRead);
    curl_easy_setopt(curl, CURLOPT_READDATA, &s);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, OnWrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    curl_slist* headers = curl_slist_append(nullptr, "Content-Type: application/octet-stream");
    headers = curl_slist_append(headers, "Expect:");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    if (method == "PUT") {
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
        curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, curl_off_t(size));
    } else {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, curl_off_t(size));
    }
    const CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    Response r = Finish(curl, res);
    r.body = std::move(body);
    return r;
}

Response Receive(const std::string& url, const std::string& key, uint64_t max_bytes,
                 const std::function<bool(const char*, size_t)>& write, const std::function<bool(uint64_t)>& progress) {
    CURL* curl = Prepare(url, key);
    if (!curl) return {0, {}, "curl"};
    Stream s;
    s.write = &write;
    s.progress = &progress;
    s.max = max_bytes;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, OnWrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    return Finish(curl, curl_easy_perform(curl));
}

std::string Origin(const std::string& url) {
    std::string lower;
    for (char c : url) lower += char(std::tolower(static_cast<unsigned char>(c)));
    std::string scheme, port;
    size_t at;
    if (lower.rfind("http://", 0) == 0) {
        scheme = "http";
        port = "80";
        at = 7;
    } else if (lower.rfind("https://", 0) == 0) {
        scheme = "https";
        port = "443";
        at = 8;
    } else {
        return "";
    }
    const size_t end = lower.find_first_of("/?#", at);
    std::string authority = lower.substr(at, end == std::string::npos ? std::string::npos : end - at);
    // a user in the address would send the key to another host than the one it seems
    if (authority.empty() || authority.find('@') != std::string::npos || authority.find('\\') != std::string::npos) return "";
    std::string host = authority;
    const size_t colon = authority.rfind(':');
    if (colon != std::string::npos && authority.find(']', colon) == std::string::npos) {
        host = authority.substr(0, colon);
        port = authority.substr(colon + 1);
        if (port.empty() || port.find_first_not_of("0123456789") != std::string::npos) return "";
    }
    return scheme + "://" + host + ":" + port;
}

bool SameOrigin(const std::string& a, const std::string& b) {
    const std::string x = Origin(a);
    return !x.empty() && x == Origin(b);
}

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
