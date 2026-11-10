// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#pragma once

// NXDock's own MTP responder (Media Transfer Protocol over USB), written from the published PTP
// (ISO 15740) and MTP 1.1 specifications. It knows nothing of the console: the USB side and the
// storages are given to it, so it is tested on the computer with a fake cable and folders.

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace nxdock::mtp {

// The bulk endpoints of the cable. Read gives what one transfer brought (up to `size`); false
// when the cable is gone or the responder is stopping.
struct Transport {
    virtual ~Transport() = default;
    virtual bool Read(void* buf, size_t size, size_t* got) = 0;
    virtual bool Write(const void* buf, size_t size) = 0;
    // a transfer of no bytes, which ends a data phase that filled whole packets
    virtual bool WriteEnd() = 0;
    virtual size_t PacketSize() const = 0;
};

struct Entry {
    std::string name;
    bool dir = false;
    uint64_t size = 0;
    int64_t mtime = 0; // seconds since 1970
};

// A file being received; Write is called in order, Close at the end (ok false when it broke).
struct Sink {
    virtual ~Sink() = default;
    virtual bool Write(const void* data, size_t size) = 0;
    virtual bool Close(bool ok) = 0;
};

// A file being sent.
struct Source {
    virtual ~Source() = default;
    virtual bool Read(uint64_t off, void* buf, size_t size) = 0;
};

// One storage the computer sees. Paths are inside it, with "/" between parts and no leading "/"
// ("" is its root).
struct Storage {
    virtual ~Storage() = default;
    virtual std::string Description() const = 0;
    virtual bool ReadOnly() const { return false; }
    virtual void Space(uint64_t& total, uint64_t& free) = 0;
    virtual bool List(const std::string& dir, std::vector<Entry>& out) = 0;
    virtual bool Stat(const std::string& path, Entry& out) = 0;
    virtual std::unique_ptr<Source> Open(const std::string& path) = 0;
    // a new file of `size` bytes; null refuses it (busy, full, not allowed); `busy` asks the
    // computer to try again later
    virtual std::unique_ptr<Sink> Create(const std::string& path, uint64_t size, bool& busy) = 0;
    virtual bool MakeDir(const std::string& path) = 0;
    virtual bool Remove(const std::string& path, bool dir) = 0;
    virtual bool Rename(const std::string& from, const std::string& to) = 0;
};

// Operation, response and property codes used here (PTP / MTP).
enum : uint16_t {
    OpGetDeviceInfo = 0x1001, OpOpenSession = 0x1002, OpCloseSession = 0x1003, OpGetStorageIDs = 0x1004,
    OpGetStorageInfo = 0x1005, OpGetNumObjects = 0x1006, OpGetObjectHandles = 0x1007, OpGetObjectInfo = 0x1008,
    OpGetObject = 0x1009, OpDeleteObject = 0x100B, OpSendObjectInfo = 0x100C, OpSendObject = 0x100D,
    OpGetDevicePropDesc = 0x1014, OpGetDevicePropValue = 0x1015, OpGetPartialObject = 0x101B,
    OpGetObjectPropsSupported = 0x9801, OpGetObjectPropDesc = 0x9802, OpGetObjectPropValue = 0x9803,
    OpSetObjectPropValue = 0x9804, OpGetObjectPropList = 0x9805, OpSendObjectPropList = 0x9808,
    OpGetPartialObject64 = 0x95C1,

    RespOk = 0x2001, RespGeneralError = 0x2002, RespSessionNotOpen = 0x2003, RespInvalidTransaction = 0x2004,
    RespNotSupported = 0x2005, RespIncompleteTransfer = 0x2007, RespInvalidStorageId = 0x2008,
    RespInvalidObjectHandle = 0x2009, RespDevicePropNotSupported = 0x200A, RespStoreFull = 0x200C,
    RespStoreReadOnly = 0x200E, RespAccessDenied = 0x200F, RespDeviceBusy = 0x2019, RespInvalidParentObject = 0x201A,
    RespInvalidParameter = 0x201D, RespSessionAlreadyOpen = 0x201E, RespNoValidObjectInfo = 0x2015,
    RespInvalidObjectPropCode = 0xA801, RespSpecificationByGroupUnsupported = 0xA807, RespObjectTooLarge = 0xA809,

    PropStorageId = 0xDC01, PropObjectFormat = 0xDC02, PropProtectionStatus = 0xDC03, PropObjectSize = 0xDC04,
    PropObjectFileName = 0xDC07, PropDateCreated = 0xDC08, PropDateModified = 0xDC09, PropParentObject = 0xDC0B,
    PropPersistentUid = 0xDC41, PropName = 0xDC44,

    FormatUndefined = 0x3000, FormatAssociation = 0x3001,
};

// Little-endian building and reading of datasets.
class Writer {
public:
    void U8(uint8_t v) { m_b.push_back(v); }
    void U16(uint16_t v) { for (int i = 0; i < 2; i++) m_b.push_back(uint8_t(v >> (8 * i))); }
    void U32(uint32_t v) { for (int i = 0; i < 4; i++) m_b.push_back(uint8_t(v >> (8 * i))); }
    void U64(uint64_t v) { for (int i = 0; i < 8; i++) m_b.push_back(uint8_t(v >> (8 * i))); }
    void U128(uint64_t lo, uint64_t hi) { U64(lo); U64(hi); }
    void Str(const std::string& utf8); // a PTP string: its length in UTF-16 units with the null, then UTF-16LE
    void Array16(const std::vector<uint16_t>& v) { U32(uint32_t(v.size())); for (auto x : v) U16(x); }
    void Array32(const std::vector<uint32_t>& v) { U32(uint32_t(v.size())); for (auto x : v) U32(x); }
    std::vector<uint8_t>& Bytes() { return m_b; }

private:
    std::vector<uint8_t> m_b;
};

class Reader {
public:
    Reader(const uint8_t* p, size_t n) : m_p(p), m_n(n) {}
    bool U8(uint8_t& v);
    bool U16(uint16_t& v);
    bool U32(uint32_t& v);
    bool U64(uint64_t& v);
    bool Str(std::string& utf8);
    bool Skip(size_t n);
    size_t Left() const { return m_n - m_at; }

private:
    const uint8_t* m_p;
    size_t m_n, m_at = 0;
};

// The responder: one session at a time, one operation after another, until the cable goes or
// Stop. Storages are given with their ids (0x00010001, 0x00020001…).
class Responder {
public:
    Responder(Transport& usb, std::vector<std::shared_ptr<Storage>> storages, std::string device_name);
    // Serves until the transport fails; returns then.
    void Run();
    // A single operation (the tests drive it one by one); false when the transport failed.
    bool Serve();

private:
    struct Object {
        uint32_t storage = 0, parent = 0; // parent 0: the root of the storage
        std::string path;                 // inside the storage
        bool dir = false;
    };
    struct Pending { // the object announced by SendObjectInfo / SendObjectPropList
        bool valid = false;
        uint32_t storage = 0, parent = 0, handle = 0;
        std::string path;
        uint64_t size = 0;
        bool dir = false;
        std::unique_ptr<Sink> sink; // opened when announced, so a refusal comes before the data
    };

    bool ReadExact(void* buf, size_t size);
    bool ReadContainer(std::vector<uint8_t>& out, uint16_t& type, uint16_t& code, uint32_t& tid);
    bool ReadDataPhase(std::vector<uint8_t>& out, uint64_t max = 1 << 20);
    bool Respond(uint16_t code, std::vector<uint32_t> params = {});
    bool SendData(const std::vector<uint8_t>& data);
    bool SendStream(uint64_t size, const std::function<bool(uint64_t, void*, size_t)>& read);
    // drains a data phase the host sends to an operation that will fail
    bool Drain();

    Storage* StorageOf(uint32_t id);
    uint32_t HandleOf(uint32_t storage, uint32_t parent, const std::string& path, bool dir);
    bool Lookup(uint32_t handle, Object& out);
    std::string ParentPath(uint32_t storage, uint32_t parent, bool& ok);
    void Forget(uint32_t handle);

    bool DeviceInfo();
    bool StorageInfo(uint32_t id);
    bool ObjectHandles(uint32_t storage, uint32_t format, uint32_t parent, bool count_only);
    bool ObjectInfo(uint32_t handle);
    bool GetObject(uint32_t handle, uint64_t off, uint64_t max, bool partial, bool size64);
    bool SendObjectInfo(uint32_t storage, uint32_t parent);
    bool SendObjectPropList(uint32_t storage, uint32_t parent, uint32_t format, uint64_t size);
    bool SendObject();
    bool DeleteObject(uint32_t handle);
    bool PropsSupported();
    bool PropDesc(uint32_t prop);
    bool PropValue(uint32_t handle, uint32_t prop);
    bool SetPropValue(uint32_t handle, uint32_t prop);
    bool PropList(uint32_t handle, uint32_t format, uint32_t prop, uint32_t group, uint32_t depth);
    void PutProp(Writer& w, uint32_t handle, const Object& o, const Entry& e, uint16_t prop);

    Transport& m_usb;
    std::vector<std::shared_ptr<Storage>> m_storages;
    std::string m_name;
    std::vector<uint8_t> m_left; // bytes of a transfer not used yet
    bool m_session = false;
    uint32_t m_tid = 0;
    uint32_t m_next_handle = 1;
    std::map<uint32_t, Object> m_objects;
    std::map<std::string, uint32_t> m_by_key; // storage:path → handle
    Pending m_pending;
    uint16_t m_last_code = 0;  // the operation being served
    std::string m_info_name;   // the name SendObjectInfo gave
};

// The storage ids the responder gives: 0x00010001, 0x00020001…
uint32_t StorageId(size_t index);

} // namespace nxdock::mtp
