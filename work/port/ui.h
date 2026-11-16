/* The port's screens over the game window (ui.cpp, ui_game.cpp, ui_saves.cpp). */
#ifndef PORT_UI_H
#define PORT_UI_H

/* Hook the start screen, the in-game menu (F1) and the HUD (F10) into
 * rsx_ui. After the settings are loaded, before the frame clock starts the
 * backend. have_game = 0: the game did not load, so the start screen opens
 * on its Game page with Play off. Does nothing in a build without the UI; an
 * offscreen run never calls it. The start screen is skipped with --play on
 * the command line, PS3_SKIP_START=1, or [launcher] skip_start. */
void port_ui_init(int argc, char** argv, int have_game);
/* From the frame clock, once the backend's window is open: shows the start
 * screen (if one was asked for and the UI works). */
void port_ui_backend_ready(void);
/* From main, before the guest starts: returns when the start screen's Play
 * is chosen, at once when there is none. */
void port_ui_wait_start(void);
/* 1 while the start screen is up. */
int port_ui_start_screen(void);
/* 1 when this build has the UI (Dear ImGui was there at build time). */
int port_ui_built(void);

#endif
