// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
// Based on owo.cpp of Sphaira by ITotalJustice and the Sphaira contributors (GPL-3.0), itself
// based on hacbrewpack (the RomFS) and yati (installing). See NOTICE.
//
// NXDock's shortcut on the HOME menu: three NCAs made in memory and installed to the SD card.
//  - program: the ExeFS of a small loader (hbl/, from nx-hbloader) and a RomFS saying which .nro
//    to open (nextNroPath, nextArgv);
//  - control: the NACP and the icon of NXDock.nro itself, so the name and the icon always match it;
//  - meta: the CNMT of an Application with those two.
// Their headers are encrypted with the header key the console makes (spl); nothing is read from
// prod.keys. Like any homebrew shortcut, it needs sigpatches to open.
#include "forwarder.hpp"

#include "yati/nx/keys.hpp"
#include "yati/nx/nca.hpp"
#include "yati/nx/ncm.hpp"
#include "yati/nx/npdm.hpp"
#include "yati/nx/ns.hpp"
#include "defines.hpp"
#include "log.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <sys/stat.h>
#include <vector>

extern "C" {
extern int __system_argc;
extern char** __system_argv;
}

namespace nxdock::forwarder {
namespace {

namespace nca = sphaira::nca;
namespace ncm = sphaira::ncm;
namespace npdm = sphaira::npdm;
namespace ns = sphaira::ns;
namespace keys = sphaira::keys;

constexpr const char* kDefaultNro = "sdmc:/switch/NXDock/NXDock.nro";

constexpr u32 IVFC_MAX_LEVEL = 6;
constexpr u32 IVFC_HASH_BLOCK_SIZE = 0x4000;
constexpr u32 PFS0_EXEFS_HASH_BLOCK_SIZE = 0x10000;
constexpr u32 PFS0_META_HASH_BLOCK_SIZE = 0x1000;
constexpr u32 PFS0_PADDING_SIZE = 0x200;
constexpr u32 ROMFS_ENTRY_EMPTY = 0xFFFFFFFF;
constexpr u32 ROMFS_FILEPARTITION_OFS = 0x200;

// the loader, built from hbl/ next to NXDock (nx-hbloader as Sphaira changed it, ISC)
constexpr const u8 HBL_MAIN_DATA[]{
#embed <exefs/main>
};

constexpr const u8 HBL_NPDM_DATA[]{
#embed <exefs/main.npdm>
};

// stdio-like wrapper for std::vector
struct BufHelper {
    void write(const void* data, u64 size) {
        if (offset + size >= buf.size()) {
            buf.resize(offset + size);
        }
        std::memcpy(buf.data() + offset, data, size);
        offset += size;
    }

    void seek(u64 where_to) {
        offset = where_to;
    }

    u64 tell() const {
        return offset;
    }

    std::vector<u8> buf;
    u64 offset{};
};

struct NcaEntry {
    NcaEntry(const BufHelper& buf, NcmContentType _type) : data{buf.buf}, type{u8(_type)} {
        sha256CalculateHash(hash, data.data(), data.size());
    }

    std::vector<u8> data;
    u8 type;
    u8 hash[SHA256_HASH_SIZE];
};

struct CnmtHeader {
    u64 title_id;
    u32 title_version;
    u8 meta_type; // NcmContentMetaType
    u8 _0xD;
    NcmContentMetaHeader meta_header;
    u8 install_type; // NcmContentInstallType
    u8 _0x17;
    u32 required_sys_version;
    u8 _0x1C[0x4];
};
static_assert(sizeof(CnmtHeader) == 0x20);

struct NcmContentMetaData {
    NcmContentMetaHeader header;
    NcmApplicationMetaExtendedHeader extended;
    NcmContentInfo infos[3];
};

struct Pfs0Header {
    u32 magic;
    u32 total_files;
    u32 string_table_size;
    u32 padding;
};

struct Pfs0FileTable {
    u64 data_offset;
    u64 data_size;
    u32 name_offset;
    u32 padding;
};

struct FileEntry {
    std::string name;
    std::vector<u8> data;
};

using FileEntries = std::vector<FileEntry>;

void add_file_entry(FileEntries& entries, const char* name, const void* data, u64 size) {
    FileEntry entry;
    entry.name = name;
    entry.data.resize(size);
    std::memcpy(entry.data.data(), data, size);
    entries.emplace_back(std::move(entry));
}

u64 write_padding(BufHelper& buf, u64 off, u64 block) {
    const u64 size = block - (off % block);
    if (size) {
        std::vector<u8> padding(size);
        buf.write(padding.data(), padding.size());
    }
    return size;
}

u32 calc_path_hash(u32 parent, const u8* path, u32 start, u32 path_len) {
    u32 hash = parent ^ 123456789;
    for (u32 i = 0; i < path_len; i++) {
        hash = (hash >> 5) | (hash << 27);
        hash ^= path[start + i];
    }
    return hash;
}

u32 align32(u32 offset, u32 alignment) {
    return (offset + (alignment - 1)) & ~(alignment - 1);
}

u64 align64(u64 offset, u64 alignment) {
    return (offset + (alignment - 1)) & ~(alignment - 1);
}

u32 romfs_get_hash_table_count(u32 num_entries) {
    if (num_entries < 3) {
        return 3;
    } else if (num_entries < 19) {
        return num_entries | 1;
    }
    u32 count = num_entries;
    while (count % 2 == 0 || count % 3 == 0 || count % 5 == 0 || count % 7 == 0 || count % 11 == 0 || count % 13 == 0 || count % 17 == 0) {
        count++;
    }
    return count;
}

// NXDock: a RomFS of files at its root only, laid out in the order given (Sphaira's linked lists
// replaced by the order of the entries; the result is the same for its one or two files).
std::vector<u8> romfs_build(const FileEntries& entries, u64* out_size) {
    const u32 dir_table_size = sizeof(romfs_dir); // the root, with no name
    u32 file_table_size = 0;
    for (const auto& e : entries) {
        file_table_size += sizeof(romfs_file) + align32(e.name.length() - 1, 4);
    }
    const u32 dir_hash_count = romfs_get_hash_table_count(1);
    const u32 file_hash_count = romfs_get_hash_table_count(entries.size());
    std::vector<u32> dir_hash_table(dir_hash_count, ROMFS_ENTRY_EMPTY);
    std::vector<u32> file_hash_table(file_hash_count, ROMFS_ENTRY_EMPTY);
    std::vector<u8> dir_table(dir_table_size);
    std::vector<u8> file_table(file_table_size);

    // where each file's entry and data go
    std::vector<u32> entry_offsets(entries.size());
    std::vector<u64> data_offsets(entries.size());
    u64 partition_size = 0;
    u32 entry_offset = 0;
    for (size_t i = 0; i < entries.size(); i++) {
        partition_size = align64(partition_size, 0x10);
        data_offsets[i] = partition_size;
        partition_size += entries[i].data.size();
        entry_offsets[i] = entry_offset;
        entry_offset += sizeof(romfs_file) + align32(entries[i].name.length() - 1, 4);
    }

    for (size_t i = 0; i < entries.size(); i++) {
        auto cur = (romfs_file*)(file_table.data() + entry_offsets[i]);
        cur->parent = 0;
        cur->sibling = i + 1 < entries.size() ? entry_offsets[i + 1] : ROMFS_ENTRY_EMPTY;
        cur->dataOff = data_offsets[i];
        cur->dataSize = entries[i].data.size();
        const u32 name_size = entries[i].name.length() - 1; // without the leading '/'
        const u32 hash = calc_path_hash(0, (const u8*)entries[i].name.c_str(), 1, name_size);
        cur->nextHash = file_hash_table[hash % file_hash_count];
        file_hash_table[hash % file_hash_count] = entry_offsets[i];
        cur->nameLen = name_size;
        std::memcpy(cur->name, entries[i].name.c_str() + 1, name_size);
    }

    {
        auto root = (romfs_dir*)dir_table.data();
        root->parent = 0;
        root->sibling = ROMFS_ENTRY_EMPTY;
        root->childDir = ROMFS_ENTRY_EMPTY;
        root->childFile = entries.empty() ? ROMFS_ENTRY_EMPTY : 0;
        const u32 hash = calc_path_hash(0, nullptr, 0, 0);
        root->nextHash = dir_hash_table[hash % dir_hash_count];
        dir_hash_table[hash % dir_hash_count] = 0;
        root->nameLen = 0;
    }

    romfs_header header{};
    header.headerSize = sizeof(header);
    header.fileHashTableSize = file_hash_count * 4;
    header.fileTableSize = file_table_size;
    header.dirHashTableSize = dir_hash_count * 4;
    header.dirTableSize = dir_table_size;
    header.fileDataOff = ROMFS_FILEPARTITION_OFS;
    header.dirHashTableOff = align64(partition_size + ROMFS_FILEPARTITION_OFS, 4);
    header.dirTableOff = header.dirHashTableOff + header.dirHashTableSize;
    header.fileHashTableOff = header.dirTableOff + dir_table_size;
    header.fileTableOff = header.fileHashTableOff + header.fileHashTableSize;

    BufHelper buf;
    buf.write(&header, sizeof(header));
    for (size_t i = 0; i < entries.size(); i++) {
        buf.seek(data_offsets[i] + ROMFS_FILEPARTITION_OFS);
        buf.write(entries[i].data.data(), entries[i].data.size());
    }
    buf.seek(header.dirHashTableOff);
    buf.write(dir_hash_table.data(), header.dirHashTableSize);
    buf.write(dir_table.data(), dir_table.size());
    buf.write(file_hash_table.data(), header.fileHashTableSize);
    buf.write(file_table.data(), file_table.size());

    buf.seek(buf.buf.size());
    *out_size = buf.tell();
    write_padding(buf, buf.tell(), IVFC_HASH_BLOCK_SIZE);
    return buf.buf;
}

bool npdm_patch_kc(std::vector<u8>& npdm, u32 off, u32 size, u32 bitmask, u32 value) {
    const u32 pattern = BIT(bitmask) - 1;
    const u32 mask = BIT(bitmask) | pattern;
    for (u32 i = 0; i < size; i += 4) {
        u32 cup;
        std::memcpy(&cup, npdm.data() + off + i, sizeof(cup));
        if ((cup & mask) == pattern) {
            cup = value | pattern;
            std::memcpy(npdm.data() + off + i, &cup, sizeof(cup));
            return true;
        }
    }
    return false;
}

// the kernel flags of the loader: cores 0 to 2, priorities 28 to 59, as a game
constexpr u32 kKernelFlags = ((((2u << 8) | 0u) << 6 | 28u) << 6 | 59u) << 4;
static_assert((kKernelFlags | 0x7) == 0x020073B7);

// Atmosphère 1.8.0 moved the debug flag of the NPDM (HOS 19.0.0): as Sphaira, it is set when that
// version or a newer one runs.
bool WantsSvcDebug() {
    if (R_FAILED(splInitialize())) {
        return false;
    }
    ON_SCOPE_EXIT(splExit());
    u64 ver{};
    if (R_FAILED(splGetConfig((SplConfigItem)65000, &ver))) { // the version of Exosphère
        return false;
    }
    return (ver >> 40) >= MAKEHOSVERSION(1, 8, 0);
}

bool patch_npdm(std::vector<u8>& data, u64 tid) {
    constexpr u8 ADDRESS_SPACE_SHIFT = 1;
    constexpr u8 ADDRESS_SPACE_MASK = 0x7 << ADDRESS_SPACE_SHIFT;
    if (data.size() < sizeof(npdm::Meta)) {
        return false;
    }
    npdm::Meta meta{};
    npdm::Aci0 aci0{};
    npdm::Acid acid{};
    std::memcpy(&meta, data.data(), sizeof(meta));
    if (u64(meta.aci0_offset) + sizeof(aci0) > data.size() || u64(meta.acid_offset) + sizeof(acid) > data.size()) {
        return false;
    }
    std::memcpy(&aci0, data.data() + meta.aci0_offset, sizeof(aci0));
    std::memcpy(&acid, data.data() + meta.acid_offset, sizeof(acid));

    std::memset(meta.title_name, 0, sizeof(meta.title_name));
    std::memcpy(meta.title_name, "NXDock", 6);
    std::memset(meta.product_code, 0, sizeof(meta.product_code));
    meta.flags = (meta.flags & ~ADDRESS_SPACE_MASK) | (1 << ADDRESS_SPACE_SHIFT); // 36-bit, as Sphaira
    aci0.program_id = tid;
    acid.program_id_min = tid;
    acid.program_id_max = tid;

    const bool aci0_ok = npdm_patch_kc(data, meta.aci0_offset + aci0.kac_offset, aci0.kac_size, 3, kKernelFlags);
    const bool acid_ok = npdm_patch_kc(data, meta.acid_offset + acid.kac_offset, acid.kac_size, 3, kKernelFlags);
    if (WantsSvcDebug()) {
        npdm_patch_kc(data, meta.aci0_offset + aci0.kac_offset, aci0.kac_size, 16, BIT(19));
        npdm_patch_kc(data, meta.acid_offset + acid.kac_offset, acid.kac_size, 16, BIT(19));
    }

    std::memcpy(data.data(), &meta, sizeof(meta));
    std::memcpy(data.data() + meta.aci0_offset, &aci0, sizeof(aci0));
    std::memcpy(data.data() + meta.acid_offset, &acid, sizeof(acid));
    return aci0_ok && acid_ok;
}

void patch_nacp(NacpStruct& nacp, u64 tid) {
    nacp.startup_user_account = 0x00; // no profile to pick
    nacp.user_account_switch_lock = 0x00;
    nacp.add_on_content_registration_type = 0x01; // on demand
    nacp.screenshot = 0x0;                        // allowed
    nacp.video_capture = 0x2;                     // auto
    nacp.logo_type = 0x2;
    nacp.logo_handling = 0x0;
    nacp.data_loss_confirmation = 0x0; // no saves
    nacp.required_network_service_license_on_launch = 0x0;
    const char error_code[] = "nxdock";
    static_assert(sizeof(error_code) <= 9);
    nacp.application_error_code_category = 0; // a char[8] in truth
    std::memcpy(&nacp.application_error_code_category, error_code, sizeof(error_code) - 1);

    nacp.presence_group_id = tid;
    nacp.save_data_owner_id = tid;
    nacp.pseudo_device_id_seed = tid;
    nacp.add_on_content_base_id = tid ^ 0x1000;
    for (auto& id : nacp.local_communication_id) {
        id = tid;
    }

    nacp.play_log_policy = 0x0;
    nacp.play_log_query_capability = 0x0;

    // no save data is made for it
    nacp.user_account_save_data_size = 0x0;
    nacp.user_account_save_data_journal_size = 0x0;
    nacp.device_save_data_size = 0x0;
    nacp.device_save_data_journal_size = 0x0;
    nacp.user_account_save_data_size_max = 0x0;
    nacp.user_account_save_data_journal_size_max = 0x0;
    nacp.device_save_data_size_max = 0x0;
    nacp.device_save_data_journal_size_max = 0x0;
}

std::vector<u8> build_pfs0(const FileEntries& entries) {
    BufHelper buf;
    Pfs0Header header{};
    std::vector<Pfs0FileTable> file_table(entries.size());
    std::vector<char> string_table;
    u64 string_offset{};
    u64 data_offset{};

    for (u32 i = 0; i < entries.size(); i++) {
        file_table[i].data_offset = data_offset;
        file_table[i].data_size = entries[i].data.size();
        file_table[i].name_offset = string_offset;
        file_table[i].padding = 0;
        string_table.resize(string_offset + entries[i].name.length() + 1);
        std::memcpy(string_table.data() + string_offset, entries[i].name.c_str(), entries[i].name.length() + 1);
        data_offset += entries[i].data.size();
        string_offset += entries[i].name.length() + 1;
    }
    string_table.resize((string_table.size() + 0x1F) & ~0x1F);

    header.magic = 0x30534650; // PFS0
    header.total_files = entries.size();
    header.string_table_size = string_table.size();
    header.padding = 0;

    buf.write(&header, sizeof(header));
    buf.write(file_table.data(), sizeof(Pfs0FileTable) * file_table.size());
    buf.write(string_table.data(), string_table.size());
    for (const auto& e : entries) {
        buf.write(e.data.data(), e.data.size());
    }
    return buf.buf;
}

std::vector<u8> build_pfs0_hash_table(const std::vector<u8>& pfs0, u32 block_size) {
    BufHelper buf;
    u8 hash[SHA256_HASH_SIZE];
    u32 read_size = block_size;
    for (u32 i = 0; i < pfs0.size(); i += read_size) {
        if (i + read_size >= pfs0.size()) {
            read_size = pfs0.size() - i;
        }
        sha256CalculateHash(hash, pfs0.data() + i, read_size);
        buf.write(hash, sizeof(hash));
    }
    return buf.buf;
}

void write_nca_padding(BufHelper& buf) {
    write_padding(buf, buf.tell(), 0x200);
}

void nca_encrypt_header(nca::Header* header, const u8* key) {
    Aes128XtsContext ctx{};
    aes128XtsContextCreate(&ctx, key, key + 0x10, true);
    u8 sector{};
    for (u64 pos = 0; pos < 0xC00; pos += 0x200) {
        aes128XtsContextResetSector(&ctx, sector++, true);
        aes128XtsEncrypt(&ctx, (u8*)header + pos, (const u8*)header + pos, 0x200);
    }
}

void write_nca_section(nca::Header& nca_header, u8 index, u64 start, u64 end) {
    auto& section = nca_header.fs_table[index];
    section.media_start_offset = start / 0x200;
    section.media_end_offset = end / 0x200;
    section._0x8[0] = 0x1; // always 1
}

void write_nca_fs_header_pfs0(nca::Header& nca_header, u8 index, const u8* master_hash, u64 hash_table_size, u32 block_size) {
    auto& fs_header = nca_header.fs_header[index];
    fs_header.hash_type = nca::HashType_HierarchicalSha256;
    fs_header.fs_type = nca::FileSystemType_PFS0;
    fs_header.version = 0x2;
    fs_header.hash_data.hierarchical_sha256_data.layer_count = 0x2;
    fs_header.hash_data.hierarchical_sha256_data.block_size = block_size;
    fs_header.encryption_type = nca::EncryptionType_None;
    fs_header.hash_data.hierarchical_sha256_data.hash_layer.size = hash_table_size;
    std::memcpy(fs_header.hash_data.hierarchical_sha256_data.master_hash, master_hash, SHA256_HASH_SIZE);
    sha256CalculateHash(&nca_header.fs_header_hash[index], &fs_header, sizeof(fs_header));
}

void write_nca_fs_header_romfs(nca::Header& nca_header, u8 index) {
    auto& fs_header = nca_header.fs_header[index];
    fs_header.hash_type = nca::HashType_HierarchicalIntegrity;
    fs_header.fs_type = nca::FileSystemType_RomFS;
    fs_header.version = 0x2;
    fs_header.hash_data.integrity_meta_info.magic = 0x43465649; // IVFC
    fs_header.hash_data.integrity_meta_info.version = 0x20000;
    fs_header.hash_data.integrity_meta_info.master_hash_size = SHA256_HASH_SIZE;
    fs_header.hash_data.integrity_meta_info.info_level_hash.max_layers = 0x7;
    fs_header.encryption_type = nca::EncryptionType_None;
    fs_header.hash_data.integrity_meta_info.info_level_hash.levels[5].block_size = 0x0E; // 0x4000
    sha256CalculateHash(&nca_header.fs_header_hash[index], &fs_header, sizeof(fs_header));
}

void write_nca_pfs0(nca::Header& nca_header, u8 index, const FileEntries& entries, u32 block_size, BufHelper& buf) {
    const auto pfs0 = build_pfs0(entries);
    const auto hash_table = build_pfs0_hash_table(pfs0, block_size);
    u8 master_hash[SHA256_HASH_SIZE];
    sha256CalculateHash(master_hash, hash_table.data(), hash_table.size());

    buf.write(hash_table.data(), hash_table.size());
    const auto padding_size = write_padding(buf, hash_table.size(), PFS0_PADDING_SIZE);
    nca_header.fs_header[index].hash_data.hierarchical_sha256_data.pfs0_layer.offset = hash_table.size() + padding_size;
    nca_header.fs_header[index].hash_data.hierarchical_sha256_data.pfs0_layer.size = pfs0.size();

    buf.write(pfs0.data(), pfs0.size());
    write_nca_padding(buf);

    const u64 section_start = index == 0 ? sizeof(nca_header) : u64(nca_header.fs_table[index - 1].media_end_offset) * 0x200;
    write_nca_section(nca_header, index, section_start, buf.tell());
    write_nca_fs_header_pfs0(nca_header, index, master_hash, hash_table.size(), block_size);
}

std::vector<u8> ivfc_create_level(const std::vector<u8>& src) {
    BufHelper buf;
    u8 hash[SHA256_HASH_SIZE];
    u64 read_size = IVFC_HASH_BLOCK_SIZE;
    for (u32 i = 0; i < src.size(); i += read_size) {
        if (i + read_size >= src.size()) {
            read_size = src.size() - i;
        }
        sha256CalculateHash(hash, src.data() + i, read_size);
        buf.write(hash, sizeof(hash));
    }
    write_padding(buf, buf.tell(), IVFC_HASH_BLOCK_SIZE);
    return buf.buf;
}

void write_nca_romfs(nca::Header& nca_header, u8 index, const FileEntries& entries, BufHelper& buf) {
    auto& meta_info = nca_header.fs_header[index].hash_data.integrity_meta_info;
    auto& info_level_hash = meta_info.info_level_hash;

    std::vector<u8> ivfc[IVFC_MAX_LEVEL];
    u64 data_size = 0;
    ivfc[5] = romfs_build(entries, &data_size);
    info_level_hash.levels[5].hash_data_size = data_size;
    for (int b = 4; b >= 0; b--) {
        ivfc[b] = ivfc_create_level(ivfc[b + 1]);
        info_level_hash.levels[b].hash_data_size = ivfc[b].size();
        info_level_hash.levels[b].block_size = 0x0E; // 0x4000
    }
    info_level_hash.levels[0].logical_offset = 0;
    for (int i = 1; i <= 5; i++) {
        info_level_hash.levels[i].logical_offset = info_level_hash.levels[i - 1].logical_offset + info_level_hash.levels[i - 1].hash_data_size;
    }
    for (const auto& iv : ivfc) {
        buf.write(iv.data(), iv.size());
    }
    write_nca_padding(buf);

    sha256CalculateHash(meta_info.master_hash, ivfc[0].data(), ivfc[0].size());

    const u64 section_start = index == 0 ? sizeof(nca_header) : u64(nca_header.fs_table[index - 1].media_end_offset) * 0x200;
    write_nca_section(nca_header, index, section_start, buf.tell());
    write_nca_fs_header_romfs(nca_header, index);
}

void write_nca_header_encrypted(nca::Header& nca_header, u64 tid, const keys::Keys& k, nca::ContentType type, BufHelper& buf) {
    nca_header.magic = NCA3_MAGIC;
    nca_header.distribution_type = nca::DistributionType_System;
    nca_header.content_type = type;
    nca_header.program_id = tid;
    nca_header.sdk_version = 0x000C1100;
    nca_header.size = buf.tell();
    nca_encrypt_header(&nca_header, k.header_key);
    buf.seek(0);
    buf.write(&nca_header, sizeof(nca_header));
}

NcaEntry create_program_nca(u64 tid, const keys::Keys& k, const FileEntries& exefs, const FileEntries& romfs) {
    BufHelper buf;
    nca::Header nca_header{};
    buf.write(&nca_header, sizeof(nca_header));
    write_nca_pfs0(nca_header, 0, exefs, PFS0_EXEFS_HASH_BLOCK_SIZE, buf);
    write_nca_romfs(nca_header, 1, romfs, buf);
    write_nca_header_encrypted(nca_header, tid, k, nca::ContentType_Program, buf);
    return {buf, NcmContentType_Program};
}

NcaEntry create_control_nca(u64 tid, const keys::Keys& k, const FileEntries& romfs) {
    BufHelper buf;
    nca::Header nca_header{};
    buf.write(&nca_header, sizeof(nca_header));
    write_nca_romfs(nca_header, 0, romfs, buf);
    write_nca_header_encrypted(nca_header, tid, k, nca::ContentType_Control, buf);
    return {buf, NcmContentType_Control};
}

struct MetaOut {
    NcmContentMetaKey key{};
    ncm::ContentStorageRecord record{};
    NcmContentMetaData data{};
};

NcaEntry create_meta_nca(u64 tid, const keys::Keys& k, const std::vector<NcaEntry>& ncas, MetaOut& out) {
    CnmtHeader cnmt_header{};
    NcmApplicationMetaExtendedHeader cnmt_extended{};
    NcmPackagedContentInfo packaged[2]{};
    u8 digest[0x20]{};

    cnmt_header.title_id = tid;
    cnmt_header.title_version = 0;
    cnmt_header.meta_type = NcmContentMetaType_Application;
    cnmt_header.meta_header.extended_header_size = sizeof(cnmt_extended);
    cnmt_header.meta_header.content_count = 0x2; // program + control
    cnmt_header.meta_header.content_meta_count = 0x1;
    cnmt_header.meta_header.attributes = 0x0;
    cnmt_header.meta_header.storage_id = NcmStorageId_SdCard;
    cnmt_extended.patch_id = tid | 0x800;

    for (u32 i = 0; i < 2; i++) {
        std::memcpy(packaged[i].hash, ncas[i].hash, sizeof(packaged[i].hash));
        std::memcpy(&packaged[i].info.content_id, ncas[i].hash, sizeof(packaged[i].info.content_id));
        packaged[i].info.content_type = ncas[i].type;
        ncmU64ToContentInfoSize(ncas[i].data.size(), &packaged[i].info);
    }

    BufHelper cnmt_buf;
    cnmt_buf.write(&cnmt_header, sizeof(cnmt_header));
    cnmt_buf.write(&cnmt_extended, sizeof(cnmt_extended));
    cnmt_buf.write(&packaged, sizeof(packaged));
    cnmt_buf.write(digest, sizeof(digest));

    FileEntries cnmt;
    char cnmt_name[34];
    std::snprintf(cnmt_name, sizeof(cnmt_name), "Application_%016lX.cnmt", tid);
    add_file_entry(cnmt, cnmt_name, cnmt_buf.buf.data(), cnmt_buf.buf.size());

    BufHelper buf;
    nca::Header nca_header{};
    buf.write(&nca_header, sizeof(nca_header));
    write_nca_pfs0(nca_header, 0, cnmt, PFS0_META_HASH_BLOCK_SIZE, buf);
    write_nca_header_encrypted(nca_header, tid, k, nca::ContentType_Meta, buf);
    NcaEntry entry{buf, NcmContentType_Meta};

    out.key.id = tid;
    out.key.version = 0;
    out.key.type = NcmContentMetaType_Application;
    out.key.install_type = NcmContentInstallType_Full;
    out.record.key = out.key;
    out.record.storage_id = NcmStorageId_SdCard;

    out.data.header = cnmt_header.meta_header;
    out.data.header.content_count++; // with the meta NCA
    out.data.header.storage_id = 0;
    out.data.extended = cnmt_extended;
    std::memcpy(&out.data.infos[0].content_id, entry.hash, sizeof(out.data.infos[0].content_id));
    out.data.infos[0].content_type = entry.type;
    out.data.infos[0].attr = 0;
    ncmU64ToContentInfoSize(entry.data.size(), &out.data.infos[0]);
    out.data.infos[0].id_offset = 0;
    out.data.infos[1] = packaged[0].info;
    out.data.infos[2] = packaged[1].info;
    return entry;
}

bool FileExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

// The .nro NXDock runs from (hbloader gives its path as argv[0]), else its usual place.
std::string NroPath() {
    if (__system_argc > 0 && __system_argv && __system_argv[0]) {
        std::string p = __system_argv[0];
        if (p.starts_with("sdmc:/") && p.ends_with(".nro") && p.size() < 0x200 && FileExists(p)) {
            return p;
        }
    }
    return kDefaultNro;
}

// The same title id as Sphaira gives a forwarder of this .nro, so one made there is updated too.
u64 TitleIdOf(const std::string& nro) {
    u64 hash_data[SHA256_HASH_SIZE / sizeof(u64)];
    const std::string hash_path = nro + nro; // the path, then the arguments (the path again)
    sha256CalculateHash(hash_data, hash_path.data(), hash_path.length());
    return 0x0500000000000000 | (hash_data[0] & 0x00FFFFFFFFFFF000);
}

// The icon (a JPEG) and the NACP of the .nro, from its assets.
bool ReadNroAssets(const std::string& path, std::vector<u8>& icon, NacpStruct& nacp) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        return false;
    }
    ON_SCOPE_EXIT(std::fclose(f));
    NroStart start{};
    NroHeader header{};
    if (std::fread(&start, sizeof(start), 1, f) != 1 || std::fread(&header, sizeof(header), 1, f) != 1 || header.magic != NROHEADER_MAGIC) {
        return false;
    }
    NroAssetHeader assets{};
    if (std::fseek(f, long(header.size), SEEK_SET) != 0 || std::fread(&assets, sizeof(assets), 1, f) != 1 || assets.magic != NROASSETHEADER_MAGIC) {
        return false;
    }
    if (assets.icon.size == 0 || assets.icon.size > 0x20000 || assets.nacp.size != sizeof(NacpStruct)) {
        return false;
    }
    icon.resize(assets.icon.size);
    if (std::fseek(f, long(header.size + assets.icon.offset), SEEK_SET) != 0 || std::fread(icon.data(), icon.size(), 1, f) != 1) {
        return false;
    }
    return std::fseek(f, long(header.size + assets.nacp.offset), SEEK_SET) == 0 && std::fread(&nacp, sizeof(nacp), 1, f) == 1;
}

bool RunningFrom(u64 tid) {
    u64 program_id = 0;
    return R_SUCCEEDED(svcGetInfo(&program_id, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0)) && program_id == tid;
}

} // namespace

Status Query() {
    Status s;
    s.nro = NroPath();
    s.title_id = TitleIdOf(s.nro);
    s.nro_found = FileExists(s.nro);
    s.running_from = RunningFrom(s.title_id);
    if (R_SUCCEEDED(ns::Initialize())) {
        s32 count = 0;
        s.installed = R_SUCCEEDED(nsCountApplicationContentMeta(s.title_id, &count)) && count > 0;
        ns::Exit();
    }
    s.installed = s.installed || s.running_from;
    return s;
}

Result Install() {
    const std::string nro = NroPath();
    const u64 tid = TitleIdOf(nro);
    R_UNLESS(!RunningFrom(tid), kResultRunning);

    std::vector<u8> icon;
    NacpStruct nacp{};
    R_UNLESS(ReadNroAssets(nro, icon, nacp), kResultNoNro);

    R_TRY(ncmInitialize());
    ON_SCOPE_EXIT(ncmExit());
    R_TRY(ns::Initialize());
    ON_SCOPE_EXIT(ns::Exit());

    keys::Keys k;
    R_TRY(keys::parse_keys(k, false)); // only the header key, made by the console

    std::vector<NcaEntry> ncas;
    {
        FileEntries exefs;
        add_file_entry(exefs, "main", HBL_MAIN_DATA, sizeof(HBL_MAIN_DATA));
        add_file_entry(exefs, "main.npdm", HBL_NPDM_DATA, sizeof(HBL_NPDM_DATA));
        R_UNLESS(patch_npdm(exefs[1].data, tid), kResultNpdm);
        FileEntries romfs;
        add_file_entry(romfs, "/nextArgv", nro.data(), nro.length());
        add_file_entry(romfs, "/nextNroPath", nro.data(), nro.length());
        ncas.emplace_back(create_program_nca(tid, k, exefs, romfs));
    }
    {
        patch_nacp(nacp, tid);
        FileEntries romfs;
        add_file_entry(romfs, "/control.nacp", &nacp, sizeof(nacp));
        add_file_entry(romfs, "/icon_AmericanEnglish.dat", icon.data(), icon.size());
        ncas.emplace_back(create_control_nca(tid, k, romfs));
    }
    MetaOut meta;
    ncas.emplace_back(create_meta_nca(tid, k, ncas, meta));

    // a shortcut made before goes first, so nothing of it is left behind
    nsDeleteApplicationEntity(tid);

    {
        NcmContentStorage cs;
        R_TRY(ncmOpenContentStorage(&cs, NcmStorageId_SdCard));
        ON_SCOPE_EXIT(ncmContentStorageClose(&cs));
        for (const auto& n : ncas) {
            NcmContentId content_id;
            NcmPlaceHolderId placeholder_id;
            std::memcpy(&content_id, n.hash, sizeof(content_id));
            R_TRY(ncmContentStorageGeneratePlaceHolderId(&cs, &placeholder_id));
            ncmContentStorageDeletePlaceHolder(&cs, &placeholder_id);
            R_TRY(ncmContentStorageCreatePlaceHolder(&cs, &content_id, &placeholder_id, n.data.size()));
            R_TRY(ncmContentStorageWritePlaceHolder(&cs, &placeholder_id, 0, n.data.data(), n.data.size()));
            ncmContentStorageDelete(&cs, &content_id);
            R_TRY(ncmContentStorageRegister(&cs, &content_id, &placeholder_id));
        }
    }
    {
        NcmContentMetaDatabase db;
        R_TRY(ncmOpenContentMetaDatabase(&db, NcmStorageId_SdCard));
        ON_SCOPE_EXIT(ncmContentMetaDatabaseClose(&db));
        R_TRY(ncmContentMetaDatabaseSet(&db, &meta.key, &meta.data, sizeof(meta.data)));
        R_TRY(ncmContentMetaDatabaseCommit(&db));
    }
    R_TRY(ns::PushApplicationRecord(tid, &meta.record, 1));
    ns::InvalidateApplicationControlCache(tid);
    log_write("[NXDock] HOME shortcut %016lX installed for %s\n", tid, nro.c_str());
    R_SUCCEED();
}

Result Remove() {
    const u64 tid = TitleIdOf(NroPath());
    R_UNLESS(!RunningFrom(tid), kResultRunning);
    R_TRY(ns::Initialize());
    ON_SCOPE_EXIT(ns::Exit());
    const Result rc = nsDeleteApplicationCompletely(tid);
    log_write("[NXDock] HOME shortcut %016lX removed: 0x%X\n", tid, rc);
    return rc;
}

} // namespace nxdock::forwarder
