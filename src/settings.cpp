#include "settings.hpp"
#include "file_io.hpp"
#include <algorithm>
#include "util.hpp"
#include <libtorrent/entry.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/bdecode.hpp>
#include <iterator>
#include <regex>
#include <vector>
#include <limits>

namespace lt = libtorrent;
namespace fs = std::filesystem;

namespace sf {

wchar_t const* const kRatioActionLabels[4] = {
    L"Metti in pausa", L"Passa a Super-seeding", L"Rimuovi dalla lista (file conservati)", L"Rimuovi e cancella i file"
};

static fs::path settings_file() { return state_dir() / L"settings.dat"; }

static bool read_file(fs::path const& p, std::vector<char>& out) {
    // Settings and the old JSON import are small metadata files, not payload data.
    return file_io::read_bounded(p, out, 32u * 1024u * 1024u);
}

static std::int64_t gi(lt::bdecode_node const& d, char const* k, std::int64_t def) { return d.dict_find_int_value(k, def); }
static int gint(lt::bdecode_node const& d, char const* k, int def) {
    auto const value = gi(d, k, def);
    return value < (std::numeric_limits<int>::min)() || value > (std::numeric_limits<int>::max)()
        ? def : static_cast<int>(value);
}
static std::string gs(lt::bdecode_node const& d, char const* k, std::string const& def) {
    auto v = d.dict_find_string(k);
    return v ? std::string(v.string_value()) : def;
}
static double gd(lt::bdecode_node const& d, char const* k, double def) {
    // doubles stored as integer * 1000 (bencode has no floats)
    return static_cast<double>(d.dict_find_int_value(k, static_cast<std::int64_t>(def * 1000))) / 1000.0;
}

// Values that fit an int can still be meaningless (port 0, encryption 7, a negative
// connection limit...): bring every persisted value back into the range the dialogs
// accept, so a damaged or hand-edited settings.dat cannot reach libtorrent as is.
static void sanitize(AppSettings& s) {
    AppSettings const def;
    auto in = [](int v, int lo, int hi) { return v >= lo && v <= hi; };
    auto fix = [&](int& v, int lo, int hi, int fallback) { if (!in(v, lo, hi)) v = fallback; };
    fix(s.port, 1024, 65535, def.port);
    fix(s.encryption, 0, 2, def.encryption);
    fix(s.down_kib, 0, 2000000, 0);
    fix(s.up_kib, 0, 2000000, 0);
    fix(s.connections, 10, 20000, def.connections);
    if (s.upload_slots != -1) fix(s.upload_slots, 1, 2000, def.upload_slots);
    fix(s.active_downloads, 1, 1000, def.active_downloads);
    fix(s.active_seeds, 1, 1000, def.active_seeds);
    fix(s.global_ratio_action, 0, 3, def.global_ratio_action);
    fix(s.disk_io, 0, 3, def.disk_io);
    fix(s.process_priority, 0, 2, def.process_priority);
    fix(s.create_format, 0, 2, 0);
    fix(s.create_piece, 0, 11, 0);
    if (s.create_trackers.size() > 64 * 1024) s.create_trackers.clear();
    fix(s.split_left, 60, 2000, def.split_left);
    fix(s.split_bottom, 60, 4000, def.split_bottom);
    fix(s.win_w, 0, 30000, 0);
    fix(s.win_h, 0, 30000, 0);
    if (!(s.global_ratio >= 0 && s.global_ratio <= 10000)) s.global_ratio = 0;
    for (auto& [hash, m] : s.torrents) {
        fix(m.ratio_mode, RatioGlobal, RatioCustom, RatioGlobal);
        fix(m.ratio_action, -1, 3, -1);
        if (!(m.ratio_limit > 0 && m.ratio_limit <= 10000)) m.ratio_limit = 2.0;
    }
}

static bool parse_settings(std::vector<char> const& buf, AppSettings& s) {
    lt::error_code ec;
    lt::bdecode_node d = lt::bdecode(buf, ec);
    if (ec || d.type() != lt::bdecode_node::dict_t) return false;
    AppSettings def;
    s.download_dir = gs(d, "download_dir", def.download_dir);
    s.close_to_tray = gi(d, "close_to_tray", 0) != 0;
    s.minimize_to_tray = gi(d, "minimize_to_tray", 0) != 0;
    s.trim_when_hidden = gi(d, "trim_when_hidden", 1) != 0;
    s.start_minimized = gi(d, "start_minimized", 1) != 0;
    s.process_priority = std::clamp(gint(d, "process_priority", 0), 0, 2);
    s.create_format = gint(d, "create_format", 0);
    s.create_piece = gint(d, "create_piece", 0);
    s.create_trackers = gs(d, "create_trackers", "");
    s.create_seed = gi(d, "create_seed", 1) != 0;
    s.create_private = gi(d, "create_private", 0) != 0;
    s.create_copy_magnet = gi(d, "create_copy_magnet", 0) != 0;
    s.start_paused = gi(d, "start_paused", 0) != 0;
    s.confirm_remove = gi(d, "confirm_remove", 1) != 0;
    s.user_agent = gs(d, "user_agent", def.user_agent);
    if (s.user_agent.rfind("SeedFlow/", 0) == 0) s.user_agent = SF_USER_AGENT; // follow app updates
    s.send_client_name = gi(d, "send_client_name", 1) != 0;
    s.peer_id_prefix = gs(d, "peer_id_prefix", "");
    s.client_name = gs(d, "client_name", "");
    // Presets of earlier versions follow the updated ones (same client, current release).
    struct PresetUpdate { char const *ua, *peer, *client, *new_ua, *new_peer, *new_client; };
    static PresetUpdate const kPresetUpdates[] = {
        {"qBittorrent/5.1.4", "-qB5140-", "qBittorrent/5.1.4", "qBittorrent/5.3.0", "-qB5300-", "qBittorrent/5.3.0"},
        {"Transmission/4.1.1", "-TR4110-", "Transmission 4.1.1", "Transmission/4.1.3", "-TR4130-", "Transmission 4.1.3"},
    };
    for (auto const& u : kPresetUpdates)
        if (s.user_agent == u.ua && s.peer_id_prefix == u.peer && s.client_name == u.client) {
            s.user_agent = u.new_ua; s.peer_id_prefix = u.new_peer; s.client_name = u.new_client;
        }
    s.anonymous_mode = gi(d, "anonymous_mode", 0) != 0;
    s.port = gint(d, "port", def.port);
    s.dht = gi(d, "dht", 1) != 0; s.upnp = gi(d, "upnp", 1) != 0;
    s.natpmp = gi(d, "natpmp", 1) != 0; s.lsd = gi(d, "lsd", 1) != 0; s.pex = gi(d, "pex", 1) != 0; s.ipv6 = gi(d, "ipv6", 1) != 0;
    s.announce_all_trackers = gi(d, "announce_all_trackers", 1) != 0; s.announce_all_tiers = gi(d, "announce_all_tiers", 1) != 0;
    s.encryption = gint(d, "encryption", 1);
    s.auto_port_check = gi(d, "auto_port_check", 0) != 0;
    s.down_kib = gint(d, "down_kib", 0);
    s.up_kib = gint(d, "up_kib", 0);
    s.connections = gint(d, "connections", def.connections);
    s.upload_slots = gint(d, "upload_slots", def.upload_slots);
    s.active_downloads = gint(d, "active_downloads", def.active_downloads);
    s.active_seeds = gint(d, "active_seeds", def.active_seeds);
    s.preallocate = gi(d, "preallocate", 0) != 0;
    s.global_ratio = gd(d, "global_ratio", 0.0);
    s.global_ratio_action = gint(d, "global_ratio_action", ActPause);
    s.disk_io = gint(d, "disk_io", 0);
    s.perf_profile = gs(d, "perf_profile", "");
    s.connection_profile = gs(d, "connection_profile", "");
    s.win_x = gint(d, "win_x", -1); s.win_y = gint(d, "win_y", -1);
    s.win_w = gint(d, "win_w", 0); s.win_h = gint(d, "win_h", 0);
    s.win_max = gint(d, "win_max", 0);
    s.split_left = gint(d, "split_left", def.split_left);
    s.split_bottom = gint(d, "split_bottom", def.split_bottom);
    s.columns = gs(d, "columns", "");
    s.migrated = gi(d, "migrated", 0) != 0;
    s.int_settings.clear(); s.bool_settings.clear(); s.torrents.clear();
    if (auto is = d.dict_find_dict("int_settings"))
        for (int i = 0; i < is.dict_size(); ++i) {
            auto [k, v] = is.dict_at(i);
            if (v.type() == lt::bdecode_node::int_t && v.int_value() >= (std::numeric_limits<int>::min)() &&
                v.int_value() <= (std::numeric_limits<int>::max)()) s.int_settings[std::string(k)] = static_cast<int>(v.int_value());
        }
    if (auto bs = d.dict_find_dict("bool_settings"))
        for (int i = 0; i < bs.dict_size(); ++i) {
            auto [k, v] = bs.dict_at(i);
            if (v.type() == lt::bdecode_node::int_t) s.bool_settings[std::string(k)] = v.int_value() != 0;
        }
    s.stats = {};
    if (auto st = d.dict_find_dict("stats")) {
        s.stats.downloaded = gi(st, "downloaded", 0); s.stats.uploaded = gi(st, "uploaded", 0);
        s.stats.runtime = gi(st, "runtime", 0); s.stats.peak_down = gi(st, "peak_down", 0);
        s.stats.peak_up = gi(st, "peak_up", 0); s.stats.launches = gint(st, "launches", 0);
        s.stats.added = gint(st, "added", 0); s.stats.completed = gint(st, "completed", 0);
        s.stats.since = gi(st, "since", 0);
    }
    if (auto ts = d.dict_find_dict("torrents"))
        for (int i = 0; i < ts.dict_size(); ++i) {
            auto [k, v] = ts.dict_at(i);
            if (v.type() != lt::bdecode_node::dict_t) continue;
            TorrentMeta m;
            m.ratio_mode = gint(v, "ratio_mode", RatioGlobal);
            m.ratio_limit = gd(v, "ratio_limit", 2.0);
            m.ratio_action = gint(v, "ratio_action", -1);
            m.ratio_done = gi(v, "ratio_done", 0) != 0;
            m.added = gi(v, "added", 0);
            m.order = std::max(0, gint(v, "order", 0));
            s.torrents[std::string(k)] = m;
        }
    sanitize(s);
    return true;
}

bool load_settings(AppSettings& s) {
    std::vector<char> buf;
    for (auto const& p : {settings_file(), fs::path(settings_file().wstring() + L".bak")})
        if (read_file(p, buf) && parse_settings(buf, s)) return true;
    return false;
}

bool save_settings(AppSettings const& s, std::string& error) {
    try {
        lt::entry e(lt::entry::dictionary_t);
        auto ms = [](double v) { return static_cast<std::int64_t>(v * 1000.0 + (v >= 0 ? 0.5 : -0.5)); };
        e["download_dir"] = s.download_dir;
        e["close_to_tray"] = s.close_to_tray; e["minimize_to_tray"] = s.minimize_to_tray;
        e["trim_when_hidden"] = s.trim_when_hidden; e["start_paused"] = s.start_paused;
        e["start_minimized"] = s.start_minimized; e["process_priority"] = s.process_priority;
        e["create_format"] = s.create_format; e["create_piece"] = s.create_piece; e["create_trackers"] = s.create_trackers;
        e["create_seed"] = s.create_seed; e["create_private"] = s.create_private; e["create_copy_magnet"] = s.create_copy_magnet;
        e["confirm_remove"] = s.confirm_remove; e["user_agent"] = s.user_agent;
        e["send_client_name"] = s.send_client_name;
        e["peer_id_prefix"] = s.peer_id_prefix; e["client_name"] = s.client_name; e["anonymous_mode"] = s.anonymous_mode;
        e["port"] = s.port; e["dht"] = s.dht; e["upnp"] = s.upnp; e["natpmp"] = s.natpmp; e["lsd"] = s.lsd; e["pex"] = s.pex; e["ipv6"] = s.ipv6; e["announce_all_trackers"] = s.announce_all_trackers; e["announce_all_tiers"] = s.announce_all_tiers;
        e["encryption"] = s.encryption; e["auto_port_check"] = s.auto_port_check;
        e["down_kib"] = s.down_kib; e["up_kib"] = s.up_kib;
        e["connections"] = s.connections; e["upload_slots"] = s.upload_slots;
        e["active_downloads"] = s.active_downloads; e["active_seeds"] = s.active_seeds;
        e["preallocate"] = s.preallocate;
        e["global_ratio"] = ms(s.global_ratio); e["global_ratio_action"] = s.global_ratio_action;
        e["disk_io"] = s.disk_io; e["perf_profile"] = s.perf_profile; e["connection_profile"] = s.connection_profile;
        e["win_x"] = s.win_x; e["win_y"] = s.win_y; e["win_w"] = s.win_w; e["win_h"] = s.win_h; e["win_max"] = s.win_max;
        e["split_left"] = s.split_left; e["split_bottom"] = s.split_bottom; e["columns"] = s.columns;
        e["migrated"] = s.migrated;
        auto& is = e["int_settings"]; is = lt::entry(lt::entry::dictionary_t);
        for (auto const& [k, v] : s.int_settings) is[k] = v;
        auto& bs = e["bool_settings"]; bs = lt::entry(lt::entry::dictionary_t);
        for (auto const& [k, v] : s.bool_settings) bs[k] = v;
        auto& st = e["stats"];
        st["downloaded"] = s.stats.downloaded; st["uploaded"] = s.stats.uploaded; st["runtime"] = s.stats.runtime;
        st["peak_down"] = s.stats.peak_down; st["peak_up"] = s.stats.peak_up; st["launches"] = s.stats.launches;
        st["added"] = s.stats.added; st["completed"] = s.stats.completed; st["since"] = s.stats.since;
        auto& ts = e["torrents"]; ts = lt::entry(lt::entry::dictionary_t);
        for (auto const& [k, m] : s.torrents) {
            auto& t = ts[k];
            t["ratio_mode"] = m.ratio_mode; t["ratio_limit"] = ms(m.ratio_limit);
            t["ratio_action"] = m.ratio_action; t["ratio_done"] = m.ratio_done; t["added"] = m.added; t["order"] = m.order;
        }
        std::vector<char> out;
        lt::bencode(std::back_inserter(out), e);
        return file_io::write_atomic(settings_file(), out, error, true);
    } catch (std::exception const& ex) { error = ex.what(); return false; }
}

// Minimal reader for the old WPF session.json: only flat scalar keys are needed.
static std::string json_unescape(std::string const& v) {
    std::string out;
    auto hex4 = [&](std::size_t pos, unsigned& cp) {
        if (pos + 4 > v.size()) return false;
        cp = 0;
        for (std::size_t k = pos; k < pos + 4; ++k) {
            unsigned char c = static_cast<unsigned char>(v[k]);
            unsigned digit = c >= '0' && c <= '9' ? c - '0' :
                c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 16;
            if (digit > 15) return false;
            cp = (cp << 4) | digit;
        }
        return true;
    };
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (v[i] == '\\' && i + 1 < v.size()) {
            char c = v[++i];
            if (c == 'n') out += '\n'; else if (c == 't') out += '\t';
            else if (c == 'r') out += '\r'; else if (c == 'b') out += '\b'; else if (c == 'f') out += '\f';
            else if (c == 'u' && i + 4 < v.size()) {
                unsigned cp = 0;
                if (!hex4(i + 1, cp)) { out += narrow(L"\ufffd"); continue; }
                i += 4;
                std::wstring w(1, static_cast<wchar_t>(cp));
                if (cp >= 0xd800 && cp <= 0xdbff) {
                    unsigned low = 0;
                    if (i + 6 < v.size() && v[i + 1] == '\\' && v[i + 2] == 'u' &&
                        hex4(i + 3, low) && low >= 0xdc00 && low <= 0xdfff) {
                        w += static_cast<wchar_t>(low);
                        i += 6;
                    } else w = L"\ufffd";
                } else if (cp >= 0xdc00 && cp <= 0xdfff) w = L"\ufffd";
                out += narrow(w);
            } else out += c;
        } else out += v[i];
    }
    return out;
}
LegacyImport import_legacy(AppSettings& s) {
    LegacyImport r;
    std::vector<char> buf;
    if (!read_file(app_data_dir() / L"session.json", buf)) return r;
    std::string text(buf.begin(), buf.end());
    r.found = true;
    auto num = [&](char const* key, int& target) {
        std::smatch m; std::regex re(std::string("\"") + key + "\"\\s*:\\s*(-?\\d+)");
        if (std::regex_search(text, m, re)) target = std::stoi(m[1]);
    };
    auto boolean = [&](char const* key, bool& target) {
        std::smatch m; std::regex re(std::string("\"") + key + "\"\\s*:\\s*(true|false)");
        if (std::regex_search(text, m, re)) target = m[1] == "true";
    };
    auto str = [&](char const* key, std::string& target) {
        std::smatch m; std::regex re(std::string("\"") + key + "\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
        if (std::regex_search(text, m, re)) target = json_unescape(m[1]);
    };
    num("ListenPort", s.port); num("GlobalDownloadKiB", s.down_kib); num("GlobalUploadKiB", s.up_kib);
    num("MaxConnections", s.connections); num("UploadSlots", s.upload_slots);
    num("ActiveDownloads", s.active_downloads); num("ActiveSeeds", s.active_seeds); num("EncryptionMode", s.encryption);
    boolean("EnableDht", s.dht); boolean("EnableUpnp", s.upnp); boolean("EnableNatPmp", s.natpmp);
    boolean("EnableLsd", s.lsd); boolean("CloseToTray", s.close_to_tray); boolean("PreallocateFiles", s.preallocate);
    str("DownloadFolder", s.download_dir);
    std::string ua; str("UserAgent", ua);
    if (!ua.empty() && ua.rfind("SeedFlow/", 0) != 0) s.user_agent = ua;
    r.folder = s.download_dir;
    std::regex src("\"Source\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), src); it != std::sregex_iterator(); ++it) {
        std::string v = json_unescape((*it)[1]);
        if (v.rfind("magnet:?", 0) == 0) r.magnets.push_back(v);
        else if (v.size() > 8 && v.substr(v.size() - 8) == ".torrent") r.torrent_files.push_back(v);
    }
    return r;
}

} // namespace sf
