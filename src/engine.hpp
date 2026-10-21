// SeedFlow - BitTorrent engine (thin layer over libtorrent-rasterbar 2.x).
// Used only from the GUI thread: no locking, no C ABI, no JSON.
#pragma once
#include "version.h"
#include <libtorrent/session.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <memory>
#include <chrono>
#include <ctime>
#include <filesystem>

namespace sf {

struct TorrentView {
    int id = 0;
    std::string name, hash, error, save_path;
    int state = 0;                 // lt::torrent_status::state_t
    float progress = 0;
    std::int64_t total_wanted = 0, total_wanted_done = 0, total_done = 0;
    std::int64_t all_time_download = 0, all_time_upload = 0;
    int download_rate = 0, upload_rate = 0;
    int seeds = 0, peers = 0, list_seeds = 0, list_peers = 0, connections = 0;
    int queue_position = -1;
    bool seeding = false, finished = false, paused = false, auto_managed = false;
    bool super_seeding = false, has_error = false, has_metadata = false, moving = false;
    std::int64_t seeding_seconds = 0, active_seconds = 0;
    std::time_t added = 0, completed = 0;
    float distributed_copies = 0;
    int down_limit = 0, up_limit = 0;   // bytes/s, 0 = unlimited
};

struct TrackerView {
    std::string url, message;
    int tier = 0, status = 0;      // 0 not contacted, 1 working, 2 updating, 3 error, 4 disabled,
                                   // 5 backup (not needed: another tracker works)
    int ignored_endpoints = 0;     // local addresses ignored because no tracker answers on them
    int seeds = -1, peers = -1, downloaded = -1, fails = 0;
    long long next_announce = -1;  // seconds
};
struct PeerView {
    std::string address, client;
    int port = 0;
    float progress = 0;
    int down = 0, up = 0;
    std::int64_t total_down = 0, total_up = 0;
    bool incoming = false, encrypted = false, utp = false, seed = false;
};
struct FileView { int index = 0; std::string path; std::int64_t size = 0, done = 0; int priority = 4; };
struct DetailsView {
    std::string comment, creator, current_tracker, hash_v1, hash_v2;
    std::time_t creation_date = 0, last_seen_complete = 0;
    int piece_length = 0, num_pieces = 0, pieces_have = 0, files = 0, uploads = 0;
    bool is_private = false;
    std::int64_t wasted = 0, next_announce = 0;
};
struct ListenView { std::string address, type, error; int port = 0; bool ok = false; };
struct PortMapView { std::string transport, protocol, error, local; int external_port = 0; bool ok = false; };
struct NetView {
    int port = 0; bool listening = false;
    bool dht = false, upnp = false, natpmp = false, lsd = false, pex = false;
    std::string external_ip;
    std::int64_t uptime = 0, dht_nodes = 0, peers_connected = 0, incoming_connections = 0;
    bool has_incoming = false;
    std::vector<ListenView> listen;
    std::vector<PortMapView> portmaps;
};
struct CountersView {
    std::int64_t uptime = 0;
    std::int64_t recv_payload = 0, sent_payload = 0, recv_bytes = 0, sent_bytes = 0;
    std::int64_t recv_ip_overhead = 0, sent_ip_overhead = 0, recv_tracker = 0, sent_tracker = 0;
    std::int64_t recv_failed = 0, recv_redundant = 0, dht_bytes_in = 0, dht_bytes_out = 0;
    std::int64_t dht_nodes = 0, peers_connected = 0, peers_half_open = 0, tcp_peers = 0, utp_peers = 0;
    std::int64_t incoming_connections = 0, has_incoming = 0, banned_peers = 0;
    std::int64_t disk_blocks_in_use = 0, cached_blocks = 0, queued_disk_jobs = 0;
    std::int64_t piece_passed = 0, piece_failed = 0;
};

struct EngineConfig {
    int disk_io = 0;               // 0 default, 1 pread, 2 mmap, 3 posix
    int port = 51413;
    bool dht = true, upnp = true, natpmp = true, lsd = true, pex = true, ipv6 = true;
    std::string user_agent = SF_USER_AGENT;
    std::filesystem::path state_dir;   // resume files and DHT state
};

enum class AddResult { Ok, Duplicate, Error };

class Engine {
public:
    Engine();
    ~Engine();
    bool start(EngineConfig const& cfg, std::string& error);
    // Saves resume data for every torrent (up to `wait`) and the DHT state, then stops.
    void stop(std::chrono::milliseconds wait);
    bool running() const { return session_ != nullptr; }

    // Adding torrents. folder is UTF-8. Returns new id (>0) or -1.
    int add_magnet(std::string const& uri, std::string const& folder, bool paused, std::string& error, AddResult* result = nullptr);
    int add_torrent_file(std::string const& file_utf8, std::string const& folder, bool paused, std::string& error, AddResult* result = nullptr);
    int add_resume_file(std::filesystem::path const& file, std::string& error, AddResult* result = nullptr);
    int load_saved_torrents(std::filesystem::path const& dir, std::vector<std::string>& log);

    std::vector<int> ids() const;
    bool view(int id, TorrentView& out) const;
    void views(std::vector<TorrentView>& out) const;
    std::string hash_of(int id) const;

    bool pause(int id, bool pause);
    bool force_start(int id);
    bool remove(int id, bool delete_files);
    bool recheck(int id);
    bool reannounce(int id);
    bool queue(int id, int action); // 0 up, 1 down, 2 top, 3 bottom
    bool super_seed(int id, bool on, std::string& error);
    bool set_torrent_limits(int id, int down_kib, int up_kib);
    bool magnet_uri(int id, std::string& out) const;

    std::vector<TrackerView> trackers(int id) const;
    bool add_tracker(int id, std::string const& url, int tier, std::string& error);
    bool remove_tracker(int id, std::string const& url);
    std::vector<PeerView> peers(int id) const;
    std::vector<FileView> files(int id) const;
    bool file_priority(int id, int index, int priority);
    bool details(int id, DetailsView& out) const;

    // Session-wide settings.
    bool set_network(int port, bool dht, bool upnp, bool natpmp, bool lsd, bool ipv6, std::string& error);
    bool set_limits(int down_kib, int up_kib, int connections, int upload_slots);
    bool set_queue(int downloads, int seeds);
    bool set_encryption(int mode);
    void set_preallocate(bool on) { preallocate_ = on; }
    bool set_user_agent(std::string const& ua);
    // peer_prefix: peer ID prefix (empty = SeedFlow), client: handshake name (empty = none sent)
    bool set_identity(std::string const& peer_prefix, std::string const& client);
    bool set_int(std::string const& name, int value, std::string& error);
    bool set_bool(std::string const& name, bool value, std::string& error);
    bool get_int(std::string const& name, bool current, int& value) const;

    // Call from a timer: drains alerts, writes resume files, requests counters.
    void poll();
    // Periodic resume save for torrents whose state changed.
    void save_modified_resume();
    std::vector<std::string> take_events();
    NetView network() const;
    CountersView counters() const;
    std::string disk_io_name() const { return disk_io_name_; }
    static std::string libtorrent_version();
    int abi_version() const;

private:
    int register_handle(libtorrent::torrent_handle const& h);
    libtorrent::torrent_handle const* find(int id) const;
    int find_id(libtorrent::torrent_handle const& h) const;
    void push_event(std::string s);
    void push_throttled(std::string const& key, std::string s);
    void write_resume(libtorrent::add_torrent_params const& p);
    std::filesystem::path resume_path(std::string const& hash) const;
    void process_alerts();
    void resolve_metrics();
    std::int64_t counter(char const* name) const;

    std::unique_ptr<libtorrent::session> session_;
    std::map<int, libtorrent::torrent_handle> items_;
    int next_id_ = 1;
    bool preallocate_ = false;
    int outstanding_resume_ = 0;
    int port_ = 51413;
    bool dht_ = true, upnp_ = true, natpmp_ = true, lsd_ = true, pex_ = true, ipv6_ = true;
    std::string disk_io_name_ = "predefinito";
    std::string external_ip_;
    std::vector<ListenView> listen_;
    std::map<std::string, PortMapView> portmaps_;
    std::vector<std::int64_t> counters_;
    std::map<std::string, int> metric_;
    std::deque<std::string> events_;
    std::map<std::string, std::chrono::steady_clock::time_point> throttle_;
    std::chrono::steady_clock::time_point started_;
    std::filesystem::path state_dir_;
};

} // namespace sf

