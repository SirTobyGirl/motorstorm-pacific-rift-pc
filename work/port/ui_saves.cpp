/* MotorStorm: Pacific Rift port: the Saves page and the save backup Play
 * makes. The idea comes from the RACiT launcher's Saves screen (Ratchet &
 * Clank: A Crack in Time: back up, restore, "each Play backs up your saves
 * first"); the layout follows the Apocalypse port's pages.
 *
 * The saves are the PS3 hard drive's savedata folder (port_paths_savedata):
 * one folder per save, each with PARAM.SFO, ICON0.PNG and the game's .DAT.
 * A backup is a plain copy of that folder's contents in
 * <user>/backups/<yyyymmdd-hhmmss>-<why>/, so the folder names sort by time
 * and a backup can also be put back by hand.
 *
 * backup_saves and last_backup_note are built with or without the UI, since
 * main.cpp may call them in a build that has none; the page itself is only in
 * a PS3RECOMP_UI build. Every file operation uses std::filesystem's
 * error_code overloads, so a bad folder becomes a line on the page rather
 * than an exception. */
#include "settings.h"
#include "ui_internal.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

/* One lock around every change to the saves or the backups: Play may back up
 * from its own thread while the page restores or deletes. Recursive because a
 * restore makes a backup of the current saves first. */
std::recursive_mutex g_op;
/* What the last backup did, for a line on the screen. */
std::mutex g_note_lock;
std::string g_note = "No backup made yet.";

void set_note(const std::string& n)
{
    std::lock_guard<std::mutex> l(g_note_lock);
    g_note = n;
}

std::tm local_time(std::time_t t)
{
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

[[maybe_unused]] std::string human_size(std::uintmax_t b)
{
    char buf[32];
    if (b < 1024) snprintf(buf, sizeof buf, "%u B", (unsigned)b);
    else if (b < 1024 * 1024) snprintf(buf, sizeof buf, "%.1f KB", b / 1024.0);
    else snprintf(buf, sizeof buf, "%.1f MB", b / (1024.0 * 1024.0));
    return buf;
}

/* A file's time as wall-clock time. C++17 has no clock_cast, so shift by the
 * difference between the two clocks' "now"; off by microseconds at most. */
[[maybe_unused]] std::time_t to_time_t(fs::file_time_type ft)
{
    using namespace std::chrono;
    const auto sys = time_point_cast<system_clock::duration>(
        ft - fs::file_time_type::clock::now() + system_clock::now());
    return system_clock::to_time_t(sys);
}

/* A backup folder's name starts with the time it was made: 8 digits, a dash,
 * 6 digits, a dash. Only such folders are listed or pruned, so anything else
 * the player keeps in backups/ is left alone. */
bool is_backup_name(const std::string& n)
{
    if (n.size() < 16 || n[8] != '-' || n[15] != '-') return false;
    for (int i = 0; i < 15; i++)
        if (i != 8 && (n[i] < '0' || n[i] > '9')) return false;
    return true;
}

std::string backups_root() { return (fs::path(port_user_dir()) / "backups").string(); }

/* The backup folders, oldest first. The name sorts by time to the second;
 * backups made within the same second are ordered by when their folder was
 * last written (when the copy into it finished), then by name. */
std::vector<std::string> backup_names(const std::string& root)
{
    struct item { std::string name; fs::file_time_type t; };
    std::vector<item> v;
    std::error_code ec;
    for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        const std::string n = it->path().filename().string();
        if (!it->is_directory(e2) || !is_backup_name(n)) continue;
        const fs::file_time_type t = it->last_write_time(e2);
        v.push_back({n, e2 ? fs::file_time_type::min() : t});
    }
    std::sort(v.begin(), v.end(), [](const item& a, const item& b) {
        const int c = a.name.compare(0, 15, b.name, 0, 15);
        if (c) return c < 0;
        if (a.t != b.t) return a.t < b.t;
        return a.name < b.name;
    });
    std::vector<std::string> names;
    for (auto& i : v) names.push_back(std::move(i.name));
    return names;
}

/* The reason in a backup's name: after the time, without the ".N" a second
 * backup in the same second gets (a tag never holds a dot). */
[[maybe_unused]] std::string backup_why(const std::string& n)
{
    if (n.size() <= 16) return "";
    const std::string w = n.substr(16);
    return w.substr(0, w.find('.'));
}

/* Copy src's contents into dst (created), keeping subfolders. */
bool copy_tree(const fs::path& src, const fs::path& dst, std::error_code& ec)
{
    fs::create_directories(dst, ec);
    if (ec) return false;
    fs::copy(src, dst, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    return !ec;
}

/* Whether a folder holds anything (an unreadable one counts as empty). */
bool has_entries(const fs::path& p)
{
    std::error_code ec;
    fs::directory_iterator it(p, ec);
    return !ec && it != fs::directory_iterator();
}

/* The backup itself. `keep` names a backup the pruning must not remove (the
 * one a restore is about to read); the one just made is never removed. */
bool do_backup(const char* why, const std::string& keep, std::string* made)
{
    std::lock_guard<std::recursive_mutex> l(g_op);
    const port_settings* s = port_settings_get();
    const fs::path src = port_paths_savedata(s);
    const int limit = s->keep_backups;
    if (!has_entries(src)) { set_note("No saves yet: nothing to back up."); return true; }

    char stamp[32];
    const std::tm tm = local_time(std::time(nullptr));
    strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tm);
    /* The tag goes into a folder name: keep it to letters, digits and dashes. */
    std::string tag = why && *why ? why : "manual";
    for (char& c : tag)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-')) c = '-';

    const fs::path root = backups_root();
    std::string name = std::string(stamp) + "-" + tag;
    std::error_code ec;
    /* Two backups in the same second (Back up now pressed twice) get a
     * number rather than mixing into one folder. */
    for (int n = 2; fs::exists(root / name, ec); n++)
        name = std::string(stamp) + "-" + tag + "." + std::to_string(n);

    if (!copy_tree(src, root / name, ec)) {
        std::error_code e2;
        fs::remove_all(root / name, e2);  /* no half backups in the list */
        set_note("Backup failed: " + ec.message());
        return false;
    }
    if (made) *made = name;

    int pruned = 0;
    if (limit > 0) {
        std::vector<std::string> v = backup_names(root.string());
        v.erase(std::remove_if(v.begin(), v.end(),
                               [&](const std::string& n) { return n == name || n == keep; }), v.end());
        /* The one just made counts towards the limit too. */
        const size_t others = limit > 1 ? (size_t)limit - 1 : 0;
        for (size_t i = 0; i + others < v.size(); i++) {
            std::error_code e2;
            if (fs::remove_all(root / v[i], e2) != (std::uintmax_t)-1 && !e2) pruned++;
        }
    }
    std::string note = "Backed up to backups/" + name;
    if (pruned) note += " (removed " + std::to_string(pruned) + (pruned == 1 ? " old backup)" : " old backups)");
    set_note(note);
    fprintf(stderr, "[saves] %s\n", note.c_str());
    return true;
}

/* Put a backup back: back up the current saves first, then swap the savedata
 * folder's contents for the backup's. The backup is copied next to savedata
 * before anything is removed, so a failed copy leaves the saves as they were. */
[[maybe_unused]] bool restore_backup(const std::string& name, std::string& msg)
{
    std::lock_guard<std::recursive_mutex> l(g_op);
    const fs::path from = fs::path(backups_root()) / name;
    std::error_code ec;
    if (!is_backup_name(name) || !fs::is_directory(from, ec)) { msg = "That backup is gone."; return false; }
    if (!do_backup("before-restore", name, nullptr)) {
        msg = "Not restored: the current saves could not be backed up first.";
        return false;
    }
    const fs::path dst = port_paths_savedata(port_settings_get());
    const fs::path stage = dst.parent_path() / "savedata.restoring";
    fs::remove_all(stage, ec);
    if (!copy_tree(from, stage, ec)) {
        std::error_code e2;
        fs::remove_all(stage, e2);
        msg = "Not restored: " + ec.message();
        return false;
    }
    fs::create_directories(dst, ec);
    for (fs::directory_iterator it(dst, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        fs::remove_all(it->path(), e2);
        if (e2) { msg = "Restore failed removing " + it->path().filename().string() + ": " + e2.message(); return false; }
    }
    if (ec) { msg = "Restore failed: " + ec.message(); return false; }
    for (fs::directory_iterator it(stage, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        fs::rename(it->path(), dst / it->path().filename(), e2);
        if (e2) { msg = "Restore failed moving " + it->path().filename().string() + ": " + e2.message(); return false; }
    }
    fs::remove_all(stage, ec);
    msg = "Restored backups/" + name + ". The previous saves are in a before-restore backup.";
    fprintf(stderr, "[saves] %s\n", msg.c_str());
    return true;
}

/* PARAM.SFO: "\0PSF", then version, key table, data table and entry count
 * (little-endian u32s), then 16-byte entries. Only the string keys the page
 * shows are read. */
struct sfo_text { std::string title, sub_title, detail; };

[[maybe_unused]] uint32_t rd32(const unsigned char* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
[[maybe_unused]] uint16_t rd16(const unsigned char* p) { return (uint16_t)(p[0] | p[1] << 8); }

[[maybe_unused]] bool read_sfo(const fs::path& path, sfo_text& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<unsigned char> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (d.size() < 20 || memcmp(d.data(), "\0PSF", 4) != 0) return false;
    const uint32_t keys = rd32(&d[8]), data = rd32(&d[12]), n = rd32(&d[16]);
    if (keys > d.size() || data > d.size() || n > 4096 || 20 + (size_t)n * 16 > d.size()) return false;
    for (uint32_t i = 0; i < n; i++) {
        const unsigned char* e = &d[20 + i * 16];
        const size_t ko = (size_t)keys + rd16(e), dl = rd32(e + 4), dof = (size_t)data + rd32(e + 12);
        if (ko >= d.size() || dof > d.size() || dl > d.size() - dof) continue;
        const char* k = (const char*)&d[ko];
        if (!memchr(k, 0, d.size() - ko)) continue;
        /* 0x0204 is a UTF-8 string with its terminator inside data_len. */
        if (rd16(e + 2) != 0x0204) continue;
        std::string v((const char*)&d[dof], strnlen((const char*)&d[dof], dl));
        if (!strcmp(k, "TITLE")) out.title = v;
        else if (!strcmp(k, "SUB_TITLE")) out.sub_title = v;
        else if (!strcmp(k, "DETAIL")) out.detail = v;
    }
    return true;
}

/* A save folder's (or a backup's) total size and newest file time. */
[[maybe_unused]] void tree_stats(const fs::path& p, std::uintmax_t& bytes, std::time_t& newest)
{
    bytes = 0; newest = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(p, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2)) continue;
        const std::uintmax_t sz = it->file_size(e2);
        if (!e2) bytes += sz;
        const auto t = it->last_write_time(e2);
        if (!e2) newest = std::max(newest, to_time_t(t));
    }
}

} /* namespace */

namespace pui {

bool backup_saves(const char* why) { return do_backup(why, std::string(), nullptr); }

const char* last_backup_note()
{
    /* A copy per thread, so the pointer stays valid while another thread
     * makes a backup and changes the note. */
    thread_local std::string copy;
    std::lock_guard<std::mutex> l(g_note_lock);
    copy = g_note;
    return copy.c_str();
}

} /* namespace pui */

#ifdef PS3RECOMP_UI

#include "imgui.h"

namespace {

struct save_row { std::string folder, title, sub; std::uintmax_t bytes; std::time_t mtime; };
struct backup_row { std::string name, why, when; std::uintmax_t bytes; int saves; };

/* The folders are read at most every two seconds (or at once after a change
 * made here), not every frame: the page is up while the game runs. */
struct scan_cache {
    double at = -1e9;
    std::string savedata;
    bool savedata_exists = false;
    std::vector<save_row> saves;
    std::vector<backup_row> backups;
    std::string error;
} g_scan;

std::string g_msg;       /* what the last restore or delete did */
bool g_msg_ok = true;
std::string g_pending;   /* the backup a confirm popup is about */

std::string fmt_time(std::time_t t)
{
    if (!t) return "-";
    char buf[32];
    const std::tm tm = local_time(t);
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tm);
    return buf;
}

void rescan(const port_settings& s)
{
    scan_cache c;
    c.at = ImGui::GetTime();
    c.savedata = port_paths_savedata(&s);
    std::error_code ec;
    c.savedata_exists = fs::is_directory(c.savedata, ec);
    if (c.savedata_exists)
    for (fs::directory_iterator it(c.savedata, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_directory(e2)) continue;
        save_row r{it->path().filename().string(), "", "", 0, 0};
        sfo_text t;
        if (read_sfo(it->path() / "PARAM.SFO", t)) { r.title = t.title; r.sub = t.sub_title; }
        if (r.title.empty()) r.title = r.folder;
        tree_stats(it->path(), r.bytes, r.mtime);
        c.saves.push_back(r);
    }
    if (ec) c.error = "Could not read the saves folder: " + ec.message();
    std::sort(c.saves.begin(), c.saves.end(), [](const save_row& a, const save_row& b) { return a.folder < b.folder; });

    const fs::path root = backups_root();
    std::vector<std::string> names = backup_names(root.string());
    for (auto n = names.rbegin(); n != names.rend(); ++n) {   /* newest first */
        backup_row r{*n, backup_why(*n), "", 0, 0};
        r.when = n->substr(0, 4) + "-" + n->substr(4, 2) + "-" + n->substr(6, 2) + " " +
                 n->substr(9, 2) + ":" + n->substr(11, 2) + ":" + n->substr(13, 2);
        std::time_t unused;
        tree_stats(root / *n, r.bytes, unused);
        for (fs::directory_iterator it(root / *n, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code e2;
            if (it->is_directory(e2)) r.saves++;
        }
        ec.clear();
        c.backups.push_back(r);
    }
    g_scan = std::move(c);
}

/* A confirm popup for the pending backup; true when the player said yes. */
bool confirm(const char* id, const char* text, const char* yes)
{
    bool ok = false;
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 30, 0));
    if (ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", text);
        ImGui::TextUnformatted(g_pending.c_str());
        if (ImGui::Button(yes)) { ok = true; ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    return ok;
}

} /* namespace */

namespace pui {

void page_saves(port_settings& s)
{
    const float em = ImGui::GetFontSize();
    if (ImGui::GetTime() - g_scan.at > 2.0 || g_scan.savedata != port_paths_savedata(&s)) rescan(s);
    const bool in_game = !on_start_screen();

    ImGui::SeparatorText("Saves");
    ImGui::TextWrapped("Folder: %s", g_scan.savedata.c_str());
    if (port_settings_pinned() & PORT_PIN_SAVES)
        hint("Set by PS3_GAMEDATA_ROOT for this run.");
    if (ImGui::Button("Open folder##saves")) open_folder(g_scan.savedata_exists ? g_scan.savedata.c_str() : port_paths_saves(&s));
    if (!g_scan.error.empty()) warn(g_scan.error.c_str());
    if (!g_scan.savedata_exists || g_scan.saves.empty()) {
        hint("No saves yet. The game makes one the first time it saves (after Choose Your Driver).");
    } else if (ImGui::BeginTable("##save_list", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Save", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Last written", ImGuiTableColumnFlags_WidthFixed, em * 9);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, em * 5);
        ImGui::TableHeadersRow();
        for (const save_row& r : g_scan.saves) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(r.title.c_str());
            if (!r.sub.empty()) { ImGui::SameLine(); hint(r.sub.c_str()); }
            ImGui::TableNextColumn(); ImGui::TextUnformatted(fmt_time(r.mtime).c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(human_size(r.bytes).c_str());
        }
        ImGui::EndTable();
    }
    if (in_game) hint("The game is running and may write its saves at any moment. A backup made now is of the saves as they are on disk.");

    ImGui::SeparatorText("Backups");
    if (ImGui::Button("Back up now")) { backup_saves("manual"); g_scan.at = -1e9; }
    ImGui::SameLine();
    if (ImGui::Button("Open backups folder")) {
        std::error_code ec;
        fs::create_directories(backups_root(), ec);
        open_folder(backups_root().c_str());
    }
    hint(last_backup_note());

    bool b = s.backup_on_play != 0;
    if (ImGui::Checkbox("Back up the saves each time Play is pressed", &b)) { s.backup_on_play = b; save(); }
    ImGui::SetNextItemWidth(em * 12);
    ImGui::SliderInt("Backups to keep", &s.keep_backups, 0, 30, s.keep_backups ? "%d" : "all");
    if (ImGui::IsItemDeactivatedAfterEdit()) save();
    hint("The oldest backups beyond this are removed after each new one; 0 keeps them all.");

    if (!g_msg.empty()) { if (g_msg_ok) good(g_msg.c_str()); else warn(g_msg.c_str()); }
    if (in_game) warn("Restore from the start screen (quit and start the game again).");

    bool open_restore = false, open_delete = false;
    if (g_scan.backups.empty()) {
        hint("No backups yet.");
    } else if (ImGui::BeginTable("##backup_list", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Made", ImGuiTableColumnFlags_WidthFixed, em * 10);
        ImGui::TableSetupColumn("Why", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Saves", ImGuiTableColumnFlags_WidthFixed, em * 3.5f);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, em * 5);
        ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, em * 9);
        ImGui::TableHeadersRow();
        for (const backup_row& r : g_scan.backups) {
            ImGui::PushID(r.name.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(r.when.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(r.why.c_str());
            ImGui::TableNextColumn(); ImGui::Text("%d", r.saves);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(human_size(r.bytes).c_str());
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(in_game);
            if (ImGui::SmallButton("Restore")) { g_pending = r.name; open_restore = true; }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete")) { g_pending = r.name; open_delete = true; }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    /* The popups are opened outside the rows' PushID, so the ID OpenPopup
     * makes is the one BeginPopupModal looks for. */
    if (open_restore) ImGui::OpenPopup("Restore backup?##saves");
    if (open_delete) ImGui::OpenPopup("Delete backup?##saves");
    if (confirm("Restore backup?##saves",
                "Replace the current saves with this backup? The current saves are backed up first.", "Restore")) {
        if (!on_start_screen()) { g_msg = "Restore from the start screen (quit and start the game again)."; g_msg_ok = false; }
        else g_msg_ok = restore_backup(g_pending, g_msg);
        g_scan.at = -1e9;
    }
    if (confirm("Delete backup?##saves", "Delete this backup for good?", "Delete")) {
        std::lock_guard<std::recursive_mutex> l(g_op);
        std::error_code ec;
        if (is_backup_name(g_pending)) fs::remove_all(fs::path(backups_root()) / g_pending, ec);
        g_msg_ok = !ec;
        g_msg = ec ? "Could not delete " + g_pending + ": " + ec.message() : "Deleted " + g_pending + ".";
        g_scan.at = -1e9;
    }
}

} /* namespace pui */

#endif /* PS3RECOMP_UI */
