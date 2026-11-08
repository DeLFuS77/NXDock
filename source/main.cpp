// NXDock: installs your own games from your Switch Library Manager Web library.
// Copyright (C) 2026 DeLFuS77
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include <switch.h>
#include <cstdio>

// I0: a bootable shell. The real interface (deko3d + nanovg) and the
// install core arrive in I1.
int main(int, char**) {
    consoleInit(nullptr);

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    std::printf("\x1b[2;3HNXDock " NXDOCK_VERSION "\n");
    std::printf("\x1b[4;3HInstall your own games over the network.\n");
    std::printf("\x1b[6;3HThis build only checks that NXDock starts.\n");
    std::printf("\x1b[8;3HPress + to exit.\n");

    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) {
            break;
        }
        consoleUpdate(nullptr);
    }

    consoleExit(nullptr);
    return 0;
}
