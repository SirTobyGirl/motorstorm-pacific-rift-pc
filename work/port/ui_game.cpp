/* MotorStorm: Pacific Rift port: the Game page of the start screen and the
 * in-game menu. It says where the dumped game, the firmware's dev_flash and
 * the saves are, checks each one, and has a folder picker to change them.
 *
 * The idea comes from the RACiT launcher's (Ratchet & Clank: A Crack in Time)
 * set-up / "your game" screen, which checks the game folder the player points
 * it at and accepts the disc folder, PS3_GAME or USRDIR alike. The layout
 * follows the Apocalypse port's pages.
 *
 * Everything runs on the window's thread inside an ImGui frame, except the
 * SHA-256 of EBOOT.elf (~19 MB), which runs on a thread of its own so the
 * page never stalls; its result is kept per path, size and modification time.
 * The UI font has Latin-1 only, so the text uses no other characters. */
#include "settings.h"
#include "ui_internal.h"

#ifdef PS3RECOMP_UI

#include "imgui.h"
#include "imgui_internal.h"   /* SetFocusID: the picker puts the cursor on its list */

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

/* The executable the port was lifted from: BCES-00129, 01.00 (scripts/lift.sh). */
const char* const k_eboot_sha256 = "bbb7fe2545e54847483439f59cda54e907db23a111d7afe9a1b655624886b287";
const char* const k_title_id = "BCES00129";

/* ---- SHA-256 (FIPS 180-4), small and self-contained ---------------------- */
struct sha256 {
    uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    uint8_t buf[64];
    size_t n = 0;
    uint64_t bytes = 0;

    static uint32_t ror(uint32_t x, int r) { return (x >> r) | (x << (32 - r)); }

    void block(const uint8_t* p)
    {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
        };
        uint32_t w[64];
        for (int i = 0; i < 16; i++)
            w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
        for (int i = 16; i < 64; i++) {
            const uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; i++) {
            const uint32_t t1 = hh + (ror(e, 6) ^ ror(e, 11) ^ ror(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            const uint32_t t2 = (ror(a, 2) ^ ror(a, 13) ^ ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    void update(const uint8_t* p, size_t len)
    {
        bytes += len;
        while (len) {
            const size_t take = std::min(len, sizeof buf - n);
            memcpy(buf + n, p, take);
            n += take; p += take; len -= take;
            if (n == sizeof buf) { block(buf); n = 0; }
        }
    }

    std::string hex()
    {
        const uint64_t bits = bytes * 8;
        const uint8_t pad = 0x80, zero = 0;
        update(&pad, 1);
        while (n != 56) update(&zero, 1);
        uint8_t len[8];
        for (int i = 0; i < 8; i++) len[i] = (uint8_t)(bits >> (56 - 8 * i));
        update(len, 8);
        char out[65];
        for (int i = 0; i < 8; i++) snprintf(out + i * 8, 9, "%08x", h[i]);
        return out;
    }
};

/* The file's SHA-256 in hex, or "" when it can't be read. */
std::string sha256_file(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    sha256 h;
    std::vector<char> chunk(1 << 20);
    while (f) {
        f.read(chunk.data(), (std::streamsize)chunk.size());
        if (f.gcount() > 0) h.update((const uint8_t*)chunk.data(), (size_t)f.gcount());
    }
    if (f.bad()) return "";
    return h.hex();
}

/* Hashing in the background: one file at a time, results kept by
 * path|size|mtime. The state is never freed, so a hash still running when
 * the process exits has nothing destroyed under it. */
struct hash_state {
    std::mutex mu;
    std::map<std::string, std::string> done;   /* key -> hex, or "error" */
    bool busy = false;
};
hash_state& hashes() { static hash_state* h = new hash_state; return *h; }

/* The hex hash, "error", or "" while it is being worked out. */
std::string hash_of(const std::string& path, uintmax_t size, long long mtime)
{
    const std::string key = path + '|' + std::to_string(size) + '|' + std::to_string(mtime);
    hash_state& hs = hashes();
    std::lock_guard<std::mutex> lock(hs.mu);
    auto it = hs.done.find(key);
    if (it != hs.done.end()) return it->second;
    if (!hs.busy) {
        hs.busy = true;
        std::thread([key, path] {
            std::string h = sha256_file(path);
            hash_state& st = hashes();
            std::lock_guard<std::mutex> l(st.mu);
            st.done[key] = h.empty() ? "error" : h;
            st.busy = false;
        }).detach();
    }
    return "";
}

/* ---- small file checks ----------------------------------------------------- */
bool is_dir(const fs::path& p) { std::error_code ec; return fs::is_directory(p, ec); }
bool is_file(const fs::path& p) { std::error_code ec; return fs::is_regular_file(p, ec); }

/* Whether the file starts with `magic` (n bytes). */
bool starts_with(const fs::path& p, const char* magic, size_t n)
{
    std::ifstream f(p, std::ios::binary);
    char b[8] = {};
    if (!f || n > sizeof b || !f.read(b, (std::streamsize)n)) return false;
    return memcmp(b, magic, n) == 0;
}

/* PARAM.SFO: "\0PSF", then version, key table, data table and entry count
 * (little-endian u32s), then 16-byte entries: key offset u16, format u16,
 * length u32, max length u32, data offset u32. Only the text fields are read. */
struct sfo_info { bool ok = false; std::string title, title_id; };

uint32_t le32(const std::vector<uint8_t>& d, size_t o) { return d[o] | d[o + 1] << 8 | d[o + 2] << 16 | (uint32_t)d[o + 3] << 24; }
uint16_t le16(const std::vector<uint8_t>& d, size_t o) { return (uint16_t)(d[o] | d[o + 1] << 8); }

sfo_info read_sfo(const fs::path& p)
{
    sfo_info r;
    std::ifstream f(p, std::ios::binary);
    if (!f) return r;
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (d.size() < 20 || d.size() > (1u << 20) || memcmp(d.data(), "\0PSF", 4) != 0) return r;
    const size_t keys = le32(d, 8), data = le32(d, 12), count = le32(d, 16);
    if (count > 256 || 20 + count * 16 > d.size()) return r;
    for (size_t i = 0; i < count; i++) {
        const size_t e = 20 + i * 16;
        const size_t ko = keys + le16(d, e), fmt = le16(d, e + 2), len = le32(d, e + 4), dof = data + le32(d, e + 12);
        if (ko >= d.size() || dof > d.size() || len > d.size() - dof) return r;
        const char* k = (const char*)&d[ko];
        const std::string key(k, strnlen(k, d.size() - ko));
        if (fmt != 0x0204 && fmt != 0x0004) continue;   /* utf-8 text */
        std::string v((const char*)&d[dof], len);
        v.resize(strnlen(v.c_str(), v.size()));
        if (key == "TITLE") r.title = v;
        else if (key == "TITLE_ID") r.title_id = v;
    }
    r.ok = true;
    return r;
}

/* Lexically clean, no trailing separator (except the root). */
std::string clean(const std::string& in)
{
    std::string p = in;
    if (p.size() > 1 && p[0] == '~' && p[1] == '/') { const char* h = getenv("HOME"); if (h) p = h + p.substr(1); }
    else if (p == "~") { const char* h = getenv("HOME"); if (h) p = h; }
    std::error_code ec;
    const fs::path abs = fs::absolute(fs::path(p), ec);   /* "./gamedata" is relative to the start folder */
    p = (ec ? fs::path(p) : abs).lexically_normal().string();
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    return p;
}

/* The folder holding PS3_GAME for a folder the player picked: the folder
 * itself, PS3_GAME or USRDIR inside a dump, or a folder with the dump one or
 * two levels down (a folder of dumps, say). "" when none. */
std::string find_game_root(const std::string& dir)
{
    const fs::path p(clean(dir));
    if (is_dir(p / "PS3_GAME")) return p.string();
    if (p.filename() == "PS3_GAME" && is_dir(p)) return p.parent_path().string();
    if (p.filename() == "USRDIR" && p.parent_path().filename() == "PS3_GAME") return p.parent_path().parent_path().string();
    std::vector<fs::path> level = { p };
    for (int depth = 0; depth < 2; depth++) {
        std::vector<fs::path> next;
        for (const fs::path& d : level) {
            std::error_code ec;
            fs::directory_iterator it(d, fs::directory_options::skip_permission_denied, ec), end;
            for (int n = 0; !ec && it != end && n < 300; it.increment(ec), n++) {
                std::error_code ec2;
                if (!it->is_directory(ec2) || it->path().filename().string()[0] == '.') continue;
                if (is_dir(it->path() / "PS3_GAME")) return it->path().string();
                next.push_back(it->path());
            }
        }
        std::sort(next.begin(), next.end());
        level.swap(next);
    }
    return "";
}

/* The firmware folder for a folder the player picked: itself, or a dev_flash
 * inside it (RPCS3's config folder, say). */
std::string find_dev_flash(const std::string& dir)
{
    const fs::path p(clean(dir));
    if (!is_dir(p / "sys") && is_dir(p / "dev_flash")) return (p / "dev_flash").string();
    return p.string();
}

/* ---- what each folder holds, re-checked at most once a second --------------- */
struct game_check {
    std::string path; double t = -10.0;
    bool ps3_game = false, bin = false, psarc_parts = false;
    int elf = 0;                 /* 0 missing, 1 an ELF, 2 something else (still encrypted?) */
    uintmax_t elf_size = 0; long long elf_mtime = 0;
    int sfo = 0;                 /* 0 missing, 1 read, 2 unreadable */
    sfo_info info;
    int psarc = 0;               /* 0 missing, 1 "PSAR", 2 something else */
};

void refresh(game_check& c, const std::string& path)
{
    const double now = ImGui::GetTime();
    if (path == c.path && now - c.t < 1.0) return;
    c = game_check(); c.path = path; c.t = now;
    if (path.empty()) return;
    const fs::path g = fs::path(path) / "PS3_GAME", usr = g / "USRDIR";
    c.ps3_game = is_dir(g);
    if (!c.ps3_game) return;
    const fs::path elf = usr / "EBOOT.elf";
    if (is_file(elf)) {
        c.elf = starts_with(elf, "\x7f" "ELF", 4) ? 1 : 2;
        std::error_code ec;
        c.elf_size = fs::file_size(elf, ec);
        c.elf_mtime = (long long)fs::last_write_time(elf, ec).time_since_epoch().count();
    }
    c.bin = is_file(usr / "EBOOT.BIN");
    if (is_file(g / "PARAM.SFO")) { c.info = read_sfo(g / "PARAM.SFO"); c.sfo = c.info.ok ? 1 : 2; }
    if (is_file(usr / "ms2.psarc")) c.psarc = starts_with(usr / "ms2.psarc", "PSAR", 4) ? 1 : 2;
    std::error_code ec;
    for (fs::directory_iterator it(usr, ec), end; !ec && it != end; it.increment(ec))
        if (it->path().filename().string().rfind("ms2.psarc.666", 0) == 0) { c.psarc_parts = true; break; }
}

/* Files the game opens from /dev_flash (docs/REQUIRED_FILES.md): a system
 * font and the firmware MP3 decoder MultiStream loads. */
const char* const k_flash_files[][2] = {
    { "data/font/SCE-PS3-RD-R-LATIN.TTF", "the system fonts" },
    { "sys/external/flashMP3.pic", "the firmware MP3 decoder the game's audio loads" },
};
struct flash_check { std::string path; double t = -10.0; bool dir = false; bool has[2] = {}; };

void refresh(flash_check& c, const std::string& path)
{
    const double now = ImGui::GetTime();
    if (path == c.path && now - c.t < 1.0) return;
    c = flash_check(); c.path = path; c.t = now;
    c.dir = !path.empty() && is_dir(path);
    for (int i = 0; i < 2 && c.dir; i++) c.has[i] = is_file(fs::path(path) / k_flash_files[i][0]);
}

struct saves_check { std::string path; double t = -10.0; bool dir = false; int saves = 0; };

void refresh(saves_check& c, const std::string& path, const std::string& savedata)
{
    const double now = ImGui::GetTime();
    if (path == c.path && now - c.t < 1.0) return;
    c = saves_check(); c.path = path; c.t = now;
    c.dir = !path.empty() && is_dir(path);
    std::error_code ec;
    for (fs::directory_iterator it(savedata, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code ec2;
        if (it->is_directory(ec2)) c.saves++;
    }
}

/* ---- the folder picker ----------------------------------------------------- */
enum { T_GAME, T_FLASH, T_SAVES };
const char* const k_target_name[] = { "the game (the folder holding PS3_GAME)", "the firmware's dev_flash",
                                      "the saves" };

struct picker_state {
    bool open_now = false;
    int target = T_GAME;
    std::string dir, listed, error;
    char edit[PORT_PATH_MAX] = {};
    std::vector<std::string> subdirs;
    bool unreadable = false, focus_list = false;
    std::vector<std::pair<std::string, std::string>> places;   /* label, path */
    std::string found;   /* the game or dev_flash folder `dir` would give */
} g_pick;

void pick_go(const std::string& dir)
{
    g_pick.dir = clean(dir);
    snprintf(g_pick.edit, sizeof g_pick.edit, "%s", g_pick.dir.c_str());
    g_pick.error.clear();
}

void pick_list()
{
    picker_state& p = g_pick;
    if (p.listed == p.dir) return;
    p.listed = p.dir;
    p.subdirs.clear();
    p.unreadable = false;
    std::error_code ec;
    fs::directory_iterator it(p.dir, ec), end;
    if (ec) p.unreadable = true;
    for (; !ec && it != end && p.subdirs.size() < 4000; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        std::error_code ec2;
        if (!name.empty() && name[0] != '.' && it->is_directory(ec2)) p.subdirs.push_back(name);
    }
    if (ec) p.unreadable = true;
    std::sort(p.subdirs.begin(), p.subdirs.end(), [](const std::string& a, const std::string& b) {
        const size_t n = std::min(a.size(), b.size());
        for (size_t i = 0; i < n; i++) {
            const int x = tolower((unsigned char)a[i]), y = tolower((unsigned char)b[i]);
            if (x != y) return x < y;
        }
        return a.size() < b.size();
    });
    p.found = p.target == T_GAME ? find_game_root(p.dir) : p.target == T_FLASH ? find_dev_flash(p.dir) : p.dir;
    p.focus_list = true;
}

/* Open the picker on `start` (or its nearest existing parent, else home). */
void pick_open(int target, const std::string& start, const std::string& in_use, const std::string& def)
{
    picker_state& p = g_pick;
    p.target = target;
    p.open_now = true;
    p.listed.clear();
    const char* home = getenv("HOME");
    fs::path d = clean(start.empty() ? (home ? home : "/") : start);
    while (!d.empty() && !is_dir(d) && d.has_parent_path() && d.parent_path() != d) d = d.parent_path();
    pick_go(is_dir(d) ? d.string() : (home ? home : "/"));
    p.places.clear();
    if (home) p.places.push_back({ "Home", home });
    if (!in_use.empty()) p.places.push_back({ "In use", in_use });
    if (!def.empty() && def != in_use) p.places.push_back({ "Default", def });
    /* Mounted drives: where a dump on a USB disk or a second drive shows up. */
    std::vector<std::string> roots;
    const char* user = getenv("USER");
#ifdef __APPLE__
    roots.push_back("/Volumes");
#else
    if (user) { roots.push_back(std::string("/media/") + user); roots.push_back(std::string("/run/media/") + user); }
    roots.push_back("/mnt");
#endif
    (void)user;
    for (const std::string& r : roots) {
        std::error_code ec;
        for (fs::directory_iterator it(r, ec), end; !ec && it != end && p.places.size() < 16; it.increment(ec)) {
            std::error_code ec2;
            if (it->is_directory(ec2)) p.places.push_back({ it->path().filename().string(), it->path().string() });
        }
    }
}

/* A button on the current line when it fits, else on the next. */
bool flow_button(const char* label, bool first)
{
    const ImGuiStyle& st = ImGui::GetStyle();
    const float w = ImGui::CalcTextSize(label, nullptr, true).x + st.FramePadding.x * 2.0f;
    if (!first) {
        const float right = ImGui::GetItemRectMax().x + st.ItemSpacing.x + w;
        if (right <= ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x) ImGui::SameLine();
    }
    return ImGui::Button(label);
}

char* field(port_settings& s, int target)
{
    return target == T_GAME ? s.game_dir : target == T_FLASH ? s.dev_flash : s.saves_dir;
}

void set_path(port_settings& s, int target, const std::string& path)
{
    snprintf(field(s, target), PORT_PATH_MAX, "%s", path.c_str());
    pui::save();
}

void draw_picker(port_settings& s)
{
    picker_state& p = g_pick;
    if (p.open_now) { ImGui::OpenPopup("Choose a folder##port_pick"); p.open_now = false; }
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float fs = ImGui::GetFontSize();
    ImGui::SetNextWindowSize(ImVec2(std::min(fs * 38.0f, ds.x * 0.94f), std::min(fs * 28.0f, ds.y * 0.92f)),
                             ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Choose a folder##port_pick", nullptr, ImGuiWindowFlags_NoSavedSettings)) return;
    pick_list();
    ImGui::TextDisabled("For %s", k_target_name[p.target]);

    /* The path, editable: Enter or Go opens it. */
    const float go_w = ImGui::CalcTextSize("Go").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(std::max(fs * 4.0f, ImGui::GetContentRegionAvail().x - go_w - ImGui::GetStyle().ItemSpacing.x));
    bool go = ImGui::InputText("##pick_path", p.edit, sizeof p.edit, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    go |= ImGui::Button("Go##pick_go");
    if (go) {
        if (is_dir(clean(p.edit))) pick_go(p.edit);
        else p.error = "Not a folder: " + std::string(p.edit);
    }
    for (size_t i = 0; i < p.places.size(); i++) {
        ImGui::PushID((int)i);
        if (flow_button(p.places[i].first.c_str(), i == 0)) {
            if (is_dir(p.places[i].second)) pick_go(p.places[i].second);
            else p.error = "Not there (yet): " + p.places[i].second;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", p.places[i].second.c_str());
        ImGui::PopID();
    }
    if (!p.error.empty()) pui::warn(p.error.c_str());

    /* The subfolders: one press (Cross/A, Enter, a click) opens one. */
    const float footer = ImGui::GetFrameHeightWithSpacing() * 2.0f + ImGui::GetTextLineHeightWithSpacing() * 2.0f;
    ImGui::BeginChild("##pick_list", ImVec2(0, std::max(fs * 3.0f, ImGui::GetContentRegionAvail().y - footer)),
                      ImGuiChildFlags_Borders);
    std::string enter;
    const fs::path cur(p.dir);
    if (cur.has_parent_path() && cur.parent_path() != cur) {
        if (ImGui::Selectable(".. (up)##pick_up", false, ImGuiSelectableFlags_NoAutoClosePopups))
            enter = cur.parent_path().string();
        if (p.focus_list) { ImGui::SetScrollY(0.0f); ImGui::SetFocusID(ImGui::GetItemID(), ImGui::GetCurrentWindow()); }
    }
    p.focus_list = false;
    if (p.unreadable) pui::warn("Can't read this folder.");
    else if (p.subdirs.empty()) pui::hint("No folders in here.");
    for (const std::string& d : p.subdirs) {
        ImGui::PushID(d.c_str());
        if (ImGui::Selectable(d.c_str(), false, ImGuiSelectableFlags_NoAutoClosePopups)) enter = (cur / d).string();
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (!enter.empty()) pick_go(enter);

    /* What "Use this folder" would set. */
    std::string use = p.dir;
    if (p.target == T_GAME) {
        if (p.found.empty()) pui::warn("No PS3_GAME here.");
        else { use = p.found; pui::good(p.found == p.dir ? "Game found here." : ("Game found in " + p.found).c_str()); }
    } else if (p.target == T_FLASH) {
        use = p.found;
        if (is_dir(fs::path(use) / "sys") && is_dir(fs::path(use) / "data"))
            pui::good(use == p.dir ? "Firmware found here." : ("Firmware found in " + use).c_str());
        else pui::warn("No dev_flash here (it has sys and data folders).");
    } else {
        ImGui::TextDisabled("The saves go in %s/dev_hdd0/...", p.dir.c_str());
    }
    const bool fits = use.size() < PORT_PATH_MAX;
    ImGui::BeginDisabled(!fits);
    if (ImGui::Button("Use this folder##pick_use")) { set_path(s, p.target, use); ImGui::CloseCurrentPopup(); }
    ImGui::EndDisabled();
    ImGui::SameLine();
    /* A modal does not close on Escape / Circle by itself; here it does,
     * unless the path field has them (they leave the field first). */
    const bool cancel_key = !ImGui::IsAnyItemActive() &&
                            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                            (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false));
    if (ImGui::Button("Cancel##pick_cancel") || cancel_key) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

/* ---- the page's rows ------------------------------------------------------- */
/* The path, wrapped, and its buttons; pinned rows greyed out. True when the
 * path in use is not the one this run started with. */
bool path_rows(port_settings& s, int target, const std::string& in_use, const std::string& at_boot, unsigned pin)
{
    port_settings d = s;
    field(d, target)[0] = 0;
    const char* dp = target == T_GAME ? port_paths_game(&d) : target == T_FLASH ? port_paths_dev_flash(&d)
                                                                               : port_paths_saves(&d);
    const std::string def = dp ? dp : "";
    ImGui::PushID(target);
    ImGui::PushTextWrapPos(0.0f);
    if (in_use.empty()) ImGui::TextDisabled("Not set");
    else ImGui::TextUnformatted(in_use.c_str());
    ImGui::PopTextWrapPos();
    if (pin) pui::pin_begin(pin);
    ImGui::BeginGroup();
    if (flow_button("Change...", true)) pick_open(target, in_use, in_use, def);
    ImGui::BeginDisabled(!field(s, target)[0]);
    if (flow_button("Use default", false)) { field(s, target)[0] = 0; pui::save(); }
    ImGui::EndDisabled();
    ImGui::EndGroup();
    if (pin) pui::pin_end(pin);
    ImGui::BeginDisabled(!is_dir(in_use));
    if (flow_button("Open folder", false)) pui::open_folder(in_use.c_str());
    ImGui::EndDisabled();
    if (flow_button("Copy path", false)) ImGui::SetClipboardText(in_use.c_str());
    ImGui::PopID();
    if (pin && (port_settings_pinned() & pin)) {
        char t[160];
        snprintf(t, sizeof t, "Set by %s (run.sh or the terminal).", port_settings_pin_var(pin));
        pui::hint(t);
    }
    const bool changed = in_use != at_boot;
    if (changed) pui::hint("Takes effect after a restart.");
    return changed;
}

void game_rows(const game_check& c)
{
    char t[PORT_PATH_MAX + 200];
    if (c.path.empty()) { pui::warn("Choose the folder of your dumped disc: the one holding PS3_GAME."); return; }
    if (!c.ps3_game) { pui::warn("No PS3_GAME folder here. Choose the folder of your dumped disc (the one holding PS3_GAME)."); return; }
    if (c.elf == 1) {
        const std::string h = hash_of((fs::path(c.path) / "PS3_GAME/USRDIR/EBOOT.elf").string(), c.elf_size, c.elf_mtime);
        if (h.empty()) pui::hint("EBOOT.elf: decrypted. Checking its checksum...");
        else if (h == k_eboot_sha256) pui::good("EBOOT.elf: decrypted, BCES-00129 version 01.00 (the one this port was made for).");
        else if (h == "error") pui::warn("EBOOT.elf could not be read.");
        else {
            snprintf(t, sizeof t, "EBOOT.elf is not the BCES-00129 01.00 executable (sha256 %.12s...). "
                                  "The port runs only the executable it was made for.", h.c_str());
            pui::warn(t);
        }
    } else if (c.elf == 2) {
        pui::warn("EBOOT.elf is not a decrypted ELF. Decrypt EBOOT.BIN with RPCS3 (Utilities > Decrypt PS3 "
                  "Binaries) and save the result as PS3_GAME/USRDIR/EBOOT.elf.");
    } else if (c.bin) {
        pui::warn("Only EBOOT.BIN here, which is encrypted. Decrypt it to EBOOT.elf with RPCS3 (Utilities > "
                  "Decrypt PS3 Binaries) and put it in PS3_GAME/USRDIR.");
    } else {
        pui::warn("No EBOOT.elf in PS3_GAME/USRDIR.");
    }
    if (c.sfo == 1) {
        snprintf(t, sizeof t, "PARAM.SFO: %s (%s)", c.info.title.empty() ? "no title" : c.info.title.c_str(),
                 c.info.title_id.empty() ? "no title id" : c.info.title_id.c_str());
        if (c.info.title_id == k_title_id) pui::good(t);
        else { pui::warn(t); pui::hint("This port is for the European release, BCES00129."); }
    } else {
        pui::warn(c.sfo == 2 ? "PS3_GAME/PARAM.SFO could not be read." : "No PS3_GAME/PARAM.SFO.");
    }
    if (c.psarc == 1) pui::good("ms2.psarc: the game's data.");
    else if (c.psarc == 2) pui::warn("ms2.psarc does not start with PSAR: a damaged copy, or its pieces joined out of order.");
    else if (c.psarc_parts)
        pui::warn("No ms2.psarc: this dump has it in 1 GB pieces (ms2.psarc.66600 ... .66604). The game opens one "
                  "file: join the pieces, in order, into PS3_GAME/USRDIR/ms2.psarc (docs/REQUIRED_FILES.md).");
    else pui::warn("No PS3_GAME/USRDIR/ms2.psarc (the game's data, about 4.8 GB).");
}

void flash_rows(const flash_check& c)
{
    if (!c.dir) {
        pui::warn("No folder here.");
    } else {
        bool all = true;
        for (int i = 0; i < 2; i++)
            if (!c.has[i]) {
                char t[256];
                snprintf(t, sizeof t, "Missing %s (%s).", k_flash_files[i][0], k_flash_files[i][1]);
                pui::warn(t);
                all = false;
            }
        if (all) pui::good("The firmware files the game reads are there (system fonts, the MP3 decoder).");
        if (all) return;
    }
    pui::hint("Install the PS3 firmware (PS3UPDAT.PUP from Sony) in RPCS3 (File > Install Firmware): it makes "
              "the dev_flash this needs. A dev_flash from another machine can simply be copied.");
}

void saves_rows(const saves_check& c, const std::string& savedata)
{
    if (!c.dir) pui::hint("Not made yet: the game makes it on its first save.");
    else if (c.saves == 0) pui::hint("No saves in it yet.");
    else {
        char t[64];
        snprintf(t, sizeof t, "%d save folder%s in it.", c.saves, c.saves == 1 ? "" : "s");
        pui::good(t);
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("The saves themselves: %s", savedata.c_str());
    ImGui::PopTextWrapPos();
    pui::hint("Changing this folder does not move the saves already made. The Saves page backs them up and restores them.");
}

/* port_paths_* as a string ("" for none). */
std::string str(const char* p) { return p ? p : ""; }

game_check g_game;
flash_check g_flash;
saves_check g_saves;

} /* namespace */

namespace pui {

void page_game(port_settings& s)
{
    /* Copies: port_paths_* return a buffer the next call overwrites. */
    const port_settings& b = boot_settings();
    const std::string game = str(port_paths_game(&s)), game0 = str(port_paths_game(&b));
    const std::string flash = str(port_paths_dev_flash(&s)), flash0 = str(port_paths_dev_flash(&b));
    const std::string saves = str(port_paths_saves(&s)), saves0 = str(port_paths_saves(&b));
    const std::string savedata = str(port_paths_savedata(&s));
    refresh(g_game, game);
    refresh(g_flash, flash);
    refresh(g_saves, saves, savedata);

    /* A restart applies the new folders: they are read once, at start. On
     * the start screen nothing has run yet, so it costs nothing. */
    if (game != game0 || flash != flash0 || saves != saves0) {
        if (on_start_screen()) {
            if (ImGui::Button("Restart now##game_restart")) restart_now();
            ImGui::SameLine();
            hint("Restart to use it.");
        } else {
            if (ImGui::Button("Restart now##game_restart")) restart_now();
            ImGui::SameLine();
            hint("Takes effect after a restart. Unsaved race progress is lost.");
        }
    }

    ImGui::SeparatorText("Game");
    path_rows(s, T_GAME, game, game0, 0);
    game_rows(g_game);

    ImGui::SeparatorText("Firmware (dev_flash)");
    path_rows(s, T_FLASH, flash, flash0, PORT_PIN_DEV_FLASH);
    flash_rows(g_flash);

    ImGui::SeparatorText("Saves folder");
    path_rows(s, T_SAVES, saves, saves0, PORT_PIN_SAVES);
    saves_rows(g_saves, savedata);

    draw_picker(s);
}

bool game_ready(const port_settings& s)
{
    static std::string path;
    static double t = -10.0;
    static bool ok = false;
    const char* g = port_paths_game(&s);
    const std::string elf = g && *g ? port_paths_eboot(g) : "";
    const double now = ImGui::GetTime();
    if (elf != path || now - t >= 1.0) {
        path = elf; t = now;
        ok = !elf.empty() && is_file(elf) && starts_with(elf, "\x7f" "ELF", 4);
    }
    return ok;
}

} /* namespace pui */

#endif /* PS3RECOMP_UI */
