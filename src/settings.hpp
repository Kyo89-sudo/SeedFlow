// SeedFlow - persistent application settings, per-torrent extras and statistics.
// Stored bencoded in %LOCALAPPDATA%\SeedFlow\State\settings.dat (with .bak copy).
// Per-torrent transfer state (paused, limits, trackers, file priorities,
// super-seeding, counters) lives in libtorrent's own .fastresume files.
#pragma once
#include "version.h"
#include <string>
#include <map>
#include <vector>
#include <cstdint>
#include <ctime>

namespace sf {

enum RatioMode { RatioGlobal = 0, RatioNone = 1, RatioCustom = 2 };
enum RatioAction { ActPause = 0, ActSuperSeed = 1, ActRemove = 2, ActRemoveDelete = 3 };
extern wchar_t const* const kRatioActionLabels[4];

struct TorrentMeta {
    int ratio_mode = RatioGlobal;
    double ratio_limit = 2.0;
    int ratio_action = -1;      // -1 = use global action
    bool ratio_done = false;    // action already executed for the current limit
    std::int64_t added = 0;     // unix time
};

struct GlobalStats {
    std::int64_t downloaded = 0, uploaded = 0, runtime = 0;
    std::int64_t peak_down = 0, peak_up = 0;
    int launches = 0, added = 0, completed = 0;
    std::int64_t since = 0;     // unix time of first launch / last reset
};

struct AppSettings {
    // general
    std::string download_dir;   // UTF-8
    bool close_to_tray = false;
    bool minimize_to_tray = false;
    bool trim_when_hidden = true;
    bool start_paused = false;
    bool confirm_remove = true;
    std::string user_agent = SF_USER_AGENT;
    std::string peer_id_prefix;     // empty = SeedFlow (-SFxyz0-, from the version)
    std::string client_name;        // handshake client version; empty = SF_CLIENT_NAME
    bool send_client_name = true;   // client name in the BitTorrent extension handshake
    bool anonymous_mode = false;    // libtorrent anonymous_mode: hides client identification
    bool start_minimized = true;    // "start with Windows" launches with --tray
    // torrent creation dialog (last choices)
    int create_format = 0;          // 0 hybrid v1+v2, 1 v1, 2 v2
    int create_piece = 0;           // 0 automatic, else index into piece_size_choices() + 1
    std::string create_trackers;    // UTF-8, one per line, blank line = new tier
    bool create_seed = true, create_private = false, create_copy_magnet = false;
    int process_priority = 0;       // 0 normal, 1 below normal, 2 idle (applied at start and on change)
    // network
    int port = 51413;
    bool dht = true, upnp = true, natpmp = true, lsd = true, pex = true, ipv6 = true;
    bool announce_all_trackers = true, announce_all_tiers = true; // qBittorrent "always announce to all ..."
    int encryption = 1;         // 0 allowed, 1 preferred, 2 required
    bool auto_port_check = false;
    // transfers
    int down_kib = 0, up_kib = 0;
    int connections = 200, upload_slots = 8;
    int active_downloads = 3, active_seeds = 5;
    bool preallocate = false;
    double global_ratio = 0.0;  // 0 = disabled
    int global_ratio_action = ActPause;
    // performance (disk_io: 0 default, 1 pread, 2 mmap, 3 posix - applied at start)
    int disk_io = 0;
    std::string perf_profile;
    std::string connection_profile;
    std::map<std::string, int> int_settings;   // libtorrent setting name -> value
    std::map<std::string, int> bool_settings;
    // window
    int win_x = -1, win_y = -1, win_w = 0, win_h = 0, win_max = 0;
    int split_left = 170, split_bottom = 230;
    std::string columns;        // saved list view column widths
    bool migrated = false;
    GlobalStats stats;
    std::map<std::string, TorrentMeta> torrents; // key: info-hash hex
};

bool load_settings(AppSettings& s);
bool save_settings(AppSettings const& s, std::string& error);
// One-time import from SeedFlow <= 0.7.1 (WPF): reads a few keys of session.json.
struct LegacyImport { bool found = false; std::string folder; std::vector<std::string> magnets, torrent_files; };
LegacyImport import_legacy(AppSettings& s);

} // namespace sf

