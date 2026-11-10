// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "mtp_proto.hpp"

#include <algorithm>
#include <cstring>
#include <ctime>

namespace nxdock::mtp {
namespace {

enum : uint16_t { TypeCommand = 1, TypeData = 2, TypeResponse = 3 };
enum : uint16_t { DtU16 = 0x0004, DtU32 = 0x0006, DtU64 = 0x0008, DtU128 = 0x000A, DtStr = 0xFFFF };
enum : uint16_t { DevicePropFriendlyName = 0xD402 };

constexpr size_t kChunk = 1024 * 1024; // a multiple of every packet size

const std::vector<uint16_t> kOperations = {
    OpGetDeviceInfo, OpOpenSession, OpCloseSession, OpGetStorageIDs, OpGetStorageInfo, OpGetNumObjects,
    OpGetObjectHandles, OpGetObjectInfo, OpGetObject, OpDeleteObject, OpSendObjectInfo, OpSendObject,
    OpGetDevicePropDesc, OpGetDevicePropValue, OpGetPartialObject, OpGetObjectPropsSupported, OpGetObjectPropDesc,
    OpGetObjectPropValue, OpSetObjectPropValue, OpGetObjectPropList, OpSendObjectPropList, OpGetPartialObject64,
};
const std::vector<uint16_t> kProps = {PropStorageId, PropObjectFormat, PropProtectionStatus, PropObjectSize, PropObjectFileName,
                                      PropDateCreated, PropDateModified, PropParentObject, PropPersistentUid, PropName};

uint16_t PropType(uint16_t prop) {
    switch (prop) {
        case PropStorageId:
        case PropParentObject: return DtU32;
        case PropObjectFormat:
        case PropProtectionStatus: return DtU16;
        case PropObjectSize: return DtU64;
        case PropPersistentUid: return DtU128;
        default: return DtStr;
    }
}

std::string Date(int64_t t) {
    if (t <= 0) return "";
    const time_t tt = time_t(t);
    struct tm g{};
#ifdef _WIN32
    gmtime_s(&g, &tt);
#else
    gmtime_r(&tt, &g);
#endif
    char s[32];
    std::snprintf(s, sizeof(s), "%04d%02d%02dT%02d%02d%02d", g.tm_year + 1900, g.tm_mon + 1, g.tm_mday, g.tm_hour, g.tm_min, g.tm_sec);
    return s;
}

std::string Join(const std::string& dir, const std::string& name) {
    return dir.empty() ? name : dir + "/" + name;
}

uint32_t Get32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

// a name the storages can take: no separators, no "." or ".."
bool GoodName(const std::string& n) {
    return !n.empty() && n != "." && n != ".." && n.find('/') == std::string::npos && n.find('\\') == std::string::npos &&
           n.find(':') == std::string::npos;
}

} // namespace

uint32_t StorageId(size_t index) {
    return uint32_t((index + 1) << 16 | 1);
}

void Writer::Str(const std::string& s) {
    // UTF-8 to UTF-16
    std::vector<uint16_t> u;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = (unsigned char)s[i];
        uint32_t cp;
        int n;
        if (c < 0x80) cp = c, n = 1;
        else if ((c >> 5) == 6) cp = c & 0x1F, n = 2;
        else if ((c >> 4) == 14) cp = c & 0x0F, n = 3;
        else cp = c & 0x07, n = 4;
        for (int k = 1; k < n && i + size_t(k) < s.size(); k++) cp = cp << 6 | ((unsigned char)s[i + size_t(k)] & 0x3F);
        i += size_t(n);
        if (cp >= 0x10000) {
            cp -= 0x10000;
            u.push_back(uint16_t(0xD800 | (cp >> 10)));
            u.push_back(uint16_t(0xDC00 | (cp & 0x3FF)));
        } else {
            u.push_back(uint16_t(cp));
        }
    }
    if (u.empty()) {
        U8(0);
        return;
    }
    if (u.size() > 254) u.resize(254);
    U8(uint8_t(u.size() + 1));
    for (auto c : u) U16(c);
    U16(0);
}

bool Reader::U8(uint8_t& v) {
    if (m_at + 1 > m_n) return false;
    v = m_p[m_at++];
    return true;
}
bool Reader::U16(uint16_t& v) {
    if (m_at + 2 > m_n) return false;
    v = uint16_t(m_p[m_at] | m_p[m_at + 1] << 8);
    m_at += 2;
    return true;
}
bool Reader::U32(uint32_t& v) {
    if (m_at + 4 > m_n) return false;
    v = Get32(m_p + m_at);
    m_at += 4;
    return true;
}
bool Reader::U64(uint64_t& v) {
    uint32_t lo, hi;
    if (!U32(lo) || !U32(hi)) return false;
    v = uint64_t(hi) << 32 | lo;
    return true;
}
bool Reader::Skip(size_t n) {
    if (m_at + n > m_n) return false;
    m_at += n;
    return true;
}
bool Reader::Str(std::string& out) {
    out.clear();
    uint8_t n;
    if (!U8(n)) return false;
    std::vector<uint16_t> u(n);
    for (auto& c : u) {
        if (!U16(c)) return false;
    }
    for (size_t i = 0; i < u.size(); i++) {
        uint32_t cp = u[i];
        if (cp == 0) break;
        if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < u.size()) cp = 0x10000 + ((cp - 0xD800) << 10) + (u[++i] - 0xDC00);
        if (cp < 0x80) out += char(cp);
        else if (cp < 0x800) out += char(0xC0 | cp >> 6), out += char(0x80 | (cp & 0x3F));
        else if (cp < 0x10000) out += char(0xE0 | cp >> 12), out += char(0x80 | ((cp >> 6) & 0x3F)), out += char(0x80 | (cp & 0x3F));
        else out += char(0xF0 | cp >> 18), out += char(0x80 | ((cp >> 12) & 0x3F)), out += char(0x80 | ((cp >> 6) & 0x3F)), out += char(0x80 | (cp & 0x3F));
    }
    return true;
}

Responder::Responder(Transport& usb, std::vector<std::shared_ptr<Storage>> storages, std::string device_name)
    : m_usb(usb), m_storages(std::move(storages)), m_name(std::move(device_name)) {}

bool Responder::ReadExact(void* buf, size_t size) {
    auto out = static_cast<uint8_t*>(buf);
    while (size) {
        if (!m_left.empty()) {
            const size_t n = std::min(size, m_left.size());
            std::memcpy(out, m_left.data(), n);
            m_left.erase(m_left.begin(), m_left.begin() + long(n));
            out += n;
            size -= n;
            continue;
        }
        std::vector<uint8_t> t(std::max(size, size_t(512)));
        size_t got = 0;
        if (!m_usb.Read(t.data(), t.size(), &got)) return false;
        t.resize(got); // a transfer of no bytes (the end of a data phase) is simply passed over
        m_left = std::move(t);
    }
    return true;
}

bool Responder::ReadContainer(std::vector<uint8_t>& out, uint16_t& type, uint16_t& code, uint32_t& tid) {
    uint8_t h[12];
    if (!ReadExact(h, sizeof(h))) return false;
    const uint32_t len = Get32(h);
    type = uint16_t(h[4] | h[5] << 8);
    code = uint16_t(h[6] | h[7] << 8);
    tid = Get32(h + 8);
    out.clear();
    if (len > 12 && len != 0xFFFFFFFF) {
        if (len - 12 > (64u << 20)) return false;
        out.resize(len - 12);
        if (!ReadExact(out.data(), out.size())) return false;
    }
    return true;
}

bool Responder::ReadDataPhase(std::vector<uint8_t>& out, uint64_t max) {
    uint16_t type, code;
    uint32_t tid;
    if (!ReadContainer(out, type, code, tid)) return false;
    if (out.size() > max) out.resize(size_t(max));
    return type == TypeData;
}

bool Responder::Drain() {
    std::vector<uint8_t> ignored;
    return ReadDataPhase(ignored, 0);
}

bool Responder::Respond(uint16_t code, std::vector<uint32_t> params) {
    Writer w;
    w.U32(uint32_t(12 + 4 * params.size()));
    w.U16(TypeResponse);
    w.U16(code);
    w.U32(m_tid);
    for (auto p : params) w.U32(p);
    return m_usb.Write(w.Bytes().data(), w.Bytes().size());
}

bool Responder::SendStream(uint64_t size, const std::function<bool(uint64_t, void*, size_t)>& read) {
    // the header and the data in one stream of transfers: every transfer but the last fills
    // whole packets, so the host does not take a short packet for the end
    std::vector<uint8_t> buf(kChunk);
    const uint64_t total = size + 12;
    Writer h;
    h.U32(total > 0xFFFFFFFFull ? 0xFFFFFFFF : uint32_t(total));
    h.U16(TypeData);
    h.U16(m_last_code);
    h.U32(m_tid);
    std::memcpy(buf.data(), h.Bytes().data(), 12);
    size_t fill = 12;
    uint64_t off = 0;
    while (true) {
        const size_t n = size_t(std::min<uint64_t>(buf.size() - fill, size - off));
        if (n && !read(off, buf.data() + fill, n)) return false;
        off += n;
        fill += n;
        if (off == size) break;
        if (!m_usb.Write(buf.data(), fill)) return false;
        fill = 0;
    }
    if (!m_usb.Write(buf.data(), fill)) return false;
    // a stream that ended on a whole packet is closed with a transfer of no bytes
    if (total % m_usb.PacketSize() == 0) return m_usb.WriteEnd();
    return true;
}

bool Responder::SendData(const std::vector<uint8_t>& data) {
    return SendStream(data.size(), [&](uint64_t off, void* buf, size_t n) {
        std::memcpy(buf, data.data() + off, n);
        return true;
    });
}

Storage* Responder::StorageOf(uint32_t id) {
    for (size_t i = 0; i < m_storages.size(); i++) {
        if (StorageId(i) == id) return m_storages[i].get();
    }
    return nullptr;
}

uint32_t Responder::HandleOf(uint32_t storage, uint32_t parent, const std::string& path, bool dir) {
    const std::string key = std::to_string(storage) + ":" + path;
    const auto it = m_by_key.find(key);
    if (it != m_by_key.end()) {
        m_objects[it->second].dir = dir;
        return it->second;
    }
    const uint32_t h = m_next_handle++;
    m_objects[h] = {storage, parent, path, dir};
    m_by_key[key] = h;
    return h;
}

bool Responder::Lookup(uint32_t handle, Object& out) {
    const auto it = m_objects.find(handle);
    if (it == m_objects.end()) return false;
    out = it->second;
    return true;
}

void Responder::Forget(uint32_t handle) {
    const auto it = m_objects.find(handle);
    if (it == m_objects.end()) return;
    const std::string prefix = std::to_string(it->second.storage) + ":" + it->second.path;
    for (auto k = m_by_key.begin(); k != m_by_key.end();) {
        if (k->first == prefix || k->first.rfind(prefix + "/", 0) == 0) {
            m_objects.erase(k->second);
            k = m_by_key.erase(k);
        } else {
            ++k;
        }
    }
}

std::string Responder::ParentPath(uint32_t storage, uint32_t parent, bool& ok) {
    ok = true;
    if (parent == 0 || parent == 0xFFFFFFFF) return "";
    Object o;
    if (!Lookup(parent, o) || !o.dir || o.storage != storage) {
        ok = false;
        return "";
    }
    return o.path;
}

void Responder::Run() {
    while (Serve()) {
    }
}

bool Responder::Serve() {
    std::vector<uint8_t> payload;
    uint16_t type, code;
    if (!ReadContainer(payload, type, code, m_tid)) return false;
    if (type != TypeCommand) return true; // a stray data phase: passed over
    m_last_code = code;
    uint32_t p[5] = {};
    for (size_t i = 0; i < 5 && i * 4 + 4 <= payload.size(); i++) p[i] = Get32(payload.data() + i * 4);

    if (code == OpGetDeviceInfo) return DeviceInfo();
    if (code == OpOpenSession) {
        if (m_session) return Respond(RespSessionAlreadyOpen, {});
        m_session = true;
        m_objects.clear();
        m_by_key.clear();
        m_next_handle = 1;
        m_pending = {};
        return Respond(RespOk);
    }
    if (!m_session) {
        if (code == OpSendObject || code == OpSendObjectInfo || code == OpSendObjectPropList || code == OpSetObjectPropValue) Drain();
        return Respond(RespSessionNotOpen);
    }
    switch (code) {
        case OpCloseSession:
            m_session = false;
            if (m_pending.sink) m_pending.sink->Close(false);
            m_pending = {};
            return Respond(RespOk);
        case OpGetStorageIDs: {
            Writer w;
            std::vector<uint32_t> ids;
            for (size_t i = 0; i < m_storages.size(); i++) ids.push_back(StorageId(i));
            w.Array32(ids);
            return SendData(w.Bytes()) && Respond(RespOk);
        }
        case OpGetStorageInfo: return StorageInfo(p[0]);
        case OpGetNumObjects: return ObjectHandles(p[0], p[1], p[2], true);
        case OpGetObjectHandles: return ObjectHandles(p[0], p[1], p[2], false);
        case OpGetObjectInfo: return ObjectInfo(p[0]);
        case OpGetObject: return GetObject(p[0], 0, ~0ull, false, false);
        case OpGetPartialObject: return GetObject(p[0], p[1], p[2], true, false);
        case OpGetPartialObject64: return GetObject(p[0], uint64_t(p[2]) << 32 | p[1], p[3], true, true);
        case OpDeleteObject: return DeleteObject(p[0]);
        case OpSendObjectInfo: return SendObjectInfo(p[0], p[1]);
        case OpSendObjectPropList: return SendObjectPropList(p[0], p[1], p[2], uint64_t(p[3]) << 32 | p[4]);
        case OpSendObject: return SendObject();
        case OpGetObjectPropsSupported: return PropsSupported();
        case OpGetObjectPropDesc: return PropDesc(p[0]);
        case OpGetObjectPropValue: return PropValue(p[0], p[1]);
        case OpSetObjectPropValue: return SetPropValue(p[0], p[1]);
        case OpGetObjectPropList: return PropList(p[0], p[1], p[2], p[3], p[4]);
        case OpGetDevicePropDesc:
        case OpGetDevicePropValue: {
            if (p[0] != DevicePropFriendlyName) return Respond(RespDevicePropNotSupported);
            Writer w;
            if (code == OpGetDevicePropDesc) {
                w.U16(DevicePropFriendlyName);
                w.U16(DtStr);
                w.U8(0); // get only
                w.Str(m_name);
                w.Str(m_name);
                w.U8(0);
            } else {
                w.Str(m_name);
            }
            return SendData(w.Bytes()) && Respond(RespOk);
        }
        default: return Respond(RespNotSupported);
    }
}

bool Responder::DeviceInfo() {
    Writer w;
    w.U16(100);
    w.U32(6); // the Microsoft extension: this is MTP
    w.U16(100);
    w.Str("microsoft.com: 1.0;");
    w.U16(0);
    w.Array16(kOperations);
    w.Array16({});
    w.Array16({DevicePropFriendlyName});
    w.Array16({});
    w.Array16({FormatUndefined, FormatAssociation});
    w.Str("DeLFuS77");
    w.Str(m_name);
    w.Str("1.0");
    w.Str("0000000000000001");
    return SendData(w.Bytes()) && Respond(RespOk);
}

bool Responder::StorageInfo(uint32_t id) {
    Storage* s = StorageOf(id);
    if (!s) return Respond(RespInvalidStorageId);
    uint64_t total = 0, free = 0;
    s->Space(total, free);
    Writer w;
    w.U16(0x0004); // removable RAM
    w.U16(0x0002); // a hierarchy of folders
    w.U16(s->ReadOnly() ? 1 : 0);
    w.U64(total);
    w.U64(free);
    w.U32(0xFFFFFFFF);
    w.Str(s->Description());
    w.Str("");
    return SendData(w.Bytes()) && Respond(RespOk);
}

bool Responder::ObjectHandles(uint32_t storage, uint32_t format, uint32_t parent, bool count_only) {
    std::vector<uint32_t> handles;
    for (size_t i = 0; i < m_storages.size(); i++) {
        const uint32_t sid = StorageId(i);
        if (storage != 0xFFFFFFFF && storage != sid) continue;
        bool ok;
        const std::string dir = ParentPath(sid, parent, ok);
        if (!ok) {
            if (storage == 0xFFFFFFFF) continue;
            return Respond(RespInvalidParentObject);
        }
        std::vector<Entry> entries;
        m_storages[i]->List(dir, entries);
        for (const auto& e : entries) {
            if (format && format != (e.dir ? FormatAssociation : FormatUndefined)) continue;
            handles.push_back(HandleOf(sid, parent == 0xFFFFFFFF ? 0 : parent, Join(dir, e.name), e.dir));
        }
    }
    if (storage != 0xFFFFFFFF && !StorageOf(storage)) return Respond(RespInvalidStorageId);
    if (count_only) return Respond(RespOk, {uint32_t(handles.size())});
    Writer w;
    w.Array32(handles);
    return SendData(w.Bytes()) && Respond(RespOk);
}

bool Responder::ObjectInfo(uint32_t handle) {
    Object o;
    if (!Lookup(handle, o)) return Respond(RespInvalidObjectHandle);
    Entry e;
    if (!StorageOf(o.storage)->Stat(o.path, e)) return Respond(RespInvalidObjectHandle);
    Writer w;
    w.U32(o.storage);
    w.U16(e.dir ? FormatAssociation : FormatUndefined);
    w.U16(0);
    w.U32(e.size > 0xFFFFFFFFull ? 0xFFFFFFFF : uint32_t(e.size));
    w.U16(0);
    for (int i = 0; i < 6; i++) w.U32(0); // thumbnail and image fields
    w.U32(o.parent);
    w.U16(e.dir ? 1 : 0); // a generic folder
    w.U32(0);
    w.U32(0);
    w.Str(e.name);
    w.Str(Date(e.mtime));
    w.Str(Date(e.mtime));
    w.Str("");
    return SendData(w.Bytes()) && Respond(RespOk);
}

bool Responder::GetObject(uint32_t handle, uint64_t off, uint64_t max, bool partial, bool /*size64*/) {
    Object o;
    if (!Lookup(handle, o) || o.dir) return Respond(RespInvalidObjectHandle);
    Storage* s = StorageOf(o.storage);
    Entry e;
    if (!s->Stat(o.path, e)) return Respond(RespInvalidObjectHandle);
    auto src = s->Open(o.path);
    if (!src) return Respond(RespAccessDenied);
    if (off > e.size) off = e.size;
    const uint64_t n = std::min<uint64_t>(max, e.size - off);
    bool ok = true;
    if (!SendStream(n, [&](uint64_t at, void* buf, size_t k) {
            if (!src->Read(off + at, buf, k)) {
                ok = false;
                std::memset(buf, 0, k); // the stream has to go on to its length
            }
            return true;
        })) {
        return false;
    }
    if (!ok) return Respond(RespIncompleteTransfer);
    return partial ? Respond(RespOk, {uint32_t(std::min<uint64_t>(n, 0xFFFFFFFF))}) : Respond(RespOk);
}

bool Responder::SendObjectInfo(uint32_t storage, uint32_t parent) {
    std::vector<uint8_t> data;
    if (!ReadDataPhase(data)) return false;
    if (m_pending.sink) m_pending.sink->Close(false);
    m_pending = {};
    Reader r(data.data(), data.size());
    uint32_t sid, size32;
    uint16_t format, prot;
    std::string name;
    if (!r.U32(sid) || !r.U16(format) || !r.U16(prot) || !r.U32(size32) || !r.Skip(2 + 6 * 4 + 4 + 2 + 4 + 4) || !r.Str(name)) {
        return Respond(RespInvalidParameter);
    }
    m_info_name = name;
    return SendObjectPropList(storage, parent, format, size32 == 0xFFFFFFFF ? ~0ull : size32);
}

bool Responder::SendObjectPropList(uint32_t storage, uint32_t parent, uint32_t format, uint64_t size) {
    // called for SendObjectPropList with its data phase still to read, and for SendObjectInfo
    // (its data read already, the name kept in m_pending_name)
    std::string name;
    if (m_last_code == OpSendObjectPropList) {
        std::vector<uint8_t> data;
        if (!ReadDataPhase(data)) return false;
        if (m_pending.sink) m_pending.sink->Close(false);
        m_pending = {};
        Reader r(data.data(), data.size());
        uint32_t count;
        if (!r.U32(count)) return Respond(RespInvalidParameter);
        for (uint32_t i = 0; i < count; i++) {
            uint32_t h;
            uint16_t prop, dt;
            if (!r.U32(h) || !r.U16(prop) || !r.U16(dt)) return Respond(RespInvalidParameter);
            switch (dt) {
                case DtStr: {
                    std::string v;
                    if (!r.Str(v)) return Respond(RespInvalidParameter);
                    if (prop == PropObjectFileName) name = v;
                    break;
                }
                case 0x0001: case 0x0002: if (!r.Skip(1)) return Respond(RespInvalidParameter); break;
                case 0x0003: case 0x0004: if (!r.Skip(2)) return Respond(RespInvalidParameter); break;
                case 0x0005: case DtU32: if (!r.Skip(4)) return Respond(RespInvalidParameter); break;
                case 0x0007: case DtU64: if (!r.Skip(8)) return Respond(RespInvalidParameter); break;
                case 0x0009: case DtU128: if (!r.Skip(16)) return Respond(RespInvalidParameter); break;
                default: return Respond(RespInvalidParameter);
            }
        }
    } else {
        name = m_info_name;
    }
    if (storage == 0 || storage == 0xFFFFFFFF) storage = StorageId(0);
    Storage* s = StorageOf(storage);
    if (!s) return Respond(RespInvalidStorageId);
    if (s->ReadOnly()) return Respond(RespStoreReadOnly);
    bool ok;
    const std::string dir = ParentPath(storage, parent, ok);
    if (!ok) return Respond(RespInvalidParentObject);
    if (!GoodName(name)) return Respond(RespInvalidParameter);
    const std::string path = Join(dir, name);
    const bool folder = format == FormatAssociation;
    if (folder) {
        if (!s->MakeDir(path)) return Respond(RespAccessDenied);
        const uint32_t h = HandleOf(storage, parent == 0xFFFFFFFF ? 0 : parent, path, true);
        return Respond(RespOk, {storage, parent == 0xFFFFFFFF ? 0 : parent, h});
    }
    if (size == ~0ull) return Respond(RespObjectTooLarge); // a file over 4 GB needs SendObjectPropList
    bool busy = false;
    auto sink = s->Create(path, size, busy);
    if (!sink) return Respond(busy ? RespDeviceBusy : RespAccessDenied);
    m_pending.valid = true;
    m_pending.storage = storage;
    m_pending.parent = parent == 0xFFFFFFFF ? 0 : parent;
    m_pending.path = path;
    m_pending.size = size;
    m_pending.handle = HandleOf(storage, m_pending.parent, path, false);
    m_pending.sink = std::move(sink);
    if (m_last_code == OpSendObjectPropList) return Respond(RespOk, {storage, m_pending.parent, m_pending.handle, 0});
    return Respond(RespOk, {storage, m_pending.parent, m_pending.handle});
}

bool Responder::SendObject() {
    // the header of the data phase, then exactly the announced size
    uint8_t h[12];
    if (!ReadExact(h, sizeof(h))) return false;
    if (!m_pending.valid || !m_pending.sink) {
        // nothing announced: the data is read and dropped
        const uint32_t len = Get32(h);
        std::vector<uint8_t> drop(kChunk);
        uint64_t left = len >= 12 && len != 0xFFFFFFFF ? len - 12 : 0;
        while (left) {
            const size_t n = size_t(std::min<uint64_t>(left, drop.size()));
            if (!ReadExact(drop.data(), n)) return false;
            left -= n;
        }
        return Respond(RespNoValidObjectInfo);
    }
    uint64_t left = m_pending.size;
    bool ok = true;
    std::vector<uint8_t> buf(kChunk);
    while (left) {
        size_t got;
        if (!m_left.empty()) {
            got = std::min<size_t>(size_t(std::min<uint64_t>(left, m_left.size())), m_left.size());
            std::memcpy(buf.data(), m_left.data(), got);
            m_left.erase(m_left.begin(), m_left.begin() + long(got));
        } else {
            if (!m_usb.Read(buf.data(), size_t(std::min<uint64_t>(left, buf.size())), &got)) {
                m_pending.sink->Close(false);
                m_pending = {};
                return false;
            }
            if (got == 0) continue;
        }
        // once the receiver refuses (a cancel, no room) the rest is read and dropped
        if (ok && !m_pending.sink->Write(buf.data(), got)) ok = false;
        left -= got;
    }
    ok = m_pending.sink->Close(ok) && ok;
    m_pending.sink.reset();
    m_pending.valid = false;
    return Respond(ok ? RespOk : RespIncompleteTransfer);
}

bool Responder::DeleteObject(uint32_t handle) {
    Object o;
    if (!Lookup(handle, o)) return Respond(RespInvalidObjectHandle);
    Storage* s = StorageOf(o.storage);
    if (s->ReadOnly()) return Respond(RespStoreReadOnly);
    if (!s->Remove(o.path, o.dir)) return Respond(RespAccessDenied);
    Forget(handle);
    return Respond(RespOk);
}

bool Responder::PropsSupported() {
    Writer w;
    w.Array16(kProps);
    return SendData(w.Bytes()) && Respond(RespOk);
}

bool Responder::PropDesc(uint32_t prop) {
    if (std::find(kProps.begin(), kProps.end(), uint16_t(prop)) == kProps.end()) return Respond(RespInvalidObjectPropCode);
    Writer w;
    const uint16_t dt = PropType(uint16_t(prop));
    w.U16(uint16_t(prop));
    w.U16(dt);
    w.U8(prop == PropObjectFileName || prop == PropName ? 1 : 0);
    switch (dt) {
        case DtU16: w.U16(0); break;
        case DtU32: w.U32(0); break;
        case DtU64: w.U64(0); break;
        case DtU128: w.U128(0, 0); break;
        default: w.U8(0); break;
    }
    w.U32(0);
    w.U8(0);
    return SendData(w.Bytes()) && Respond(RespOk);
}

void Responder::PutProp(Writer& w, uint32_t handle, const Object& o, const Entry& e, uint16_t prop) {
    switch (prop) {
        case PropStorageId: w.U32(o.storage); break;
        case PropObjectFormat: w.U16(e.dir ? FormatAssociation : FormatUndefined); break;
        case PropProtectionStatus: w.U16(0); break;
        case PropObjectSize: w.U64(e.size); break;
        case PropObjectFileName:
        case PropName: w.Str(e.name); break;
        case PropDateCreated:
        case PropDateModified: w.Str(Date(e.mtime)); break;
        case PropParentObject: w.U32(o.parent); break;
        case PropPersistentUid: w.U128(handle, o.storage); break;
        default: break;
    }
}

bool Responder::PropValue(uint32_t handle, uint32_t prop) {
    Object o;
    if (!Lookup(handle, o)) return Respond(RespInvalidObjectHandle);
    if (std::find(kProps.begin(), kProps.end(), uint16_t(prop)) == kProps.end()) return Respond(RespInvalidObjectPropCode);
    Entry e;
    if (!StorageOf(o.storage)->Stat(o.path, e)) return Respond(RespInvalidObjectHandle);
    Writer w;
    PutProp(w, handle, o, e, uint16_t(prop));
    return SendData(w.Bytes()) && Respond(RespOk);
}

bool Responder::SetPropValue(uint32_t handle, uint32_t prop) {
    std::vector<uint8_t> data;
    if (!ReadDataPhase(data)) return false;
    Object o;
    if (!Lookup(handle, o)) return Respond(RespInvalidObjectHandle);
    if (prop != PropObjectFileName && prop != PropName) return Respond(RespAccessDenied);
    Reader r(data.data(), data.size());
    std::string name;
    if (!r.Str(name) || !GoodName(name)) return Respond(RespInvalidParameter);
    Storage* s = StorageOf(o.storage);
    const auto slash = o.path.find_last_of('/');
    const std::string to = slash == std::string::npos ? name : o.path.substr(0, slash + 1) + name;
    if (s->ReadOnly() || !s->Rename(o.path, to)) return Respond(RespAccessDenied);
    const bool dir = o.dir;
    const uint32_t parent = o.parent, storage = o.storage;
    Forget(handle);
    // the same handle keeps naming it
    m_objects[handle] = {storage, parent, to, dir};
    m_by_key[std::to_string(storage) + ":" + to] = handle;
    return Respond(RespOk);
}

bool Responder::PropList(uint32_t handle, uint32_t format, uint32_t prop, uint32_t group, uint32_t depth) {
    (void)format;
    if (group != 0) return Respond(RespSpecificationByGroupUnsupported);
    std::vector<uint32_t> handles;
    if (depth == 0) {
        Object o;
        if (!Lookup(handle, o)) return Respond(RespInvalidObjectHandle);
        handles.push_back(handle);
    } else {
        // the objects inside: of a folder, or at the roots
        for (size_t i = 0; i < m_storages.size(); i++) {
            const uint32_t sid = StorageId(i);
            std::string dir;
            uint32_t parent = 0;
            if (handle != 0 && handle != 0xFFFFFFFF) {
                Object o;
                if (!Lookup(handle, o)) return Respond(RespInvalidObjectHandle);
                if (o.storage != sid) continue;
                dir = o.path;
                parent = handle;
            }
            std::vector<Entry> entries;
            m_storages[i]->List(dir, entries);
            for (const auto& e : entries) handles.push_back(HandleOf(sid, parent, Join(dir, e.name), e.dir));
        }
    }
    Writer w;
    uint32_t count = 0;
    Writer body;
    for (const auto h : handles) {
        Object o;
        Entry e;
        if (!Lookup(h, o) || !StorageOf(o.storage)->Stat(o.path, e)) continue;
        for (const auto p : kProps) {
            if (prop != 0xFFFFFFFF && prop != p) continue;
            body.U32(h);
            body.U16(p);
            body.U16(PropType(p));
            PutProp(body, h, o, e, p);
            count++;
        }
    }
    w.U32(count);
    w.Bytes().insert(w.Bytes().end(), body.Bytes().begin(), body.Bytes().end());
    return SendData(w.Bytes()) && Respond(RespOk);
}

} // namespace nxdock::mtp
