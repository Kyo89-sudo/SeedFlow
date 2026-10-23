// SeedFlow - "Grafico" tab: speed graph with range / source / series controls,
// summary tiles and a hover read-out. Everything is custom-drawn in one window.
#pragma once
#include "win.hpp"
#include "speed_history.hpp"
#include <string>

namespace sf {

inline constexpr int kGraphRangeCount = 4;
inline constexpr std::uint64_t kGraphRangeMs[kGraphRangeCount] = {60000, 300000, 1800000, 7200000};

struct GraphView {
    SpeedHistory const* samples = nullptr;   // the UsageHistory level that covers the window
    std::uint64_t now = 0;                   // same clock as the samples (ms)
    int range = 1;                           // index into kGraphRangeMs
    int source = 0;                          // 0 all torrents, 1 selected torrent
    bool show_down = true, show_up = true, show_limits = true;
    bool has_selection = true;               // false: source 1 without a selected torrent
    std::int64_t current_down = 0, current_up = 0;
    std::int64_t limit_down = 0, limit_up = 0;           // bytes/s, 0 = unlimited
    std::wstring tile3_label, tile3_value, tile3_sub;     // session or selected torrent
    int hover_x = -1;                        // client x of the mouse over the plot, -1 none
};

// Clickable areas of the last drawing (client coordinates).
struct GraphHit {
    RECT range[kGraphRangeCount]{};
    RECT source[2]{};
    RECT down{}, up{}, limits{};
    RECT plot{};
};

void draw_speed_graph(HDC dc, RECT area, GraphView const& view, HFONT font, HFONT big, HFONT bold, UINT dpi,
                      GraphHit* hit = nullptr);
LRESULT CALLBACK speed_graph_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp);
// Provided by the main window: the view for the current settings and selection, and
// the reaction to a click on a control (part 0 range, 1 source, 2 download, 3 upload, 4 limits).
GraphView current_graph_view();
void graph_option_clicked(int part, int index);

} // namespace sf
