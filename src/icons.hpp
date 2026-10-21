// SeedFlow - icon set (style A: duotone 24x24, stroke 2) pre-rendered from
// res/icons/svg into PNG strips (16..48 px) embedded as RCDATA resources.
#pragma once
#include "win.hpp"
#include "icon_ids.hpp"

namespace sf::icons {

// 32bpp image list (premultiplied alpha).
HIMAGELIST make_imagelist(Id const* ids, int count, int size);
// Icon for status bar / tray; optional status dot (bottom-right) when badge != CLR_INVALID.
HICON make_icon(Id id, int size, COLORREF badge = CLR_INVALID);
// 32bpp premultiplied DIB for menu items (hbmpItem). Caller deletes.
HBITMAP make_menu_bitmap(Id id, int size);
// Alpha-blends one icon at (x, y).
void draw(HDC dc, Id id, int x, int y, int size);
// Releases the decoded strips (e.g. after a DPI change once image lists are rebuilt).
void trim_cache();

} // namespace sf::icons
