// SeedFlow per-user installer and uninstaller. Built with the Windows SDK.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <filesystem>
#include <string>
#include <thread>
#include <stdexcept>
#include <algorithm>
#include <vector>
#include "version.h"

namespace fs = std::filesystem;
namespace {
constexpr wchar_t kInstallKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\SeedFlow";
constexpr UINT kComplete = WM_APP + 1;
constexpr UINT kProgress = WM_APP + 2;
HWND window, status_label, primary, torrent_box, magnet_box, desktop_box, defaults_box, progress;
HFONT normal_font, title_font;
HICON logo;
HINSTANCE instance;
std::jthread worker;
bool busy = false, finished = false;
fs::path destination;
std::wstring result_message;
#ifdef SF_INSTALLER_TEST
fs::path test_root;
std::vector<int> progress_events;
#endif

void report_progress(int percent, wchar_t const* stage) {
#ifdef SF_INSTALLER_TEST
    progress_events.push_back(percent);
#endif
    if (window) PostMessageW(window, kProgress, static_cast<WPARAM>(percent), reinterpret_cast<LPARAM>(stage));
}

std::wstring quote(std::wstring const& value) {
    // Paths created here contain no quotes and never end with a separator.
    return L"\"" + value + L"\"";
}
void require(bool success, wchar_t const* operation) {
    if (!success) throw std::runtime_error("Windows operation failed");
    (void)operation;
}
fs::path known_folder(REFKNOWNFOLDERID id) {
#ifdef SF_INSTALLER_TEST
    auto path = test_root / (IsEqualGUID(id, FOLDERID_Desktop) ? L"desktop" : L"menu");
    fs::create_directories(path);
    return path;
#else
    PWSTR text = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &text))) throw std::runtime_error("Known folder unavailable");
    fs::path path(text);
    CoTaskMemFree(text);
    return path;
#endif
}
fs::path own_path() {
    std::wstring buffer(32768, L'\0');
    auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!length || length >= buffer.size()) throw std::runtime_error("Executable path unavailable");
    buffer.resize(length);
    return fs::path(buffer);
}
void registry(std::wstring const& key, wchar_t const* name, std::wstring const& value) {
    HKEY handle = nullptr;
    LSTATUS error = RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &handle, nullptr);
    if (error == ERROR_SUCCESS) {
        error = RegSetValueExW(handle, name, 0, REG_SZ, reinterpret_cast<BYTE const*>(value.c_str()),
                              static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(handle);
    }
    if (error != ERROR_SUCCESS) throw std::runtime_error("Registry write failed");
}
std::wstring registry_text(wchar_t const* key, wchar_t const* name) {
    wchar_t buffer[32768]{};
    DWORD size = sizeof(buffer);
    if (RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS) return {};
    return buffer;
}
void delete_value(wchar_t const* key, wchar_t const* name) {
    HKEY handle = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, KEY_SET_VALUE, &handle) == ERROR_SUCCESS) {
        RegDeleteValueW(handle, name);
        RegCloseKey(handle);
    }
}
void extract(int id, fs::path const& path, int first, int last, wchar_t const* stage) {
    report_progress(first, stage);
    HRSRC resource = FindResourceW(instance, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!resource) throw std::runtime_error("Embedded payload missing");
    auto data = LockResource(LoadResource(instance, resource));
    DWORD const size = SizeofResource(instance, resource);
    if (!data || !size) throw std::runtime_error("Embedded payload empty");
    fs::path temporary = path.wstring() + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create payload");
    DWORD copied = 0;
    bool saved = true;
    while (copied < size) {
        DWORD written = 0;
        DWORD const chunk = std::min<DWORD>(size - copied, 256 * 1024);
        if (!WriteFile(file, static_cast<BYTE const*>(data) + copied, chunk, &written, nullptr) || written != chunk) {
            saved = false;
            break;
        }
        copied += written;
        int const percent = first + static_cast<int>(static_cast<ULONGLONG>(copied) * (last - first - 1) / size);
        report_progress(percent, stage);
    }
    saved = saved && FlushFileBuffers(file);
    CloseHandle(file);
    if (!saved || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        throw std::runtime_error("Cannot replace payload. Close SeedFlow and retry.");
    }
    report_progress(last, stage);
}
void shortcut(fs::path const& path) {
    IShellLinkW* link = nullptr;
    HRESULT result = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link));
    if (SUCCEEDED(result)) {
        result = link->SetPath((destination / L"SeedFlow.exe").c_str());
        if (SUCCEEDED(result)) result = link->SetWorkingDirectory(destination.c_str());
        IPersistFile* file = nullptr;
        if (SUCCEEDED(result)) result = link->QueryInterface(IID_PPV_ARGS(&file));
        if (SUCCEEDED(result)) { result = file->Save(path.c_str(), TRUE); file->Release(); }
        link->Release();
    }
    if (FAILED(result)) throw std::runtime_error("Shortcut creation failed");
}
void register_type(wchar_t const* name, wchar_t const* description, bool protocol) {
    std::wstring const key = std::wstring(L"Software\\Classes\\") + name;
    registry(key, nullptr, description);
    if (protocol) registry(key, L"URL Protocol", L"");
    registry(key + L"\\DefaultIcon", nullptr, quote((destination / L"SeedFlow.exe").wstring()) + L",0");
    registry(key + L"\\shell\\open\\command", nullptr, quote((destination / L"SeedFlow.exe").wstring()) + L" \"%1\"");
}
void install(bool torrent, bool magnet, bool desktop) {
    report_progress(0, L"Preparazione della cartella…");
    fs::create_directories(destination);
    extract(201, destination / L"SeedFlow.exe", 0, 65, L"Copia di SeedFlow…");
    extract(202, destination / L"Uninstall.exe", 65, 75, L"Copia del disinstallatore…");
    extract(203, destination / L"LICENSE", 75, 78, L"Copia della licenza…");
    extract(204, destination / L"ThirdPartyNotices.txt", 78, 82, L"Copia degli avvisi delle dipendenze…");
    report_progress(82, L"Registrazione in Windows…");
    if (torrent) {
        register_type(L"SeedFlow.Torrent", L"File BitTorrent", false);
        registry(L"Software\\Classes\\.torrent\\OpenWithProgids", L"SeedFlow.Torrent", L"");
        registry(L"Software\\SeedFlow\\Capabilities\\FileAssociations", L".torrent", L"SeedFlow.Torrent");
    }
    if (magnet) {
        register_type(L"SeedFlow.Magnet", L"URL:Magnet", true);
        registry(L"Software\\SeedFlow\\Capabilities\\URLAssociations", L"magnet", L"SeedFlow.Magnet");
    }
    registry(L"Software\\SeedFlow\\Capabilities", L"ApplicationName", L"SeedFlow");
    registry(L"Software\\SeedFlow\\Capabilities", L"ApplicationDescription", L"Client BitTorrent nativo per Windows");
    registry(L"Software\\RegisteredApplications", L"SeedFlow", L"Software\\SeedFlow\\Capabilities");
    registry(kInstallKey, L"DisplayName", L"SeedFlow");
    registry(kInstallKey, L"DisplayVersion", SF_VERSION_WSTRING);
    registry(kInstallKey, L"Publisher", L"SeedFlow contributors");
    registry(kInstallKey, L"DisplayIcon", (destination / L"SeedFlow.exe").wstring());
    registry(kInstallKey, L"InstallLocation", destination.wstring());
    registry(kInstallKey, L"UninstallString", quote((destination / L"Uninstall.exe").wstring()));
    registry(kInstallKey, L"URLInfoAbout", L"https://github.com/Kyo89-sudo/SeedFlow");
    report_progress(92, L"Creazione dei collegamenti…");
    fs::path const menu = known_folder(FOLDERID_Programs) / L"SeedFlow";
    fs::create_directories(menu);
    shortcut(menu / L"SeedFlow.lnk");
    if (desktop) shortcut(known_folder(FOLDERID_Desktop) / L"SeedFlow.lnk");
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    report_progress(100, L"Installazione completata.");
}
void uninstall() {
    report_progress(0, L"Controllo dell'installazione…");
    // Remove only our known files, never the download/state folders or recursively.
    if (registry_text(kInstallKey, L"InstallLocation") != destination.wstring())
        throw std::runtime_error("Installation location mismatch");
    for (auto name : {L"SeedFlow.exe", L"LICENSE", L"ThirdPartyNotices.txt", L"Uninstall.exe"}) {
        auto path = destination / name;
        if (!DeleteFileW(path.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND)
            throw std::runtime_error("Close SeedFlow before uninstalling");
    }
    report_progress(50, L"Rimozione della registrazione Windows…");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\SeedFlow.Torrent");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\SeedFlow.Magnet");
    delete_value(L"Software\\Classes\\.torrent\\OpenWithProgids", L"SeedFlow.Torrent");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\SeedFlow\\Capabilities");
    delete_value(L"Software\\RegisteredApplications", L"SeedFlow");
    RegDeleteTreeW(HKEY_CURRENT_USER, kInstallKey);
    report_progress(85, L"Rimozione dei collegamenti…");
    auto menu = known_folder(FOLDERID_Programs) / L"SeedFlow";
    DeleteFileW((menu / L"SeedFlow.lnk").c_str());
    RemoveDirectoryW(menu.c_str());
    DeleteFileW((known_folder(FOLDERID_Desktop) / L"SeedFlow.lnk").c_str());
    RemoveDirectoryW(destination.c_str());
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    report_progress(100, L"Disinstallazione completata.");
}
HWND control(wchar_t const* type, wchar_t const* text, DWORD style, int x, int y, int w, int h, int id) {
    HWND item = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, window,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    SendMessageW(item, WM_SETFONT, reinterpret_cast<WPARAM>(normal_font), TRUE);
    return item;
}
LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
    case WM_CREATE: {
        window = hwnd;
        normal_font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        title_font = CreateFontW(-24, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        control(L"STATIC", L"Cartella di installazione", 0, 28, 124, 590, 24, 0);
        control(L"STATIC", destination.c_str(), SS_PATHELLIPSIS, 28, 152, 590, 28, 0);
#ifdef SF_UNINSTALLER
        control(L"STATIC", L"Rimuovi SeedFlow e la sua integrazione con Windows.", 0, 28, 204, 590, 30, 0);
        control(L"STATIC", L"I download e le impostazioni personali vengono conservati.", 0, 28, 242, 590, 30, 0);
#else
        torrent_box = control(L"BUTTON", L"Registra SeedFlow per i file .torrent", BS_AUTOCHECKBOX | WS_TABSTOP, 28, 204, 590, 26, 11);
        magnet_box = control(L"BUTTON", L"Registra SeedFlow per i link magnet", BS_AUTOCHECKBOX | WS_TABSTOP, 28, 242, 590, 26, 12);
        desktop_box = control(L"BUTTON", L"Crea un collegamento sul desktop", BS_AUTOCHECKBOX | WS_TABSTOP, 28, 280, 590, 26, 13);
        defaults_box = control(L"BUTTON", L"Apri la scelta delle app predefinite al termine", BS_AUTOCHECKBOX | WS_TABSTOP, 28, 318, 590, 26, 14);
        SendMessageW(torrent_box, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(magnet_box, BM_SETCHECK, BST_CHECKED, 0);
        SendMessageW(defaults_box, BM_SETCHECK, BST_CHECKED, 0);
#endif
        status_label = control(L"STATIC", L"Pronto", 0, 28, 366, 590, 44, 0);
        progress = control(PROGRESS_CLASSW, L"", 0, 28, 425, 400, 18, 0);
        SendMessageW(progress, PBM_SETRANGE32, 0, 100);
#ifdef SF_UNINSTALLER
        primary = control(L"BUTTON", L"Disinstalla", BS_DEFPUSHBUTTON | WS_TABSTOP, 462, 416, 156, 36, 1);
#else
        primary = control(L"BUTTON", L"Installa SeedFlow", BS_DEFPUSHBUTTON | WS_TABSTOP, 462, 416, 156, 36, 1);
#endif
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        SetBkColor(reinterpret_cast<HDC>(wp), RGB(255,255,255));
        return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(hwnd, &paint);
        RECT rect{28,102,618,103};
        HBRUSH brush = CreateSolidBrush(RGB(222,226,229));
        FillRect(dc, &rect, brush);
        DeleteObject(brush);
        DrawIconEx(dc, 28, 28, logo, 40, 40, 0, nullptr, DI_NORMAL);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(35,42,47));
        auto previous = SelectObject(dc, title_font);
        TextOutW(dc, 84, 22, L"SeedFlow", 8);
        SelectObject(dc, normal_font);
        SetTextColor(dc, RGB(95,103,110));
#ifdef SF_UNINSTALLER
        std::wstring const caption = L"Disinstallazione  ·  " SF_VERSION_WSTRING;
#else
        std::wstring const caption = L"Installazione  ·  " SF_VERSION_WSTRING;
#endif
        TextOutW(dc, 86, 55, caption.c_str(), static_cast<int>(caption.size()));
        SelectObject(dc, previous);
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == 1) {
            if (finished) { DestroyWindow(hwnd); return 0; }
            bool const torrent = SendMessageW(torrent_box, BM_GETCHECK, 0, 0) == BST_CHECKED;
            bool const magnet = SendMessageW(magnet_box, BM_GETCHECK, 0, 0) == BST_CHECKED;
            bool const desktop = SendMessageW(desktop_box, BM_GETCHECK, 0, 0) == BST_CHECKED;
            busy = true;
            for (HWND item : {primary, torrent_box, magnet_box, desktop_box, defaults_box}) if (item) EnableWindow(item, FALSE);
            SetWindowTextW(status_label, L"Operazione in corso…");
            SendMessageW(progress, PBM_SETPOS, 0, 0);
            SendMessageW(progress, PBM_SETSTATE, PBST_NORMAL, 0);
            worker = std::jthread([torrent, magnet, desktop, hwnd] {
                HRESULT const com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
                bool success = false;
                try {
#ifdef SF_UNINSTALLER
                    uninstall();
#else
                    install(torrent, magnet, desktop);
#endif
                    success = true;
                    result_message = L"Operazione completata.";
                } catch (std::exception const&) {
                    result_message = L"Operazione incompleta. Chiudi SeedFlow e riprova. Controlla i permessi sulla cartella.";
                }
                if (SUCCEEDED(com)) CoUninitialize();
                PostMessageW(hwnd, kComplete, success, 0);
            });
        }
        return 0;
    case kProgress:
        if (busy) {
            SendMessageW(progress, PBM_SETPOS, wp, 0);
            std::wstring const label = std::wstring(reinterpret_cast<wchar_t const*>(lp)) + L"  " + std::to_wstring(wp) + L"%";
            SetWindowTextW(status_label, label.c_str());
        }
        return 0;
    case kComplete:
        worker.join();
        busy = false;
        finished = wp != 0;
        if (finished) SendMessageW(progress, PBM_SETPOS, 100, 0);
        SendMessageW(progress, PBM_SETSTATE, finished ? PBST_NORMAL : PBST_ERROR, 0);
        SetWindowTextW(status_label, result_message.c_str());
        SetWindowTextW(primary, finished ? L"Chiudi" : L"Riprova");
        EnableWindow(primary, TRUE);
        if (!finished) {
            for (HWND item : {torrent_box, magnet_box, desktop_box, defaults_box}) if (item) EnableWindow(item, TRUE);
        }
#ifndef SF_UNINSTALLER
        if (finished && SendMessageW(defaults_box, BM_GETCHECK, 0, 0) == BST_CHECKED)
            ShellExecuteW(hwnd, L"open", L"ms-settings:defaultapps", nullptr, nullptr, SW_SHOWNORMAL);
#endif
        return 0;
    case WM_CLOSE:
        if (!busy) DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wp, lp);
}
}
int WINAPI wWinMain(HINSTANCE module, HINSTANCE, PWSTR, int show) {
    instance = module;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
#ifdef SF_UNINSTALLER
        int count = 0;
        auto args = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!args) return 1;
        if (count == 3 && std::wstring(args[1]) == L"--remove") destination = args[2];
        else {
            destination = own_path().parent_path();
            auto temporary = fs::temp_directory_path() / (L"SeedFlow-uninstall-" + std::to_wstring(GetTickCount64()) + L".exe");
            require(CopyFileW(own_path().c_str(), temporary.c_str(), TRUE) != FALSE, L"Copia disinstallazione");
            std::wstring command = quote(temporary.wstring()) + L" --remove " + quote(destination.wstring());
            STARTUPINFOW startup{sizeof(startup)};
            PROCESS_INFORMATION process{};
            require(CreateProcessW(temporary.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process) != FALSE, L"Avvio disinstallazione");
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            LocalFree(args);
            return 0;
        }
        LocalFree(args);
#else
        destination = known_folder(FOLDERID_LocalAppData) / L"Programs" / L"SeedFlow";
#endif
        HANDLE singleton = CreateMutexW(nullptr, FALSE, L"Local\\SeedFlowSetup");
        if (!singleton || GetLastError() == ERROR_ALREADY_EXISTS) {
            if (singleton) CloseHandle(singleton);
            MessageBoxW(nullptr, L"È già aperta un'installazione o disinstallazione di SeedFlow.", L"SeedFlow", MB_OK | MB_ICONINFORMATION);
            return 1;
        }
        INITCOMMONCONTROLSEX common{sizeof(common), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&common);
        logo = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(101), IMAGE_ICON, 64, 64, LR_DEFAULTCOLOR));
        WNDCLASSW cls{};
        cls.lpfnWndProc = procedure;
        cls.hInstance = instance;
        cls.hIcon = logo;
        cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        cls.hbrBackground = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
        cls.lpszClassName = L"SeedFlowSetup";
        RegisterClassW(&cls);
        RECT rect{0,0,646,480};
        AdjustWindowRect(&rect, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
        window = CreateWindowExW(0, cls.lpszClassName, L"SeedFlow " SF_VERSION_WSTRING,
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT,
            rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, instance, nullptr);
        if (!window) return 1;
        ShowWindow(window, show);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
        }
        DeleteObject(normal_font);
        DeleteObject(title_font);
        DestroyIcon(logo);
        CloseHandle(singleton);
#ifdef SF_UNINSTALLER
        // POSIX disposition allows supported Windows versions to unlink the temp helper.
        HANDLE file = CreateFileW(own_path().c_str(), DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, 0, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            FILE_DISPOSITION_INFO_EX disposition{FILE_DISPOSITION_FLAG_DELETE | FILE_DISPOSITION_FLAG_POSIX_SEMANTICS};
            SetFileInformationByHandle(file, FileDispositionInfoEx, &disposition, sizeof(disposition));
            CloseHandle(file);
        }
#endif
        CoUninitialize();
        return 0;
    } catch (...) {
        MessageBoxW(nullptr, L"Impossibile avviare l'installazione.", L"SeedFlow", MB_OK | MB_ICONERROR);
        CoUninitialize();
        return 1;
    }
}
