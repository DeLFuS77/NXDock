// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
//
// Clear errors and the check before installing, on the computer: every result gives a title,
// what happened, what to do and its code, in every language; a packed error comes back whole;
// the check warns only when the firmware, the keys or Atmosphère do not fit.
#include "errors.hpp"
#include "i18n_more.hpp"
#include "precheck.hpp"
#include "defines.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace nxdock {
int g_lang = 0;
int CurrentLanguage() { return g_lang; }
} // namespace nxdock

extern "C" void log_write(const char*, ...) {}

using namespace nxdock;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

constexpr unsigned Fw(unsigned a, unsigned b, unsigned c) { return a << 26 | b << 20 | c << 16; }

int main() {
    // every NXDock result, every language: nothing empty, the code always there
    for (g_lang = 0; g_lang < 11; g_lang++) {
        for (u32 d = 1; d <= u32(NXDockResult::UpdateInGame); d++) {
            const auto e = ExplainError(MAKERESULT(Module_NXDock, d), {});
            CHECK(!e.title.empty() && !e.what.empty() && !e.todo.empty());
            CHECK(e.code.size() == 9);
        }
        const auto other = ExplainError(MAKERESULT(123, 45), {});
        CHECK(other.what.find("2123-0045") != std::string::npos);
        // an update that would stop its game says so, with the error the console would show
        CHECK(ExplainError(Result_UpdateStopsGame, {}).title != other.title);
        CHECK(ExplainError(Result_UpdateStopsGame, {}).what.find("2002-4021") != std::string::npos);
        CHECK(ExplainError(Result_UpdateInGame, {}).title != other.title);
    }
    g_lang = 0;

    // the figures go in
    ErrorContext ctx;
    ctx.needed = 6ull << 30;
    ctx.free_bytes = 2ll << 30;
    auto space = ExplainError(Result_NotEnoughSpace, ctx);
    CHECK(space.what.find("6.0 GB") != std::string::npos && space.what.find("2.0 GB") != std::string::npos);
    CHECK(space.what.find("Nothing was installed.") != std::string::npos);
    ctx.nand = true;
    CHECK(ExplainError(Result_NotEnoughSpace, ctx).title.find("internal") != std::string::npos);
    ctx.firmware = Fw(20, 0, 0);
    ctx.console_firmware = Fw(19, 0, 1);
    const auto fw = ExplainError(Result_FirmwareTooOld, ctx);
    CHECK(fw.what.find("20.0.0") != std::string::npos && fw.what.find("19.0.1") != std::string::npos);
    // a cancelled one and a removal do not say "Nothing was installed."
    CHECK(ExplainError(Result_TransferCancelled, {}).what.find("Nothing") == std::string::npos);
    ErrorContext removal;
    removal.install = false;
    CHECK(ExplainError(Result_GameRunning, removal).what.find("Nothing") == std::string::npos);
    // no room in the file system itself
    CHECK(ExplainError(MAKERESULT(2, 30), {}).title.find("SD card") != std::string::npos);

    // packed and back
    const std::string packed = PackError(fw);
    const auto back = UnpackError(packed);
    CHECK(back.title == fw.title && back.what == fw.what && back.todo == fw.todo && back.code == fw.code);
    CHECK(packed.find('\t') == std::string::npos && packed.find('\n') == std::string::npos);
    // an error of an older history line is only "what happened"
    const auto old = UnpackError("Cancelled. Nothing was installed.");
    CHECK(old.title.empty() && old.what == "Cancelled. Nothing was installed.");

    // keys by firmware
    CHECK(KeyGenerationOf(Fw(1, 0, 0)) == 0);
    CHECK(KeyGenerationOf(Fw(8, 0, 1)) == 7);
    CHECK(KeyGenerationOf(Fw(8, 1, 0)) == 8);
    CHECK(KeyGenerationOf(Fw(19, 0, 1)) == 18);
    CHECK(KeyGenerationOf(Fw(20, 1, 5)) == 19);

    // the check before installing
    SystemInfo info;
    info.firmware = Fw(19, 0, 1);
    info.atmosphere = true;
    info.ams_major = 1, info.ams_minor = 8, info.ams_micro = 0;
    info.ams_target = Fw(19, 0, 1);
    info.key_generation = 18;
    auto fine = CheckBeforeInstall(info, Fw(18, 0, 0));
    CHECK(!fine.Warns());
    CHECK(fine.firmware.state == PreCheck::State::Fine && fine.atmosphere.state == PreCheck::State::Fine);
    auto none = CheckBeforeInstall(info, 0);
    CHECK(!none.Warns());
    auto newer = CheckBeforeInstall(info, Fw(20, 0, 0));
    CHECK(newer.Warns());
    CHECK(newer.firmware.state == PreCheck::State::Warn && newer.keys.state == PreCheck::State::Warn);
    CHECK(newer.atmosphere.state == PreCheck::State::Warn);
    CHECK(newer.firmware.right.find("19.0.1") != std::string::npos && newer.firmware.right.find("20.0.0") != std::string::npos);
    CHECK(newer.atmosphere.right.find("1.8.0") != std::string::npos);
    // Atmosphère newer than needed: only the firmware and the keys warn
    info.ams_target = Fw(20, 1, 0);
    auto ams_ok = CheckBeforeInstall(info, Fw(20, 0, 0));
    CHECK(ams_ok.atmosphere.state == PreCheck::State::Fine && ams_ok.firmware.state == PreCheck::State::Warn);
    // another firmware, no Atmosphère: unknown, never a warning by itself
    SystemInfo plain;
    plain.firmware = Fw(19, 0, 1);
    auto other_cfw = CheckBeforeInstall(plain, Fw(17, 0, 0));
    CHECK(!other_cfw.Warns() && other_cfw.atmosphere.state == PreCheck::State::Unknown);

    if (failures) {
        std::printf("errors_test: %d failures\n", failures);
        return 1;
    }
    std::printf("errors_test: ok\n");
    return 0;
}
