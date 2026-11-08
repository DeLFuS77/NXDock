// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "i18n.hpp"

#include <switch.h>

namespace nxdock {
namespace {

constexpr Strings kEnglish{
    "Library", "Queue", "Installed", "Settings",
    "free", "All", "Not installed", "With update", "DLC",
    "Search", "View", "Exit", "Back",
    "Installing", "more in the queue", "left",
    "Network", "Decompress", "threads", "Write to SD", "Check SHA-256", "of",
    "In the queue", "Waiting", "Done", "Checked", "The console stays awake while installing", "Pause", "Remove",
    "Base game + update · to the SD card",
    "Theme", "Dark", "Light", "Change",
    "Coming soon", "Preview with sample data: nothing is installed yet.",
    "Update",
};

constexpr Strings kSpanish{
    "Biblioteca", "Cola", "Instalado", "Ajustes",
    "libres", "Todo", "Sin instalar", "Con actualización", "DLC",
    "Buscar", "Ver", "Salir", "Atrás",
    "Instalando", "más en cola", "quedan",
    "Red", "Descomprimir", "hilos", "Escribir en la SD", "Comprobar SHA-256", "de",
    "En cola", "Esperando", "Hecho", "Comprobado", "La consola no se duerme mientras instala", "Pausar", "Quitar",
    "Juego base + actualización · a la SD",
    "Tema", "Oscuro", "Claro", "Cambiar",
    "Muy pronto", "Vista previa con datos de ejemplo: todavía no se instala nada.",
    "Actualización",
};

const Strings* g_strings = &kEnglish;

} // namespace

void InitStrings() {
    u64 code = 0;
    SetLanguage lang{};
    if (R_SUCCEEDED(setInitialize())) {
        if (R_SUCCEEDED(setGetSystemLanguage(&code)) && R_SUCCEEDED(setMakeLanguage(code, &lang))) {
            if (lang == SetLanguage_ES || lang == SetLanguage_ES419) {
                g_strings = &kSpanish;
            }
        }
        setExit();
    }
}

const Strings& Tr() {
    return *g_strings;
}

} // namespace nxdock
