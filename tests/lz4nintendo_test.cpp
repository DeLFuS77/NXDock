// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// On the computer: the LZ4 compressors of Nintendo make, byte for byte, the blocks the ones of
// the library app make (switchfs/lz4nintendo.go). tests/fixtures/lz4nintendo_golden.txt holds the
// size and FNV-1a hash of each block the library app made of inputs made here again in the same
// way: zeros, noise, repeats, words and runs, a small alphabet, text, a JPEG, tables and runs
// with noise, of 0 to 70,000 bytes, at every kind of level.
// Usage: lz4nintendo_test <golden file> <text file> <jpeg file>
#include "lz4nintendo.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t id) : s((id + 1) * 0x9E3779B97F4A7C15ull) {}
    uint64_t Next() {
        uint64_t x = s;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        s = x;
        return x * 0x2545F4914F6CDD1Dull;
    }
    int Intn(int n) { return int(Next() % uint64_t(n)); }
};

std::vector<uint8_t> g_text, g_jpeg;

std::vector<uint8_t> ReadFile(const char* path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// The inputs of the generator of the golden file, in the same order of draws.
std::vector<uint8_t> Input(int kind, int variant, int size) {
    Rng r(uint64_t(kind) << 40 | uint64_t(size) << 8 | uint64_t(variant));
    std::vector<uint8_t> out;
    out.reserve(size_t(size) + 64);
    switch (kind) {
    case 0:
        out.assign(size_t(size), 0);
        break;
    case 1:
        while (int(out.size()) < size) out.push_back(uint8_t(r.Next()));
        break;
    case 2: {
        const int p = 1 + r.Intn(37);
        std::vector<uint8_t> pattern(static_cast<size_t>(p));
        for (auto& b : pattern) b = uint8_t(r.Next());
        while (int(out.size()) < size) out.insert(out.end(), pattern.begin(), pattern.end());
        break;
    }
    case 3: {
        static const char* const words[] = {"switch ", "game ", "update ", "zelda ", "nintendo ", "\0\0\0\0", "level_", "texture "};
        static const size_t lengths[] = {7, 5, 7, 6, 9, 4, 6, 8};
        while (int(out.size()) < size) {
            switch (r.Intn(4)) {
            case 0: {
                const int w = r.Intn(8);
                out.insert(out.end(), words[w], words[w] + lengths[w]);
                break;
            }
            case 1: {
                const uint8_t b = uint8_t(r.Intn(256));
                out.insert(out.end(), size_t(r.Intn(40)), b);
                break;
            }
            default:
                out.push_back(uint8_t(r.Intn(256)));
            }
        }
        break;
    }
    case 4:
        while (int(out.size()) < size) out.push_back(uint8_t("ACGT"[r.Intn(4)]));
        break;
    case 5:
    case 6: {
        const auto& src = kind == 6 ? g_jpeg : g_text;
        size_t at = size_t(r.Intn(int(src.size())));
        while (int(out.size()) < size) {
            out.push_back(src[at]);
            at = (at + 1) % src.size();
        }
        break;
    }
    case 7:
        for (int i = 0; int(out.size()) < size; i++) {
            const uint32_t v = uint32_t(i * 7 + r.Intn(3));
            for (int k = 0; k < 4; k++) out.push_back(uint8_t(v >> (8 * k)));
            if (r.Intn(5) == 0) out.insert(out.end(), 8, 0);
        }
        break;
    case 8:
        while (int(out.size()) < size) {
            const uint8_t b = uint8_t(r.Next());
            out.insert(out.end(), size_t(r.Intn(300)), b);
            for (int n = r.Intn(20); n > 0; n--) out.push_back(uint8_t(r.Next()));
        }
        break;
    }
    out.resize(size_t(size));
    return out;
}

uint64_t Fnv(const uint8_t* p, size_t n) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: %s golden text jpeg\n", argv[0]);
        return 2;
    }
    g_text = ReadFile(argv[2]);
    g_jpeg = ReadFile(argv[3]);
    if (g_text.empty() || g_jpeg.empty()) {
        std::fprintf(stderr, "the text or the jpeg could not be read\n");
        return 2;
    }
    std::ifstream golden(argv[1]);
    std::vector<uint8_t> work(NX_LZ4_WORK_SIZE);
    std::string line;
    int checked = 0, failed = 0;
    int last_kind = -1, last_variant = -1, last_size = -1;
    std::vector<uint8_t> in;
    while (std::getline(golden, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream fields(line);
        int kind, variant, size, level;
        size_t want_size;
        std::string want_hash;
        fields >> kind >> variant >> size >> level >> want_size >> want_hash;
        if (kind != last_kind || variant != last_variant || size != last_size) {
            in = Input(kind, variant, size);
            last_kind = kind;
            last_variant = variant;
            last_size = size;
        }
        std::vector<uint8_t> out(nx_lz4_bound(in.size()));
        const int64_t n = nx_lz4_compress(in.data(), in.size(), out.data(), out.size(), level, work.data());
        char hash[17] = "-";
        if (n >= 0) std::snprintf(hash, sizeof(hash), "%016llx", (unsigned long long)Fnv(out.data(), size_t(n)));
        bool ok = n >= 0 && size_t(n) == want_size && want_hash == hash;
        // and it decompresses to the input
        std::vector<uint8_t> back(in.size());
        if (ok && nx_lz4_decompress(out.data(), size_t(n), back.data(), back.size()) != 0) ok = false;
        if (ok && back != in) ok = false;
        // a buffer one byte too small is refused, not overrun
        if (ok && n > 0) {
            std::vector<uint8_t> small(size_t(n) - 1);
            if (nx_lz4_compress(in.data(), in.size(), small.data(), small.size(), level, work.data()) != -1) ok = false;
        }
        if (!ok) {
            if (failed < 20) {
                std::printf("FAIL kind %d variant %d size %d level %d: %lld bytes %s, want %zu %s\n", kind, variant, size, level,
                            (long long)n, hash, want_size, want_hash.c_str());
            }
            failed++;
        }
        checked++;
    }
    // levels that are not Nintendo's
    const uint8_t some[16] = {1, 2, 3};
    uint8_t out[64];
    for (int level : {-1, 17, NX_LZ4_LEVEL_FAST4 - 1, 65}) {
        if (nx_lz4_compress(some, sizeof(some), out, sizeof(out), level, work.data()) != -1) {
            std::printf("FAIL level %d is not one of Nintendo's\n", level);
            failed++;
        }
    }
    // damaged blocks are refused
    {
        const auto in2 = Input(3, 0, 65536);
        std::vector<uint8_t> block(nx_lz4_bound(in2.size()));
        const int64_t n = nx_lz4_compress(in2.data(), in2.size(), block.data(), block.size(), 0, work.data());
        std::vector<uint8_t> back(in2.size());
        if (n < 4 || nx_lz4_decompress(block.data(), size_t(n) - 3, back.data(), back.size()) == 0 ||
            nx_lz4_decompress(block.data(), size_t(n), back.data(), back.size() - 1) == 0) {
            std::printf("FAIL a damaged block was decompressed\n");
            failed++;
        }
    }
    if (checked < 6000) {
        std::printf("FAIL only %d blocks in the golden file\n", checked);
        failed++;
    }
    std::printf("%s %d blocks made as the library app makes them\n", failed ? "FAIL" : "ok  ", checked - failed);
    return failed ? 1 : 0;
}
