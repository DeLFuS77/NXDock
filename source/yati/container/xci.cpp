#include "yati/container/xci.hpp"
#include "defines.hpp"
#include "log.hpp"

#include <cstring>
#include <limits>

namespace sphaira::yati::container {
namespace {

#define XCI_MAGIC std::byteswap(0x48454144)
#define HFS0_MAGIC 0x30534648
#define HFS0_ROOT_HEADER_OFFSET 0xF000
#define HFS0_ROOT_HEADER_OFFSET_WITH_KEY_AREA (HFS0_ROOT_HEADER_OFFSET + 0x1000)

// NXDock: the same limits and checks as the PFS0 of an NSP (nsp.cpp).
constexpr u32 MAX_HFS0_FILES = 0x10000;
constexpr u32 MAX_HFS0_STRING_TABLE_SIZE = 16 * 1024 * 1024;

bool CheckedAdd(u64 lhs, u64 rhs, u64& out) {
    if (rhs > std::numeric_limits<u64>::max() - lhs) {
        return false;
    }
    out = lhs + rhs;
    return true;
}

Result ReadExact(source::Base* source, void* data, s64 offset, s64 size) {
    if (!size) {
        R_SUCCEED();
    }

    u64 bytes_read{};
    R_TRY(source->Read(data, offset, size, &bytes_read));
    R_UNLESS(bytes_read == static_cast<u64>(size), Result_NspInvalidHeader);
    R_SUCCEED();
}

} // namespace

auto Xci::Hfs0::GetHfs0Data() const -> std::vector<u8> {
    s64 off = 0;
    std::vector<u8> out(GetHfs0Size());

    std::memcpy(out.data() + off, &header, sizeof(header));
    off += sizeof(header);

    std::memcpy(out.data() + off, file_table.data(), file_table.size() * sizeof(Hfs0FileTableEntry));
    off += file_table.size() * sizeof(Hfs0FileTableEntry);

    for (auto& str : string_table) {
        // NXDock: names that share their place in the table never write past it.
        if (u64(off) + str.length() + 1 > out.size()) {
            break;
        }
        std::memcpy(out.data() + off, str.data(), str.length());
        off += str.length() + 1;
    }

    return out;
}

Result Xci::GetRoot(Root& out) {
    // try and get root at normal offset.
    s64 offset = HFS0_ROOT_HEADER_OFFSET;
    auto rc = Hfs0GetPartition(m_source, offset, out.hfs0);
    if (rc == Result_XciBadMagic) {
        // otherwise, try and again as maybe the key area pre-prended.
        offset = HFS0_ROOT_HEADER_OFFSET_WITH_KEY_AREA;
        rc = Hfs0GetPartition(m_source, offset, out.hfs0);
    }

    if (R_SUCCEEDED(rc)) {
        out.hfs0_offset = offset;

        for (u32 i = 0; i < out.hfs0.header.total_files; i++) {
            Partition partition{out.hfs0.string_table[i]};
            R_TRY(ReadPartitionFromHfs0(m_source, out.hfs0, i, partition));
            out.partitions.emplace_back(partition);
        }
    }

    return rc;
}

Result Xci::GetCollections(Collections& out) {
    Root root;
    R_TRY(GetRoot(root));
    log_write("[XCI] got root partition\n");

    for (auto& parition : root.partitions) {
        if (parition.name == "secure") {
            out = parition.collections;
            R_SUCCEED();
        }
    }

    return Result_XciSecurePartitionNotFound;
}

Result Xci::Hfs0GetPartition(source::Base* source, s64 off, Hfs0& out) {
    R_UNLESS(off >= 0, Result_NspInvalidHeader);

    // get header
    R_TRY(ReadExact(source, std::addressof(out.header), off, sizeof(out.header)));
    log_write("checking magic: %X vs %X\n", out.header.magic, HFS0_MAGIC);
    R_UNLESS(out.header.magic == HFS0_MAGIC, Result_XciBadMagic);
    // NXDock: the tables are bounded before the memory for them is taken.
    R_UNLESS(out.header.total_files <= MAX_HFS0_FILES, Result_NspInvalidHeader);
    R_UNLESS(out.header.string_table_size <= MAX_HFS0_STRING_TABLE_SIZE, Result_NspInvalidHeader);

    const auto table_size = static_cast<u64>(out.header.total_files) * sizeof(Hfs0FileTableEntry);
    u64 table_offset{};
    u64 strings_offset{};
    u64 data_offset{};
    R_UNLESS(CheckedAdd(static_cast<u64>(off), sizeof(out.header), table_offset), Result_NspInvalidHeader);
    R_UNLESS(CheckedAdd(table_offset, table_size, strings_offset), Result_NspInvalidHeader);
    R_UNLESS(CheckedAdd(strings_offset, out.header.string_table_size, data_offset), Result_NspInvalidHeader);
    R_UNLESS(data_offset <= static_cast<u64>(std::numeric_limits<s64>::max()), Result_NspInvalidHeader);

    // get file table
    out.file_table.resize(out.header.total_files);
    R_TRY(ReadExact(source, out.file_table.data(), table_offset, table_size));

    // get string table
    std::vector<char> string_table(out.header.string_table_size);
    R_TRY(ReadExact(source, string_table.data(), strings_offset, string_table.size()));

    // NXDock: every name starts inside the table and ends there, and every file's place in
    // the partition does not wrap.
    out.string_table.clear();
    for (const auto& file : out.file_table) {
        R_UNLESS(file.name_offset < string_table.size(), Result_NspInvalidHeader);
        const auto name = string_table.data() + file.name_offset;
        const auto name_size = string_table.size() - file.name_offset;
        R_UNLESS(std::memchr(name, '\0', name_size), Result_NspInvalidHeader);

        u64 file_offset{};
        u64 file_end{};
        R_UNLESS(CheckedAdd(data_offset, file.data_offset, file_offset), Result_NspInvalidHeader);
        R_UNLESS(CheckedAdd(file_offset, file.data_size, file_end), Result_NspInvalidHeader);
        R_UNLESS(file_end <= static_cast<u64>(std::numeric_limits<s64>::max()), Result_NspInvalidHeader);

        out.string_table.emplace_back(name);
    }

    out.data_offset = data_offset;
    R_SUCCEED();
}

Result Xci::ReadPartitionFromHfs0(source::Base* source, const Hfs0& root, u32 index, Partition& out) {
    log_write("[XCI] fetching %s partition\n", root.string_table[index].c_str());
    out.hfs0_offset = root.data_offset + root.file_table[index].data_offset;
    out.hfs0_size = root.file_table[index].data_size;

    R_TRY(Hfs0GetPartition(source, out.hfs0_offset, out.hfs0));
    log_write("[XCI] got %s partition\n", root.string_table[index].c_str());

    for (u32 i = 0; i < out.hfs0.header.total_files; i++) {
        CollectionEntry entry;
        entry.name = out.hfs0.string_table[i];
        entry.offset = out.hfs0.data_offset + out.hfs0.file_table[i].data_offset;
        entry.size = out.hfs0.file_table[i].data_size;
        out.collections.emplace_back(entry);
    }

    log_write("[XCI] read %s partition count: %zu\n", root.string_table[index].c_str(), out.collections.size());
    R_SUCCEED();
}

} // namespace sphaira::yati::container
