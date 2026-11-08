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

#include "app.hpp"

int main(int, char**) {
    nxdock::App app;
    if (!app.Init()) {
        app.Exit();
        return 1;
    }
    while (app.Frame()) {
    }
    app.Exit();
    return 0;
}
