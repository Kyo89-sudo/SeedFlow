#include "speed_graph.hpp"
#include "app.hpp"
#include "paint.hpp"
#include "util.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace sf {
namespace {

COLORREF const kGrid = RGB(0xE1, 0xE6, 0xEC), kGridV = RGB(0xEE, 0xF1, 0xF4), kBorder = RGB(0xC9, 0xD1, 0xDA),
               kTileBorder = RGB(0xE1, 0xE6, 0xEC), kSegBorder = RGB(0xB9, 0xC2, 0xCC);

// Pixels of a 32-bit top-down DIB (0x00RRGGBB), blended directly: the surface is opaque.
struct Pixels {
    std::uint32_t* bits = nullptr;
    int w = 0, h = 0;
    void blend(int x, int y, COLORREF c, double a) {
        if (a <= 0 || x < 0 || y < 0 || x >= w || y >= h) return;
        a = std::min(a, 1.0);
        std::uint32_t& p = bits[static_cast<std::size_t>(y) * w + x];
        auto ch = [&](int shift, int v) {
            int const old = static_cast<int>((p >> shift) & 0xFF);
            return static_cast<std::uint32_t>(std::lround(old + (v - old) * a)) & 0xFF;
        };
        p = (ch(16, GetRValue(c)) << 16) | (ch(8, GetGValue(c)) << 8) | ch(0, GetBValue(c));
    }
};

double sd_round(double px, double py, double x0, double y0, double x1, double y1, double r) {
    double const cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, hx = (x1 - x0) / 2 - r, hy = (y1 - y0) / 2 - r;
    double const qx = std::abs(px - cx) - hx, qy = std::abs(py - cy) - hy;
    return std::hypot(std::max(qx, 0.0), std::max(qy, 0.0)) + std::min(std::max(qx, qy), 0.0) - r;
}

// Anti-aliased rounded rectangle: optional fill, 1 px border.
void round_rect(Pixels& px, RECT r, double radius, COLORREF fill, bool filled, COLORREF border, double stroke) {
    for (int y = std::max<LONG>(0, r.top); y < std::min<LONG>(px.h, r.bottom); ++y)
        for (int x = std::max<LONG>(0, r.left); x < std::min<LONG>(px.w, r.right); ++x) {
            double const d = sd_round(x + 0.5, y + 0.5, r.left, r.top, r.right, r.bottom, radius);
            double const cov = std::clamp(0.5 - d, 0.0, 1.0);
            if (cov <= 0) continue;
            if (filled) px.blend(x, y, fill, cov);
            double const ring = std::clamp(0.5 - std::abs(d + stroke / 2) + stroke / 2, 0.0, 1.0);
            px.blend(x, y, border, ring);
        }
}

// Thick anti-aliased segment (distance field over its bounding box).
void line(Pixels& px, double x0, double y0, double x1, double y1, COLORREF c, double width, RECT clip) {
    double const hw = width / 2;
    int const left = std::max<int>(clip.left, static_cast<int>(std::floor(std::min(x0, x1) - hw - 1)));
    int const right = std::min<int>(clip.right, static_cast<int>(std::ceil(std::max(x0, x1) + hw + 1)));
    int const top = std::max<int>(clip.top, static_cast<int>(std::floor(std::min(y0, y1) - hw - 1)));
    int const bottom = std::min<int>(clip.bottom, static_cast<int>(std::ceil(std::max(y0, y1) + hw + 1)));
    double const dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
    for (int y = top; y < bottom; ++y)
        for (int x = left; x < right; ++x) {
            double const pxc = x + 0.5, pyc = y + 0.5;
            double t = len2 > 0 ? ((pxc - x0) * dx + (pyc - y0) * dy) / len2 : 0;
            t = std::clamp(t, 0.0, 1.0);
            double const d = std::hypot(pxc - (x0 + t * dx), pyc - (y0 + t * dy));
            double const cov = std::clamp(hw + 0.5 - d, 0.0, 1.0);
            if (cov > 0) px.blend(x, y, c, cov);
        }
}

void disc(Pixels& px, double cx, double cy, double r, COLORREF fill, COLORREF border, double stroke) {
    RECT b{static_cast<LONG>(cx - r - 2), static_cast<LONG>(cy - r - 2), static_cast<LONG>(cx + r + 2), static_cast<LONG>(cy + r + 2)};
    for (int y = std::max<LONG>(0, b.top); y < std::min<LONG>(px.h, b.bottom); ++y)
        for (int x = std::max<LONG>(0, b.left); x < std::min<LONG>(px.w, b.right); ++x) {
            double const d = std::hypot(x + 0.5 - cx, y + 0.5 - cy) - r;
            px.blend(x, y, fill, std::clamp(0.5 - d, 0.0, 1.0));
            px.blend(x, y, border, std::clamp(0.5 - std::abs(d + stroke / 2) + stroke / 2, 0.0, 1.0));
        }
}

// Smallest "round" scale top (4 grid steps) above the peak: 1-2-2.5-5 x 10^n in B, KiB, MiB, GiB.
std::int64_t nice_top(double peak) {
    double const want = std::max(peak * 1.08, 4.0 * 1024);
    static double const units[] = {1.0, 1024.0, 1048576.0, 1073741824.0};
    static double const mult[] = {1, 2, 2.5, 5, 10, 20, 25, 50, 100, 200, 250, 500};
    for (double u : units)
        for (double m : mult)
            if (u * m * 4 >= want) return static_cast<std::int64_t>(u * m * 4);
    return static_cast<std::int64_t>(std::min(9.0e18, want));
}

std::wstring ago_label(std::uint64_t ms, int range_index) {
    static wchar_t const* const full[] = {L"-1 min", L"-5 min", L"-30 min", L"-2 ore"};
    if (ms == 0) return L"ora";
    if (ms >= kGraphRangeMs[range_index]) return full[range_index];
    std::uint64_t const s = ms / 1000;
    wchar_t buf[32];
    if (s >= 3600) swprintf(buf, 32, L"-%llu h %02llu", s / 3600, (s % 3600) / 60);
    else swprintf(buf, 32, L"-%llu:%02llu", s / 60, s % 60);
    return buf;
}

std::wstring clock_ago(std::uint64_t ms) {
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER t; t.LowPart = ft.dwLowDateTime; t.HighPart = ft.dwHighDateTime;
    t.QuadPart -= ms * 10000ULL;
    ft.dwLowDateTime = t.LowPart; ft.dwHighDateTime = t.HighPart;
    FILETIME local; SYSTEMTIME st{};
    if (!FileTimeToLocalFileTime(&ft, &local) || !FileTimeToSystemTime(&local, &st)) return {};
    wchar_t buf[16];
    swprintf(buf, 16, L"%02u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
    return buf;
}

struct Point { double x, y; };
using Run = std::vector<Point>;

} // namespace

void draw_speed_graph(HDC target, RECT area, GraphView const& v, HFONT font, HFONT big, HFONT bold, UINT dpi, GraphHit* hit) {
    int const w = area.right - area.left, h = area.bottom - area.top;
    if (w <= 0 || h <= 0) return;
    auto s = [dpi](double n) { return static_cast<int>(std::lround(n * (dpi ? dpi : 96) / 96.0)); };
    int const range = std::clamp(v.range, 0, kGraphRangeCount - 1);
    std::uint64_t const window = kGraphRangeMs[range];

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bmp = dc ? CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
    if (!dc || !bmp || !bits) { if (bmp) DeleteObject(bmp); if (dc) DeleteDC(dc); return; }
    HGDIOBJ old_bmp = SelectObject(dc, bmp);
    HGDIOBJ old_font = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    Pixels px{static_cast<std::uint32_t*>(bits), w, h};
    {   // background
        COLORREF const bg = GetSysColor(COLOR_WINDOW);
        std::uint32_t const value = (static_cast<std::uint32_t>(GetRValue(bg)) << 16) | (GetGValue(bg) << 8) | GetBValue(bg);
        std::fill(px.bits, px.bits + static_cast<std::size_t>(w) * h, value);
    }
    struct Text { RECT r; std::wstring s; COLORREF c; UINT fmt; HFONT f; };
    std::vector<Text> texts;
    auto text = [&](RECT r, std::wstring str, COLORREF c, UINT fmt, HFONT f = nullptr) {
        texts.push_back({r, std::move(str), c, fmt | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS, f ? f : font});
    };
    auto measure = [&](std::wstring const& str, HFONT f = nullptr) {
        HGDIOBJ o = SelectObject(dc, f ? f : font);
        SIZE sz{}; GetTextExtentPoint32W(dc, str.c_str(), static_cast<int>(str.size()), &sz);
        SelectObject(dc, o);
        return static_cast<int>(sz.cx);
    };
    GraphHit hits{};

    // ---- top bar: range, source, series, limits
    int const bar_top = s(8), bar_h = s(24);
    int x = s(12);
    auto label = [&](wchar_t const* str) {
        int const tw = measure(str);
        text({x, bar_top, x + tw + 2, bar_top + bar_h}, str, paint::kText, DT_LEFT);
        x += tw + s(8);
    };
    auto segmented = [&](std::vector<std::wstring> const& items, int selected, RECT* out) {
        int const pad = s(10);
        int total = 0;
        std::vector<int> widths;
        for (auto const& it : items) { widths.push_back(measure(it) + 2 * pad); total += widths.back(); }
        RECT const box{x, bar_top + s(1), x + total, bar_top + bar_h - s(1)};
        double const radius = s(4);
        round_rect(px, box, radius, RGB(255, 255, 255), true, kSegBorder, 1);
        int cx = x;
        for (std::size_t i = 0; i < items.size(); ++i) {
            RECT seg{cx, box.top, cx + widths[i], box.bottom};
            out[i] = {area.left + seg.left, area.top + seg.top, area.left + seg.right, area.top + seg.bottom};
            if (static_cast<int>(i) == selected) {
                // selected segment: blue fill, clipped to the rounded outline at the ends
                for (int yy = seg.top; yy < seg.bottom; ++yy)
                    for (int xx = seg.left; xx < seg.right; ++xx) {
                        double const d = sd_round(xx + 0.5, yy + 0.5, box.left, box.top, box.right, box.bottom, radius);
                        px.blend(xx, yy, paint::kBlue, std::clamp(0.5 - d, 0.0, 1.0));
                    }
            } else if (i > 0 && static_cast<int>(i) != selected + 1) {
                for (int yy = seg.top + s(4); yy < seg.bottom - s(4); ++yy) px.blend(seg.left, yy, kSegBorder, 1);
            }
            text(seg, items[i], static_cast<int>(i) == selected ? RGB(255, 255, 255) : paint::kText, DT_CENTER);
            cx += widths[i];
        }
        x = box.right + s(16);
    };
    auto checkbox = [&](wchar_t const* str, bool on, COLORREF swatch, bool has_swatch, RECT& out) {
        int const box = s(13);
        int const top = bar_top + (bar_h - box) / 2;
        int const start = x;
        RECT const b{x, top, x + box, top + box};
        round_rect(px, b, s(2), on ? paint::kBlue : RGB(255, 255, 255), true, on ? paint::kBlue : RGB(0x76, 0x80, 0x8A), 1);
        if (on) {
            line(px, b.left + box * 0.24, b.top + box * 0.52, b.left + box * 0.43, b.top + box * 0.71, RGB(255, 255, 255), s(1.6), {0, 0, w, h});
            line(px, b.left + box * 0.43, b.top + box * 0.71, b.left + box * 0.78, b.top + box * 0.30, RGB(255, 255, 255), s(1.6), {0, 0, w, h});
        }
        x += box + s(6);
        if (has_swatch) {
            line(px, x, bar_top + bar_h / 2.0, x + s(14), bar_top + bar_h / 2.0, swatch, s(3), {0, 0, w, h});
            x += s(14) + s(5);
        }
        int const tw = measure(str);
        text({x, bar_top, x + tw + 2, bar_top + bar_h}, str, paint::kText, DT_LEFT);
        x += tw;
        out = {area.left + start, area.top + bar_top, area.left + x, area.top + bar_top + bar_h};
        x += s(14);
    };
    label(L"Intervallo");
    segmented({L"1 min", L"5 min", L"30 min", L"2 ore"}, range, hits.range);
    label(L"Mostra");
    segmented({L"Tutti i torrent", L"Torrent selezionato"}, std::clamp(v.source, 0, 1), hits.source);
    checkbox(L"Download", v.show_down, paint::kBlue, true, hits.down);
    checkbox(L"Upload", v.show_up, paint::kGreen, true, hits.up);
    {   // "Mostra limiti" right-aligned when it fits, otherwise after the others
        int const need = s(13) + s(6) + measure(L"Mostra limiti");
        if (w - s(12) - need > x) x = w - s(12) - need;
        checkbox(L"Mostra limiti", v.show_limits, 0, false, hits.limits);
    }

    // ---- samples inside the window (one before it, to reach the left edge)
    std::vector<SpeedSample> samples;
    if (v.samples && v.has_selection) {
        auto const& hist = *v.samples;
        for (std::size_t i = 0; i < hist.size(); ++i) {
            auto const& smp = hist.at(i);
            if (smp.time > v.now) continue;
            if (v.now - smp.time > window) {
                if (samples.empty()) samples.push_back(smp); else samples.back() = smp;
                continue;
            }
            samples.push_back(smp);
        }
        if (!samples.empty() && v.now - samples.front().time > window && samples.size() == 1) samples.clear();
    }
    std::int64_t peak_d = 0, peak_u = 0, sum_d = 0, sum_u = 0; int counted = 0;
    for (auto const& smp : samples)
        if (v.now - smp.time <= window) {
            peak_d = std::max(peak_d, smp.down); peak_u = std::max(peak_u, smp.up);
            sum_d += smp.down; sum_u += smp.up; ++counted;
        }

    // ---- tiles
    int y = bar_top + bar_h + s(8);
    bool const tiles = h >= s(230);
    if (tiles) {
        int const th = s(54), gap = s(10), tw = (w - s(24) - 2 * gap) / 3;
        struct Tile { wchar_t const* label; COLORREF dot; std::wstring value, sub; };
        auto avg = [&](std::int64_t sum) { return counted ? sum / counted : 0; };
        Tile const items[3] = {
            {L"Download", paint::kBlue, fmt_speed0(v.current_down), L"media " + fmt_speed0(avg(sum_d)) + L" · picco " + fmt_speed0(peak_d)},
            {L"Upload", paint::kGreen, fmt_speed0(v.current_up), L"media " + fmt_speed0(avg(sum_u)) + L" · picco " + fmt_speed0(peak_u)},
            {nullptr, paint::kGray, v.tile3_value, v.tile3_sub},
        };
        for (int i = 0; i < 3; ++i) {
            RECT const t{s(12) + i * (tw + gap), y, s(12) + i * (tw + gap) + tw, y + th};
            round_rect(px, t, s(5), 0, false, kTileBorder, 1);
            disc(px, t.left + s(14), t.top + s(13), s(3.5), items[i].dot, items[i].dot, 1);
            std::wstring const lbl = items[i].label ? items[i].label : v.tile3_label;
            text({t.left + s(24), t.top + s(4), t.right - s(8), t.top + s(22)}, lbl, paint::kGray, DT_LEFT);
            text({t.left + s(10), t.top + s(19), t.right - s(8), t.top + s(39)}, items[i].value, paint::kText, DT_LEFT, big);
            text({t.left + s(10), t.top + s(36), t.right - s(8), t.top + s(52)}, items[i].sub, paint::kGray, DT_LEFT);
        }
        y += th + s(10);
    }

    // ---- plot
    struct Tip { bool ok = false; int hx = -1; std::function<double(std::uint64_t)> X; std::function<double(std::int64_t)> Y; } tip;
    RECT plot{s(78), y + s(6), w - s(18), h - s(26)};
    hits.plot = {area.left + plot.left, area.top + plot.top, area.left + plot.right, area.top + plot.bottom};
    bool const plot_ok = plot.right - plot.left >= s(60) && plot.bottom - plot.top >= s(30);
    if (plot_ok) {
        double peak = 0;
        if (v.show_down) peak = std::max<double>(peak, static_cast<double>(peak_d));
        if (v.show_up) peak = std::max<double>(peak, static_cast<double>(peak_u));
        std::int64_t const lim_d = v.show_limits && v.show_down ? v.limit_down : 0, lim_u = v.show_limits && v.show_up ? v.limit_up : 0;
        // a limit far above the traffic would flatten the curves: it is drawn only when close enough
        for (std::int64_t lim : {lim_d, lim_u}) if (lim > 0 && (peak == 0 || lim <= peak * 3)) peak = std::max<double>(peak, static_cast<double>(lim));
        std::int64_t const top = nice_top(peak);
        double const pw = plot.right - plot.left, ph = plot.bottom - plot.top;
        std::uint64_t const now = v.now;
        auto X = [plot, pw, now, window](std::uint64_t t) { return plot.left + (1.0 - static_cast<double>(now - t) / static_cast<double>(window)) * pw; };
        auto Y = [plot, ph, top](std::int64_t rate) { return plot.bottom - std::min(1.0, static_cast<double>(rate) / static_cast<double>(top)) * ph; };
        // grid
        for (int k = 0; k <= 4; ++k) {
            int const gy = plot.bottom - static_cast<int>(std::lround(ph * k / 4));
            for (int gx = plot.left; gx <= plot.right; ++gx) px.blend(gx, gy, kGrid, 1);
            text({0, gy - s(10), plot.left - s(8), gy + s(10)}, fmt_speed0(top / 4 * k), paint::kGray, DT_RIGHT);
        }
        for (int k = 0; k <= 4; ++k) {
            int const gx = plot.left + static_cast<int>(std::lround(pw * k / 4));
            if (k > 0 && k < 4) for (int gy = plot.top; gy < plot.bottom; ++gy) px.blend(gx, gy, kGridV, 1);
            std::uint64_t const ago = window - window * static_cast<std::uint64_t>(k) / 4;
            text({gx - s(40), plot.bottom + s(4), gx + s(40), plot.bottom + s(22)}, ago_label(ago, range), paint::kGray, DT_CENTER);
        }
        // limit lines (dashed)
        auto limit_line = [&](std::int64_t lim, COLORREF c, wchar_t const* name, int slot) {
            if (lim <= 0 || lim > top) return;
            int const ly = static_cast<int>(std::lround(Y(lim)));
            for (int lx = plot.left; lx < plot.right; ++lx) if (((lx - plot.left) / s(5)) % 2 == 0) px.blend(lx, ly, c, 0.75);
            text({plot.left + s(6) + slot * s(190), ly - s(17), plot.left + s(6) + slot * s(190) + s(185), ly - s(1)},
                 std::wstring(L"limite ") + name + L" " + fmt_speed0(lim), c, DT_LEFT);
        };
        limit_line(lim_d, paint::kBlue, L"download", 0);
        limit_line(lim_u, paint::kGreen, L"upload", lim_d > 0 && std::abs(Y(lim_d) - Y(lim_u)) < s(18) ? 1 : 0);
        // series as runs of connected points (a gap breaks the curve)
        std::uint64_t const gap = window / 300 * 5 / 2 + 2000;
        auto runs_of = [&](bool upload) {
            std::vector<Run> runs;
            std::uint64_t prev = 0;
            for (auto const& smp : samples) {
                Point const p{X(smp.time), Y(upload ? smp.up : smp.down)};
                if (runs.empty() || smp.time - prev > gap) runs.emplace_back();
                runs.back().push_back(p);
                prev = smp.time;
            }
            return runs;
        };
        RECT const clip{plot.left, plot.top - s(2), plot.right + 1, plot.bottom + 1};
        auto draw_series = [&](bool upload, COLORREF c, double a_top, double a_bottom) {
            auto const runs = runs_of(upload);
            for (auto const& run : runs) {
                // area: per column, from the interpolated value down to the axis
                if (run.size() >= 2) {
                    std::size_t seg = 0;
                    int const x0 = std::max<int>(plot.left, static_cast<int>(std::ceil(run.front().x)));
                    int const x1 = std::min<int>(plot.right, static_cast<int>(std::floor(run.back().x)));
                    for (int cx = x0; cx <= x1; ++cx) {
                        while (seg + 1 < run.size() - 1 && run[seg + 1].x < cx) ++seg;
                        Point const& a = run[seg]; Point const& b = run[seg + 1];
                        double const t = b.x > a.x ? std::clamp((cx - a.x) / (b.x - a.x), 0.0, 1.0) : 0;
                        double const yv = a.y + (b.y - a.y) * t;
                        int const ystart = static_cast<int>(std::floor(yv));
                        for (int cy = std::max<int>(plot.top, ystart); cy < plot.bottom; ++cy) {
                            double const k = (cy - plot.top) / ph;
                            double alpha = a_top + (a_bottom - a_top) * k;
                            if (cy == ystart) alpha *= 1.0 - (yv - ystart);
                            px.blend(cx, cy, c, alpha);
                        }
                    }
                }
                for (std::size_t i = 1; i < run.size(); ++i) line(px, run[i - 1].x, run[i - 1].y, run[i].x, run[i].y, c, s(1.6), clip);
                if (run.size() == 1) disc(px, run[0].x, run[0].y, s(1.5), c, c, 1);
            }
        };
        if (v.show_down) draw_series(false, paint::kBlue, 0.26, 0.04);
        if (v.show_up) draw_series(true, paint::kGreen, 0.30, 0.05);
        // messages
        if (v.source == 1 && !v.has_selection) text(plot, L"Seleziona un torrent nella lista", paint::kGray, DT_CENTER);
        else if (!v.show_down && !v.show_up) text(plot, L"Attiva Download o Upload per vedere il grafico", paint::kGray, DT_CENTER);
        else if (counted == 0) text(plot, L"Raccolta dei dati di velocità…", paint::kGray, DT_CENTER);
        // hover read-out, drawn after the other text so nothing shows through it
        tip.hx = v.hover_x - area.left;
        tip.X = X; tip.Y = Y;
        tip.ok = true;
    }

    // ---- text on top of the pixels
    auto flush_text = [&]() {
        GdiFlush();
        for (auto const& t : texts) {
            SelectObject(dc, t.f);
            SetTextColor(dc, t.c);
            RECT r = t.r;
            DrawTextW(dc, t.s.c_str(), -1, &r, t.fmt);
        }
        GdiFlush();
        texts.clear();
    };
    flush_text();

    // ---- hover read-out
    if (tip.ok && tip.hx >= plot.left && tip.hx <= plot.right && counted > 0 && (v.show_down || v.show_up)) {
        texts.clear();
        int const hx = tip.hx;
        auto const& X = tip.X;
        auto const& Y = tip.Y;
        SpeedSample const* best = nullptr;
        double best_d = 1e18;
        for (auto const& smp : samples) {
            if (v.now - smp.time > window) continue;
            double const d = std::abs(X(smp.time) - hx);
            if (d < best_d) { best_d = d; best = &smp; }
        }
        if (best && best_d <= s(24)) {
            double const sx = X(best->time);
            for (int gy = plot.top; gy < plot.bottom; ++gy) if ((gy / s(3)) % 2 == 0) px.blend(static_cast<int>(sx), gy, paint::kGray, 0.8);
            if (v.show_down) disc(px, sx, Y(best->down), s(3.5), RGB(255, 255, 255), paint::kBlue, s(2));
            if (v.show_up) disc(px, sx, Y(best->up), s(3.5), RGB(255, 255, 255), paint::kGreen, s(2));
            std::uint64_t const ago = v.now - best->time;
            std::wstring const head = ago_label(ago, range) + L" (" + clock_ago(ago) + L")";
            std::wstring const ld = L"↓ " + fmt_speed0(best->down), lu = L"↑ " + fmt_speed0(best->up);
            int const bw = std::max({measure(head, bold), measure(ld), measure(lu)}) + s(20);
            int const lines = 1 + (v.show_down ? 1 : 0) + (v.show_up ? 1 : 0);
            int const bh = s(8) + lines * s(16) + s(4);
            int bx = static_cast<int>(sx) + s(10);
            if (bx + bw > plot.right) bx = static_cast<int>(sx) - s(10) - bw;
            RECT const box{bx, plot.top + s(4), bx + bw, plot.top + s(4) + bh};
            round_rect(px, box, s(4), RGB(255, 255, 255), true, kBorder, 1);
            int ty = box.top + s(5);
            text({box.left + s(10), ty, box.right - s(4), ty + s(16)}, head, paint::kText, DT_LEFT, bold); ty += s(16);
            if (v.show_down) { text({box.left + s(10), ty, box.right - s(4), ty + s(16)}, ld, paint::kBlue, DT_LEFT); ty += s(16); }
            if (v.show_up) text({box.left + s(10), ty, box.right - s(4), ty + s(16)}, lu, paint::kGreen, DT_LEFT);
            flush_text();
        }
    }
    BitBlt(target, area.left, area.top, w, h, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old_font);
    SelectObject(dc, old_bmp);
    DeleteObject(bmp);
    DeleteDC(dc);
    if (hit) *hit = hits;
}

namespace {
GraphHit g_hit{};
bool in(RECT const& r, POINT p) { return r.right > r.left && PtInRect(&r, p); }
// part: 0 range, 1 source, 2 down, 3 up, 4 limits; -1 none
int hit_part(POINT p, int& index) {
    for (int i = 0; i < kGraphRangeCount; ++i) if (in(g_hit.range[i], p)) { index = i; return 0; }
    for (int i = 0; i < 2; ++i) if (in(g_hit.source[i], p)) { index = i; return 1; }
    index = 0;
    if (in(g_hit.down, p)) return 2;
    if (in(g_hit.up, p)) return 3;
    if (in(g_hit.limits, p)) return 4;
    return -1;
}
} // namespace

LRESULT CALLBACK speed_graph_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
    case WM_ERASEBKGND: return 1;
    case WM_SIZE: InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_MOUSEMOVE: {
        POINT const p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int const hx = in(g_hit.plot, p) ? p.x : -1;
        if (hx != g.graph_hover_x) { g.graph_hover_x = hx; InvalidateRect(hwnd, nullptr, FALSE); }
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&tme);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (g.graph_hover_x != -1) { g.graph_hover_x = -1; InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            POINT p; GetCursorPos(&p); ScreenToClient(hwnd, &p);
            int index = 0;
            if (hit_part(p, index) >= 0) { SetCursor(LoadCursorW(nullptr, IDC_HAND)); return TRUE; }
        }
        break;
    case WM_LBUTTONDOWN: {
        POINT const p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        int index = 0;
        int const part = hit_part(p, index);
        if (part >= 0) { graph_option_clicked(part, index); InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        RECT area{}; GetClientRect(hwnd, &area);
        if (area.right > 0 && area.bottom > 0)
            draw_speed_graph(dc, area, current_graph_view(), g.font, g.big, g.bold, g.dpi, &g_hit);
        EndPaint(hwnd, &paint);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}

} // namespace sf
