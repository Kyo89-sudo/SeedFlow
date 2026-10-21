// SeedFlow - small helpers: UTF-8/UTF-16, formatting, paths, memory.
#pragma once
#include <string>
#include <cstdint>
#include <ctime>
#include <filesystem>

namespace sf {

std::wstring widen(std::string const& utf8);
std::string narrow(std::wstring const& wide);

std::wstring fmt_bytes(std::int64_t bytes);              // "1,23 GiB"
std::wstring fmt_speed(std::int64_t bytes_per_second);   // "" when 0
std::wstring fmt_speed0(std::int64_t bytes_per_second);  // "0 B/s" when 0
std::wstring fmt_duration(std::int64_t seconds);         // "2g 3h", "" when < 0
std::wstring fmt_date(std::time_t t);                    // "dd/mm/yyyy hh:mm" or "-"
std::wstring fmt_ratio(double ratio, bool has_traffic = true);
std::wstring fmt_double(double v, int decimals);
std::wstring fmt_int(std::int64_t v);                    // thousands separators
bool parse_double(std::wstring const& text, double& out); // accepts ',' or '.'
bool parse_int(std::wstring const& text, long long& out);

// qBittorrent-style ratio: when (almost) nothing was downloaded because the data
// was added complete, divide by the completed size instead of returning infinity.
double effective_ratio(std::int64_t downloaded, std::int64_t uploaded, std::int64_t total_done);

std::filesystem::path app_data_dir();      // %LOCALAPPDATA%\SeedFlow
std::filesystem::path state_dir();         // %LOCALAPPDATA%\SeedFlow\State
std::filesystem::path default_download_dir();
std::filesystem::path exe_path();

struct MemoryUsage { std::uint64_t working_set = 0, private_bytes = 0, peak_working_set = 0; };
MemoryUsage memory_usage();
void trim_working_set();

bool copy_to_clipboard(void* hwnd, std::wstring const& text);
std::wstring clipboard_text(void* hwnd);
void open_folder_select(std::wstring const& path);
void open_path(std::wstring const& path);

// Process priority class: 0 normal / 1 below normal / 2 idle.
void apply_process_priority(int priority);

// "Start with Windows" (HKCU\...\Run\SeedFlow).
bool autostart_enabled();
bool set_autostart(bool on, bool minimized);

} // namespace sf
