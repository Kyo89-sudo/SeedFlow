#include "speed_graph.hpp"
#include "app.hpp"
#include "paint.hpp"
#include "util.hpp"
#include <cmath>
#include <string>

namespace sf {
void draw_speed_graph(HDC dc, RECT area, SpeedHistory const& history,
    std::uint64_t now, HFONT font, UINT dpi) {
    int const saved = SaveDC(dc);
    if (!saved) return;
    auto px = [dpi](int n) { return MulDiv(n, static_cast<int>(dpi ? dpi : 96), 96); };
    FillRect(dc, &area, GetSysColorBrush(COLOR_WINDOW));
    SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    auto text = [&](RECT rect, std::wstring const& value, COLORREF color, UINT alignment) {
        SetTextColor(dc, color);
        DrawTextW(dc, value.c_str(), -1, &rect, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS | alignment);
    };
    SpeedSample latest{};
    if (history.size()) latest = history.at(history.size() - 1);
    if (latest.time > now || now - latest.time > 5000) latest.down = latest.up = 0;
    text({area.left + px(12), area.top + px(4), area.right - px(12), area.top + px(26)},
        L"↓ Download: " + fmt_speed0(latest.down) + L"     ↑ Upload: " + fmt_speed0(latest.up), paint::kText, DT_LEFT);
    RECT plot{area.left + px(88), area.top + px(38), area.right - px(16), area.bottom - px(32)};
    if (plot.right - plot.left < px(60) || plot.bottom - plot.top < px(25)) { RestoreDC(dc, saved); return; }
    std::int64_t peak = 0;
    for (std::size_t i = 0; i < history.size(); ++i) {
        auto const& value = history.at(i);
        if (value.time <= now && now - value.time <= SpeedHistory::window_ms)
            peak = std::max({peak, value.down, value.up});
    }
    double const top = std::max(1024.0, static_cast<double>(peak) * 1.15);
    SelectObject(dc, GetStockObject(DC_PEN));
    SetDCPenColor(dc, RGB(225, 230, 236));
    for (int tick = 0; tick <= 4; ++tick) {
        int const y = plot.bottom - MulDiv(plot.bottom - plot.top, tick, 4);
        MoveToEx(dc, plot.left, y, nullptr); LineTo(dc, plot.right, y);
        auto const rate = static_cast<std::int64_t>(std::min(9.0e18, top * tick / 4.0));
        text({area.left + px(4), y - px(10), plot.left - px(8), y + px(10)}, fmt_speed0(rate), paint::kGray, DT_RIGHT);
    }
    for (int tick = 0; tick <= 2; ++tick) {
        int const x = plot.left + MulDiv(plot.right - plot.left, tick, 2);
        MoveToEx(dc, x, plot.top, nullptr); LineTo(dc, x, plot.bottom);
        auto const label = tick == 0 ? L"-5 min" : tick == 1 ? L"-2,5 min" : L"Ora";
        text({x - px(35), plot.bottom + px(4), x + px(35), plot.bottom + px(25)}, label, paint::kGray, DT_CENTER);
    }
    int const clipped = SaveDC(dc);
    if (clipped) IntersectClipRect(dc, plot.left, plot.top, plot.right + 1, plot.bottom + 1);
    auto curve = [&](bool upload, COLORREF color, int style) {
        HPEN pen = CreatePen(style, 1, color);
        if (!pen) return;
        HGDIOBJ old = SelectObject(dc, pen);
        bool connected = false;
        std::uint64_t previous = 0;
        for (std::size_t i = 0; i < history.size(); ++i) {
            auto const& value = history.at(i);
            if (value.time > now || now - value.time > SpeedHistory::window_ms) { connected = false; continue; }
            double const relative = 1.0 - static_cast<double>(now - value.time) / SpeedHistory::window_ms;
            int const x = plot.left + static_cast<int>(std::lround(relative * (plot.right - plot.left)));
            auto const rate = upload ? value.up : value.down;
            int const y = plot.bottom - static_cast<int>(std::lround(rate / top * (plot.bottom - plot.top)));
            if (!connected || value.time - previous > 5000) MoveToEx(dc, x, y, nullptr);
            else LineTo(dc, x, y);
            SetPixelV(dc, x, y, color);
            previous = value.time; connected = true;
        }
        SelectObject(dc, old); DeleteObject(pen);
    };
    curve(false, paint::kBlue, PS_SOLID);
    curve(true, paint::kGreen, PS_DOT);
    if (clipped) RestoreDC(dc, clipped);
    text({plot.left, area.top + px(23), plot.right, area.top + px(37)},
        L"Download: blu continuo · Upload: verde tratteggiato · Totale di tutti i torrent", paint::kGray, DT_LEFT);
    if (!history.size()) text(plot, L"Raccolta dei dati di velocità…", paint::kGray, DT_CENTER);
    RestoreDC(dc, saved);
}

LRESULT CALLBACK speed_graph_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
    case WM_ERASEBKGND: return 1;
    case WM_SIZE: InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        RECT area{}; GetClientRect(hwnd, &area);
        if (area.right > 0 && area.bottom > 0) {
            HDC memory = CreateCompatibleDC(dc);
            HBITMAP bitmap = memory ? CreateCompatibleBitmap(dc, area.right, area.bottom) : nullptr;
            if (bitmap) {
                HGDIOBJ old = SelectObject(memory, bitmap);
                draw_speed_graph(memory, area, g.speed_history, GetTickCount64(), g.font, g.dpi);
                BitBlt(dc, 0, 0, area.right, area.bottom, memory, 0, 0, SRCCOPY);
                SelectObject(memory, old); DeleteObject(bitmap);
            } else draw_speed_graph(dc, area, g.speed_history, GetTickCount64(), g.font, g.dpi);
            if (memory) DeleteDC(memory);
        }
        EndPaint(hwnd, &paint); return 0;
    }
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}
} // namespace sf
