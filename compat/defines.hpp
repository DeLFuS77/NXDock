// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// NXDock's stand-in for Sphaira's defines.hpp, with only what the yati core uses: the result
// helpers, a scope guard and the result codes. The macros follow Sphaira's (GPL-3.0).
#pragma once

#include <switch.h>
#include <utility>
#include "log.hpp"

#define R_SUCCEED() return (Result)0
#define R_THROW(_rc) return _rc
// NXDock: a failure is noted in install.log with where it happened, to read after a test.
#define R_TRY(r) { \
    if (const auto _rc = (r); R_FAILED(_rc)) { \
        log_write("[fail] 0x%X at %s:%d\n", (unsigned)_rc, __FILE__, __LINE__); \
        R_THROW(_rc); \
    } \
}
#define R_UNLESS(expr, res) { \
    if (!(expr)) { \
        R_THROW(res); \
    } \
}

#define CONCATENATE_IMPL(s1, s2) s1##s2
#define CONCATENATE(s1, s2) CONCATENATE_IMPL(s1, s2)
#define ANONYMOUS_VARIABLE(pref) CONCATENATE(pref, __COUNTER__)

template<typename Function>
struct ScopeGuard {
    ScopeGuard(Function&& function) : m_function(std::forward<Function>(function)) {}
    ~ScopeGuard() { m_function(); }
    ScopeGuard(const ScopeGuard&) = delete;
    void operator=(const ScopeGuard&) = delete;

private:
    const Function m_function;
};

#define ON_SCOPE_EXIT(_f) ScopeGuard ANONYMOUS_VARIABLE(SCOPE_EXIT_STATE_){[&] { _f; }};

// The file system results the core names.
enum FsError : Result {
    FsError_UnsupportedOperateRangeForFileStorage = 0x314402,
};

// NXDock's own result module; the descriptions are the order below.
constexpr u32 Module_NXDock = 506;

enum class NXDockResult : u32 {
    TransferCancelled = 1,
    StreamBadSeek,
    CoreUnavailable,
    // keys and NCA headers
    KeyMissingNcaKeyArea,
    KeyMissingTitleKek,
    KeyMissingMasterKey,
    KeyFailedDecyptETicketDeviceKey,
    NcaFailedNcaHeaderHashVerify,
    NcaBadSigKeyGen,
    NcaBadMagic,
    // containers
    NspBadMagic,
    NspInvalidHeader,
    XciBadMagic,
    XciSecurePartitionNotFound,
    // tickets
    EsBadTitleKeyType,
    EsPersonalisedTicketDeviceIdMissmatch,
    EsFailedDecryptPersonalisedTicket,
    EsBadDecryptedPersonalisedTicketSize,
    EsBadTicketSize,
    EsInvalidTicketBadRightsId,
    EsInvalidTicketFromatVersion,
    EsInvalidTicketKeyType,
    EsInvalidTicketKeyRevision,
    // the install
    YatiContainerNotFound,
    YatiNcaNotFound,
    YatiInvalidNcaReadSize,
    YatiInvalidNcaSigKeyGen,
    YatiInvalidNcaMagic,
    YatiInvalidNcaSignature0,
    YatiInvalidNcaSignature1,
    YatiInvalidNcaSha256,
    YatiNczSectionNotFound,
    YatiInvalidNczSectionCount,
    YatiNczBlockNotFound,
    YatiInvalidNczBlockVersion,
    YatiInvalidNczBlockType,
    YatiInvalidNczBlockTotal,
    YatiInvalidNczBlockSizeExponent,
    YatiInvalidNczZstdError,
    YatiTicketNotFound,
    YatiInvalidTicketBadRightsId,
    YatiCertNotFound,
    YatiNcmDbCorruptHeader,
    YatiNcmDbCorruptInfos,
    YatiHttpReadFailed,
    // added by NXDock
    NotEnoughSpace,        // the storage has less room than the install needs
    NczExperimental,       // NCZSECT2/3 of NXPress: the app sends them as plain NCA
    FirmwareTooOld,        // the content needs a newer system version
    NczBlockTooLarge,      // a block too large to decompress in memory
    NetworkLost,           // the app stopped answering and retries ran out
    ServerRefused,         // the app answered with an error (key, gone file)
    ServerChanged,         // the file changed on the app while installing
    OutOfMemory,
};

#define MAKE_NXDOCK_RESULT_ENUM(x) Result_##x = MAKERESULT(Module_NXDock, (u32)NXDockResult::x)

enum : Result {
    MAKE_NXDOCK_RESULT_ENUM(TransferCancelled),
    MAKE_NXDOCK_RESULT_ENUM(StreamBadSeek),
    MAKE_NXDOCK_RESULT_ENUM(CoreUnavailable),
    MAKE_NXDOCK_RESULT_ENUM(KeyMissingNcaKeyArea),
    MAKE_NXDOCK_RESULT_ENUM(KeyMissingTitleKek),
    MAKE_NXDOCK_RESULT_ENUM(KeyMissingMasterKey),
    MAKE_NXDOCK_RESULT_ENUM(KeyFailedDecyptETicketDeviceKey),
    MAKE_NXDOCK_RESULT_ENUM(NcaFailedNcaHeaderHashVerify),
    MAKE_NXDOCK_RESULT_ENUM(NcaBadSigKeyGen),
    MAKE_NXDOCK_RESULT_ENUM(NcaBadMagic),
    MAKE_NXDOCK_RESULT_ENUM(NspBadMagic),
    MAKE_NXDOCK_RESULT_ENUM(NspInvalidHeader),
    MAKE_NXDOCK_RESULT_ENUM(XciBadMagic),
    MAKE_NXDOCK_RESULT_ENUM(XciSecurePartitionNotFound),
    MAKE_NXDOCK_RESULT_ENUM(EsBadTitleKeyType),
    MAKE_NXDOCK_RESULT_ENUM(EsPersonalisedTicketDeviceIdMissmatch),
    MAKE_NXDOCK_RESULT_ENUM(EsFailedDecryptPersonalisedTicket),
    MAKE_NXDOCK_RESULT_ENUM(EsBadDecryptedPersonalisedTicketSize),
    MAKE_NXDOCK_RESULT_ENUM(EsBadTicketSize),
    MAKE_NXDOCK_RESULT_ENUM(EsInvalidTicketBadRightsId),
    MAKE_NXDOCK_RESULT_ENUM(EsInvalidTicketFromatVersion),
    MAKE_NXDOCK_RESULT_ENUM(EsInvalidTicketKeyType),
    MAKE_NXDOCK_RESULT_ENUM(EsInvalidTicketKeyRevision),
    MAKE_NXDOCK_RESULT_ENUM(YatiContainerNotFound),
    MAKE_NXDOCK_RESULT_ENUM(YatiNcaNotFound),
    MAKE_NXDOCK_RESULT_ENUM(YatiInvalidNcaReadSize),
    MAKE_NXDOCK_RESULT_ENUM(YatiInvalidNcaSigKeyGen),
    MAKE_NXDOCK_RESULT_ENUM(YatiInvalidNcaMagic),
    MAKE_NXDOCK_RESULT_ENUM(YatiInvalidNcaSignature0),
    MAKE_NXDOCK_RESULT_ENUM(YatiInvalidNcaSignature1),
    MAKE_NXDOCK_RESULT_ENUM(YatiInvalidNcaSha256),
    MAKE_NXDOCK_RESULT_ENUM(YatiNczSectionNotFound),
    MAKE_NXDOCK_RESULT_ENUM(YatiInvalidNczSectionCount),
    MAKE_NXDOCK_RESULT_ENUM(YatiNczBlockNotFound),
    MAKE_NXDOCK_RESULT_ENUM(YatiInvalidNczBlockVersion),
    MAKE_NXDOCK_RESULT_ENUM(YatiInvalidNczBlockType),
    MAKE_NXDOCK_RESULT_ENUM(YatiInvalidNczBlockTotal),
    MAKE_NXDOCK_RESULT_ENUM(YatiInvalidNczBlockSizeExponent),
    MAKE_NXDOCK_RESULT_ENUM(YatiInvalidNczZstdError),
    MAKE_NXDOCK_RESULT_ENUM(YatiTicketNotFound),
    MAKE_NXDOCK_RESULT_ENUM(YatiInvalidTicketBadRightsId),
    MAKE_NXDOCK_RESULT_ENUM(YatiCertNotFound),
    MAKE_NXDOCK_RESULT_ENUM(YatiNcmDbCorruptHeader),
    MAKE_NXDOCK_RESULT_ENUM(YatiNcmDbCorruptInfos),
    MAKE_NXDOCK_RESULT_ENUM(YatiHttpReadFailed),
    MAKE_NXDOCK_RESULT_ENUM(NotEnoughSpace),
    MAKE_NXDOCK_RESULT_ENUM(NczExperimental),
    MAKE_NXDOCK_RESULT_ENUM(FirmwareTooOld),
    MAKE_NXDOCK_RESULT_ENUM(NczBlockTooLarge),
    MAKE_NXDOCK_RESULT_ENUM(NetworkLost),
    MAKE_NXDOCK_RESULT_ENUM(ServerRefused),
    MAKE_NXDOCK_RESULT_ENUM(ServerChanged),
    MAKE_NXDOCK_RESULT_ENUM(OutOfMemory),
};
