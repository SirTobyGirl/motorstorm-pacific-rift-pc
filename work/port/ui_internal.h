/* The port's UI, shared between its files: ui.cpp (the start screen, the
 * in-game menu, the settings pages, the HUD), ui_game.cpp (where the game
 * is) and ui_saves.cpp (save backups). Everything here runs on the window's
 * thread, inside an ImGui frame, unless a comment says otherwise.
 * Layout after the Apocalypse port's ui_internal.h. */
#ifndef PORT_UI_INTERNAL_H
#define PORT_UI_INTERNAL_H

#include "settings.h"

#include <string>

namespace pui {

/* ---- ui.cpp ---- */
/* Write user/settings.ini now (every change is saved at once). */
void save();
/* A wrapped line of help in the dimmed colour, under a widget. */
void hint(const char* text);
/* A line in the warning colour (amber) or the OK colour (green). */
void warn(const char* text);
void good(const char* text);
/* Around a widget an environment variable decides (PORT_PIN_*): greys it
 * out and says which variable on hover. */
void pin_begin(unsigned bit);
void pin_end(unsigned bit);
/* The desktop's file manager on a folder (xdg-open / open), in the
 * background. */
void open_folder(const char* path);
/* True while the start screen is up (the game has not booted yet). */
bool on_start_screen();
/* Start the process again: same binary and arguments, settings re-read.
 * `extra_env` ("NAME=value") is added when not null. Does not return when it
 * works. Progress since the last save is lost. */
void restart_now(const char* extra_env = nullptr);
/* The settings as this run started with them (what a restart would change). */
const port_settings& boot_settings();

/* ---- ui_game.cpp ---- */
/* The Game page: where the game, dev_flash and the saves are, whether each
 * looks right, and a folder picker to change them. */
void page_game(port_settings& s);
/* Whether the game folder in use has a loadable EBOOT.elf (a quick check:
 * the file exists and starts with the ELF magic). The start screen's Play
 * needs it. Cached; refreshed when the path changes. */
bool game_ready(const port_settings& s);

/* ---- ui_saves.cpp ---- */
/* The Saves page: the saves on the PS3 hard drive, backups, restore. */
void page_saves(port_settings& s);
/* Copy the saves folder to <user>/backups/<yyyymmdd-hhmmss>-<why>, then
 * drop the oldest backups beyond keep_backups. True when done (or when
 * there is nothing to back up yet). last_backup_note() says what happened,
 * for a line on the screen. */
bool backup_saves(const char* why);
const char* last_backup_note();

} /* namespace pui */

#endif
