#include "creator.hpp"
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/error_code.hpp>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <exception>
#include <filesystem>

namespace lt = libtorrent;

namespace sf {
namespace {

struct Cancelled {};   // thrown from the hashing callback: libtorrent unwinds and aborts its disk jobs

std::string trim(std::string s) {
    auto const not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}
bool starts_with_ci(std::string const& s, char const* prefix) {
    std::size_t n = std::char_traits<char>::length(prefix);
    if (s.size() < n) return false;
    for (std::size_t i = 0; i < n; ++i)
        if (std::tolower(static_cast<unsigned char>(s[i])) != prefix[i]) return false;
    return true;
}
bool printable_ascii(std::string const& s) {
    return std::all_of(s.begin(), s.end(), [](unsigned char c) { return c > 32 && c < 127; });
}
std::vector<std::string> lines_of(std::string const& text) {
    std::vector<std::string> out;
    std::string line;
    for (char c : text) {
        if (c == '\n') { out.push_back(trim(line)); line.clear(); }
        else if (c != '\r') line += c;
    }
    out.push_back(trim(line));
    return out;
}

} // namespace

std::vector<std::vector<std::string>> parse_tracker_tiers(std::string const& text, std::string& error) {
    error.clear();
    std::vector<std::vector<std::string>> tiers(1);
    for (auto const& url : lines_of(text)) {
        if (url.empty()) { if (!tiers.back().empty()) tiers.emplace_back(); continue; }
        if (!(starts_with_ci(url, "http://") || starts_with_ci(url, "https://") || starts_with_ci(url, "udp://") ||
              starts_with_ci(url, "wss://")) || !printable_ascii(url) || url.size() > 2048) {
            error = "Tracker non valido: " + url.substr(0, 120);
            return {};
        }
        bool dup = false;
        for (auto const& t : tiers) dup = dup || std::find(t.begin(), t.end(), url) != t.end();
        if (!dup) tiers.back().push_back(url);
    }
    if (tiers.back().empty()) tiers.pop_back();
    return tiers;
}

std::vector<std::string> parse_web_seeds(std::string const& text, std::string& error) {
    error.clear();
    std::vector<std::string> out;
    for (auto const& url : lines_of(text)) {
        if (url.empty()) continue;
        if (!(starts_with_ci(url, "http://") || starts_with_ci(url, "https://")) || !printable_ascii(url) || url.size() > 2048) {
            error = "Web seed non valido (serve un URL http/https): " + url.substr(0, 120);
            return {};
        }
        if (std::find(out.begin(), out.end(), url) == out.end()) out.push_back(url);
    }
    return out;
}

std::vector<int> piece_size_choices() {
    std::vector<int> v;
    for (int s = 16 * 1024; s <= 16 * 1024 * 1024; s *= 2) v.push_back(s);
    return v;
}

CreateResult create_torrent_file(CreateRequest const& req, std::atomic<bool> const* cancel,
                                 std::atomic<int>* done, std::atomic<int>* total) {
    CreateResult r;
    try {
        namespace fs = std::filesystem;
        fs::path const src = fs::u8path(req.source);
        std::error_code ec;
        if (req.source.empty() || !fs::exists(src, ec)) { r.error = "La sorgente non esiste."; return r; }
        if (req.piece_size != 0) {
            auto const sizes = piece_size_choices();
            if (std::find(sizes.begin(), sizes.end(), req.piece_size) == sizes.end()) { r.error = "Dimensione dei pezzi non valida."; return r; }
        }
        lt::create_flags_t flags{};
        if (req.format == TorrentFormat::V1) flags |= lt::create_torrent::v1_only;
        else if (req.format == TorrentFormat::V2) flags |= lt::create_torrent::v2_only;

        // libtorrent works with UTF-8 paths; the torrent root is the source's own name.
        fs::path const absolute = fs::absolute(src, ec);
        std::string const source_u8 = (ec ? src : absolute).lexically_normal().u8string();
        std::string trimmed = source_u8;
        while (trimmed.size() > 1 && (trimmed.back() == '/' || trimmed.back() == '\\')) trimmed.pop_back();
        std::string const parent = fs::u8path(trimmed).parent_path().u8string();

        auto files = lt::list_files(trimmed, [](std::string const&) { return true; }, flags);
        std::int64_t size = 0;
        for (auto const& f : files) size += f.size;
        if (files.empty() || size == 0) { r.error = "La sorgente è vuota: non ci sono dati da condividere."; return r; }
        r.num_files = static_cast<int>(files.size());

        lt::create_torrent t(std::move(files), req.piece_size, flags);
        int tier = 0;
        for (auto const& group : req.tiers) {
            for (auto const& url : group) t.add_tracker(url, tier);
            ++tier;
        }
        for (auto const& url : req.web_seeds) t.add_url_seed(url);
        if (!req.comment.empty()) t.set_comment(req.comment.c_str());
        if (!req.creator.empty()) t.set_creator(req.creator.c_str());
        t.set_creation_date(std::time(nullptr));
        t.set_priv(req.is_private);

        r.num_pieces = t.num_pieces();
        r.piece_size = t.piece_length();
        r.total_size = t.total_size();
        if (total) *total = t.num_pieces();

        lt::error_code hash_ec;
        lt::set_piece_hashes(t, parent, [&](lt::piece_index_t) {
            if (done) ++*done;
            if (cancel && *cancel) throw Cancelled{};
        }, hash_ec);
        if (hash_ec) { r.error = "Lettura dei file non riuscita: " + hash_ec.message(); return r; }
        if (cancel && *cancel) { r.cancelled = true; return r; }

        r.torrent = t.generate_buf();
        // Read it back exactly as a client would, to validate what was produced.
        lt::add_torrent_params const atp = lt::load_torrent_buffer(
            lt::span<char const>(r.torrent.data(), static_cast<std::ptrdiff_t>(r.torrent.size())));
        if (!atp.ti) { r.error = "Torrent generato non valido."; r.torrent.clear(); return r; }
        r.name = atp.ti->name();
        r.magnet = lt::make_magnet_uri(atp);
        r.ok = true;
    } catch (Cancelled const&) {
        r = CreateResult{};
        r.cancelled = true;
    } catch (std::exception const& ex) {
        r = CreateResult{};
        r.error = ex.what();
    }
    return r;
}

TorrentCreator::~TorrentCreator() { cancel(); wait(); }

bool TorrentCreator::start(CreateRequest request) {
    if (running_) return false;
    wait();
    cancel_ = false; finished_ = false; done_ = 0; total_ = 0;
    { std::lock_guard<std::mutex> lock(mutex_); result_ = CreateResult{}; }
    running_ = true;
    try {
        worker_ = std::thread(&TorrentCreator::run, this, std::move(request));
    } catch (...) { running_ = false; return false; }
    return true;
}

void TorrentCreator::run(CreateRequest request) {
    CreateResult r = create_torrent_file(request, &cancel_, &done_, &total_);
    { std::lock_guard<std::mutex> lock(mutex_); result_ = std::move(r); }
    finished_ = true;
    running_ = false;
}

CreateResult TorrentCreator::take_result() {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::move(result_);
}

void TorrentCreator::wait() { if (worker_.joinable()) worker_.join(); }

} // namespace sf
