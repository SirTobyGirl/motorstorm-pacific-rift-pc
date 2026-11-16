/* MotorStorm: Pacific Rift port: the port's screens over the game's window,
 * drawn with Dear ImGui through the toolkit's rsx_ui (libs/video/rsx_ui.h).
 *
 *   the start screen     before the game boots, over the disc's PIC1.PNG:
 *                        Play, Settings, Saves, Game files, Help, Quit
 *   F1, or Select+Start held half a second   the in-game menu (Escape closes it)
 *   F10                  the performance HUD: detailed, compact, off
 *   F11, or Alt+Enter    full screen and back (the backend)
 *
 * The screens follow the RACiT launcher (Ratchet & Clank: A Crack in Time's PC
 * port): a start screen whose Play backs the saves up first, settings grouped
 * as display / gameplay / controller, a saves screen (ui_saves.cpp) and a
 * screen for where the game is (ui_game.cpp). The start screen, the restart
 * and the PIC1.PNG backdrop are ported from the Apocalypse port's ui.cpp.
 *
 * While a menu is open the game gets a neutral pad (cellPad.c). Every change
 * is written to user/settings.ini at once (settings.h). What only takes effect
 * at start (resolution, frame rate, shape, camera shake, folders) is applied by
 * starting the process again: Play does that by itself on the start screen;
 * in game the page offers Restart now. */
#include "settings.h"
#include "ui.h"
#include "ui_internal.h"

#ifdef PS3RECOMP_UI

#include "imgui.h"
#include "rsx_ui.h"

#include <SDL2/SDL.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

#include <sys/resource.h>   /* getrusage: the HUD's CPU figure */
#ifdef __APPLE__
#include <mach-o/dyld.h>    /* _NSGetExecutablePath: Restart now */
#endif
#ifdef PORT_HAVE_PNG
#include <png.h>            /* the start screen's backdrop, PIC1.PNG */
#endif

extern "C" unsigned ppu_boot_frames_presented(void);       /* main.cpp */
extern "C" char** port_argv;                               /* main.cpp: as the game was started */
extern "C" volatile int g_pad_sdl_ready;                   /* cellPad.c: SDL's controller subsystem is up */
/* The backend's side: draws in the last frame, the upscaler's switch and what
 * it is doing. FSR 1 on the Vulkan backend, MetalFX on the Metal one. */
#ifdef __APPLE__
extern "C" unsigned rsx_metal_backend_guest_draws(void);
extern "C" void rsx_metal_backend_set_upscaler(int on, float sharpness);
extern "C" int  rsx_metal_backend_upscaler_state(unsigned* in_w, unsigned* in_h, unsigned* out_w, unsigned* out_h);
#define rsx_backend_guest_draws    rsx_metal_backend_guest_draws
#define rsx_backend_set_upscaler   rsx_metal_backend_set_upscaler
#define rsx_backend_upscaler_state rsx_metal_backend_upscaler_state
#define PORT_UPSCALER_NAME "MetalFX"
#define PORT_FULLSCREEN_KEYS "F11, Option+Return"
#else
extern "C" unsigned rsx_vulkan_backend_guest_draws(void);  /* draws in the last frame */
extern "C" void rsx_vulkan_backend_set_fsr(int on, float sharpness);
extern "C" int  rsx_vulkan_backend_fsr_state(unsigned* in_w, unsigned* in_h, unsigned* out_w, unsigned* out_h);
#define rsx_backend_guest_draws    rsx_vulkan_backend_guest_draws
#define rsx_backend_set_upscaler   rsx_vulkan_backend_set_fsr
#define rsx_backend_upscaler_state rsx_vulkan_backend_fsr_state
#define PORT_UPSCALER_NAME "FSR 1"
#define PORT_FULLSCREEN_KEYS "F11, Alt+Enter"
#endif

namespace {

double g_fps = 0.0, g_fps_t = 0.0;
/* Ported from the Apocalypse port's ui.cpp (k_scales). */
const char* const k_scales[] = { "720p (1x)", "1440p (2x)", "4K (3x)" };
int g_boot_scale = 1;          /* the scale this run renders at (PS3RECOMP_RES_SCALE at start) */
int g_boot_fps = 30;           /* the frame rate this run runs at (PS3RECOMP_FPS at start) */
const char* const k_fps[] = { "30 fps (the original)", "60 fps" };
unsigned g_fps_frames = 0;
bool g_styled = false;
port_settings g_boot;          /* the settings this run started with */

/* The start screen: asked for (port_ui_init), up (port_ui_backend_ready until
 * Play), and the boot's go (port_ui_wait_start). */
bool g_want_start = false;
bool g_have_game = true;
std::atomic<bool> g_start{false};
std::atomic<int> g_go{1};

const ImU32 k_accent = IM_COL32(255, 158, 51, 255);              /* volcanic orange */

void style()
{
    ImGuiStyle& s = ImGui::GetStyle();
    ImGui::StyleColorsDark(&s);
    s.WindowRounding = 10.0f; s.FrameRounding = 6.0f; s.TabRounding = 6.0f;
    s.GrabRounding = 6.0f; s.PopupRounding = 6.0f; s.ChildRounding = 6.0f;
    s.WindowPadding = ImVec2(22, 18); s.FramePadding = ImVec2(12, 7);
    s.ItemSpacing = ImVec2(12, 10); s.WindowBorderSize = 0.0f;
    ImVec4* c = s.Colors;
    const ImVec4 accent(1.00f, 0.62f, 0.20f, 1.00f);
    c[ImGuiCol_WindowBg]        = ImVec4(0.07f, 0.06f, 0.05f, 0.94f);
    c[ImGuiCol_ChildBg]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_PopupBg]         = ImVec4(0.09f, 0.08f, 0.07f, 0.98f);
    c[ImGuiCol_FrameBg]         = ImVec4(0.18f, 0.15f, 0.12f, 1.00f);
    c[ImGuiCol_FrameBgHovered]  = ImVec4(0.27f, 0.20f, 0.14f, 1.00f);
    c[ImGuiCol_FrameBgActive]   = ImVec4(0.33f, 0.23f, 0.15f, 1.00f);
    c[ImGuiCol_Tab]             = ImVec4(0.16f, 0.13f, 0.10f, 1.00f);
    c[ImGuiCol_TabHovered]      = ImVec4(0.55f, 0.32f, 0.12f, 1.00f);
    c[ImGuiCol_TabSelected]     = ImVec4(0.62f, 0.34f, 0.10f, 1.00f);
    c[ImGuiCol_Button]          = ImVec4(0.24f, 0.18f, 0.13f, 1.00f);
    c[ImGuiCol_ButtonHovered]   = ImVec4(0.55f, 0.32f, 0.12f, 1.00f);
    c[ImGuiCol_ButtonActive]    = ImVec4(0.72f, 0.40f, 0.12f, 1.00f);
    c[ImGuiCol_Header]          = ImVec4(0.24f, 0.18f, 0.13f, 1.00f);
    c[ImGuiCol_HeaderHovered]   = ImVec4(0.55f, 0.32f, 0.12f, 1.00f);
    c[ImGuiCol_HeaderActive]    = ImVec4(0.72f, 0.40f, 0.12f, 1.00f);
    c[ImGuiCol_TitleBg]         = ImVec4(0.30f, 0.17f, 0.07f, 1.00f);
    c[ImGuiCol_TitleBgActive]   = ImVec4(0.45f, 0.24f, 0.08f, 1.00f);
    c[ImGuiCol_ScrollbarGrab]   = ImVec4(0.40f, 0.30f, 0.22f, 1.00f);
    c[ImGuiCol_CheckMark]       = accent;
    c[ImGuiCol_SliderGrab]      = accent;
    c[ImGuiCol_NavHighlight]    = accent;
    c[ImGuiCol_Separator]       = ImVec4(0.33f, 0.26f, 0.20f, 1.00f);
    c[ImGuiCol_Text]            = ImVec4(0.97f, 0.94f, 0.90f, 1.00f);
    c[ImGuiCol_TextDisabled]    = ImVec4(0.68f, 0.62f, 0.55f, 1.00f);
}

void save()
{
    if (port_settings_save(port_settings_get(), port_settings_path()))
        fprintf(stderr, "[ui] could not write %s\n", port_settings_path());
}

void hint(const char* t)
{
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("%s", t);
    ImGui::PopStyleColor();
}

void coloured(const ImVec4& c, const char* t)
{
    ImGui::PushStyleColor(ImGuiCol_Text, c);
    ImGui::TextWrapped("%s", t);
    ImGui::PopStyleColor();
}

/* A section heading inside a page. */
void heading(const char* t)
{
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(k_accent));
    ImGui::TextUnformatted(t);
    ImGui::PopStyleColor();
    ImGui::Separator();
}

void pin_begin(unsigned bit)
{
    if (port_settings_pinned() & bit) ImGui::BeginDisabled();
}

void pin_end(unsigned bit)
{
    if (!(port_settings_pinned() & bit)) return;
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Set by %s in the environment, which wins over this menu.",
                          port_settings_pin_var(bit));
}

void open_folder(const char* path)
{
#ifndef _WIN32
#ifdef __APPLE__
    const char* tool = "open";
#else
    const char* tool = "xdg-open";
#endif
    char* const av[] = { (char*)tool, (char*)path, nullptr };
    pid_t pid;
    if (posix_spawnp(&pid, tool, nullptr, nullptr, av, environ) == 0) {
        std::thread([pid] { int st; waitpid(pid, &st, 0); }).detach();   /* reap it */
    } else {
        fprintf(stderr, "[ui] could not start %s %s\n", tool, path);
    }
#else
    (void)path;
#endif
}

/* The settings that only take effect at start and differ from this run's,
 * as a list for a line on the screen ("" when none). A row the environment
 * decides does not count: a restart keeps that variable. */
std::string restart_note(const port_settings& s)
{
    const unsigned pin = port_settings_pinned();
    std::string n;
    auto add = [&](const char* w) { n += n.empty() ? "" : ", "; n += w; };
    if (!(pin & PORT_PIN_SCALE) && s.scale != g_boot.scale) add("resolution");
    if (!(pin & PORT_PIN_FPS) && s.fps != g_boot.fps) add("frame rate");
    if (!(pin & PORT_PIN_STRETCH) && s.stretch != g_boot.stretch) add("picture shape");
    if (!(pin & PORT_PIN_CAMERA_SHAKE) && s.camera_shake != g_boot.camera_shake) add("camera shake");
    if (!(pin & PORT_PIN_CATCH_UP) && s.catch_up != g_boot.catch_up) add("frame-time catch-up");
    if (strcmp(s.game_dir, g_boot.game_dir)) add("game folder");
    if (!(pin & PORT_PIN_DEV_FLASH) && strcmp(s.dev_flash, g_boot.dev_flash)) add("dev_flash folder");
    if (!(pin & PORT_PIN_SAVES) && strcmp(s.saves_dir, g_boot.saves_dir)) add("saves folder");
    return n;
}

/* Start the process again: same binary, same arguments, the settings file
 * read anew (port_settings_restart_env drops what this run exported from it).
 * Idea from the Apocalypse port's ui.cpp (restart_process). */
void restart_now(const char* extra)
{
#ifndef _WIN32
    save();
    char** env = port_settings_restart_env(extra);
    if (!env || !port_argv || !port_argv[0]) { fprintf(stderr, "[ui] restart failed\n"); return; }
    char self[4096];
#ifdef __APPLE__
    { uint32_t sz = sizeof self;
      if (_NSGetExecutablePath(self, &sz) != 0) return; }
#else
    const ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
    if (n <= 0) return;
    self[n] = 0;
#endif
    fprintf(stderr, "[ui] starting again%s%s\n", extra ? " with " : "", extra ? extra : "");
    fflush(stderr); fflush(stdout);
    execve(self, port_argv, env);
    fprintf(stderr, "[ui] restart failed\n");
#else
    (void)extra;
#endif
}

/* In game: start again straight into the game (no start screen). */
void restart_button()
{
    const std::string note = restart_note(*port_settings_get());
    if (note.empty() || g_start) return;
    ImGui::Spacing();
    coloured(ImVec4(1.0f, 0.80f, 0.40f, 1.0f), ("Changed, applies after a restart: " + note + ".").c_str());
    if (ImGui::Button("Restart now")) {
        if (port_settings_get()->backup_on_play) pui::backup_saves("restart");
        restart_now("PS3_SKIP_START=1");
    }
    ImGui::SameLine();
    hint("Race progress since the last save is lost.");
}

/* A note under a row that changes only at start. */
void at_start_hint(bool changed)
{
    if (changed) hint(g_start ? "Applies when you press Play." : "Takes effect after a restart.");
}

void set_fps_counter(int mode)
{
    port_settings& s = *port_settings_get();
    s.fps_counter = mode >= 0 && mode <= 2 ? mode : 0;
    if (!g_start) rsx_ui_set_visible(s.fps_counter);
    save();
}

/* ---- settings pages --------------------------------------------------- */

void page_display(port_settings& s)
{
    heading("Window");
    /* The window as it is: F11 changes it without the menu. */
    bool b = rsx_ui_fullscreen();
    pin_begin(PORT_PIN_FULLSCREEN);
    if (ImGui::Checkbox("Full screen  (" PORT_FULLSCREEN_KEYS ")", &b)) {
        s.fullscreen = b; rsx_ui_set_fullscreen(b); save();
    }
    pin_end(PORT_PIN_FULLSCREEN);
    static const char* const k_shape[] = { "Keep 16:9 (black bars)", "Stretch to the window" };
    int sh = s.stretch ? 1 : 0;
    pin_begin(PORT_PIN_STRETCH);
    if (ImGui::Combo("Picture shape", &sh, k_shape, 2)) { s.stretch = sh; save(); }
    pin_end(PORT_PIN_STRETCH);
    at_start_hint(s.stretch != g_boot.stretch);

    heading("Graphics");
    /* Render scale: every render target N times the game's 1280x720. */
    int sc = s.scale - 1;
    pin_begin(PORT_PIN_SCALE);
    if (ImGui::Combo("Resolution", &sc, k_scales, 3)) { s.scale = sc + 1; save(); }
    pin_end(PORT_PIN_SCALE);
    at_start_hint(s.scale != g_boot.scale);
    hint("The game renders at this multiple of the PS3's 1280x720. 1440p needs a mid-range "
         "GPU; 4K a fast one.");
    /* The upscaler (AMD FSR 1 on Vulkan, MetalFX on Metal): takes the
     * presented frame to the window's size, applies now. */
#ifdef __APPLE__
    const char* const up_label = "MetalFX upscaling";
#else
    const char* const up_label = "AMD FSR 1 upscaling";
#endif
    bool fsr = s.upscaler != 0;
    pin_begin(PORT_PIN_UPSCALER);
    if (ImGui::Checkbox(up_label, &fsr)) {
        s.upscaler = fsr ? 1 : 0;
        rsx_backend_set_upscaler(s.upscaler, s.fsr_sharpness);
        save();
    }
    pin_end(PORT_PIN_UPSCALER);
    if (s.upscaler) {
        float shp = s.fsr_sharpness;
        if (ImGui::SliderFloat("Sharpness", &shp, 0.0f, 1.0f, "%.2f")) {
            s.fsr_sharpness = shp;
            rsx_backend_set_upscaler(1, shp);
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) save();
    }
    hint("Upscaling sharpens the game's frame up to a window larger than it; it applies at once.");

    if (!g_start) {
        heading("Now");
        const ImGuiIO& io = ImGui::GetIO();
        unsigned iw = 0, ih = 0, ow = 0, oh = 0;
        const int on = rsx_backend_upscaler_state(&iw, &ih, &ow, &oh);
        ImGui::Text("Window: %.0f x %.0f", io.DisplaySize.x * io.DisplayFramebufferScale.x,
                    io.DisplaySize.y * io.DisplayFramebufferScale.y);
        ImGui::Text("Game frame: %u x %u (%dx)", iw, ih, g_boot_scale);
        if (on) ImGui::Text(PORT_UPSCALER_NAME ": %u x %u -> %u x %u", iw, ih, ow, oh);
        else if (s.upscaler) hint(PORT_UPSCALER_NAME " is idle: the window is not larger than the game frame.");
        else ImGui::TextUnformatted(g_boot.stretch ? "Stretched to the window" : "Fitted to the window, shape kept");
    }
    restart_button();
}

void page_gameplay(port_settings& s)
{
    heading("Frame rate");
    int fi = s.fps == 60 ? 1 : 0;
    pin_begin(PORT_PIN_FPS);
    if (ImGui::Combo("Frame rate", &fi, k_fps, 2)) { s.fps = fi ? 60 : 30; save(); }
    pin_end(PORT_PIN_FPS);
    at_start_hint(s.fps != g_boot.fps);
    hint("60 fps runs the game's physics at 60 steps a second at the right speed. It needs a "
         "fast CPU; below 60 the game runs a little slow rather than jerking.");
    bool cu = s.catch_up != 0;
    pin_begin(PORT_PIN_CATCH_UP);
    if (ImGui::Checkbox("Catch up after a slow frame (60 fps)", &cu)) { s.catch_up = cu; save(); }
    pin_end(PORT_PIN_CATCH_UP);
    at_start_hint(s.catch_up != g_boot.catch_up);
    hint("Keeps the game's speed exact when a frame is late, but the car lurches and riders can "
         "fall off. Off is smoother.");

    heading("Camera");
    bool cs = s.camera_shake != 0;
    pin_begin(PORT_PIN_CAMERA_SHAKE);
    if (ImGui::Checkbox("Camera shake", &cs)) { s.camera_shake = cs; save(); }
    pin_end(PORT_PIN_CAMERA_SHAKE);
    at_start_hint(s.camera_shake != g_boot.camera_shake);
    hint("The game's own shake on rough ground and crashes. Off: a steady camera.");

    heading("Performance HUD");
    static const char* const k_hud[] = { "Off", "Detailed", "Compact" };
    int h = s.fps_counter;
    if (ImGui::Combo("HUD  (F10)", &h, k_hud, 3)) set_fps_counter(h);
    if (!g_start) {
        ImGui::Text("Now: %.1f fps, %u draws in the last frame", g_fps, rsx_backend_guest_draws());
    }

    heading("Start screen");
    bool sk = s.skip_start != 0;
    pin_begin(PORT_PIN_SKIP_START);
    if (ImGui::Checkbox("Skip the start screen, boot straight into the game", &sk)) { s.skip_start = sk; save(); }
    pin_end(PORT_PIN_SKIP_START);
    hint("F1 (or Select+Start held) still opens these settings in game. To get the start screen "
         "back, untick this there.");
    restart_button();
}

void page_controller()
{
    /* SDL's joystick calls are not safe while the game's cellPadInit may be
     * starting SDL's controller support on another thread. Before the boot
     * (the start screen) the game is not running and the window's thread
     * started it itself (port_ui_backend_ready). */
    const bool sdl_ok = g_pad_sdl_ready || (g_start && SDL_WasInit(SDL_INIT_GAMECONTROLLER));
    if (!sdl_ok) {
        ImGui::TextUnformatted("Controller: the game has not started its pads yet");
        return;
    }
    heading("Controllers");
    int shown = 0;
    std::string held;
    /* Under SDL's joystick lock: cellPad polls the same pads from the game's
     * threads (see rsx_ui_frame). */
    SDL_LockJoysticks();
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (!SDL_IsGameController(i)) continue;
        SDL_GameController* gc = SDL_GameControllerFromInstanceID(SDL_JoystickGetDeviceInstanceID(i));
        const char* name = SDL_GameControllerNameForIndex(i);
        ImGui::BulletText("%s", name ? name : "a controller");
        shown++;
        if (gc && held.empty()) {
            static const struct { SDL_GameControllerButton b; const char* n; } k[] = {
                { SDL_CONTROLLER_BUTTON_A, "Cross/A" }, { SDL_CONTROLLER_BUTTON_B, "Circle/B" },
                { SDL_CONTROLLER_BUTTON_X, "Square/X" }, { SDL_CONTROLLER_BUTTON_Y, "Triangle/Y" },
                { SDL_CONTROLLER_BUTTON_LEFTSHOULDER, "L1" }, { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, "R1" },
                { SDL_CONTROLLER_BUTTON_BACK, "Select" }, { SDL_CONTROLLER_BUTTON_START, "Start" },
                { SDL_CONTROLLER_BUTTON_LEFTSTICK, "L3" }, { SDL_CONTROLLER_BUTTON_RIGHTSTICK, "R3" },
                { SDL_CONTROLLER_BUTTON_DPAD_UP, "Up" }, { SDL_CONTROLLER_BUTTON_DPAD_DOWN, "Down" },
                { SDL_CONTROLLER_BUTTON_DPAD_LEFT, "Left" }, { SDL_CONTROLLER_BUTTON_DPAD_RIGHT, "Right" },
            };
            for (const auto& e : k)
                if (SDL_GameControllerGetButton(gc, e.b)) { held += held.empty() ? "" : "  "; held += e.n; }
            const int l2 = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
            const int r2 = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
            if (l2 > 8000) { held += held.empty() ? "" : "  "; held += "L2"; }
            if (r2 > 8000) { held += held.empty() ? "" : "  "; held += "R2"; }
        }
    }
    SDL_UnlockJoysticks();
    if (!shown) {
        coloured(ImVec4(1.0f, 0.80f, 0.40f, 1.0f), "No controller found. Connect one; most are picked up "
                 "while the game runs (Xbox, PlayStation, Switch Pro, Steam Input).");
    } else {
        ImGui::Text("Buttons held: %s", held.empty() ? "none" : held.c_str());
        hint("Press buttons to test them here. The game sees a neutral pad while a menu is open.");
    }
}

void page_keys()
{
    heading("Port keys");
    ImGui::TextUnformatted("F1          the settings (or Select+Start held on a pad)");
    ImGui::TextUnformatted("F10         the performance HUD: detailed, compact, off");
    ImGui::TextUnformatted("F11         full screen and back (" PORT_FULLSCREEN_KEYS ")");
    ImGui::TextUnformatted("Escape      close a menu; with none open, quit the game");
    heading("Menus");
    hint("Mouse, keyboard (arrows, Enter, Escape) or a pad (D-pad or stick, Cross/A selects, "
         "Circle/B goes back) all work in the start screen and the settings.");
}

/* ---- the in-game menu ---------------------------------------------------- */

/* A page body that scrolls when the window is short. */
void begin_body(float bottom_reserve)
{
    ImGui::BeginChild("##body", ImVec2(0, -bottom_reserve), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NavFlattened);
}

void draw_menu()
{
    port_settings& s = *port_settings_get();
    ImGuiIO& io = ImGui::GetIO();
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 150));
    const float fs = ImGui::GetFontSize();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(fs * 40.0f, io.DisplaySize.x * 0.94f),
                                    std::min(fs * 32.0f, io.DisplaySize.y * 0.92f)), ImGuiCond_Always);
    ImGui::Begin("MotorStorm: Pacific Rift  ·  Settings", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    if (ImGui::BeginTabBar("pages")) {
        /* Each tab's body scrolls on its own; the bar and the footer stay. */
        ImGui::PushID("ingame");
        if (ImGui::BeginTabItem("Display")) { begin_body(fs * 2.8f); page_display(s); ImGui::EndChild(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Gameplay")) { begin_body(fs * 2.8f); page_gameplay(s); ImGui::EndChild(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Controller")) { begin_body(fs * 2.8f); page_controller(); ImGui::EndChild(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Saves")) { begin_body(fs * 2.8f); pui::page_saves(s); ImGui::EndChild(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Game files")) { begin_body(fs * 2.8f); pui::page_game(s); ImGui::EndChild(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Keys")) { begin_body(fs * 2.8f); page_keys(); ImGui::EndChild(); ImGui::EndTabItem(); }
        ImGui::PopID();
        ImGui::EndTabBar();
    }
    ImGui::Separator();
    hint("Saved at once to user/settings.ini.");
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - fs * 7.5f);
    if (ImGui::Button("Close  (F1)")) rsx_ui_set_menu_open(0);
    ImGui::End();
}

/* ---- the start screen ---------------------------------------------------- */

enum StartPage { PAGE_MAIN, PAGE_SETTINGS, PAGE_SAVES, PAGE_GAME, PAGE_HELP };
StartPage g_page = PAGE_MAIN;
bool g_page_focus = true;      /* focus the page's first item on its first frame */
std::string g_play_note;       /* why Play did not start, for a line under the menu */

void go_page(StartPage p) { g_page = p; g_page_focus = true; }

/* One line of the menu: big text, an optional line under it, a bar beside
 * the one under the pointer or the pad. From the Apocalypse port's ui.cpp. */
bool menu_item(const char* id, const char* label, const char* sub, bool enabled = true)
{
    const float fs = ImGui::GetFontSize();
    ImFont* tf = (ImFont*)rsx_ui_title_font();
    const float big = fs * 1.75f;
    const float h = big * 1.25f + (sub ? fs * 1.35f : 0.0f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    if (g_page_focus) { ImGui::SetKeyboardFocusHere(); g_page_focus = false; }
    const bool pressed = ImGui::Selectable(id, false, 0, ImVec2(ImGui::GetContentRegionAvail().x, h));
    const bool hot = ImGui::IsItemHovered() || ImGui::IsItemFocused();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hot) dl->AddRectFilled(p, ImVec2(p.x + fs * 0.22f, p.y + h - fs * 0.25f), k_accent);
    const ImU32 main_col = !enabled ? IM_COL32(150, 140, 130, 170)
                         : hot ? IM_COL32(255, 255, 255, 255) : IM_COL32(220, 210, 196, 215);
    dl->AddText(tf, big, ImVec2(p.x + fs * 0.85f, p.y), main_col, label);
    if (sub)
        dl->AddText(ImVec2(p.x + fs * 0.9f, p.y + big * 1.12f),
                    !enabled ? IM_COL32(255, 180, 90, 230)
                    : hot ? IM_COL32(255, 200, 140, 255) : IM_COL32(190, 178, 160, 200), sub);
    return pressed && enabled;
}

/* PIC1.PNG filling the window, darkened on the left where the menu is. */
void draw_backdrop(const ImVec2& d, bool panel)
{
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    int iw = 0, ih = 0;
    if (void* tex = rsx_ui_image(&iw, &ih)) {
        const float da = d.x / d.y, ia = (float)iw / (float)ih;
        ImVec2 uv0(0, 0), uv1(1, 1);
        if (da > ia) { const float v = ia / da; uv0.y = (1 - v) * 0.5f; uv1.y = 1 - uv0.y; }
        else         { const float u = da / ia; uv0.x = (1 - u) * 0.5f; uv1.x = 1 - uv0.x; }
        bg->AddImage((ImTextureID)(uintptr_t)tex, ImVec2(0, 0), d, uv0, uv1);
    } else {
        bg->AddRectFilledMultiColor(ImVec2(0, 0), d, IM_COL32(60, 30, 10, 255), IM_COL32(30, 14, 6, 255),
                                    IM_COL32(6, 3, 2, 255), IM_COL32(16, 8, 4, 255));
    }
    const ImU32 dark = IM_COL32(0, 0, 0, 225), clear = IM_COL32(0, 0, 0, 0);
    bg->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(d.x * 0.62f, d.y), dark, clear, clear, dark);
    bg->AddRectFilled(ImVec2(0, 0), d, IM_COL32(0, 0, 0, panel ? 140 : 30));
}

/* "Play": back the saves up, then boot -- or start the process again when a
 * setting that only takes effect at start changed here (nothing has booted,
 * so that costs only the start-up). */
void play()
{
    port_settings& s = *port_settings_get();
    save();
    if (!g_have_game || !pui::game_ready(s)) {
        g_play_note = "The game is not where it should be: see Game files.";
        go_page(PAGE_GAME);
        return;
    }
    if (s.backup_on_play && !pui::backup_saves("play"))
        fprintf(stderr, "[ui] the save backup failed (%s); starting anyway\n", pui::last_backup_note());
    if (!restart_note(s).empty()) {
        fprintf(stderr, "[ui] starting again with the new settings (%s)\n", restart_note(s).c_str());
        restart_now("PS3_SKIP_START=1");
        g_play_note = "Could not start again with the new settings; they apply next time.";
    }
    g_start = false;
    rsx_ui_set_modal(0);
    rsx_ui_set_menu_open(0);
    rsx_ui_set_visible(s.fps_counter);
    g_go = 1;
}

void quit()
{
    fflush(stdout); fflush(stderr);
    _Exit(0);
}

void page_help()
{
    heading("MotorStorm: Pacific Rift  ·  PC port");
    { FILE* f = fopen("version.txt", "r");      /* the repository's, beside disc/ */
      char line[160];
      while (f && fgets(line, sizeof line, f)) {
          line[strcspn(line, "\r\n")] = 0;
          if (!strncmp(line, "version:", 8) || !strncmp(line, "toolkit:", 8)) hint(line);
      }
      if (f) fclose(f); }
    hint("A static recompilation of the PS3 game (BCES-00129). Your own dumped copy of the game "
         "is needed; nothing of the game comes with the port.");
    page_keys();
    heading("Files");
    if (ImGui::Button("Open the logs folder")) open_folder("work/runs");
    ImGui::SameLine();
    if (ImGui::Button("Open the settings folder")) open_folder(port_user_dir());
    hint("Each run writes a log to work/runs (run.sh). Attach the latest when you report a problem.");
}

void draw_start()
{
    port_settings& s = *port_settings_get();
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 d = io.DisplaySize;
    const float fs = ImGui::GetFontSize();
    const bool panel = g_page != PAGE_MAIN;
    draw_backdrop(d, panel);

    /* the title, top left */
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    ImFont* tf = (ImFont*)rsx_ui_title_font();
    const ImVec2 tp(d.x * 0.07f, d.y * 0.08f);
    const float th = fs * 3.0f;
    bg->AddText(tf, th, tp, IM_COL32(255, 255, 255, 255), "MotorStorm: Pacific Rift");
    bg->AddRectFilled(ImVec2(tp.x + 4, tp.y + th * 1.06f), ImVec2(tp.x + 4 + fs * 5.0f, tp.y + th * 1.06f + 5), k_accent);

    /* Circle, Escape: back to the main page. */
    if (g_page != PAGE_MAIN && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) &&
        (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
        go_page(PAGE_MAIN);

    const float top = tp.y + th * 1.06f + fs * 2.0f;
    const float width = panel ? std::min(fs * 42.0f, d.x * 0.88f) : std::min(fs * 30.0f, d.x * 0.62f);
    const float height = d.y - top - fs * 2.8f;
    ImGui::SetNextWindowPos(ImVec2(tp.x - fs * 0.9f, top), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
    /* The main page sits on a dark glass panel: PIC1.PNG keeps a blank white
     * box (where the PS3 put the game's icon) right behind the menu. */
    if (!panel) ImGui::SetNextWindowBgAlpha(0.90f);
    ImGui::Begin("##start", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoScrollbar);
    bool go = false;
    if (g_page == PAGE_MAIN) {
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_NavHighlight, ImVec4(0, 0, 0, 0));
        const bool ready = g_have_game && pui::game_ready(s);
        const std::string note = restart_note(s);
        std::string play_sub;
        if (!ready) play_sub = "The game was not found: set it up in Game files";
        else if (s.backup_on_play) play_sub = "Backs up your saves, then starts the game";
        else play_sub = "Starts the game";
        if (ready && !note.empty()) play_sub += " (with the new " + note + ")";
        if (menu_item("##play", "PLAY", play_sub.c_str(), ready)) go = true;
        if (menu_item("##settings", "SETTINGS", "Display, gameplay, controller")) go_page(PAGE_SETTINGS);
        if (menu_item("##saves", "SAVES", "Back up and restore")) go_page(PAGE_SAVES);
        if (menu_item("##game", "GAME FILES", ready ? "Where the game, firmware and saves are"
                                                    : "Not set up yet: start here")) go_page(PAGE_GAME);
        if (menu_item("##help", "HELP", "Controls, logs, about")) go_page(PAGE_HELP);
        if (menu_item("##quit", "QUIT", nullptr)) quit();
        ImGui::PopStyleColor(4);
        if (!g_play_note.empty()) { ImGui::Spacing(); coloured(ImVec4(1.0f, 0.80f, 0.40f, 1.0f), g_play_note.c_str()); }
    } else {
        static const char* const k_titles[] = { "", "Settings", "Saves", "Game files", "Help" };
        ImGui::PushFont(nullptr);
        ImGui::TextUnformatted(k_titles[g_page]);
        ImGui::PopFont();
        ImGui::Separator();
        begin_body(fs * 3.0f);
        switch (g_page) {
        case PAGE_SETTINGS:
            if (ImGui::BeginTabBar("pages")) {
                if (ImGui::BeginTabItem("Display")) { page_display(s); ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("Gameplay")) { page_gameplay(s); ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("Controller")) { page_controller(); ImGui::EndTabItem(); }
                ImGui::EndTabBar();
            }
            break;
        case PAGE_SAVES: pui::page_saves(s); break;
        case PAGE_GAME:  pui::page_game(s); break;
        case PAGE_HELP:  page_help(); break;
        default: break;
        }
        ImGui::EndChild();
        ImGui::Separator();
        if (g_page_focus) { ImGui::SetKeyboardFocusHere(); g_page_focus = false; }
        if (ImGui::Button("BACK", ImVec2(fs * 7.0f, fs * 1.8f))) go_page(PAGE_MAIN);
        ImGui::SameLine();
        const std::string note = restart_note(s);
        hint(note.empty() ? "Saved at once. F1 (or Select+Start held) opens the settings in game."
                          : ("Play starts the game with the new " + note + ".").c_str());
    }
    ImGui::End();

    /* the controls, bottom left */
    const char* keys = g_page == PAGE_MAIN ? "Cross/A / Enter  select"
                                           : "Cross/A / Enter  select      Circle/B / Esc  back";
    bg->AddText(ImVec2(tp.x, d.y - fs * 2.0f), IM_COL32(220, 210, 196, 200), keys);
    /* PS3_UI_AUTOPLAY_MS=<ms>: press Play by itself that long after the start
     * screen first draws -- a scripted run through the start screen without a
     * hand on the pad (from the Apocalypse port). */
    static long autoplay_ms = -2;
    static double autoplay_at = 0;
    if (autoplay_ms == -2) {
        const char* e = getenv("PS3_UI_AUTOPLAY_MS");
        autoplay_ms = (e && *e) ? atol(e) : -1;
        autoplay_at = ImGui::GetTime() + (double)autoplay_ms / 1000.0;
    }
    if (autoplay_ms >= 0 && g_page == PAGE_MAIN && ImGui::GetTime() >= autoplay_at) {
        autoplay_ms = -1;
        go = true;
    }
    if (go) play();
}

/* ---- performance HUD ------------------------------------------------------
 * Frame times come from the guest's presented-frame counter, sampled on each
 * UI frame: the time since it last moved, split over the frames it moved by.
 * The last k_ft samples feed the graph and the 1% low / worst figures. */
constexpr int k_ft = 240;
float g_ft[k_ft];               /* ms, ring */
int g_ft_n = 0, g_ft_i = 0;
double g_ft_since = 0.0;        /* s since the counter last moved */
unsigned g_ft_last = 0;
double g_cpu = -1.0;            /* this process, % of one core */

void hud_sample(double dt)
{
    const unsigned f = ppu_boot_frames_presented();
    g_ft_since += dt;
    if (f != g_ft_last) {
        const unsigned d = f - g_ft_last;
        g_ft_last = f;
        if (d < 1000) {
            const float ms = (float)(g_ft_since * 1000.0 / d);
            for (unsigned k = 0; k < d && k < (unsigned)k_ft; k++) {
                g_ft[g_ft_i] = ms; g_ft_i = (g_ft_i + 1) % k_ft;
                if (g_ft_n < k_ft) g_ft_n++;
            }
        }
        g_ft_since = 0.0;
    }
#ifndef _WIN32
    /* CPU time of the whole process (every guest thread) every half second. */
    static double acc = 0.0, last_cpu = -1.0;
    acc += dt;
    if (acc >= 0.5) {
        struct rusage ru;
        if (getrusage(RUSAGE_SELF, &ru) == 0) {
            const double t = (double)ru.ru_utime.tv_sec + (double)ru.ru_utime.tv_usec / 1e6 +
                             (double)ru.ru_stime.tv_sec + (double)ru.ru_stime.tv_usec / 1e6;
            if (last_cpu >= 0.0) g_cpu = (t - last_cpu) / acc * 100.0;
            last_cpu = t;
        }
        acc = 0.0;
    }
#endif
}

ImU32 fps_colour(double fps, double target)
{
    if (fps >= target * 0.95) return IM_COL32(120, 230, 120, 255);
    if (fps >= target * 0.75) return IM_COL32(255, 205, 90, 255);
    return IM_COL32(255, 100, 80, 255);
}

void draw_fps_compact()
{
    const ImGuiIO& io = ImGui::GetIO();
    char t[32];
    snprintf(t, sizeof t, "%.0f fps", g_fps);
    const ImVec2 sz = ImGui::CalcTextSize(t);
    const ImVec2 p(io.DisplaySize.x - sz.x - 18.0f, 12.0f);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(ImVec2(p.x - 8, p.y - 4), ImVec2(p.x + sz.x + 8, p.y + sz.y + 4), IM_COL32(0, 0, 0, 150), 6.0f);
    dl->AddText(p, fps_colour(g_fps, g_boot_fps), t);
}

void draw_fps_detailed()
{
    const ImGuiIO& io = ImGui::GetIO();
    const float fs = ImGui::GetFontSize();
    const double target = g_boot_fps, target_ms = 1000.0 / target;

    /* Average, worst and 1% low over the samples held. */
    float sorted[k_ft];
    double sum = 0.0;
    for (int k = 0; k < g_ft_n; k++) { sorted[k] = g_ft[k]; sum += g_ft[k]; }
    std::sort(sorted, sorted + g_ft_n);
    const double avg = g_ft_n ? sum / g_ft_n : 0.0;
    const double worst = g_ft_n ? sorted[g_ft_n - 1] : 0.0;
    double low1 = 0.0;              /* mean fps of the slowest 1% of frames */
    if (g_ft_n) {
        const int m = g_ft_n / 100 > 0 ? g_ft_n / 100 : 1;
        double s = 0.0;
        for (int k = g_ft_n - m; k < g_ft_n; k++) s += sorted[k];
        low1 = 1000.0 / (s / m);
    }

    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 12.0f, 12.0f), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.62f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fs * 0.7f, fs * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(fs * 0.5f, fs * 0.2f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::Begin("##hud", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    const float w = fs * 15.0f;

    /* fps, large, coloured against the target */
    ImGui::SetWindowFontScale(1.6f);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(fps_colour(g_fps, target)));
    ImGui::Text("%.0f", g_fps);
    ImGui::PopStyleColor();
    ImGui::SetWindowFontScale(1.0f);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("fps  / %d", g_boot_fps);

    ImGui::Text("%.1f ms avg   %.1f ms worst", avg, worst);
    ImGui::Text("1%% low %.0f fps", low1);

    /* frame-time graph: bars, oldest left; dashed target line */
    const float gh = fs * 3.0f;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + gh), IM_COL32(0, 0, 0, 90), 4.0f);
    const float top_ms = (float)(target_ms * 2.5);
    const float bw = w / k_ft;
    for (int k = 0; k < g_ft_n; k++) {
        const int idx = (g_ft_i - g_ft_n + k + k_ft) % k_ft;
        const float ms = g_ft[idx];
        const float hgt = gh * (ms < top_ms ? ms / top_ms : 1.0f);
        const float x = p0.x + (k_ft - g_ft_n + k) * bw;
        const ImU32 c = ms <= target_ms * 1.05 ? IM_COL32(120, 230, 120, 200)
                      : ms <= target_ms * 1.5 ? IM_COL32(255, 205, 90, 220) : IM_COL32(255, 100, 80, 240);
        dl->AddRectFilled(ImVec2(x, p0.y + gh - hgt), ImVec2(x + (bw > 1.5f ? bw - 0.5f : bw), p0.y + gh), c);
    }
    const float ty = p0.y + gh - gh * (float)(target_ms / top_ms);
    for (float x = p0.x; x < p0.x + w; x += 8.0f)
        dl->AddLine(ImVec2(x, ty), ImVec2(x + 4.0f < p0.x + w ? x + 4.0f : p0.x + w, ty), IM_COL32(255, 255, 255, 110));
    ImGui::Dummy(ImVec2(w, gh));

    /* what is being rendered */
    unsigned iw = 0, ih = 0, ow = 0, oh = 0;
    const int fsr = rsx_backend_upscaler_state(&iw, &ih, &ow, &oh);
    if (iw && ih) {
        if (fsr) ImGui::TextDisabled("%ux%u  " PORT_UPSCALER_NAME " -> %ux%u", iw, ih, ow, oh);
        else     ImGui::TextDisabled("%ux%u  (%dx)", iw, ih, g_boot_scale);
    }
    if (g_cpu >= 0.0) ImGui::TextDisabled("%u draws   CPU %.0f%%", rsx_backend_guest_draws(), g_cpu);
    else              ImGui::TextDisabled("%u draws", rsx_backend_guest_draws());

    ImGui::End();
    ImGui::PopStyleVar(3);
}

void draw_fps()
{
    if (port_settings_get()->fps_counter == 2) draw_fps_compact();
    else draw_fps_detailed();
}

void draw(void*)
{
    if (!g_styled) { style(); g_styled = true; }
    /* Guest frames presented over half a second; a fresh count after the UI
     * was hidden (no ImGui frames were built meanwhile). */
    static int last_frame = -10;
    if (ImGui::GetFrameCount() != last_frame + 1) {
        g_fps_frames = ppu_boot_frames_presented(); g_fps_t = 0.0;
        g_ft_last = g_fps_frames; g_ft_since = 0.0;
    }
    last_frame = ImGui::GetFrameCount();
    hud_sample(rsx_ui_frame_dt());
    g_fps_t += rsx_ui_frame_dt();
    if (g_fps_t >= 0.5) {
        const unsigned f = ppu_boot_frames_presented();
        g_fps = (double)(f - g_fps_frames) / g_fps_t;
        g_fps_frames = f; g_fps_t = 0.0;
    }
    if (g_start) { draw_start(); return; }
    if (port_settings_get()->fps_counter) draw_fps();
    if (rsx_ui_menu_open()) draw_menu();
}

/* The window's thread, under rsx_ui's lock: no rsx_ui_set_draw/_set_hotkey here. */
int hotkey(int key, void*)
{
    if (key == SDLK_F10 && !g_start) {
        /* detailed -> compact -> off -> detailed */
        static const int next[] = { 1, 2, 0 };
        set_fps_counter(next[port_settings_get()->fps_counter % 3]);
        static const char* const n[] = { "off", "detailed", "compact" };
        fprintf(stderr, "[ui] HUD %s\n", n[port_settings_get()->fps_counter % 3]);
        return 1;
    }
    return 0;
}

/* The disc's PIC1.PNG (PS3_GAME, beside USRDIR) for the start screen. From
 * the Apocalypse port's ui.cpp. */
void load_backdrop(const char* game_dir)
{
#ifdef PORT_HAVE_PNG
    const std::string path = std::string(game_dir && *game_dir ? game_dir : ".") + "/PS3_GAME/PIC1.PNG";
    png_image im;
    memset(&im, 0, sizeof im);
    im.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&im, path.c_str())) {
        fprintf(stderr, "[ui] no picture at %s: the start screen has a plain backdrop\n", path.c_str());
        return;
    }
    im.format = PNG_FORMAT_RGBA;
    std::vector<unsigned char> px(PNG_IMAGE_SIZE(im));
    if (png_image_finish_read(&im, nullptr, px.data(), 0, nullptr))
        rsx_ui_set_image(px.data(), (int)im.width, (int)im.height);
    png_image_free(&im);
#else
    (void)game_dir;
#endif
}

} /* namespace */

/* ui_internal.h: the helpers above for the pages in other files. */
namespace pui {
void save() { ::save(); }
void hint(const char* t) { ::hint(t); }
void warn(const char* t) { coloured(ImVec4(1.0f, 0.72f, 0.35f, 1.0f), t); }
void good(const char* t) { coloured(ImVec4(0.55f, 0.90f, 0.55f, 1.0f), t); }
void pin_begin(unsigned bit) { ::pin_begin(bit); }
void pin_end(unsigned bit) { ::pin_end(bit); }
void open_folder(const char* path) { ::open_folder(path); }
bool on_start_screen() { return g_start.load(); }
void restart_now(const char* extra_env)
{
    /* From the start screen: back to the start screen, so drop a
     * PS3_SKIP_START=1 an earlier restart left in the environment. */
    if (g_start && !extra_env) {
        unsetenv("PS3_SKIP_START");
        ::restart_now(nullptr);
    } else {
        ::restart_now(extra_env);
    }
}
const port_settings& boot_settings() { return g_boot; }
} /* namespace pui */

void port_ui_init(int argc, char** argv, int have_game)
{
    (void)argc; (void)argv;
    { const char* e = getenv("PS3RECOMP_RES_SCALE");
      const int n = e ? atoi(e) : 1;
      g_boot_scale = n >= 1 && n <= 4 ? n : 1; }
    { const char* e = getenv("PS3RECOMP_FPS");
      g_boot_fps = (e && atoi(e) == 60) ? 60 : 30; }
    g_boot = *port_settings_get();
    g_have_game = have_game != 0;
    /* The titles and the start screen's menu in the game's own menu font
     * (Franklin Gothic Demi Condensed), which the game unpacks beside its
     * disc folder (/dev_hdd1) on its first run; before that, a system font. */
    { const std::string f = std::string(port_paths_game(port_settings_get())) + "/framdcn.ttf";
      if (FILE* t = fopen(f.c_str(), "rb")) { fclose(t); setenv("PS3RECOMP_UI_TITLE_FONT", f.c_str(), 0); } }
    rsx_ui_set_draw(draw, nullptr);
    rsx_ui_set_hotkey(hotkey, nullptr);
    rsx_ui_set_visible(port_settings_get()->fps_counter);
    const char* skip = getenv("PS3_SKIP_START");
    g_want_start = !g_have_game || !((skip && skip[0] == '1') || port_settings_get()->skip_start);
    if (g_want_start) {
        g_go = 0;                   /* port_ui_backend_ready decides */
        if (!g_have_game) g_page = PAGE_GAME;
        /* PS3_UI_PAGE=settings|saves|game|help: open the start screen on that
         * page (for a screenshot of it, with PS3RECOMP_UI_SHOT). */
        if (const char* pg = getenv("PS3_UI_PAGE")) {
            if (!strcmp(pg, "settings")) g_page = PAGE_SETTINGS;
            else if (!strcmp(pg, "saves")) g_page = PAGE_SAVES;
            else if (!strcmp(pg, "game")) g_page = PAGE_GAME;
            else if (!strcmp(pg, "help")) g_page = PAGE_HELP;
        }
        load_backdrop(port_paths_game(port_settings_get()));
    }
}

void port_ui_backend_ready(void)
{
    if (!g_want_start) return;
    if (!rsx_ui_available()) {
        fprintf(stderr, "[ui] no UI: the game starts without its start screen\n");
        g_go = 1;
        return;
    }
#ifndef __APPLE__
    /* A pad moves through the start screen: start SDL's controller support
     * here, on the thread that pumps the window's events, before the game
     * runs (its cellPadInit then only takes another reference). */
    if (SDL_WasInit(SDL_INIT_GAMECONTROLLER) == 0) {
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");   /* as cellPad sets it */
        SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    }
#endif
    g_start = true;
    rsx_ui_set_modal(1);
    rsx_ui_set_visible(1);
    fprintf(stderr, "[ui] start screen: Play boots the game\n");
}

void port_ui_wait_start(void)
{
    while (!g_go.load()) {
        /* The window went away under the start screen: nothing can press Play. */
        if (g_start.load() && !rsx_ui_available()) {
            fprintf(stderr, "[ui] the start screen is gone: the game starts\n");
            g_start = false;
            g_go = 1;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

int port_ui_start_screen(void) { return g_start.load() ? 1 : 0; }
int port_ui_built(void) { return 1; }

#else

void port_ui_init(int, char**, int) {}
void port_ui_backend_ready(void) {}
void port_ui_wait_start(void) {}
int port_ui_start_screen(void) { return 0; }
int port_ui_built(void) { return 0; }

#endif
