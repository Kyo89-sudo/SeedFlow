#include "util.hpp"
#include "win.hpp"
#include <psapi.h>
#include <shlobj.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <vector>
#include <cerrno>
#include <limits>
#include <stdexcept>

namespace sf {

std::wstring widen(std::string const& s) {
    if (s.empty()) return {};
    if (s.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) throw std::length_error("UTF-8 input too long");
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) throw std::runtime_error("UTF-8 conversion failed");
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}
std::string narrow(std::wstring const& w) {
    if (w.empty()) return {};
    if (w.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) throw std::length_error("UTF-16 input too long");
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) throw std::runtime_error("UTF-16 conversion failed");
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring fmt_double(double v, int decimals) {
    wchar_t buf[64];
    swprintf(buf, 64, L"%.*f", decimals, v);
    for (wchar_t* p = buf; *p; ++p) if (*p == L'.') *p = L',';
    return buf;
}
std::wstring fmt_int(std::int64_t v) {
    wchar_t raw[32];
    auto magnitude = v < 0 ? std::uint64_t{0} - static_cast<std::uint64_t>(v) : static_cast<std::uint64_t>(v);
    swprintf(raw, 32, L"%llu", static_cast<unsigned long long>(magnitude));
    std::wstring digits = raw, out;
    int count = 0;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        if (count && count % 3 == 0) out.insert(out.begin(), L'.');
        out.insert(out.begin(), *it);
        ++count;
    }
    return v < 0 ? L"-" + out : out;
}
std::wstring fmt_bytes(std::int64_t bytes) {
    static wchar_t const* units[] = {L"B", L"KiB", L"MiB", L"GiB", L"TiB", L"PiB"};
    double v = bytes < 0 ? 0.0 : static_cast<double>(bytes);
    int i = 0;
    while (v >= 1024.0 && i < 5) { v /= 1024.0; ++i; }
    if (i == 0) return fmt_int(static_cast<std::int64_t>(v)) + L" B";
    return fmt_double(v, v >= 100 ? 1 : 2) + L" " + units[i];
}
std::wstring fmt_speed(std::int64_t bps) { return bps <= 0 ? std::wstring() : fmt_bytes(bps) + L"/s"; }
std::wstring fmt_speed0(std::int64_t bps) { return fmt_bytes(bps < 0 ? 0 : bps) + L"/s"; }
std::wstring fmt_duration(std::int64_t s) {
    if (s < 0) return {};
    wchar_t buf[64];
    if (s >= 365LL * 86400) return L"> 1 anno";
    if (s >= 86400) swprintf(buf, 64, L"%lldg %lldh", s / 86400, (s % 86400) / 3600);
    else if (s >= 3600) swprintf(buf, 64, L"%lldh %02lldm", s / 3600, (s % 3600) / 60);
    else if (s >= 60) swprintf(buf, 64, L"%lldm %02llds", s / 60, s % 60);
    else swprintf(buf, 64, L"%llds", s);
    return buf;
}
std::wstring fmt_date(std::time_t t) {
    if (t <= 0) return L"—";
    std::tm tm{};
    localtime_s(&tm, &t);
    wchar_t buf[64];
    wcsftime(buf, 64, L"%d/%m/%Y %H:%M", &tm);
    return buf;
}
std::wstring fmt_ratio(double r, bool has_traffic) {
    if (std::isinf(r)) return L"∞";
    if (!has_traffic) return L"—";
    return fmt_double(r, 2);
}
bool parse_double(std::wstring const& text, double& out) {
    std::wstring t;
    for (wchar_t c : text) if (c != L' ') t += (c == L',' ? L'.' : c);
    if (t.empty()) return false;
    wchar_t* end = nullptr;
    errno = 0;
    double value = std::wcstod(t.c_str(), &end);
    if (end == t.c_str() || *end != 0 || errno == ERANGE || !std::isfinite(value)) return false;
    out = value;
    return true;
}
bool parse_int(std::wstring const& text, long long& out) {
    std::wstring t;
    for (wchar_t c : text) if (c != L' ' && c != L'.') t += c;
    if (t.empty()) return false;
    wchar_t* end = nullptr;
    errno = 0;
    long long value = std::wcstoll(t.c_str(), &end, 10);
    if (end == t.c_str() || *end != 0 || errno == ERANGE) return false;
    out = value;
    return true;
}
double effective_ratio(std::int64_t downloaded, std::int64_t uploaded, std::int64_t total_done) {
    std::int64_t base = downloaded < total_done / 100 ? total_done : downloaded;
    if (base <= 0) return uploaded > 0 ? INFINITY : 0.0;
    return static_cast<double>(uploaded) / static_cast<double>(base);
}

std::filesystem::path app_data_dir() {
    PWSTR p = nullptr;
    std::filesystem::path out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p))) out = std::filesystem::path(p) / L"SeedFlow";
    if (p) CoTaskMemFree(p);
    if (out.empty()) out = exe_path().parent_path() / L"SeedFlowData";
    return out;
}
std::filesystem::path state_dir() { return app_data_dir() / L"State"; }
std::filesystem::path default_download_dir() {
    PWSTR p = nullptr;
    std::filesystem::path out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &p))) out = std::filesystem::path(p) / L"SeedFlow";
    if (p) CoTaskMemFree(p);
    if (out.empty()) out = app_data_dir() / L"Download";
    return out;
}
std::filesystem::path exe_path() {
    std::vector<wchar_t> buf(32768);
    DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    return std::filesystem::path(std::wstring(buf.data(), n));
}

MemoryUsage memory_usage() {
    MemoryUsage m;
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        m.working_set = pmc.WorkingSetSize;
        m.private_bytes = pmc.PrivateUsage;
        m.peak_working_set = pmc.PeakWorkingSetSize;
    }
    return m;
}
// Pages come back on demand: lowers the working set shown by Task Manager while
// the window is hidden; it does not reduce the memory the engine really needs.
void trim_working_set() {
    HANDLE heaps[64];
    DWORD n = GetProcessHeaps(64, heaps);
    for (DWORD i = 0; i < n && i < 64; ++i) HeapCompact(heaps[i], 0);
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
}

bool copy_to_clipboard(void* hwnd, std::wstring const& text) {
    if (text.size() > (std::numeric_limits<std::size_t>::max)() / sizeof(wchar_t) - 1) return false;
    std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    bool ok = false;
    if (mem) {
        if (void* dst = GlobalLock(mem)) {
            std::memcpy(dst, text.c_str(), bytes);
            GlobalUnlock(mem);
            if (OpenClipboard(static_cast<HWND>(hwnd))) {
                ok = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
                CloseClipboard();
            }
        }
        if (!ok) GlobalFree(mem);
    }
    return ok;
}
std::wstring clipboard_text(void* hwnd) {
    std::wstring out;
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(static_cast<HWND>(hwnd))) return out;
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (auto* p = static_cast<wchar_t const*>(GlobalLock(h))) {
            auto const count = GlobalSize(h) / sizeof(wchar_t);
            auto const* end = std::find(p, p + count, L'\0');
            if (end != p + count) out.assign(p, end);
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return out;
}
void open_folder_select(std::wstring const& path) {
    std::wstring args = L"/select,\"" + path + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
}
void open_path(std::wstring const& path) {
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// ------------------------------------------------------------------ process / autostart
void apply_process_priority(int priority) {
    DWORD cls = priority == 2 ? IDLE_PRIORITY_CLASS : priority == 1 ? BELOW_NORMAL_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS;
    SetPriorityClass(GetCurrentProcess(), cls);
}

static wchar_t const kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
bool autostart_enabled() {
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, L"SeedFlow", RRF_RT_REG_SZ, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
}
bool set_autostart(bool on, bool minimized) {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
    LSTATUS r;
    if (on) {
        std::wstring cmd = L"\"" + exe_path().wstring() + L"\"" + (minimized ? L" --tray" : L"");
        r = RegSetValueExW(key, L"SeedFlow", 0, REG_SZ, reinterpret_cast<BYTE const*>(cmd.c_str()), static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        r = RegDeleteValueW(key, L"SeedFlow");
        if (r == ERROR_FILE_NOT_FOUND) r = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return r == ERROR_SUCCESS;
}

} // namespace sf
