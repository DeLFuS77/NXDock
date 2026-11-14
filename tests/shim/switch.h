// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// The few libnx names the network source and the texts use, so they can be tested on the computer.
#pragma once

#include <chrono>
#include <cstdint>
#include <thread>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int32_t s32;
typedef int64_t s64;
typedef u32 Result;

#define R_SUCCEEDED(res) ((res) == 0)
#define R_FAILED(res) ((res) != 0)
#define R_MODULE(res) ((res) & 0x1FF)
#define R_DESCRIPTION(res) (((res) >> 9) & 0x1FFF)
#define MAKERESULT(module, description) ((((module) & 0x1FF)) | ((description) & 0x1FFF) << 9)

enum { Module_Libnx = 345 };
enum { LibnxError_NotFound = 1 };

inline void svcSleepThread(s64 ns) {
    std::this_thread::sleep_for(std::chrono::nanoseconds(ns));
}

struct UEvent {
    int signalled;
};
inline void ueventCreate(UEvent* e, bool) { e->signalled = 0; }
inline void ueventSignal(UEvent* e) { e->signalled = 1; }

struct NcmContentMetaKey {
    u64 id;
    u32 version;
    u8 type;
    u8 install_type;
    u8 padding[2];
};

// The language of the console, which a computer does not have: English.
enum SetLanguage {
    SetLanguage_JA, SetLanguage_ENUS, SetLanguage_FR, SetLanguage_DE, SetLanguage_IT, SetLanguage_ES, SetLanguage_ZHCN,
    SetLanguage_KO, SetLanguage_NL, SetLanguage_PT, SetLanguage_RU, SetLanguage_ZHTW, SetLanguage_ENGB, SetLanguage_FRCA,
    SetLanguage_ES419, SetLanguage_ZHHANS, SetLanguage_ZHHANT, SetLanguage_PTBR,
};
inline Result setInitialize() { return 1; }
inline Result setGetSystemLanguage(u64*) { return 1; }
inline Result setMakeLanguage(u64, SetLanguage*) { return 1; }
inline void setExit() {}
