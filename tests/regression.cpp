// Offline tests exercise the production implementation without opening its GUI,
// starting a torrent session, touching user settings or changing the clipboard.
#include "../src/mainwindow.cpp"
#include "../src/settings.cpp"
#include "../src/creator.hpp"
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <numeric>
#include <future>
#include <cmath>
#include <fstream>
#include <filesystem>

static int checks = 0;
struct FileFixture {
    std::filesystem::path folder;
    explicit FileFixture(std::filesystem::path const& base) : folder(std::filesystem::absolute(base) /
        (L"SeedFlow-regression-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()))) {
        if (!std::filesystem::create_directory(folder)) throw std::runtime_error("Fixture directory already exists");
    }
    ~FileFixture() { std::error_code ignored; std::filesystem::remove_all(folder, ignored); }
};
static void check(bool condition, char const* name) {
    if (!condition) throw std::runtime_error(name);
    ++checks;
    std::cout << "OK " << name << '\n';
}
static std::wstring resource_text(HMODULE module, int id) {
    HRSRC info = FindResourceW(module, MAKEINTRESOURCEW(id), RT_DIALOG);
    if (!info) throw std::runtime_error("Dialog resource missing");
    HGLOBAL handle = LoadResource(module, info);
    auto* data = static_cast<wchar_t const*>(LockResource(handle));
    return {data, SizeofResource(module, info) / sizeof(wchar_t)};
}
int wmain(int argc, wchar_t** argv) {
    HMODULE module = nullptr;
    try {
        check(argc == 2 || argc == 3, "executable argument");
        {
            FileFixture fixture(std::filesystem::path(argv[1]).parent_path());
            auto path = fixture.folder / L"settings.dat";
            std::vector<char> first{'o', 'l', 'd'}, second{'n', 'e', 'w'}, read{'x'};
            std::string error = "previous error";
            bool const first_saved = sf::file_io::write_atomic(path, first, error, true);
            if (!first_saved) std::cerr << "File I/O: " << error << "\n";
            check(first_saved && error.empty(), "first atomic write succeeds and clears old error");
            check(sf::file_io::read_bounded(path, read, 3) && read == first, "bounded file read accepts exact limit");
            check(!sf::file_io::read_bounded(path, read, 2) && read == first, "oversized file rejected before replacing caller buffer");
            check(sf::file_io::write_atomic(path, second, error, true), "atomic replacement succeeds on existing Windows file");
            check(sf::file_io::read_bounded(path.wstring() + L".bak", read, 3) && read == first, "backup preserves previous complete settings");
            check(sf::file_io::read_bounded(path, read, 3) && read == second, "replacement contains the new complete data");
            std::filesystem::remove(path.wstring() + L".bak");
            std::filesystem::create_directory(path.wstring() + L".bak");
            check(!sf::file_io::write_atomic(path, first, error, true) && !error.empty(), "backup failure is reported instead of ignored");
            check(sf::file_io::read_bounded(path, read, 3) && read == second, "backup failure leaves existing settings unchanged");
            check(!std::filesystem::exists(path.wstring() + L".tmp"), "failed replacement removes its temporary file");
            auto empty = fixture.folder / L"empty.dat";
            check(sf::file_io::write_atomic(empty, {}, error) && sf::file_io::read_bounded(empty, read, 0) && read.empty(), "empty file write and read remain supported");
        }
        std::wstring text = L"Identità · Velocità · Giù · è à ì ò ù · 中文 · \U0001f642";
        check(sf::widen(sf::narrow(text)) == text, "UTF-8 / UTF-16 accents, CJK and supplementary character");
        check(sf::json_unescape("Velocit\\u00e0") == sf::narrow(L"Velocità"), "legacy JSON accent");
        check(sf::json_unescape("\\ud83d\\ude42") == sf::narrow(L"\U0001f642"), "legacy JSON surrogate pair");
        check(sf::json_unescape("\\ud83d") == sf::narrow(L"\ufffd"), "legacy JSON unpaired surrogate");
        check(sf::json_unescape("\\uZZZZ").find(sf::narrow(L"\ufffd")) == 0, "legacy JSON invalid hex without exception");
        check(sf::fmt_int((std::numeric_limits<std::int64_t>::min)()) == L"-9.223.372.036.854.775.808", "INT64_MIN formatting");
        long long integer = 17;
        check(!sf::parse_int(L"9223372036854775808", integer) && integer == 17, "integer overflow rejected without output mutation");
        check(sf::parse_int(L"-9.223.372.036.854.775.808", integer) && integer == (std::numeric_limits<long long>::min)(), "minimum integer parsing");
        double decimal = 17;
        check(sf::parse_double(L"1,25", decimal) && decimal == 1.25, "Italian decimal parsing");
        check(!sf::parse_double(L"1e9999", decimal) && decimal == 1.25, "decimal overflow rejected");
        sf::AppSettings settings;
        libtorrent::entry e(libtorrent::entry::dictionary_t);
        e["user_agent"] = "SeedFlow/0.8.0";
        e["download_dir"] = sf::narrow(text);
        std::vector<char> encoded;
        libtorrent::bencode(std::back_inserter(encoded), e);
        check(sf::parse_settings(encoded, settings) && settings.user_agent == SF_USER_AGENT &&
            sf::widen(settings.download_dir) == text, "settings UTF-8 and default version migration");
        e["user_agent"] = "Custom/1.0";
        encoded.clear(); libtorrent::bencode(std::back_inserter(encoded), e);
        check(sf::parse_settings(encoded, settings) && settings.user_agent == "Custom/1.0", "custom user agent preserved");
        e["user_agent"] = "qBittorrent/5.1.4"; e["peer_id_prefix"] = "-qB5140-"; e["client_name"] = "qBittorrent/5.1.4";
        encoded.clear(); libtorrent::bencode(std::back_inserter(encoded), e);
        check(sf::parse_settings(encoded, settings) && settings.user_agent == "qBittorrent/5.3.0" && settings.peer_id_prefix == "-qB5300-",
              "previous client preset follows the updated one");
        e["user_agent"] = "Custom/1.0"; e["peer_id_prefix"] = ""; e["client_name"] = "";
        {
            sf::g.settings.recent_dirs.clear();
            for (int i = 0; i < 10; ++i) sf::remember_dir("C:\\Dati\\" + std::to_string(i));
            sf::remember_dir("c:\\dati\\3");
            check(sf::g.settings.recent_dirs.size() == 8 && sf::g.settings.recent_dirs.front() == "c:\\dati\\3" &&
                  std::count_if(sf::g.settings.recent_dirs.begin(), sf::g.settings.recent_dirs.end(),
                      [](std::string const& d) { return _stricmp(d.c_str(), "C:\\Dati\\3") == 0; }) == 1,
                  "recent save folders: newest first, no duplicates, at most 8");
            libtorrent::entry dirs(libtorrent::entry::dictionary_t);
            dirs["ask_save_path"] = 0;
            dirs["recent_dirs"] = libtorrent::entry::list_type{libtorrent::entry("D:\\Uno"), libtorrent::entry("D:\\Due")};
            std::vector<char> buf; libtorrent::bencode(std::back_inserter(buf), dirs);
            sf::AppSettings loaded;
            check(sf::parse_settings(buf, loaded) && !loaded.ask_save_path && loaded.recent_dirs.size() == 2 && loaded.recent_dirs[1] == "D:\\Due",
                  "save-path question and recent folders persisted");
            sf::g.settings.recent_dirs.clear();
        }

        // Corrupt persisted integers must not wrap into apparently valid settings.
        e["port"] = std::int64_t{4294979641LL}; // low 32 bits equal a different valid port
        e["up_kib"] = (std::numeric_limits<std::int64_t>::max)();
        e["int_settings"]["send_buffer_watermark"] = (std::numeric_limits<std::int64_t>::max)();
        e["bool_settings"]["enable_dht"] = std::int64_t{4294967296LL};
        settings.stats.uploaded = 123;
        encoded.clear(); libtorrent::bencode(std::back_inserter(encoded), e);
        check(sf::parse_settings(encoded, settings) && settings.port == sf::AppSettings{}.port && settings.up_kib == 0,
            "oversized saved integers fall back without narrowing wraparound");
        check(!settings.int_settings.count("send_buffer_watermark"), "oversized advanced integer setting ignored");
        check(settings.bool_settings.at("enable_dht") == 1, "large nonzero persisted boolean remains true");
        check(settings.stats.uploaded == 0, "missing statistics reset on repeated settings parse");
        e["port"] = 80; e["encryption"] = 7; e["connections"] = -5; e["upload_slots"] = 0; e["global_ratio"] = -1000;
        e["torrents"]["00ff"]["ratio_mode"] = 9; e["torrents"]["00ff"]["ratio_action"] = 12; e["torrents"]["00ff"]["ratio_limit"] = 0;
        encoded.clear(); libtorrent::bencode(std::back_inserter(encoded), e);
        {
            sf::AppSettings const def;
            check(sf::parse_settings(encoded, settings) && settings.port == def.port && settings.encryption == def.encryption &&
                settings.connections == def.connections && settings.upload_slots == def.upload_slots && settings.global_ratio == 0,
                "out-of-range saved values restored to defaults");
            auto const& meta = settings.torrents.at("00ff");
            check(meta.ratio_mode == sf::RatioGlobal && meta.ratio_action == -1 && meta.ratio_limit == 2.0, "out-of-range torrent ratio values restored");
        }
        {
            std::string error;
            auto tiers = sf::parse_tracker_tiers("udp://a.example:1/announce\r\nhttp://b.example/announce\r\n\r\nhttps://c.example/announce", error);
            check(error.empty() && tiers.size() == 2 && tiers[0].size() == 2 && tiers[1].size() == 1, "create torrent: tracker tiers");
            check(sf::parse_tracker_tiers("magnet:?xt=x", error).empty() && !error.empty(), "create torrent: invalid tracker rejected");
            check(sf::parse_web_seeds("udp://x", error).empty() && !error.empty(), "create torrent: web seed must be http(s)");
            check(sf::piece_size_choices().size() == 11 && sf::piece_size_choices().front() == 16384, "create torrent: piece sizes");
            std::filesystem::path dir = std::filesystem::temp_directory_path() / L"SeedFlowRegressionCreate";
            std::error_code ec;
            std::filesystem::remove_all(dir, ec);
            std::filesystem::create_directories(dir / L"Dati àè", ec);
            { std::ofstream(dir / L"Dati àè" / L"uno.bin", std::ios::binary) << std::string(300000, 'x'); }
            for (int format = 0; format < 3; ++format) {
                sf::CreateRequest request;
                request.source = sf::narrow((dir / L"Dati àè").wstring());
                request.tiers = tiers;
                request.format = static_cast<sf::TorrentFormat>(format);
                auto result = sf::create_torrent_file(request);
                // v2-only + a folder with a single file: libtorrent names the torrent after the file
                // and the data is seeded from inside the folder.
                bool const v2_single = format == 2;
                check(result.ok && !result.torrent.empty() &&
                      result.name == (v2_single ? std::string("uno.bin") : sf::narrow(L"Dati àè")) &&
                      std::filesystem::path(sf::widen(result.seed_dir)).lexically_normal() == (v2_single ? dir / L"Dati àè" : dir).lexically_normal() &&
                      result.magnet.rfind("magnet:?", 0) == 0, "create torrent: hybrid / v1 / v2 from a Unicode folder");
            }
            std::filesystem::remove_all(dir, ec);
        }
        sf::NetView network;
        network.port = 51413; network.listening = true; network.uptime = 300; network.peers_connected = 1;
        sf::PortCheck verified; verified.done = verified.success = verified.reachable = true;
        verified.port = network.port; verified.when = std::time(nullptr);
        std::wstring net_text, net_tip;
        check(sf::evaluate_network(network, verified, net_text, net_tip) == sf::NetState::Open,
            "recent port check applies to the tested listening port");
        verified.port = 51414;
        check(sf::evaluate_network(network, verified, net_text, net_tip) != sf::NetState::Open,
            "port check from previous port cannot mark a new port open");
        verified.port = network.port; verified.when = std::time(nullptr) + 3600;
        check(sf::evaluate_network(network, verified, net_text, net_tip) != sf::NetState::Open,
            "future-dated port check cannot remain valid after clock rollback");
        verified.when = std::time(nullptr) - 1801;
        check(sf::evaluate_network(network, verified, net_text, net_tip) != sf::NetState::Open,
            "expired port check cannot mark port open");
        auto invalid_port = sf::check_port_online(65536);
        check(invalid_port.done && !invalid_port.success && invalid_port.port == 65536 && !invalid_port.status.empty(),
            "invalid diagnostic port rejected before network access");

        for (UINT dpi : {96u, 144u, 192u}) for (int width : {760, 1024, 1920}) {
            auto px = [dpi](int v) { return MulDiv(v, static_cast<int>(dpi), 96); };
            std::vector<int> const need{px(110), px(90), px(230), px(330), px(360), px(100)};
            auto edges = sf::status_part_edges(MulDiv(width, static_cast<int>(dpi), 96), need, 0, 4);
            int const client = MulDiv(width, static_cast<int>(dpi), 96);
            check(edges.size() == 6 && edges[0] >= 0 && edges[4] <= client && edges[5] == -1 &&
                std::is_sorted(edges.begin(), edges.begin() + 5),
                "status panes stay ordered and within resized client area at multiple DPI");
            int total = 0; for (int v : need) total += v;
            if (client >= total)
                for (int i = 1; i < 5; ++i) check(edges[i] - edges[i - 1] >= need[i], "status pane wide enough for its text when there is room");
            // only some panes shown (network hidden): the last part takes the spare room
            std::vector<int> const some{px(230), px(330), px(100)};
            auto e3 = sf::status_part_edges(client, some, 2, -1);
            check(e3.size() == 3 && e3[0] == std::min(client, px(230)) && e3[1] <= client && e3[2] == -1,
                "status bar with hidden panes keeps the visible ones at their text width");
        }
        check(sf::status_part_edges(800, {}, 0, -1).empty() && sf::status_part_edges(800, {120}, 0, -1) == std::vector<int>{-1},
            "status bar edges for no parts or a single part");
        {
            libtorrent::entry pane_settings(libtorrent::entry::dictionary_t);
            pane_settings["status_panes"] = 0x24;
            std::vector<char> buf; libtorrent::bencode(std::back_inserter(buf), pane_settings);
            sf::AppSettings loaded;
            check(sf::parse_settings(buf, loaded) && loaded.status_panes == 0x24, "visible status bar panes persisted");
            pane_settings["status_panes"] = 0x1FF;
            buf.clear(); libtorrent::bencode(std::back_inserter(buf), pane_settings);
            check(sf::parse_settings(buf, loaded) && loaded.status_panes == 0x3F, "unknown status bar pane bits dropped");
        }
        {
            bool in_range = true, avoided = true;
            for (int i = 0; i < 200; ++i) {
                int const p = sf::random_listen_port(51413);
                in_range = in_range && p >= 10000 && p <= 65000;
                avoided = avoided && p != 51413;
            }
            check(in_range && avoided && sf::random_listen_port(65000) != 65000, "random listen port in 10000-65000 and different from the current one");
            libtorrent::entry e(libtorrent::entry::dictionary_t);
            e["random_port_start"] = 1;
            e["recent_dirs"] = libtorrent::entry::list_type{libtorrent::entry(std::string(1500, 'x')), libtorrent::entry("D:\\Ok")};
            std::vector<char> buf; libtorrent::bencode(std::back_inserter(buf), e);
            sf::AppSettings loaded;
            check(sf::parse_settings(buf, loaded) && loaded.random_port_start && loaded.recent_dirs.size() == 1 && loaded.recent_dirs[0] == "D:\\Ok",
                  "random port option persisted, oversized recent folders dropped");
        }

        std::vector<int> order(sf::ColCount);
        std::iota(order.begin(), order.end(), 0);
        check(sf::valid_column_order(order), "valid persisted column order accepted");
        order[1] = order[0];
        check(!sf::valid_column_order(order), "duplicate persisted columns rejected");
        order[1] = -1;
        check(!sf::valid_column_order(order), "negative persisted column rejected");
        order[1] = sf::ColCount;
        check(!sf::valid_column_order(order), "out-of-range persisted column rejected");
        order.pop_back();
        check(!sf::valid_column_order(order), "incomplete persisted column order rejected");
        sf::PortCheckJob mailbox;
        sf::PortCheck port_result;
        check(!mailbox.take(port_result), "pending diagnostics result is not consumed");
        sf::PortCheck completed; completed.done = true; completed.success = true; completed.ip = "127.0.0.1";
        mailbox.complete(std::move(completed));
        check(mailbox.take(port_result) && port_result.done && port_result.success && port_result.ip == "127.0.0.1",
            "diagnostics result delivered through owned mailbox");
        check(!mailbox.take(port_result), "diagnostics result consumed only once");
        auto owner = std::make_shared<sf::PortCheckJob>();
        std::weak_ptr<sf::PortCheckJob> weak = owner;
        std::promise<void> proceed;
        auto signal = proceed.get_future();
        std::thread worker([job = owner, gate = std::move(signal)]() mutable {
            gate.wait(); sf::PortCheck result; result.done = true; job->complete(std::move(result));
        });
        owner.reset();
        bool const held_by_worker = !weak.expired();
        proceed.set_value(); worker.join();
        check(held_by_worker && weak.expired(), "closing diagnostics owner releases mailbox after worker completion");
        check(sf::icons::make_icon(sf::icons::IcMagnet, 0) == nullptr, "zero-size icon rejected");
        check(sf::icons::make_icon(sf::icons::IcMagnet, -2147483647) == nullptr, "negative-size icon rejected before allocation");
        check(sf::icons::make_menu_bitmap(sf::icons::IcMagnet, 32768) == nullptr, "oversized icon rejected before allocation");
        check(sf::icons::make_imagelist(nullptr, 1, 16) == nullptr, "null icon array rejected");

        sf::SpeedHistory history;
        check(history.size() == 0, "speed history starts empty");
        history.append(1000, -1, -2);
        check(history.at(0).down == 0 && history.at(0).up == 0, "negative speed samples clamped");
        history.append(1000, 100, 50);
        check(history.size() == 1 && history.at(0).down == 100, "duplicate timestamp replaces last sample");
        history.append(999, 900, 900);
        check(history.size() == 1, "out-of-order speed sample ignored");
        for (int i = 2; i < 1000; ++i) history.append(static_cast<std::uint64_t>(i) * 1000, i, i / 2);
        check(history.size() == sf::SpeedHistory::capacity && history.at(0).time == 699000 &&
            history.at(history.size() - 1).time == 999000, "speed history wraps chronologically at fixed capacity");
        sf::SpeedHistory fixture;
        for (int i = 0; i <= 300; ++i) {
            double const wave = 0.6 + 0.25 * std::sin(i * 0.045) + 0.1 * std::sin(i * 0.16);
            fixture.append(static_cast<std::uint64_t>(i) * 1000,
                i < 18 ? 0 : static_cast<std::int64_t>(wave * 8 * 1024 * 1024),
                static_cast<std::int64_t>((0.4 + 0.2 * std::sin(i * 0.03)) * 1024 * 1024));
        }
        BITMAPINFO bi{}; bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = 900; bi.bmiHeader.biHeight = -280;
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
        void* graph_bits = nullptr;
        HDC graph_dc = CreateCompatibleDC(nullptr);
        HBITMAP graph_bitmap = CreateDIBSection(graph_dc, &bi, DIB_RGB_COLORS, &graph_bits, nullptr, 0);
        check(graph_dc && graph_bitmap && graph_bits, "create native graph drawing surface");
        HGDIOBJ graph_old = SelectObject(graph_dc, graph_bitmap);
        HFONT graph_font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        // Warm GDI font realization before measuring repeated redraws.
        sf::draw_speed_graph(graph_dc, {0, 0, 900, 280}, fixture, 300000, graph_font, 96);
        GdiFlush();
        DWORD graph_handles = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        for (int i = 0; i < 40; ++i)
            sf::draw_speed_graph(graph_dc, {0, 0, 900, 280}, fixture, 300000, graph_font, 96);
        GdiFlush();
        std::cout << "Graph GDI handles: " << graph_handles << " -> " << GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) << '\n';
        check(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= graph_handles,
            "native speed graph redraws without GDI handle growth");
        if (argc == 3) {
            GdiFlush();
            BITMAPFILEHEADER file_header{}; file_header.bfType = 0x4d42;
            file_header.bfOffBits = sizeof(file_header) + sizeof(BITMAPINFOHEADER);
            file_header.bfSize = file_header.bfOffBits + 900 * 280 * 4;
            std::ofstream image(std::filesystem::path(argv[2]), std::ios::binary);
            image.write(reinterpret_cast<char*>(&file_header), sizeof(file_header));
            image.write(reinterpret_cast<char*>(&bi.bmiHeader), sizeof(bi.bmiHeader));
            image.write(static_cast<char*>(graph_bits), 900 * 280 * 4);
            check(static_cast<bool>(image), "export native graph rendering for visual inspection");
        }
        sf::draw_speed_graph(graph_dc, {0, 0, 320, 140}, sf::SpeedHistory{}, 0, graph_font, 144);
        sf::draw_speed_graph(graph_dc, {0, 0, 8, 8}, fixture, 300000, graph_font, 192);
        SelectObject(graph_dc, graph_old); DeleteObject(graph_font); DeleteObject(graph_bitmap); DeleteDC(graph_dc);

        module = LoadLibraryExW(argv[1], nullptr, LOAD_LIBRARY_AS_DATAFILE);
        check(module != nullptr, "load actual executable resources without execution");
        HMENU menu = LoadMenuW(module, MAKEINTRESOURCEW(IDR_MAINMENU));
        check(menu != nullptr, "Unicode menu resource");
        wchar_t title[256]{};
        GetMenuStringW(menu, ID_OPT_PREFS, title, 256, MF_BYCOMMAND);
        check(std::wstring(title) == L"Preferenze...\tCtrl+P", "unified preferences menu item");
        DestroyMenu(menu);
        for (int id : {IDD_PREFS, IDD_PG_GENERAL, IDD_PG_FOLDERS, IDD_PG_CONNECTION, IDD_PG_BANDWIDTH, IDD_PG_BITTORRENT,
                       IDD_PG_QUEUE, IDD_PG_IDENTITY, IDD_PG_SYSTEM, IDD_PG_PERFORMANCE})
            check(!resource_text(module, id).empty(), "preferences page template present");
        HWND preferences = CreateDialogParamW(module, MAKEINTRESOURCEW(IDD_PREFS), nullptr,
            [](HWND, UINT, WPARAM, LPARAM) -> INT_PTR { return FALSE; }, 0);
        check(preferences != nullptr, "create preferences frame for layout verification");
        HWND tree = GetDlgItem(preferences, IDC_PREFTREE);
        RECT tree_rect{}; GetClientRect(tree, &tree_rect);
        HDC tree_dc = GetDC(tree);
        auto tree_font = reinterpret_cast<HFONT>(SendMessageW(tree, WM_GETFONT, 0, 0));
        auto previous_font = SelectObject(tree_dc, tree_font);
        SIZE extent{}; GetTextExtentPoint32W(tree_dc, L"Prestazioni e memoria", 21, &extent);
        SelectObject(tree_dc, previous_font); ReleaseDC(tree, tree_dc);
        check(tree_rect.right >= extent.cx + 2 * static_cast<int>(TreeView_GetIndent(tree)) + 24,
            "performance and memory title fits tree including indentation and icon");
        DestroyWindow(preferences);
        check(resource_text(module, IDD_PG_SYSTEM).find(L"Priorità del processo") != std::wstring::npos, "advanced page accent intact");
        for (int size : sf::icons::kStripSizes) {
            HRSRC r = FindResourceW(module, MAKEINTRESOURCEW(IDR_ICONS_BASE + size), MAKEINTRESOURCEW(10));
            check(r != nullptr && SizeofResource(module, r) > 1000, "icon strip resource embedded");
            auto const* png = static_cast<unsigned char const*>(LockResource(LoadResource(module, r)));
            auto be32 = [&](int o) { return (png[o] << 24) | (png[o + 1] << 16) | (png[o + 2] << 8) | png[o + 3]; };
            check(png[1] == 'P' && png[2] == 'N' && png[3] == 'G' && be32(16) == size * sf::icons::IcCount && be32(20) == size,
                  "icon strip PNG has one cell per icon");
        }
        {
            sf::TorrentView v; v.has_metadata = true; v.distributed_copies = 2.37f;
            check(sf::paint::health(v).level == 4, "health: 2.37 copies = good");
            v.distributed_copies = 0.4f;
            check(sf::paint::health(v).level == 1, "health: no full copy = critical");
            v.seeding = true; v.progress = 1; v.distributed_copies = -1; v.list_seeds = 1;
            check(sf::paint::health(v).level == 3 && sf::paint::health(v).copies == 1.0, "health: only our copy, scrape counts us once");
            v.has_metadata = false;
            check(sf::paint::health(v).level == 0, "health: unknown without metadata");
        }
        auto user_agent = resource_text(module, IDD_PG_IDENTITY);
        check(user_agent.find(L"Modalità anonima") != std::wstring::npos, "compiled dialog accent intact");
        check(user_agent.find(L"Campi vuoti = identità SeedFlow.") != std::wstring::npos, "long dialog label not truncated");
        check(resource_text(module, IDD_LIMITS).find(L"Limiti di velocità del torrent") != std::wstring::npos, "compiled dialog title accent intact");
        check(sf::about_version_line().find(L"Versione " SF_VERSION_WSTRING L" ") != std::wstring::npos, "About shows the current version");
        {
            HRSRC manifest = FindResourceW(module, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(24));
            check(manifest != nullptr, "embedded manifest present");
            std::string xml(static_cast<char const*>(LockResource(LoadResource(module, manifest))), SizeofResource(module, manifest));
            check(xml.find(std::string("name=\"SeedFlow\" version=\"") + SF_FILE_VERSION_STRING + "\"") != std::string::npos,
                  "manifest version matches FileVersion");
        }

        // Warm the font cache before counting live GDI handles.
        DWORD const handles_before = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        for (int iteration = 0; iteration < 25; ++iteration) {
            std::size_t before = sf::menu_bitmaps.size();
            HMENU popup = LoadMenuW(module, MAKEINTRESOURCEW(IDR_CTX_TORRENT));
            sf::decorate_menu(GetSubMenu(popup, 0));
            DestroyMenu(popup);
            sf::release_menu_bitmaps_from(before);
            check(sf::menu_bitmaps.size() == before, "context-menu bitmaps released");
        }
        check(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= handles_before + 4, "GDI handles do not grow across repeated menus");
        FreeLibrary(module); module = nullptr;
        std::cout << "PASSED " << checks << " checks\n";
        return 0;
    } catch (std::exception const& ex) {
        if (module) FreeLibrary(module);
        std::cerr << "FAILED: " << ex.what() << '\n';
        return 1;
    }
}

