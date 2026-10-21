// SeedFlow - icon set: PNG strips (one per size) decoded once with WIC into
// premultiplied BGRA; sizes without a strip are area-resampled from the next larger one.
#include "icons.hpp"
#include "resource.h"
#include <wincodec.h>
#include <shlwapi.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

namespace sf::icons {
namespace {

// Local GUID copies: avoids depending on libwindowscodecs / uuid exports.
const GUID kClsidWicFactory = {0xcacaf262, 0x9370, 0x4615, {0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a}};
const GUID kIidWicFactory = {0xec5ec8a9, 0xc395, 0x4314, {0x9c, 0x77, 0x54, 0xd7, 0xa9, 0x35, 0xff, 0x70}};
const GUID kPbgra32 = {0x6fddc324, 0x4e03, 0x4bfe, {0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x10}};

struct Strip { int size = 0; std::vector<std::uint32_t> px; };   // width = size * IcCount
std::map<int, Strip> g_cache;

template <class T> void release(T*& p) { if (p) { p->Release(); p = nullptr; } }

bool decode(int size, Strip& out) {
    HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_ICONS_BASE + size), MAKEINTRESOURCEW(10) /*RT_RCDATA*/);
    if (!res) return false;
    HGLOBAL h = LoadResource(nullptr, res);
    auto const* data = static_cast<BYTE const*>(LockResource(h));
    DWORD len = SizeofResource(nullptr, res);
    if (!data || !len) return false;
    IWICImagingFactory* f = nullptr;
    IStream* stream = nullptr;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* conv = nullptr;
    bool ok = false;
    if (SUCCEEDED(CoCreateInstance(kClsidWicFactory, nullptr, CLSCTX_INPROC_SERVER, kIidWicFactory, reinterpret_cast<void**>(&f)))
        && (stream = SHCreateMemStream(data, len)) != nullptr
        && SUCCEEDED(f->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &dec))
        && SUCCEEDED(dec->GetFrame(0, &frame))
        && SUCCEEDED(f->CreateFormatConverter(&conv))
        && SUCCEEDED(conv->Initialize(frame, kPbgra32, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
        UINT w = 0, hh = 0;
        conv->GetSize(&w, &hh);
        if (static_cast<int>(w) == size * IcCount && static_cast<int>(hh) == size) {
            out.size = size;
            out.px.assign(static_cast<std::size_t>(w) * hh, 0);
            ok = SUCCEEDED(conv->CopyPixels(nullptr, w * 4, w * hh * 4, reinterpret_cast<BYTE*>(out.px.data())));
        }
    }
    release(conv); release(frame); release(dec); release(stream); release(f);
    return ok;
}

Strip const* strip(int size) {
    auto it = g_cache.find(size);
    if (it != g_cache.end()) return it->second.px.empty() ? nullptr : &it->second;
    Strip& s = g_cache[size];
    if (!decode(size, s)) s.px.clear();
    return s.px.empty() ? nullptr : &s;
}

// One icon as premultiplied BGRA (size x size).
std::vector<std::uint32_t> pixels(Id id, int size) {
    if (id < 0 || id >= IcCount || size <= 0 || size > 1024) return {};
    std::vector<std::uint32_t> out(static_cast<std::size_t>(size) * size, 0);
    int src = 0;
    for (int s : kStripSizes) if (s == size) { src = s; break; }
    if (!src) {                              // smallest strip >= size, else the largest
        for (int s : kStripSizes) if (s >= size) { src = s; break; }
        if (!src) src = kStripSizes[std::size(kStripSizes) - 1];
    }
    Strip const* st = strip(src);
    if (!st) return out;
    int stride = src * IcCount, x0 = id * src;
    if (src == size) {
        for (int y = 0; y < size; ++y)
            std::copy_n(&st->px[static_cast<std::size_t>(y) * stride + x0], size, &out[static_cast<std::size_t>(y) * size]);
        return out;
    }
    // Area-weighted resampling on premultiplied channels (no fringes).
    double k = static_cast<double>(src) / size;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            double sy0 = y * k, sy1 = (y + 1) * k, sx0 = x * k, sx1 = (x + 1) * k, acc[4] = {0, 0, 0, 0}, area = 0;
            for (int sy = static_cast<int>(sy0); sy < src && sy < sy1; ++sy) {
                double wy = std::min<double>(sy + 1, sy1) - std::max<double>(sy, sy0);
                for (int sx = static_cast<int>(sx0); sx < src && sx < sx1; ++sx) {
                    double w = wy * (std::min<double>(sx + 1, sx1) - std::max<double>(sx, sx0));
                    std::uint32_t p = st->px[static_cast<std::size_t>(sy) * stride + x0 + sx];
                    for (int c = 0; c < 4; ++c) acc[c] += w * ((p >> (c * 8)) & 0xFF);
                    area += w;
                }
            }
            std::uint32_t v = 0;
            for (int c = 0; c < 4; ++c) v |= static_cast<std::uint32_t>(std::clamp(acc[c] / area + 0.5, 0.0, 255.0)) << (c * 8);
            out[static_cast<std::size_t>(y) * size + x] = v;
        }
    return out;
}

HBITMAP to_bitmap(std::vector<std::uint32_t> const& px, int w, int h) {
    if (w <= 0 || h <= 0 || px.size() != static_cast<std::size_t>(w) * h) return nullptr;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bmp && bits) std::memcpy(bits, px.data(), px.size() * 4);
    return bmp;
}

// Status dot with a white ring in the bottom-right quarter (premultiplied, antialiased).
void add_badge(std::vector<std::uint32_t>& px, int size, COLORREF c) {
    double r = size * 0.22, cx = size - r - 0.5, cy = size - r - 0.5, ring = std::max(1.0, size / 16.0);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            double d = std::hypot(x + 0.5 - cx, y + 0.5 - cy);
            double outer = std::clamp(r + ring + 0.5 - d, 0.0, 1.0);    // ring + dot coverage
            if (outer <= 0) continue;
            double inner = std::clamp(r + 0.5 - d, 0.0, 1.0);          // dot coverage
            double cr = 255 + (GetRValue(c) - 255) * inner, cg = 255 + (GetGValue(c) - 255) * inner, cb = 255 + (GetBValue(c) - 255) * inner;
            std::uint32_t& p = px[static_cast<std::size_t>(y) * size + x];
            double a0 = ((p >> 24) & 0xFF) / 255.0, keep = 1 - outer;
            auto ch = [&](int shift, double v) { return static_cast<std::uint32_t>(std::clamp(v * outer + ((p >> shift) & 0xFF) * keep + 0.5, 0.0, 255.0)); };
            std::uint32_t a = static_cast<std::uint32_t>(std::clamp((outer + a0 * keep) * 255 + 0.5, 0.0, 255.0));
            p = (a << 24) | (ch(16, cr) << 16) | (ch(8, cg) << 8) | ch(0, cb);
        }
}

} // namespace

HIMAGELIST make_imagelist(Id const* ids, int count, int size) {
    if (!ids || count <= 0 || size <= 0 || size > 1024) return nullptr;
    HIMAGELIST il = ImageList_Create(size, size, ILC_COLOR32, count, 4);
    if (!il) return nullptr;
    for (int i = 0; i < count; ++i) {
        HBITMAP bmp = to_bitmap(pixels(ids[i], size), size, size);
        ImageList_Add(il, bmp, nullptr);
        DeleteObject(bmp);
    }
    return il;
}

HICON make_icon(Id id, int size, COLORREF badge) {
    auto px = pixels(id, size);
    if (px.empty()) return nullptr;
    if (badge != CLR_INVALID) add_badge(px, size, badge);
    // CreateIconIndirect expects straight (non-premultiplied) alpha.
    for (auto& p : px) {
        std::uint32_t a = p >> 24;
        if (!a || a == 255) continue;
        auto un = [&](int s) { return std::min<std::uint32_t>(255, ((p >> s) & 0xFF) * 255 / a) << s; };
        p = (a << 24) | un(16) | un(8) | un(0);
    }
    HBITMAP color = to_bitmap(px, size, size);
    std::vector<unsigned char> zeros(static_cast<std::size_t>((size + 15) / 16 * 2 * size), 0);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, zeros.data());
    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmColor = color;
    ii.hbmMask = mask;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

HBITMAP make_menu_bitmap(Id id, int size) { return to_bitmap(pixels(id, size), size, size); }

void draw(HDC dc, Id id, int x, int y, int size) {
    HBITMAP bmp = make_menu_bitmap(id, size);
    if (!bmp) return;
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(mem, bmp);
    BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    GdiAlphaBlend(dc, x, y, size, size, mem, 0, 0, size, size, bf);
    SelectObject(mem, old);
    DeleteDC(mem);
    DeleteObject(bmp);
}

void trim_cache() { g_cache.clear(); }

} // namespace sf::icons
