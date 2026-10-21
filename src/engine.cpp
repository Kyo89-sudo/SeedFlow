#include "engine.hpp"
#include "version.h"
#include <libtorrent/session_params.hpp>
#include <libtorrent/session_stats.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/error_code.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/announce_entry.hpp>
#include <libtorrent/peer_info.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/write_resume_data.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/fingerprint.hpp>
#include <libtorrent/version.hpp>
#include <libtorrent/span.hpp>
#include <libtorrent/posix_disk_io.hpp>
#include <libtorrent/mmap_disk_io.hpp>
#include <libtorrent/extensions/ut_pex.hpp>
#include <libtorrent/extensions/ut_metadata.hpp>
#include <libtorrent/extensions/smart_ban.hpp>
#if __has_include(<libtorrent/pread_disk_io.hpp>)
#include <libtorrent/pread_disk_io.hpp>
#define SF_HAS_PREAD 1
#else
#define SF_HAS_PREAD 0
#endif
#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>
#include <thread>
#include <set>

// comment()/creator()/files()/make_magnet_uri(handle) are deprecated in 2.1 but
// still provided with deprecated functions enabled (vcpkg feature "deprfun").
#if defined(_MSC_VER)
#pragma warning(disable: 4996)
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

namespace lt = libtorrent;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace sf {

static char const* const kVersion = SF_VERSION_STRING;

Engine::Engine() = default;
Engine::~Engine() { if (session_) stop(3s); }

std::string Engine::libtorrent_version() { return LIBTORRENT_VERSION; }
int Engine::abi_version() const { return TORRENT_ABI_VERSION; }

static std::string listen_string(int port, bool ipv6) {
    std::string v4 = "0.0.0.0:" + std::to_string(port);
    return ipv6 ? v4 + ",[::]:" + std::to_string(port) : v4;
}
template <typename H> static std::string hex_of(H const& h) { std::ostringstream s; s << h; return s.str(); }
static std::string hash_key(lt::info_hash_t const& ih) { return hex_of(ih.get_best()); }

static fs::path u8path(std::string const& s) {
#if defined(__cpp_char8_t)
    return fs::path(std::u8string(s.begin(), s.end()));
#else
    return fs::u8path(s);
#endif
}
static bool read_file(fs::path const& p, std::vector<char>& out) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return !out.empty();
}
static bool write_file_atomic(fs::path const& p, std::vector<char> const& data) {
    fs::path tmp = p; tmp += L".tmp";
    {
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        if (!o) return false;
        o.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!o) return false;
    }
    std::error_code ec;
    fs::rename(tmp, p, ec);
    return !ec;
}

bool Engine::start(EngineConfig const& cfg, std::string& error) {
    if (session_) return true;
    try {
        state_dir_ = cfg.state_dir;
        std::error_code fec;
        if (!state_dir_.empty()) fs::create_directories(state_dir_, fec);
        port_ = cfg.port; dht_ = cfg.dht; upnp_ = cfg.upnp; natpmp_ = cfg.natpmp; lsd_ = cfg.lsd; pex_ = cfg.pex;

        lt::session_params params;
        // Restore the DHT routing table saved at the last exit (faster bootstrap).
        std::vector<char> state;
        if (!state_dir_.empty() && read_file(state_dir_ / L"session.dat", state)) {
            try { params = lt::read_session_params(state, lt::session_handle::save_dht_state); }
            catch (...) { params = lt::session_params(); }
        }
        auto& s = params.settings;
        s.set_str(lt::settings_pack::user_agent, cfg.user_agent);
        s.set_str(lt::settings_pack::peer_fingerprint, lt::generate_fingerprint("SF", SF_VERSION_MAJOR, SF_VERSION_MINOR, SF_VERSION_PATCH, 0));
        s.set_str(lt::settings_pack::handshake_client_version, std::string("SeedFlow ") + kVersion);
        s.set_str(lt::settings_pack::listen_interfaces, listen_string(cfg.port, cfg.ipv6));
        ipv6_ = cfg.ipv6;
        s.set_bool(lt::settings_pack::enable_dht, cfg.dht);
        s.set_bool(lt::settings_pack::enable_upnp, cfg.upnp);
        s.set_bool(lt::settings_pack::enable_natpmp, cfg.natpmp);
        s.set_bool(lt::settings_pack::enable_lsd, cfg.lsd);
        s.set_int(lt::settings_pack::alert_mask, static_cast<int>(static_cast<std::uint32_t>(
            lt::alert_category::error | lt::alert_category::status | lt::alert_category::port_mapping
            | lt::alert_category::storage | lt::alert_category::performance_warning)));
        // Loading many torrents at startup produces several alerts each: a small queue
        // dropped the listen/port-mapping alerts that the diagnostics rely on.
        s.set_int(lt::settings_pack::alert_queue_size, 5000);
        // Like uTorrent/qBittorrent: announce to every tracker and tier, so each
        // tracker shows its real state and more peers are found.
        s.set_bool(lt::settings_pack::announce_to_all_tiers, true);
        s.set_bool(lt::settings_pack::announce_to_all_trackers, true);
        switch (cfg.disk_io) {
#if SF_HAS_PREAD
            case 1: params.disk_io_constructor = lt::pread_disk_io_constructor; disk_io_name_ = "pread"; break;
#endif
            case 2: params.disk_io_constructor = lt::mmap_disk_io_constructor; disk_io_name_ = "mmap"; break;
            case 3: params.disk_io_constructor = lt::posix_disk_io_constructor; disk_io_name_ = "posix"; break;
            default:
                disk_io_name_ = (SF_HAS_PREAD && LIBTORRENT_VERSION_NUM >= 20100) ? "predefinito (pread)" : "predefinito";
                break;
        }
        // Extensions are added explicitly so that PeX can be switched off (restart needed).
        params.extensions.clear();
        session_ = std::make_unique<lt::session>(std::move(params));
        session_->add_extension(&lt::create_ut_metadata_plugin);
        session_->add_extension(&lt::create_smart_ban_plugin);
        if (cfg.pex) session_->add_extension(&lt::create_ut_pex_plugin);
        resolve_metrics();
        started_ = std::chrono::steady_clock::now();
        session_->post_session_stats();
        return true;
    } catch (std::exception const& e) { error = e.what(); session_.reset(); return false; }
}

void Engine::stop(std::chrono::milliseconds wait) {
    if (!session_) return;
    try {
        session_->pause();
        outstanding_resume_ = 0;
        for (auto const& [id, h] : items_) {
            if (!h.is_valid()) continue;
            h.save_resume_data(lt::torrent_handle::save_info_dict | lt::torrent_handle::flush_disk_cache);
            ++outstanding_resume_;
        }
        auto deadline = std::chrono::steady_clock::now() + wait;
        while (outstanding_resume_ > 0 && std::chrono::steady_clock::now() < deadline) {
            session_->wait_for_alert(100ms);
            process_alerts();
        }
        if (!state_dir_.empty()) {
            auto buf = lt::write_session_params_buf(session_->session_state(lt::session_handle::save_dht_state),
                lt::session_handle::save_dht_state);
            write_file_atomic(state_dir_ / L"session.dat", buf);
        }
    } catch (...) {}
    items_.clear();
    session_.reset();
}

// ---------------------------------------------------------------- torrents
int Engine::register_handle(lt::torrent_handle const& h) {
    int id = next_id_++;
    items_[id] = h;
    return id;
}
lt::torrent_handle const* Engine::find(int id) const {
    auto it = items_.find(id);
    if (it == items_.end() || !it->second.is_valid()) return nullptr;
    return &it->second;
}
int Engine::find_id(lt::torrent_handle const& h) const {
    for (auto const& [id, handle] : items_) if (handle == h) return id;
    return 0;
}
fs::path Engine::resume_path(std::string const& hash) const { return state_dir_ / (hash + ".fastresume"); }

static std::string add_error(lt::error_code const& ec, AddResult* r) {
    if (ec == lt::errors::duplicate_torrent) { if (r) *r = AddResult::Duplicate; return "Torrent gia' presente nell'elenco."; }
    if (r) *r = AddResult::Error;
    return ec.message();
}
// FIX r10: without duplicate_is_error libtorrent 2.x silently returns the existing
// handle, and the same torrent was registered twice.
static void prepare(lt::add_torrent_params& p, bool paused, bool preallocate) {
    p.flags |= lt::torrent_flags::duplicate_is_error;
    if (preallocate) p.storage_mode = lt::storage_mode_allocate;
    if (paused) { p.flags |= lt::torrent_flags::paused; p.flags &= ~lt::torrent_flags::auto_managed; }
    else { p.flags |= lt::torrent_flags::auto_managed | lt::torrent_flags::paused; } // queue manager starts it
}

int Engine::add_magnet(std::string const& uri, std::string const& folder, bool paused, std::string& error, AddResult* result) {
    if (!session_) { error = "Motore non avviato"; return -1; }
    try {
        lt::error_code ec;
        auto p = lt::parse_magnet_uri(uri, ec);
        if (ec) { error = "Magnet non valido: " + ec.message(); if (result) *result = AddResult::Error; return -1; }
        p.save_path = folder;
        prepare(p, paused, preallocate_);
        auto h = session_->add_torrent(std::move(p), ec);
        if (ec) { error = add_error(ec, result); return -1; }
        if (result) *result = AddResult::Ok;
        h.save_resume_data(lt::torrent_handle::save_info_dict); // persist immediately
        ++outstanding_resume_;
        return register_handle(h);
    } catch (std::exception const& e) { error = e.what(); if (result) *result = AddResult::Error; return -1; }
}

int Engine::add_torrent_file(std::string const& file, std::string const& folder, bool paused, std::string& error, AddResult* result) {
    if (!session_) { error = "Motore non avviato"; return -1; }
    try {
        std::vector<char> buf;
        if (!read_file(u8path(file), buf)) { error = "File .torrent non leggibile"; if (result) *result = AddResult::Error; return -1; }
        lt::error_code ec;
        auto ti = std::make_shared<lt::torrent_info>(lt::span<char const>(buf.data(), static_cast<std::ptrdiff_t>(buf.size())), ec, lt::from_span);
        if (ec) { error = "File .torrent non valido: " + ec.message(); if (result) *result = AddResult::Error; return -1; }
        lt::add_torrent_params p;
        p.ti = ti;
        p.save_path = folder;
        prepare(p, paused, preallocate_);
        auto h = session_->add_torrent(std::move(p), ec);
        if (ec) { error = add_error(ec, result); return -1; }
        if (result) *result = AddResult::Ok;
        h.save_resume_data(lt::torrent_handle::save_info_dict);
        ++outstanding_resume_;
        return register_handle(h);
    } catch (std::exception const& e) { error = e.what(); if (result) *result = AddResult::Error; return -1; }
}

int Engine::add_resume_file(fs::path const& file, std::string& error, AddResult* result) {
    if (!session_) { error = "Motore non avviato"; return -1; }
    try {
        std::vector<char> buf;
        if (!read_file(file, buf)) { error = "Fast resume non leggibile"; if (result) *result = AddResult::Error; return -1; }
        lt::error_code ec;
        auto p = lt::read_resume_data(buf, ec);
        if (ec) { error = "Fast resume non valido: " + ec.message(); if (result) *result = AddResult::Error; return -1; }
        p.flags |= lt::torrent_flags::duplicate_is_error;
        auto h = session_->add_torrent(std::move(p), ec);
        if (ec) { error = add_error(ec, result); return -1; }
        if (result) *result = AddResult::Ok;
        return register_handle(h);
    } catch (std::exception const& e) { error = e.what(); if (result) *result = AddResult::Error; return -1; }
}

int Engine::load_saved_torrents(fs::path const& dir, std::vector<std::string>& log) {
    int n = 0;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return 0;
    for (auto const& entry : fs::directory_iterator(dir, ec)) {
        if (entry.path().extension() != L".fastresume") continue;
        std::string err;
        AddResult r = AddResult::Error;
        if (add_resume_file(entry.path(), err, &r) > 0) ++n;
        else if (r != AddResult::Duplicate) log.push_back("Ripristino non riuscito (" + entry.path().filename().u8string() + "): " + err);
    }
    return n;
}

std::vector<int> Engine::ids() const {
    std::vector<int> v;
    for (auto const& [id, h] : items_) v.push_back(id);
    return v;
}
std::string Engine::hash_of(int id) const {
    auto const* h = find(id);
    if (!h) return {};
    try { return hash_key(h->info_hashes()); } catch (...) { return {}; }
}

static void fill_view(int id, lt::torrent_status const& s, TorrentView& v) {
    v.id = id;
    v.name = s.name;
    v.hash = hash_key(s.info_hashes);
    v.error = s.errc ? s.errc.message() : std::string();
    v.save_path = s.save_path;
    v.state = static_cast<int>(s.state);
    v.progress = s.progress;
    v.total_wanted = s.total_wanted;
    v.total_wanted_done = s.total_wanted_done;
    v.total_done = s.total_done;
    v.all_time_download = s.all_time_download;
    v.all_time_upload = s.all_time_upload;
    v.download_rate = s.download_rate;
    v.upload_rate = s.upload_rate;
    v.seeds = s.num_seeds;
    v.peers = std::max(0, s.num_peers - s.num_seeds);
    v.list_seeds = s.num_complete >= 0 ? s.num_complete : s.list_seeds;
    v.list_peers = s.num_incomplete >= 0 ? s.num_incomplete : std::max(0, s.list_peers - s.list_seeds);
    v.connections = s.num_connections;
    v.queue_position = static_cast<int>(s.queue_position);
    v.seeding = s.is_seeding;
    v.finished = s.is_finished;
    v.paused = bool(s.flags & lt::torrent_flags::paused);
    v.auto_managed = bool(s.flags & lt::torrent_flags::auto_managed);
    v.super_seeding = bool(s.flags & lt::torrent_flags::super_seeding);
    v.has_error = bool(s.errc);
    v.has_metadata = s.has_metadata;
    v.moving = s.moving_storage;
    v.seeding_seconds = s.seeding_duration.count();
    v.active_seconds = s.active_duration.count();
    v.added = s.added_time;
    v.completed = s.completed_time;
    v.distributed_copies = s.distributed_copies;
    v.down_limit = std::max(0, s.download_limit);
    v.up_limit = std::max(0, s.upload_limit);
}
bool Engine::view(int id, TorrentView& out) const {
    auto const* h = find(id);
    if (!h) return false;
    try { fill_view(id, h->status(), out); return true; } catch (...) { return false; }
}
void Engine::views(std::vector<TorrentView>& out) const {
    out.clear();
    out.reserve(items_.size());
    for (auto const& [id, h] : items_) {
        if (!h.is_valid()) continue;
        try { TorrentView v; fill_view(id, h.status(), v); out.push_back(std::move(v)); } catch (...) {}
    }
}

// FIX: an auto-managed torrent would be resumed again by the queue manager,
// so a manual pause also clears auto_managed; resume restores queue management.
bool Engine::pause(int id, bool p) {
    auto const* h = find(id);
    if (!h) return false;
    try {
        if (p) { h->unset_flags(lt::torrent_flags::auto_managed); h->pause(); }
        else { h->set_flags(lt::torrent_flags::auto_managed); h->resume(); }
        return true;
    } catch (...) { return false; }
}
bool Engine::force_start(int id) {
    auto const* h = find(id);
    if (!h) return false;
    try { h->unset_flags(lt::torrent_flags::auto_managed); h->resume(); return true; } catch (...) { return false; }
}
bool Engine::remove(int id, bool delete_files) {
    auto it = items_.find(id);
    if (it == items_.end() || !session_) return false;
    try {
        std::string hash = it->second.is_valid() ? hash_key(it->second.info_hashes()) : std::string();
        if (delete_files) session_->remove_torrent(it->second, lt::session_handle::delete_files);
        else session_->remove_torrent(it->second);
        items_.erase(it);
        if (!hash.empty() && !state_dir_.empty()) { std::error_code ec; fs::remove(resume_path(hash), ec); }
        return true;
    } catch (...) { return false; }
}
bool Engine::recheck(int id) {
    auto const* h = find(id);
    if (!h) return false;
    try { h->force_recheck(); return true; } catch (...) { return false; }
}
bool Engine::reannounce(int id) {
    auto const* h = find(id);
    if (!h) return false;
    try {
        h->force_reannounce(0, -1, lt::torrent_handle::ignore_min_interval);
        if (dht_) h->force_dht_announce();
        return true;
    } catch (...) { return false; }
}
bool Engine::queue(int id, int action) {
    auto const* h = find(id);
    if (!h) return false;
    try {
        switch (action) {
            case 0: h->queue_position_up(); break;
            case 1: h->queue_position_down(); break;
            case 2: h->queue_position_top(); break;
            case 3: h->queue_position_bottom(); break;
            default: return false;
        }
        return true;
    } catch (...) { return false; }
}
bool Engine::super_seed(int id, bool on, std::string& error) {
    auto const* h = find(id);
    if (!h) return false;
    try {
        if (on) {
            if (!h->status(lt::status_flags_t{}).is_seeding) { error = "Il Super-seeding richiede un torrent completo in seeding."; return false; }
            h->set_flags(lt::torrent_flags::super_seeding);
        } else h->unset_flags(lt::torrent_flags::super_seeding);
        return true;
    } catch (std::exception const& e) { error = e.what(); return false; }
}
bool Engine::set_torrent_limits(int id, int down_kib, int up_kib) {
    auto const* h = find(id);
    if (!h || down_kib < 0 || up_kib < 0 || down_kib > 2000000 || up_kib > 2000000) return false;
    try { h->set_download_limit(down_kib * 1024); h->set_upload_limit(up_kib * 1024); return true; } catch (...) { return false; }
}
bool Engine::magnet_uri(int id, std::string& out) const {
    auto const* h = find(id);
    if (!h) return false;
    try { out = lt::make_magnet_uri(*h); return !out.empty(); } catch (...) { return false; }
}

// FIX r10: announce_entry::tier is a uint8_t; the old JSON writer streamed it as a
// character (tier 0 = NUL) which truncated the output and left the list empty.
// libtorrent 2.x keeps one announce endpoint per local listen address (every
// network adapter, IPv4 and IPv6) and per protocol (v1/v2). On Windows a PC
// typically has extra adapters (Hyper-V/WSL vEthernet, VPN, VirtualBox, IPv6
// without global connectivity) whose announces always time out; those endpoints
// then walk through the backup tiers, so EVERY tracker collected a "timed out"
// error and the working one looked like "updating" forever (bug of r10/r11).
// FIX r12: endpoints of local addresses that never got a successful reply from
// any tracker are ignored while at least one address works, and a tracker that
// the working addresses did not need is shown as a backup instead of an error.
std::vector<TrackerView> Engine::trackers(int id) const {
    std::vector<TrackerView> out;
    auto const* h = find(id);
    if (!h) return out;
    try {
        auto const now = lt::clock_type::now();
        auto const list = h->trackers();
        auto ok = [](lt::announce_infohash const& ih) { return ih.start_sent && ih.fails == 0 && !ih.last_error; };
        auto key = [](lt::announce_endpoint const& ep) { std::ostringstream s; s << ep.local_endpoint; return s.str(); };
        std::set<std::string> good_endpoints, all_endpoints;
        bool any_working_tracker = false;
        for (auto const& ae : list)
            for (auto const& ep : ae.endpoints) {
                all_endpoints.insert(key(ep));
                for (auto const& ih : ep.info_hashes) if (ok(ih)) { good_endpoints.insert(key(ep)); any_working_tracker = true; }
            }
        int ignored = static_cast<int>(all_endpoints.size() - (good_endpoints.empty() ? all_endpoints.size() : good_endpoints.size()));
        for (auto const& ae : list) {
            TrackerView t;
            t.url = ae.url;
            t.tier = static_cast<int>(ae.tier);
            bool any_enabled = ae.endpoints.empty(), working = false, updating = false, failed = false, untried = false;
            std::string ok_message, err_message;
            for (auto const& ep : ae.endpoints) {
                if (!good_endpoints.empty() && !good_endpoints.count(key(ep))) continue; // dead local address
                if (!ep.enabled) continue;
                any_enabled = true;
                for (auto const& ih : ep.info_hashes) {
                    bool used = ih.start_sent || ih.updating || ih.fails > 0 || ih.last_error;
                    if (!used) continue; // e.g. the v2 slot of a v1-only torrent
                    if (ok(ih)) { working = true; if (ok_message.empty()) ok_message = ih.message; }
                    else if (ih.updating) updating = true;
                    else if (ih.fails > 0 || ih.last_error) {
                        failed = true;
                        if (err_message.empty()) err_message = ih.last_error ? ih.last_error.message() : ih.message;
                    } else untried = true;
                    t.fails = std::max(t.fails, static_cast<int>(ih.fails));
                    t.seeds = std::max(t.seeds, ih.scrape_complete);
                    t.peers = std::max(t.peers, ih.scrape_incomplete);
                    t.downloaded = std::max(t.downloaded, ih.scrape_downloaded);
                    if (ih.next_announce > now) {
                        long long sec = std::chrono::duration_cast<std::chrono::seconds>(ih.next_announce - now).count();
                        if (t.next_announce < 0 || sec < t.next_announce) t.next_announce = sec;
                    }
                }
                bool touched = false;
                for (auto const& ih : ep.info_hashes) if (ih.start_sent || ih.updating || ih.fails > 0 || ih.last_error) touched = true;
                if (!touched) untried = true;
            }
            if (!any_enabled) t.status = 4;
            else if (working) t.status = 1;
            else if (updating) t.status = 2;
            else if (failed) t.status = 3;                      // a working address really got an error
            else t.status = any_working_tracker ? 5 : 0;        // backup tier not needed / not yet contacted
            (void)untried;
            t.message = working ? ok_message : err_message;
            t.ignored_endpoints = ignored;
            out.push_back(std::move(t));
        }
    } catch (...) {}
    return out;
}
static bool valid_tracker_url(std::string const& u) {
    auto starts = [&](char const* p) { return u.rfind(p, 0) == 0; };
    return u.size() > 8 && u.size() < 2048 && (starts("http://") || starts("https://") || starts("udp://") || starts("wss://"));
}
bool Engine::add_tracker(int id, std::string const& url, int tier, std::string& error) {
    auto const* h = find(id);
    if (!h) { error = "Torrent non trovato"; return false; }
    if (!valid_tracker_url(url)) { error = "URL non valido (http, https, udp, wss): " + url; return false; }
    try {
        for (auto const& ae : h->trackers()) if (ae.url == url) { error = "Gia' presente: " + url; return false; }
        lt::announce_entry ae(url);
        ae.tier = static_cast<std::uint8_t>(std::clamp(tier, 0, 255));
        h->add_tracker(ae);
        h->force_reannounce(0, -1, lt::torrent_handle::ignore_min_interval);
        return true;
    } catch (std::exception const& e) { error = e.what(); return false; }
}
bool Engine::remove_tracker(int id, std::string const& url) {
    auto const* h = find(id);
    if (!h) return false;
    try {
        auto list = h->trackers();
        auto before = list.size();
        list.erase(std::remove_if(list.begin(), list.end(), [&](lt::announce_entry const& a) { return a.url == url; }), list.end());
        if (list.size() == before) return false;
        h->replace_trackers(list);
        return true;
    } catch (...) { return false; }
}

std::vector<PeerView> Engine::peers(int id) const {
    std::vector<PeerView> out;
    auto const* h = find(id);
    if (!h) return out;
    try {
        std::vector<lt::peer_info> list;
        h->get_peer_info(list);
        for (auto const& p : list) {
            if (!(p.connection_type & lt::peer_info::standard_bittorrent)) continue;
            PeerView v;
#if LIBTORRENT_VERSION_NUM >= 20100
            auto const ep = p.remote_endpoint();
            v.incoming = !(p.flags & lt::peer_info::outgoing_connection);
#else
            auto const ep = p.ip;
            v.incoming = !(p.flags & lt::peer_info::local_connection);
#endif
            v.address = ep.address().to_string();
            v.port = ep.port();
            v.client = p.client;
            v.progress = p.progress;
            v.down = p.down_speed; v.up = p.up_speed;
            v.total_down = p.total_download; v.total_up = p.total_upload;
            v.encrypted = bool(p.flags & (lt::peer_info::rc4_encrypted | lt::peer_info::plaintext_encrypted));
            v.utp = bool(p.flags & lt::peer_info::utp_socket);
            v.seed = bool(p.flags & lt::peer_info::seed);
            out.push_back(std::move(v));
        }
    } catch (...) {}
    return out;
}
std::vector<FileView> Engine::files(int id) const {
    std::vector<FileView> out;
    auto const* h = find(id);
    if (!h) return out;
    try {
        auto ti = h->torrent_file();
        if (!ti) return out;
#if LIBTORRENT_VERSION_NUM >= 20100
        auto const& fs = ti->layout();
#else
        auto const& fs = ti->files();
#endif
        std::vector<std::int64_t> progress;
        h->file_progress(progress, lt::torrent_handle::piece_granularity);
        auto prio = h->get_file_priorities();
        for (int i = 0; i < fs.num_files(); ++i) {
            lt::file_index_t fi{i};
            if (fs.pad_file_at(fi)) continue;
            FileView f;
            f.index = i;
            f.path = fs.file_path(fi);
            f.size = fs.file_size(fi);
            f.done = i < static_cast<int>(progress.size()) ? progress[static_cast<std::size_t>(i)] : 0;
            f.priority = i < static_cast<int>(prio.size()) ? static_cast<int>(static_cast<std::uint8_t>(prio[static_cast<std::size_t>(i)])) : 4;
            out.push_back(std::move(f));
        }
    } catch (...) {}
    return out;
}
bool Engine::file_priority(int id, int index, int priority) {
    auto const* h = find(id);
    if (!h || index < 0 || priority < 0 || priority > 7) return false;
    try {
        h->file_priority(lt::file_index_t{index}, lt::download_priority_t{static_cast<std::uint8_t>(priority)});
        return true;
    } catch (...) { return false; }
}
bool Engine::details(int id, DetailsView& d) const {
    auto const* h = find(id);
    if (!h) return false;
    try {
        auto s = h->status(lt::torrent_handle::query_pieces);
        d.current_tracker = s.current_tracker;
        d.hash_v1 = s.info_hashes.has_v1() ? hex_of(s.info_hashes.v1) : "";
        d.hash_v2 = s.info_hashes.has_v2() ? hex_of(s.info_hashes.v2) : "";
        d.last_seen_complete = s.last_seen_complete;
        d.pieces_have = s.num_pieces;
        d.uploads = s.num_uploads;
        d.wasted = s.total_failed_bytes + s.total_redundant_bytes;
        d.next_announce = std::chrono::duration_cast<std::chrono::seconds>(s.next_announce).count();
        if (auto ti = h->torrent_file()) {
#if TORRENT_ABI_VERSION < 4
            d.comment = ti->comment();
            d.creator = ti->creator();
            d.creation_date = ti->creation_date();
#endif
            d.piece_length = ti->piece_length();
            d.num_pieces = ti->num_pieces();
            d.files = ti->num_files();
            d.is_private = ti->priv();
        }
        return true;
    } catch (...) { return false; }
}

// ---------------------------------------------------------------- settings
bool Engine::set_network(int port, bool dht, bool upnp, bool natpmp, bool lsd, bool ipv6, std::string& error) {
    if (port < 1024 || port > 65535) { error = "Porta fuori intervallo (1024-65535)"; return false; }
    try {
        lt::settings_pack p;
        p.set_str(lt::settings_pack::listen_interfaces, listen_string(port, ipv6));
        p.set_bool(lt::settings_pack::enable_dht, dht);
        p.set_bool(lt::settings_pack::enable_upnp, upnp);
        p.set_bool(lt::settings_pack::enable_natpmp, natpmp);
        p.set_bool(lt::settings_pack::enable_lsd, lsd);
        if (session_) {
            if (port != port_ || upnp != upnp_ || natpmp != natpmp_ || ipv6 != ipv6_) { listen_.clear(); portmaps_.clear(); }
            session_->apply_settings(p);
        }
        port_ = port; dht_ = dht; upnp_ = upnp; natpmp_ = natpmp; lsd_ = lsd; ipv6_ = ipv6;
        return true;
    } catch (std::exception const& e) { error = e.what(); return false; }
}
bool Engine::set_limits(int down_kib, int up_kib, int connections, int upload_slots) {
    if (!session_ || down_kib < 0 || up_kib < 0 || down_kib > 2000000 || up_kib > 2000000 ||
        connections < 10 || connections > 20000 || upload_slots == 0 || upload_slots < -1 || upload_slots > 2000) return false;
    lt::settings_pack p;
    p.set_int(lt::settings_pack::download_rate_limit, down_kib * 1024);
    p.set_int(lt::settings_pack::upload_rate_limit, up_kib * 1024);
    p.set_int(lt::settings_pack::connections_limit, connections);
    p.set_int(lt::settings_pack::unchoke_slots_limit, upload_slots);
    session_->apply_settings(p);
    return true;
}
bool Engine::set_queue(int downloads, int seeds) {
    if (!session_ || downloads < 1 || downloads > 1000 || seeds < 1 || seeds > 1000) return false;
    lt::settings_pack p;
    p.set_int(lt::settings_pack::active_downloads, downloads);
    p.set_int(lt::settings_pack::active_seeds, seeds);
    p.set_int(lt::settings_pack::active_limit, downloads + seeds);
    session_->apply_settings(p);
    return true;
}
// 0: plaintext allowed; 1: encryption preferred; 2: encryption required.
bool Engine::set_encryption(int mode) {
    if (!session_ || mode < 0 || mode > 2) return false;
    lt::settings_pack p;
    int policy = mode == 2 ? lt::settings_pack::pe_forced : lt::settings_pack::pe_enabled;
    p.set_int(lt::settings_pack::in_enc_policy, policy);
    p.set_int(lt::settings_pack::out_enc_policy, policy);
    p.set_int(lt::settings_pack::allowed_enc_level, mode == 2 ? lt::settings_pack::pe_rc4 : lt::settings_pack::pe_both);
    p.set_bool(lt::settings_pack::prefer_rc4, mode != 0);
    session_->apply_settings(p);
    return true;
}
bool Engine::set_user_agent(std::string const& ua) {
    if (!session_ || ua.empty() || ua.size() > 120) return false;
    lt::settings_pack p;
    p.set_str(lt::settings_pack::user_agent, ua);
    session_->apply_settings(p);
    return true;
}
bool Engine::set_identity(std::string const& peer_prefix, std::string const& client) {
    if (!session_) return false;
    std::string prefix = peer_prefix.empty() ? lt::generate_fingerprint("SF", SF_VERSION_MAJOR, SF_VERSION_MINOR, SF_VERSION_PATCH, 0) : peer_prefix;
    if (prefix.size() > 20) prefix.resize(20);
    lt::settings_pack p;
    p.set_str(lt::settings_pack::peer_fingerprint, prefix);
    p.set_str(lt::settings_pack::handshake_client_version, client);
    session_->apply_settings(p);
    return true;
}
static int setting_index(std::string const& name, int type_base) {
    int idx = lt::setting_by_name(name);
    if (idx < 0 || (idx & lt::settings_pack::type_mask) != type_base) return -1;
    return idx;
}
bool Engine::set_int(std::string const& name, int value, std::string& error) {
    int idx = setting_index(name, lt::settings_pack::int_type_base);
    if (idx < 0) { error = "Impostazione sconosciuta: " + name; return false; }
    if (!session_) return true;
    lt::settings_pack p;
    p.set_int(idx, value);
    session_->apply_settings(p);
    return true;
}
bool Engine::set_bool(std::string const& name, bool value, std::string& error) {
    int idx = setting_index(name, lt::settings_pack::bool_type_base);
    if (idx < 0) { error = "Impostazione sconosciuta: " + name; return false; }
    if (!session_) return true;
    lt::settings_pack p;
    p.set_bool(idx, value);
    session_->apply_settings(p);
    return true;
}
bool Engine::get_int(std::string const& name, bool current, int& value) const {
    int idx = setting_index(name, lt::settings_pack::int_type_base);
    if (idx < 0) return false;
    value = (current && session_) ? session_->get_settings().get_int(idx) : lt::default_settings().get_int(idx);
    return true;
}

// ---------------------------------------------------------------- alerts
void Engine::push_event(std::string s) {
    events_.push_back(std::move(s));
    while (events_.size() > 400) events_.pop_front();
}
void Engine::push_throttled(std::string const& key, std::string s) {
    auto now = std::chrono::steady_clock::now();
    auto it = throttle_.find(key);
    if (it != throttle_.end() && now - it->second < 60s) return;
    if (throttle_.size() > 200) throttle_.clear();
    throttle_[key] = now;
    push_event(std::move(s));
}
std::vector<std::string> Engine::take_events() {
    std::vector<std::string> v(events_.begin(), events_.end());
    events_.clear();
    return v;
}
void Engine::write_resume(lt::add_torrent_params const& p) {
    if (state_dir_.empty()) return;
    try {
        auto buf = lt::write_resume_data_buf(p);
        write_file_atomic(resume_path(hash_key(p.info_hashes)), buf);
    } catch (...) {}
}
static std::string transport_name(lt::portmap_transport t) { return t == lt::portmap_transport::upnp ? "UPnP" : "NAT-PMP"; }
static std::string protocol_name(lt::portmap_protocol p) {
    return p == lt::portmap_protocol::tcp ? "TCP" : p == lt::portmap_protocol::udp ? "UDP" : "-";
}
static std::string socket_name(lt::socket_type_t t) {
    switch (t) {
        case lt::socket_type_t::tcp: return "TCP";
        case lt::socket_type_t::utp: return "uTP";
        case lt::socket_type_t::tcp_ssl: return "TCP/SSL";
        case lt::socket_type_t::utp_ssl: return "uTP/SSL";
        default: return "altro";
    }
}
void Engine::process_alerts() {
    if (!session_) return;
    std::vector<lt::alert*> alerts;
    session_->pop_alerts(&alerts);
    for (auto* a : alerts) {
        try {
            if (auto* r = lt::alert_cast<lt::save_resume_data_alert>(a)) {
                write_resume(r->params);
                if (outstanding_resume_ > 0) --outstanding_resume_;
            } else if (auto* rf = lt::alert_cast<lt::save_resume_data_failed_alert>(a)) {
                if (outstanding_resume_ > 0) --outstanding_resume_;
                if (rf->error != lt::errors::resume_data_not_modified)
                    push_throttled("resume", "Salvataggio fast resume non riuscito: " + rf->error.message());
            } else if (auto* st = lt::alert_cast<lt::session_stats_alert>(a)) {
                auto v = st->counters();
                counters_.assign(v.begin(), v.end());
            } else if (auto* ls = lt::alert_cast<lt::listen_succeeded_alert>(a)) {
                ListenView l{ls->address.to_string(), socket_name(ls->socket_type), "", ls->port, true};
                listen_.erase(std::remove_if(listen_.begin(), listen_.end(), [&](ListenView const& x) {
                    return x.address == l.address && x.type == l.type; }), listen_.end());
                listen_.push_back(l);
                push_event("In ascolto su " + l.address + ":" + std::to_string(l.port) + " (" + l.type + ")");
            } else if (auto* lf = lt::alert_cast<lt::listen_failed_alert>(a)) {
                ListenView l{lf->address.to_string(), socket_name(lf->socket_type), lf->error.message(), lf->port, false};
                listen_.erase(std::remove_if(listen_.begin(), listen_.end(), [&](ListenView const& x) {
                    return x.address == l.address && x.type == l.type; }), listen_.end());
                listen_.push_back(l);
                push_event("Ascolto NON riuscito su " + l.address + ":" + std::to_string(l.port) + " (" + l.type + "): " + l.error);
            } else if (auto* pm = lt::alert_cast<lt::portmap_alert>(a)) {
                PortMapView m{transport_name(pm->map_transport), protocol_name(pm->map_protocol), "", pm->local_address.to_string(), pm->external_port, true};
                portmaps_[m.transport + "|" + m.protocol + "|" + m.local] = m;
                // a later success supersedes an earlier generic error of the same router/interface
                portmaps_.erase(m.transport + "|-|" + m.local);
                push_event(m.transport + ": porta esterna " + std::to_string(m.external_port) + " " + m.protocol + " mappata");
            } else if (auto* pe = lt::alert_cast<lt::portmap_error_alert>(a)) {
                PortMapView m{transport_name(pe->map_transport), "-", pe->error.message(), pe->local_address.to_string(), 0, false};
                bool had_success = false;
                for (auto const& [k, v] : portmaps_) if (v.ok && v.transport == m.transport && v.local == m.local) had_success = true;
                if (!had_success) portmaps_[m.transport + "|-|" + m.local] = m;
                push_throttled("pm" + m.transport, m.transport + " non riuscito: " + m.error);
            } else if (auto* ex = lt::alert_cast<lt::external_ip_alert>(a)) {
                std::string ip = ex->external_address.to_string();
                if (ip != external_ip_) push_event("IP esterno segnalato dai peer/tracker: " + ip);
                external_ip_ = ip;
            } else if (auto* perf = lt::alert_cast<lt::performance_alert>(a)) {
                push_throttled("perf" + std::to_string(static_cast<int>(perf->warning_code)), "Avviso prestazioni: " + perf->message());
            } else if (auto* fin = lt::alert_cast<lt::torrent_finished_alert>(a)) {
                // also posted after loading an already complete torrent: report real downloads only
                if (fin->handle.is_valid() && fin->handle.status(lt::status_flags_t{}).total_payload_download > 0)
                    push_event("Download completato: " + std::string(fin->torrent_name()));
                if (fin->handle.is_valid()) { fin->handle.save_resume_data(lt::torrent_handle::save_info_dict); ++outstanding_resume_; }
            } else if (auto* mr = lt::alert_cast<lt::metadata_received_alert>(a)) {
                if (mr->handle.is_valid()) { mr->handle.save_resume_data(lt::torrent_handle::save_info_dict); ++outstanding_resume_; }
            } else if (lt::alert_cast<lt::torrent_deleted_alert>(a)) {
                push_event("File eliminati dal disco: " + std::string(static_cast<lt::torrent_alert*>(a)->torrent_name()));
            } else if (auto* df = lt::alert_cast<lt::torrent_delete_failed_alert>(a)) {
                push_event("Eliminazione file NON riuscita: " + df->error.message());
            } else if (lt::alert_cast<lt::tracker_error_alert>(a) || lt::alert_cast<lt::scrape_failed_alert>(a)) {
                // visible in the Tracker tab
            } else if (a->category() & lt::alert_category::error) {
                push_throttled("err" + std::to_string(a->type()), "Errore: " + a->message());
            }
        } catch (...) {}
    }
}
void Engine::poll() {
    if (!session_) return;
    try { process_alerts(); session_->post_session_stats(); } catch (...) {}
}
void Engine::save_modified_resume() {
    for (auto const& [id, h] : items_) {
        try {
            if (h.is_valid() && h.need_save_resume_data()) {
                h.save_resume_data(lt::torrent_handle::save_info_dict | lt::torrent_handle::only_if_modified);
                ++outstanding_resume_;
            }
        } catch (...) {}
    }
}

void Engine::resolve_metrics() {
    metric_.clear();
    for (auto const& m : lt::session_stats_metrics()) metric_[m.name] = m.value_index;
}
std::int64_t Engine::counter(char const* name) const {
    auto it = metric_.find(name);
    if (it == metric_.end() || it->second < 0 || it->second >= static_cast<int>(counters_.size())) return 0;
    return counters_[static_cast<std::size_t>(it->second)];
}
NetView Engine::network() const {
    NetView n;
    n.port = port_; n.dht = dht_; n.upnp = upnp_; n.natpmp = natpmp_; n.lsd = lsd_; n.pex = pex_;
    if (!session_) return n;
    try { n.listening = session_->is_listening(); } catch (...) {}
    n.external_ip = external_ip_;
    n.uptime = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started_).count();
    n.dht_nodes = counter("dht.dht_nodes");
    n.peers_connected = counter("peer.num_peers_connected");
    n.incoming_connections = counter("peer.incoming_connections");
    n.has_incoming = counter("net.has_incoming_connections") > 0;
    n.listen = listen_;
    for (auto const& [k, v] : portmaps_) n.portmaps.push_back(v);
    return n;
}
CountersView Engine::counters() const {
    CountersView c;
    if (!session_) return c;
    c.uptime = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started_).count();
    c.recv_payload = counter("net.recv_payload_bytes");
    c.sent_payload = counter("net.sent_payload_bytes");
    c.recv_bytes = counter("net.recv_bytes");
    c.sent_bytes = counter("net.sent_bytes");
    c.recv_ip_overhead = counter("net.recv_ip_overhead_bytes");
    c.sent_ip_overhead = counter("net.sent_ip_overhead_bytes");
    c.recv_tracker = counter("net.recv_tracker_bytes");
    c.sent_tracker = counter("net.sent_tracker_bytes");
    c.recv_failed = counter("net.recv_failed_bytes");
    c.recv_redundant = counter("net.recv_redundant_bytes");
    c.dht_bytes_in = counter("dht.dht_bytes_in");
    c.dht_bytes_out = counter("dht.dht_bytes_out");
    c.dht_nodes = counter("dht.dht_nodes");
    c.peers_connected = counter("peer.num_peers_connected");
    c.peers_half_open = counter("peer.num_peers_half_open");
    c.tcp_peers = counter("peer.num_tcp_peers");
    c.utp_peers = counter("peer.num_utp_peers");
    c.incoming_connections = counter("peer.incoming_connections");
    c.has_incoming = counter("net.has_incoming_connections");
    c.banned_peers = counter("peer.num_banned_peers");
    c.disk_blocks_in_use = counter("disk.disk_blocks_in_use");
    c.cached_blocks = counter("disk.cached_blocks");
    c.queued_disk_jobs = counter("disk.queued_disk_jobs");
    c.piece_passed = counter("ses.num_piece_passed");
    c.piece_failed = counter("ses.num_piece_failed");
    return c;
}

} // namespace sf
