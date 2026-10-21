#pragma once
#include "win.hpp"
#include "speed_history.hpp"
namespace sf {
void draw_speed_graph(HDC dc, RECT area, SpeedHistory const& history,
    std::uint64_t now, HFONT font, UINT dpi);
LRESULT CALLBACK speed_graph_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp);
}
