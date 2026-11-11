// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "shop.hpp"

#include <yyjson.h>
#include <zlib.h>
#include <zstd.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <map>

namespace nxdock {
namespace {

constexpr size_t kMaxIndex = 16 * 1024 * 1024; // an index larger than this is refused: it and its copies must fit in an applet

std::string UrlDecode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size() && std::isxdigit((unsigned char)s[i + 1]) && std::isxdigit((unsigned char)s[i + 2])) {
            out += char(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        } else {
            out += s[i] == '+' ? ' ' : s[i];
        }
    }
    return out;
}

// An address made whole: "http…" as it is, "/x" against the host, "x" against the folder.
std::string Resolve(const std::string& base, const std::string& url) {
    if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0) return url;
    const size_t scheme = base.find("://");
    const size_t host_end = base.find('/', scheme == std::string::npos ? 0 : scheme + 3);
    const std::string host = host_end == std::string::npos ? base : base.substr(0, host_end);
    if (!url.empty() && url[0] == '/') return host + url;
    std::string folder = base.substr(0, base.find_last_of('/') + 1);
    if (folder.size() <= host.size()) folder = host + "/";
    return folder + url;
}

unsigned long long Hex16(const std::string& s, size_t at) {
    return std::strtoull(s.substr(at, 16).c_str(), nullptr, 16);
}

// [0100000000010000] and [v65536] in a name
void ReadTags(const std::string& name, unsigned long long& tid, unsigned& version) {
    for (size_t i = name.find('['); i != std::string::npos; i = name.find('[', i + 1)) {
        const size_t end = name.find(']', i);
        if (end == std::string::npos) break;
        const std::string tag = name.substr(i + 1, end - i - 1);
        if (tag.size() == 16 && std::all_of(tag.begin(), tag.end(), [](char c) { return std::isxdigit((unsigned char)c); })) {
            tid = Hex16(tag, 0);
        } else if (tag.size() > 1 && (tag[0] == 'v' || tag[0] == 'V') && std::all_of(tag.begin() + 1, tag.end(), [](char c) { return std::isdigit((unsigned char)c); })) {
            version = unsigned(std::strtoul(tag.c_str() + 1, nullptr, 10));
        }
    }
}

// A name without its tags and its extension: "Game [0100…][v0].nsp" → "Game".
std::string PlainName(std::string name) {
    const auto dot = name.find_last_of('.');
    if (dot != std::string::npos && name.size() - dot <= 5) name = name.substr(0, dot);
    std::string out;
    int depth = 0;
    for (char c : name) {
        if (c == '[' || c == '(') depth++;
        else if ((c == ']' || c == ')') && depth) depth--;
        else if (!depth) out += c;
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '-' || out.back() == '_')) out.pop_back();
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    return out;
}

std::string Lower(std::string s) {
    for (auto& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}

} // namespace

const char* ShopsPath() {
    return "sdmc:/config/NXDock/shops.ini";
}

std::vector<ShopConfig> LoadShops(const std::string& path) {
    std::vector<ShopConfig> out;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return out;
    char line[2048];
    while (std::fgets(line, sizeof(line), f)) {
        std::string l = line;
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        std::vector<std::string> parts;
        size_t start = 0;
        for (size_t i = 0; i <= l.size(); i++) {
            if (i == l.size() || l[i] == '\t') {
                parts.push_back(l.substr(start, i - start));
                start = i + 1;
            }
        }
        if (parts.size() >= 2 && !parts[1].empty()) {
            out.push_back({parts[0], parts[1], parts.size() > 2 ? parts[2] : "", parts.size() > 3 ? parts[3] : ""});
        }
    }
    std::fclose(f);
    return out;
}

bool SaveShops(const std::string& path, const std::vector<ShopConfig>& shops) {
    const std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    const auto clean = [](std::string s) {
        for (auto& c : s) {
            if (c == '\t' || c == '\n' || c == '\r') c = ' ';
        }
        return s;
    };
    for (const auto& s : shops) {
        std::fprintf(f, "%s\t%s\t%s\t%s\n", clean(s.name).c_str(), clean(s.url).c_str(), clean(s.user).c_str(), clean(s.pass).c_str());
    }
    const bool ok = std::fclose(f) == 0;
    std::remove(path.c_str());
    return ok && std::rename(tmp.c_str(), path.c_str()) == 0;
}

std::string ShopKey(const ShopConfig& shop) {
    if (shop.user.empty() && shop.pass.empty()) return {};
    return shop.user + '\x1f' + shop.pass;
}

ShopError UnpackIndex(const std::string& bytes, std::string& json) {
    size_t start = 0;
    while (start < bytes.size() && std::isspace((unsigned char)bytes[start])) start++;
    if (start < bytes.size() && bytes[start] == '{') {
        json = bytes.substr(start);
        return ShopError::None;
    }
    if (bytes.size() < 8 || bytes.compare(0, 7, "TINFOIL") != 0) return ShopError::NotIndex;
    const unsigned char flag = (unsigned char)bytes[7];
    if (flag & 0xF0) return ShopError::Encrypted;
    // the size and the data: after a session key of 0x100 bytes (zeros when not encrypted), or
    // straight after the flag
    std::string data;
    for (const size_t at : {size_t(8 + 0x100), size_t(8)}) {
        if (bytes.size() < at + 8) continue;
        unsigned long long size = 0;
        for (int i = 7; i >= 0; i--) size = size << 8 | (unsigned char)bytes[at + size_t(i)];
        if (size == bytes.size() - at - 8) {
            data = bytes.substr(at + 8);
            break;
        }
    }
    if (data.empty()) return ShopError::NotIndex;
    switch (flag & 0x0F) {
        case 0x0E: { // zstd
            const unsigned long long n = ZSTD_getFrameContentSize(data.data(), data.size());
            if (n != ZSTD_CONTENTSIZE_UNKNOWN && n != ZSTD_CONTENTSIZE_ERROR && n <= kMaxIndex) {
                json.resize(size_t(n));
                const size_t got = ZSTD_decompress(json.data(), json.size(), data.data(), data.size());
                if (ZSTD_isError(got)) return ShopError::NotIndex;
                json.resize(got);
            } else {
                ZSTD_DStream* d = ZSTD_createDStream();
                ZSTD_initDStream(d);
                ZSTD_inBuffer in{data.data(), data.size(), 0};
                std::vector<char> chunk(1 << 16);
                json.clear();
                while (in.pos < in.size && json.size() < kMaxIndex) {
                    ZSTD_outBuffer out{chunk.data(), chunk.size(), 0};
                    const size_t rc = ZSTD_decompressStream(d, &out, &in);
                    if (ZSTD_isError(rc)) {
                        ZSTD_freeDStream(d);
                        return ShopError::NotIndex;
                    }
                    json.append(chunk.data(), out.pos);
                    if (rc == 0) break;
                }
                ZSTD_freeDStream(d);
            }
            break;
        }
        case 0x0D: { // zlib
            z_stream z{};
            if (inflateInit(&z) != Z_OK) return ShopError::NotIndex;
            z.next_in = reinterpret_cast<Bytef*>(data.data());
            z.avail_in = uInt(data.size());
            std::vector<char> chunk(1 << 16);
            json.clear();
            int rc = Z_OK;
            while (rc == Z_OK && json.size() < kMaxIndex) {
                z.next_out = reinterpret_cast<Bytef*>(chunk.data());
                z.avail_out = uInt(chunk.size());
                rc = inflate(&z, Z_NO_FLUSH);
                json.append(chunk.data(), chunk.size() - z.avail_out);
            }
            inflateEnd(&z);
            if (rc != Z_STREAM_END) return ShopError::NotIndex;
            break;
        }
        case 0x00: json = data; break;
        default: return ShopError::NotIndex;
    }
    return !json.empty() && json.find('{') != std::string::npos ? ShopError::None : ShopError::NotIndex;
}

std::vector<ShopFile> ParseIndex(const std::string& json, const std::string& base, std::vector<std::string>* directories) {
    std::vector<ShopFile> out;
    yyjson_doc* doc = yyjson_read(json.data(), json.size(), 0);
    if (!doc) return out;
    yyjson_val* root = yyjson_doc_get_root(doc);
    size_t i, n;
    yyjson_val* item;
    yyjson_arr_foreach(yyjson_obj_get(root, "files"), i, n, item) {
        const char* url = yyjson_get_str(yyjson_obj_get(item, "url"));
        if (!url && yyjson_is_str(item)) url = yyjson_get_str(item);
        if (!url) continue;
        ShopFile f;
        std::string u = url;
        // "…/file.nsp#Shown name.nsp": the name after #
        const auto hash = u.find('#');
        std::string shown = hash == std::string::npos ? "" : UrlDecode(u.substr(hash + 1));
        if (hash != std::string::npos) u = u.substr(0, hash);
        f.url = Resolve(base, u);
        if (shown.empty()) {
            const auto slash = u.find_last_of('/');
            shown = UrlDecode(slash == std::string::npos ? u : u.substr(slash + 1));
        }
        f.name = shown;
        yyjson_val* size = yyjson_obj_get(item, "size");
        f.size = yyjson_is_uint(size) ? yyjson_get_uint(size) : yyjson_is_sint(size) ? (unsigned long long)std::max<long long>(0, yyjson_get_sint(size)) : 0;
        ReadTags(f.name, f.title_id, f.version);
        const std::string ext = Lower(f.name.substr(f.name.find_last_of('.') + 1));
        if (ext != "nsp" && ext != "nsz" && ext != "xci" && ext != "xcz") continue;
        out.push_back(std::move(f));
    }
    if (directories) {
        yyjson_arr_foreach(yyjson_obj_get(root, "directories"), i, n, item) {
            const char* url = yyjson_get_str(item);
            if (url) directories->push_back(Resolve(base, url));
        }
    }
    yyjson_doc_free(doc);
    return out;
}

std::vector<Game> ShopGames(const std::vector<ShopFile>& files) {
    std::map<unsigned long long, Game> games;
    std::map<unsigned long long, bool> named_by_base;
    unsigned long long untagged = 1;
    for (const auto& f : files) {
        CatalogFile c;
        c.id = f.url;
        c.name = f.name;
        c.url = f.url;
        c.size = f.size;
        c.title_id = f.title_id;
        c.version = f.version;
        c.format = Lower(f.name.substr(f.name.find_last_of('.') + 1));
        unsigned long long base = f.title_id;
        if (!f.title_id) {
            base = untagged++; // without a title id: a game of its own
        } else if ((f.title_id & 0xFFF) == 0x800) {
            c.kind = CatalogFile::Kind::Update;
            base = f.title_id ^ 0x800;
        } else if ((f.title_id & 0xFFF) != 0) {
            c.kind = CatalogFile::Kind::Dlc;
            base = (f.title_id & ~0xFFFull) - 0x1000;
            c.dlc_name = PlainName(f.name);
        }
        Game& g = games[base];
        g.title_id = f.title_id ? base : 0;
        char key[32];
        std::snprintf(key, sizeof(key), "shop:%016llX", base);
        g.key = key;
        const bool from_base = c.kind == CatalogFile::Kind::Game;
        if (g.name.empty() || (from_base && !named_by_base[base])) {
            g.name = PlainName(f.name);
            named_by_base[base] = from_base;
        }
        g.files.push_back(std::move(c));
    }
    std::vector<Game> out;
    for (auto& [id, g] : games) {
        if (g.name.empty()) g.name = g.key;
        out.push_back(std::move(g));
    }
    std::sort(out.begin(), out.end(), [](const Game& a, const Game& b) { return Lower(a.name) < Lower(b.name); });
    return out;
}

} // namespace nxdock
