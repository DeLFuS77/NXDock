#include "yati/nx/keys.hpp"
#include "yati/nx/nca.hpp"
#include "yati/nx/es.hpp"
#include "yati/nx/crypto.hpp"
#include "defines.hpp"
#include "log.hpp"
#include <memory>
#include <bit>
#include <cstring>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace sphaira::keys {
namespace {

constexpr u8 HEADER_KEK_SRC[0x10] = {
    0x1F, 0x12, 0x91, 0x3A, 0x4A, 0xCB, 0xF0, 0x0D, 0x4C, 0xDE, 0x3A, 0xF6, 0xD5, 0x23, 0x88, 0x2A
};

constexpr u8 HEADER_KEY_SRC[0x20] = {
    0x5A, 0x3E, 0xD8, 0x4F, 0xDE, 0xC0, 0xD8, 0x26, 0x31, 0xF7, 0xE2, 0x5D, 0x19, 0x7B, 0xF5, 0xD0,
    0x1C, 0x9B, 0x7B, 0xFA, 0xF6, 0x28, 0x18, 0x3D, 0x71, 0xF6, 0x4D, 0x73, 0xF1, 0x50, 0xB9, 0xD2
};

} // namespace

void parse_hex_key(void* key, const char* hex) {
    char low[0x11]{};
    char upp[0x11]{};
    std::memcpy(low, hex, 0x10);
    std::memcpy(upp, hex + 0x10, 0x10);
    *(u64*)key = std::byteswap(std::strtoul(low, nullptr, 0x10));
    *(u64*)((u8*)key + 8) = std::byteswap(std::strtoul(upp, nullptr, 0x10));
}

Result parse_keys(Keys& out, bool read_from_file) {
    R_TRY(splCryptoInitialize());
    ON_SCOPE_EXIT(splCryptoExit());

    u8 header_kek[0x20];
    R_TRY(splCryptoGenerateAesKek(HEADER_KEK_SRC, 0, 0, header_kek));
    R_TRY(splCryptoGenerateAesKey(header_kek, HEADER_KEY_SRC, out.header_key));
    R_TRY(splCryptoGenerateAesKey(header_kek, HEADER_KEY_SRC + 0x10, out.header_key + 0x10));

    // NXDock: installing never reads the user's prod.keys (only the header key, from the console).
    // Compress games does (read_from_file): the title keks and key area keys open the title key of
    // a game's ticket. They stay in memory, are never shown, logged or written anywhere.
    out.from_file = false;
    if (read_from_file) {
        for (const char* path : {"sdmc:/switch/prod.keys", "sdmc:/prod.keys"}) {
            FILE* f = std::fopen(path, "r");
            if (!f) continue;
            char line[512];
            while (std::fgets(line, sizeof(line), f)) {
                char* eq = std::strchr(line, '=');
                if (!eq) continue;
                // the name, trimmed
                char* end = eq;
                while (end > line && (end[-1] == ' ' || end[-1] == '\t')) end--;
                *end = 0;
                char* hex = eq + 1;
                while (*hex == ' ' || *hex == '\t') hex++;
                size_t n = 0;
                while (std::isxdigit((unsigned char)hex[n])) n++;
                if (n != 32) continue;
                const auto indexed = [&](const char* prefix, KeySection& section) {
                    const size_t pl = std::strlen(prefix);
                    if (std::strncmp(line, prefix, pl) != 0 || std::strlen(line) != pl + 2) return false;
                    const unsigned long i = std::strtoul(line + pl, nullptr, 16);
                    if (i >= section.size()) return false;
                    parse_hex_key(&section[i], hex);
                    return true;
                };
                if (indexed("titlekek_", out.titlekek) || indexed("master_key_", out.master_key) ||
                    indexed("key_area_key_application_", out.key_area_key[0]) || indexed("key_area_key_ocean_", out.key_area_key[1]) ||
                    indexed("key_area_key_system_", out.key_area_key[2])) {
                    out.from_file = true;
                }
            }
            std::fclose(f);
            if (out.from_file) break;
        }
        log_write("[NXDock] prod.keys: %s\n", out.from_file ? "read (for Compress games)" : "not found in sdmc:/switch/");
    }

    R_SUCCEED();
}

} // namespace sphaira::keys
