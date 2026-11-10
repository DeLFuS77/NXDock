// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// NXDock's MTP responder on the computer, with a fake cable: what a computer asks when it opens
// the console in its file explorer, copies a file in both ways (in one transfer with its header,
// and in many), announces a file over 4 GB the MTP way, renames, makes a folder and deletes; and
// that data phases filling whole packets are closed with a transfer of no bytes.
#include "mtp_proto.hpp"
#include "mtp_storage.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <sys/stat.h>

using namespace nxdock::mtp;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

struct FakeCable : Transport {
    std::deque<std::vector<uint8_t>> in;   // transfers from the computer
    std::vector<std::vector<uint8_t>> out; // transfers to it
    int ends = 0;
    bool Read(void* buf, size_t size, size_t* got) override {
        if (in.empty()) return false;
        auto& t = in.front();
        const size_t n = std::min(size, t.size());
        std::memcpy(buf, t.data(), n);
        *got = n;
        if (n == t.size()) in.pop_front();
        else t.erase(t.begin(), t.begin() + long(n));
        return true;
    }
    bool Write(const void* buf, size_t size) override {
        const auto* p = static_cast<const uint8_t*>(buf);
        out.emplace_back(p, p + size);
        return true;
    }
    bool WriteEnd() override {
        ends++;
        return true;
    }
    size_t PacketSize() const override { return 512; }
};

static uint32_t tid = 1;

static std::vector<uint8_t> Container(uint16_t type, uint16_t code, std::vector<uint32_t> params, const std::vector<uint8_t>& data = {},
                                      uint32_t length = 0) {
    Writer w;
    w.U32(length ? length : uint32_t(12 + 4 * params.size() + data.size()));
    w.U16(type);
    w.U16(code);
    w.U32(tid);
    for (auto p : params) w.U32(p);
    w.Bytes().insert(w.Bytes().end(), data.begin(), data.end());
    return w.Bytes();
}

// Sends a command (and a data phase), serves it, and gives the data phase and the response back.
struct Answer {
    std::vector<uint8_t> data;
    uint16_t code = 0;
    std::vector<uint32_t> params;
};

static Answer Ask(Responder& r, FakeCable& c, uint16_t op, std::vector<uint32_t> params, const std::vector<std::vector<uint8_t>>& data = {}) {
    tid++;
    c.out.clear();
    c.in.push_back(Container(1, op, params));
    for (const auto& d : data) c.in.push_back(d);
    Answer a;
    if (!r.Serve()) return a;
    // join the transfers: a data phase (type 2), then the response (type 3)
    std::vector<uint8_t> all;
    for (auto& t : c.out) all.insert(all.end(), t.begin(), t.end());
    size_t at = 0;
    while (at + 12 <= all.size()) {
        uint32_t len = uint32_t(all[at] | all[at + 1] << 8 | all[at + 2] << 16 | all[at + 3] << 24);
        const uint16_t type = uint16_t(all[at + 4] | all[at + 5] << 8);
        const uint16_t code = uint16_t(all[at + 6] | all[at + 7] << 8);
        if (type == 2) {
            if (len == 0xFFFFFFFF) len = uint32_t(all.size() - at); // not in these tests
            a.data.assign(all.begin() + long(at) + 12, all.begin() + long(at) + len);
        } else if (type == 3) {
            a.code = code;
            for (size_t p = at + 12; p + 4 <= at + len; p += 4) a.params.push_back(uint32_t(all[p] | all[p + 1] << 8 | all[p + 2] << 16 | all[p + 3] << 24));
        }
        at += len;
    }
    return a;
}

static std::string ReadName(const std::vector<uint8_t>& info) {
    // the file name of an ObjectInfo dataset, after its fixed part
    Reader r(info.data(), info.size());
    std::string name;
    r.Skip(52);
    r.Str(name);
    return name;
}

static void Write(const std::string& path, const std::string& data) {
    FILE* f = std::fopen(path.c_str(), "wb");
    std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
}

static std::string ReadFile(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return "<none>";
    std::string s;
    char b[4096];
    size_t n;
    while ((n = std::fread(b, 1, sizeof(b), f)) > 0) s.append(b, n);
    std::fclose(f);
    return s;
}

// A storage that is busy or refuses, as an install folder can be.
struct Refusing : Storage {
    bool busy = true;
    std::string Description() const override { return "Install"; }
    void Space(uint64_t& t, uint64_t& f) override { t = f = 1ull << 40; }
    bool List(const std::string&, std::vector<Entry>&) override { return true; }
    bool Stat(const std::string&, Entry&) override { return false; }
    std::unique_ptr<Source> Open(const std::string&) override { return nullptr; }
    std::unique_ptr<Sink> Create(const std::string&, uint64_t, bool& b) override {
        b = busy;
        return nullptr;
    }
    bool MakeDir(const std::string&) override { return false; }
    bool Remove(const std::string&, bool) override { return false; }
    bool Rename(const std::string&, const std::string&) override { return false; }
};

int main(int argc, char** argv) {
    const std::string root = std::string(argc > 1 ? argv[1] : "/tmp") + "/mtp_test";
    std::system(("rm -rf " + root + " && mkdir -p " + root + "/games").c_str());
    std::string big(1500, '\0');
    for (size_t i = 0; i < big.size(); i++) big[i] = char(i * 31);
    Write(root + "/hello.txt", "hello console");
    Write(root + "/games/big.bin", big);

    FakeCable cable;
    auto sd = std::make_shared<FolderStorage>(root, "SD card");
    auto install = std::make_shared<Refusing>();
    Responder r(cable, {sd, install}, "NXDock");

    // before a session: device info answers, the rest does not
    auto a = Ask(r, cable, OpGetDeviceInfo, {});
    CHECK(a.code == RespOk && a.data.size() > 40);
    {
        Reader rd(a.data.data(), a.data.size());
        uint16_t version;
        uint32_t ext;
        uint16_t ext_version;
        std::string desc;
        rd.U16(version);
        rd.U32(ext);
        rd.U16(ext_version);
        rd.Str(desc);
        CHECK(version == 100 && ext == 6 && desc.find("microsoft.com") != std::string::npos);
    }
    CHECK(Ask(r, cable, OpGetStorageIDs, {}).code == RespSessionNotOpen);
    CHECK(Ask(r, cable, OpOpenSession, {1}).code == RespOk);
    CHECK(Ask(r, cable, OpOpenSession, {1}).code == RespSessionAlreadyOpen);

    a = Ask(r, cable, OpGetStorageIDs, {});
    CHECK(a.code == RespOk && a.data.size() == 4 + 8);
    const uint32_t sd_id = StorageId(0), inst_id = StorageId(1);
    a = Ask(r, cable, OpGetStorageInfo, {sd_id});
    CHECK(a.code == RespOk);
    CHECK(Ask(r, cable, OpGetStorageInfo, {0x12345}).code == RespInvalidStorageId);

    // the root of the SD card: a folder and a file
    a = Ask(r, cable, OpGetObjectHandles, {sd_id, 0, 0xFFFFFFFF});
    CHECK(a.code == RespOk);
    uint32_t n = a.data.size() >= 4 ? uint32_t(a.data[0]) : 0;
    CHECK(n == 2);
    uint32_t hello = 0, games = 0;
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t h = uint32_t(a.data[4 + 4 * i]);
        const auto info = Ask(r, cable, OpGetObjectInfo, {h});
        const std::string name = ReadName(info.data);
        if (name == "hello.txt") hello = h;
        if (name == "games") games = h;
    }
    CHECK(hello && games);
    CHECK(Ask(r, cable, OpGetNumObjects, {sd_id, 0, 0xFFFFFFFF}).params.size() == 1);

    // reading: whole, partial, and a file whose data phase ends on a whole packet
    a = Ask(r, cable, OpGetObject, {hello});
    CHECK(a.code == RespOk && std::string(a.data.begin(), a.data.end()) == "hello console");
    a = Ask(r, cable, OpGetPartialObject, {hello, 6, 4});
    CHECK(a.code == RespOk && std::string(a.data.begin(), a.data.end()) == "cons" && a.params.size() == 1 && a.params[0] == 4);
    a = Ask(r, cable, OpGetObjectHandles, {sd_id, 0, games});
    const uint32_t bigh = a.data.size() >= 8 ? uint32_t(a.data[4]) : 0;
    CHECK(bigh != 0);
    Write(root + "/games/big.bin", big.substr(0, 1024 - 12)); // header + data = 1024: two whole packets
    const int ends = cable.ends;
    a = Ask(r, cable, OpGetObject, {bigh});
    CHECK(a.code == RespOk && a.data.size() == 1024 - 12 && cable.ends == ends + 1);

    // the properties a file explorer lists
    a = Ask(r, cable, OpGetObjectPropsSupported, {0x3000});
    CHECK(a.code == RespOk && a.data.size() > 4);
    a = Ask(r, cable, OpGetObjectPropList, {0xFFFFFFFF, 0, 0xFFFFFFFF, 0, 1});
    CHECK(a.code == RespOk && a.data.size() > 4 && a.data[0] >= 2 * 10);
    a = Ask(r, cable, OpGetObjectPropValue, {hello, PropObjectSize});
    CHECK(a.code == RespOk && a.data.size() == 8 && a.data[0] == 13);

    // writing a small file: the info, then the data with its header in one transfer
    {
        Writer info;
        info.U32(sd_id);
        info.U16(0x3000);
        info.U16(0);
        info.U32(5);
        info.U16(0);
        for (int i = 0; i < 6; i++) info.U32(0);
        info.U32(0);
        info.U16(0);
        info.U32(0);
        info.U32(0);
        info.Str("new.txt");
        info.Str("");
        info.Str("");
        info.Str("");
        a = Ask(r, cable, OpSendObjectInfo, {sd_id, 0xFFFFFFFF}, {Container(2, OpSendObjectInfo, {}, info.Bytes())});
        CHECK(a.code == RespOk && a.params.size() == 3);
        const std::string five = "12345";
        a = Ask(r, cable, OpSendObject, {}, {Container(2, OpSendObject, {}, std::vector<uint8_t>(five.begin(), five.end()))});
        CHECK(a.code == RespOk && ReadFile(root + "/new.txt") == "12345");
    }

    // a file announced with SendObjectPropList (as for files over 4 GB), sent in many transfers
    {
        std::string data(5000, 'x');
        for (size_t i = 0; i < data.size(); i++) data[i] = char('a' + i % 26);
        Writer props;
        props.U32(1);
        props.U32(0);
        props.U16(PropObjectFileName);
        props.U16(0xFFFF);
        props.Str("über game.nsp");
        a = Ask(r, cable, OpSendObjectPropList, {sd_id, games, 0x3000, 0, uint32_t(data.size())},
                {Container(2, OpSendObjectPropList, {}, props.Bytes())});
        CHECK(a.code == RespOk && a.params.size() == 4);
        std::vector<std::vector<uint8_t>> parts;
        const auto whole = Container(2, OpSendObject, {}, std::vector<uint8_t>(data.begin(), data.end()));
        parts.push_back(std::vector<uint8_t>(whole.begin(), whole.begin() + 12)); // the header alone
        for (size_t at = 12; at < whole.size(); at += 1000) {
            parts.push_back(std::vector<uint8_t>(whole.begin() + long(at), whole.begin() + long(std::min(whole.size(), at + 1000))));
        }
        a = Ask(r, cable, OpSendObject, {}, parts);
        CHECK(a.code == RespOk && ReadFile(root + "/games/über game.nsp") == data);
    }

    // a file of more than 4 GB through SendObjectInfo is refused (it needs SendObjectPropList)
    {
        Writer info;
        info.U32(sd_id);
        info.U16(0x3000);
        info.U16(0);
        info.U32(0xFFFFFFFF);
        info.U16(0);
        for (int i = 0; i < 6; i++) info.U32(0);
        info.U32(0);
        info.U16(0);
        info.U32(0);
        info.U32(0);
        info.Str("huge.xci");
        a = Ask(r, cable, OpSendObjectInfo, {sd_id, 0xFFFFFFFF}, {Container(2, OpSendObjectInfo, {}, info.Bytes())});
        CHECK(a.code == RespObjectTooLarge);
    }

    // a folder, a rename, a delete, and names that are refused
    {
        Writer info;
        info.U32(sd_id);
        info.U16(0x3001);
        info.U16(0);
        info.U32(0);
        info.U16(0);
        for (int i = 0; i < 6; i++) info.U32(0);
        info.U32(0);
        info.U16(1);
        info.U32(0);
        info.U32(0);
        info.Str("saves");
        a = Ask(r, cable, OpSendObjectInfo, {sd_id, 0xFFFFFFFF}, {Container(2, OpSendObjectInfo, {}, info.Bytes())});
        CHECK(a.code == RespOk);
        struct stat st{};
        CHECK(stat((root + "/saves").c_str(), &st) == 0 && S_ISDIR(st.st_mode));
    }
    {
        Writer name;
        name.Str("renamed.txt");
        a = Ask(r, cable, OpSetObjectPropValue, {hello, PropObjectFileName}, {Container(2, OpSetObjectPropValue, {}, name.Bytes())});
        CHECK(a.code == RespOk && ReadFile(root + "/renamed.txt") == "hello console");
        CHECK(ReadName(Ask(r, cable, OpGetObjectInfo, {hello}).data) == "renamed.txt");
        Writer bad;
        bad.Str("../evil");
        a = Ask(r, cable, OpSetObjectPropValue, {hello, PropObjectFileName}, {Container(2, OpSetObjectPropValue, {}, bad.Bytes())});
        CHECK(a.code == RespInvalidParameter);
    }
    CHECK(Ask(r, cable, OpDeleteObject, {hello}).code == RespOk && ReadFile(root + "/renamed.txt") == "<none>");
    CHECK(Ask(r, cable, OpGetObjectInfo, {hello}).code == RespInvalidObjectHandle);

    // an install folder that is busy says so, and the data that follows anyway is dropped
    {
        Writer props;
        props.U32(1);
        props.U32(0);
        props.U16(PropObjectFileName);
        props.U16(0xFFFF);
        props.Str("game.nsp");
        a = Ask(r, cable, OpSendObjectPropList, {inst_id, 0xFFFFFFFF, 0x3000, 0, 10}, {Container(2, OpSendObjectPropList, {}, props.Bytes())});
        CHECK(a.code == RespDeviceBusy);
        a = Ask(r, cable, OpSendObject, {}, {Container(2, OpSendObject, {}, std::vector<uint8_t>(10, 1))});
        CHECK(a.code == RespNoValidObjectInfo);
        install->busy = false;
        a = Ask(r, cable, OpSendObjectPropList, {inst_id, 0xFFFFFFFF, 0x3000, 0, 10}, {Container(2, OpSendObjectPropList, {}, props.Bytes())});
        CHECK(a.code == RespAccessDenied);
        Ask(r, cable, OpSendObject, {}, {Container(2, OpSendObject, {}, std::vector<uint8_t>(10, 1))});
    }

    CHECK(Ask(r, cable, 0x9999, {}).code == RespNotSupported);
    CHECK(Ask(r, cable, OpCloseSession, {}).code == RespOk);
    // the cable gone: Serve ends
    cable.in.clear();
    CHECK(!r.Serve());

    if (failures) {
        std::printf("mtp_test: %d failures\n", failures);
        return 1;
    }
    std::printf("mtp_test: ok\n");
    return 0;
}
