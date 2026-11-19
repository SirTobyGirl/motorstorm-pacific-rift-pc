/* MotorStorm: Pacific Rift port: user/settings.ini (settings.h). */
#include "settings.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

static port_settings g_settings;
static unsigned g_pinned = 0;

void port_settings_defaults(port_settings* s)
{
    memset(s, 0, sizeof *s);
    s->fullscreen = 0;
    s->scale = 1;
    s->upscaler = 0;
    s->fsr_sharpness = 0.8f;
    s->stretch = 0;
    s->fps = 30;
    s->aniso = 0;
    s->detail = 0;
    s->camera_shake = 0;
    s->catch_up = 0;
    s->fps_counter = 1;
    s->skip_start = 0;
    s->backup_on_play = 1;
    s->keep_backups = 8;
}

static char* trim(char* t)
{
    while (*t == ' ' || *t == '\t') t++;
    char* e = t + strlen(t);
    while (e > t && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
    return t;
}

static int as_bool(const char* v, int def)
{
    if (!strcmp(v, "1") || !strcmp(v, "true") || !strcmp(v, "on") || !strcmp(v, "yes")) return 1;
    if (!strcmp(v, "0") || !strcmp(v, "false") || !strcmp(v, "off") || !strcmp(v, "no")) return 0;
    return def;
}

int port_settings_load(port_settings* s, const char* path)
{
    port_settings_defaults(s);
    FILE* f = fopen(path, "r");
    if (!f) return 0;
    char line[2048], section[64] = "";
    while (fgets(line, sizeof line, f)) {
        char* t = trim(line);
        if (!*t || *t == '#' || *t == ';') continue;
        if (*t == '[') {
            char* e = strchr(t, ']');
            if (e) { *e = 0; snprintf(section, sizeof section, "%s", t + 1); }
            continue;
        }
        char* eq = strchr(t, '=');
        if (!eq) continue;
        *eq = 0;
        const char* k = trim(t);
        /* [paths] values are taken whole: a folder name may hold ';' or '#'. */
        if (!strcmp(section, "paths")) {
            const char* v = trim(eq + 1);
            if (!strcmp(k, "game")) snprintf(s->game_dir, sizeof s->game_dir, "%s", v);
            else if (!strcmp(k, "dev_flash")) snprintf(s->dev_flash, sizeof s->dev_flash, "%s", v);
            else if (!strcmp(k, "saves")) snprintf(s->saves_dir, sizeof s->saves_dir, "%s", v);
            continue;
        }
        /* Elsewhere a value may carry a comment after it ("scale = 2 ; 1440p"). */
        if (char* c = strpbrk(eq + 1, ";#")) *c = 0;
        const char* v = trim(eq + 1);
        const long n = strtol(v, nullptr, 10);
        if (!strcmp(section, "video")) {
            if (!strcmp(k, "fullscreen")) s->fullscreen = as_bool(v, s->fullscreen);
            else if (!strcmp(k, "scale")) { if (n >= 1 && n <= 3) s->scale = (int)n; }
            else if (!strcmp(k, "upscaler")) {
                if (!strcmp(v, "fsr") || !strcmp(v, "fsr1") || !strcmp(v, "metalfx") || !strcmp(v, "on")) s->upscaler = 1;
                else if (!strcmp(v, "off") || !strcmp(v, "none")) s->upscaler = 0;
            } else if (!strcmp(k, "fsr_sharpness")) {
                char* end = nullptr;
                const float x = strtof(v, &end);
                if (end != v && x >= 0.0f && x <= 1.0f) s->fsr_sharpness = x;
            } else if (!strcmp(k, "stretch")) s->stretch = as_bool(v, s->stretch);
            else if (!strcmp(k, "fps")) { if (n == 30 || n == 60) s->fps = (int)n; }
            else if (!strcmp(k, "aniso")) { if (n == 0 || n == 2 || n == 4 || n == 8 || n == 16) s->aniso = (int)n; }
            else if (!strcmp(k, "detail")) { if (n >= 0 && n <= 2) s->detail = (int)n; }
        } else if (!strcmp(section, "game")) {
            if (!strcmp(k, "camera_shake")) s->camera_shake = as_bool(v, s->camera_shake);
            else if (!strcmp(k, "catch_up")) s->catch_up = as_bool(v, s->catch_up);
        } else if (!strcmp(section, "performance")) {
            if (!strcmp(k, "fps_counter")) {   /* 0 off, 1 detailed, 2 compact; on/off as 1/0 */
                char* end = nullptr;
                const long m = strtol(v, &end, 10);
                s->fps_counter = (end != v && m >= 0 && m <= 2) ? (int)m : as_bool(v, s->fps_counter);
            }
        } else if (!strcmp(section, "launcher")) {
            if (!strcmp(k, "skip_start")) s->skip_start = as_bool(v, s->skip_start);
            else if (!strcmp(k, "backup_on_play")) s->backup_on_play = as_bool(v, s->backup_on_play);
            else if (!strcmp(k, "keep_backups")) { if (n >= 0 && n <= 999) s->keep_backups = (int)n; }
        }
    }
    fclose(f);
    return 1;
}

static void make_dir_for(const char* path)
{
    std::string d = path;
    const size_t k = d.find_last_of('/');
    if (k == std::string::npos || k == 0) return;
    d.resize(k);
#ifdef _WIN32
    _mkdir(d.c_str());
#else
    mkdir(d.c_str(), 0755);
#endif
}

int port_settings_save(const port_settings* s, const char* path)
{
    make_dir_for(path);
    FILE* f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f,
            "# MotorStorm: Pacific Rift port settings. The start screen and the in-game\n"
            "# menu (F1) write this file. An environment variable set before the game\n"
            "# starts wins over a value here.\n"
            "\n[video]\n"
            "fullscreen = %d\n"
            "scale = %d          ; 1 = 720p, 2 = 1440p, 3 = 4K (takes effect after a restart)\n"
#ifdef __APPLE__
            "upscaler = %s  ; off, or metalfx (MetalFX: upscales the frame to the window)\n"
#else
            "upscaler = %s      ; off, or fsr (AMD FSR 1: upscales the frame to the window)\n"
#endif
            "fsr_sharpness = %.2f ; the upscaler's sharpening, 0..1, 1 = sharpest\n"
            "stretch = %d        ; 0 = keep the 16:9 shape, 1 = fill the window (after a restart)\n"
            "fps = %d            ; 30 (original) or 60 (takes effect after a restart)\n"
            "aniso = %d          ; anisotropic filtering: 0 = as the game asks, or 2, 4, 8, 16 (sharper textures at a slant; after a restart)\n"
            "detail = %d         ; distance detail: 0 = as the game ships, 1 = enhanced, 2 = maximum (costs CPU and render time; after a restart)\n"
            "\n[game]\n"
            "camera_shake = %d   ; the game's own camera shake (after a restart)\n"
            "catch_up = %d       ; 60 fps mode: a late frame makes up the lost time (after a restart)\n"
            "\n[performance]\n"
            "fps_counter = %d      ; HUD (F10 cycles): 0 = off, 1 = detailed, 2 = compact\n"
            "\n[launcher]\n"
            "skip_start = %d       ; 1 = no start screen, the game boots at once\n"
            "backup_on_play = %d   ; back up the saves each time Play is pressed\n"
            "keep_backups = %d     ; save backups kept (0 = all)\n"
            "\n[paths]\n"
            "; Empty = the default. Taken from the end of '=' to the end of the line.\n"
            "game = %s\n"
            "dev_flash = %s\n"
            "saves = %s\n",
            s->fullscreen ? 1 : 0, s->scale,
#ifdef __APPLE__
            s->upscaler ? "metalfx" : "off",
#else
            s->upscaler ? "fsr" : "off",
#endif
            (double)s->fsr_sharpness, s->stretch ? 1 : 0, s->fps == 60 ? 60 : 30, s->aniso, s->detail,
            s->camera_shake ? 1 : 0, s->catch_up ? 1 : 0, s->fps_counter,
            s->skip_start ? 1 : 0, s->backup_on_play ? 1 : 0, s->keep_backups,
            s->game_dir, s->dev_flash, s->saves_dir);
    return fclose(f) == 0 ? 0 : -1;
}

/* Which variable decides each pinnable field. */
static const struct { unsigned bit; const char* var; } k_pins[] = {
    { PORT_PIN_FULLSCREEN, "PS3RECOMP_VK_FULLSCREEN" }, { PORT_PIN_SCALE, "PS3RECOMP_RES_SCALE" },
    { PORT_PIN_UPSCALER, "PS3RECOMP_FSR" }, { PORT_PIN_STRETCH, "PS3RECOMP_STRETCH" },
    { PORT_PIN_FPS, "PS3RECOMP_FPS" }, { PORT_PIN_CAMERA_SHAKE, "PS3RECOMP_CAMERA_SHAKE" },
    { PORT_PIN_ANISO, "PS3RECOMP_ANISO" }, { PORT_PIN_DETAIL, "PS3RECOMP_LOD_OFFSET" },
    /* Not PS3_SKIP_START: Restart now sets it to go straight back into the
     * game, and the start screen's checkbox must stay usable after that. */
    { PORT_PIN_CATCH_UP, "PS3RECOMP_DT_MAX" },
    { PORT_PIN_DEV_FLASH, "PS3_DEV_FLASH" }, { PORT_PIN_SAVES, "PS3_GAMEDATA_ROOT" },
};

/* The player's own PS3_DEV_FLASH / PS3_GAMEDATA_ROOT, as they were before
 * port_settings_export set them from the file: the folder lookups below read
 * these, so a change on the Game files page shows at once and is not hidden
 * behind the value this run exported. */
static bool g_env_noted = false;
static std::string g_env_dev_flash, g_env_gamedata;

static const char* user_env(const char* name, const std::string& noted)
{
    if (!g_env_noted) return getenv(name);
    return noted.empty() ? nullptr : noted.c_str();
}

unsigned port_settings_note_env(void)
{
    { const char* e = getenv("PS3_DEV_FLASH"); g_env_dev_flash = e ? e : ""; }
    { const char* e = getenv("PS3_GAMEDATA_ROOT"); g_env_gamedata = e ? e : ""; }
    g_env_noted = true;
    g_pinned = 0;
    for (const auto& p : k_pins) {
        const char* e = getenv(p.var);
        if (e && *e) g_pinned |= p.bit;
    }
    return g_pinned;
}

unsigned port_settings_pinned(void) { return g_pinned; }

const char* port_settings_pin_var(unsigned bit)
{
    for (const auto& p : k_pins) if (p.bit == bit) return p.var;
    return "";
}

/* The variables port_settings_export set (not the caller): a restart drops
 * them, so the new process takes the file's values again. */
static std::string g_exported[24];
static int g_n_exported = 0;

static void export_var(const char* name, const char* value)
{
    if (getenv(name)) return;                      /* set by the caller: it wins */
    if (g_n_exported < 24) g_exported[g_n_exported++] = name;
#ifdef _WIN32
    _putenv_s(name, value);
#else
    setenv(name, value, 0);
#endif
}

void port_settings_export(const port_settings* s)
{
    char b[32];
    if (s->fullscreen) {
        export_var("PS3RECOMP_VK_FULLSCREEN", "1");
        export_var("PS3RECOMP_METAL_FULLSCREEN", "1");     /* the Metal backend's name */
    }
    if (s->scale > 1) { snprintf(b, sizeof b, "%d", s->scale); export_var("PS3RECOMP_RES_SCALE", b); }
    if (s->upscaler) export_var("PS3RECOMP_FSR", "1");
    if (s->stretch) export_var("PS3RECOMP_STRETCH", "1");
    if (s->fps == 60) export_var("PS3RECOMP_FPS", "60");
    if (s->aniso) { snprintf(b, sizeof b, "%d", s->aniso); export_var("PS3RECOMP_ANISO", b); }
    if (s->detail > 0) {
        /* The game's own level-of-detail bias, cull thresholds and shadow distance (port_vq_float, port_patches.cpp).
         * More objects to draw: asynchronous recording splits the render thread's work over two cores so 30 fps holds. */
        export_var("PS3RECOMP_LOD_OFFSET", s->detail >= 2 ? "-2" : "-1");
        export_var("PS3RECOMP_CULL_SCALE", s->detail >= 2 ? "0.3" : "0.5");
        export_var("PS3RECOMP_SHADOW_DIST", s->detail >= 2 ? "2" : "1.5");
        export_var("VK_ASYNC_RECORD", "1");
    }
    if (s->camera_shake) export_var("PS3RECOMP_CAMERA_SHAKE", "1");
    if (s->catch_up) export_var("PS3RECOMP_DT_MAX", "2");
    snprintf(b, sizeof b, "%.2f", (double)s->fsr_sharpness);
    export_var("PS3RECOMP_FSR_SHARPNESS", b);
    export_var("PS3_DEV_FLASH", port_paths_dev_flash(s));
    export_var("PS3_GAMEDATA_ROOT", port_paths_saves(s));
}

/* ---- folders ------------------------------------------------------------- */

static bool is_dir(const std::string& p)
{
    struct stat st;
    return !p.empty() && stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

static std::string g_cmdline_game;

void port_paths_set_cmdline_game(const char* eboot)
{
    /* <game>/PS3_GAME/USRDIR/EBOOT.elf -> <game> */
    std::string p = eboot ? eboot : "";
    for (char& c : p) if (c == '\\') c = '/';
    for (int i = 0; i < 3; i++) {
        const size_t k = p.find_last_of('/');
        if (k == std::string::npos) { p = "."; break; }
        p.resize(k);
    }
    g_cmdline_game = p.empty() ? "/" : p;
}

const char* port_paths_game(const port_settings* s)
{
    static std::string r;
    r = s && s->game_dir[0] ? s->game_dir : (g_cmdline_game.empty() ? "disc" : g_cmdline_game);
    return r.c_str();
}

const char* port_paths_eboot(const char* game_dir)
{
    static std::string r;
    r = std::string(game_dir && *game_dir ? game_dir : ".") + "/PS3_GAME/USRDIR/EBOOT.elf";
    return r.c_str();
}

const char* port_paths_dev_flash(const port_settings* s)
{
    static std::string r;
    const char* e = user_env("PS3_DEV_FLASH", g_env_dev_flash);
    if (e && *e) return r = e, r.c_str();
    if (s && s->dev_flash[0]) return r = s->dev_flash, r.c_str();
    const char* home = getenv("HOME");
    const std::string h = home ? home : "";
    const char* d = getenv("PS3_DEV_FLASH_DEFAULT");
    const std::string cands[] = {
        d && *d ? d : "", "dev_flash", h + "/.config/rpcs3/dev_flash",
        h + "/Library/Application Support/rpcs3/dev_flash",
    };
    for (const auto& c : cands) if (is_dir(c)) return r = c, r.c_str();
    return r = h + "/.config/rpcs3/dev_flash", r.c_str();
}

const char* port_paths_saves(const port_settings* s)
{
    static std::string r;
    const char* e = user_env("PS3_GAMEDATA_ROOT", g_env_gamedata);
    if (e && *e) return r = e, r.c_str();
    if (s && s->saves_dir[0]) return r = s->saves_dir, r.c_str();
    return r = "gamedata", r.c_str();
}

const char* port_paths_savedata(const port_settings* s)
{
    static std::string r;
    r = std::string(port_paths_saves(s)) + "/dev_hdd0/home/00000001/savedata";
    return r.c_str();
}

const char* port_user_dir(void)
{
    static std::string d;
    if (d.empty()) {
        const char* u = getenv("PS3_USER_DIR");
        d = u && *u ? u : "user";
    }
    return d.c_str();
}

const char* port_settings_path(void)
{
    static std::string p;
    if (p.empty()) p = std::string(port_user_dir()) + "/settings.ini";
    return p.c_str();
}

char** port_settings_restart_env(const char* extra)
{
#ifdef _WIN32
    (void)extra;
    return nullptr;
#else
    extern char** environ;
    int n = 0;
    while (environ[n]) n++;
    char** out = (char**)calloc((size_t)n + 2, sizeof(char*));
    if (!out) return nullptr;
    int m = 0;
    for (int i = 0; i < n; i++) {
        const char* e = environ[i];
        bool drop = false;
        for (int k = 0; k < g_n_exported && !drop; k++) {
            const std::string& v = g_exported[k];
            drop = !strncmp(e, v.c_str(), v.size()) && e[v.size()] == '=';
        }
        if (extra) {   /* the same name as `extra`: extra replaces it */
            const char* q = strchr(extra, '=');
            if (q && !strncmp(e, extra, (size_t)(q - extra) + 1)) drop = true;
        }
        if (!drop) out[m++] = strdup(e);
    }
    if (extra) out[m++] = strdup(extra);
    out[m] = nullptr;
    return out;
#endif
}

port_settings* port_settings_get(void) { return &g_settings; }
