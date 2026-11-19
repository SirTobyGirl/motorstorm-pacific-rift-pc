/* MotorStorm: Pacific Rift port. The player's settings: user/settings.ini (or
 * $PS3_USER_DIR/settings.ini), read at start by a windowed run and turned into
 * the environment variables the port already reads. A variable already set
 * wins over the file, so run.sh and the test scripts keep working as they did;
 * the menu greys such a row out (port_settings_pinned).
 * The start screen and the in-game menu (ui.cpp) change the values and save
 * them.
 *
 * Grown from the Apocalypse port's settings.h; the start screen, saves and
 * game-folder pages take their idea from the RACiT launcher (Ratchet & Clank:
 * A Crack in Time). */
#ifndef PORT_SETTINGS_H
#define PORT_SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

#define PORT_PATH_MAX 1024

typedef struct port_settings {
    /* [video] */
    int fullscreen;     /* 0 window, 1 full screen (PS3RECOMP_VK_FULLSCREEN) */
    int scale;          /* render scale: 1 (720p), 2 (1440p), 3 (4K) (PS3RECOMP_RES_SCALE);
                         * read at start only */
    int upscaler;       /* 0 off, 1 upscale to the window's size (PS3RECOMP_FSR): AMD FSR 1 on the
                         * Vulkan backend, MetalFX on the Metal one */
    float fsr_sharpness;/* the upscaler's sharpening, 0..1, 1 sharpest (PS3RECOMP_FSR_SHARPNESS) */
    int stretch;        /* 0 keep the 16:9 shape (black bars), 1 fill the window (PS3RECOMP_STRETCH);
                         * read at start only */
    int fps;            /* 30 (the game's own) or 60 (PS3RECOMP_FPS, port_patches.cpp); read at start only */
    int aniso;          /* anisotropic filtering of mip-mapped textures: 0 as the game asks, else 2, 4, 8 or 16 at least
                         * (PS3RECOMP_ANISO, Vulkan backend); read at start only */
    int detail;         /* distance detail: 0 as the game ships, 1 enhanced, 2 maximum (the game's own level-of-detail bias,
                         * cull thresholds and shadow distance, PS3RECOMP_LOD_OFFSET / CULL_SCALE / SHADOW_DIST; also turns
                         * on asynchronous recording); read at start only */
    /* [game] */
    int camera_shake;   /* 0 a steady camera (default), 1 the game's shake (PS3RECOMP_CAMERA_SHAKE) */
    int catch_up;       /* 60 fps mode: 0 every frame is 1/60 s (default), 1 a late frame makes up
                         * the lost time (PS3RECOMP_DT_MAX=2) */
    /* [performance] */
    int fps_counter;    /* the HUD drawn by the UI (F10 cycles): 0 off, 1 detailed (default), 2 compact */
    /* [launcher] */
    int skip_start;     /* 1: no start screen, the game boots at once (F1 still opens the menu) */
    int backup_on_play; /* 1: Play copies the saves to <user>/backups first (default) */
    int keep_backups;   /* how many save backups to keep (default 8; 0 = all) */
    /* [paths]: empty = the default (see port_paths_*) */
    char game_dir[PORT_PATH_MAX];   /* the folder holding PS3_GAME (the dumped disc) */
    char dev_flash[PORT_PATH_MAX];  /* the firmware's dev_flash folder */
    char saves_dir[PORT_PATH_MAX];  /* where the saves live: <saves_dir>/dev_hdd0/home/00000001/savedata
                                     * (PS3_GAMEDATA_ROOT) */
} port_settings;

/* The defaults: a window at 720p, no upscaler, the detailed HUD, the start
 * screen, a save backup on each Play. */
void port_settings_defaults(port_settings* s);

/* Read `path` (missing or unreadable: the defaults). Returns 1 when the file
 * was read. Unknown keys and bad values are ignored. */
int port_settings_load(port_settings* s, const char* path);

/* Write `path`, creating its directory. 0 on success. */
int port_settings_save(const port_settings* s, const char* path);

/* The fields an environment variable set before the start decides. */
enum {
    PORT_PIN_FULLSCREEN = 1 << 0, PORT_PIN_SCALE = 1 << 1, PORT_PIN_UPSCALER = 1 << 2,
    PORT_PIN_STRETCH = 1 << 3, PORT_PIN_FPS = 1 << 4, PORT_PIN_CAMERA_SHAKE = 1 << 5,
    PORT_PIN_CATCH_UP = 1 << 6, PORT_PIN_SKIP_START = 1 << 7, PORT_PIN_DEV_FLASH = 1 << 8,
    PORT_PIN_SAVES = 1 << 9, PORT_PIN_ANISO = 1 << 10, PORT_PIN_DETAIL = 1 << 11,
};

/* Before port_settings_export: note which fields the environment already
 * decides (port_settings_pinned), so the UI can grey them out and say why. */
unsigned port_settings_note_env(void);
unsigned port_settings_pinned(void);
/* The variable that pins `bit`, for the UI's explanation. */
const char* port_settings_pin_var(unsigned bit);

/* Each value as the environment variable the port reads, unless that
 * variable is already set. Also PS3_DEV_FLASH and PS3_GAMEDATA_ROOT from
 * port_paths_*. */
void port_settings_export(const port_settings* s);

/* The environment for starting the process again: this one without what
 * port_settings_export set (the new process reads the file again), plus
 * `extra` ("NAME=value", replacing that name) when not NULL. Built aside
 * rather than by unsetting: other threads read the environment, and a failed
 * exec keeps it whole. NULL-terminated, from malloc. */
char** port_settings_restart_env(const char* extra);

/* The folders in use, the setting's or the default:
 *   game:      [paths] game_dir, else the folder the command line's EBOOT.elf
 *              is in (<game>/PS3_GAME/USRDIR/EBOOT.elf), set by
 *              port_paths_set_cmdline_game
 *   dev_flash: $PS3_DEV_FLASH, else [paths] dev_flash, else the first of
 *              ./dev_flash, ~/.config/rpcs3/dev_flash,
 *              ~/Library/Application Support/rpcs3/dev_flash that exists
 *              ($PS3_DEV_FLASH_DEFAULT first when run.sh sets it)
 *   saves:     $PS3_GAMEDATA_ROOT, else [paths] saves_dir, else ./gamedata;
 *              the saves themselves are in port_paths_savedata(): one folder
 *              each (BLES00000, ...) with PARAM.SFO, ICON0.PNG, *.DAT
 * Returned strings stay valid until the next call of the same function. */
void port_paths_set_cmdline_game(const char* eboot_elf);
const char* port_paths_game(const port_settings* s);
const char* port_paths_dev_flash(const port_settings* s);
const char* port_paths_saves(const port_settings* s);
const char* port_paths_savedata(const port_settings* s);  /* <saves>/dev_hdd0/home/00000001/savedata */
/* <game>/PS3_GAME/USRDIR/EBOOT.elf for a game folder. */
const char* port_paths_eboot(const char* game_dir);

/* user/settings.ini, or $PS3_USER_DIR/settings.ini; port_user_dir() is the
 * folder ("user" or $PS3_USER_DIR). */
const char* port_settings_path(void);
const char* port_user_dir(void);

/* The process-wide settings, loaded once by main(). */
port_settings* port_settings_get(void);

#ifdef __cplusplus
}
#endif

#endif
