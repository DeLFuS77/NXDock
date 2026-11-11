// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "zip.hpp"

#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <memory>

namespace nxdock::zip {
namespace {

constexpr size_t kChunk = 256 * 1024;

void Le16(unsigned char* p, uint32_t v) {
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}
void Le32(unsigned char* p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (unsigned char)(v >> (8 * i));
}
uint32_t Get16(const unsigned char* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8;
}
uint32_t Get32(const unsigned char* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
uint64_t Get64(const unsigned char* p) {
    return uint64_t(Get32(p)) | uint64_t(Get32(p + 4)) << 32;
}

} // namespace

bool SafeName(const std::string& name) {
    if (name.empty() || name[0] == '/' || name.find('\\') != std::string::npos || name.find(':') != std::string::npos) return false;
    size_t start = 0;
    while (start <= name.size()) {
        const size_t end = name.find('/', start);
        const std::string part = name.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (part == "..") return false;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}

Writer::~Writer() {
    if (m_file) std::fclose(m_file);
}

bool Writer::Open(const std::string& path) {
    m_file = std::fopen(path.c_str(), "wb");
    m_pos = 0;
    m_entries.clear();
    m_ok = m_file != nullptr;
    return m_ok;
}

bool Writer::Put(const void* data, size_t size) {
    if (!m_ok) return false;
    if (std::fwrite(data, 1, size, m_file) != size) m_ok = false;
    m_pos += uint32_t(size);
    return m_ok;
}

bool Writer::AddFolder(const std::string& name) {
    return AddFile(name.back() == '/' ? name : name + "/", 0, [](uint64_t, void*, size_t) { return true; });
}

bool Writer::AddFile(const std::string& name, uint64_t size, const std::function<bool(uint64_t, void*, size_t)>& read) {
    if (!m_ok || size > 0xFFFFFFFFull) return m_ok = false;
    std::unique_ptr<unsigned char[]> buf(new unsigned char[kChunk]);
    uLong crc = crc32(0, nullptr, 0);
    for (uint64_t off = 0; off < size;) {
        const size_t n = size_t(std::min<uint64_t>(kChunk, size - off));
        if (!read(off, buf.get(), n)) return m_ok = false;
        crc = crc32(crc, buf.get(), uInt(n));
        off += n;
    }
    Entry e{name, uint32_t(crc), uint32_t(size), m_pos};
    unsigned char h[30] = {};
    Le32(h, 0x04034b50);
    Le16(h + 4, 20);
    Le16(h + 6, 0x0800); // UTF-8 names
    Le32(h + 14, e.crc);
    Le32(h + 18, e.size);
    Le32(h + 22, e.size);
    Le16(h + 26, uint32_t(name.size()));
    Put(h, sizeof(h));
    Put(name.data(), name.size());
    for (uint64_t off = 0; off < size;) {
        const size_t n = size_t(std::min<uint64_t>(kChunk, size - off));
        if (!read(off, buf.get(), n) || !Put(buf.get(), n)) return m_ok = false;
        off += n;
    }
    m_entries.push_back(e);
    return m_ok;
}

bool Writer::Close() {
    if (!m_file) return false;
    const uint32_t start = m_pos;
    for (const auto& e : m_entries) {
        unsigned char h[46] = {};
        Le32(h, 0x02014b50);
        Le16(h + 4, 20);
        Le16(h + 6, 20);
        Le16(h + 8, 0x0800);
        Le32(h + 16, e.crc);
        Le32(h + 20, e.size);
        Le32(h + 24, e.size);
        Le16(h + 28, uint32_t(e.name.size()));
        Le32(h + 42, e.offset);
        Put(h, sizeof(h));
        Put(e.name.data(), e.name.size());
    }
    unsigned char end[22] = {};
    Le32(end, 0x06054b50);
    Le16(end + 8, uint32_t(m_entries.size()));
    Le16(end + 10, uint32_t(m_entries.size()));
    Le32(end + 12, m_pos - start);
    Le32(end + 16, start);
    Put(end, sizeof(end));
    const bool ok = m_ok && std::fclose(m_file) == 0;
    m_file = nullptr;
    return ok;
}

Reader::~Reader() {
    if (m_file) std::fclose(m_file);
}

bool Reader::Open(const std::string& path) {
    m_entries.clear();
    m_file = std::fopen(path.c_str(), "rb");
    if (!m_file) return false;
    std::fseek(m_file, 0, SEEK_END);
    const long long size = (long long)ftello(m_file);
    // the end record is in the last 64 KB and 22 bytes
    const long long tail = std::min<long long>(size, 65557);
    std::vector<unsigned char> buf(static_cast<size_t>(tail));
    fseeko(m_file, size - tail, SEEK_SET);
    if (std::fread(buf.data(), 1, buf.size(), m_file) != buf.size()) return false;
    long long at = -1;
    for (long long i = tail - 22; i >= 0; i--) {
        if (Get32(&buf[size_t(i)]) == 0x06054b50) {
            at = i;
            break;
        }
    }
    if (at < 0) return false;
    uint64_t count = Get16(&buf[size_t(at) + 10]);
    uint64_t dir_size = Get32(&buf[size_t(at) + 12]), dir_at = Get32(&buf[size_t(at) + 16]);
    // zip64: the end record locator just before
    if ((count == 0xFFFF || dir_at == 0xFFFFFFFF) && at >= 20 && Get32(&buf[size_t(at) - 20]) == 0x07064b50) {
        const uint64_t rec = Get64(&buf[size_t(at) - 12]);
        unsigned char z[56];
        fseeko(m_file, (off_t)rec, SEEK_SET);
        if (std::fread(z, 1, sizeof(z), m_file) != sizeof(z) || Get32(z) != 0x06064b50) return false;
        count = Get64(z + 32);
        dir_size = Get64(z + 40);
        dir_at = Get64(z + 48);
    }
    // the list of files of a mod or a save is small: refuse huge ones and absurd counts
    if (dir_size > 4ull * 1024 * 1024 || count > 65536 || count > dir_size / 46) return false;
    std::vector<unsigned char> dir(static_cast<size_t>(dir_size));
    fseeko(m_file, (off_t)dir_at, SEEK_SET);
    if (std::fread(dir.data(), 1, dir.size(), m_file) != dir.size()) return false;
    size_t p = 0;
    for (uint64_t i = 0; i < count; i++) {
        if (p + 46 > dir.size() || Get32(&dir[p]) != 0x02014b50) return false;
        Entry e;
        e.method = uint16_t(Get16(&dir[p + 10]));
        e.crc = Get32(&dir[p + 16]);
        e.packed = Get32(&dir[p + 20]);
        e.size = Get32(&dir[p + 24]);
        const size_t name_len = Get16(&dir[p + 28]), extra_len = Get16(&dir[p + 30]), comment_len = Get16(&dir[p + 32]);
        e.local = Get32(&dir[p + 42]);
        if (p + 46 + name_len + extra_len + comment_len > dir.size()) return false;
        e.name.assign(reinterpret_cast<const char*>(&dir[p + 46]), name_len);
        // zip64 sizes in the extra field
        size_t x = p + 46 + name_len;
        const size_t xend = x + extra_len;
        while (x + 4 <= xend) {
            const uint32_t id = Get16(&dir[x]), len = Get16(&dir[x + 2]);
            // the field must stay inside the extra block and the list
            if (x + 4 + len > xend || xend > dir.size()) break;
            if (id == 1) {
                size_t q = x + 4;
                if (e.size == 0xFFFFFFFF && q + 8 <= x + 4 + len) e.size = Get64(&dir[q]), q += 8;
                if (e.packed == 0xFFFFFFFF && q + 8 <= x + 4 + len) e.packed = Get64(&dir[q]), q += 8;
                if (e.local == 0xFFFFFFFF && q + 8 <= x + 4 + len) e.local = Get64(&dir[q]);
            }
            x += 4 + len;
        }
        m_entries.push_back(e);
        p += 46 + name_len + extra_len + comment_len;
    }
    return true;
}

bool Reader::Extract(const Entry& e, const std::function<bool(const void*, size_t)>& out) {
    if (!m_file || (e.method != 0 && e.method != 8)) return false;
    unsigned char h[30];
    fseeko(m_file, (off_t)e.local, SEEK_SET);
    if (std::fread(h, 1, sizeof(h), m_file) != sizeof(h) || Get32(h) != 0x04034b50) return false;
    fseeko(m_file, (off_t)(e.local + 30 + Get16(h + 26) + Get16(h + 28)), SEEK_SET);
    std::unique_ptr<unsigned char[]> in(new unsigned char[kChunk]), o(new unsigned char[kChunk]);
    uLong crc = crc32(0, nullptr, 0);
    uint64_t left = e.packed, given = 0;
    if (e.method == 0) {
        if (e.packed != e.size) return false;
        while (left) {
            const size_t n = size_t(std::min<uint64_t>(kChunk, left));
            if (std::fread(in.get(), 1, n, m_file) != n) return false;
            crc = crc32(crc, in.get(), uInt(n));
            if (!out(in.get(), n)) return false;
            left -= n;
            given += n;
        }
    } else {
        z_stream z{};
        if (inflateInit2(&z, -MAX_WBITS) != Z_OK) return false;
        int rc = Z_OK;
        while (rc != Z_STREAM_END) {
            if (z.avail_in == 0 && left) {
                const size_t n = size_t(std::min<uint64_t>(kChunk, left));
                if (std::fread(in.get(), 1, n, m_file) != n) break;
                z.next_in = in.get();
                z.avail_in = uInt(n);
                left -= n;
            }
            z.next_out = o.get();
            z.avail_out = uInt(kChunk);
            rc = inflate(&z, Z_NO_FLUSH);
            if (rc != Z_OK && rc != Z_STREAM_END) break;
            const size_t n = kChunk - z.avail_out;
            if (n) {
                // never more than the list says: a zip bomb would fill the card
                if (given + n > e.size) {
                    rc = Z_DATA_ERROR;
                    break;
                }
                crc = crc32(crc, o.get(), uInt(n));
                if (!out(o.get(), n)) {
                    rc = Z_DATA_ERROR;
                    break;
                }
                given += n;
            }
            if (rc == Z_OK && z.avail_in == 0 && !left && n == 0) break;
        }
        inflateEnd(&z);
        if (rc != Z_STREAM_END) return false;
    }
    return given == e.size && uint32_t(crc) == e.crc;
}

} // namespace nxdock::zip
