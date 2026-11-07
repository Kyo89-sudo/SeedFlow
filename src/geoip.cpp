#include "geoip.hpp"
#include "util.hpp"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sf::geoip {
namespace fs = std::filesystem;
using NetworkKey = std::array<char, 18>;
struct NetworkHash {
    std::size_t operator()(NetworkKey const& key) const noexcept {
        return std::hash<std::string_view>{}({key.data(), key.size()});
    }
};
struct Database {
    std::unordered_map<NetworkKey, std::string, NetworkHash> networks;
    std::unordered_map<std::string, std::string> resolved;
};
struct Resolver {
    std::string directory;
    bool requested = false;
    std::unique_ptr<Database> active;
    std::mutex mutex;
    bool finished = false;
    std::unique_ptr<Database> pending;
    // Last member: joins before the mutex/result slot is destroyed.
    std::jthread worker;
};
static Resolver& resolver() {
    static Resolver value;
    return value;
}
static std::vector<std::string_view> fields(std::string const& row) {
    std::string_view text(row);
    if (!text.empty() && text.back() == '\r') text.remove_suffix(1);
    std::vector<std::string_view> out;
    std::size_t start = 0;
    bool quoted = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '"') {
            if (quoted && i + 1 < text.size() && text[i + 1] == '"') { ++i; continue; }
            quoted = !quoted;
        } else if (text[i] == ',' && !quoted) {
            out.push_back(text.substr(start, i - start));
            start = i + 1;
        }
    }
    if (quoted) return {};
    out.push_back(text.substr(start));
    for (auto& field : out)
        if (field.size() >= 2 && field.front() == '"' && field.back() == '"')
            field = field.substr(1, field.size() - 2);
    return out;
}
static NetworkKey key(int family, std::array<unsigned char, 16> const& address, int prefix, int bytes) {
    NetworkKey out{};
    out[0] = static_cast<char>(family);
    out[1] = static_cast<char>(prefix);
    for (int i = 0; i < bytes; ++i) {
        int const bits = std::clamp(prefix - i * 8, 0, 8);
        unsigned int const mask = bits == 0 ? 0 : (0xffu << (8 - bits)) & 0xffu;
        out[static_cast<std::size_t>(i + 2)] = static_cast<char>(address[static_cast<std::size_t>(i)] & mask);
    }
    return out;
}
static bool readable_size(fs::path const& path, std::uintmax_t limit) {
    std::error_code error;
    auto const size = fs::file_size(path, error);
    return !error && size <= limit;
}
static void load_blocks(fs::path const& path, int family, int bytes,
                        std::unordered_map<std::string, std::string> const& countries,
                        Database& db, std::stop_token stop) {
    if (stop.stop_requested() || !readable_size(path, 256ull * 1024 * 1024)) return;
    std::ifstream in(path, std::ios::binary);
    std::string row;
    std::getline(in, row); // header
    while (!stop.stop_requested() && std::getline(in, row)) {
        if (row.empty() || row.size() > 4096) continue;
        auto const columns = fields(row);
        if (columns.size() < 3) continue;
        auto const slash = columns[0].find('/');
        if (slash == std::string_view::npos) continue;
        auto const text = columns[0].substr(slash + 1);
        int prefix = 0;
        auto const parsed = std::from_chars(text.data(), text.data() + text.size(), prefix);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || prefix < 0 || prefix > bytes * 8) continue;
        auto country = countries.find(std::string(columns[1]));
        if (country == countries.end()) country = countries.find(std::string(columns[2]));
        if (country == countries.end()) continue;
        std::array<unsigned char, 16> address{};
        std::string const ip(columns[0].substr(0, slash));
        if (InetPtonA(family, ip.c_str(), address.data()) != 1) continue;
        db.networks.emplace(key(family, address, prefix, bytes), country->second);
    }
}
static void load(Database& db, std::string const& directory, std::stop_token stop) {
    if (stop.stop_requested()) return;
    fs::path const root(widen(directory));
    auto const locations = root / L"GeoLite2-Country-Locations-en.csv";
    if (!readable_size(locations, 64ull * 1024 * 1024)) return;
    std::ifstream in(locations, std::ios::binary);
    std::unordered_map<std::string, std::string> countries;
    std::string row;
    std::getline(in, row);
    while (!stop.stop_requested() && std::getline(in, row)) {
        if (row.empty() || row.size() > 4096) continue;
        auto const columns = fields(row);
        if (columns.size() < 5 || columns[0].empty() || columns[1] != "en" || columns[4].size() != 2) continue;
        std::string code(columns[4]);
        if (!std::all_of(code.begin(), code.end(), [](unsigned char c) { return c >= 'A' && c <= 'Z'; })) continue;
        countries.emplace(std::string(columns[0]), std::move(code));
    }
    if (countries.empty()) return;
    load_blocks(root / L"GeoLite2-Country-Blocks-IPv4.csv", AF_INET, 4, countries, db, stop);
    load_blocks(root / L"GeoLite2-Country-Blocks-IPv6.csv", AF_INET6, 16, countries, db, stop);
}
static std::string find_country(Database const& db, std::string const& text) {
    std::array<unsigned char, 16> address{};
    int family = AF_INET;
    int bytes = 4;
    if (InetPtonA(AF_INET, text.c_str(), address.data()) != 1) {
        family = AF_INET6;
        bytes = 16;
        if (InetPtonA(AF_INET6, text.c_str(), address.data()) != 1) return {};
        if (std::all_of(address.begin(), address.begin() + 10, [](unsigned char b) { return b == 0; }) &&
            address[10] == 0xff && address[11] == 0xff) {
            std::array<unsigned char, 16> v4{};
            std::copy(address.begin() + 12, address.end(), v4.begin());
            address = v4;
            family = AF_INET;
            bytes = 4;
        }
    }
    for (int prefix = bytes * 8; prefix >= 0; --prefix) {
        auto const found = db.networks.find(key(family, address, prefix, bytes));
        if (found != db.networks.end()) return found->second;
    }
    return {};
}
void set_directory(std::string directory) {
    auto& state = resolver();
    if (state.directory == directory) return;
    state.directory = std::move(directory);
    state.requested = false;
    state.active.reset();
    state.worker.request_stop();
    std::lock_guard lock(state.mutex);
    state.pending.reset();
}
bool prepare_peer_flags() {
    auto& state = resolver();
    if (state.directory.empty()) return false;
    if (state.worker.joinable()) {
        {
            std::lock_guard lock(state.mutex);
            if (!state.finished) return false;
            state.active = std::move(state.pending);
            state.finished = false;
        }
        state.worker.join();
    }
    if (!state.requested) {
        state.requested = true;
        try {
            state.worker = std::jthread([&state, directory = state.directory](std::stop_token stop) {
                std::unique_ptr<Database> result;
                try {
                    result = std::make_unique<Database>();
                    load(*result, directory, stop);
                } catch (...) { result.reset(); }
                if (stop.stop_requested()) result.reset();
                std::lock_guard lock(state.mutex);
                if (!stop.stop_requested()) state.pending = std::move(result);
                state.finished = true;
            });
        } catch (std::system_error const&) {
            // Optional feature: keep the peer list usable if a thread cannot start.
        }
        return false;
    }
    return state.active && !state.active->networks.empty();
}
std::string peer_country(std::string const& address) {
    auto& state = resolver();
    if (!state.active) return {};
    auto& db = *state.active;
    auto found = db.resolved.find(address);
    if (found == db.resolved.end()) {
        if (db.resolved.size() >= 20000) db.resolved.clear();
        found = db.resolved.emplace(address, find_country(db, address)).first;
    }
    return found->second;
}
}
