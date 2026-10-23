// SeedFlow - dialogs (templates in res/SeedFlow.rc).
#include "app.hpp"
#include "version.h"
#include "icons.hpp"
#include "resource.h"
#include "util.hpp"
#include "tuning.hpp"
#include "creator.hpp"
#include "file_io.hpp"
#include "paint.hpp"
#include <shobjidl.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <algorithm>
#include <cmath>
#include <random>
#include <map>

namespace fs = std::filesystem;

namespace sf {

// ------------------------------------------------------------------ helpers
static std::wstring get_text(HWND dlg, int id) {
    HWND c = GetDlgItem(dlg, id);
    int n = GetWindowTextLengthW(c);
    std::wstring s(static_cast<std::size_t>(n) + 1, L'\0');
    int copied = GetWindowTextW(c, s.data(), n + 1);
    s.resize(static_cast<std::size_t>(copied));
    return s;
}
static void set_text(HWND dlg, int id, std::wstring const& s) { SetDlgItemTextW(dlg, id, s.c_str()); }
static void set_int(HWND dlg, int id, long long v) { set_text(dlg, id, std::to_wstring(v)); }
static bool get_int(HWND dlg, int id, long long lo, long long hi, long long& out, wchar_t const* what) {
    if (!parse_int(get_text(dlg, id), out) || out < lo || out > hi) {
        std::wstring msg = std::wstring(what) + L": inserisci un valore tra " + std::to_wstring(lo) + L" e " + std::to_wstring(hi) + L".";
        MessageBoxW(dlg, msg.c_str(), L"SeedFlow", MB_ICONWARNING);
        SetFocus(GetDlgItem(dlg, id));
        SendDlgItemMessageW(dlg, id, EM_SETSEL, 0, -1);
        return false;
    }
    return true;
}
static void check(HWND dlg, int id, bool on) { CheckDlgButton(dlg, id, on ? BST_CHECKED : BST_UNCHECKED); }
static bool checked(HWND dlg, int id) { return IsDlgButtonChecked(dlg, id) == BST_CHECKED; }
static void combo_add(HWND dlg, int id, wchar_t const* text) { SendDlgItemMessageW(dlg, id, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text)); }
static void combo_sel(HWND dlg, int id, int index) { SendDlgItemMessageW(dlg, id, CB_SETCURSEL, static_cast<WPARAM>(index), 0); }
static int combo_get(HWND dlg, int id) { return static_cast<int>(SendDlgItemMessageW(dlg, id, CB_GETCURSEL, 0, 0)); }
static void set_mono(HWND dlg, int id) { SendDlgItemMessageW(dlg, id, WM_SETFONT, reinterpret_cast<WPARAM>(g.mono), TRUE); }

bool pick_folder(HWND owner, std::wstring& folder) {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return false;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    dlg->SetTitle(L"Cartella di download");
    IShellItem* start = nullptr;
    if (!folder.empty() && SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(&start)))) {
        dlg->SetFolder(start);
        start->Release();
    }
    bool ok = false;
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) { folder = p; CoTaskMemFree(p); ok = true; }
            item->Release();
        }
    }
    dlg->Release();
    return ok;
}

// Normalizes "\n" to "\r\n" for multiline EDIT controls.
static std::wstring crlf(std::wstring const& s) {
    std::wstring out;
    for (wchar_t c : s) { if (c == L'\n') out += L"\r\n"; else if (c != L'\r') out += c; }
    return out;
}

// ------------------------------------------------------------------ save folder (shared by the magnet and torrent dialogs)
void remember_dir(std::string const& folder) {
    if (folder.empty()) return;
    auto& dirs = g.settings.recent_dirs;
    std::wstring const wanted = widen(folder);
    dirs.erase(std::remove_if(dirs.begin(), dirs.end(), [&](std::string const& d) {
        return _wcsicmp(widen(d).c_str(), wanted.c_str()) == 0; }), dirs.end());
    dirs.insert(dirs.begin(), folder);
    if (dirs.size() > 8) dirs.resize(8);
}
// Destination combo: the proposed folder first, then the recent ones.
static void fill_folder_combo(HWND dlg, int id, std::string const& current) {
    std::vector<std::wstring> items{widen(current)};
    for (auto const& d : g.settings.recent_dirs) {
        std::wstring w = widen(d);
        bool dup = false;
        for (auto const& x : items) dup = dup || _wcsicmp(x.c_str(), w.c_str()) == 0;
        if (!dup) items.push_back(w);
    }
    for (auto const& w : items) combo_add(dlg, id, w.c_str());
    set_text(dlg, id, items.front());
}
// Reads the folder from the dialog; it must be absolute, and is created on request.
static bool take_folder(HWND dlg, int id, std::string& out) {
    std::wstring f = get_text(dlg, id);
    while (f.size() > 3 && (f.back() == L'\\' || f.back() == L'/')) f.pop_back();
    std::error_code ec;
    if (f.empty() || !fs::path(f).is_absolute()) {
        MessageBoxW(dlg, L"Indica un percorso completo (es. D:\\Download).", L"SeedFlow", MB_ICONWARNING);
        return false;
    }
    if (!fs::is_directory(fs::path(f), ec)) {
        if (MessageBoxW(dlg, (L"La cartella «" + f + L"» non esiste. Crearla?").c_str(), L"SeedFlow", MB_ICONQUESTION | MB_YESNO) != IDYES) return false;
        if (!fs::create_directories(fs::path(f), ec) && !fs::is_directory(fs::path(f), ec)) {
            MessageBoxW(dlg, L"Impossibile creare la cartella.", L"SeedFlow", MB_ICONWARNING);
            return false;
        }
    }
    out = narrow(f);
    return true;
}
// ------------------------------------------------------------------ magnet
struct MagnetCtx { std::vector<std::string>* magnets; std::string* folder; bool* paused; };
static INT_PTR CALLBACK magnet_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* ctx = reinterpret_cast<MagnetCtx*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        ctx = reinterpret_cast<MagnetCtx*>(lp);
        fill_folder_combo(dlg, IDC_FOLDER, *ctx->folder);
        check(dlg, IDC_PAUSED, *ctx->paused);
        std::wstring clip = clipboard_text(dlg);
        if (clip.find(L"magnet:?") != std::wstring::npos) set_text(dlg, IDC_EDIT, crlf(clip));
        SetFocus(GetDlgItem(dlg, IDC_EDIT));
        return FALSE;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_BROWSE: { std::wstring f = get_text(dlg, IDC_FOLDER); if (pick_folder(dlg, f)) set_text(dlg, IDC_FOLDER, f); return TRUE; }
        case IDOK: {
            std::wstring text = get_text(dlg, IDC_EDIT), line;
            ctx->magnets->clear();
            auto take = [&]() {
                auto p = line.find(L"magnet:?");
                if (p != std::wstring::npos) {
                    std::wstring m = line.substr(p);
                    while (!m.empty() && iswspace(m.back())) m.pop_back();
                    ctx->magnets->push_back(narrow(m));
                }
                line.clear();
            };
            for (wchar_t c : text) { if (c == L'\n') take(); else if (c != L'\r') line += c; }
            take();
            if (ctx->magnets->empty()) { MessageBoxW(dlg, L"Nessun magnet link valido (deve iniziare con magnet:?).", L"SeedFlow", MB_ICONWARNING); return TRUE; }
            if (!take_folder(dlg, IDC_FOLDER, *ctx->folder)) return TRUE;
            *ctx->paused = checked(dlg, IDC_PAUSED);
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        case IDCANCEL: EndDialog(dlg, IDCANCEL); return TRUE;
        }
        break;
    }
    return FALSE;
}
// ------------------------------------------------------------------ add torrent (save path)
struct AddCtx { std::wstring summary; std::string* folder; bool* paused; bool* dont_ask; };
static INT_PTR CALLBACK add_torrent_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* ctx = reinterpret_cast<AddCtx*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        ctx = reinterpret_cast<AddCtx*>(lp);
        set_text(dlg, IDC_A_INFO, ctx->summary);
        fill_folder_combo(dlg, IDC_A_FOLDER, *ctx->folder);
        check(dlg, IDC_PAUSED, *ctx->paused);
        check(dlg, IDC_A_NOASK, false);
        return TRUE;
    }
    case WM_COMMAND:
        if (!ctx) break;
        switch (LOWORD(wp)) {
        case IDC_BROWSE: { std::wstring f = get_text(dlg, IDC_A_FOLDER); if (pick_folder(dlg, f)) set_text(dlg, IDC_A_FOLDER, f); return TRUE; }
        case IDOK: {
            if (!take_folder(dlg, IDC_A_FOLDER, *ctx->folder)) return TRUE;
            *ctx->paused = checked(dlg, IDC_PAUSED);
            *ctx->dont_ask = checked(dlg, IDC_A_NOASK);
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        case IDCANCEL: EndDialog(dlg, IDCANCEL); return TRUE;
        }
        break;
    }
    return FALSE;
}
bool dlg_add_torrent(HWND owner, std::wstring const& summary, std::string& folder, bool& paused, bool& dont_ask) {
    AddCtx ctx{summary, &folder, &paused, &dont_ask};
    return DialogBoxParamW(g.inst, MAKEINTRESOURCEW(IDD_ADDTORRENT), owner, add_torrent_proc, reinterpret_cast<LPARAM>(&ctx)) == IDOK;
}

bool dlg_magnet(HWND owner, std::vector<std::string>& magnets, std::string& folder, bool& paused) {
    MagnetCtx ctx{&magnets, &folder, &paused};
    return DialogBoxParamW(g.inst, MAKEINTRESOURCEW(IDD_MAGNET), owner, magnet_proc, reinterpret_cast<LPARAM>(&ctx)) == IDOK;
}

// ------------------------------------------------------------------ transfers / ratio
static void fill_ratio_actions(HWND dlg, int id, bool with_global) {
    if (with_global) combo_add(dlg, id, L"Usa l'azione globale");
    for (auto const* label : kRatioActionLabels) combo_add(dlg, id, label);
}
struct RatioCtx { std::vector<int> ids; };
static INT_PTR CALLBACK ratio_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* ctx = reinterpret_cast<RatioCtx*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        ctx = reinterpret_cast<RatioCtx*>(lp);
        Row const* r = row_by_id(ctx->ids[0]);
        auto const& m = meta_for(r ? r->v.hash : g.engine.hash_of(ctx->ids[0]));
        std::wstring info = ctx->ids.size() > 1 ? std::to_wstring(ctx->ids.size()) + L" torrent selezionati."
            : L"«" + widen(r ? r->v.name : "") + L"»\nRatio attuale: " + (r ? fmt_ratio(r->ratio) : L"—");
        set_text(dlg, IDC_R_CURRENT, info);
        CheckRadioButton(dlg, IDC_R_GLOBAL, IDC_R_CUSTOM, m.ratio_mode == RatioNone ? IDC_R_NONE : m.ratio_mode == RatioCustom ? IDC_R_CUSTOM : IDC_R_GLOBAL);
        set_text(dlg, IDC_RATIO, fmt_double(m.ratio_limit > 0 ? m.ratio_limit : 2.0, 2));
        fill_ratio_actions(dlg, IDC_RATIOACTION, true);
        combo_sel(dlg, IDC_RATIOACTION, m.ratio_action + 1);
        std::wstring global = g.settings.global_ratio > 0 ? L"Usa l'impostazione globale (" + fmt_double(g.settings.global_ratio, 2) + L")"
                                                         : L"Usa l'impostazione globale (disattivata)";
        set_text(dlg, IDC_R_GLOBAL, global);
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDOK: {
            int mode = checked(dlg, IDC_R_NONE) ? RatioNone : checked(dlg, IDC_R_CUSTOM) ? RatioCustom : RatioGlobal;
            double limit = 0;
            if (mode == RatioCustom && (!parse_double(get_text(dlg, IDC_RATIO), limit) || limit <= 0 || limit > 10000)) {
                MessageBoxW(dlg, L"Inserisci un ratio maggiore di 0 (es. 10).", L"SeedFlow", MB_ICONWARNING); return TRUE;
            }
            int action = combo_get(dlg, IDC_RATIOACTION) - 1;
            if (action == ActRemoveDelete && MessageBoxW(dlg, L"Al raggiungimento del ratio il torrent verrà rimosso e i suoi FILE CANCELLATI. Confermi?",
                    L"SeedFlow", MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES) return TRUE;
            for (int id : ctx->ids) {
                std::string hash = g.engine.hash_of(id);
                if (hash.empty()) continue;
                auto& m = meta_for(hash);
                m.ratio_mode = mode;
                if (mode == RatioCustom) m.ratio_limit = limit;
                m.ratio_action = action;
                m.ratio_done = false;
            }
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        case IDCANCEL: EndDialog(dlg, IDCANCEL); return TRUE;
        }
        break;
    }
    return FALSE;
}
bool dlg_ratio(HWND owner, std::vector<int> const& ids) {
    RatioCtx ctx{ids};
    return DialogBoxParamW(g.inst, MAKEINTRESOURCEW(IDD_RATIO), owner, ratio_proc, reinterpret_cast<LPARAM>(&ctx)) == IDOK;
}

struct LimitsCtx { int* down; int* up; };
static INT_PTR CALLBACK limits_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* ctx = reinterpret_cast<LimitsCtx*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg) {
    case WM_INITDIALOG:
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        ctx = reinterpret_cast<LimitsCtx*>(lp);
        set_int(dlg, IDC_DOWN, *ctx->down); set_int(dlg, IDC_UP, *ctx->up);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            long long d, u;
            if (!get_int(dlg, IDC_DOWN, 0, 2000000, d, L"Download") || !get_int(dlg, IDC_UP, 0, 2000000, u, L"Upload")) return TRUE;
            *ctx->down = static_cast<int>(d); *ctx->up = static_cast<int>(u);
            EndDialog(dlg, IDOK); return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) { EndDialog(dlg, IDCANCEL); return TRUE; }
        break;
    }
    return FALSE;
}
bool dlg_limits(HWND owner, int& down, int& up) {
    LimitsCtx ctx{&down, &up};
    return DialogBoxParamW(g.inst, MAKEINTRESOURCEW(IDD_LIMITS), owner, limits_proc, reinterpret_cast<LPARAM>(&ctx)) == IDOK;
}

static INT_PTR CALLBACK trackers_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        std::wstring clip = clipboard_text(dlg);
        if (clip.find(L"://") != std::wstring::npos && clip.find(L"announce") != std::wstring::npos) set_text(dlg, IDC_EDIT, crlf(clip));
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            *reinterpret_cast<std::wstring*>(GetWindowLongPtrW(dlg, DWLP_USER)) = get_text(dlg, IDC_EDIT);
            EndDialog(dlg, IDOK); return TRUE;
        }
        if (LOWORD(wp) == IDCANCEL) { EndDialog(dlg, IDCANCEL); return TRUE; }
        break;
    }
    return FALSE;
}
bool dlg_trackers(HWND owner, std::wstring& text) {
    return DialogBoxParamW(g.inst, MAKEINTRESOURCEW(IDD_TRACKERS), owner, trackers_proc, reinterpret_cast<LPARAM>(&text)) == IDOK && !text.empty();
}

// ------------------------------------------------------------------ user agent / identity
struct UaPreset { wchar_t const* name; char const* agent; char const* peer; char const* client; };
// Versions checked 22 October 2026: qBittorrent 5.3.0, Transmission 4.1.3, Deluge 2.2.0 (release tags on GitHub).
// peer = peer ID prefix.
static UaPreset const kUaPresets[] = {
    {L"SeedFlow " SF_VERSION_WSTRING L" (predefinito)", SF_USER_AGENT, "", ""},
    {L"qBittorrent 5.3.0", "qBittorrent/5.3.0", "-qB5300-", "qBittorrent/5.3.0"},
    {L"Transmission 4.1.3", "Transmission/4.1.3", "-TR4130-", "Transmission 4.1.3"},
    {L"Deluge 2.2.0", "Deluge/2.2.0 libtorrent/2.0.11.0", "-DE220s-", "Deluge 2.2.0"},
    {L"libtorrent 2.1.2", "libtorrent/2.1.2.0", "-LT2120-", "libtorrent/2.1.2.0"},
    {L"Personalizzato", nullptr, nullptr, nullptr},
};
static bool valid_ascii(std::wstring const& v, std::size_t max) {
    if (v.size() > max) return false;
    for (wchar_t c : v) if (c < 32 || c > 126) return false;
    return true;
}
// ------------------------------------------------------------------ performance
static void perf_fill(HWND dlg, std::map<std::string, int> const& values) {
    auto const& fields = tuning::memory_fields();
    for (std::size_t i = 0; i < fields.size() && i < 10; ++i) {
        auto const& f = fields[i];
        int v = 0;
        auto it = values.find(f.name);
        if (it != values.end()) v = it->second;
        else g.engine.get_int(f.name, true, v);
        set_int(dlg, IDC_FIELD0 + static_cast<int>(i), (v + f.scale / 2) / f.scale);
    }
}
static void perf_meminfo(HWND dlg) {
    auto m = memory_usage();
    auto c = g.engine.counters();
    std::wstring t = L"Memoria del processo: " + fmt_bytes(static_cast<std::int64_t>(m.working_set)) + L" (working set), " +
        fmt_bytes(static_cast<std::int64_t>(m.private_bytes)) + L" privata, picco " + fmt_bytes(static_cast<std::int64_t>(m.peak_working_set)) +
        L".\nCache libtorrent in uso: " + fmt_bytes(c.disk_blocks_in_use * 16384) + L"; blocchi in cache: " + fmt_bytes(c.cached_blocks * 16384) +
        L"; I/O disco attivo: " + widen(g.engine.disk_io_name()) + L".";
    set_text(dlg, IDC_MEMINFO, t);
}
// ------------------------------------------------------------------ preferences
// Unified dialog (uTorrent style): tree on the left, one child page per section.
// Every page is created at start; OK / Applica asks each page to validate and store
// its values (WM_PAGE_APPLY), stopping at the first page that refuses.
constexpr UINT WM_PAGE_APPLY = WM_APP + 60;     // page -> DWLP_MSGRESULT TRUE if stored
static bool pref_autostart = false;             // applied to the registry only when every page is valid
static INT_PTR page_result(HWND dlg, bool ok) { SetWindowLongPtrW(dlg, DWLP_MSGRESULT, ok ? TRUE : FALSE); return TRUE; }

static INT_PTR CALLBACK pg_general(HWND dlg, UINT msg, WPARAM wp, LPARAM) {
    auto& s = g.settings;
    switch (msg) {
    case WM_INITDIALOG:
        check(dlg, IDC_CLOSETRAY, s.close_to_tray); check(dlg, IDC_MINTRAY, s.minimize_to_tray);
        check(dlg, IDC_CONFIRMREMOVE, s.confirm_remove); check(dlg, IDC_STARTPAUSED, s.start_paused);
        check(dlg, IDC_ASKPATH, s.ask_save_path);
        for (int i = 0; i < 6; ++i) check(dlg, IDC_SB_NET + i, (s.status_panes >> i) & 1);
        check(dlg, IDC_AUTOSTART, autostart_enabled()); check(dlg, IDC_STARTMIN, s.start_minimized);
        EnableWindow(GetDlgItem(dlg, IDC_STARTMIN), checked(dlg, IDC_AUTOSTART));
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_AUTOSTART) EnableWindow(GetDlgItem(dlg, IDC_STARTMIN), checked(dlg, IDC_AUTOSTART));
        break;
    case WM_PAGE_APPLY: {
        s.close_to_tray = checked(dlg, IDC_CLOSETRAY); s.minimize_to_tray = checked(dlg, IDC_MINTRAY);
        s.confirm_remove = checked(dlg, IDC_CONFIRMREMOVE); s.start_paused = checked(dlg, IDC_STARTPAUSED);
        s.ask_save_path = checked(dlg, IDC_ASKPATH);
        s.status_panes = 0;
        for (int i = 0; i < 6; ++i) if (checked(dlg, IDC_SB_NET + i)) s.status_panes |= 1 << i;
        s.start_minimized = checked(dlg, IDC_STARTMIN);
        pref_autostart = checked(dlg, IDC_AUTOSTART);
        return page_result(dlg, true);
    }
    }
    return FALSE;
}

static INT_PTR CALLBACK pg_folders(HWND dlg, UINT msg, WPARAM wp, LPARAM) {
    auto& s = g.settings;
    switch (msg) {
    case WM_INITDIALOG:
        set_text(dlg, IDC_FOLDER, widen(download_dir()));
        check(dlg, IDC_PREALLOC, s.preallocate);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_BROWSE) {
            std::wstring f = get_text(dlg, IDC_FOLDER);
            if (pick_folder(dlg, f)) set_text(dlg, IDC_FOLDER, f);
            return TRUE;
        }
        break;
    case WM_PAGE_APPLY: {
        std::wstring f = get_text(dlg, IDC_FOLDER);
        std::error_code ec;
        if (f.empty() || !fs::path(f).is_absolute()) {
            MessageBoxW(dlg, L"Cartella di download: indica un percorso completo (es. D:\\Download).", L"SeedFlow", MB_ICONWARNING);
            return page_result(dlg, false);
        }
        if (!fs::is_directory(fs::path(f), ec)) {
            if (MessageBoxW(dlg, (L"La cartella «" + f + L"» non esiste. Crearla?").c_str(), L"SeedFlow", MB_ICONQUESTION | MB_YESNO) != IDYES
                || !fs::create_directories(fs::path(f), ec)) return page_result(dlg, false);
        }
        if (narrow(f) != s.download_dir) log_line(L"Cartella di download: " + f);
        s.download_dir = narrow(f);
        s.preallocate = checked(dlg, IDC_PREALLOC);
        return page_result(dlg, true);
    }
    }
    return FALSE;
}

static INT_PTR CALLBACK pg_connection(HWND dlg, UINT msg, WPARAM wp, LPARAM) {
    auto& s = g.settings;
    switch (msg) {
    case WM_INITDIALOG:
        set_int(dlg, IDC_PORT, s.port);
        check(dlg, IDC_UPNP, s.upnp); check(dlg, IDC_NATPMP, s.natpmp); check(dlg, IDC_AUTOCHECK, s.auto_port_check);
        check(dlg, IDC_RANDOMSTART, s.random_port_start);
        check(dlg, IDC_IPV6, s.ipv6);
        combo_add(dlg, IDC_ENCRYPTION, L"Consentita (accetta anche connessioni in chiaro)");
        combo_add(dlg, IDC_ENCRYPTION, L"Preferita (cifra quando il peer lo supporta)");
        combo_add(dlg, IDC_ENCRYPTION, L"Richiesta (solo connessioni cifrate)");
        combo_sel(dlg, IDC_ENCRYPTION, std::clamp(s.encryption, 0, 2));
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_RANDOMPORT) {
            long long current = 0;
            parse_int(get_text(dlg, IDC_PORT), current);
            set_int(dlg, IDC_PORT, random_listen_port(static_cast<int>(current)));
            return TRUE;
        }
        break;
    case WM_PAGE_APPLY: {
        long long port;
        if (!get_int(dlg, IDC_PORT, 1024, 65535, port, L"Porta")) return page_result(dlg, false);
        s.port = static_cast<int>(port);
        s.upnp = checked(dlg, IDC_UPNP); s.natpmp = checked(dlg, IDC_NATPMP); s.auto_port_check = checked(dlg, IDC_AUTOCHECK);
        s.random_port_start = checked(dlg, IDC_RANDOMSTART);
        s.ipv6 = checked(dlg, IDC_IPV6);
        s.encryption = std::max(0, combo_get(dlg, IDC_ENCRYPTION));
        return page_result(dlg, true);
    }
    }
    return FALSE;
}

static INT_PTR CALLBACK pg_bandwidth(HWND dlg, UINT msg, WPARAM, LPARAM) {
    auto& s = g.settings;
    switch (msg) {
    case WM_INITDIALOG:
        set_int(dlg, IDC_DOWN, s.down_kib); set_int(dlg, IDC_UP, s.up_kib);
        set_int(dlg, IDC_CONNECTIONS, s.connections); set_int(dlg, IDC_SLOTS, s.upload_slots);
        return TRUE;
    case WM_PAGE_APPLY: {
        long long down, up, conn, slots;
        if (!get_int(dlg, IDC_DOWN, 0, 2000000, down, L"Download") || !get_int(dlg, IDC_UP, 0, 2000000, up, L"Upload") ||
            !get_int(dlg, IDC_CONNECTIONS, 10, 20000, conn, L"Connessioni") || !get_int(dlg, IDC_SLOTS, -1, 2000, slots, L"Slot upload"))
            return page_result(dlg, false);
        if (slots == 0) { MessageBoxW(dlg, L"Slot upload: usa -1 per illimitati oppure un valore positivo.", L"SeedFlow", MB_ICONWARNING); return page_result(dlg, false); }
        s.down_kib = static_cast<int>(down); s.up_kib = static_cast<int>(up);
        s.connections = static_cast<int>(conn); s.upload_slots = static_cast<int>(slots);
        return page_result(dlg, true);
    }
    }
    return FALSE;
}

static INT_PTR CALLBACK pg_bittorrent(HWND dlg, UINT msg, WPARAM, LPARAM) {
    auto& s = g.settings;
    switch (msg) {
    case WM_INITDIALOG:
        check(dlg, IDC_DHT, s.dht); check(dlg, IDC_PEX, s.pex); check(dlg, IDC_LSD, s.lsd);
        check(dlg, IDC_ALLTRACKERS, s.announce_all_trackers); check(dlg, IDC_ALLTIERS, s.announce_all_tiers);
        return TRUE;
    case WM_PAGE_APPLY:
        if (checked(dlg, IDC_PEX) != s.pex) MessageBoxW(dlg, L"La modifica del PeX sarà applicata al prossimo avvio di SeedFlow.", L"SeedFlow", MB_ICONINFORMATION);
        s.dht = checked(dlg, IDC_DHT); s.pex = checked(dlg, IDC_PEX); s.lsd = checked(dlg, IDC_LSD);
        s.announce_all_trackers = checked(dlg, IDC_ALLTRACKERS); s.announce_all_tiers = checked(dlg, IDC_ALLTIERS);
        return page_result(dlg, true);
    }
    return FALSE;
}

static INT_PTR CALLBACK pg_queue(HWND dlg, UINT msg, WPARAM, LPARAM) {
    auto& s = g.settings;
    switch (msg) {
    case WM_INITDIALOG:
        set_int(dlg, IDC_ACTIVEDL, s.active_downloads); set_int(dlg, IDC_ACTIVESEED, s.active_seeds);
        check(dlg, IDC_RATIOON, s.global_ratio > 0);
        set_text(dlg, IDC_RATIO, fmt_double(s.global_ratio > 0 ? s.global_ratio : 2.0, 2));
        fill_ratio_actions(dlg, IDC_RATIOACTION, false);
        combo_sel(dlg, IDC_RATIOACTION, std::clamp(s.global_ratio_action, 0, 3));
        return TRUE;
    case WM_PAGE_APPLY: {
        long long adl, aseed;
        if (!get_int(dlg, IDC_ACTIVEDL, 1, 1000, adl, L"Download attivi") || !get_int(dlg, IDC_ACTIVESEED, 1, 1000, aseed, L"Seed attivi"))
            return page_result(dlg, false);
        double ratio = 0;
        bool on = checked(dlg, IDC_RATIOON);
        if (on && (!parse_double(get_text(dlg, IDC_RATIO), ratio) || ratio <= 0 || ratio > 10000)) {
            MessageBoxW(dlg, L"Ratio: inserisci un numero maggiore di 0 (es. 2 oppure 10).", L"SeedFlow", MB_ICONWARNING);
            return page_result(dlg, false);
        }
        int action = std::max(0, combo_get(dlg, IDC_RATIOACTION));
        double new_ratio = on ? ratio : 0;
        bool changed = std::fabs(new_ratio - s.global_ratio) > 1e-9 || action != s.global_ratio_action;
        if (changed && on && action == ActRemoveDelete &&
            MessageBoxW(dlg, L"Al raggiungimento del ratio i torrent verranno rimossi e i loro FILE CANCELLATI dal disco. Confermi?",
                L"SeedFlow", MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) != IDYES) return page_result(dlg, false);
        if (changed) for (auto& [hash, m] : s.torrents) if (m.ratio_mode == RatioGlobal) m.ratio_done = false; // new limit: re-arm
        s.active_downloads = static_cast<int>(adl); s.active_seeds = static_cast<int>(aseed);
        s.global_ratio = new_ratio;
        s.global_ratio_action = action;
        return page_result(dlg, true);
    }
    }
    return FALSE;
}

static INT_PTR CALLBACK pg_identity(HWND dlg, UINT msg, WPARAM wp, LPARAM) {
    auto& s = g.settings;
    int const custom = static_cast<int>(std::size(kUaPresets)) - 1;
    auto set_fields_editable = [&](bool on) {
        for (int id : {IDC_UA_VALUE, IDC_UA_PEERID, IDC_UA_CLIENT}) SendDlgItemMessageW(dlg, id, EM_SETREADONLY, !on, 0);
    };
    switch (msg) {
    case WM_INITDIALOG: {
        int sel = custom;
        for (int i = 0; i < custom; ++i) {
            combo_add(dlg, IDC_UA_PRESET, kUaPresets[i].name);
            auto const& p = kUaPresets[i];
            bool peer_ok = p.peer ? s.peer_id_prefix == p.peer : s.peer_id_prefix.empty();
            bool client_ok = p.client ? s.client_name == p.client : s.client_name.empty();
            if (s.user_agent == p.agent && peer_ok && client_ok) sel = i;
        }
        combo_add(dlg, IDC_UA_PRESET, kUaPresets[custom].name);
        combo_sel(dlg, IDC_UA_PRESET, sel);
        set_text(dlg, IDC_UA_VALUE, widen(s.user_agent));
        set_text(dlg, IDC_UA_PEERID, widen(s.peer_id_prefix));
        set_text(dlg, IDC_UA_CLIENT, widen(s.client_name));
        set_fields_editable(sel == custom);
        check(dlg, IDC_UA_HANDSHAKE, s.send_client_name);
        check(dlg, IDC_UA_ANON, s.anonymous_mode);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_UA_PRESET && HIWORD(wp) == CBN_SELCHANGE) {
            int sel = combo_get(dlg, IDC_UA_PRESET);
            bool is_custom = sel == custom || sel < 0;
            set_fields_editable(is_custom);
            if (!is_custom) {
                auto const& p = kUaPresets[sel];
                set_text(dlg, IDC_UA_VALUE, widen(p.agent));
                set_text(dlg, IDC_UA_PEERID, widen(p.peer ? p.peer : ""));
                set_text(dlg, IDC_UA_CLIENT, widen(p.client ? p.client : ""));
            } else SetFocus(GetDlgItem(dlg, IDC_UA_VALUE));
            return TRUE;
        }
        break;
    case WM_PAGE_APPLY: {
        std::wstring ua = get_text(dlg, IDC_UA_VALUE), peer = get_text(dlg, IDC_UA_PEERID), client = get_text(dlg, IDC_UA_CLIENT);
        if (ua.empty() || !valid_ascii(ua, 120)) { MessageBoxW(dlg, L"User-Agent: da 1 a 120 caratteri ASCII stampabili.", L"SeedFlow", MB_ICONWARNING); return page_result(dlg, false); }
        if (!valid_ascii(peer, 20)) { MessageBoxW(dlg, L"Prefisso peer ID: massimo 20 caratteri ASCII (formato tipico -XX1234-).", L"SeedFlow", MB_ICONWARNING); return page_result(dlg, false); }
        if (!valid_ascii(client, 60)) { MessageBoxW(dlg, L"Nome client: massimo 60 caratteri ASCII.", L"SeedFlow", MB_ICONWARNING); return page_result(dlg, false); }
        bool changed = narrow(ua) != s.user_agent || narrow(peer) != s.peer_id_prefix || narrow(client) != s.client_name;
        s.user_agent = narrow(ua);
        s.peer_id_prefix = narrow(peer);
        s.client_name = narrow(client);
        s.send_client_name = checked(dlg, IDC_UA_HANDSHAKE);
        s.anonymous_mode = checked(dlg, IDC_UA_ANON);
        if (changed) log_line(L"Identità client: UA «" + ua + L"», peer ID " + (peer.empty() ? std::wstring(L"SeedFlow predefinito") : peer));
        return page_result(dlg, true);
    }
    }
    return FALSE;
}

static INT_PTR CALLBACK pg_system(HWND dlg, UINT msg, WPARAM, LPARAM) {
    auto& s = g.settings;
    switch (msg) {
    case WM_INITDIALOG:
        combo_add(dlg, IDC_PRIORITY, L"Normale (predefinita)");
        combo_add(dlg, IDC_PRIORITY, L"Sotto il normale");
        combo_add(dlg, IDC_PRIORITY, L"Bassa");
        combo_sel(dlg, IDC_PRIORITY, std::clamp(s.process_priority, 0, 2));
        return TRUE;
    case WM_PAGE_APPLY:
        s.process_priority = std::clamp(combo_get(dlg, IDC_PRIORITY), 0, 2);
        return page_result(dlg, true);
    }
    return FALSE;
}

static INT_PTR CALLBACK pg_performance(HWND dlg, UINT msg, WPARAM wp, LPARAM) {
    auto& s = g.settings;
    auto const& fields = tuning::memory_fields();
    switch (msg) {
    case WM_INITDIALOG: {
        for (std::size_t i = 0; i < fields.size() && i < 10; ++i) {
            set_text(dlg, IDC_FLABEL0 + static_cast<int>(i), fields[i].label);
            set_text(dlg, IDC_FUNIT0 + static_cast<int>(i), fields[i].unit);
        }
        for (auto const* n : tuning::kProfileNames) combo_add(dlg, IDC_PROFILE, n);
        int prof = 1;
        for (int i = 0; i < 3; ++i) if (widen(s.perf_profile) == tuning::kProfileNames[i]) prof = i;
        combo_sel(dlg, IDC_PROFILE, prof);
        combo_add(dlg, IDC_DISKIO, L"Predefinito (pread multithread + cache)");
        combo_add(dlg, IDC_DISKIO, L"pread (multithread + cache di scrittura)");
        combo_add(dlg, IDC_DISKIO, L"mmap (file mappati in memoria)");
        combo_add(dlg, IDC_DISKIO, L"POSIX (semplice, memoria minima)");
        combo_sel(dlg, IDC_DISKIO, std::clamp(s.disk_io, 0, 3));
        check(dlg, IDC_TRIM, s.trim_when_hidden);
        perf_fill(dlg, s.int_settings);
        set_text(dlg, IDC_HELPTEXT, L"Seleziona un campo per la descrizione. I valori si applicano subito, tranne il motore di I/O (al prossimo avvio).");
        perf_meminfo(dlg);
        SetTimer(dlg, 1, 2000, nullptr);
        return TRUE;
    }
    case WM_TIMER: if (IsWindowVisible(dlg)) perf_meminfo(dlg); return TRUE;
    case WM_DESTROY: KillTimer(dlg, 1); break;
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id >= IDC_FIELD0 && id < IDC_FIELD0 + 10 && HIWORD(wp) == EN_SETFOCUS) {
            std::size_t i = static_cast<std::size_t>(id - IDC_FIELD0);
            if (i < fields.size()) set_text(dlg, IDC_HELPTEXT, fields[i].help);
            return TRUE;
        }
        if (id == IDC_APPLYPROFILE) { perf_fill(dlg, tuning::memory_profile(std::max(0, combo_get(dlg, IDC_PROFILE)))); return TRUE; }
        if (id == IDC_DEFAULTS) {
            std::map<std::string, int> defs;
            for (auto const& f : fields) { int v = 0; if (g.engine.get_int(f.name, false, v)) defs[f.name] = v; }
            perf_fill(dlg, defs);
            return TRUE;
        }
        break;
    }
    case WM_PAGE_APPLY: {
        std::map<std::string, int> values;
        for (std::size_t i = 0; i < fields.size() && i < 10; ++i) {
            long long v;
            if (!get_int(dlg, IDC_FIELD0 + static_cast<int>(i), fields[i].min, fields[i].max, v, fields[i].label)) return page_result(dlg, false);
            values[fields[i].name] = static_cast<int>(v) * fields[i].scale;
        }
        for (auto const& [k, v] : values) s.int_settings[k] = v;
        int prof = std::max(0, combo_get(dlg, IDC_PROFILE));
        auto preset = tuning::memory_profile(prof);
        bool same = true;
        for (auto const& [k, v] : preset) if (values[k] != v) same = false;
        s.perf_profile = same ? narrow(tuning::kProfileNames[prof]) : "Personalizzato";
        int disk = std::max(0, combo_get(dlg, IDC_DISKIO));
        if (disk != s.disk_io) MessageBoxW(dlg, L"Il nuovo motore di I/O su disco sarà usato al prossimo avvio di SeedFlow.", L"SeedFlow", MB_ICONINFORMATION);
        s.disk_io = disk;
        s.trim_when_hidden = checked(dlg, IDC_TRIM);
        return page_result(dlg, true);
    }
    }
    return FALSE;
}

struct PageDef { int page; int parent; wchar_t const* title; int tmpl; DLGPROC proc; icons::Id icon; };
static PageDef const kPages[] = {
    {PrefGeneral, -1, L"Generale", IDD_PG_GENERAL, pg_general, icons::IcSettings},
    {PrefFolders, -1, L"Cartelle", IDD_PG_FOLDERS, pg_folders, icons::IcFolder},
    {PrefConnection, -1, L"Connessione", IDD_PG_CONNECTION, pg_connection, icons::IcGlobe},
    {PrefBandwidth, -1, L"Banda", IDD_PG_BANDWIDTH, pg_bandwidth, icons::IcBandwidth},
    {PrefBitTorrent, -1, L"BitTorrent", IDD_PG_BITTORRENT, pg_bittorrent, icons::IcMagnet},
    {PrefQueue, -1, L"Code e ratio", IDD_PG_QUEUE, pg_queue, icons::IcRatio},
    {PrefIdentity, -1, L"Identità", IDD_PG_IDENTITY, pg_identity, icons::IcIdentity},
    {PrefAdvanced, -1, L"Avanzate", IDD_PG_SYSTEM, pg_system, icons::IcPreferences},
    {PrefPerformance, PrefAdvanced, L"Prestazioni", IDD_PG_PERFORMANCE, pg_performance, icons::IcCpu},
};
struct PrefsCtx {
    int start = PrefGeneral, current = -1;
    HWND pages[std::size(kPages)] = {};
    HTREEITEM items[std::size(kPages)] = {};
    HIMAGELIST il = nullptr;
    HFONT title_font = nullptr;
    bool applied = false;
};
static void prefs_show(HWND dlg, PrefsCtx& c, int index) {
    if (index < 0 || index >= static_cast<int>(std::size(kPages)) || index == c.current) return;
    if (c.current >= 0) ShowWindow(c.pages[c.current], SW_HIDE);
    c.current = index;
    ShowWindow(c.pages[index], SW_SHOW);
    set_text(dlg, IDC_PAGETITLE, kPages[index].title);
    TreeView_SelectItem(GetDlgItem(dlg, IDC_PREFTREE), c.items[index]);
}
static bool prefs_apply(HWND dlg, PrefsCtx& c) {
    // Pages write straight into g.settings: keep a copy so that a page refusing its
    // values (and a later Annulla) cannot leave half-applied settings to be persisted.
    AppSettings const backup = g.settings;
    for (int i = 0; i < static_cast<int>(std::size(kPages)); ++i) {
        if (!SendMessageW(c.pages[i], WM_PAGE_APPLY, 0, 0)) { g.settings = backup; prefs_show(dlg, c, i); return false; }
    }
    if (pref_autostart != autostart_enabled() || pref_autostart) {
        if (!set_autostart(pref_autostart, g.settings.start_minimized))
            MessageBoxW(dlg, L"Impossibile aggiornare l'avvio automatico (registro di Windows).", L"SeedFlow", MB_ICONWARNING);
    }
    apply_session_settings();
    apply_process_priority(g.settings.process_priority);
    persist();
    c.applied = true;
    return true;
}
static INT_PTR CALLBACK prefs_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* c = reinterpret_cast<PrefsCtx*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        c = reinterpret_cast<PrefsCtx*>(lp);
        HWND tree = GetDlgItem(dlg, IDC_PREFTREE);
        SetWindowTheme(tree, L"Explorer", nullptr);
        UINT dpi = GetDpiForWindow(dlg);
        int isz = MulDiv(16, static_cast<int>(dpi), 96);
        icons::Id ids[std::size(kPages)];
        for (std::size_t i = 0; i < std::size(kPages); ++i) ids[i] = kPages[i].icon;
        c->il = icons::make_imagelist(ids, static_cast<int>(std::size(kPages)), isz);
        TreeView_SetImageList(tree, c->il, TVSIL_NORMAL);
        TreeView_SetItemHeight(tree, isz + MulDiv(8, static_cast<int>(dpi), 96));
        // bold page title
        LOGFONTW lf{};
        GetObjectW(reinterpret_cast<HFONT>(SendMessageW(dlg, WM_GETFONT, 0, 0)), sizeof(lf), &lf);
        lf.lfWeight = FW_SEMIBOLD;
        lf.lfHeight = lf.lfHeight * 5 / 4;
        c->title_font = CreateFontIndirectW(&lf);
        SendDlgItemMessageW(dlg, IDC_PAGETITLE, WM_SETFONT, reinterpret_cast<WPARAM>(c->title_font), FALSE);
        RECT area;
        GetWindowRect(GetDlgItem(dlg, IDC_PAGEAREA), &area);
        MapWindowPoints(nullptr, dlg, reinterpret_cast<POINT*>(&area), 2);
        for (std::size_t i = 0; i < std::size(kPages); ++i) {
            auto const& p = kPages[i];
            HWND page = CreateDialogParamW(g.inst, MAKEINTRESOURCEW(p.tmpl), dlg, p.proc, 0);
            c->pages[i] = page;
            SetWindowPos(page, GetDlgItem(dlg, IDC_PAGEAREA), area.left, area.top, area.right - area.left, area.bottom - area.top, SWP_NOACTIVATE | SWP_HIDEWINDOW);
            TVINSERTSTRUCTW ins{};
            ins.hParent = p.parent >= 0 ? c->items[p.parent] : TVI_ROOT;
            ins.hInsertAfter = TVI_LAST;
            ins.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE;
            ins.item.pszText = const_cast<wchar_t*>(p.title);
            ins.item.lParam = static_cast<LPARAM>(i);
            ins.item.iImage = ins.item.iSelectedImage = static_cast<int>(i);
            c->items[i] = TreeView_InsertItem(tree, &ins);
        }
        for (std::size_t i = 0; i < std::size(kPages); ++i) if (kPages[i].parent < 0) TreeView_Expand(tree, c->items[i], TVE_EXPAND);
        int start = 0;
        for (std::size_t i = 0; i < std::size(kPages); ++i) if (kPages[i].page == c->start) start = static_cast<int>(i);
        prefs_show(dlg, *c, start);
        SetFocus(tree);
        return FALSE;
    }
    case WM_NOTIFY: {
        auto* nm = reinterpret_cast<NMHDR*>(lp);
        if (c && nm->idFrom == IDC_PREFTREE && nm->code == TVN_SELCHANGEDW) {
            auto* tv = reinterpret_cast<NMTREEVIEWW*>(lp);
            prefs_show(dlg, *c, static_cast<int>(tv->itemNew.lParam));
            return TRUE;
        }
        break;
    }
    case WM_COMMAND:
        if (!c) break;
        switch (LOWORD(wp)) {
        case IDOK: if (prefs_apply(dlg, *c)) EndDialog(dlg, IDOK); return TRUE;
        case IDC_APPLYNOW: prefs_apply(dlg, *c); return TRUE;
        case IDCANCEL: EndDialog(dlg, IDCANCEL); return TRUE;
        }
        break;
    case WM_DESTROY:
        if (c) {
            TreeView_SetImageList(GetDlgItem(dlg, IDC_PREFTREE), nullptr, TVSIL_NORMAL);
            if (c->il) ImageList_Destroy(c->il);
            if (c->title_font) DeleteObject(c->title_font);
        }
        break;
    }
    return FALSE;
}
bool dlg_preferences(HWND owner, int page) {
    PrefsCtx ctx;
    ctx.start = page;
    DialogBoxParamW(g.inst, MAKEINTRESOURCEW(IDD_PREFS), owner, prefs_proc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.applied;
}

// ------------------------------------------------------------------ create torrent
static bool pick_source_file(HWND owner, std::wstring& path) {
    IFileOpenDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return false;
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
    dlg->SetTitle(L"File da condividere");
    bool ok = false;
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) { path = p; CoTaskMemFree(p); ok = true; }
            item->Release();
        }
    }
    dlg->Release();
    return ok;
}
static bool pick_torrent_destination(HWND owner, std::wstring const& folder, std::wstring const& name, std::wstring& path) {
    IFileSaveDialog* dlg = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg)))) return false;
    COMDLG_FILTERSPEC filter[] = {{L"File torrent (*.torrent)", L"*.torrent"}};
    dlg->SetFileTypes(1, filter);
    dlg->SetDefaultExtension(L"torrent");
    dlg->SetTitle(L"Salva il file .torrent");
    dlg->SetFileName((name + L".torrent").c_str());
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT);
    IShellItem* start = nullptr;
    if (!folder.empty() && SUCCEEDED(SHCreateItemFromParsingName(folder.c_str(), nullptr, IID_PPV_ARGS(&start)))) {
        dlg->SetFolder(start);
        start->Release();
    }
    bool ok = false;
    if (SUCCEEDED(dlg->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) { path = p; CoTaskMemFree(p); ok = true; }
            item->Release();
        }
    }
    dlg->Release();
    return ok;
}

struct CreateCtx {
    TorrentCreator creator;
    std::wstring output, save_dir;
    bool seed = false, copy_magnet = false, closing = false;
    std::wstring status;
    COLORREF status_color = paint::kText;
};
static void create_set_busy(HWND dlg, bool busy) {
    for (int id : {IDC_C_SOURCE, IDC_C_FILE, IDC_C_FOLDER, IDC_C_TRACKERS, IDC_C_WEBSEEDS, IDC_C_COMMENT, IDC_C_PRIVATE,
                   IDC_C_FORMAT, IDC_C_PIECE, IDC_C_SEED, IDC_C_MAGNET, IDOK})
        EnableWindow(GetDlgItem(dlg, id), !busy);
    set_text(dlg, IDCANCEL, busy ? L"Annulla" : L"Chiudi");
    DragAcceptFiles(dlg, !busy);
}
static void create_status(HWND dlg, CreateCtx& c, std::wstring const& text, COLORREF color = paint::kText) {
    c.status = text;
    c.status_color = color;
    set_text(dlg, IDC_C_STATUS, text);
    InvalidateRect(GetDlgItem(dlg, IDC_C_STATUS), nullptr, TRUE);
}
static void create_store_choices(HWND dlg) {
    auto& s = g.settings;
    s.create_format = std::clamp(combo_get(dlg, IDC_C_FORMAT), 0, 2);
    s.create_piece = std::clamp(combo_get(dlg, IDC_C_PIECE), 0, 11);
    std::wstring trackers = get_text(dlg, IDC_C_TRACKERS);
    trackers.erase(std::remove(trackers.begin(), trackers.end(), L'\r'), trackers.end());
    s.create_trackers = narrow(trackers);
    s.create_private = checked(dlg, IDC_C_PRIVATE);
    s.create_seed = checked(dlg, IDC_C_SEED);
    s.create_copy_magnet = checked(dlg, IDC_C_MAGNET);
}
static void create_begin(HWND dlg, CreateCtx& c) {
    std::wstring source = get_text(dlg, IDC_C_SOURCE);
    while (source.size() > 3 && (source.back() == L'\\' || source.back() == L'/')) source.pop_back();
    std::error_code ec;
    fs::path src(source);
    if (source.empty() || !src.is_absolute() || !fs::exists(src, ec)) {
        MessageBoxW(dlg, L"Scegli un file o una cartella esistente da condividere.", L"Crea torrent", MB_ICONWARNING);
        return;
    }
    if (src.parent_path() == src) {
        MessageBoxW(dlg, L"Non si può creare un torrent di un intero disco: scegli una cartella.", L"Crea torrent", MB_ICONWARNING);
        return;
    }
    std::string error;
    auto tiers = parse_tracker_tiers(narrow(get_text(dlg, IDC_C_TRACKERS)), error);
    if (!error.empty()) { MessageBoxW(dlg, widen(error).c_str(), L"Crea torrent", MB_ICONWARNING); return; }
    auto seeds = parse_web_seeds(narrow(get_text(dlg, IDC_C_WEBSEEDS)), error);
    if (!error.empty()) { MessageBoxW(dlg, widen(error).c_str(), L"Crea torrent", MB_ICONWARNING); return; }
    bool const priv = checked(dlg, IDC_C_PRIVATE);
    if (priv && tiers.empty() &&
        MessageBoxW(dlg, L"Un torrent privato senza tracker non può trovare peer (DHT, PeX e LSD sono disattivati). Continuare?",
                    L"Crea torrent", MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) != IDYES) return;
    std::wstring output;
    if (!pick_torrent_destination(dlg, src.parent_path().wstring(), src.filename().wstring(), output)) return;
    create_store_choices(dlg);
    persist();

    CreateRequest req;
    req.source = narrow(src.wstring());
    req.tiers = std::move(tiers);
    req.web_seeds = std::move(seeds);
    req.comment = narrow(get_text(dlg, IDC_C_COMMENT));
    req.creator = SF_CLIENT_NAME;
    req.is_private = priv;
    req.format = static_cast<TorrentFormat>(g.settings.create_format);
    auto const sizes = piece_size_choices();
    req.piece_size = g.settings.create_piece > 0 && g.settings.create_piece <= static_cast<int>(sizes.size())
        ? sizes[static_cast<std::size_t>(g.settings.create_piece - 1)] : 0;
    c.output = output;
    c.save_dir = src.parent_path().wstring();
    c.seed = g.settings.create_seed;
    c.copy_magnet = g.settings.create_copy_magnet;
    if (!c.creator.start(std::move(req))) { MessageBoxW(dlg, L"Impossibile avviare la creazione.", L"Crea torrent", MB_ICONERROR); return; }
    create_set_busy(dlg, true);
    create_status(dlg, c, L"Elenco dei file e calcolo degli hash…");
    SetTimer(dlg, 1, 100, nullptr);
}
static void create_finish(HWND dlg, CreateCtx& c) {
    KillTimer(dlg, 1);
    CreateResult r = c.creator.take_result();
    create_set_busy(dlg, false);
    InvalidateRect(GetDlgItem(dlg, IDC_C_PROGRESS), nullptr, FALSE);
    if (c.closing) { EndDialog(dlg, IDCANCEL); return; }
    if (r.cancelled) { create_status(dlg, c, L"Creazione annullata.", paint::kGray); return; }
    if (!r.ok) { create_status(dlg, c, L"Errore: " + widen(r.error), paint::kRed); return; }
    std::string error;
    if (!file_io::write_atomic(fs::path(c.output), r.torrent, error)) {
        create_status(dlg, c, L"Salvataggio non riuscito: " + widen(error), paint::kRed);
        return;
    }
    std::wstring summary = fs::path(c.output).filename().wstring() + L": " + std::to_wstring(r.num_files) +
        (r.num_files == 1 ? L" file, " : L" file, ") + fmt_bytes(r.total_size) + L", " + std::to_wstring(r.num_pieces) +
        L" pezzi da " + fmt_bytes(r.piece_size);
    log_line(L"Torrent creato: " + summary);
    std::wstring extra;
    if (c.copy_magnet && copy_to_clipboard(g.main, widen(r.magnet))) extra += L"\nMagnet copiato negli appunti.";
    if (c.seed) {
        int id = add_created_torrent(c.output, r.seed_dir.empty() ? c.save_dir : widen(r.seed_dir), error);
        extra += id > 0 ? L"\nSeeding avviato." : L"\nSeeding non avviato: " + widen(error);
    }
    MessageBoxW(dlg, (L"Torrent creato.\n\n" + summary + extra).c_str(), L"Crea torrent", MB_ICONINFORMATION);
    EndDialog(dlg, IDOK);
}
static INT_PTR CALLBACK create_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    auto* c = reinterpret_cast<CreateCtx*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(dlg, DWLP_USER, lp);
        auto const& s = g.settings;
        combo_add(dlg, IDC_C_FORMAT, L"Ibrido v1 + v2 (consigliato)");
        combo_add(dlg, IDC_C_FORMAT, L"v1 (massima compatibilità)");
        combo_add(dlg, IDC_C_FORMAT, L"v2 (solo client recenti)");
        combo_sel(dlg, IDC_C_FORMAT, std::clamp(s.create_format, 0, 2));
        combo_add(dlg, IDC_C_PIECE, L"Automatica (consigliata)");
        for (int size : piece_size_choices()) combo_add(dlg, IDC_C_PIECE, fmt_bytes(size).c_str());
        combo_sel(dlg, IDC_C_PIECE, std::clamp(s.create_piece, 0, 11));
        set_text(dlg, IDC_C_TRACKERS, crlf(widen(s.create_trackers)));
        check(dlg, IDC_C_PRIVATE, s.create_private);
        check(dlg, IDC_C_SEED, s.create_seed);
        check(dlg, IDC_C_MAGNET, s.create_copy_magnet);
        set_text(dlg, IDC_C_INFO, L"Il seeding parte dalla cartella in cui si trovano i dati, senza ricontrollarli.");
        DragAcceptFiles(dlg, TRUE);
        return TRUE;
    }
    case WM_DROPFILES: {
        auto drop = reinterpret_cast<HDROP>(wp);
        wchar_t path[MAX_PATH * 4];
        if (DragQueryFileW(drop, 0, path, static_cast<UINT>(std::size(path)))) set_text(dlg, IDC_C_SOURCE, path);
        DragFinish(drop);
        return TRUE;
    }
    case WM_CTLCOLORSTATIC:
        if (c && reinterpret_cast<HWND>(lp) == GetDlgItem(dlg, IDC_C_STATUS)) {
            SetTextColor(reinterpret_cast<HDC>(wp), c->status_color);
            SetBkMode(reinterpret_cast<HDC>(wp), TRANSPARENT);
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_BTNFACE));
        }
        break;
    case WM_DRAWITEM: {
        auto* di = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if (!c || di->CtlID != IDC_C_PROGRESS) break;
        FillRect(di->hDC, &di->rcItem, GetSysColorBrush(COLOR_BTNFACE));
        int total = c->creator.pieces_total(), done = std::min(c->creator.pieces_done(), total);
        bool busy = c->creator.running();
        double frac = total > 0 ? static_cast<double>(done) / total : 0.0;
        std::wstring text = busy ? (total > 0 ? L"Hash " + fmt_double(frac * 100.0, 1) + L"%  (" + std::to_wstring(done) + L" / " +
                                    std::to_wstring(total) + L" pezzi)" : L"Preparazione…") : L"";
        paint::progress_bar(di->hDC, di->rcItem, busy ? frac : 0.0, paint::kGreen,   // same green as the Crea icon
                            busy ? paint::Bar::Striped : paint::Bar::Solid,
                            g.anim_phase, text, g.font);
        return TRUE;
    }
    case WM_TIMER:
        if (!c) break;
        ++g.anim_phase;
        InvalidateRect(GetDlgItem(dlg, IDC_C_PROGRESS), nullptr, FALSE);
        if (c->creator.finished() && !c->creator.running()) create_finish(dlg, *c);
        return TRUE;
    case WM_COMMAND:
        if (!c) break;
        switch (LOWORD(wp)) {
        case IDC_C_FILE: { std::wstring p = get_text(dlg, IDC_C_SOURCE); if (pick_source_file(dlg, p)) set_text(dlg, IDC_C_SOURCE, p); return TRUE; }
        case IDC_C_FOLDER: { std::wstring p = get_text(dlg, IDC_C_SOURCE); if (pick_folder(dlg, p)) set_text(dlg, IDC_C_SOURCE, p); return TRUE; }
        case IDOK: if (!c->creator.running()) create_begin(dlg, *c); return TRUE;
        case IDCANCEL:
            if (c->creator.running()) { c->creator.cancel(); create_status(dlg, *c, L"Annullamento…", paint::kGray); return TRUE; }
            create_store_choices(dlg);
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    case WM_CLOSE:
        if (c && c->creator.running()) { c->closing = true; c->creator.cancel(); return TRUE; }   // ends when the worker stops
        break;
    case WM_DESTROY:
        if (c) { KillTimer(dlg, 1); c->creator.cancel(); c->creator.wait(); }
        break;
    }
    return FALSE;
}
void dlg_create_torrent(HWND owner) {
    CreateCtx ctx;
    DialogBoxParamW(g.inst, MAKEINTRESOURCEW(IDD_CREATE), owner, create_proc, reinterpret_cast<LPARAM>(&ctx));
    persist();
}

// ------------------------------------------------------------------ wizard
static tuning::WizardResult wizard_result;
static bool wizard_compute(HWND dlg) {
    double down = 0, up = 0;
    if (!parse_double(get_text(dlg, IDC_DOWNMBPS), down) || down <= 0 || down > 100000 ||
        !parse_double(get_text(dlg, IDC_UPMBPS), up) || up <= 0 || up > 100000) {
        MessageBoxW(dlg, L"Inserisci velocità di download e upload in Mb/s (es. 1000 e 1000).", L"SeedFlow", MB_ICONWARNING);
        return false;
    }
    wizard_result = tuning::compute(down, up, std::max(0, combo_get(dlg, IDC_DISKTYPE)), std::max(0, combo_get(dlg, IDC_RAMPRIO)), checked(dlg, IDC_LIMITUP));
    HWND lv = GetDlgItem(dlg, IDC_RESULTS);
    ListView_DeleteAllItems(lv);
    int i = 0;
    for (auto const& e : wizard_result.explain) {
        LVITEMW it{};
        it.mask = LVIF_TEXT;
        it.iItem = i;
        it.pszText = const_cast<wchar_t*>(e.setting.c_str());
        ListView_InsertItem(lv, &it);
        ListView_SetItemText(lv, i, 1, const_cast<wchar_t*>(e.value.c_str()));
        ListView_SetItemText(lv, i, 2, const_cast<wchar_t*>(e.why.c_str()));
        ++i;
    }
    return true;
}
static INT_PTR CALLBACK wizard_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM) {
    auto const& presets = tuning::connections();
    switch (msg) {
    case WM_INITDIALOG: {
        int sel = 7; // FTTH 1 Gb symmetric
        for (std::size_t i = 0; i < presets.size(); ++i) {
            combo_add(dlg, IDC_CONNTYPE, presets[i].name);
            if (widen(g.settings.connection_profile) == presets[i].name) sel = static_cast<int>(i);
        }
        combo_sel(dlg, IDC_CONNTYPE, sel);
        set_text(dlg, IDC_DOWNMBPS, fmt_double(presets[static_cast<std::size_t>(sel)].down, 0));
        set_text(dlg, IDC_UPMBPS, fmt_double(presets[static_cast<std::size_t>(sel)].up, 0));
        for (auto const* d : tuning::kDiskTypes) combo_add(dlg, IDC_DISKTYPE, d);
        combo_sel(dlg, IDC_DISKTYPE, 2);
        combo_add(dlg, IDC_RAMPRIO, L"Risparmio RAM");
        combo_add(dlg, IDC_RAMPRIO, L"Bilanciata");
        combo_add(dlg, IDC_RAMPRIO, L"Massime prestazioni");
        combo_sel(dlg, IDC_RAMPRIO, 1);
        check(dlg, IDC_LIMITUP, true);
        HWND lv = GetDlgItem(dlg, IDC_RESULTS);
        ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT | LVS_EX_LABELTIP | LVS_EX_DOUBLEBUFFER);
        LVCOLUMNW c{};
        c.mask = LVCF_TEXT | LVCF_WIDTH;
        c.pszText = const_cast<wchar_t*>(L"Impostazione"); c.cx = scale(140); ListView_InsertColumn(lv, 0, &c);
        c.pszText = const_cast<wchar_t*>(L"Valore"); c.cx = scale(170); ListView_InsertColumn(lv, 1, &c);
        c.pszText = const_cast<wchar_t*>(L"Perché"); c.cx = scale(420); ListView_InsertColumn(lv, 2, &c);
        wizard_compute(dlg);
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_CONNTYPE:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                int sel = combo_get(dlg, IDC_CONNTYPE);
                if (sel >= 0 && sel < static_cast<int>(presets.size()) - 1) {
                    set_text(dlg, IDC_DOWNMBPS, fmt_double(presets[static_cast<std::size_t>(sel)].down, presets[static_cast<std::size_t>(sel)].down < 1 ? 1 : 0));
                    set_text(dlg, IDC_UPMBPS, fmt_double(presets[static_cast<std::size_t>(sel)].up, presets[static_cast<std::size_t>(sel)].up < 1 ? 1 : 0));
                }
                wizard_compute(dlg);
            }
            return TRUE;
        case IDC_DISKTYPE: case IDC_RAMPRIO: if (HIWORD(wp) == CBN_SELCHANGE) wizard_compute(dlg); return TRUE;
        case IDC_LIMITUP: wizard_compute(dlg); return TRUE;
        case IDC_CALC: wizard_compute(dlg); return TRUE;
        case IDOK: {
            if (!wizard_compute(dlg)) return TRUE;
            auto& s = g.settings;
            auto const& r = wizard_result;
            s.down_kib = r.down_kib; s.up_kib = r.up_kib;
            s.connections = r.connections; s.upload_slots = r.upload_slots;
            s.active_downloads = r.active_downloads; s.active_seeds = r.active_seeds;
            for (auto const& [k, v] : r.ints) s.int_settings[k] = v;
            int sel = combo_get(dlg, IDC_CONNTYPE);
            s.connection_profile = sel >= 0 ? narrow(presets[static_cast<std::size_t>(sel)].name) : "";
            int ram = std::max(0, combo_get(dlg, IDC_RAMPRIO));
            s.perf_profile = narrow(tuning::kProfileNames[ram]) + " (wizard)";
            log_line(L"Ottimizzazione applicata: " + widen(s.connection_profile));
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        case IDCANCEL: EndDialog(dlg, IDCANCEL); return TRUE;
        }
        break;
    }
    return FALSE;
}
bool dlg_wizard(HWND owner) { return DialogBoxParamW(g.inst, MAKEINTRESOURCEW(IDD_WIZARD), owner, wizard_proc, 0) == IDOK; }

// ------------------------------------------------------------------ diagnostics
static int firewall_state = -2; // -2 not checked
static std::wstring diagnostics_report() {
    auto n = g.engine.network();
    std::wstring text, tip;
    NetState st = evaluate_network(n, g.port_check, text, tip);
    std::wstring r;
    auto line = [&](std::wstring const& s) { r += s + L"\r\n"; };
    auto yn = [](bool b) { return b ? std::wstring(L"sì") : std::wstring(L"no"); };
    line(L"STATO: " + text);
    line(L"  " + tip);
    line(L"");
    line(L"PORTA DI ASCOLTO " + std::to_wstring(n.port) + L" — in ascolto: " + yn(n.listening));
    if (n.listen.empty()) line(n.listening ? L"  in ascolto (dettaglio dei socket non disponibile)" : L"  nessun socket in ascolto");
    for (auto const& l : n.listen)
        line(L"  " + std::wstring(l.ok ? L"[OK]   " : L"[ERR]  ") + widen(l.address) + L":" + std::to_wstring(l.port) + L"  " + widen(l.type) + (l.ok ? L"" : L"  — " + widen(l.error)));
    line(L"");
    line(L"MAPPATURA PORTA SUL ROUTER  (UPnP: " + yn(n.upnp) + L", NAT-PMP: " + yn(n.natpmp) + L")");
    if (n.portmaps.empty()) line(n.upnp || n.natpmp ? L"  nessuna risposta dal router (UPnP/NAT-PMP disattivato sul router o non supportato)" : L"  disattivata");
    for (auto const& m : n.portmaps)
        line(L"  " + std::wstring(m.ok ? L"[OK]   " : L"[ERR]  ") + widen(m.transport) + L" " + widen(m.protocol) +
             (m.ok ? L"  porta esterna " + std::to_wstring(m.external_port) : L"  — " + widen(m.error)) + (m.local.empty() ? L"" : L"  (" + widen(m.local) + L")"));
    for (auto const& m : n.portmaps)
        if (m.ok && m.external_port > 0 && m.external_port != n.port)
            line(L"  ATTENZIONE: il router ha assegnato la porta esterna " + std::to_wstring(m.external_port) + L" " + widen(m.protocol) +
                 L" invece della " + std::to_wstring(n.port) + L": la " + std::to_wstring(n.port) + L" " + widen(m.protocol) +
                 L" \u00E8 gi\u00E0 occupata sul router (altro dispositivo, inoltro manuale o vecchia mappatura UPnP).");
    line(L"");
    line(L"CONNESSIONI");
    line(L"  Connessioni in ingresso da Internet: " + yn(n.has_incoming) + L"  (totale accettate: " + fmt_int(n.incoming_connections) + L")");
    line(L"  Peer connessi: " + fmt_int(n.peers_connected));
    line(L"  DHT: " + (n.dht ? fmt_int(n.dht_nodes) + L" nodi" : std::wstring(L"disattivato")) + L"   PeX: " + yn(n.pex) + L"   LSD: " + yn(n.lsd));
    line(L"  Tempo dall'avvio: " + fmt_duration(n.uptime));
    line(L"");
    bool const current_check = g.port_check.done && g.port_check.port == n.port;
    line(L"INDIRIZZI");
    line(L"  IP esterno visto dai peer: " + (n.external_ip.empty() ? std::wstring(L"non ancora noto") : widen(n.external_ip)));
    if (current_check && g.port_check.success) line(L"  IP esterno (verifica online): " + widen(g.port_check.ip));
    std::string ext = current_check && g.port_check.success && !g.port_check.ip.empty() ? g.port_check.ip : n.external_ip;
    if (is_cgnat(ext)) line(L"  ATTENZIONE: indirizzo CGNAT (100.64.0.0/10). Il provider condivide l'IP: la porta non si può aprire senza IP pubblico.");
    else if (is_private_ipv4(ext)) line(L"  ATTENZIONE: l'IP esterno è privato: probabile doppio NAT.");
    for (auto const& a : local_adapters()) line(L"  " + a);
    line(L"");
    line(L"VERIFICA ONLINE (ifconfig.co)");
    if (g.port_check_running) line(L"  in corso…");
    else if (!current_check) line(L"  non eseguita: premi «Verifica porta online».");
    else if (!g.port_check.success) line(L"  non riuscita: " + widen(g.port_check.status));
    else line(L"  porta " + std::to_wstring(n.port) + L" TCP: " + (g.port_check.reachable ? L"RAGGIUNGIBILE" : L"NON raggiungibile") +
              L" (" + fmt_date(g.port_check.when) + L")" + (g.port_check.status.empty() ? L"" : L" — " + widen(g.port_check.status)));
    line(L"");
    line(L"FIREWALL DI WINDOWS");
    line(firewall_state == 1 ? L"  regola in ingresso «" + std::wstring(kFirewallRule) + L"» presente"
        : firewall_state == 0 ? L"  regola specifica assente (Windows potrebbe aver creato la sua al primo avvio: se la porta risulta chiusa, aggiungila)"
        : L"  stato non verificabile");
    line(L"");
    line(L"SUGGERIMENTI");
    if (st == NetState::NotListening) line(L"  • Cambia porta in Preferenze > Connessione: quella attuale è occupata.");
    if (st == NetState::Firewalled || (current_check && g.port_check.success && !g.port_check.reachable)) {
        line(L"  • Abilita UPnP sul router oppure inoltra manualmente la porta " + std::to_wstring(n.port) + L" TCP+UDP verso questo PC.");
        line(L"  • Verifica che il firewall di Windows (o un antivirus) consenta SeedFlow in ingresso.");
        line(L"  • Con CGNAT chiedi al provider un IP pubblico (spesso gratuito per le FTTH).");
    }
    bool port_mismatch = false;
    for (auto const& m : n.portmaps) if (m.ok && m.external_port > 0 && m.external_port != n.port) port_mismatch = true;
    if (port_mismatch)
        line(L"  • Porta esterna diversa: nella pagina UPnP/port forwarding del router elimina le vecchie regole sulla porta " + std::to_wstring(n.port) +
             L" (o riavvia il router), oppure scegli un'altra porta in Preferenze > Connessione. Senza la stessa porta UDP, uTP e DHT in ingresso funzionano peggio.");
    if (st == NetState::Open && !port_mismatch) line(L"  • Nessun problema rilevato.");
    return r;
}
static INT_PTR CALLBACK diag_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM) {
    switch (msg) {
    case WM_INITDIALOG:
        set_mono(dlg, IDC_REPORT);
        SendDlgItemMessageW(dlg, IDC_STATUS, WM_SETFONT, reinterpret_cast<WPARAM>(g.bold), TRUE);
        if (firewall_state == -2) firewall_state = firewall_rule_exists();
        set_text(dlg, IDC_REPORT, diagnostics_report());
        set_text(dlg, IDC_STATUS, g.net_text + L" — " + g.net_tip);
        SetTimer(dlg, 1, 3000, nullptr);
        return TRUE;
    case WM_TIMER:
    case WM_APP_PORTCHECK + 100: {
        int pos = static_cast<int>(SendDlgItemMessageW(dlg, IDC_REPORT, EM_GETFIRSTVISIBLELINE, 0, 0));
        set_text(dlg, IDC_REPORT, diagnostics_report());
        SendDlgItemMessageW(dlg, IDC_REPORT, EM_LINESCROLL, 0, pos);
        set_text(dlg, IDC_STATUS, g.net_text + L" — " + g.net_tip);
        EnableWindow(GetDlgItem(dlg, IDC_CHECKPORT), !g.port_check_running);
        return TRUE;
    }
    case WM_SIZE: {
        RECT rc; GetClientRect(dlg, &rc);
        int m = scale(12), bh = scale(26);
        SetWindowPos(GetDlgItem(dlg, IDC_STATUS), nullptr, m, m, rc.right - 2 * m, scale(30), SWP_NOZORDER);
        SetWindowPos(GetDlgItem(dlg, IDC_REPORT), nullptr, m, m + scale(34), rc.right - 2 * m, rc.bottom - 3 * m - bh - scale(34), SWP_NOZORDER);
        int y = rc.bottom - m - bh;
        int ids[] = {IDC_CHECKPORT, IDC_REFRESH, IDC_FIREWALL, IDC_COPY};
        int x = m;
        for (int id : ids) {
            RECT br; GetWindowRect(GetDlgItem(dlg, id), &br);
            int w = br.right - br.left;
            SetWindowPos(GetDlgItem(dlg, id), nullptr, x, y, w, bh, SWP_NOZORDER);
            x += w + scale(6);
        }
        RECT cr; GetWindowRect(GetDlgItem(dlg, IDCANCEL), &cr);
        SetWindowPos(GetDlgItem(dlg, IDCANCEL), nullptr, rc.right - m - (cr.right - cr.left), y, cr.right - cr.left, bh, SWP_NOZORDER);
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_CHECKPORT:
            start_port_check(g.main);
            EnableWindow(GetDlgItem(dlg, IDC_CHECKPORT), FALSE);
            set_text(dlg, IDC_REPORT, diagnostics_report());
            return TRUE;
        case IDC_REFRESH: firewall_state = firewall_rule_exists(); SendMessageW(dlg, WM_TIMER, 1, 0); return TRUE;
        case IDC_FIREWALL: {
            std::wstring m;
            add_firewall_rule(m);
            firewall_state = firewall_rule_exists();
            MessageBoxW(dlg, m.c_str(), L"SeedFlow", MB_ICONINFORMATION);
            SendMessageW(dlg, WM_TIMER, 1, 0);
            return TRUE;
        }
        case IDC_COPY: copy_to_clipboard(dlg, get_text(dlg, IDC_REPORT)); return TRUE;
        case IDCANCEL: case IDOK: KillTimer(dlg, 1); EndDialog(dlg, IDCANCEL); return TRUE;
        }
        break;
    }
    return FALSE;
}
void dlg_diagnostics(HWND owner) { DialogBoxParamW(g.inst, MAKEINTRESOURCEW(IDD_DIAG), owner, diag_proc, 0); }

// ------------------------------------------------------------------ statistics
static std::wstring stats_report() {
    auto c = g.engine.counters();
    auto const& st = g.settings.stats;
    auto m = memory_usage();
    std::int64_t down = 0, up = 0;
    int dl = 0, seeding = 0, paused = 0;
    for (auto const& r : g.rows) {
        down += r.v.download_rate; up += r.v.upload_rate;
        if (r.v.paused && !r.v.auto_managed) ++paused; else if (r.v.seeding) ++seeding; else ++dl;
    }
    std::wstring r;
    auto line = [&](std::wstring const& k, std::wstring const& v) {
        std::wstring key = k;
        if (key.size() < 34) key.append(34 - key.size(), L' ');
        r += L"  " + key + v + L"\r\n";
    };
    auto title = [&](wchar_t const* t) { r += std::wstring(t) + L"\r\n"; };
    title(L"SESSIONE CORRENTE");
    line(L"Durata", fmt_duration(c.uptime));
    line(L"Scaricati (dati utili)", fmt_bytes(c.recv_payload));
    line(L"Caricati (dati utili)", fmt_bytes(c.sent_payload));
    line(L"Ratio di sessione", fmt_ratio(effective_ratio(c.recv_payload, c.sent_payload, 0), c.recv_payload || c.sent_payload));
    line(L"Traffico totale ↓ / ↑", fmt_bytes(c.recv_bytes + c.recv_ip_overhead) + L" / " + fmt_bytes(c.sent_bytes + c.sent_ip_overhead));
    line(L"Overhead protocollo+IP ↓ / ↑", fmt_bytes(std::max<std::int64_t>(0, c.recv_bytes - c.recv_payload) + c.recv_ip_overhead) + L" / " +
         fmt_bytes(std::max<std::int64_t>(0, c.sent_bytes - c.sent_payload) + c.sent_ip_overhead));
    line(L"Tracker ↓ / ↑", fmt_bytes(c.recv_tracker) + L" / " + fmt_bytes(c.sent_tracker));
    line(L"DHT ↓ / ↑", fmt_bytes(c.dht_bytes_in) + L" / " + fmt_bytes(c.dht_bytes_out));
    line(L"Dati scartati (hash errati/doppi)", fmt_bytes(c.recv_failed + c.recv_redundant));
    line(L"Pezzi verificati / falliti", fmt_int(c.piece_passed) + L" / " + fmt_int(c.piece_failed));
    line(L"Velocità attuale ↓ / ↑", fmt_speed0(down) + L" / " + fmt_speed0(up));
    line(L"Peer connessi (TCP / uTP)", fmt_int(c.peers_connected) + L" (" + fmt_int(c.tcp_peers) + L" / " + fmt_int(c.utp_peers) + L")");
    line(L"Connessioni in ingresso", fmt_int(c.incoming_connections));
    line(L"Peer bannati", fmt_int(c.banned_peers));
    line(L"Torrent (download/seed/pausa)", std::to_wstring(dl) + L" / " + std::to_wstring(seeding) + L" / " + std::to_wstring(paused));
    r += L"\r\n";
    title((L"STORICO (dal " + fmt_date(static_cast<std::time_t>(st.since)) + L", inclusi i torrent rimossi)").c_str());
    line(L"Scaricati", fmt_bytes(st.downloaded));
    line(L"Caricati", fmt_bytes(st.uploaded));
    line(L"Ratio globale", fmt_ratio(effective_ratio(st.downloaded, st.uploaded, 0), st.downloaded || st.uploaded));
    line(L"Tempo di esecuzione", fmt_duration(st.runtime));
    line(L"Avvii", fmt_int(st.launches));
    line(L"Torrent aggiunti / completati", fmt_int(st.added) + L" / " + fmt_int(st.completed));
    line(L"Picco ↓ / ↑", fmt_speed0(st.peak_down) + L" / " + fmt_speed0(st.peak_up));
    r += L"\r\n";
    title(L"MEMORIA");
    line(L"Working set (Task Manager)", fmt_bytes(static_cast<std::int64_t>(m.working_set)));
    line(L"Memoria privata", fmt_bytes(static_cast<std::int64_t>(m.private_bytes)));
    line(L"Picco working set", fmt_bytes(static_cast<std::int64_t>(m.peak_working_set)));
    line(L"Buffer disco libtorrent in uso", fmt_bytes(c.disk_blocks_in_use * 16384));
    line(L"Blocchi in cache di scrittura", fmt_bytes(c.cached_blocks * 16384));
    line(L"Operazioni disco in coda", fmt_int(c.queued_disk_jobs));
    line(L"Motore I/O disco", widen(g.engine.disk_io_name()));
    line(L"Profilo prestazioni", g.settings.perf_profile.empty() ? L"predefinito" : widen(g.settings.perf_profile));
    return r;
}
static INT_PTR CALLBACK stats_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM) {
    switch (msg) {
    case WM_INITDIALOG:
        set_mono(dlg, IDC_REPORT);
        set_text(dlg, IDC_REPORT, stats_report());
        SetTimer(dlg, 1, 2000, nullptr);
        return TRUE;
    case WM_TIMER: {
        int pos = static_cast<int>(SendDlgItemMessageW(dlg, IDC_REPORT, EM_GETFIRSTVISIBLELINE, 0, 0));
        set_text(dlg, IDC_REPORT, stats_report());
        SendDlgItemMessageW(dlg, IDC_REPORT, EM_LINESCROLL, 0, pos);
        return TRUE;
    }
    case WM_SIZE: {
        RECT rc; GetClientRect(dlg, &rc);
        int m = scale(12), bh = scale(26);
        SetWindowPos(GetDlgItem(dlg, IDC_REPORT), nullptr, m, m, rc.right - 2 * m, rc.bottom - 3 * m - bh, SWP_NOZORDER);
        int y = rc.bottom - m - bh;
        RECT b1; GetWindowRect(GetDlgItem(dlg, IDC_RESETSTATS), &b1);
        SetWindowPos(GetDlgItem(dlg, IDC_RESETSTATS), nullptr, m, y, b1.right - b1.left, bh, SWP_NOZORDER);
        RECT b2; GetWindowRect(GetDlgItem(dlg, IDC_COPY), &b2);
        SetWindowPos(GetDlgItem(dlg, IDC_COPY), nullptr, m + (b1.right - b1.left) + scale(6), y, b2.right - b2.left, bh, SWP_NOZORDER);
        RECT b3; GetWindowRect(GetDlgItem(dlg, IDCANCEL), &b3);
        SetWindowPos(GetDlgItem(dlg, IDCANCEL), nullptr, rc.right - m - (b3.right - b3.left), y, b3.right - b3.left, bh, SWP_NOZORDER);
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_RESETSTATS:
            if (MessageBoxW(dlg, L"Azzerare le statistiche storiche (scaricati, caricati, tempi, contatori)?", L"SeedFlow", MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2) == IDYES) {
                g.settings.stats = GlobalStats();
                g.settings.stats.since = static_cast<std::int64_t>(std::time(nullptr));
                g.settings.stats.launches = 1;
                persist();
                SendMessageW(dlg, WM_TIMER, 1, 0);
            }
            return TRUE;
        case IDC_COPY: copy_to_clipboard(dlg, get_text(dlg, IDC_REPORT)); return TRUE;
        case IDCANCEL: case IDOK: KillTimer(dlg, 1); EndDialog(dlg, IDCANCEL); return TRUE;
        }
        break;
    }
    return FALSE;
}
void dlg_stats(HWND owner) { DialogBoxParamW(g.inst, MAKEINTRESOURCEW(IDD_STATS), owner, stats_proc, 0); }

// ------------------------------------------------------------------ about
static std::wstring windows_version() {
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOEXW*);
    OSVERSIONINFOEXW v{};
    v.dwOSVersionInfoSize = sizeof(v);
    if (auto fn = reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")))) fn(&v);
    std::wstring name = v.dwBuildNumber >= 22000 ? L"Windows 11" : L"Windows 10";
    return name + L" (build " + std::to_wstring(v.dwBuildNumber) + L")";
}
// Version line of the About box, always built from src/version.h.
std::wstring about_version_line() {
    return L"Versione " SF_VERSION_WSTRING L" (Win32 nativa) — x64";
}
static std::wstring about_text() {
    std::wstring t;
    auto line = [&](std::wstring const& s) { t += s + L"\r\n"; };
    line(L"Client BitTorrent nativo per Windows, leggero e senza pubblicità.");
    line(L"");
    line(L"Motore: libtorrent-rasterbar " + widen(Engine::libtorrent_version()) + L" (ABI " + std::to_wstring(g.engine.abi_version()) + L")");
    line(L"I/O disco: " + widen(g.engine.disk_io_name()));
#if defined(_MSC_VER)
    line(L"Compilato con MSVC " + std::to_wstring(_MSC_VER) + L", " + widen(__DATE__));
#elif defined(__clang__)
    line(L"Compilato con clang " + widen(__clang_version__) + L", " + widen(__DATE__));
#endif
    line(L"Sistema: " + windows_version());
    line(L"");
    line(L"Funzioni: torrent e magnet v1/v2, creazione di torrent v1/v2/hybrid, DHT, PeX, LSD, UPnP/NAT-PMP, cifratura, "
         L"code automatiche, Super-seeding, limiti di velocità e di ratio per torrent o globali, gestione tracker, "
         L"priorità dei file, fast resume, salute dello swarm, grafico delle velocità, diagnostica di rete con verifica "
         L"online della porta, ottimizzazione per tipo di connessione, statistiche storiche.");
    line(L"");
    line(L"Dati e impostazioni: " + app_data_dir().wstring());
    line(L"Cartella di download: " + widen(download_dir()));
    line(L"");
    line(L"Licenza: GNU GPL v3 (GPL-3.0-only).");
    line(L"Componenti di terze parti:");
    line(L"  • libtorrent-rasterbar — Arvid Norberg e collaboratori, licenza BSD 3-clause");
    line(L"  • Boost C++ Libraries — Boost Software License 1.0");
    line(L"  • OpenSSL (se incluso nella build) — Apache License 2.0");
    line(L"  • Icone: disegni SVG SeedFlow, sorgenti in res/icons");
    return t;
}
static INT_PTR CALLBACK about_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: {
        HICON big = static_cast<HICON>(LoadImageW(g.inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, scale(48), scale(48), 0));
        SendDlgItemMessageW(dlg, IDC_LOGO, STM_SETICON, reinterpret_cast<WPARAM>(big), 0);
        SetWindowLongPtrW(dlg, DWLP_USER, reinterpret_cast<LONG_PTR>(big));
        SendDlgItemMessageW(dlg, IDC_TITLE, WM_SETFONT, reinterpret_cast<WPARAM>(g.big), TRUE);
        set_text(dlg, IDC_VERSION, about_version_line());
        set_text(dlg, IDC_REPORT, about_text());
        return TRUE;
    }
    case WM_NOTIFY: {
        auto* nm = reinterpret_cast<NMHDR*>(lp);
        if (nm->idFrom == IDC_LINK && (nm->code == NM_CLICK || nm->code == NM_RETURN)) {
            open_path(reinterpret_cast<NMLINK*>(lp)->item.szUrl);
            return TRUE;
        }
        break;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_COPY: copy_to_clipboard(dlg, SF_DISPLAY_VERSION_W L"\r\n" + about_text()); return TRUE;
        case IDC_DATAFOLDER: open_path(app_data_dir().wstring()); return TRUE;
        case IDOK: case IDCANCEL: {
            if (auto big = reinterpret_cast<HICON>(GetWindowLongPtrW(dlg, DWLP_USER))) DestroyIcon(big);
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        }
        break;
    }
    return FALSE;
}
void dlg_about(HWND owner) { DialogBoxParamW(g.inst, MAKEINTRESOURCEW(IDD_ABOUT), owner, about_proc, 0); }

} // namespace sf


