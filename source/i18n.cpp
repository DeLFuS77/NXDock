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
    "1. Choose your library", "Switch Library Manager Web apps found on your network.", "Looking on your network…",
    "Type the address", "For an app outside home or behind a proxy", "Chosen",
    "2. Accept it in the app",
    "Open the app on your phone or computer: a notice with this code shows on any page. Check that it matches and press Accept.",
    "Waiting for Accept · the code runs out in %d:%02d", "Remember this library", "Search again", "New code",
    "Connecting to your library…", "Not linked", "Forget this library",
    "Install over the network is off in the app: an administrator has to turn it on.", "Choose",
    "No library found. Check that the app is open and on the same network, or type its address.",
    "That address is not a Switch Library Manager Web app.",
    "Your library does not answer. Check that the app is open and on the same network.",
    "Too many consoles are waiting in the app. Try again in a few minutes.",
    "Nobody accepted the code in time.",
    "The console was refused in the app.",
    "Connected to %s",
    "Address by hand", "Key by hand",
    "Loading your library…", "Nothing here with this filter.", "Try again",
    "The queue is empty", "Choose a game and press A to see what to install.", "Filter",
    "Version", "Format", "Will take", "Firmware", "On your console", "Not on your console", "or newer",
    "Base game", "Update", "DLC: ", "Install to", "SD card", "Internal memory", "Install %s",
    "Installing comes in the next step of NXDock.", "Free on the SD card: %s · %s left after", "new", "installed", "Select", "Search your library",
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
    "1. Elige tu biblioteca", "Apps de Switch Library Manager Web encontradas en tu red.", "Buscando en la red…",
    "Escribir la dirección", "Para una app fuera de casa o detrás de un proxy", "Elegida",
    "2. Acéptala en la app",
    "Abre la app en el móvil o el ordenador: sale un aviso con este código en cualquier página. Comprueba que coincide y pulsa Aceptar.",
    "Esperando a que aceptes · el código caduca en %d:%02d", "Recordar esta biblioteca", "Buscar otra vez", "Código nuevo",
    "Conectando con tu biblioteca…", "Sin vincular", "Olvidar esta biblioteca",
    "«Instalar por red» está apagado en la app: un administrador tiene que encenderlo.", "Elegir",
    "No se ha encontrado ninguna biblioteca. Comprueba que la app está abierta y en la misma red, o escribe su dirección.",
    "Esa dirección no es una app de Switch Library Manager Web.",
    "Tu biblioteca no responde. Comprueba que la app está abierta y en la misma red.",
    "Hay demasiadas consolas esperando en la app. Prueba otra vez dentro de unos minutos.",
    "Nadie ha aceptado el código a tiempo.",
    "La consola se ha rechazado en la app.",
    "Conectada a %s",
    "Dirección a mano", "Llave a mano",
    "Cargando tu biblioteca…", "No hay nada con este filtro.", "Probar otra vez",
    "La cola está vacía", "Elige un juego y pulsa A para ver qué instalar.", "Filtro",
    "Versión", "Formato", "Ocupará", "Firmware", "En tu consola", "No está en tu consola", "o más",
    "Juego base", "Actualización", "DLC: ", "Instalar en", "Tarjeta SD", "Memoria interna", "Instalar %s",
    "La instalación llega en el siguiente paso de NXDock.", "Libre en la SD: %s · quedarán %s", "nueva", "instalado", "Marcar", "Buscar en tu biblioteca",
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
