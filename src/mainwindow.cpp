// SeedFlow - main window (native Win32).
#include "app.hpp"
#include "icons.hpp"
#include "paint.hpp"
#include "resource.h"
#include "util.hpp"
#include "tuning.hpp"
#include "version.h"
#include <shellapi.h>
#include <shobjidl.h>
#include <uxtheme.h>
#include <vssym32.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <thread>
#include "speed_graph.hpp"

namespace fs = std::filesystem;

namespace sf {

App g;
static wchar_t const* const kClass = L"SeedFlowMainWindow";
static wchar_t const* const kGeneralClass = L"SeedFlowGeneralPanel";
static wchar_t const* const kSpeedClass = L"SeedFlowSpeedPanel";

int scale(int px) { return MulDiv(px, static_cast<int>(g.dpi), 96); }

// ------------------------------------------------------------------ helpers
void log_line(std::wstring const& text) {
    if (!g.log) return;
    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t stamp[32];
    swprintf(stamp, 32, L"[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    std::wstring line = stamp + text + L"\r\n";
    int len = GetWindowTextLengthW(g.log);
    if (len > 120000) { // keep the log bounded
        SendMessageW(g.log, EM_SETSEL, 0, len / 2);
        SendMessageW(g.log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(L""));
        len = GetWindowTextLengthW(g.log);
    }
    SendMessageW(g.log, EM_SETSEL, len, len);
    SendMessageW(g.log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line.c_str()));
}
void persist() {
    std::string err;
    if (!save_settings(g.settings, err)) log_line(L"Salvataggio impostazioni non riuscito: " + widen(err));
}
std::string download_dir() {
    if (!g.settings.download_dir.empty()) return g.settings.download_dir;
    return narrow(default_download_dir().wstring());
}
TorrentMeta& meta_for(std::string const& hash) {
    auto it = g.settings.torrents.find(hash);
    if (it == g.settings.torrents.end()) {
        TorrentMeta m;
        m.added = static_cast<std::int64_t>(std::time(nullptr));
        it = g.settings.torrents.emplace(hash, m).first;
    }
    return it->second;
}
bool ratio_target(TorrentMeta const& m, double& limit, int& action) {
    if (m.ratio_mode == RatioNone) return false;
    if (m.ratio_mode == RatioCustom) {
        if (m.ratio_limit <= 0) return false;
        limit = m.ratio_limit;
    } else {
        if (g.settings.global_ratio <= 0) return false;
        limit = g.settings.global_ratio;
    }
    action = std::clamp(m.ratio_action >= 0 ? m.ratio_action : g.settings.global_ratio_action, 0, 3);
    return true;
}
Row const* row_by_id(int id) {
    for (auto const& r : g.rows) if (r.v.id == id) return &r;
    return nullptr;
}
std::vector<int> selected_ids() {
    std::vector<int> ids;
    int i = -1;
    while ((i = ListView_GetNextItem(g.list, i, LVNI_SELECTED)) >= 0)
        if (i < static_cast<int>(g.visible.size())) ids.push_back(g.rows[static_cast<std::size_t>(g.visible[static_cast<std::size_t>(i)])].v.id);
    return ids;
}
std::wstring tr_status(int s) {
    switch (s) {
        case 1: return L"Funzionante";
        case 2: return L"Aggiornamento…";
        case 3: return L"Errore";
        case 4: return L"Disabilitato";
        case 5: return L"Di riserva";
        default: return L"Non contattato";
    }
}

// State descriptions and icon indices into il_states.
enum StateIcon { SiDownload, SiSeed, SiPause, SiQueued, SiCheck, SiMeta, SiError, SiDone, SiMove, SiCount };
static void describe(TorrentView const& v, Row& r) {
    using namespace paint;
    auto set = [&](wchar_t const* t, int icon, COLORREF c) { r.state = t; r.state_icon = icon; r.color = c; };
    bool checking = v.state == 1 || v.state == 7;
    if (v.has_error) return set(L"Errore", SiError, kRed);
    if (v.moving) return set(L"Spostamento", SiMove, kPurple);
    if (checking && v.paused && v.auto_managed) return set(L"In coda per verifica", SiCheck, kOrange);
    if (v.paused && !v.auto_managed) return set(checking ? L"Verifica in pausa" : L"In pausa", SiPause, kGray);
    if (v.paused) return set(v.seeding || v.progress >= 1 ? L"In coda (seed)" : L"In coda", SiQueued, kGray);
    switch (v.state) {
        case 1: return set(L"Verifica", SiCheck, kOrange);
        case 2: return set(L"Metadati", SiMeta, kBlue);
        case 3: return set(L"Download", SiDownload, kBlue);
        case 4: return set(L"Completato", SiDone, kGreen);
        case 5: return set(v.super_seeding ? L"Super-seeding" : L"Seeding", SiSeed, kGreen);
        case 7: return set(L"Verifica ripristino", SiCheck, kOrange);
        default: return set(v.seeding ? L"Seeding" : L"Attivo", SiSeed, kGreen);
    }
}
// How the progress column is drawn for a row.
struct BarLook { double fraction; paint::Bar kind; std::wstring text; bool animated; };
static BarLook bar_look(Row const& r) {
    auto const& v = r.v;
    std::wstring pct = fmt_double(v.progress * 100.0, 1) + L"%";
    bool checking = v.state == 1 || v.state == 7;
    if (v.moving) return {1.0, paint::Bar::Indeterminate, L"Spostamento…", true};
    if (checking && !v.has_error) {
        if (v.paused) return {v.progress, paint::Bar::Striped, (v.auto_managed ? L"In coda: verifica " : L"Verifica ") + pct, false};
        if (v.state == 7) return {0.0, paint::Bar::Indeterminate, L"Lettura dati di ripristino…", true};
        return {v.progress, paint::Bar::Striped, L"Verifica " + pct, true};
    }
    if (v.state == 2 && !v.paused && !v.has_error) return {0.0, paint::Bar::Indeterminate, L"Recupero metadati…", true};
    return {v.progress, paint::Bar::Solid, pct, false};
}
static bool is_complete(TorrentView const& v) { return v.seeding || v.finished || v.progress >= 1.0f; }

// ------------------------------------------------------------------ fonts / image lists
static void create_fonts() {
    if (g.font) DeleteObject(g.font);
    if (g.bold) DeleteObject(g.bold);
    if (g.mono) DeleteObject(g.mono);
    if (g.big) DeleteObject(g.big);
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, g.dpi);
    g.font = CreateFontIndirectW(&ncm.lfMessageFont);
    LOGFONTW b = ncm.lfMessageFont; b.lfWeight = FW_SEMIBOLD;
    g.bold = CreateFontIndirectW(&b);
    LOGFONTW big = ncm.lfMessageFont; big.lfHeight = big.lfHeight * 3 / 2; big.lfWeight = FW_SEMIBOLD;
    g.big = CreateFontIndirectW(&big);
    LOGFONTW m = ncm.lfMessageFont; wcscpy_s(m.lfFaceName, L"Consolas");
    g.mono = CreateFontIndirectW(&m);
}
static void create_imagelists() {
    using namespace icons;
    for (HIMAGELIST* il : {&g.il_main_bar, &g.il_toolbar, &g.il_states, &g.il_filters, &g.il_small}) if (*il) { ImageList_Destroy(*il); *il = nullptr; }
    // order = toolbar image indices
    Id tb[] = {IcAdd, IcMagnet, IcPlay, IcPause, IcRemove, IcDelete, IcUp, IcDown, IcSuperseed, IcRatio, IcWizard,
               IcStats, IcDiagnostics, IcRefresh, IcCopy, IcDelete, IcPreferences, IcCreate};
    g.il_toolbar = make_imagelist(tb, static_cast<int>(std::size(tb)), scale(16));
    g.il_main_bar = make_imagelist(tb, static_cast<int>(std::size(tb)), scale(20));
    Id st[SiCount] = {IcDownload, IcUpload, IcPause, IcQueue, IcChecking, IcMagnet, IcError, IcCheck, IcMoving};
    g.il_states = make_imagelist(st, SiCount, scale(16));
    Id fl[] = {IcAll, IcDownload, IcUpload, IcCheck, IcSuperseed, IcPause, IcError};
    g.il_filters = make_imagelist(fl, static_cast<int>(std::size(fl)), scale(16));
    Id sm[] = {IcInfo, IcTracker, IcPeers, IcFile, IcLog, IcCheck, IcRefresh, IcError, IcQueue, IcRemove, IcSpeed};
    g.il_small = make_imagelist(sm, static_cast<int>(std::size(sm)), scale(16));
}

// ------------------------------------------------------------------ menu icons
static std::vector<HBITMAP> menu_bitmaps;
static void release_menu_bitmaps_from(std::size_t first) {
    while (menu_bitmaps.size() > first) {
        DeleteObject(menu_bitmaps.back());
        menu_bitmaps.pop_back();
    }
}
static void set_menu_icon(HMENU menu, UINT cmd, icons::Id id) {
    // decorate_menu() lists every command of every menu: only build bitmaps for items
    // that exist in this one (a context menu used to create ~40 bitmaps per click).
    if (GetMenuState(menu, cmd, MF_BYCOMMAND) == static_cast<UINT>(-1)) return;
    HBITMAP bmp = icons::make_menu_bitmap(id, scale(16));
    menu_bitmaps.push_back(bmp);
    MENUITEMINFOW mi{};
    mi.cbSize = sizeof(mi);
    mi.fMask = MIIM_BITMAP;
    mi.hbmpItem = bmp;
    SetMenuItemInfoW(menu, cmd, FALSE, &mi);
}
static void decorate_menu(HMENU m) {
    using namespace icons;
    set_menu_icon(m, ID_FILE_OPEN, IcAdd);
    set_menu_icon(m, ID_FILE_MAGNET, IcMagnet);
    set_menu_icon(m, ID_FILE_PASTE, IcCopy);
    set_menu_icon(m, ID_FILE_CREATE, IcCreate);
    set_menu_icon(m, ID_T_STARTALL, IcPlay);
    set_menu_icon(m, ID_T_PAUSEALL, IcPause);
    set_menu_icon(m, ID_FILE_EXIT, IcExit);
    set_menu_icon(m, ID_OPT_PREFS, IcPreferences);
    set_menu_icon(m, ID_TOOLS_WIZARD, IcWizard);
    set_menu_icon(m, ID_TOOLS_DIAG, IcDiagnostics);
    set_menu_icon(m, ID_TOOLS_STATS, IcStats);
    set_menu_icon(m, ID_TOOLS_DATAFOLDER, IcFolder);
    set_menu_icon(m, ID_HELP_ABOUT, IcInfo);
    set_menu_icon(m, ID_HELP_GUIDE, IcFile);
    set_menu_icon(m, ID_HELP_KEYS, IcWindow);
    set_menu_icon(m, ID_HELP_RELEASES, IcPriority);
    set_menu_icon(m, ID_HELP_GITHUB, IcGlobe);
    set_menu_icon(m, ID_HELP_REPORT, IcError);
    set_menu_icon(m, ID_T_START, IcPlay);
    set_menu_icon(m, ID_T_FORCESTART, IcPriority);
    set_menu_icon(m, ID_T_PAUSE, IcPause);
    set_menu_icon(m, ID_T_COPYMAGNET, IcCopy);
    set_menu_icon(m, ID_T_OPENFOLDER, IcFolder);
    set_menu_icon(m, ID_T_MOVE, IcMoving);
    set_menu_icon(m, ID_T_RATIO, IcRatio);
    set_menu_icon(m, ID_T_LIMITS, IcSpeed);
    set_menu_icon(m, ID_T_SUPERSEED, IcSuperseed);
    set_menu_icon(m, ID_T_REANNOUNCE, IcRefresh);
    set_menu_icon(m, ID_T_RECHECK, IcChecking);
    set_menu_icon(m, ID_T_REMOVE, IcRemove);
    set_menu_icon(m, ID_T_DELETE, IcDelete);
    set_menu_icon(m, ID_TR_ADD, IcAdd);
    set_menu_icon(m, ID_TR_REMOVE, IcDelete);
    set_menu_icon(m, ID_TR_COPY, IcCopy);
    set_menu_icon(m, ID_TR_REANNOUNCE, IcRefresh);
    set_menu_icon(m, ID_TRAY_OPEN, IcWindow);
}
static void update_menu_checks(HMENU m) {
    CheckMenuItem(m, ID_OPT_CLOSETRAY, g.settings.close_to_tray ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m, ID_OPT_MINTRAY, g.settings.minimize_to_tray ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m, ID_OPT_STARTPAUSED, g.settings.start_paused ? MF_CHECKED : MF_UNCHECKED);
    CheckMenuItem(m, ID_OPT_CONFIRMREMOVE, g.settings.confirm_remove ? MF_CHECKED : MF_UNCHECKED);
}

// ------------------------------------------------------------------ list view setup
struct ColDef { wchar_t const* title; int width; int fmt; };
static ColDef const kCols[ColCount] = {
    {L"Nome", 300, LVCFMT_LEFT}, {L"#", 44, LVCFMT_RIGHT}, {L"Dimensione", 78, LVCFMT_RIGHT},
    {L"Completato", 96, LVCFMT_LEFT}, {L"Stato", 96, LVCFMT_LEFT}, {L"↓ Velocità", 82, LVCFMT_RIGHT},
    {L"↑ Velocità", 82, LVCFMT_RIGHT}, {L"Tempo", 64, LVCFMT_RIGHT}, {L"Seed", 66, LVCFMT_RIGHT},
    {L"Peer", 66, LVCFMT_RIGHT}, {L"Ratio", 56, LVCFMT_RIGHT}, {L"Obiettivo", 70, LVCFMT_LEFT},
    {L"Scaricati", 80, LVCFMT_RIGHT}, {L"Caricati", 80, LVCFMT_RIGHT}, {L"Aggiunto", 110, LVCFMT_LEFT},
    {L"Salute", 76, LVCFMT_LEFT},
};
static void add_columns(HWND lv, std::vector<std::pair<std::wstring, int>> const& cols) {
    LVCOLUMNW c{};
    c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    for (int i = 0; i < static_cast<int>(cols.size()); ++i) {
        c.pszText = const_cast<wchar_t*>(cols[static_cast<std::size_t>(i)].first.c_str());
        c.cx = scale(std::abs(cols[static_cast<std::size_t>(i)].second));
        c.fmt = cols[static_cast<std::size_t>(i)].second < 0 ? LVCFMT_RIGHT : LVCFMT_LEFT;
        ListView_InsertColumn(lv, i, &c);
    }
}
static HWND make_listview(HWND parent, DWORD extra_style, int id) {
    HWND lv = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | LVS_REPORT | LVS_SHOWSELALWAYS | extra_style,
        0, 0, 10, 10, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g.inst, nullptr);
    ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP | LVS_EX_HEADERDRAGDROP);
    SendMessageW(lv, CCM_SETUNICODEFORMAT, TRUE, 0);
    SetWindowTheme(lv, L"Explorer", nullptr);
    SendMessageW(lv, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
    return lv;
}
static bool valid_column_order(std::vector<int> const& order) {
    if (order.size() != ColCount) return false;
    bool seen[ColCount]{};
    for (int column : order) {
        if (column < 0 || column >= ColCount || seen[column]) return false;
        seen[column] = true;
    }
    return true;
}
static void load_column_widths() {
    // "w0,w1,...;o0,o1,..." widths in 96-DPI pixels and column order
    std::wstring s = widen(g.settings.columns);
    auto semi = s.find(L';');
    std::wstring widths = s.substr(0, semi), order = semi == std::wstring::npos ? L"" : s.substr(semi + 1);
    int i = 0;
    std::size_t pos = 0;
    while (pos < widths.size() && i < ColCount) {
        auto comma = widths.find(L',', pos);
        long long width;
        if (parse_int(widths.substr(pos, comma - pos), width) && width > 0 && width <= 4096) {
            if (i == ColQueue) width = std::max<long long>(width, 44);   // room for "#" and the sort arrow
            ListView_SetColumnWidth(g.list, i, scale(static_cast<int>(width)));
        }
        ++i;
        if (comma == std::wstring::npos) break;
        pos = comma + 1;
    }
    std::vector<int> ord;
    pos = 0;
    while (pos < order.size()) {
        auto comma = order.find(L',', pos);
        long long column;
        if (!parse_int(order.substr(pos, comma - pos), column) || column < 0 || column >= ColCount) {
            ord.clear();
            break;
        }
        ord.push_back(static_cast<int>(column));
        if (comma == std::wstring::npos) break;
        pos = comma + 1;
    }
    if (ord.size() == ColCount - 1) {   // older settings without the Salute column: insert it after Peer
        auto it = std::find(ord.begin(), ord.end(), static_cast<int>(ColPeers));
        ord.insert(it == ord.end() ? ord.end() : it + 1, ColHealth);
    }
    if (!valid_column_order(ord)) {   // default: queue number first, then the name
        ord.clear();
        ord.push_back(ColQueue);
        ord.push_back(ColName);
        for (int c = 0; c < ColCount; ++c) if (c != ColQueue && c != ColName && c != ColHealth) {
            ord.push_back(c);
            if (c == ColPeers) ord.push_back(ColHealth);
        }
    }
    ListView_SetColumnOrderArray(g.list, ColCount, ord.data());
}
static void save_column_widths() {
    std::wstring s;
    for (int i = 0; i < ColCount; ++i) {
        if (i) s += L',';
        s += std::to_wstring(MulDiv(ListView_GetColumnWidth(g.list, i), 96, static_cast<int>(g.dpi)));
    }
    int ord[ColCount];
    if (ListView_GetColumnOrderArray(g.list, ColCount, ord)) {
        s += L';';
        for (int i = 0; i < ColCount; ++i) { if (i) s += L','; s += std::to_wstring(ord[i]); }
    }
    g.settings.columns = narrow(s);
}
static void update_sort_arrow() {
    HWND hdr = ListView_GetHeader(g.list);
    for (int i = 0; i < ColCount; ++i) {
        HDITEMW it{};
        it.mask = HDI_FORMAT;
        Header_GetItem(hdr, i, &it);
        it.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (i == g.sort_col) it.fmt |= g.sort_desc ? HDF_SORTDOWN : HDF_SORTUP;
        Header_SetItem(hdr, i, &it);
    }
}

// ------------------------------------------------------------------ general panel
static std::vector<std::pair<std::wstring, std::wstring>> general_items;
static std::wstring general_title, general_error;
static double general_progress = 0;
static COLORREF general_color = paint::kGreen;
static paint::Bar general_kind = paint::Bar::Solid;
static std::wstring general_bar_text;

static LRESULT CALLBACK general_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HGDIOBJ oldb = SelectObject(mem, bmp);
        FillRect(mem, &rc, GetSysColorBrush(COLOR_WINDOW));
        SetBkMode(mem, TRANSPARENT);
        int pad = scale(10), y = scale(8);
        HGDIOBJ oldf = SelectObject(mem, g.bold);
        SetTextColor(mem, GetSysColor(COLOR_WINDOWTEXT));
        RECT tr{pad, y, std::max(pad, static_cast<int>(rc.right) - pad - (general_items.empty() ? 0 : scale(230))), y + scale(20)};
        DrawTextW(mem, general_title.c_str(), -1, &tr, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX | DT_VCENTER);
        if (!general_items.empty()) {
            RECT pr{rc.right - pad - scale(220), y, rc.right - pad, y + scale(20)};
            SelectObject(mem, g.font);
            SetTextColor(mem, general_color);
            DrawTextW(mem, general_bar_text.c_str(), -1, &pr, DT_SINGLELINE | DT_RIGHT | DT_VCENTER | DT_NOPREFIX);
            y += scale(24);
            RECT bar{pad, y, rc.right - pad, y + scale(8)};
            paint::progress_bar(mem, bar, general_progress, general_color, general_kind, g.anim_phase, L"", g.font);
            y += scale(14);
        } else y += scale(26);
        // key/value grid in three column pairs
        int cols = rc.right > scale(900) ? 3 : rc.right > scale(560) ? 2 : 1;
        int colw = (rc.right - 2 * pad) / cols;
        int keyw = scale(118), lineh = scale(19);
        for (std::size_t i = 0; i < general_items.size(); ++i) {
            int c = static_cast<int>(i % static_cast<std::size_t>(cols));
            int r = static_cast<int>(i / static_cast<std::size_t>(cols));
            int x = pad + c * colw, yy = y + r * lineh;
            SelectObject(mem, g.font);
            SetTextColor(mem, RGB(95, 107, 120));
            RECT k{x, yy, x + keyw, yy + lineh};
            DrawTextW(mem, general_items[i].first.c_str(), -1, &k, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            SetTextColor(mem, GetSysColor(COLOR_WINDOWTEXT));
            RECT v{x + keyw, yy, x + colw - scale(12), yy + lineh};
            DrawTextW(mem, general_items[i].second.c_str(), -1, &v, DT_SINGLELINE | DT_PATH_ELLIPSIS | DT_NOPREFIX);
        }
        if (!general_error.empty()) {
            int rows = static_cast<int>((general_items.size() + static_cast<std::size_t>(cols) - 1) / static_cast<std::size_t>(cols));
            RECT er{pad, y + rows * lineh + scale(4), rc.right - pad, rc.bottom};
            SetTextColor(mem, RGB(198, 40, 40));
            DrawTextW(mem, general_error.c_str(), -1, &er, DT_WORDBREAK | DT_NOPREFIX);
        }
        SelectObject(mem, oldf);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldb);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE: InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_CONTEXTMENU: {
        auto ids = selected_ids();
        if (ids.empty()) return 0;
        DetailsView d;
        Row const* r = row_by_id(ids[0]);
        if (!r || !g.engine.details(ids[0], d)) return 0;
        HMENU m = CreatePopupMenu();
        AppendMenuW(m, MF_STRING, 1, L"Copia info hash");
        AppendMenuW(m, MF_STRING, 2, L"Copia percorso");
        AppendMenuW(m, MF_STRING, 3, L"Copia nome");
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (pt.x == -1) GetCursorPos(&pt);
        int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
        DestroyMenu(m);
        std::wstring text = cmd == 1 ? widen(d.hash_v1.empty() ? d.hash_v2 : d.hash_v1)
                          : cmd == 2 ? widen(r->v.save_path) : cmd == 3 ? widen(r->v.name) : L"";
        if (!text.empty()) copy_to_clipboard(g.main, text);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ------------------------------------------------------------------ detail list sync
static std::map<HWND, std::vector<std::wstring>> lv_keys;
// Updates a report list view in place: keeps selection/scroll when the keys are unchanged.
static void sync_listview(HWND lv, std::vector<std::wstring> const& keys, std::vector<std::vector<std::wstring>> const& cells,
                          std::vector<int> const& images = {}) {
    auto& old = lv_keys[lv];
    SendMessageW(lv, WM_SETREDRAW, FALSE, 0);
    if (old != keys) {
        std::set<std::wstring> selected;
        for (int i = -1; (i = ListView_GetNextItem(lv, i, LVNI_SELECTED)) >= 0;)
            if (i < static_cast<int>(old.size())) selected.insert(old[static_cast<std::size_t>(i)]);
        int top = ListView_GetTopIndex(lv);
        ListView_DeleteAllItems(lv);
        for (std::size_t i = 0; i < keys.size(); ++i) {
            LVITEMW it{};
            it.mask = LVIF_TEXT | (images.empty() ? 0 : LVIF_IMAGE);
            it.iItem = static_cast<int>(i);
            it.pszText = const_cast<wchar_t*>(cells[i][0].c_str());
            it.iImage = images.empty() ? 0 : images[i];
            ListView_InsertItem(lv, &it);
            for (std::size_t c = 1; c < cells[i].size(); ++c)
                ListView_SetItemText(lv, static_cast<int>(i), static_cast<int>(c), const_cast<wchar_t*>(cells[i][c].c_str()));
            if (selected.count(keys[i])) ListView_SetItemState(lv, static_cast<int>(i), LVIS_SELECTED, LVIS_SELECTED);
        }
        if (top > 0 && top < static_cast<int>(keys.size())) ListView_EnsureVisible(lv, std::min(static_cast<int>(keys.size()) - 1, top + ListView_GetCountPerPage(lv) - 1), FALSE);
        old = keys;
    } else {
        wchar_t buf[1024];
        for (std::size_t i = 0; i < keys.size(); ++i) {
            for (std::size_t c = 0; c < cells[i].size(); ++c) {
                ListView_GetItemText(lv, static_cast<int>(i), static_cast<int>(c), buf, 1024);
                if (cells[i][c] != buf) ListView_SetItemText(lv, static_cast<int>(i), static_cast<int>(c), const_cast<wchar_t*>(cells[i][c].c_str()));
            }
            if (!images.empty()) {
                LVITEMW it{}; it.mask = LVIF_IMAGE; it.iItem = static_cast<int>(i);
                ListView_GetItem(lv, &it);
                if (it.iImage != images[i]) { it.iImage = images[i]; ListView_SetItem(lv, &it); }
            }
        }
    }
    SendMessageW(lv, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(lv, nullptr, FALSE);
}
static void clear_listview(HWND lv) { lv_keys[lv].clear(); ListView_DeleteAllItems(lv); }

static int current_tab() { return TabCtrl_GetCurSel(g.tabs); }
static int detail_id() {
    int i = ListView_GetNextItem(g.list, -1, LVNI_FOCUSED);
    if (i < 0 || ListView_GetItemState(g.list, i, LVIS_SELECTED) == 0) i = ListView_GetNextItem(g.list, -1, LVNI_SELECTED);
    if (i < 0 || i >= static_cast<int>(g.visible.size())) return 0;
    return g.rows[static_cast<std::size_t>(g.visible[static_cast<std::size_t>(i)])].v.id;
}

static void refresh_general(int id) {
    general_items.clear();
    general_error.clear();
    Row const* r = id ? row_by_id(id) : nullptr;
    if (!r) { general_title = L"Seleziona un torrent"; general_progress = 0; InvalidateRect(g.general, nullptr, FALSE); return; }
    auto const& v = r->v;
    DetailsView d;
    g.engine.details(id, d);
    general_title = widen(v.name);
    BarLook look = bar_look(*r);
    general_progress = look.fraction;
    general_kind = look.kind;
    general_bar_text = look.text;
    general_color = r->color;
    std::int64_t remaining = std::max<std::int64_t>(0, v.total_wanted - v.total_wanted_done);
    std::wstring eta = is_complete(v) || v.paused ? L"—" : v.download_rate > 0 ? fmt_duration(remaining / v.download_rate) : L"∞";
    auto const& meta = meta_for(v.hash);
    double limit = 0; int action = 0;
    std::wstring target = ratio_target(meta, limit, action) ? fmt_double(limit, 2) + L" → " + kRatioActionLabels[action] : L"nessuno";
    auto add = [&](wchar_t const* k, std::wstring val) { general_items.emplace_back(k, std::move(val)); };
    add(L"Stato", r->state);
    add(L"Dimensione", v.has_metadata ? fmt_bytes(v.total_wanted) : L"in attesa dei metadati");
    add(L"Tempo rimanente", eta);
    add(L"Scaricati", fmt_bytes(v.all_time_download));
    add(L"Caricati", fmt_bytes(v.all_time_upload));
    add(L"Ratio", fmt_ratio(r->ratio, v.all_time_download || v.all_time_upload || v.total_done));
    add(L"Obiettivo ratio", target);
    add(L"Velocità", L"↓ " + fmt_speed0(v.download_rate) + L"   ↑ " + fmt_speed0(v.upload_rate));
    add(L"Seed / Peer", std::to_wstring(v.seeds) + L" (" + std::to_wstring(v.list_seeds) + L") / " + std::to_wstring(v.peers) + L" (" + std::to_wstring(v.list_peers) + L")");
    auto hl = paint::health(v);
    add(L"Salute", hl.level ? std::wstring(hl.label) + L" (" + fmt_double(hl.copies, 2) + L" copie)" : std::wstring(hl.label));
    add(L"Disponibilità", v.distributed_copies >= 0 ? fmt_double(v.distributed_copies, 2) : L"—");
    add(L"Connessioni", std::to_wstring(v.connections) + L" (upload a " + std::to_wstring(d.uploads) + L")");
    add(L"Scartati", fmt_bytes(d.wasted));
    add(L"Aggiunto", fmt_date(v.added));
    add(L"Completato il", fmt_date(v.completed));
    add(L"Attivo / in seed", fmt_duration(v.active_seconds) + L" / " + fmt_duration(v.seeding_seconds));
    add(L"Pezzi", d.num_pieces ? std::to_wstring(d.pieces_have) + L" / " + std::to_wstring(d.num_pieces) + L" × " + fmt_bytes(d.piece_length) : L"—");
    add(L"Super-seeding", v.super_seeding ? L"attivo" : L"disattivato");
    add(L"Limiti", (v.down_limit ? L"↓ " + fmt_speed(v.down_limit) : std::wstring(L"↓ ∞")) + L"  " + (v.up_limit ? L"↑ " + fmt_speed(v.up_limit) : std::wstring(L"↑ ∞")));
    add(L"Tracker attuale", d.current_tracker.empty() ? L"—" : widen(d.current_tracker));
    add(L"Prossimo annuncio", d.next_announce > 0 ? fmt_duration(d.next_announce) : L"—");
    add(L"Privato", d.is_private ? L"sì (DHT/PeX/LSD disattivati)" : L"no");
    add(L"Percorso", widen(v.save_path));
    add(L"Info hash", widen(d.hash_v1.empty() ? d.hash_v2 : d.hash_v1));
    add(L"Creato da", d.creator.empty() ? L"—" : widen(d.creator) + (d.creation_date ? L", " + fmt_date(d.creation_date) : L""));
    if (!d.comment.empty()) add(L"Commento", widen(d.comment));
    if (!v.error.empty()) general_error = L"Errore: " + widen(v.error);
    InvalidateRect(g.general, nullptr, FALSE);
}
static void refresh_trackers(int id) {
    if (!id) { clear_listview(g.trackers); return; }
    auto list = g.engine.trackers(id);
    std::vector<std::wstring> keys;
    std::vector<std::vector<std::wstring>> cells;
    std::vector<int> images;
    auto num = [](int v) { return v >= 0 ? fmt_int(v) : std::wstring(L"—"); };
    for (auto const& t : list) {
        keys.push_back(widen(t.url));
        cells.push_back({widen(t.url), std::to_wstring(t.tier), tr_status(t.status), num(t.seeds), num(t.peers), num(t.downloaded),
            t.next_announce >= 0 ? fmt_duration(t.next_announce) : L"",
            t.status == 5 && t.message.empty() ? std::wstring(L"Non necessario: un altro tracker risponde") : widen(t.message)});
        images.push_back(t.status == 1 ? 5 : t.status == 2 ? 6 : t.status == 3 ? 7 : t.status == 4 ? 9 : 8);
    }
    // Pseudo rows for the trackerless sources (like uTorrent/qBittorrent).
    DetailsView d;
    g.engine.details(id, d);
    auto pseudo = [&](wchar_t const* name, bool on) {
        keys.push_back(name);
        bool enabled = on && !d.is_private;
        cells.push_back({name, L"", enabled ? L"Attivo" : L"Disattivato", L"", L"", L"", L"",
            d.is_private ? L"Torrent privato" : (on ? L"" : L"Disattivato in Preferenze > BitTorrent")});
        images.push_back(enabled ? 5 : 9);
    };
    if (!list.empty() && list.front().ignored_endpoints > 0) {
        keys.push_back(L"** [indirizzi locali] **");
        cells.push_back({L"** [indirizzi locali] **", L"", L"Ignorati", L"", L"", L"", L"",
            std::to_wstring(list.front().ignored_endpoints) + L" indirizzi locali (adattatori virtuali, VPN o IPv6 senza connettivit\u00E0) non ricevono risposta dai tracker: non sono considerati errori."});
        images.push_back(9);
    }
    pseudo(L"** [DHT] **", g.settings.dht);
    pseudo(L"** [PeX] **", g.engine.network().pex);
    pseudo(L"** [LSD] **", g.settings.lsd);
    sync_listview(g.trackers, keys, cells, images);
}
static void refresh_peers(int id) {
    if (!id) { clear_listview(g.peers); return; }
    auto list = g.engine.peers(id);
    std::sort(list.begin(), list.end(), [](PeerView const& a, PeerView const& b) { return a.down + a.up > b.down + b.up; });
    std::vector<std::wstring> keys;
    std::vector<std::vector<std::wstring>> cells;
    for (auto const& p : list) {
        std::wstring flags = std::wstring(p.incoming ? L"I" : L"O") + (p.encrypted ? L"E" : L"") + (p.utp ? L"P" : L"") + (p.seed ? L"S" : L"");
        keys.push_back(widen(p.address) + L":" + std::to_wstring(p.port));
        cells.push_back({widen(p.address), widen(p.client), flags, fmt_double(p.progress * 100, 1) + L"%",
            fmt_speed(p.down), fmt_speed(p.up), fmt_bytes(p.total_down), fmt_bytes(p.total_up)});
    }
    sync_listview(g.peers, keys, cells);
}
static wchar_t const* prio_label(int p) { return p == 0 ? L"Non scaricare" : p <= 1 ? L"Bassa" : p >= 7 ? L"Alta" : L"Normale"; }
static std::vector<FileView> last_files;
static void refresh_files(int id) {
    if (!id) { clear_listview(g.files); last_files.clear(); return; }
    last_files = g.engine.files(id);
    std::vector<std::wstring> keys;
    std::vector<std::vector<std::wstring>> cells;
    for (auto const& f : last_files) {
        keys.push_back(std::to_wstring(f.index));
        double pct = f.size > 0 ? 100.0 * static_cast<double>(f.done) / static_cast<double>(f.size) : 100.0;
        cells.push_back({widen(f.path), fmt_bytes(f.size), fmt_bytes(f.done), fmt_double(pct, 1) + L"%", prio_label(f.priority)});
    }
    sync_listview(g.files, keys, cells);
}
static int shown_detail_id = -1;
static void refresh_details(bool force) {
    int id = detail_id();
    bool changed = id != shown_detail_id;
    shown_detail_id = id;
    switch (current_tab()) {
        case 0: refresh_general(id); break;
        case 1: if (force || changed || g.ticks % 2 == 0) refresh_trackers(id); break;
        case 2: if (force || changed || g.ticks % 2 == 0) refresh_peers(id); break;
        case 3: if (force || changed || g.ticks % 3 == 0) refresh_files(id); break;
        case 5: InvalidateRect(g.chart, nullptr, FALSE); break;
        default: break;
    }
}
static void show_tab(int tab) {
    ShowWindow(g.general, tab == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(g.tracker_bar, tab == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(g.trackers, tab == 1 ? SW_SHOW : SW_HIDE);
    ShowWindow(g.peers, tab == 2 ? SW_SHOW : SW_HIDE);
    ShowWindow(g.files, tab == 3 ? SW_SHOW : SW_HIDE);
    ShowWindow(g.log, tab == 4 ? SW_SHOW : SW_HIDE);
    ShowWindow(g.chart, tab == 5 ? SW_SHOW : SW_HIDE);
    refresh_details(true);
}

// ------------------------------------------------------------------ filters
static wchar_t const* const kFilterNames[] = {L"Tutti", L"In download", L"In seeding", L"Completati", L"Attivi", L"In pausa", L"Con errori"};
static bool passes(int filter, TorrentView const& v) {
    switch (filter) {
        case 1: return !is_complete(v) && !(v.paused && !v.auto_managed);
        case 2: return v.seeding && !v.paused;
        case 3: return is_complete(v);
        case 4: return v.download_rate > 1024 || v.upload_rate > 1024;
        case 5: return v.paused && !v.auto_managed;
        case 6: return v.has_error;
        default: return true;
    }
}
static void update_filter_counts() {
    int counts[7] = {};
    for (auto const& r : g.rows) for (int f = 0; f < 7; ++f) if (passes(f, r.v)) ++counts[f];
    for (int f = 0; f < 7; ++f) {
        std::wstring text = std::wstring(kFilterNames[f]) + L" (" + std::to_wstring(counts[f]) + L")";
        wchar_t buf[64];
        ListView_GetItemText(g.filters, f, 0, buf, 64);
        if (text != buf) ListView_SetItemText(g.filters, f, 0, const_cast<wchar_t*>(text.c_str()));
    }
}

// ------------------------------------------------------------------ sorting / visible rows
static std::wstring cell_text(Row const& r, int col) {
    auto const& v = r.v;
    switch (col) {
        case ColQueue: return r.number > 0 ? std::to_wstring(r.number) : L"";
        case ColName: return v.name.empty() ? L"(recupero metadati…) " + widen(v.hash) : widen(v.name);
        case ColSize: return v.has_metadata ? fmt_bytes(v.total_wanted) : L"?";
        case ColProgress: return fmt_double(v.progress * 100.0, 1) + L"%";
        case ColState: return r.state;
        case ColDown: return fmt_speed(v.download_rate);
        case ColUp: return fmt_speed(v.upload_rate);
        case ColEta: {
            if (is_complete(v) || v.paused) return L"";
            std::int64_t remaining = std::max<std::int64_t>(0, v.total_wanted - v.total_wanted_done);
            return v.download_rate > 0 ? fmt_duration(remaining / v.download_rate) : L"∞";
        }
        case ColSeeds: return std::to_wstring(v.seeds) + L" (" + std::to_wstring(std::max(0, v.list_seeds)) + L")";
        case ColPeers: return std::to_wstring(v.peers) + L" (" + std::to_wstring(std::max(0, v.list_peers)) + L")";
        case ColRatio: return fmt_ratio(r.ratio, v.all_time_download || v.all_time_upload || v.total_done);
        case ColTarget: return r.target < 0 ? L"—" : fmt_double(r.target, 2);
        case ColDownloaded: return fmt_bytes(v.all_time_download);
        case ColUploaded: return fmt_bytes(v.all_time_upload);
        case ColAdded: return fmt_date(v.added);
        case ColHealth: return paint::health(v).label;
        default: return L"";
    }
}
static bool less_by(Row const& a, Row const& b, int col) {
    auto const &x = a.v, &y = b.v;
    switch (col) {
        case ColQueue: return a.number != b.number ? a.number < b.number : x.id < y.id;
        case ColName: return _wcsicmp(widen(x.name).c_str(), widen(y.name).c_str()) < 0;
        case ColSize: return x.total_wanted < y.total_wanted;
        case ColProgress: return x.progress < y.progress;
        case ColState: return a.state < b.state;
        case ColDown: return x.download_rate < y.download_rate;
        case ColUp: return x.upload_rate < y.upload_rate;
        case ColEta: {
            auto eta = [](TorrentView const& v) { return v.download_rate > 0 ? (v.total_wanted - v.total_wanted_done) / v.download_rate : (std::int64_t(1) << 50); };
            return eta(x) < eta(y);
        }
        case ColSeeds: return x.seeds != y.seeds ? x.seeds < y.seeds : x.list_seeds < y.list_seeds;
        case ColPeers: return x.peers != y.peers ? x.peers < y.peers : x.list_peers < y.list_peers;
        case ColRatio: return a.ratio < b.ratio;
        case ColTarget: return a.target < b.target;
        case ColDownloaded: return x.all_time_download < y.all_time_download;
        case ColUploaded: return x.all_time_upload < y.all_time_upload;
        case ColAdded: return x.added < y.added;
        case ColHealth: return paint::health(x).copies < paint::health(y).copies;
        default: return x.id < y.id;
    }
}
static bool rebuilding = false;
static void rebuild_visible() {
    rebuilding = true;
    auto selected = selected_ids();
    int focused_id = detail_id();
    g.visible.clear();
    for (std::size_t i = 0; i < g.rows.size(); ++i) if (passes(g.filter, g.rows[i].v)) g.visible.push_back(static_cast<int>(i));
    std::stable_sort(g.visible.begin(), g.visible.end(), [](int a, int b) {
        auto const &ra = g.rows[static_cast<std::size_t>(a)], &rb = g.rows[static_cast<std::size_t>(b)];
        return g.sort_desc ? less_by(rb, ra, g.sort_col) : less_by(ra, rb, g.sort_col);
    });
    ListView_SetItemCountEx(g.list, static_cast<int>(g.visible.size()), LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);
    // restore selection by id (virtual list views keep it by index)
    ListView_SetItemState(g.list, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    for (std::size_t i = 0; i < g.visible.size(); ++i) {
        int id = g.rows[static_cast<std::size_t>(g.visible[i])].v.id;
        UINT state = 0;
        if (std::find(selected.begin(), selected.end(), id) != selected.end()) state |= LVIS_SELECTED;
        if (id == focused_id) state |= LVIS_FOCUSED;
        if (state) ListView_SetItemState(g.list, static_cast<int>(i), state, state);
    }
    InvalidateRect(g.list, nullptr, FALSE);
    rebuilding = false;
}

// ------------------------------------------------------------------ ratio actions
static void apply_ratio_rules() {
    bool changed = false;
    for (auto const& r : g.rows) {
        auto const& v = r.v;
        if (!is_complete(v) || v.has_error) continue;
        auto& meta = meta_for(v.hash);
        double limit = 0; int action = 0;
        if (meta.ratio_done || !ratio_target(meta, limit, action) || r.ratio < limit) continue;
        std::wstring name = widen(v.name), err;
        std::string e8;
        switch (action) {
            case ActPause:
                if (!(v.paused && !v.auto_managed)) g.engine.pause(v.id, true);
                log_line(L"Ratio " + fmt_double(r.ratio, 2) + L" ≥ " + fmt_double(limit, 2) + L": in pausa → " + name);
                meta.ratio_done = true; changed = true;
                break;
            case ActSuperSeed:
                if (!v.seeding || v.paused) continue; // retry when it seeds again
                if (!v.super_seeding && !g.engine.super_seed(v.id, true, e8)) { log_line(L"Super-seeding non attivato: " + widen(e8)); }
                else log_line(L"Ratio " + fmt_double(r.ratio, 2) + L" raggiunto: Super-seeding → " + name);
                meta.ratio_done = true; changed = true;
                break;
            case ActRemove:
            case ActRemoveDelete:
                if (g.engine.remove(v.id, action == ActRemoveDelete)) {
                    log_line(L"Ratio " + fmt_double(r.ratio, 2) + L" raggiunto: " + (action == ActRemoveDelete ? L"rimosso e file cancellati → " : L"rimosso → ") + name);
                    g.settings.torrents.erase(v.hash);
                    changed = true;
                }
                break;
            default: break;
        }
    }
    if (changed) persist();
}

// ------------------------------------------------------------------ status bar / tray
// Panes: 0 network, 1 DHT, 2 download speed, 3 upload speed, 4 totals, 5 RAM.
// Each can be hidden from Preferences > General (settings.status_panes, one bit per pane);
// the status bar parts are the visible panes in this order.
static void tray_update_tip(std::int64_t down, std::int64_t up);
static std::array<std::wstring, 6> status_texts;   // per pane
static std::vector<int> status_need;                // per visible part
static int status_mask = -1;                        // panes currently laid out
static void fit_status_panes();
static void layout();
static bool pane_shown(int pane) { return (g.settings.status_panes >> pane) & 1; }
static int pane_count(int mask) { int n = 0; for (int p = 0; p < 6; ++p) n += (mask >> p) & 1; return n; }
static int status_part(int pane) {                 // part index of a pane, -1 when hidden
    if (pane < 0 || pane > 5 || !pane_shown(pane)) return -1;
    int part = 0;
    for (int p = 0; p < pane; ++p) part += pane_shown(p);
    return part;
}
static int status_pane_at(int part) {
    for (int p = 0; p < 6; ++p) if (pane_shown(p) && part-- == 0) return p;
    return -1;
}
static int status_height() {
    // the style bit, not IsWindowVisible: the main window may be hidden in the tray
    if (!g.status || !(GetWindowLongW(g.status, GWL_STYLE) & WS_VISIBLE)) return 0;
    RECT sr; GetWindowRect(g.status, &sr);
    return sr.bottom - sr.top;
}
static void set_pane_text(int pane, std::wstring const& text, std::wstring const* tip = nullptr) {
    int const part = status_part(pane);
    if (part < 0) return;
    SendMessageW(g.status, SB_SETTEXTW, part, reinterpret_cast<LPARAM>(text.c_str()));
    if (tip) SendMessageW(g.status, SB_SETTIPTEXTW, part, reinterpret_cast<LPARAM>(tip->c_str()));
}
static void update_status(std::int64_t down, std::int64_t up) {
    std::wstring dht = L"DHT: " + (g.settings.dht ? fmt_int(g.engine.counters().dht_nodes) + L" nodi" : std::wstring(L"disattivato"));
    auto limit = [](int kib) { return kib ? L" [max " + fmt_speed(static_cast<std::int64_t>(kib) * 1024) + L"]" : std::wstring(); };
    std::wstring d = L"Velocità download: " + fmt_speed0(down) + limit(g.settings.down_kib);
    std::wstring u = L"Velocità upload: " + fmt_speed0(up) + limit(g.settings.up_kib);
    std::wstring totals = L"Totale scaricati " + fmt_bytes(g.settings.stats.downloaded) + L"  caricati " + fmt_bytes(g.settings.stats.uploaded) +
        L"  ratio " + fmt_ratio(effective_ratio(g.settings.stats.downloaded, g.settings.stats.uploaded, 0), g.settings.stats.downloaded || g.settings.stats.uploaded);
    auto mem = memory_usage();
    std::wstring ram = L"RAM " + fmt_bytes(static_cast<std::int64_t>(mem.working_set));
    status_texts = {g.net_text, dht, d, u, totals, ram};
    fit_status_panes();   // applies a change of the visible panes first
    for (int pane = 1; pane < 6; ++pane) set_pane_text(pane, status_texts[pane], &status_texts[pane]);
    tray_update_tip(down, up);
}
// Network pane text, icon and tooltip (also re-applied after the visible panes change).
static void show_network_pane() {
    int const part = status_part(0);
    if (part < 0) return;
    SendMessageW(g.status, SB_SETICON, part, reinterpret_cast<LPARAM>(g.net_icon));
    SendMessageW(g.status, SB_SETTEXTW, part, reinterpret_cast<LPARAM>(g.net_text.c_str()));
    SendMessageW(g.status, SB_SETTIPTEXTW, part, reinterpret_cast<LPARAM>(g.net_tip.c_str()));
}
static void update_network_indicator() {
    auto n = g.engine.network();
    std::wstring text, tip;
    NetState st = evaluate_network(n, g.port_check, text, tip);
    if (st != g.net_state || g.net_icon == nullptr || text != g.net_text) {
        COLORREF color = st == NetState::Open ? RGB(46, 125, 50) : st == NetState::Firewalled ? RGB(239, 108, 0)
                       : st == NetState::Starting ? RGB(120, 144, 156) : RGB(198, 40, 40);
        HICON old = g.net_icon;
        g.net_icon = icons::make_icon(icons::IcGlobe, scale(16), color);
        g.net_state = st;
        g.net_text = text;
        g.net_tip = tip;
        show_network_pane();
        if (old) DestroyIcon(old);
    } else if (tip != g.net_tip) {
        g.net_tip = tip;
        int const part = status_part(0);
        if (part >= 0) SendMessageW(g.status, SB_SETTIPTEXTW, part, reinterpret_cast<LPARAM>(g.net_tip.c_str()));
    }
}
// Status bar parts sized from the text they show (need[i] = text width + padding).
// Every part gets what it needs; `flex` takes the spare room. When the window is too
// narrow, `shrink_first` gives up to half of its width first, then all parts shrink
// proportionally. Returns the right edges for SB_SETPARTS (the last one is -1).
std::vector<int> status_part_edges(int width, std::vector<int> const& need, int flex, int shrink_first) {
    int const available = std::max(0, width);
    std::vector<int> w = need;
    int const n = static_cast<int>(w.size());
    if (n == 0) return {};
    int total = 0;
    for (int v : w) total += v;
    if (total > available && shrink_first >= 0 && shrink_first < n) {
        int const cut = std::min(total - available, w[static_cast<std::size_t>(shrink_first)] / 2);
        w[static_cast<std::size_t>(shrink_first)] -= cut; total -= cut;
    }
    if (total > available && total > 0) {
        for (auto& v : w) v = MulDiv(v, available, total);
        total = 0;
        for (int v : w) total += v;
    }
    if (flex >= 0 && flex < n) w[static_cast<std::size_t>(flex)] += std::max(0, available - total);
    std::vector<int> edges(w.size());
    int x = 0;
    for (int i = 0; i + 1 < n; ++i) { x += w[static_cast<std::size_t>(i)]; edges[static_cast<std::size_t>(i)] = std::min(x, available); }
    edges.back() = -1;
    return edges;
}
static void layout_status() {
    if (!g.status || status_need.empty()) return;
    RECT rc; GetClientRect(g.main, &rc);
    int const net = status_part(0), totals = status_part(4);
    auto const edges = status_part_edges(rc.right, status_need, net >= 0 ? net : static_cast<int>(status_need.size()) - 1, totals);
    SendMessageW(g.status, SB_SETPARTS, edges.size(), reinterpret_cast<LPARAM>(edges.data()));
}
// Measures the pane texts with the status bar font; relayouts only when a part needs
// more room or could give back a noticeable amount (avoids flicker every second).
static void fit_status_panes() {
    if (!g.status) return;
    int const mask = g.settings.status_panes & 0x3F;
    if (mask != status_mask) {                       // panes shown or hidden from the preferences
        bool const was_visible = status_mask != 0;
        status_mask = mask;
        status_need.clear();
        for (int i = 0; i < 6; ++i) SendMessageW(g.status, SB_SETICON, i, 0);
        ShowWindow(g.status, mask ? SW_SHOWNA : SW_HIDE);
        if (mask) {
            int const parts = pane_count(mask);
            std::vector<int> edges(static_cast<std::size_t>(parts), -1);
            SendMessageW(g.status, SB_SETPARTS, edges.size(), reinterpret_cast<LPARAM>(edges.data()));
            show_network_pane();
        }
        if (was_visible != (mask != 0) && g.main) layout();
    }
    if (!mask) return;
    HDC dc = GetDC(g.status);
    HGDIOBJ old = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(g.status, WM_GETFONT, 0, 0)));
    std::vector<int> need;
    int const last = status_pane_at(pane_count(mask) - 1);
    for (int pane = 0; pane < 6; ++pane) {
        if (!pane_shown(pane)) continue;
        SIZE sz{};
        auto const& text = status_texts[static_cast<std::size_t>(pane)];
        GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &sz);
        need.push_back(sz.cx + scale(14) + (pane == 0 ? scale(22) : 0) + (pane == last ? scale(18) : 0));   // icon / size grip
    }
    SelectObject(dc, old);
    ReleaseDC(g.status, dc);
    bool relayout = need.size() != status_need.size();
    for (std::size_t i = 0; !relayout && i < need.size(); ++i)
        if (need[i] > status_need[i] || status_need[i] - need[i] > scale(40)) relayout = true;
    if (relayout) { status_need = need; layout_status(); }
}

static NOTIFYICONDATAW tray_data() {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g.main;
    nid.uID = 1;
    nid.uVersion = NOTIFYICON_VERSION_4;
    return nid;
}
static void tray_add() {
    if (g.tray_visible) return;
    auto nid = tray_data();
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_APP_TRAY;
    nid.hIcon = g.app_icon_small;
    wcscpy_s(nid.szTip, L"SeedFlow");
    if (Shell_NotifyIconW(NIM_ADD, &nid)) {
        Shell_NotifyIconW(NIM_SETVERSION, &nid);
        g.tray_visible = true;
    }
}
static void tray_remove() {
    if (!g.tray_visible) return;
    auto nid = tray_data();
    Shell_NotifyIconW(NIM_DELETE, &nid);
    g.tray_visible = false;
}
static void tray_update_tip(std::int64_t down, std::int64_t up) {
    if (!g.tray_visible) return;
    auto nid = tray_data();
    nid.uFlags = NIF_TIP | NIF_SHOWTIP;
    std::wstring tip = L"SeedFlow — " + g.net_text + L"\nDownload " + fmt_speed0(down) + L"   Upload " + fmt_speed0(up);
    wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}
static void hide_to_tray() {
    tray_add();
    if (!g.tray_visible) return; // never hide without an icon to come back
    ShowWindow(g.main, SW_HIDE);
    g.hidden = true;
    if (g.settings.trim_when_hidden) trim_working_set();
}
static void restore_from_tray() {
    ShowWindow(g.main, SW_SHOW);
    if (IsIconic(g.main)) ShowWindow(g.main, SW_RESTORE);
    SetForegroundWindow(g.main);
    g.hidden = false;
    if (!g.settings.close_to_tray && !g.settings.minimize_to_tray) tray_remove();
    refresh_now();
}

// ------------------------------------------------------------------ refresh
static void update_animation();
static void update_queue_buttons();
void refresh_now() {
    g.current_down = g.current_up = 0;
    if (!g.engine.running()) return;
    g.engine.poll();
    for (auto const& e : g.engine.take_events()) log_line(widen(e));

    std::vector<TorrentView> views;
    g.engine.views(views);
    std::int64_t down = 0, up = 0;
    g.rows.clear();
    g.rows.reserve(views.size());
    for (auto& v : views) {
        Row r;
        describe(v, r);
        r.ratio = effective_ratio(v.all_time_download, v.all_time_upload, v.total_done);
        auto& meta = meta_for(v.hash);
        double limit = 0; int action = 0;
        r.target = ratio_target(meta, limit, action) ? limit : -1;
        down += v.download_rate; up += v.upload_rate;
        // completion statistics: only torrents seen downloading in this session
        if (!is_complete(v) && v.state == 3) g.was_downloading.insert(v.id);
        else if (is_complete(v) && g.was_downloading.erase(v.id)) { ++g.settings.stats.completed; }
        r.v = std::move(v);
        g.rows.push_back(std::move(r));
    }
    g.current_down = down; g.current_up = up;
    // # column: queued torrents keep their libtorrent position; torrents outside the
    // queue (seeding / finished) follow, in SeedFlow's own order (moved with the arrows,
    // date added for new ones). The order is renumbered 1..n and saved with the settings.
    {
        int queued = 0;
        std::vector<Row*> rest;
        for (auto& r : g.rows) {
            if (r.v.queue_position >= 0) { r.number = r.v.queue_position + 1; queued = std::max(queued, r.number); }
            else rest.push_back(&r);
        }
        std::sort(rest.begin(), rest.end(), [](Row const* a, Row const* b) {
            int const oa = meta_for(a->v.hash).order, ob = meta_for(b->v.hash).order;
            if ((oa > 0) != (ob > 0)) return oa > 0;            // ordered ones first, new ones after
            if (oa != ob) return oa < ob;
            return a->v.added != b->v.added ? a->v.added < b->v.added : a->v.id < b->v.id; });
        int index = 0;
        for (Row* r : rest) { r->number = ++queued; meta_for(r->v.hash).order = ++index; }
    }
    apply_ratio_rules();

    // global statistics from the session payload counters (includes removed torrents)
    auto c = g.engine.counters();
    if (g.last_recv >= 0 && c.recv_payload >= g.last_recv) g.settings.stats.downloaded += c.recv_payload - g.last_recv;
    if (g.last_sent >= 0 && c.sent_payload >= g.last_sent) g.settings.stats.uploaded += c.sent_payload - g.last_sent;
    if (c.recv_payload > 0 || c.sent_payload > 0 || g.last_recv < 0) { g.last_recv = c.recv_payload; g.last_sent = c.sent_payload; }
    g.settings.stats.peak_down = std::max<std::int64_t>(g.settings.stats.peak_down, down);
    g.settings.stats.peak_up = std::max<std::int64_t>(g.settings.stats.peak_up, up);

    if (!g.hidden) {
        rebuild_visible();
        update_filter_counts();
        refresh_details(false);
    }
    if (g.ticks % 5 == 0 || g.net_icon == nullptr) update_network_indicator();
    update_status(down, up);
    update_animation();
    update_queue_buttons();
}
// Animated progress cells (verifying / metadata / moving): a 100 ms timer runs only
// while at least one visible row needs it, and repaints just those cells.
static bool general_animated() {
    int id = detail_id();
    Row const* r = id ? row_by_id(id) : nullptr;
    return r && TabCtrl_GetCurSel(g.tabs) == 0 && bar_look(*r).animated;
}
static void update_animation() {
    bool need = false;
    if (!g.hidden && !IsIconic(g.main)) {
        for (int idx : g.visible) if (bar_look(g.rows[static_cast<std::size_t>(idx)]).animated) { need = true; break; }
        need = need || general_animated();
    }
    if (need != g.anim_running) {
        if (need) SetTimer(g.main, TIMER_ANIM, 100, nullptr); else KillTimer(g.main, TIMER_ANIM);
        g.anim_running = need;
    }
}
static void on_anim_timer() {
    ++g.anim_phase;
    int top = ListView_GetTopIndex(g.list), n = ListView_GetCountPerPage(g.list) + 1;
    for (int i = top; i < top + n && i < static_cast<int>(g.visible.size()); ++i) {
        if (!bar_look(g.rows[static_cast<std::size_t>(g.visible[static_cast<std::size_t>(i)])]).animated) continue;
        RECT rc;
        if (ListView_GetSubItemRect(g.list, i, ColProgress, LVIR_BOUNDS, &rc)) InvalidateRect(g.list, &rc, FALSE);
    }
    if (general_animated()) InvalidateRect(g.general, nullptr, FALSE);
}
static void consume_port_check() {
    if (!g.port_check_job) return;
    PortCheck result;
    if (!g.port_check_job->take(result)) return;
    g.port_check_job.reset();
    g.port_check_running = false;
    if (result.port != g.settings.port) {
        log_line(L"Verifica porta ignorata: la porta è cambiata durante il controllo.");
        update_network_indicator();
        return;
    }
    g.port_check = std::move(result);
    auto const& pc = g.port_check;
    if (pc.success) log_line(L"Verifica porta online: " + std::wstring(pc.reachable ? L"APERTA" : L"CHIUSA") + L" (IP " + widen(pc.ip) + L")");
    else log_line(L"Verifica porta online non riuscita: " + widen(pc.status));
    update_network_indicator();
}
// Speed samples for the "Grafico" tab: totals every second, per torrent for the torrents
// that moved data in this session (removed torrents are dropped once a minute).
static void record_usage() {
    std::uint64_t const now = GetTickCount64();
    if (g.last_sample_ms && now > g.last_sample_ms) {
        std::uint64_t const dt = std::min<std::uint64_t>(now - g.last_sample_ms, 5000);
        g.session_down += g.current_down * static_cast<std::int64_t>(dt) / 1000;
        g.session_up += g.current_up * static_cast<std::int64_t>(dt) / 1000;
    }
    g.last_sample_ms = now;
    g.usage.add(now, g.current_down, g.current_up);
    for (auto const& r : g.rows) {
        auto it = g.torrent_usage.find(r.v.id);
        if (it == g.torrent_usage.end()) {
            if (r.v.download_rate <= 0 && r.v.upload_rate <= 0) continue;
            it = g.torrent_usage.emplace(r.v.id, UsageHistory{}).first;
        }
        it->second.add(now, r.v.download_rate, r.v.upload_rate);
    }
    if (g.ticks % 60 == 0)
        for (auto it = g.torrent_usage.begin(); it != g.torrent_usage.end();)
            it = row_by_id(it->first) ? std::next(it) : g.torrent_usage.erase(it);
}
GraphView current_graph_view() {
    GraphView view;
    auto const& s = g.settings;
    view.now = GetTickCount64();
    view.range = std::clamp(s.graph_range, 0, kGraphRangeCount - 1);
    view.source = std::clamp(s.graph_source, 0, 1);
    view.show_down = s.graph_flags & 1; view.show_up = s.graph_flags & 2; view.show_limits = s.graph_flags & 4;
    view.hover_x = g.graph_hover_x;
    std::uint64_t const window = kGraphRangeMs[view.range];
    if (view.source == 0) {
        view.samples = &g.usage.level(g.usage.level_for(window));
        view.current_down = g.current_down; view.current_up = g.current_up;
        view.limit_down = static_cast<std::int64_t>(s.down_kib) * 1024;
        view.limit_up = static_cast<std::int64_t>(s.up_kib) * 1024;
        view.tile3_label = L"Sessione";
        view.tile3_value = fmt_bytes(g.session_down) + L" ↓ · " + fmt_bytes(g.session_up) + L" ↑";
        view.tile3_sub = L"ratio " + fmt_ratio(g.session_down > 0 ? static_cast<double>(g.session_up) / g.session_down : 0,
                                               g.session_down > 0 || g.session_up > 0) +
                         L" · da " + fmt_duration(static_cast<std::int64_t>(g.ticks));
        return view;
    }
    auto const ids = selected_ids();
    Row const* row = ids.empty() ? nullptr : row_by_id(ids.front());
    if (!row) { view.has_selection = false; view.tile3_label = L"Torrent"; view.tile3_value = L"—"; return view; }
    auto const& tv = row->v;
    static UsageHistory const empty;
    auto it = g.torrent_usage.find(tv.id);
    UsageHistory const& h = it == g.torrent_usage.end() ? empty : it->second;
    view.samples = &h.level(h.level_for(window));
    view.current_down = tv.download_rate; view.current_up = tv.upload_rate;
    view.limit_down = tv.down_limit; view.limit_up = tv.up_limit;
    view.tile3_label = widen(tv.name);
    view.tile3_value = fmt_bytes(tv.all_time_download) + L" ↓ · " + fmt_bytes(tv.all_time_upload) + L" ↑";
    wchar_t pct[16]; swprintf(pct, 16, L"%.1f%%", tv.progress * 100.0);
    view.tile3_sub = L"ratio " + fmt_ratio(row->ratio, tv.all_time_download > 0 || tv.all_time_upload > 0) + L" · " + pct;
    return view;
}
void graph_option_clicked(int part, int index) {
    auto& s = g.settings;
    switch (part) {
        case 0: s.graph_range = std::clamp(index, 0, kGraphRangeCount - 1); break;
        case 1: s.graph_source = std::clamp(index, 0, 1); break;
        case 2: s.graph_flags ^= 1; break;
        case 3: s.graph_flags ^= 2; break;
        case 4: s.graph_flags ^= 4; break;
        default: return;
    }
    persist();
}
static void on_timer() {
    consume_port_check();
    ++g.ticks;
    ++g.settings.stats.runtime;
    refresh_now();
    record_usage();
    if (!g.hidden && current_tab() == 5) InvalidateRect(g.chart, nullptr, FALSE);
    if (g.ticks % 60 == 0) persist();
    if (g.ticks % 300 == 0) g.engine.save_modified_resume();
    if (g.hidden && g.settings.trim_when_hidden && g.ticks % 600 == 0) trim_working_set();
}

// ------------------------------------------------------------------ adding torrents
static void after_add(int id) {
    if (id <= 0) return;
    ++g.settings.stats.added;
    std::string hash = g.engine.hash_of(id);
    if (!hash.empty()) meta_for(hash);
}
int add_created_torrent(std::wstring const& torrent_file, std::wstring const& save_dir, std::string& error) {
    AddResult r;
    int id = g.engine.add_torrent_file(narrow(torrent_file), narrow(save_dir), false, error, &r, true);
    if (id > 0) {
        after_add(id);
        log_line(L"In seeding: " + fs::path(torrent_file).filename().wstring());
        persist();
        refresh_now();
    }
    return id;
}
static void add_files(std::vector<std::wstring> const& files) {
    std::string folder = download_dir();
    bool paused = g.settings.start_paused;
    if (g.settings.ask_save_path) {
        // Summary for the save-path dialog: name and size of one torrent, or count and total.
        std::wstring summary;
        std::int64_t total = 0;
        int readable = 0;
        for (auto const& f : files) {
            std::string name, err; std::int64_t size = 0; int count = 0;
            if (Engine::peek_torrent(narrow(f), name, size, count, err)) {
                ++readable; total += size;
                if (files.size() == 1)
                    summary = widen(name) + L"\n" + fmt_bytes(size) + L" · " + std::to_wstring(count) + L" file";
            }
        }
        if (files.size() > 1) summary = std::to_wstring(files.size()) + L" torrent · " + fmt_bytes(total) + L" in totale";
        else if (!readable) summary = fs::path(files.front()).filename().wstring();
        bool dont_ask = false;
        if (!dlg_add_torrent(g.main, summary, folder, paused, dont_ask)) return;
        if (dont_ask) { g.settings.ask_save_path = false; log_line(L"Cartella di salvataggio: non verrà più chiesta (Preferenze › Generale per riattivare)."); }
        remember_dir(folder);
    }
    std::error_code ec;
    fs::create_directories(fs::path(widen(folder)), ec);
    for (auto const& f : files) {
        std::string err;
        AddResult r;
        int id = g.engine.add_torrent_file(narrow(f), folder, paused, err, &r);
        if (id > 0) { after_add(id); log_line(L"Aggiunto: " + fs::path(f).filename().wstring()); }
        else log_line(L"Non aggiunto (" + fs::path(f).filename().wstring() + L"): " + widen(err));
        if (id <= 0 && files.size() == 1)   // a duplicate is only information, other failures are warnings
            MessageBoxW(g.main, widen(err).c_str(), L"SeedFlow", r == AddResult::Duplicate ? MB_ICONINFORMATION : MB_ICONWARNING);
    }
    persist();
    refresh_now();
}
// "Sposta in un'altra cartella": choose the folder, then either move the data there or
// only point the torrents at it (data already moved by hand, followed by a recheck).
static void move_selected() {
    auto ids = selected_ids();
    if (ids.empty()) return;
    Row const* first = row_by_id(ids[0]);
    std::wstring folder = first ? widen(first->v.save_path) : widen(download_dir());
    if (!pick_folder(g.main, folder)) return;
    TASKDIALOG_BUTTON buttons[] = {
        {1001, L"Sposta i file nella nuova cartella\nSeedFlow copia i dati e poi li elimina dalla posizione attuale."},
        {1002, L"I file sono già nella nuova cartella\nAggiorna solo il percorso ed esegue una verifica dei dati."},
    };
    std::wstring const content = (ids.size() == 1 && first ? L"«" + widen(first->v.name) + L"»" : std::to_wstring(ids.size()) + L" torrent") +
                                 L"\nNuova cartella: " + folder;
    TASKDIALOGCONFIG tc{};
    tc.cbSize = sizeof(tc);
    tc.hwndParent = g.main;
    tc.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    tc.pszWindowTitle = L"Sposta in un'altra cartella";
    tc.pszMainIcon = TD_INFORMATION_ICON;
    tc.pszMainInstruction = L"Come vuoi cambiare il percorso?";
    tc.pszContent = content.c_str();
    tc.cButtons = 2;
    tc.pButtons = buttons;
    tc.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    int choice = 0;
    // TaskDialogIndirect exists only in Common Controls 6 (enabled by the manifest): resolve it at
    // run time so binaries without that manifest (the regression test) still load.
    using TaskDialogFn = HRESULT(WINAPI*)(TASKDIALOGCONFIG const*, int*, int*, BOOL*);
    auto task_dialog = reinterpret_cast<TaskDialogFn>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"comctl32.dll"), "TaskDialogIndirect")));
    if (!task_dialog || FAILED(task_dialog(&tc, &choice, nullptr, nullptr))) {
        int const r = MessageBoxW(g.main, (content + L"\n\nSì: sposta i file nella nuova cartella.\nNo: i file sono già lì, aggiorna solo il percorso e verifica.").c_str(),
                                  L"Sposta in un'altra cartella", MB_ICONQUESTION | MB_YESNOCANCEL);
        choice = r == IDYES ? 1001 : r == IDNO ? 1002 : 0;
    }
    if (choice != 1001 && choice != 1002) return;
    bool const move_files = choice == 1001;
    std::error_code ec;
    fs::create_directories(fs::path(folder), ec);
    for (int id : ids) {
        std::string err;
        if (!g.engine.move_storage(id, narrow(folder), move_files, err)) { log_line(L"Spostamento non avviato: " + widen(err)); continue; }
        if (!move_files) g.engine.recheck(id);
    }
    remember_dir(narrow(folder));
    log_line(move_files ? L"Spostamento dei file avviato verso " + folder : L"Percorso aggiornato a " + folder + L": verifica dei dati avviata.");
    persist();
    refresh_now();
}
static void add_magnets(std::vector<std::string> const& magnets, std::string const& folder, bool paused) {
    std::error_code ec;
    fs::create_directories(fs::path(widen(folder)), ec);
    for (auto const& m : magnets) {
        std::string err;
        AddResult r;
        int id = g.engine.add_magnet(m, folder, paused, err, &r);
        if (id > 0) { after_add(id); log_line(L"Magnet aggiunto"); }
        else {
            log_line(L"Magnet non aggiunto: " + widen(err));
            if (magnets.size() == 1) MessageBoxW(g.main, widen(err).c_str(), L"SeedFlow", r == AddResult::Duplicate ? MB_ICONINFORMATION : MB_ICONWARNING);
        }
    }
    persist();
    refresh_now();
}
static void handle_arguments(std::wstring const& text) {
    std::vector<std::wstring> files;
    std::vector<std::string> magnets;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(text.c_str(), &argc);
    for (int i = 0; argv && i < argc; ++i) {
        std::wstring a = argv[i];
        if (_wcsnicmp(a.c_str(), L"magnet:?", 8) == 0) magnets.push_back(narrow(a));
        else if (a.size() > 8 && _wcsicmp(a.c_str() + a.size() - 8, L".torrent") == 0) files.push_back(a);
    }
    if (argv) LocalFree(argv);
    if (!files.empty()) add_files(files);
    if (!magnets.empty()) add_magnets(magnets, download_dir(), g.settings.start_paused);
}
static void open_torrent_dialog() {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return;
    COMDLG_FILTERSPEC spec[] = {{L"File torrent (*.torrent)", L"*.torrent"}, {L"Tutti i file", L"*.*"}};
    dlg->SetFileTypes(2, spec);
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_ALLOWMULTISELECT | FOS_FILEMUSTEXIST | FOS_FORCEFILESYSTEM);
    dlg->SetTitle(L"Apri file .torrent");
    std::vector<std::wstring> files;
    if (SUCCEEDED(dlg->Show(g.main))) {
        IShellItemArray* items = nullptr;
        if (SUCCEEDED(dlg->GetResults(&items))) {
            DWORD n = 0;
            items->GetCount(&n);
            for (DWORD i = 0; i < n; ++i) {
                IShellItem* it = nullptr;
                if (SUCCEEDED(items->GetItemAt(i, &it))) {
                    PWSTR p = nullptr;
                    if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) { files.emplace_back(p); CoTaskMemFree(p); }
                    it->Release();
                }
            }
            items->Release();
        }
    }
    dlg->Release();
    if (!files.empty()) add_files(files);
}

// ------------------------------------------------------------------ settings application
void apply_session_settings() {
    auto& s = g.settings;
    auto& e = g.engine;
    std::string err;
    if (!e.set_network(s.port, s.dht, s.upnp, s.natpmp, s.lsd, s.ipv6, err)) log_line(L"Rete: " + widen(err));
    if (!e.set_limits(s.down_kib, s.up_kib, s.connections, s.upload_slots)) log_line(L"Limiti non validi: valori ripristinati.");
    e.set_queue(s.active_downloads, s.active_seeds);
    e.set_encryption(s.encryption);
    e.set_preallocate(s.preallocate);
    e.set_user_agent(s.user_agent);
    e.set_identity(s.peer_id_prefix, (s.send_client_name && !s.anonymous_mode)
        ? (s.client_name.empty() ? std::string(SF_CLIENT_NAME) : s.client_name) : std::string());
    e.set_bool("anonymous_mode", s.anonymous_mode, err);
    e.set_bool("announce_to_all_trackers", s.announce_all_trackers, err);
    e.set_bool("announce_to_all_tiers", s.announce_all_tiers, err);
    for (auto const& [k, v] : s.int_settings) if (!e.set_int(k, v, err)) log_line(widen(err));
    for (auto const& [k, v] : s.bool_settings) if (!e.set_bool(k, v != 0, err)) log_line(widen(err));
}

// ------------------------------------------------------------------ commands
static bool confirm_remove(std::vector<int> const& ids, bool delete_files) {
    if (!delete_files && !g.settings.confirm_remove) return true;
    std::wstring what = ids.size() == 1 ? L"«" + widen(row_by_id(ids[0]) ? row_by_id(ids[0])->v.name : "") + L"»"
                                        : std::to_wstring(ids.size()) + L" torrent";
    std::wstring msg = delete_files
        ? L"Rimuovere " + what + L" e CANCELLARE DEFINITIVAMENTE i file scaricati dal disco?\n\nL'operazione non può essere annullata."
        : L"Rimuovere " + what + L" dall'elenco?\nI file scaricati restano sul disco.";
    return MessageBoxW(g.main, msg.c_str(), L"SeedFlow", (delete_files ? MB_ICONWARNING : MB_ICONQUESTION) | MB_YESNO | MB_DEFBUTTON2) == IDYES;
}
static void remove_selected(bool delete_files) {
    auto ids = selected_ids();
    if (ids.empty() || !confirm_remove(ids, delete_files)) return;
    for (int id : ids) {
        Row const* r = row_by_id(id);
        std::string hash = r ? r->v.hash : g.engine.hash_of(id);
        std::wstring name = r ? widen(r->v.name) : L"";
        if (g.engine.remove(id, delete_files)) {
            g.settings.torrents.erase(hash);
            log_line((delete_files ? L"Rimosso con cancellazione file: " : L"Rimosso: ") + name);
        }
    }
    persist();
    refresh_now();
}
// libtorrent keeps only unfinished torrents in the download queue: seeds and
// finished torrents have queue position -1 and ignore queue moves.
static std::vector<std::pair<int, int>> selected_queued() {   // (queue position, id)
    std::vector<std::pair<int, int>> out;
    for (int id : selected_ids())
        if (Row const* r = row_by_id(id); r && r->v.queue_position >= 0) out.emplace_back(r->v.queue_position, id);
    return out;
}
static void update_queue_buttons() {
    bool const any = !selected_ids().empty();
    for (int cmd : {ID_T_QUP, ID_T_QDOWN})
        SendMessageW(g.toolbar, TB_ENABLEBUTTON, static_cast<WPARAM>(cmd), MAKELPARAM(any ? TRUE : FALSE, 0));
}
// Moves the selected torrents outside the libtorrent queue within SeedFlow's own order.
static void move_unqueued(int action) {
    std::vector<Row const*> list;
    for (auto const& r : g.rows) if (r.v.queue_position < 0) list.push_back(&r);
    std::sort(list.begin(), list.end(), [](Row const* a, Row const* b) { return a->number < b->number; });
    auto const sel = selected_ids();
    auto selected = [&](Row const* r) { return std::find(sel.begin(), sel.end(), r->v.id) != sel.end(); };
    if (action == 0) {
        for (std::size_t i = 1; i < list.size(); ++i)
            if (selected(list[i]) && !selected(list[i - 1])) std::swap(list[i], list[i - 1]);
    } else if (action == 1) {
        for (std::size_t i = list.size(); i-- > 1;)
            if (selected(list[i - 1]) && !selected(list[i])) std::swap(list[i], list[i - 1]);
    } else {
        std::stable_partition(list.begin(), list.end(), [&](Row const* r) { return action == 2 ? selected(r) : !selected(r); });
    }
    int index = 0;
    for (Row const* r : list) meta_for(r->v.hash).order = ++index;
}
static void queue_selected(int action) {   // 0 up, 1 down, 2 top, 3 bottom
    auto items = selected_queued();
    // Download queue (libtorrent): move in an order that keeps a multiple selection together.
    std::sort(items.begin(), items.end());
    if (action == 1 || action == 2) std::reverse(items.begin(), items.end());
    for (auto const& [pos, id] : items) g.engine.queue(id, action);
    // Seeding / finished torrents: SeedFlow's own order (libtorrent has no queue for them).
    if (items.size() < selected_ids().size()) { move_unqueued(action); persist(); }
    refresh_now();
}
static void for_selected(void (*fn)(int)) {
    for (int id : selected_ids()) fn(id);
    refresh_now();
}
static void start_selected(bool force) {
    for (int id : selected_ids()) {
        if (force) g.engine.force_start(id); else g.engine.pause(id, false);
        // A manual restart of a torrent that already reached its ratio must not be
        // paused/removed again immediately: mark the action as done.
        if (Row const* r = row_by_id(id)) {
            auto& meta = meta_for(r->v.hash);
            double limit = 0; int action = 0;
            if (ratio_target(meta, limit, action) && r->ratio >= limit && !meta.ratio_done) {
                meta.ratio_done = true;
                log_line(L"Limite ratio già raggiunto per «" + widen(r->v.name) + L"»: l'azione non verrà ripetuta finché non cambi il limite.");
            }
        }
    }
    persist();
    refresh_now();
}
static void copy_magnet() {
    std::wstring all;
    for (int id : selected_ids()) {
        std::string m;
        if (g.engine.magnet_uri(id, m)) { if (!all.empty()) all += L"\r\n"; all += widen(m); }
    }
    if (all.empty()) { MessageBoxW(g.main, L"Magnet non disponibile.", L"SeedFlow", MB_ICONWARNING); return; }
    if (copy_to_clipboard(g.main, all)) set_pane_text(pane_shown(4) ? 4 : status_pane_at(pane_count(g.settings.status_panes) - 1), L"Magnet copiato negli appunti");
}
static void open_selected_folder() {
    auto ids = selected_ids();
    if (ids.empty()) return;
    Row const* r = row_by_id(ids[0]);
    if (!r) return;
    fs::path p = fs::path(widen(r->v.save_path)) / fs::path(widen(r->v.name));
    std::error_code ec;
    if (!r->v.name.empty() && fs::exists(p, ec)) open_folder_select(p.wstring());
    else open_path(widen(r->v.save_path));
}
static void tracker_add() {
    int id = detail_id();
    if (!id) return;
    std::wstring text;
    if (!dlg_trackers(g.main, text)) return;
    int max_tier = -1;
    for (auto const& t : g.engine.trackers(id)) max_tier = std::max(max_tier, t.tier);
    int tier = max_tier + 1, added = 0;
    bool blank = false;
    std::wstring line;
    std::wstring errors;
    auto flush = [&]() {
        std::wstring u = line;
        while (!u.empty() && iswspace(u.back())) u.pop_back();
        while (!u.empty() && iswspace(u.front())) u.erase(u.begin());
        line.clear();
        if (u.empty()) { blank = true; return; }
        if (blank && added) { ++tier; }
        blank = false;
        std::string err;
        if (g.engine.add_tracker(id, narrow(u), tier, err)) ++added;
        else errors += widen(err) + L"\n";
    };
    for (wchar_t ch : text) { if (ch == L'\n') flush(); else if (ch != L'\r') line += ch; }
    flush();
    if (!errors.empty()) MessageBoxW(g.main, (L"Alcuni tracker non sono stati aggiunti:\n\n" + errors).c_str(), L"SeedFlow", MB_ICONWARNING);
    log_line(std::to_wstring(added) + L" tracker aggiunti.");
    refresh_trackers(id);
}
static std::vector<std::wstring> selected_tracker_urls() {
    std::vector<std::wstring> urls;
    auto const& keys = lv_keys[g.trackers];
    for (int i = -1; (i = ListView_GetNextItem(g.trackers, i, LVNI_SELECTED)) >= 0;)
        if (i < static_cast<int>(keys.size()) && keys[static_cast<std::size_t>(i)].rfind(L"** [", 0) != 0) urls.push_back(keys[static_cast<std::size_t>(i)]);
    return urls;
}
static void tracker_remove() {
    int id = detail_id();
    auto urls = selected_tracker_urls();
    if (!id || urls.empty()) return;
    std::wstring msg = urls.size() == 1 ? L"Rimuovere il tracker\n" + urls[0] + L" ?" : L"Rimuovere " + std::to_wstring(urls.size()) + L" tracker?";
    if (MessageBoxW(g.main, msg.c_str(), L"SeedFlow", MB_ICONQUESTION | MB_YESNO) != IDYES) return;
    for (auto const& u : urls) g.engine.remove_tracker(id, narrow(u));
    refresh_trackers(id);
}
static void set_file_priority(int prio) {
    int id = detail_id();
    if (!id) return;
    auto const& keys = lv_keys[g.files];
    for (int i = -1; (i = ListView_GetNextItem(g.files, i, LVNI_SELECTED)) >= 0;)
        if (i < static_cast<int>(keys.size())) g.engine.file_priority(id, _wtoi(keys[static_cast<std::size_t>(i)].c_str()), prio);
    refresh_files(id);
}
static void open_selected_file() {
    int id = detail_id();
    Row const* r = row_by_id(id);
    int i = ListView_GetNextItem(g.files, -1, LVNI_SELECTED);
    if (!r || i < 0 || i >= static_cast<int>(last_files.size())) return;
    fs::path p = fs::path(widen(r->v.save_path)) / fs::path(widen(last_files[static_cast<std::size_t>(i)].path));
    std::error_code ec;
    if (fs::exists(p, ec)) open_folder_select(p.wstring());
}

static void on_command(int cmd) {
    switch (cmd) {
    case ID_FILE_OPEN: open_torrent_dialog(); break;
    case ID_FILE_MAGNET: {
        std::vector<std::string> magnets;
        std::string folder = download_dir();
        bool paused = g.settings.start_paused;
        if (dlg_magnet(g.main, magnets, folder, paused)) { remember_dir(folder); add_magnets(magnets, folder, paused); }
        break;
    }
    case ID_FILE_CREATE: dlg_create_torrent(g.main); break;
    case ID_FILE_PASTE: {
        std::wstring clip = clipboard_text(g.main);
        std::vector<std::string> magnets;
        std::wstring line;
        auto take = [&]() { auto p = line.find(L"magnet:?"); if (p != std::wstring::npos) magnets.push_back(narrow(line.substr(p))); line.clear(); };
        for (wchar_t ch : clip) { if (ch == L'\n' || ch == L' ' || ch == L'\t') take(); else if (ch != L'\r') line += ch; }
        take();
        if (magnets.empty()) MessageBoxW(g.main, L"Negli appunti non c'è nessun magnet link.", L"SeedFlow", MB_ICONINFORMATION);
        else add_magnets(magnets, download_dir(), g.settings.start_paused);
        break;
    }
    case ID_FILE_EXIT: g.exiting = true; DestroyWindow(g.main); break;
    case ID_OPT_CLOSETRAY: g.settings.close_to_tray = !g.settings.close_to_tray; persist(); break;
    case ID_OPT_MINTRAY: g.settings.minimize_to_tray = !g.settings.minimize_to_tray; persist(); break;
    case ID_OPT_STARTPAUSED: g.settings.start_paused = !g.settings.start_paused; persist(); break;
    case ID_OPT_CONFIRMREMOVE: g.settings.confirm_remove = !g.settings.confirm_remove; persist(); break;
    case ID_OPT_PREFS: if (dlg_preferences(g.main)) refresh_now(); break;
    case ID_TOOLS_WIZARD: if (dlg_wizard(g.main)) { apply_session_settings(); persist(); } break;
    case ID_TOOLS_DIAG: dlg_diagnostics(g.main); break;
    case ID_TOOLS_STATS: dlg_stats(g.main); break;
    case ID_TOOLS_DATAFOLDER: open_path(app_data_dir().wstring()); break;
    case ID_HELP_ABOUT: dlg_about(g.main); break;
    case ID_HELP_GUIDE: dlg_guide(g.main); break;
    case ID_HELP_KEYS: dlg_guide(g.main, true); break;
    case ID_HELP_GITHUB: open_path(kProjectUrl); break;
    case ID_HELP_RELEASES: open_path(kReleasesUrl); break;
    case ID_HELP_REPORT: report_problem(g.main); break;
    case ID_T_START: start_selected(false); break;
    case ID_T_FORCESTART: start_selected(true); break;
    case ID_T_PAUSE: for_selected([](int id) { g.engine.pause(id, true); }); break;
    case ID_T_STARTALL: for (int id : g.engine.ids()) g.engine.pause(id, false); refresh_now(); break;
    case ID_T_PAUSEALL: for (int id : g.engine.ids()) g.engine.pause(id, true); refresh_now(); break;
    case ID_T_REMOVE: remove_selected(false); break;
    case ID_T_DELETE: remove_selected(true); break;
    case ID_T_COPYMAGNET: copy_magnet(); break;
    case ID_T_OPENFOLDER: open_selected_folder(); break;
    case ID_T_MOVE: move_selected(); break;
    case ID_T_RATIO: {
        // With torrents selected: their own limit. Without a selection the button used to
        // do nothing at all: open the global ratio settings instead.
        auto ids = selected_ids();
        if (ids.empty()) { if (dlg_preferences(g.main, PrefQueue)) refresh_now(); break; }
        if (dlg_ratio(g.main, ids)) { persist(); apply_ratio_rules(); refresh_now(); }
        break;
    }
    case ID_T_LIMITS: {
        auto ids = selected_ids();
        if (ids.empty()) break;
        Row const* r = row_by_id(ids[0]);
        int down = r ? r->v.down_limit / 1024 : 0, up = r ? r->v.up_limit / 1024 : 0;
        if (dlg_limits(g.main, down, up)) for (int id : ids) g.engine.set_torrent_limits(id, down, up);
        break;
    }
    case ID_T_SUPERSEED:
        for (int id : selected_ids()) {
            Row const* r = row_by_id(id);
            if (!r) continue;
            std::string err;
            if (!g.engine.super_seed(id, !r->v.super_seeding, err) && !err.empty()) MessageBoxW(g.main, widen(err).c_str(), L"Super-seeding", MB_ICONINFORMATION);
        }
        refresh_now();
        break;
    case ID_T_QTOP: queue_selected(2); break;
    case ID_T_QUP: queue_selected(0); break;
    case ID_T_QDOWN: queue_selected(1); break;
    case ID_T_QBOTTOM: queue_selected(3); break;
    case ID_T_REANNOUNCE: case ID_TR_REANNOUNCE: for_selected([](int id) { g.engine.reannounce(id); }); log_line(L"Riannuncio richiesto."); break;
    case ID_T_RECHECK:
        if (!selected_ids().empty() && MessageBoxW(g.main, L"Verificare di nuovo i file dei torrent selezionati?", L"SeedFlow", MB_ICONQUESTION | MB_YESNO) == IDYES)
            for_selected([](int id) { g.engine.recheck(id); });
        break;
    case ID_T_SELECTALL: ListView_SetItemState(g.list, -1, LVIS_SELECTED, LVIS_SELECTED); break;
    case ID_TR_ADD: tracker_add(); break;
    case ID_TR_REMOVE: tracker_remove(); break;
    case ID_TR_COPY: {
        std::wstring all;
        for (auto const& u : selected_tracker_urls()) { if (!all.empty()) all += L"\r\n"; all += u; }
        if (!all.empty()) copy_to_clipboard(g.main, all);
        break;
    }
    case ID_F_SKIP: set_file_priority(0); break;
    case ID_F_LOW: set_file_priority(1); break;
    case ID_F_NORMAL: set_file_priority(4); break;
    case ID_F_HIGH: set_file_priority(7); break;
    case ID_F_OPEN: open_selected_file(); break;
    case ID_TRAY_OPEN: restore_from_tray(); break;
    default: break;
    }
}

// ------------------------------------------------------------------ creation / layout
struct TbButton { int cmd; int image; wchar_t const* text; bool show_text; };
// Main toolbar: icons only, the text is the tooltip.
static TbButton const kToolbar[] = {
    {ID_FILE_OPEN, 0, L"Aggiungi torrent", false}, {ID_FILE_MAGNET, 1, L"Aggiungi magnet", false}, {ID_FILE_CREATE, 17, L"Crea torrent", false}, {0, -1, nullptr, false},
    {ID_T_START, 2, L"Avvia", false}, {ID_T_PAUSE, 3, L"Pausa", false}, {ID_T_REMOVE, 4, L"Rimuovi dalla lista", false},
    {ID_T_DELETE, 5, L"Rimuovi e cancella i file", false}, {0, -1, nullptr, false},
    {ID_T_QUP, 6, L"Sposta su in coda", false}, {ID_T_QDOWN, 7, L"Sposta giù in coda", false}, {0, -1, nullptr, false},
    {ID_T_SUPERSEED, 8, L"Super-seeding", false}, {ID_T_RATIO, 9, L"Limite di ratio", false}, {0, -1, nullptr, false},
    {ID_TOOLS_WIZARD, 10, L"Ottimizza le impostazioni", false}, {ID_TOOLS_STATS, 11, L"Statistiche", false}, {ID_TOOLS_DIAG, 12, L"Diagnostica di rete", false},
    {0, -1, nullptr, false}, {ID_OPT_PREFS, 16, L"Preferenze", false},
};
static HWND make_toolbar(HWND parent, TbButton const* buttons, int count, bool compact_buttons, HIMAGELIST images) {
    HWND tb = CreateWindowExW(0, TOOLBARCLASSNAMEW, nullptr,
        WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_LIST | TBSTYLE_TOOLTIPS | CCS_NODIVIDER | CCS_NOPARENTALIGN | CCS_NORESIZE,
        0, 0, 10, 10, parent, nullptr, g.inst, nullptr);
    SendMessageW(tb, CCM_SETUNICODEFORMAT, TRUE, 0);
    SendMessageW(tb, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageW(tb, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_MIXEDBUTTONS | TBSTYLE_EX_DOUBLEBUFFER);
    SendMessageW(tb, TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(images));
    SendMessageW(tb, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), FALSE);
    std::vector<TBBUTTON> b(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        auto& t = b[static_cast<std::size_t>(i)];
        if (!buttons[i].cmd) { t.fsStyle = BTNS_SEP; continue; }
        t.iBitmap = buttons[i].image;
        t.idCommand = buttons[i].cmd;
        t.fsState = TBSTATE_ENABLED;
        t.fsStyle = static_cast<BYTE>(BTNS_BUTTON | BTNS_AUTOSIZE | (buttons[i].show_text ? BTNS_SHOWTEXT : 0));
        t.iString = reinterpret_cast<INT_PTR>(buttons[i].text);
    }
    SendMessageW(tb, TB_ADDBUTTONSW, static_cast<WPARAM>(count), reinterpret_cast<LPARAM>(b.data()));
    SendMessageW(tb, TB_SETPADDING, 0, MAKELPARAM(scale(compact_buttons ? 6 : 9), scale(compact_buttons ? 4 : 7)));
    SendMessageW(tb, TB_AUTOSIZE, 0, 0);
    return tb;
}
static TbButton const kTrackerBar[] = {
    {ID_TR_ADD, 0, L"Aggiungi", true}, {ID_TR_REMOVE, 15, L"Rimuovi", true}, {ID_TR_COPY, 14, L"Copia URL", true},
    {ID_TR_REANNOUNCE, 13, L"Riannuncia ora", true},
};

static int toolbar_height(HWND tb) {
    RECT r{};
    int n = static_cast<int>(SendMessageW(tb, TB_BUTTONCOUNT, 0, 0));
    if (n > 0) SendMessageW(tb, TB_GETITEMRECT, 0, reinterpret_cast<LPARAM>(&r));
    return std::max<int>(r.bottom - r.top, scale(22)) + scale(4);
}
static RECT split_v_rect, split_h_rect;
// The Nome column takes the width left by the other columns, so the list does not
// scroll horizontally at the default widths (space for the vertical scrollbar is kept).
// A width the user gives Nome by hand is a minimum: it only grows to fill free space.
static bool fitting_columns = false;
static int name_user_width = 0;
static void fit_name_column() {
    if (!g.list || fitting_columns) return;
    RECT rc; GetWindowRect(g.list, &rc);
    int const available = (rc.right - rc.left) - GetSystemMetricsForDpi(SM_CXVSCROLL, g.dpi) - GetSystemMetricsForDpi(SM_CXEDGE, g.dpi) * 2;
    int others = 0;
    for (int c = 0; c < ColCount; ++c) if (c != ColName) others += ListView_GetColumnWidth(g.list, c);
    int const width = std::max({scale(160), name_user_width, available - others});
    if (width == ListView_GetColumnWidth(g.list, ColName)) return;
    fitting_columns = true;
    ListView_SetColumnWidth(g.list, ColName, width);
    fitting_columns = false;
}
static void layout() {
    RECT rc; GetClientRect(g.main, &rc);
    int tbh = toolbar_height(g.toolbar);
    SetWindowPos(g.toolbar, nullptr, scale(2), scale(1), rc.right - scale(4), tbh, SWP_NOZORDER);
    SendMessageW(g.status, WM_SIZE, 0, 0);
    int sbh = status_height();
    layout_status();
    int top = tbh + scale(2), bottom = rc.bottom - sbh;
    int left_w = std::clamp(scale(g.split_left), scale(90), std::max(scale(90), static_cast<int>(rc.right) / 2));
    int sp = scale(5);
    int detail_h = std::clamp(scale(g.split_bottom), scale(80), std::max(scale(80), bottom - top - scale(120)));
    SetWindowPos(g.filters, nullptr, 0, top, left_w, bottom - top, SWP_NOZORDER);
    split_v_rect = {left_w, top, left_w + sp, bottom};
    int x = left_w + sp, w = rc.right - x;
    int list_h = bottom - top - detail_h - sp;
    SetWindowPos(g.list, nullptr, x, top, w, list_h, SWP_NOZORDER);
    fit_name_column();
    split_h_rect = {x, top + list_h, rc.right, top + list_h + sp};
    int ty = top + list_h + sp;
    SetWindowPos(g.tabs, nullptr, x, ty, w, detail_h, SWP_NOZORDER);
    RECT page{0, 0, w, detail_h};
    TabCtrl_AdjustRect(g.tabs, FALSE, &page);
    int px = x + page.left, py = ty + page.top, pw = page.right - page.left, ph = page.bottom - page.top;
    SetWindowPos(g.general, HWND_TOP, px, py, pw, ph, 0);
    int tbh2 = toolbar_height(g.tracker_bar);
    SetWindowPos(g.tracker_bar, HWND_TOP, px, py, pw, tbh2, 0);
    SetWindowPos(g.trackers, HWND_TOP, px, py + tbh2, pw, ph - tbh2, 0);
    SetWindowPos(g.peers, HWND_TOP, px, py, pw, ph, 0);
    SetWindowPos(g.files, HWND_TOP, px, py, pw, ph, 0);
    SetWindowPos(g.log, HWND_TOP, px, py, pw, ph, 0);
    SetWindowPos(g.chart, HWND_TOP, px, py, pw, ph, 0);
    ListView_SetColumnWidth(g.filters, 0, left_w - scale(4));
}

static void create_children(HWND hwnd) {
    create_fonts();
    create_imagelists();
    g.toolbar = make_toolbar(hwnd, kToolbar, static_cast<int>(std::size(kToolbar)), false, g.il_main_bar);

    g.filters = CreateWindowExW(0, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_NOCOLUMNHEADER | LVS_SHOWSELALWAYS,
        0, 0, 10, 10, hwnd, nullptr, g.inst, nullptr);
    ListView_SetExtendedListViewStyle(g.filters, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    SetWindowTheme(g.filters, L"Explorer", nullptr);
    SendMessageW(g.filters, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), FALSE);
    ListView_SetImageList(g.filters, g.il_filters, LVSIL_SMALL);
    ListView_SetBkColor(g.filters, RGB(243, 245, 248));
    ListView_SetTextBkColor(g.filters, CLR_NONE);
    add_columns(g.filters, {{L"Filtro", 160}});
    for (int i = 0; i < 7; ++i) {
        LVITEMW it{};
        it.mask = LVIF_TEXT | LVIF_IMAGE;
        it.iItem = i;
        it.iImage = i;
        it.pszText = const_cast<wchar_t*>(kFilterNames[i]);
        ListView_InsertItem(g.filters, &it);
    }
    ListView_SetItemState(g.filters, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);

    g.list = make_listview(hwnd, LVS_OWNERDATA, 500);
    ListView_SetImageList(g.list, g.il_states, LVSIL_SMALL);
    std::vector<std::pair<std::wstring, int>> cols;
    for (auto const& c : kCols) cols.emplace_back(c.title, c.fmt == LVCFMT_RIGHT ? -c.width : c.width);
    add_columns(g.list, cols);
    load_column_widths();
    update_sort_arrow();

    g.tabs = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_FOCUSNEVER,
        0, 0, 10, 10, hwnd, nullptr, g.inst, nullptr);
    SendMessageW(g.tabs, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), FALSE);
    TabCtrl_SetImageList(g.tabs, g.il_small);
    wchar_t const* tab_names[] = {L"Generale", L"Tracker", L"Peer", L"File", L"Log", L"Grafico"};
    int tab_icons[] = {0, 1, 2, 3, 4, 10};
    for (int i = 0; i < 6; ++i) {
        TCITEMW ti{};
        ti.mask = TCIF_TEXT | TCIF_IMAGE;
        ti.pszText = const_cast<wchar_t*>(tab_names[i]);
        ti.iImage = tab_icons[i];
        TabCtrl_InsertItem(g.tabs, i, &ti);
    }
    TabCtrl_SetPadding(g.tabs, scale(6), scale(2));

    g.chart = CreateWindowExW(0, kSpeedClass, L"Grafico", WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 10, 10, hwnd, nullptr, g.inst, nullptr);
    g.general = CreateWindowExW(0, kGeneralClass, L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, hwnd, nullptr, g.inst, nullptr);
    g.tracker_bar = make_toolbar(hwnd, kTrackerBar, static_cast<int>(std::size(kTrackerBar)), true, g.il_toolbar);
    g.trackers = make_listview(hwnd, 0, 501);
    ListView_SetImageList(g.trackers, g.il_small, LVSIL_SMALL);
    add_columns(g.trackers, {{L"URL", 300}, {L"Tier", -38}, {L"Stato", 100}, {L"Seed", -56}, {L"Peer", -56},
        {L"Completati", -70}, {L"Prossimo", -70}, {L"Messaggio", 260}});
    g.peers = make_listview(hwnd, 0, 502);
    add_columns(g.peers, {{L"IP", 140}, {L"Client", 170}, {L"Flag", 50}, {L"%", -56}, {L"↓", -80}, {L"↑", -80},
        {L"Scaricati", -80}, {L"Caricati", -80}});
    g.files = make_listview(hwnd, 0, 503);
    add_columns(g.files, {{L"Percorso", 420}, {L"Dimensione", -86}, {L"Scaricati", -86}, {L"%", -60}, {L"Priorità", 100}});
    g.log = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        0, 0, 10, 10, hwnd, nullptr, g.inst, nullptr);
    SendMessageW(g.log, WM_SETFONT, reinterpret_cast<WPARAM>(g.mono), FALSE);
    SendMessageW(g.log, EM_SETLIMITTEXT, 0, 0);

    g.status = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP | SBARS_TOOLTIPS,
        0, 0, 0, 0, hwnd, nullptr, g.inst, nullptr);
    SendMessageW(g.status, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), FALSE);
    show_tab(0);
}
static void apply_fonts_after_dpi() {
    create_fonts();
    create_imagelists();
    name_user_width = 0;    // pixel widths of the old DPI no longer apply
    status_need.clear();    // re-measure the status bar parts with the new font
    status_mask = -1;
    for (HWND w : {g.filters, g.list, g.tabs, g.trackers, g.peers, g.files, g.status, g.toolbar, g.tracker_bar})
        SendMessageW(w, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
    SendMessageW(g.log, WM_SETFONT, reinterpret_cast<WPARAM>(g.mono), TRUE);
    SendMessageW(g.toolbar, TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(g.il_main_bar));
    SendMessageW(g.tracker_bar, TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(g.il_toolbar));
    ListView_SetImageList(g.list, g.il_states, LVSIL_SMALL);
    ListView_SetImageList(g.filters, g.il_filters, LVSIL_SMALL);
    ListView_SetImageList(g.trackers, g.il_small, LVSIL_SMALL);
    TabCtrl_SetImageList(g.tabs, g.il_small);
    SendMessageW(g.toolbar, TB_AUTOSIZE, 0, 0);
    SendMessageW(g.tracker_bar, TB_AUTOSIZE, 0, 0);
    if (g.net_icon) { DestroyIcon(g.net_icon); g.net_icon = nullptr; }
    update_network_indicator();
}

// ------------------------------------------------------------------ notifications
static LRESULT on_list_customdraw(NMLVCUSTOMDRAW* cd) {
    switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREPAINT: return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT: return CDRF_NOTIFYSUBITEMDRAW;
    case CDDS_ITEMPREPAINT | CDDS_SUBITEM: {
        int item = static_cast<int>(cd->nmcd.dwItemSpec);
        if ((cd->iSubItem != ColProgress && cd->iSubItem != ColHealth) || item >= static_cast<int>(g.visible.size())) return CDRF_DODEFAULT;
        Row const& r = g.rows[static_cast<std::size_t>(g.visible[static_cast<std::size_t>(item)])];
        RECT rc;
        ListView_GetSubItemRect(g.list, item, cd->iSubItem, LVIR_BOUNDS, &rc);
        HDC dc = cd->nmcd.hdc;
        bool selected = ListView_GetItemState(g.list, item, LVIS_SELECTED) != 0;
        // Background: window colour, then the Explorer selection/hot highlight of the whole row
        // clipped to this cell, so the custom cells blend with the themed row.
        FillRect(dc, &rc, GetSysColorBrush(COLOR_WINDOW));
        bool hot = (cd->nmcd.uItemState & CDIS_HOT) != 0;
        if (selected || hot) {
            if (!g.list_theme) g.list_theme = OpenThemeData(g.list, L"ListView");
            if (g.list_theme) {
                RECT row; ListView_GetItemRect(g.list, item, &row, LVIR_BOUNDS);
                int st = selected ? (GetFocus() == g.list ? (hot ? LISS_HOTSELECTED : LISS_SELECTED) : LISS_SELECTEDNOTFOCUS) : LISS_HOT;
                DrawThemeBackground(g.list_theme, dc, LVP_LISTITEM, st, &row, &rc);
            }
        }
        if (cd->iSubItem == ColHealth) {
            paint::health_meter(dc, rc, paint::health(r.v), g.font, selected);
            return CDRF_SKIPDEFAULT;
        }
        RECT bar = rc;
        InflateRect(&bar, -scale(3), -scale(3));
        BarLook look = bar_look(r);
        paint::progress_bar(dc, bar, look.fraction, r.color, look.kind, g.anim_phase, look.text, g.font);
        return CDRF_SKIPDEFAULT;
    }
    }
    return CDRF_DODEFAULT;
}
static LRESULT on_notify(NMHDR* nm) {
    if (nm->hwndFrom == ListView_GetHeader(g.list) && (nm->code == HDN_ENDTRACKW || nm->code == HDN_DIVIDERDBLCLICKW)) {
        auto const* hd = reinterpret_cast<NMHEADERW const*>(nm);
        if (hd->iItem == ColName) {
            // Resizing Nome itself: keep what the user chose (wider means horizontal scrolling).
            if (nm->code == HDN_ENDTRACKW && hd->pitem && (hd->pitem->mask & HDI_WIDTH)) name_user_width = hd->pitem->cxy;
            else PostMessageW(g.main, WM_APP_FITCOLUMNS, 1, 0);
            return 0;
        }
        PostMessageW(g.main, WM_APP_FITCOLUMNS, 0, 0);   // the new width is applied after this notification
        return 0;
    }
    if (nm->hwndFrom == g.list) {
        switch (nm->code) {
        case LVN_GETDISPINFOW: {
            auto* di = reinterpret_cast<NMLVDISPINFOW*>(nm);
            int i = di->item.iItem;
            if (i < 0 || i >= static_cast<int>(g.visible.size())) return 0;
            Row const& r = g.rows[static_cast<std::size_t>(g.visible[static_cast<std::size_t>(i)])];
            if (di->item.mask & LVIF_TEXT) {
                static std::wstring text;
                text = cell_text(r, di->item.iSubItem);
                if (di->item.pszText && di->item.cchTextMax > 0)
                    wcsncpy_s(di->item.pszText, static_cast<std::size_t>(di->item.cchTextMax), text.c_str(), _TRUNCATE);
            }
            if (di->item.mask & LVIF_IMAGE) di->item.iImage = r.state_icon; // column 0 = Nome
            return 0;
        }
        case NM_CUSTOMDRAW: return on_list_customdraw(reinterpret_cast<NMLVCUSTOMDRAW*>(nm));
        case LVN_COLUMNCLICK: {
            int col = reinterpret_cast<NMLISTVIEW*>(nm)->iSubItem;
            if (col == g.sort_col) g.sort_desc = !g.sort_desc; else { g.sort_col = col; g.sort_desc = col != ColQueue && col != ColName; }
            update_sort_arrow();
            rebuild_visible();
            return 0;
        }
        case LVN_ITEMCHANGED: case LVN_ODSTATECHANGED: if (!rebuilding) { refresh_details(true); update_queue_buttons(); } return 0;
        case NM_DBLCLK: open_selected_folder(); return 0;
        default: break;
        }
    } else if (nm->hwndFrom == g.filters && nm->code == LVN_ITEMCHANGED) {
        auto* lv = reinterpret_cast<NMLISTVIEW*>(nm);
        if ((lv->uNewState & LVIS_SELECTED) && lv->iItem >= 0 && lv->iItem != g.filter) { g.filter = lv->iItem; rebuild_visible(); refresh_details(true); }
    } else if (nm->hwndFrom == g.tabs && nm->code == TCN_SELCHANGE) {
        show_tab(current_tab());
    } else if (nm->hwndFrom == g.status && nm->code == NM_CLICK) {
        if (status_pane_at(static_cast<int>(reinterpret_cast<NMMOUSE*>(nm)->dwItemSpec)) == 0) dlg_diagnostics(g.main);
    } else if (nm->hwndFrom == g.trackers && nm->code == NM_DBLCLK) {
        on_command(ID_TR_ADD);
    } else if (nm->hwndFrom == g.files && nm->code == NM_DBLCLK) {
        open_selected_file();
    } else if (nm->code == TTN_GETDISPINFOW) {
        auto* tt = reinterpret_cast<NMTTDISPINFOW*>(nm);
        for (auto const& b : kToolbar) if (b.cmd && static_cast<UINT_PTR>(b.cmd) == nm->idFrom) { tt->lpszText = const_cast<wchar_t*>(b.text); break; }
    }
    return 0;
}
static void show_context(HWND target, POINT pt) {
    int res = 0;
    if (target == g.list) {
        if (pt.x != -1) {
            POINT cp = pt; ScreenToClient(g.list, &cp);
            LVHITTESTINFO hit{}; hit.pt = cp;
            int i = ListView_HitTest(g.list, &hit);
            if (i >= 0 && ListView_GetItemState(g.list, i, LVIS_SELECTED) == 0) {
                ListView_SetItemState(g.list, -1, 0, LVIS_SELECTED);
                ListView_SetItemState(g.list, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            }
        } else GetCursorPos(&pt);
        if (selected_ids().empty()) return;
        res = IDR_CTX_TORRENT;
    } else if (target == g.trackers) res = IDR_CTX_TRACKER;
    else if (target == g.files) res = IDR_CTX_FILE;
    else return;
    if (pt.x == -1) GetCursorPos(&pt);
    HMENU menu = LoadMenuW(g.inst, MAKEINTRESOURCEW(res));
    if (!menu) return;
    HMENU popup = GetSubMenu(menu, 0);
    std::size_t const bitmap_start = menu_bitmaps.size();
    decorate_menu(popup);
    if (res == IDR_CTX_TORRENT) {
        Row const* r = row_by_id(selected_ids()[0]);
        if (r) CheckMenuItem(popup, ID_T_SUPERSEED, r->v.super_seeding ? MF_CHECKED : MF_UNCHECKED);
    }
    TrackPopupMenu(popup, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g.main, nullptr);
    DestroyMenu(menu);
    release_menu_bitmaps_from(bitmap_start);
}

// ------------------------------------------------------------------ window procedure
static int drag = 0; // 1 vertical splitter, 2 horizontal splitter
static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == g.taskbar_created && msg != 0) { if (g.tray_visible) { g.tray_visible = false; tray_add(); } return 0; }
    switch (msg) {
    case WM_CREATE:
        g.main = hwnd;
        g.dpi = GetDpiForWindow(hwnd);
        paint::set_dpi(g.dpi);
        create_children(hwnd);
        DragAcceptFiles(hwnd, TRUE);
        decorate_menu(GetMenu(hwnd));
        SetTimer(hwnd, TIMER_REFRESH, 1000, nullptr);
        return 0;
    case WM_SIZE:
        if (wp == SIZE_MINIMIZED) {
            if (g.settings.minimize_to_tray) hide_to_tray();
            else if (g.settings.trim_when_hidden) trim_working_set();
            return 0;
        }
        if (g.hidden && wp != SIZE_MINIMIZED) g.hidden = false;
        layout();
        return 0;
    case WM_GETMINMAXINFO: {
        auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
        mm->ptMinTrackSize = {scale(760), scale(460)};
        return 0;
    }
    case WM_DPICHANGED: {
        g.dpi = HIWORD(wp);
        paint::set_dpi(g.dpi);
        auto* r = reinterpret_cast<RECT*>(lp);
        apply_fonts_after_dpi();
        release_menu_bitmaps_from(0);           // main-menu icons at the new size
        decorate_menu(GetMenu(hwnd));
        DrawMenuBar(hwnd);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        layout();
        return 0;
    }
    case WM_SETCURSOR: {
        POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd, &pt);
        if (PtInRect(&split_v_rect, pt)) { SetCursor(LoadCursorW(nullptr, IDC_SIZEWE)); return TRUE; }
        if (PtInRect(&split_h_rect, pt)) { SetCursor(LoadCursorW(nullptr, IDC_SIZENS)); return TRUE; }
        break;
    }
    case WM_LBUTTONDOWN: {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        drag = PtInRect(&split_v_rect, pt) ? 1 : PtInRect(&split_h_rect, pt) ? 2 : 0;
        if (drag) SetCapture(hwnd);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (drag && (wp & MK_LBUTTON)) {
            RECT rc; GetClientRect(hwnd, &rc);
            int const sbh = status_height();
            int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
            if (drag == 1) g.split_left = MulDiv(std::clamp(x, scale(90), std::max(scale(90), static_cast<int>(rc.right) / 2)), 96, static_cast<int>(g.dpi));
            else g.split_bottom = MulDiv(std::max(scale(80), static_cast<int>(rc.bottom - sbh) - y), 96, static_cast<int>(g.dpi));
            layout();
        }
        return 0;
    case WM_LBUTTONUP:
        if (drag) { drag = 0; ReleaseCapture(); }
        return 0;
    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(hwnd, &rc);
        HBRUSH b = CreateSolidBrush(RGB(243, 245, 248));
        FillRect(reinterpret_cast<HDC>(wp), &rc, b);
        DeleteObject(b);
        return 1;
    }
    case WM_CTLCOLORSTATIC:
        if (reinterpret_cast<HWND>(lp) == g.log) { SetBkColor(reinterpret_cast<HDC>(wp), GetSysColor(COLOR_WINDOW)); return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW)); }
        break;
    case WM_TIMER:
        if (wp == TIMER_REFRESH) on_timer();
        else if (wp == TIMER_ANIM) on_anim_timer();
        return 0;
    case WM_NOTIFY: return on_notify(reinterpret_cast<NMHDR*>(lp));
    case WM_CONTEXTMENU: show_context(reinterpret_cast<HWND>(wp), POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}); return 0;
    case WM_INITMENUPOPUP: update_menu_checks(reinterpret_cast<HMENU>(wp)); return 0;
    case WM_COMMAND: {
        int cmd = LOWORD(wp);
        // Del / Maiusc+Del / Ctrl+A act on torrents only when the torrent list has focus.
        if (HIWORD(wp) == 1 && (cmd == ID_T_REMOVE || cmd == ID_T_DELETE || cmd == ID_T_SELECTALL) && GetFocus() != g.list) return 0;
        on_command(cmd);
        return 0;
    }
    case WM_DROPFILES: {
        auto drop = reinterpret_cast<HDROP>(wp);
        UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        std::vector<std::wstring> files;
        for (UINT i = 0; i < n; ++i) {
            wchar_t path[MAX_PATH * 4];
            if (DragQueryFileW(drop, i, path, static_cast<UINT>(std::size(path)))) {
                std::wstring p = path;
                if (p.size() > 8 && _wcsicmp(p.c_str() + p.size() - 8, L".torrent") == 0) files.push_back(p);
            }
        }
        DragFinish(drop);
        if (!files.empty()) add_files(files);
        return 0;
    }
    case WM_COPYDATA: {
        auto* cds = reinterpret_cast<COPYDATASTRUCT*>(lp);
        if (cds && cds->dwData == 0x5346 && cds->lpData) {
            std::wstring args(static_cast<wchar_t const*>(cds->lpData), cds->cbData / sizeof(wchar_t));
            restore_from_tray();
            handle_arguments(L"SeedFlow.exe " + args);
        }
        return TRUE;
    }
    case WM_APP_TRAY:
        switch (LOWORD(lp)) {
        case WM_LBUTTONDBLCLK: case NIN_SELECT: case NIN_KEYSELECT: restore_from_tray(); break;
        case WM_CONTEXTMENU: case WM_RBUTTONUP: {
            POINT pt; GetCursorPos(&pt);
            HMENU menu = LoadMenuW(g.inst, MAKEINTRESOURCEW(IDR_CTX_TRAY));
            if (!menu) break;
            HMENU popup = GetSubMenu(menu, 0);
            std::size_t const bitmap_start = menu_bitmaps.size();
            decorate_menu(popup);
            update_menu_checks(popup);
            SetMenuDefaultItem(popup, ID_TRAY_OPEN, FALSE);
            SetForegroundWindow(hwnd);
            TrackPopupMenu(popup, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
            PostMessageW(hwnd, WM_NULL, 0, 0);
            DestroyMenu(menu);
            release_menu_bitmaps_from(bitmap_start);
            break;
        }
        default: break;
        }
        return 0;
    case WM_APP_FITCOLUMNS:
        if (wp == 1) name_user_width = ListView_GetColumnWidth(g.list, ColName);   // after a double click on the Nome divider
        fit_name_column();
        return 0;
    case WM_APP_PORTCHECK:
        consume_port_check();
        return 0;
    case WM_QUERYENDSESSION: return TRUE;
    case WM_ENDSESSION:
        if (wp) {   // Windows is shutting down: the message loop may not run again
            g.exiting = true;
            persist();
            g.engine.stop(std::chrono::seconds(5));
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_CLOSE:
        if (!g.exiting && g.settings.close_to_tray) { hide_to_tray(); if (g.hidden) return 0; }
        g.exiting = true;
        DestroyWindow(hwnd);
        return 0;
    case WM_THEMECHANGED:
        if (g.list_theme) { CloseThemeData(g.list_theme); g.list_theme = nullptr; }
        break;
    case WM_DESTROY: {
        if (g.list_theme) { CloseThemeData(g.list_theme); g.list_theme = nullptr; }
        g.port_check_job.reset();
        g.port_check_running = false;
        KillTimer(hwnd, TIMER_REFRESH);
        WINDOWPLACEMENT wpl{}; wpl.length = sizeof(wpl);
        if (GetWindowPlacement(hwnd, &wpl)) {
            auto& r = wpl.rcNormalPosition;
            g.settings.win_x = r.left; g.settings.win_y = r.top;
            g.settings.win_w = MulDiv(r.right - r.left, 96, static_cast<int>(g.dpi));
            g.settings.win_h = MulDiv(r.bottom - r.top, 96, static_cast<int>(g.dpi));
            g.settings.win_max = wpl.showCmd == SW_SHOWMAXIMIZED || IsZoomed(hwnd);
        }
        g.settings.split_left = g.split_left;
        g.settings.split_bottom = g.split_bottom;
        save_column_widths();
        tray_remove();
        persist();
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void start_port_check(HWND notify) {
    if (g.port_check_running || g.exiting) return;
    try {
        auto job = std::make_shared<PortCheckJob>();
        g.port_check_job = job;
        g.port_check_running = true;
        int const port = g.settings.port;
        std::thread([notify, port, job]() {
            PortCheck result;
            try { result = check_port_online(port); }
            catch (std::exception const& ex) { result.done = true; result.status = ex.what(); }
            catch (...) { result.done = true; result.status = "Errore inatteso nella verifica porta"; }
            result.port = port;
            job->complete(std::move(result));
            PostMessageW(notify, WM_APP_PORTCHECK, 0, 0);
        }).detach();
    } catch (std::exception const& ex) {
        g.port_check_job.reset();
        g.port_check_running = false;
        log_line(L"Impossibile avviare la verifica porta: " + widen(ex.what()));
    }
}

// ------------------------------------------------------------------ startup
static void first_run_defaults() {
    // Default memory profile "Bilanciato": 32 MiB write cache instead of libtorrent's 100 MiB.
    if (g.settings.perf_profile.empty() && g.settings.int_settings.empty()) {
        g.settings.int_settings = tuning::memory_profile(1);
        g.settings.perf_profile = "Bilanciato";
    }
    if (g.settings.stats.since == 0) g.settings.stats.since = static_cast<std::int64_t>(std::time(nullptr));
}

static int run(HINSTANCE inst, int show) {
    g.inst = inst;
    std::wstring cmdline = GetCommandLineW();

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\SeedFlow.SingleInstance.1");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowW(kClass, nullptr);
        if (other) {
            int argc = 0;
            LPWSTR* argv = CommandLineToArgvW(cmdline.c_str(), &argc);
            std::wstring args;
            for (int i = 1; argv && i < argc; ++i) { args += L"\""; args += argv[i]; args += L"\" "; }
            if (argv) LocalFree(argv);
            COPYDATASTRUCT cds{0x5346, static_cast<DWORD>((args.size() + 1) * sizeof(wchar_t)), const_cast<wchar_t*>(args.c_str())};
            DWORD_PTR res = 0;
            SendMessageTimeoutW(other, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds), SMTO_ABORTIFHUNG, 3000, &res);
        }
        if (mutex) CloseHandle(mutex);
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_TAB_CLASSES | ICC_LINK_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);

    // settings + one-time migration from SeedFlow 0.7.x (WPF)
    bool loaded = load_settings(g.settings);
    LegacyImport legacy;
    if (!loaded && !g.settings.migrated) legacy = import_legacy(g.settings);
    first_run_defaults();
    apply_process_priority(g.settings.process_priority);
    ++g.settings.stats.launches;
    g.split_left = g.settings.split_left;
    g.split_bottom = g.settings.split_bottom;

    g.app_icon = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0));
    g.app_icon_small = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    g.taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    g.accel = LoadAcceleratorsW(inst, MAKEINTRESOURCEW(IDR_ACCEL));

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = general_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kGeneralClass;
    RegisterClassExW(&wc);
    wc.lpfnWndProc = speed_graph_proc;
    wc.lpszClassName = kSpeedClass;
    RegisterClassExW(&wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hIcon = g.app_icon;
    wc.hIconSm = g.app_icon_small;
    wc.lpszMenuName = MAKEINTRESOURCEW(IDR_MAINMENU);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);

    int w = g.settings.win_w > 0 ? g.settings.win_w : 1280, h = g.settings.win_h > 0 ? g.settings.win_h : 740;
    HWND hwnd = CreateWindowExW(0, kClass, L"SeedFlow " SF_VERSION_WSTRING, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, w, h, nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;
    // restore placement (pixel sizes stored at 96 DPI)
    if (g.settings.win_x >= 0 && g.settings.win_y >= 0) {
        RECT r{g.settings.win_x, g.settings.win_y, g.settings.win_x + scale(w), g.settings.win_y + scale(h)};
        HMONITOR mon = MonitorFromRect(&r, MONITOR_DEFAULTTONULL);
        if (mon) SetWindowPos(hwnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER);
    } else SetWindowPos(hwnd, nullptr, 0, 0, scale(w), scale(h), SWP_NOMOVE | SWP_NOZORDER);

    // engine
    EngineConfig cfg;
    cfg.disk_io = g.settings.disk_io;
    if (g.settings.random_port_start) {
        int const old_port = g.settings.port;
        g.settings.port = random_listen_port(old_port);
        log_line(L"Porta casuale all'avvio: " + std::to_wstring(g.settings.port) + L" (prima " + std::to_wstring(old_port) + L").");
    }
    cfg.port = g.settings.port;
    cfg.dht = g.settings.dht; cfg.pex = g.settings.pex; cfg.ipv6 = g.settings.ipv6; cfg.upnp = g.settings.upnp; cfg.natpmp = g.settings.natpmp; cfg.lsd = g.settings.lsd;
    cfg.user_agent = g.settings.user_agent;
    cfg.state_dir = state_dir();
    std::string err;
    if (!g.engine.start(cfg, err)) {
        MessageBoxW(hwnd, (L"Impossibile avviare il motore libtorrent:\n\n" + widen(err)).c_str(), L"SeedFlow", MB_ICONERROR);
    } else {
        log_line(L"SeedFlow " SF_VERSION_WSTRING L" — libtorrent " + widen(Engine::libtorrent_version()) + L", I/O disco: " + widen(g.engine.disk_io_name()));
        apply_session_settings();
        std::vector<std::string> msgs;
        int n = g.engine.load_saved_torrents(state_dir(), msgs);
        if (!g.settings.migrated) {
            // previous versions stored fast resume files in %LOCALAPPDATA%\SeedFlow\Resume
            int m = g.engine.load_saved_torrents(app_data_dir() / L"Resume", msgs);
            std::string folder = legacy.folder.empty() ? download_dir() : legacy.folder;
            for (auto const& mag : legacy.magnets) { std::string e2; if (g.engine.add_magnet(mag, folder, false, e2) > 0) ++m; }
            for (auto const& tf : legacy.torrent_files) { std::string e2; if (g.engine.add_torrent_file(tf, folder, false, e2) > 0) ++m; }
            if (m > 0) log_line(L"Importati " + std::to_wstring(m) + L" torrent dalla versione precedente.");
            n += m;
            g.settings.migrated = true;
        }
        for (auto const& msg : msgs) log_line(widen(msg));
        log_line(L"Torrent ripristinati: " + std::to_wstring(n));
        for (int id : g.engine.ids()) { std::string hsh = g.engine.hash_of(id); if (!hsh.empty()) meta_for(hsh); }
        if (g.settings.auto_port_check) start_port_check(hwnd);
        persist();
    }

    bool start_hidden = cmdline.find(L"--tray") != std::wstring::npos;   // "start with Windows" (minimised)
    if (start_hidden) hide_to_tray();
    else ShowWindow(hwnd, g.settings.win_max ? SW_SHOWMAXIMIZED : show);
    UpdateWindow(hwnd);
    refresh_now();
    handle_arguments(cmdline);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (TranslateAcceleratorW(hwnd, g.accel, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    // Saves fast resume for every torrent (bounded wait) and the DHT routing table.
    g.engine.stop(std::chrono::seconds(10));
    for (HBITMAP b : menu_bitmaps) DeleteObject(b);
    WSACleanup();
    CoUninitialize();
    if (mutex) { ReleaseMutex(mutex); CloseHandle(mutex); }
    return 0;
}

} // namespace sf

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) { return sf::run(inst, show); }


