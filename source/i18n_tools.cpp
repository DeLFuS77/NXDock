// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeLFuS77
#include "i18n_tools.hpp"

#include "i18n.hpp"

#include <cstddef>

namespace nxdock {
namespace {

constexpr Tools kEnglish{
    .fv_games = "Games",
    .fv_all = "All files",
    .fm_actions = "Actions",
    .fm_all = "All",
    .fm_title_fmt = "%d marked · %s",
    .fm_install_inside = "Install what is inside",
    .fm_games_fmt = "%d games",
    .fm_copy = "Copy",
    .fm_cut = "Cut (move)",
    .fm_paste = "Paste here",
    .fm_rename = "Rename",
    .fm_new_folder = "New folder",
    .fm_props = "Properties",
    .fm_delete = "Delete…",
    .fm_forget = "Forget what was copied",
    .fm_show_all = "Show all files",
    .fm_show_games = "Show the games only",
    .fw_none = "nothing chosen",
    .fw_clip = "nothing copied",
    .fw_one = "only one",
    .fw_system = "of the system",
    .fw_here = "not here",
    .fw_no_games = "no games",
    .fc_copy_fmt = "%d to copy · %s · from %s",
    .fc_move_fmt = "%d to move · %s · from %s",
    .fd_title_one_fmt = "Delete %s?",
    .fd_title_many_fmt = "Delete %d items?",
    .fd_body_fmt = "They are deleted for good, with all they hold, and %s are freed. It cannot be undone.",
    .fd_go_fmt = "Delete %s",
    .fd_careful = "Inside atmosphere: Atmosphère reads these files. Delete only what you know.",
    .fd_files_fmt = "%llu files · %s",
    .fd_locked_fmt = "%s is of the system: NXDock does not delete, rename or move it.",
    .fd_self_fmt = "%s is NXDock's own: it is not deleted, renamed or moved.",
    .fx_deleting = "Deleting",
    .fx_copying_fmt = "Copying · %s",
    .fx_moving_fmt = "Moving · %s",
    .fx_progress_fmt = "%s of %s · %llu of %llu files",
    .fx_speed_fmt = "%s/s · %s left",
    .fx_fat32 = "The disk is FAT32: files of 4 GB or more are split in a folder, as the console reads them.",
    .fx_stop = "Stop",
    .fx_awake = "The console stays awake while it works",
    .fx_done_delete_fmt = "Deleted · %s freed",
    .fx_done_copy_fmt = "Copied %llu files (%s)",
    .fx_done_move_fmt = "Moved %llu files (%s)",
    .fx_failed_fmt = "%s could not be read or written. What was done stays done.",
    .fx_stopped = "Stopped: what was done stays done.",
    .fx_into_itself = "A folder cannot go into itself.",
    .fx_no_room_fmt = "It needs %s and there are %s free.",
    .fn_rename_guide = "A new name",
    .fn_folder_guide = "The name of the new folder",
    .fn_bad = "That name cannot be used: it is taken, or it has / \\ : * ? \" < > |",
    .fn_renamed = "Renamed",
    .fn_made = "Folder made",
    .fp_kind = "Kind",
    .fp_folder = "Folder",
    .fp_file = "File",
    .fp_size = "Size",
    .fp_holds = "Holds",
    .fp_holds_fmt = "%llu files in %llu folders",
    .fp_where = "Where",
    .fp_changed = "Changed",
    .fp_game_fmt = "Game file · %s",
    .fa_switch = "Delete the files when they install well",
    .fa_sub = "if it fails, they stay",
    .fa_tag = "deleted at the end",
    .fa_done_fmt = "Its file was deleted: %s freed",
};

constexpr Tools kSpanish{
    .fv_games = "Juegos",
    .fv_all = "Todos los archivos",
    .fm_actions = "Acciones",
    .fm_all = "Todo",
    .fm_title_fmt = "%d marcados · %s",
    .fm_install_inside = "Instalar lo que hay dentro",
    .fm_games_fmt = "%d juegos",
    .fm_copy = "Copiar",
    .fm_cut = "Cortar (mover)",
    .fm_paste = "Pegar aquí",
    .fm_rename = "Cambiar el nombre",
    .fm_new_folder = "Carpeta nueva",
    .fm_props = "Propiedades",
    .fm_delete = "Borrar…",
    .fm_forget = "Olvidar lo copiado",
    .fm_show_all = "Ver todos los archivos",
    .fm_show_games = "Ver solo los juegos",
    .fw_none = "nada elegido",
    .fw_clip = "nada copiado",
    .fw_one = "solo uno",
    .fw_system = "del sistema",
    .fw_here = "aquí no",
    .fw_no_games = "sin juegos",
    .fc_copy_fmt = "%d para copiar · %s · desde %s",
    .fc_move_fmt = "%d para mover · %s · desde %s",
    .fd_title_one_fmt = "¿Borrar %s?",
    .fd_title_many_fmt = "¿Borrar %d elementos?",
    .fd_body_fmt = "Se borran del todo, con lo que llevan dentro, y quedan libres %s. No se puede deshacer.",
    .fd_go_fmt = "Borrar %s",
    .fd_careful = "Dentro de atmosphere: Atmosphère lee estos archivos. Borra solo lo que conozcas.",
    .fd_files_fmt = "%llu archivos · %s",
    .fd_locked_fmt = "«%s» es del sistema: NXDock no lo borra, ni lo mueve, ni le cambia el nombre.",
    .fd_self_fmt = "«%s» es del propio NXDock: no se borra, ni se mueve, ni se le cambia el nombre.",
    .fx_deleting = "Borrando",
    .fx_copying_fmt = "Copiando · %s",
    .fx_moving_fmt = "Moviendo · %s",
    .fx_progress_fmt = "%s de %s · %llu de %llu archivos",
    .fx_speed_fmt = "%s/s · faltan %s",
    .fx_fat32 = "El disco es FAT32: los archivos de 4 GB o más se parten en una carpeta, como los lee la consola.",
    .fx_stop = "Parar",
    .fx_awake = "La consola no se duerme mientras trabaja",
    .fx_done_delete_fmt = "Borrado · %s libres",
    .fx_done_copy_fmt = "Copiados %llu archivos (%s)",
    .fx_done_move_fmt = "Movidos %llu archivos (%s)",
    .fx_failed_fmt = "No se pudo leer o escribir %s. Lo que ya estaba hecho se queda hecho.",
    .fx_stopped = "Parado: lo que ya estaba hecho se queda hecho.",
    .fx_into_itself = "Una carpeta no puede ir dentro de sí misma.",
    .fx_no_room_fmt = "Hacen falta %s y quedan %s libres.",
    .fn_rename_guide = "Un nombre nuevo",
    .fn_folder_guide = "El nombre de la carpeta nueva",
    .fn_bad = "Ese nombre no vale: ya existe, o lleva / \\ : * ? \" < > |",
    .fn_renamed = "Nombre cambiado",
    .fn_made = "Carpeta creada",
    .fp_kind = "Tipo",
    .fp_folder = "Carpeta",
    .fp_file = "Archivo",
    .fp_size = "Tamaño",
    .fp_holds = "Contiene",
    .fp_holds_fmt = "%llu archivos en %llu carpetas",
    .fp_where = "Dónde",
    .fp_changed = "Cambiado",
    .fp_game_fmt = "Archivo de juego · %s",
    .fa_switch = "Borrar los archivos cuando se instalen bien",
    .fa_sub = "si falla, se quedan",
    .fa_tag = "se borra al acabar",
    .fa_done_fmt = "Su archivo se ha borrado: %s libres",
};

// The other languages take the English texts until they have their own.
constexpr Tools kFrench{};
constexpr Tools kGerman{};
constexpr Tools kItalian{};
constexpr Tools kPortuguese{};
constexpr Tools kDutch{};
constexpr Tools kRussian{};
constexpr Tools kJapanese{};
constexpr Tools kKorean{};
constexpr Tools kChinese{};

// In the order of LanguageName in i18n.cpp.
const Tools* const kAll[] = {&kEnglish, &kSpanish, &kFrench, &kGerman, &kItalian, &kPortuguese,
                             &kDutch, &kRussian, &kJapanese, &kKorean, &kChinese};
constexpr int kCount = int(sizeof(kAll) / sizeof(kAll[0]));

constexpr size_t kFields = sizeof(Tools) / sizeof(const char*);
static_assert(sizeof(Tools) == kFields * sizeof(const char*));

Tools g_filled[kCount];
bool g_ready = false;

void Fill() {
    for (int l = 0; l < kCount; l++) {
        const auto* src = reinterpret_cast<const char* const*>(kAll[l]);
        const auto* en = reinterpret_cast<const char* const*>(&kEnglish);
        auto* dst = reinterpret_cast<const char**>(&g_filled[l]);
        for (size_t i = 0; i < kFields; i++) {
            dst[i] = src[i] ? src[i] : en[i] ? en[i] : "";
        }
    }
    g_ready = true;
}

} // namespace

const Tools& TrT() {
    if (!g_ready) {
        Fill();
    }
    const int l = CurrentLanguage();
    return g_filled[l >= 0 && l < kCount ? l : 0];
}

} // namespace nxdock
