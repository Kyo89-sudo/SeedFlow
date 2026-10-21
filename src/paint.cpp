#include "paint.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sf::paint {

COLORREF mix(COLORREF a, COLORREF b, double t) {
    auto m = [t](int x, int y) { return static_cast<BYTE>(std::lround(x + (y - x) * t)); };
    return RGB(m(GetRValue(a), GetRValue(b)), m(GetGValue(a), GetGValue(b)), m(GetBValue(a), GetBValue(b)));
}

namespace {

// Premultiplied BGRA canvas, alpha-blended onto the target DC when done.
struct Canvas {
    int w, h;
    std::vector<std::uint32_t> px;
    Canvas(int w_, int h_) : w(std::max(1, w_)), h(std::max(1, h_)), px(static_cast<std::size_t>(w) * h, 0) {}
    // source-over of colour c with coverage a (0..1)
    void put(int x, int y, COLORREF c, double a) {
        if (a <= 0 || x < 0 || y < 0 || x >= w || y >= h) return;
        a = std::min(a, 1.0);
        std::uint32_t& p = px[static_cast<std::size_t>(y) * w + x];
        double k = 1 - a;
        auto ch = [&](int shift, int v) { return static_cast<std::uint32_t>(std::lround(v * a + ((p >> shift) & 0xFF) * k)); };
        std::uint32_t al = static_cast<std::uint32_t>(std::lround(255 * a + (p >> 24) * k));
        p = (al << 24) | (ch(16, GetRValue(c)) << 16) | (ch(8, GetGValue(c)) << 8) | ch(0, GetBValue(c));
    }
    void blit(HDC dc, int x, int y) const {
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        void* bits = nullptr;
        HBITMAP bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bmp || !bits) { if (bmp) DeleteObject(bmp); return; }
        std::memcpy(bits, px.data(), px.size() * 4);
        HDC mem = CreateCompatibleDC(dc);
        HGDIOBJ old = SelectObject(mem, bmp);
        BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        GdiAlphaBlend(dc, x, y, w, h, mem, 0, 0, w, h, bf);
        SelectObject(mem, old);
        DeleteDC(mem);
        DeleteObject(bmp);
    }
};

// Signed distance from the pixel centre to a rounded rectangle [x0,x1]x[y0,y1].
double sd_round(double px, double py, double x0, double y0, double x1, double y1, double r) {
    double cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, hx = (x1 - x0) / 2 - r, hy = (y1 - y0) / 2 - r;
    double qx = std::abs(px - cx) - hx, qy = std::abs(py - cy) - hy;
    double ox = std::max(qx, 0.0), oy = std::max(qy, 0.0);
    return std::hypot(ox, oy) + std::min(std::max(qx, qy), 0.0) - r;
}

UINT g_dpi = 96;
int dpi_px(HDC, double px96) { return std::max(1, static_cast<int>(std::lround(px96 * g_dpi / 96.0))); }

void text_two_tone(HDC dc, RECT rc, int split_x, std::wstring const& text, HFONT font, COLORREF over_fill, COLORREF over_track) {
    if (text.empty()) return;
    HGDIOBJ oldf = SelectObject(dc, font);
    int oldbk = SetBkMode(dc, TRANSPARENT);
    COLORREF oldc = GetTextColor(dc);
    UINT fmt = DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;
    for (int pass = 0; pass < 2; ++pass) {
        RECT clip = rc;
        if (pass == 0) clip.right = std::min(rc.right, static_cast<LONG>(split_x)); else clip.left = std::max(rc.left, static_cast<LONG>(split_x));
        if (clip.right <= clip.left) continue;
        int saved = SaveDC(dc);
        IntersectClipRect(dc, clip.left, clip.top, clip.right, clip.bottom);
        SetTextColor(dc, pass == 0 ? over_fill : over_track);
        RECT t = rc;
        DrawTextW(dc, text.c_str(), -1, &t, fmt);
        RestoreDC(dc, saved);
    }
    SetTextColor(dc, oldc);
    SetBkMode(dc, oldbk);
    SelectObject(dc, oldf);
}

} // namespace

void set_dpi(UINT dpi) { g_dpi = dpi ? dpi : 96; }

void progress_bar(HDC dc, RECT rc, double fraction, COLORREF color, Bar kind, unsigned phase,
                  std::wstring const& text, HFONT font) {
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (w <= 2 || h <= 2) return;
    fraction = std::clamp(fraction, 0.0, 1.0);
    double radius = std::min(h / 2.0, static_cast<double>(dpi_px(dc, 3)));
    double stroke = std::max(1.0, dpi_px(dc, 1) * 1.0);
    COLORREF track = mix(color, RGB(255, 255, 255), 0.86);      // the icons' 18% tint, slightly lighter
    COLORREF edge = mix(color, RGB(255, 255, 255), 0.35);
    COLORREF stripe = mix(color, RGB(0, 0, 0), 0.14);         // darker stripes keep white text readable
    COLORREF segment = mix(color, RGB(255, 255, 255), 0.55);   // light moving segment: dark text stays readable
    double fill_x0 = 0, fill_x1 = w * fraction;
    if (kind == Bar::Indeterminate) {                            // a segment sliding left to right
        double seg = w * 0.3, pos = std::fmod(phase * (w + seg) * 0.04, w + seg) - seg;   // ~2.5 s per sweep at 100 ms
        fill_x0 = std::max(0.0, pos);
        fill_x1 = std::min<double>(w, pos + seg);
    }
    double period = dpi_px(dc, 8);
    Canvas cv(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            double px = x + 0.5, py = y + 0.5;
            double d = sd_round(px, py, 0, 0, w, h, radius);
            double cov = std::clamp(0.5 - d, 0.0, 1.0);
            if (cov <= 0) continue;
            cv.put(x, y, track, cov);
            double in_fill = std::clamp(std::min(px - fill_x0, fill_x1 - px) + 0.5, 0.0, 1.0);
            if (in_fill > 0) {
                COLORREF c = kind == Bar::Indeterminate ? segment : color;
                if (kind == Bar::Striped) {
                    double s = std::fmod(px + py + phase * period * 0.25, period);
                    if (s < 0) s += period;
                    if (s < period / 2) c = stripe;
                }
                cv.put(x, y, c, cov * in_fill);
            }
            double ring = std::clamp(0.5 - std::abs(d + stroke / 2) + stroke / 2, 0.0, 1.0);   // inner outline
            cv.put(x, y, edge, ring * 0.9);
        }
    cv.blit(dc, rc.left, rc.top);
    int split = kind == Bar::Indeterminate ? rc.left : rc.left + static_cast<int>(std::lround(fill_x1));
    text_two_tone(dc, rc, split, text, font, RGB(255, 255, 255), kText);
}

Health health(TorrentView const& v) {
    Health h;
    if (!v.has_metadata) return h;
    int full = std::max({v.seeds, v.list_seeds, 0});
    bool complete = v.seeding || v.progress >= 1.0f;
    if (complete) h.copies = std::max(1.0 + std::max(v.seeds, 0), static_cast<double>(full));  // the scrape count already includes us
    else if (v.distributed_copies >= 0) h.copies = std::max<double>(v.distributed_copies, full);
    else h.copies = full;
    if (v.paused && !complete && full == 0 && v.distributed_copies <= 0) return Health{};   // no data while stopped
    double c = h.copies;
    if (c < 0.5)      h = {1, c, L"Critica: nessuna copia completa raggiungibile", kRed};
    else if (c < 1.0) h = {2, c, L"Scarsa: copie incomplete, alcune parti mancano", kOrange};
    else if (c < 2.0) h = {3, c, L"Sufficiente: una sola copia completa", kAmber};
    else if (c < 5.0) h = {4, c, L"Buona", kGreen};
    else              h = {5, c, L"Ottima", kDGreen};
    return h;
}

void health_meter(HDC dc, RECT rc, Health const& h, HFONT font, bool selected) {
    int bar_w = dpi_px(dc, 4), gap = dpi_px(dc, 2), pad = dpi_px(dc, 6);
    int full_h = std::min<int>(rc.bottom - rc.top - dpi_px(dc, 6), dpi_px(dc, 14));
    int total_w = 5 * bar_w + 4 * gap;
    if (full_h < 5 || rc.right - rc.left < total_w + 2 * pad) return;
    int x0 = rc.left + pad, base = (rc.top + rc.bottom + full_h) / 2;
    Canvas cv(total_w, full_h);
    COLORREF off = selected ? RGB(0xC4, 0xCD, 0xD6) : RGB(0xD9, 0xE0, 0xE6);
    for (int i = 0; i < 5; ++i) {
        int bh = full_h * (i + 2) / 6, bx = i * (bar_w + gap);
        COLORREF c = i < h.level ? h.color : off;
        for (int y = full_h - bh; y < full_h; ++y)
            for (int x = bx; x < bx + bar_w; ++x) {
                double d = sd_round(x + 0.5, y + 0.5, bx, full_h - bh, bx + bar_w, full_h, bar_w / 2.0 * 0.7);
                cv.put(x, y, c, std::clamp(0.5 - d, 0.0, 1.0));
            }
    }
    cv.blit(dc, x0, base - full_h);
    wchar_t buf[32];
    if (h.level == 0) wcscpy_s(buf, L"—");
    else if (h.copies >= 100) swprintf(buf, 32, L"%.0f", h.copies);
    else swprintf(buf, 32, L"%.2f", h.copies);
    RECT t{x0 + total_w + pad, rc.top, rc.right - dpi_px(dc, 2), rc.bottom};
    HGDIOBJ oldf = SelectObject(dc, font);
    int oldbk = SetBkMode(dc, TRANSPARENT);
    COLORREF oldc = SetTextColor(dc, kText);
    DrawTextW(dc, buf, -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    SetTextColor(dc, oldc);
    SetBkMode(dc, oldbk);
    SelectObject(dc, oldf);
}

} // namespace sf::paint
