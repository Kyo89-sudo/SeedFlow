// Creates only a local trackerless torrent in a dedicated build-directory fixture.
// DHT, UPnP, NAT-PMP, LSD, PeX and outgoing peer transports are disabled.
#include "../src/engine.hpp"
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/bencode.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <stdexcept>

static void check(bool condition, char const* label) {
    if (!condition) throw std::runtime_error(label);
    std::cout << "OK " << label << '\n';
}
template<typename Predicate> static bool wait_for(sf::Engine& engine, Predicate predicate) {
    for (int i = 0; i < 100; ++i) {
        engine.poll();
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "fixture argument");
        auto folder = std::filesystem::u8path(argv[1]);
        std::filesystem::create_directories(folder);
        std::string const name = "Seme_\xc3\xa8.txt";
        { std::ofstream data(folder / std::filesystem::u8path(name), std::ios::binary); data << std::string(16384, 's'); }
        // create_file_entry list: the file_storage constructor is deprecated in libtorrent 2.1.
        std::vector<libtorrent::create_file_entry> files;
        files.emplace_back(name, 16384);
        libtorrent::create_torrent torrent(std::move(files), 16384, libtorrent::create_torrent::v1_only);
        libtorrent::error_code ec;
        libtorrent::set_piece_hashes(torrent, folder.u8string(), ec);
        check(!ec, "hash local completed fixture");
        std::vector<char> metadata;
        libtorrent::bencode(std::back_inserter(metadata), torrent.generate());
        auto torrent_path = folder / "fixture.torrent";
        { std::ofstream file(torrent_path, std::ios::binary); file.write(metadata.data(), static_cast<std::streamsize>(metadata.size())); }
        sf::Engine engine;
        sf::EngineConfig cfg;
        cfg.port = 0;
        cfg.dht = cfg.upnp = cfg.natpmp = cfg.lsd = cfg.pex = false;
        std::string error;
        check(engine.start(cfg, error), "start isolated engine");
        for (auto setting : {"enable_outgoing_tcp", "enable_outgoing_utp", "enable_incoming_tcp", "enable_incoming_utp"})
            check(engine.set_bool(setting, false, error), "disable peer transport");
        int incomplete = engine.add_magnet("magnet:?xt=urn:btih:0123456789012345678901234567890123456789", folder.u8string(), true, error);
        check(incomplete > 0 && !engine.super_seed(incomplete, true, error), "reject superseed on incomplete torrent");
        int id = engine.add_torrent_file(torrent_path.u8string(), folder.u8string(), false, error);
        check(id > 0, "add local complete torrent");
        sf::TorrentView view;
        check(wait_for(engine, [&] { return engine.view(id, view) && view.seeding; }), "torrent reaches seeding after hash check");
        check(engine.super_seed(id, true, error), "enable superseed request");
        check(wait_for(engine, [&] { return engine.view(id, view) && view.super_seeding; }), "superseed flag enabled in libtorrent");
        check(engine.super_seed(id, false, error), "disable superseed request");
        check(wait_for(engine, [&] { return engine.view(id, view) && !view.super_seeding; }), "superseed flag disabled in libtorrent");
        engine.stop(std::chrono::seconds(1));
        std::cout << "PASSED superseed flag test; no external peer transfer tested\n";
        return 0;
    } catch (std::exception const& ex) {
        std::cerr << "FAILED " << ex.what() << '\n';
        return 1;
    }
}
