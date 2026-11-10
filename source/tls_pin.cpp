// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "tls_pin.hpp"

#include <curl/curl.h>
#include <cstring>
#include <mutex>

namespace nxdock::tls {
namespace {

std::mutex g_mutex;
std::string g_pin;

bool IsHttps(const std::string& url) {
    return url.size() >= 8 && (url.compare(0, 8, "https://") == 0 || url.compare(0, 8, "HTTPS://") == 0);
}

// One DER element: its tag, where its content starts and how long it is. False when broken.
bool ReadTlv(const std::vector<uint8_t>& d, size_t at, uint8_t& tag, size_t& start, size_t& len) {
    if (at + 2 > d.size()) return false;
    tag = d[at];
    size_t i = at + 1;
    uint8_t first = d[i++];
    if (first < 0x80) {
        len = first;
    } else {
        const int n = first & 0x7F;
        if (n == 0 || n > 4 || i + n > d.size()) return false;
        len = 0;
        for (int k = 0; k < n; k++) len = (len << 8) | d[i++];
    }
    start = i;
    return start + len <= d.size();
}

// SHA-256 (FIPS 180-4), small and portable: the pin is computed once per pairing.
struct Sha {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t block[64];
    size_t used = 0;
    uint64_t bits = 0;

    static uint32_t Rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
    void Compress() {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
            0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
            0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
            0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
            0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
            0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; i++) {
            w[i] = uint32_t(block[i * 4]) << 24 | uint32_t(block[i * 4 + 1]) << 16 | uint32_t(block[i * 4 + 2]) << 8 | block[i * 4 + 3];
        }
        for (int i = 16; i < 64; i++) {
            const uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            const uint32_t t1 = hh + (Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            const uint32_t t2 = (Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    void Update(const uint8_t* p, size_t n) {
        bits += uint64_t(n) * 8;
        while (n--) {
            block[used++] = *p++;
            if (used == 64) {
                Compress();
                used = 0;
            }
        }
    }
    void Final(uint8_t out[32]) {
        const uint64_t total = bits;
        const uint8_t one = 0x80, zero = 0;
        Update(&one, 1);
        while (used != 56) Update(&zero, 1);
        for (int i = 7; i >= 0; i--) {
            block[used++] = uint8_t(total >> (i * 8));
        }
        Compress();
        for (int i = 0; i < 8; i++) {
            out[i * 4] = uint8_t(h[i] >> 24);
            out[i * 4 + 1] = uint8_t(h[i] >> 16);
            out[i * 4 + 2] = uint8_t(h[i] >> 8);
            out[i * 4 + 3] = uint8_t(h[i]);
        }
    }
};

size_t Discard(char*, size_t size, size_t n, void*) {
    return size * n;
}

} // namespace

void SetPin(const std::string& pin) {
    std::lock_guard lock(g_mutex);
    g_pin = pin;
}

std::string Pin() {
    std::lock_guard lock(g_mutex);
    return g_pin;
}

void Apply(void* handle, const std::string& url) {
    if (!IsHttps(url)) {
        return;
    }
    CURL* curl = static_cast<CURL*>(handle);
    // no authority can vouch for the app's own certificate: the pin does
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    const std::string pin = Pin();
    if (!pin.empty()) {
        curl_easy_setopt(curl, CURLOPT_PINNEDPUBLICKEY, pin.c_str()); // copied by curl
    }
}

std::string ReadPin(const std::string& url, std::string& error) {
    error.clear();
    if (!IsHttps(url)) {
        return "";
    }
    CURL* curl = curl_easy_init();
    if (!curl) {
        error = "curl";
        return "";
    }
    const std::string target = url + "/install/hello";
    curl_easy_setopt(curl, CURLOPT_URL, target.c_str());
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_CERTINFO, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 6000L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Discard);
    std::string pin;
    const CURLcode rc = curl_easy_perform(curl);
    if (rc == CURLE_OK) {
        struct curl_certinfo* info = nullptr;
        if (curl_easy_getinfo(curl, CURLINFO_CERTINFO, &info) == CURLE_OK && info && info->num_of_certs > 0) {
            for (struct curl_slist* s = info->certinfo[0]; s; s = s->next) {
                if (!std::strncmp(s->data, "Cert:", 5)) {
                    pin = PinOfPem(s->data + 5);
                }
            }
        }
        if (pin.empty()) {
            error = "no certificate";
        }
    } else {
        error = curl_easy_strerror(rc);
    }
    curl_easy_cleanup(curl);
    return pin;
}

std::vector<uint8_t> Base64Decode(const std::string& text) {
    std::vector<uint8_t> out;
    uint32_t acc = 0;
    int bits = 0;
    for (const char c : text) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+') v = 62;
        else if (c == '/') v = 63;
        else continue; // line breaks, padding
        acc = (acc << 6) | uint32_t(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(uint8_t(acc >> bits));
        }
    }
    return out;
}

std::string Base64Encode(const uint8_t* data, size_t size) {
    static const char* abc = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < size; i += 3) {
        const uint32_t n = uint32_t(data[i]) << 16 | (i + 1 < size ? uint32_t(data[i + 1]) << 8 : 0) | (i + 2 < size ? data[i + 2] : 0);
        out += abc[(n >> 18) & 63];
        out += abc[(n >> 12) & 63];
        out += i + 1 < size ? abc[(n >> 6) & 63] : '=';
        out += i + 2 < size ? abc[n & 63] : '=';
    }
    return out;
}

std::vector<uint8_t> PemToDer(const std::string& pem) {
    const char* begin = "-----BEGIN CERTIFICATE-----";
    const char* end = "-----END CERTIFICATE-----";
    const size_t a = pem.find(begin);
    if (a == std::string::npos) {
        return {};
    }
    const size_t from = a + std::strlen(begin);
    const size_t b = pem.find(end, from);
    if (b == std::string::npos) {
        return {};
    }
    return Base64Decode(pem.substr(from, b - from));
}

std::vector<uint8_t> SpkiOfCertificate(const std::vector<uint8_t>& d) {
    uint8_t tag;
    size_t start, len;
    // Certificate ::= SEQUENCE { tbsCertificate, ... }
    if (!ReadTlv(d, 0, tag, start, len) || tag != 0x30) return {};
    // TBSCertificate ::= SEQUENCE { [0] version OPTIONAL, serial, signature, issuer, validity, subject, spki, ... }
    if (!ReadTlv(d, start, tag, start, len) || tag != 0x30) return {};
    size_t at = start;
    const size_t tbs_end = start + len;
    int field = 0;
    while (at < tbs_end) {
        size_t cs, cl;
        if (!ReadTlv(d, at, tag, cs, cl)) return {};
        if (field == 0 && tag == 0xA0) {
            at = cs + cl; // the version
            continue;
        }
        if (field == 5) {
            if (tag != 0x30) return {};
            return std::vector<uint8_t>(d.begin() + long(at), d.begin() + long(cs + cl));
        }
        field++;
        at = cs + cl;
    }
    return {};
}

void Sha256(const uint8_t* data, size_t size, uint8_t out[32]) {
    Sha s;
    s.Update(data, size);
    s.Final(out);
}

std::string PinOfPem(const std::string& pem) {
    const auto spki = SpkiOfCertificate(PemToDer(pem));
    if (spki.empty()) {
        return "";
    }
    uint8_t hash[32];
    Sha256(spki.data(), spki.size(), hash);
    return "sha256//" + Base64Encode(hash, sizeof(hash));
}

} // namespace nxdock::tls
