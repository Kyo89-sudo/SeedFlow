// SeedFlow - .torrent creation (v1, v2 or hybrid) on a worker thread.
// Portable C++ (no Win32): the dialog in dialogs.cpp drives it and polls progress.
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sf {

enum class TorrentFormat { Hybrid = 0, V1 = 1, V2 = 2 };

struct CreateRequest {
    std::string source;                          // UTF-8 path of a file or folder
    std::vector<std::vector<std::string>> tiers; // tracker URLs grouped by tier
    std::vector<std::string> web_seeds;
    std::string comment, creator;
    bool is_private = false;
    TorrentFormat format = TorrentFormat::Hybrid;
    int piece_size = 0;                          // bytes; 0 = automatic
};

struct CreateResult {
    bool ok = false, cancelled = false;
    std::string error;
    std::vector<char> torrent;                   // bencoded .torrent
    std::string name, magnet;
    std::int64_t total_size = 0;
    int num_files = 0, num_pieces = 0, piece_size = 0;
};

// Splits the tracker box text: one URL per line, an empty line starts a new tier.
std::vector<std::vector<std::string>> parse_tracker_tiers(std::string const& text, std::string& error);
std::vector<std::string> parse_web_seeds(std::string const& text, std::string& error);
// Piece sizes offered in the dialog (bytes, after "automatic").
std::vector<int> piece_size_choices();

class TorrentCreator {
public:
    ~TorrentCreator();
    bool start(CreateRequest request);           // false if already running
    void cancel() { cancel_ = true; }
    bool running() const { return running_; }
    bool finished() const { return finished_; }
    int pieces_done() const { return done_; }
    int pieces_total() const { return total_; }
    CreateResult take_result();                  // valid once finished()
    void wait();
private:
    void run(CreateRequest request);
    std::thread worker_;
    std::atomic<bool> cancel_{false}, running_{false}, finished_{false};
    std::atomic<int> done_{0}, total_{0};
    std::mutex mutex_;
    CreateResult result_;
};

// Synchronous helper used by the worker and by tests.
CreateResult create_torrent_file(CreateRequest const& request, std::atomic<bool> const* cancel = nullptr,
                                 std::atomic<int>* done = nullptr, std::atomic<int>* total = nullptr);

} // namespace sf
