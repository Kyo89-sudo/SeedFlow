// SeedFlow - presets derived from libtorrent's min_memory_usage() and
// high_performance_seed() (src/session.cpp, libtorrent 2.1.2) and the libtorrent
// tuning guide. Names/units are libtorrent settings_pack names/units.
#pragma once
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <thread>

namespace sf::tuning {

constexpr int KiB = 1024, MiB = 1024 * 1024;

struct Field {
    char const* name; wchar_t const* label; wchar_t const* unit; int scale, min, max; wchar_t const* help;
};
inline std::vector<Field> const& memory_fields() {
    static std::vector<Field> const f = {
        {"max_queued_disk_bytes", L"Cache di scrittura su disco", L"MiB", MiB, 1, 2048,
         L"Dati ricevuti in attesa di scrittura. In libtorrent 2.1 (I/O pread) è la cache principale; predefinito 100 MiB. Troppo bassa limita i download veloci."},
        {"aio_threads", L"Thread I/O disco", L"", 1, 1, 64, L"Predefinito 10. HDD 2-4, SSD 4-8, NVMe 8-16."},
        {"hashing_threads", L"Thread verifica hash", L"", 1, 1, 32, L"Verifica completa dei file. HDD 1, SSD/NVMe 2-4."},
        {"checking_mem_usage", L"Memoria per la verifica", L"MiB", 64, 1, 512, L"Lettura anticipata durante la verifica. Predefinito 4 MiB."},
        {"send_buffer_watermark", L"Buffer di invio max per peer", L"KiB", KiB, 9, 65536,
         L"Dati letti in anticipo per ogni peer in upload: alto = upload più veloce ma più RAM. Predefinito 500 KiB."},
        {"send_buffer_watermark_factor", L"Fattore buffer di invio", L"%", 1, 1, 1000, L"Percentuale della velocità del peer. Predefinito 50%."},
        {"send_buffer_low_watermark", L"Buffer di invio minimo", L"KiB", KiB, 1, 16384, L"Soglia iniziale: aiuta a raggiungere subito velocità alte. Predefinito 10 KiB."},
        {"file_pool_size", L"File aperti contemporaneamente", L"", 1, 4, 5000, L"Predefinito 40."},
        {"max_peerlist_size", L"Peer conosciuti per torrent", L"", 1, 100, 100000, L"Predefinito 3000."},
        {"max_paused_peerlist_size", L"Peer conosciuti (in pausa)", L"", 1, 0, 100000, L"Predefinito 1000."},
    };
    return f;
}

inline wchar_t const* const kProfileNames[3] = {L"Risparmio RAM", L"Bilanciato", L"Prestazioni"};

inline std::map<std::string, int> memory_profile(int p) {
    if (p == 0) return {{"max_queued_disk_bytes", 16 * MiB}, {"aio_threads", 2}, {"hashing_threads", 1},
        {"checking_mem_usage", 64}, {"send_buffer_watermark", 256 * KiB}, {"send_buffer_watermark_factor", 50},
        {"send_buffer_low_watermark", 10 * KiB}, {"file_pool_size", 20}, {"max_peerlist_size", 1000}, {"max_paused_peerlist_size", 100}};
    if (p == 2) return {{"max_queued_disk_bytes", 64 * MiB}, {"aio_threads", 8}, {"hashing_threads", 2},
        {"checking_mem_usage", 1024}, {"send_buffer_watermark", 3 * MiB}, {"send_buffer_watermark_factor", 150},
        {"send_buffer_low_watermark", 1 * MiB}, {"file_pool_size", 500}, {"max_peerlist_size", 4000}, {"max_paused_peerlist_size", 1000}};
    return {{"max_queued_disk_bytes", 32 * MiB}, {"aio_threads", 4}, {"hashing_threads", 1},
        {"checking_mem_usage", 256}, {"send_buffer_watermark", 1 * MiB}, {"send_buffer_watermark_factor", 100},
        {"send_buffer_low_watermark", 64 * KiB}, {"file_pool_size", 100}, {"max_peerlist_size", 2000}, {"max_paused_peerlist_size", 300}};
}

struct ConnectionPreset { wchar_t const* name; double down, up; };
inline std::vector<ConnectionPreset> const& connections() {
    static std::vector<ConnectionPreset> const c = {
        {L"ADSL (7 Mb / 0,5 Mb)", 7, 0.5},
        {L"ADSL2+ (20 Mb / 1 Mb)", 20, 1},
        {L"FTTC / VDSL (100 Mb / 20 Mb)", 100, 20},
        {L"FTTC / VDSL (200 Mb / 20 Mb)", 200, 20},
        {L"FWA 4G/5G (300 Mb / 50 Mb)", 300, 50},
        {L"FTTH (1 Gb / 100 Mb)", 1000, 100},
        {L"FTTH (1 Gb / 300 Mb)", 1000, 300},
        {L"FTTH simmetrica (1 Gb / 1 Gb)", 1000, 1000},
        {L"FTTH (2,5 Gb / 1 Gb)", 2500, 1000},
        {L"FTTH (10 Gb / 10 Gb)", 10000, 10000},
        {L"Personalizzata", 100, 20},
    };
    return c;
}
inline wchar_t const* const kDiskTypes[3] = {L"Disco meccanico (HDD)", L"SSD SATA", L"SSD NVMe"};

struct Explain { std::wstring setting, value, why; };
struct WizardResult {
    int down_kib = 0, up_kib = 0, connections = 200, upload_slots = 8, active_downloads = 3, active_seeds = 5;
    std::map<std::string, int> ints;
    std::vector<Explain> explain;
};

inline int mbps_to_kib(double mbps) { return static_cast<int>(std::lround(mbps * 1000000.0 / 8.0 / 1024.0)); }


inline WizardResult compute(double down_mbps, double up_mbps, int disk, int ram, bool limit_upload) {
    WizardResult r;
    double up = std::max(0.1, up_mbps), down = std::max(0.1, down_mbps);
    double ram_scale = ram == 0 ? 0.6 : ram == 2 ? 1.3 : 1.0;
    auto num = [](double v) { wchar_t b[32]; swprintf(b, 32, L"%g", v); return std::wstring(b); };

    // A saturated uplink also chokes the TCP ACKs of downloads and other devices:
    // 80-85% on slow/asymmetric lines, 90% from 200 Mb/s upwards.
    double pct = up < 20 ? 0.80 : up < 200 ? 0.85 : 0.90;
    r.up_kib = limit_upload ? std::max(10, static_cast<int>(mbps_to_kib(up) * pct)) : 0;
    r.down_kib = 0;
    r.explain.push_back({L"Limite upload", r.up_kib == 0 ? L"illimitato" :
        std::to_wstring(r.up_kib) + L" KiB/s (" + std::to_wstring(static_cast<int>(pct * 100)) + L"% di " + num(up) + L" Mb/s)",
        L"Evita di saturare l'uplink: latenza e download degli altri dispositivi restano buoni."});
    r.explain.push_back({L"Limite download", L"illimitato", L"Si autoregola; limitalo solo se serve banda ad altri."});

    int conn = up < 10 ? 150 : up < 50 ? 300 : up < 200 ? 500 : up < 600 ? 800 : 1200;
    r.connections = std::clamp(static_cast<int>(conn * ram_scale), 50, 5000);
    r.explain.push_back({L"Connessioni massime", std::to_wstring(r.connections), L"Ogni connessione usa buffer: crescono con la banda."});

    r.upload_slots = up < 5 ? 4 : up < 20 ? 8 : up < 100 ? 16 : up < 500 ? 32 : 64;
    bool rate_based = up >= 50;
    r.ints["choking_algorithm"] = rate_based ? 2 : 0;
    r.explain.push_back({L"Algoritmo unchoke", rate_based ? std::wstring(L"automatico (rate_based)") : L"slot fissi (" + std::to_wstring(r.upload_slots) + L")",
        rate_based ? L"Apre nuovi slot finché la banda in upload cresce." : L"Pochi slot ben serviti rendono di più su linee lente."});

    bool fast = up >= 100;
    r.ints["seed_choking_algorithm"] = fast ? 1 : 0;   // fastest_upload : round_robin
    r.ints["mixed_mode_algorithm"] = fast ? 0 : 1;     // prefer_tcp : peer_proportional
    r.explain.push_back({L"Seeding", fast ? L"fastest_upload" : L"round_robin",
        fast ? L"Privilegia i peer che ricevono più velocemente: satura meglio la fibra." : L"Distribuisce l'upload a rotazione."});
    r.explain.push_back({L"TCP / uTP", fast ? L"prefer_tcp" : L"proporzionale",
        fast ? L"Con banda abbondante il controllo di congestione uTP rallenterebbe il TCP." : L"uTP cede banda quando la linea è congestionata."});
    r.ints["connection_speed"] = up >= 500 ? 100 : up >= 100 ? 60 : 30;
    r.ints["listen_queue_size"] = up >= 500 ? 100 : up >= 100 ? 50 : 20;

    for (auto const& [k, v] : memory_profile(ram)) r.ints[k] = v;
    unsigned cores = std::max(1u, std::thread::hardware_concurrency());
    if (disk == 0) { r.ints["aio_threads"] = std::min(r.ints["aio_threads"], 4); r.ints["hashing_threads"] = 1; }
    else if (disk == 1) r.ints["aio_threads"] = std::max(r.ints["aio_threads"], 4);
    else {
        r.ints["aio_threads"] = std::max(r.ints["aio_threads"], ram == 0 ? 4 : 8);
        r.ints["hashing_threads"] = std::max(r.ints["hashing_threads"], ram == 0 ? 1 : std::min(4, std::max(2, static_cast<int>(cores / 4))));
    }
    if (up >= 500) {
        r.ints["send_buffer_watermark"] = std::max(r.ints["send_buffer_watermark"], ram == 2 ? 5 * MiB : ram == 0 ? 1 * MiB : 3 * MiB);
        r.ints["send_buffer_watermark_factor"] = std::max(r.ints["send_buffer_watermark_factor"], 150);
        r.ints["send_buffer_low_watermark"] = std::max(r.ints["send_buffer_low_watermark"], 512 * KiB);
        r.ints["max_allowed_in_request_queue"] = 2000;
    } else if (up >= 100) {
        r.ints["send_buffer_watermark"] = std::max(r.ints["send_buffer_watermark"], 1 * MiB);
        r.ints["send_buffer_watermark_factor"] = std::max(r.ints["send_buffer_watermark_factor"], 100);
    }
    if (down >= 500) {
        r.ints["max_queued_disk_bytes"] = std::max(r.ints["max_queued_disk_bytes"], ram == 0 ? 32 * MiB : 64 * MiB);
        r.ints["max_out_request_queue"] = 1500;
    } else if (down >= 100) {
        r.ints["max_queued_disk_bytes"] = std::max(r.ints["max_queued_disk_bytes"], ram == 0 ? 16 * MiB : 32 * MiB);
    }
    r.explain.push_back({L"Buffer invio per peer",
        std::to_wstring(r.ints["send_buffer_watermark"] / KiB) + L" KiB × " + std::to_wstring(r.ints["send_buffer_watermark_factor"]) + L"%",
        L"Evita che il socket resti a secco mentre il disco legge: con upload alto va alzato."});
    r.explain.push_back({L"Cache di scrittura", std::to_wstring(r.ints["max_queued_disk_bytes"] / MiB) + L" MiB",
        L"Assorbe i picchi di download. Predefinito libtorrent: 100 MiB."});
    r.explain.push_back({L"Thread disco / hash", std::to_wstring(r.ints["aio_threads"]) + L" / " + std::to_wstring(r.ints["hashing_threads"]),
        disk == 0 ? L"Un HDD peggiora con molte letture parallele." : L"Gli SSD reggono bene operazioni parallele."});
    r.active_downloads = down >= 500 ? 6 : down >= 100 ? 4 : 3;
    r.active_seeds = up >= 500 ? 20 : up >= 100 ? 12 : up >= 20 ? 8 : 4;
    r.explain.push_back({L"Torrent attivi", std::to_wstring(r.active_downloads) + L" download, " + std::to_wstring(r.active_seeds) + L" seed",
        L"Con più banda si servono più torrent senza disperderla."});
    return r;
}

} // namespace sf::tuning
