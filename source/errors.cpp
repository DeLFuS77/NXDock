// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "errors.hpp"

#include "i18n_more.hpp"
#include "defines.hpp"

#include <cstdio>

namespace nxdock {
namespace {

constexpr char kSep = '\x1f';
constexpr u32 kModuleFs = 2, kModuleNcm = 5;

std::string Size(unsigned long long bytes) {
    char t[32];
    if (bytes >= 1024ULL * 1024 * 1024) {
        std::snprintf(t, sizeof(t), "%.1f GB", double(bytes) / (1024.0 * 1024 * 1024));
    } else {
        std::snprintf(t, sizeof(t), "%.0f MB", double(bytes) / (1024.0 * 1024));
    }
    return t;
}

std::string Format(const char* fmt, const std::string& a, const std::string& b = {}) {
    char t[512];
    std::snprintf(t, sizeof(t), fmt, a.c_str(), b.c_str());
    return t;
}

} // namespace

std::string ErrorCode(unsigned rc) {
    char t[24];
    std::snprintf(t, sizeof(t), "%04u-%04u", 2000 + R_MODULE(rc), R_DESCRIPTION(rc));
    return t;
}

std::string FirmwareString(unsigned version) {
    char t[24];
    std::snprintf(t, sizeof(t), "%u.%u.%u", version >> 26, (version >> 20) & 0x3F, (version >> 16) & 0xF);
    return t;
}

ErrorText ExplainError(unsigned rc, const ErrorContext& ctx) {
    const More& m = TrM();
    ErrorText e;
    e.code = ErrorCode(rc);
    const auto set = [&](const char* t, const std::string& w, const char* d) {
        e.title = t;
        e.what = w;
        e.todo = d;
    };
    const std::string free_text = ctx.free_bytes >= 0 ? Size((unsigned long long)ctx.free_bytes) : "?";
    const u32 module = R_MODULE(rc), desc = R_DESCRIPTION(rc);
    bool known = true;
    if (module == Module_NXDock) {
        switch (NXDockResult(desc)) {
            case NXDockResult::TransferCancelled: set(m.e_cancelled_t, m.e_cancelled_w, m.e_cancelled_d); break;
            case NXDockResult::NotEnoughSpace:
                if (ctx.nand) set(m.e_space_nand_t, Format(m.e_space_nand_w, Size(ctx.needed), free_text), m.e_space_nand_d);
                else set(m.e_space_t, Format(m.e_space_w, Size(ctx.needed), free_text), m.e_space_d);
                break;
            case NXDockResult::FirmwareTooOld:
                set(m.e_firmware_t, Format(m.e_firmware_w, FirmwareString(ctx.firmware), ctx.console_firmware ? FirmwareString(ctx.console_firmware) : "?"),
                    m.e_firmware_d);
                break;
            case NXDockResult::NetworkLost:
            case NXDockResult::YatiHttpReadFailed: set(m.e_network_t, m.e_network_w, m.e_network_d); break;
            case NXDockResult::ServerRefused: set(m.e_refused_t, m.e_refused_w, m.e_refused_d); break;
            case NXDockResult::ServerChanged: set(m.e_changed_t, m.e_changed_w, m.e_changed_d); break;
            case NXDockResult::YatiInvalidNcaSha256:
            case NXDockResult::NcaFailedNcaHeaderHashVerify:
            case NXDockResult::YatiInvalidNcaReadSize: set(m.e_sha_t, m.e_sha_w, m.e_sha_d); break;
            case NXDockResult::YatiNczSectionNotFound:
            case NXDockResult::YatiInvalidNczSectionCount:
            case NXDockResult::YatiNczBlockNotFound:
            case NXDockResult::YatiInvalidNczBlockVersion:
            case NXDockResult::YatiInvalidNczBlockType:
            case NXDockResult::YatiInvalidNczBlockTotal:
            case NXDockResult::YatiInvalidNczBlockSizeExponent:
            case NXDockResult::YatiInvalidNczZstdError:
            case NXDockResult::NczBlockTooLarge:
            case NXDockResult::NczBadTable:
            case NXDockResult::NczLz4Rebuild: set(m.e_ncz_t, m.e_ncz_w, m.e_ncz_d); break;
            case NXDockResult::YatiTicketNotFound:
            case NXDockResult::YatiCertNotFound:
            case NXDockResult::YatiInvalidTicketBadRightsId:
            case NXDockResult::EsInvalidTicketBadRightsId:
            case NXDockResult::EsBadTicketSize:
            case NXDockResult::EsInvalidTicketFromatVersion:
            case NXDockResult::EsInvalidTicketKeyType:
            case NXDockResult::EsBadTitleKeyType: set(m.e_ticket_t, m.e_ticket_w, m.e_ticket_d); break;
            case NXDockResult::EsPersonalisedTicketDeviceIdMissmatch:
            case NXDockResult::EsFailedDecryptPersonalisedTicket:
            case NXDockResult::EsBadDecryptedPersonalisedTicketSize:
            case NXDockResult::KeyFailedDecyptETicketDeviceKey: set(m.e_personal_t, m.e_personal_w, m.e_personal_d); break;
            case NXDockResult::KeyMissingNcaKeyArea:
            case NXDockResult::KeyMissingTitleKek:
            case NXDockResult::KeyMissingMasterKey:
            case NXDockResult::EsInvalidTicketKeyRevision:
            case NXDockResult::NcaBadSigKeyGen:
            case NXDockResult::YatiInvalidNcaSigKeyGen: set(m.e_keys_t, m.e_keys_w, m.e_keys_d); break;
            case NXDockResult::OutOfMemory: set(m.e_memory_t, m.e_memory_w, m.e_memory_d); break;
            case NXDockResult::NczExperimental: set(m.e_experimental_t, m.e_experimental_w, m.e_experimental_d); break;
            case NXDockResult::UnknownContainer:
            case NXDockResult::NspBadMagic:
            case NXDockResult::NspInvalidHeader:
            case NXDockResult::XciBadMagic:
            case NXDockResult::XciSecurePartitionNotFound:
            case NXDockResult::YatiContainerNotFound:
            case NXDockResult::YatiNcaNotFound:
            case NXDockResult::NcaBadMagic:
            case NXDockResult::YatiInvalidNcaMagic: set(m.e_container_t, m.e_container_w, m.e_container_d); break;
            case NXDockResult::GameRunning: set(m.e_running_t, m.e_running_w, m.e_running_d); break;
            case NXDockResult::YatiNcmDbCorruptHeader:
            case NXDockResult::YatiNcmDbCorruptInfos: set(m.e_storage_t, m.e_storage_w, m.e_storage_d); break;
            default: known = false; break;
        }
    } else if (module == kModuleFs && desc == 30) {
        // the file system has no room left (UsableSpaceNotEnough)
        if (ctx.nand) set(m.e_space_nand_t, Format(m.e_space_nand_w, Size(ctx.needed), free_text), m.e_space_nand_d);
        else set(m.e_space_t, Format(m.e_space_w, Size(ctx.needed), free_text), m.e_space_d);
    } else if (module == kModuleFs || module == kModuleNcm) {
        set(m.e_storage_t, m.e_storage_w, m.e_storage_d);
    } else {
        known = false;
    }
    if (!known) {
        set(m.e_other_t, Format(m.e_other_w, e.code), m.e_other_d);
    }
    const bool cancelled = module == Module_NXDock && NXDockResult(desc) == NXDockResult::TransferCancelled;
    if (ctx.install && !cancelled) {
        e.what += std::string(" ") + m.nothing_installed;
    }
    return e;
}

std::string PackError(const ErrorText& e) {
    return e.title + kSep + e.what + kSep + e.todo + kSep + e.code;
}

ErrorText UnpackError(const std::string& line) {
    ErrorText e;
    std::string* parts[] = {&e.title, &e.what, &e.todo, &e.code};
    size_t start = 0;
    int i = 0;
    if (line.find(kSep) == std::string::npos) {
        e.what = line;
        return e;
    }
    while (i < 4) {
        const size_t end = line.find(kSep, start);
        *parts[i++] = line.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return e;
}

} // namespace nxdock
