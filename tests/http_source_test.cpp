// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The network source against range_server.py: several connections at once, pieces read in
// order inside the range and small reads outside it, answers cut half way (asked again from
// where they stopped), the key, and cancelling.
#include "http_source.hpp"

#include <curl/curl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace nxdock;

// the core's log goes nowhere in the test
extern "C" void log_write(const char*, ...) {}

namespace {

int g_failed = 0;

void Check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) g_failed++;
}

unsigned char Expected(s64 i) {
    return (unsigned char)((i * 31 + (i >> 8)) & 0xFF);
}

bool Same(const std::vector<unsigned char>& got, s64 off) {
    for (size_t i = 0; i < got.size(); i++) {
        if (got[i] != Expected(off + s64(i))) {
            std::printf("     differs at %lld\n", (long long)(off + s64(i)));
            return false;
        }
    }
    return true;
}

std::string Stats(const std::string& base) {
    std::string out;
    CURL* c = curl_easy_init();
    curl_easy_setopt(c, CURLOPT_URL, (base + "/stats").c_str());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, +[](char* d, size_t s, size_t n, void* u) {
        static_cast<std::string*>(u)->append(d, s * n);
        return s * n;
    });
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &out);
    curl_easy_perform(c);
    curl_easy_cleanup(c);
    return out;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: http_source_test BASE_URL SIZE\n");
        return 2;
    }
    const std::string base = argv[1];
    const s64 size = std::atoll(argv[2]);
    curl_global_init(CURL_GLOBAL_DEFAULT);

    HttpSource::Options opt;
    opt.connections = 4;
    opt.piece = 1024 * 1024;
    opt.ahead = 2;
    opt.give_up_seconds = 10;

    {
        sphaira::ui::ProgressBox box;
        HttpSource src(base + "/install/f/x/file.nsp", "testkey", opt, &box);
        Check(R_SUCCEEDED(src.Open()), "opens and learns the size");
        Check(src.Size() == size, "the size is the one the app gives");

        // small reads outside any range: the PFS0 header and its names
        std::vector<unsigned char> small(0x10);
        u64 got = 0;
        Check(R_SUCCEEDED(src.Read(small.data(), 0, 0x10, &got)) && got == 0x10 && Same(small, 0), "small read at the start");
        small.resize(0x300);
        Check(R_SUCCEEDED(src.Read(small.data(), 0x10, 0x300, &got)) && Same(small, 0x10), "small read next to it");

        // an NCA in the middle, read in order in pieces that do not line up with the 1 MB pieces
        const s64 nca_off = 0x4321, nca_size = size - 0x4321 - 777;
        src.HintRange(nca_off, nca_size);
        bool all = true;
        std::vector<unsigned char> buf;
        for (s64 off = 0; off < nca_size;) {
            const s64 n = std::min<s64>(3 * 1024 * 1024 + 123, nca_size - off);
            buf.resize(size_t(n));
            if (R_FAILED(src.Read(buf.data(), nca_off + off, n, &got)) || s64(got) != n || !Same(buf, nca_off + off)) {
                all = false;
                break;
            }
            off += n;
        }
        Check(all, "the whole range arrives, byte for byte, with answers cut on the way");
        Check(box.NetBytes() >= u64(nca_size), "the bytes from the network are counted");

        // the end of the file, outside the range
        small.resize(777);
        Check(R_SUCCEEDED(src.Read(small.data(), size - 777, 777, &got)) && got == 777 && Same(small, size - 777), "the last bytes");
    }
    const std::string stats = Stats(base);
    int most = 0, requests = 0, cut = 0;
    std::sscanf(stats.c_str(), "%d %d %d", &most, &requests, &cut);
    std::printf("     at most %d connections at once, %d requests, %d cut\n", most, requests, cut);
    Check(most >= 3, "several connections at once");
    Check(cut >= 1, "some answers were cut (and asked again)");

    {
        sphaira::ui::ProgressBox box;
        HttpSource src(base + "/install/f/x/file.nsp", "wrong", opt, &box);
        const Result rc = src.Open();
        Check(rc == Result(Result_ServerRefused), "a wrong key is refused, without retrying for ever");
    }

    {
        sphaira::ui::ProgressBox box;
        HttpSource src(base + "/install/f/x/file.nsp", "testkey", opt, &box);
        src.Open();
        src.HintRange(0, size);
        std::vector<unsigned char> buf(1024 * 1024);
        u64 got = 0;
        src.Read(buf.data(), 0, s64(buf.size()), &got);
        box.RequestCancel();
        const Result rc = src.Read(buf.data(), s64(buf.size()), s64(buf.size()), &got);
        Check(rc == Result(Result_TransferCancelled), "cancelling stops the reads");
    }

    curl_global_cleanup();
    std::printf(g_failed ? "%d failed\n" : "all passed\n", g_failed);
    return g_failed ? 1 : 0;
}
