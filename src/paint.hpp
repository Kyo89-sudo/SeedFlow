// SeedFlow - custom-drawn widgets sharing the icon set's look (style A duotone:
// rounded shapes, coloured outline, light tint, solid fill; same palette).
#pragma once
#include "win.hpp"
#include "engine.hpp"
#include <string>

namespace sf::paint {

// Palette of the icon set (res/icons/icons.py).
inline constexpr COLORREF kGreen = RGB(0x2E, 0x9B, 0x45), kDGreen = RGB(0x1E, 0x7B, 0x34), kBlue = RGB(0x1A, 0x73, 0xC8),
    kOrange = RGB(0xEF, 0x7D, 0x00), kRed = RGB(0xD9, 0x30, 0x25), kGray = RGB(0x5F, 0x6B, 0x78), kAmber = RGB(0xF2, 0xA9, 0x00),
    kTeal = RGB(0x00, 0x83, 0x8F), kPurple = RGB(0x7B, 0x3F, 0xB8), kNavy = RGB(0x2B, 0x4A, 0x6F), kText = RGB(0x15, 0x20, 0x2B);

void set_dpi(UINT dpi);                             // per-monitor DPI of the main window
COLORREF mix(COLORREF a, COLORREF b, double t);     // t = 0 -> a, 1 -> b

enum class Bar { Solid, Striped, Indeterminate };
// Progress bar inside rc: track (tint + outline) and fill (solid / animated stripes /
// moving segment). Text is centred, white over the fill and dark over the track.
void progress_bar(HDC dc, RECT rc, double fraction, COLORREF color, Bar kind, unsigned phase,
                  std::wstring const& text, HFONT font);

// Swarm health: 0 = unknown, 1 critical .. 5 excellent. copies = estimated full copies.
struct Health { int level = 0; double copies = -1; wchar_t const* label = L"Sconosciuta"; COLORREF color = kGray; };
Health health(TorrentView const& v);
// Five rising bars + the copies value.
void health_meter(HDC dc, RECT rc, Health const& h, HFONT font, bool selected);

} // namespace sf::paint
