// SeedFlow - shared application state for the Win32 GUI.
#pragma once
#include "win.hpp"
#include <uxtheme.h>
#include "engine.hpp"
#include "speed_history.hpp"
#include "settings.hpp"
#include "netcheck.hpp"
#include <string>
#include <vector>
#include <set>
#include <mutex>
#include <memory>
#include <utility>
#include <map>

namespace sf {

constexpr UINT WM_APP_TRAY = WM_APP + 1;
constexpr UINT WM_APP_PORTCHECK = WM_APP + 2;   // notification only; result owned by PortCheckJob
constexpr UINT WM_APP_FITCOLUMNS = WM_APP + 3;  // refit the Nome column after a header resize
constexpr UINT_PTR TIMER_REFRESH = 1;
constexpr UINT_PTR TIMER_ANIM = 2;          // progress bar animation (verifying / metadata / moving)

enum Column {
    ColName, ColQueue, ColSize, ColProgress, ColState, ColDown, ColUp, ColEta,
    ColSeeds, ColPeers, ColRatio, ColTarget, ColDownloaded, ColUploaded, ColAdded, ColHealth, ColCount
};

struct Row {
    TorrentView v;
    double ratio = 0;
    double target = -1;      // -1 none
    int state_icon = 0;
    COLORREF color = 0;
    std::wstring state;
    int number = 0;          // # column: queue position + 1, then seeds/finished numbered after the queue
};

// The worker owns its mailbox until completion. Window messages never own a pointer.
struct PortCheckJob {
    std::mutex mutex;
    PortCheck result;
    bool ready = false;
    void complete(PortCheck value) {
        std::lock_guard<std::mutex> lock(mutex);
        result = std::move(value);
        ready = true;
    }
    bool take(PortCheck& value) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!ready) return false;
        value = std::move(result);
        ready = false;
        return true;
    }
};

struct App {
    HINSTANCE inst = nullptr;
    HWND main = nullptr, toolbar = nullptr, filters = nullptr, list = nullptr, tabs = nullptr, status = nullptr;
    HWND chart = nullptr;
    UsageHistory usage;                          // all torrents
    std::map<int, UsageHistory> torrent_usage;   // per torrent, created when it moves data
    std::int64_t session_down = 0, session_up = 0;   // bytes since SeedFlow started
    std::uint64_t last_sample_ms = 0;
    int graph_hover_x = -1;
    std::int64_t current_down = 0, current_up = 0;
    HWND general = nullptr, tracker_bar = nullptr, trackers = nullptr, peers = nullptr, files = nullptr, log = nullptr;
    HFONT font = nullptr, bold = nullptr, mono = nullptr, big = nullptr;
    HIMAGELIST il_main_bar = nullptr;   // main toolbar: icon-only, 20 px at 96 DPI
    HIMAGELIST il_toolbar = nullptr, il_states = nullptr, il_filters = nullptr, il_small = nullptr;
    HICON app_icon = nullptr, app_icon_small = nullptr, net_icon = nullptr;
    HTHEME list_theme = nullptr;    // cached for the custom-drawn cells; reset on WM_THEMECHANGED
    HACCEL accel = nullptr;
    UINT dpi = 96;
    UINT taskbar_created = 0;

    Engine engine;
    AppSettings settings;
    PortCheck port_check;
    bool port_check_running = false;
    std::shared_ptr<PortCheckJob> port_check_job;

    std::vector<Row> rows;          // all torrents (latest snapshot)
    std::vector<int> visible;       // indices into rows, filtered + sorted
    int filter = 0;
    int sort_col = ColQueue;
    bool sort_desc = false;
    int split_left = 170, split_bottom = 230;
    bool tray_visible = false;
    bool hidden = false;
    bool exiting = false;
    std::int64_t last_recv = -1, last_sent = -1;
    unsigned ticks = 0;
    unsigned anim_phase = 0;
    bool anim_running = false;
    std::set<int> was_downloading;
    std::wstring net_text, net_tip;
    NetState net_state = NetState::Starting;
};
extern App g;

int scale(int px);                              // DPI scaling
std::wstring tr_status(int status);
void log_line(std::wstring const& text);
void persist();
void apply_session_settings();                  // pushes settings into the engine
void refresh_now();
std::vector<int> selected_ids();
Row const* row_by_id(int id);
std::string download_dir();
bool ratio_target(TorrentMeta const& m, double& limit, int& action);
TorrentMeta& meta_for(std::string const& hash);
void start_port_check(HWND notify);

// dialogs.cpp
bool dlg_magnet(HWND owner, std::vector<std::string>& magnets, std::string& folder, bool& paused);
enum PrefPage { PrefGeneral, PrefFolders, PrefConnection, PrefBandwidth, PrefBitTorrent, PrefQueue, PrefIdentity, PrefAdvanced, PrefPerformance };
bool dlg_preferences(HWND owner, int page = PrefGeneral);
void dlg_guide(HWND owner, bool keys = false);   // Aiuto › Guida rapida / Scorciatoie
void report_problem(HWND owner);                 // copies system info, opens a GitHub issue
extern wchar_t const* const kProjectUrl;
extern wchar_t const* const kReleasesUrl;   // true if anything was applied
void dlg_create_torrent(HWND owner);
// "Aggiungi torrent": save folder (with recent folders), paused, "don't ask again". false = cancelled.
bool dlg_add_torrent(HWND owner, std::wstring const& summary, std::string& folder, bool& paused, bool& dont_ask);
void remember_dir(std::string const& folder_utf8);   // most recent first, max 8 (settings.recent_dirs)
std::wstring about_version_line();     // "Versione x.y.z (Win32 nativa) — x64"
// mainwindow.cpp: adds a just-created .torrent for seeding (seed mode, no recheck); id or -1
int add_created_torrent(std::wstring const& torrent_file, std::wstring const& save_dir, std::string& error);
bool dlg_wizard(HWND owner);
void dlg_diagnostics(HWND owner);
void dlg_stats(HWND owner);
void dlg_about(HWND owner);
bool dlg_ratio(HWND owner, std::vector<int> const& ids);
bool dlg_limits(HWND owner, int& down_kib, int& up_kib);
bool dlg_trackers(HWND owner, std::wstring& text);
bool pick_folder(HWND owner, std::wstring& folder);

} // namespace sf
