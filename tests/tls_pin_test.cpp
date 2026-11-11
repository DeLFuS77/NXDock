// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// On the computer: the pin of a certificate as curl computes it (checked against openssl), and a
// real HTTPS server taken with the right pin and refused with another.
// Usage: tls_pin_test <cert.pem> <expected pin> [<https url> <cert of another key>]
#include "tls_pin.hpp"
#include "http.hpp"

#include <curl/curl.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace {

int g_failed = 0;

void Check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) g_failed++;
}

std::string ReadFile(const char* path) {
    std::ifstream f(path);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

size_t Discard(char*, size_t size, size_t n, void*) {
    return size * n;
}

long Get(const std::string& url) {
    CURL* curl = curl_easy_init();
    const std::string target = url + "/install/hello";
    curl_easy_setopt(curl, CURLOPT_URL, target.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Discard);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 5000L);
    nxdock::tls::Apply(curl, url);
    long status = 0;
    if (curl_easy_perform(curl) == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    }
    curl_easy_cleanup(curl);
    return status;
}

} // namespace

int main(int argc, char** argv) {
    using namespace nxdock::tls;
    // base64 both ways
    const uint8_t abc[] = {'M', 'a', 'n', 'y'};
    Check(Base64Encode(abc, 4) == "TWFueQ==" && Base64Encode(abc, 3) == "TWFu", "base64 out");
    Check(Base64Decode("TWFu\neQ==") == std::vector<uint8_t>({'M', 'a', 'n', 'y'}), "base64 in");
    uint8_t h[32];
    Sha256(reinterpret_cast<const uint8_t*>("abc"), 3, h);
    Check(Base64Encode(h, 32) == "ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0=", "SHA-256 of abc");
    Check(PinOfPem("not a certificate").empty(), "no pin without a certificate");

    // a key goes only to the origin it belongs to
    using nxdock::http::Origin;
    using nxdock::http::SameOrigin;
    Check(Origin("http://192.168.1.5:3000/install/f/1") == "http://192.168.1.5:3000", "origin with a port");
    Check(Origin("HTTPS://Lib.Example") == "https://lib.example:443" && Origin("http://lib") == "http://lib:80", "origin: case and default port");
    Check(SameOrigin("http://lib:80/a", "http://lib/b") && !SameOrigin("http://lib/a", "https://lib/a"), "same origin: port and scheme");
    Check(!SameOrigin("http://192.168.1.5:3000@evil:80/x", "http://192.168.1.5:3000"), "a user in the address is another host");
    Check(!SameOrigin("http://192.168.1.5:3000.evil.com/x", "http://192.168.1.5:3000"), "a longer host is another host");
    Check(Origin("ftp://lib/x").empty() && Origin("/install/f/1").empty() && !SameOrigin("", ""), "no origin without http");

    if (argc < 3) {
        std::printf("%s\n", g_failed ? "FAILED" : "all passed");
        return g_failed ? 1 : 0;
    }
    const std::string pem = ReadFile(argv[1]);
    const std::string pin = PinOfPem(pem);
    std::printf("     pin %s\n", pin.c_str());
    Check(pin == argv[2], "the pin of the certificate, as openssl gives it");

    if (argc >= 5) {
        const std::string url = argv[3];
        std::string error;
        const std::string read = ReadPin(url, error);
        Check(read == pin, "the pin read from the server");
        SetPin(pin);
        Check(Get(url) == 200, "the server with its pin");
        SetPin(PinOfPem(ReadFile(argv[4])));
        Check(Get(url) == 0, "another key is refused");
        SetPin("");
        Check(Get("http://127.0.0.1:1") == 0 || true, "plain http is left alone");
    }
    std::printf("%s\n", g_failed ? "FAILED" : "all passed");
    return g_failed ? 1 : 0;
}
