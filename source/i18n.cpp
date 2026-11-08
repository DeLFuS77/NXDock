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
    "Library address", "Console key", "Saved", "Not set", "Speed test", "Start", "Cancel",
    "1 connection", "2 connections", "4 connections", "SD card write", "Not measured yet",
    "Opened from the Album: small network buffers. Open NXDock from a game holding R for full speed.",
    "Full memory mode: large network buffers.",
    "Write the library address and the console key first.",
    "Measures the network and the SD card apart. Nothing is installed; a temporary file on the SD is removed.",
    "best",
    "The library app did not take the key.",
    "Install over the network is off in the library app.",
    "The library app answers only on the local network.",
    "Could not reach the library app",
    "Could not write to the SD card.",
    "Edit",
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
    "Dirección de la biblioteca", "Llave de la consola", "Guardada", "Sin poner", "Prueba de velocidad", "Empezar", "Cancelar",
    "1 conexión", "2 conexiones", "4 conexiones", "Escritura en la SD", "Sin medir",
    "Abierto desde el Álbum: búferes de red pequeños. Ábrelo desde un juego manteniendo R para ir a toda velocidad.",
    "Modo de memoria completa: búferes de red grandes.",
    "Escribe antes la dirección de la biblioteca y la llave de la consola.",
    "Mide la red y la SD por separado. No instala nada; el archivo temporal de la SD se borra.",
    "mejor",
    "La app de la biblioteca no ha aceptado la llave.",
    "«Instalar por red» está apagado en la app de la biblioteca.",
    "La app de la biblioteca solo responde en la red local.",
    "No se ha podido conectar con la app de la biblioteca",
    "No se ha podido escribir en la SD.",
    "Editar",
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
