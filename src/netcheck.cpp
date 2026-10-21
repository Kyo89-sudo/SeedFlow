#include "version.h"
#include "netcheck.hpp"
#include "util.hpp"
#include "win.hpp"
#include <winhttp.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>
#include <shellapi.h>
#include <regex>
#include <memory>

namespace sf {

wchar_t const* const kFirewallRule = L"SeedFlow (BitTorrent)";

NetState evaluate_network(NetView const& n, PortCheck const& last, std::wstring& text, std::wstring& tip) {
    bool any_ok = n.listen.empty() ? n.listening : false;
    for (auto const& l : n.listen) if (l.ok) any_ok = true;
    auto const now = std::time(nullptr);
    bool ext_valid = last.done && last.success && last.port == n.port &&
        last.when > 0 && now >= last.when && now - last.when < 30 * 60;
    std::wstring port = std::to_wstring(n.port);
    if (!n.listening || !any_ok) {
        text = L"Porta non in ascolto";
        tip = L"SeedFlow non riesce ad ascoltare sulla porta " + port + L": probabilmente è usata da un altro programma. Cambiala in Preferenze > Connessione.";
        return NetState::NotListening;
    }
    if (n.has_incoming || (ext_valid && last.reachable)) {
        text = L"Rete aperta";
        tip = L"Porta " + port + L" raggiungibile da Internet" + (n.has_incoming ? L" (connessioni in ingresso ricevute)." : L" (verifica online superata).");
        return NetState::Open;
    }
    if (ext_valid && !last.reachable) {
        text = L"Firewall / NAT";
        tip = L"La verifica online indica che la porta " + port + L" non è raggiungibile: inoltrala sul router o abilita UPnP e controlla il firewall di Windows.";
        return NetState::Firewalled;
    }
    if (n.uptime < 120) {
        text = L"Verifica rete…";
        tip = L"In attesa di connessioni in ingresso o di nodi DHT.";
        return NetState::Starting;
    }
    if (n.peers_connected == 0 && n.dht_nodes == 0) {
        text = L"Nessuna connessione";
        tip = L"Nessun peer e nessun nodo DHT: controlla la connessione, eventuali proxy/VPN o il firewall.";
        return NetState::NoConnection;
    }
    text = L"Firewall / NAT";
    tip = L"Le connessioni in uscita funzionano ma non è arrivata nessuna connessione in ingresso sulla porta " + port +
          L". Scarichi comunque, ma con meno peer. Esegui la verifica online da Strumenti > Diagnostica rete.";
    return NetState::Firewalled;
}

PortCheck check_port_online(int port) {
    PortCheck r;
    r.done = true;
    r.port = port;
    r.when = std::time(nullptr);
    if (port < 1 || port > 65535) { r.status = "Porta non valida"; return r; }
    using HttpHandle = std::unique_ptr<void, decltype(&WinHttpCloseHandle)>;
    HttpHandle s(WinHttpOpen(L"SeedFlow/" SF_VERSION_WSTRING, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0), &WinHttpCloseHandle);
    if (!s) s.reset(WinHttpOpen(L"SeedFlow/" SF_VERSION_WSTRING, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!s) { r.status = "WinHTTP non disponibile"; return r; }
    WinHttpSetTimeouts(s.get(), 10000, 10000, 15000, 15000);
    std::string body;
    DWORD code = 0;
    if (HttpHandle c{WinHttpConnect(s.get(), L"ifconfig.co", INTERNET_DEFAULT_HTTPS_PORT, 0), &WinHttpCloseHandle}) {
        std::wstring path = L"/port/" + std::to_wstring(port);
        if (HttpHandle q{WinHttpOpenRequest(c.get(), L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE), &WinHttpCloseHandle}) {
            if (WinHttpSendRequest(q.get(), L"Accept: application/json\r\n", static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(q.get(), nullptr)) {
                DWORD len = sizeof(code);
                WinHttpQueryHeaders(q.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &len, WINHTTP_NO_HEADER_INDEX);
                DWORD avail = 0;
                constexpr std::size_t limit = 65536;
                for (;;) {
                    if (!WinHttpQueryDataAvailable(q.get(), &avail)) { r.status = "Lettura della risposta non riuscita"; break; }
                    if (!avail) break;
                    if (avail > limit - body.size()) { r.status = "Risposta del servizio troppo grande"; break; }
                    std::string chunk(avail, '\0');
                    DWORD read = 0;
                    if (!WinHttpReadData(q.get(), chunk.data(), avail, &read) || read == 0) { r.status = "Risposta del servizio incompleta"; break; }
                    body.append(chunk.data(), read);
                }
            } else r.status = "richiesta non riuscita (errore " + std::to_string(GetLastError()) + ")";
        }
    }
    if (!r.status.empty()) return r;
    if (body.empty()) { r.status = "nessuna risposta dal servizio"; return r; }
    if (code != 200) { r.status = "HTTP " + std::to_string(code); return r; }
    std::smatch m;
    if (std::regex_search(body, m, std::regex("\"reachable\"\\s*:\\s*(true|false)"))) {
        r.success = true;
        r.reachable = m[1] == "true";
    } else r.status = "risposta non riconosciuta";
    if (std::regex_search(body, m, std::regex("\"ip\"\\s*:\\s*\"([^\"]+)\""))) r.ip = m[1];
    if (std::regex_search(body, m, std::regex("\"status\"\\s*:\\s*\"([^\"]*)\""))) r.status = m[1];
    return r;
}

static bool parse_v4(std::string const& ip, unsigned char b[4]) {
    in_addr a{};
    if (InetPtonA(AF_INET, ip.c_str(), &a) != 1) return false;
    std::memcpy(b, &a, 4);
    return true;
}
bool is_cgnat(std::string const& ip) {
    unsigned char b[4];
    return parse_v4(ip, b) && b[0] == 100 && b[1] >= 64 && b[1] <= 127;
}
bool is_private_ipv4(std::string const& ip) {
    unsigned char b[4];
    if (!parse_v4(ip, b)) return false;
    return b[0] == 10 || (b[0] == 172 && b[1] >= 16 && b[1] <= 31) || (b[0] == 192 && b[1] == 168);
}

std::vector<std::wstring> local_adapters() {
    std::vector<std::wstring> out;
    ULONG size = 32 * 1024;
    std::vector<unsigned char> buf(size);
    ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_GATEWAYS;
    ULONG rc = GetAdaptersAddresses(AF_INET, flags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
    if (rc == ERROR_BUFFER_OVERFLOW) {
        buf.resize(size);
        rc = GetAdaptersAddresses(AF_INET, flags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
    }
    if (rc != NO_ERROR) return out;
    for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()); a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        std::wstring line = a->FriendlyName ? a->FriendlyName : L"?";
        line += L": ";
        bool any = false;
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            wchar_t text[64] = {};
            DWORD len = 64;
            if (WSAAddressToStringW(u->Address.lpSockaddr, u->Address.iSockaddrLength, nullptr, text, &len) == 0) {
                if (any) line += L", ";
                line += text; any = true;
            }
        }
        if (!any) continue;
        if (auto* g = a->FirstGatewayAddress) {
            wchar_t text[64] = {};
            DWORD len = 64;
            if (WSAAddressToStringW(g->Address.lpSockaddr, g->Address.iSockaddrLength, nullptr, text, &len) == 0)
                line += std::wstring(L"  gateway ") + text;
        }
        if (a->TransmitLinkSpeed && a->TransmitLinkSpeed != static_cast<ULONG64>(-1))
            line += L"  " + std::to_wstring(a->TransmitLinkSpeed / 1000000) + L" Mb/s";
        out.push_back(line);
    }
    return out;
}

static int run_hidden(std::wstring const& cmdline, bool elevated) {
    if (elevated) {
        SHELLEXECUTEINFOW sei{};
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NO_CONSOLE;
        sei.lpVerb = L"runas";
        sei.lpFile = L"netsh.exe";
        sei.lpParameters = cmdline.c_str();
        sei.nShow = SW_HIDE;
        if (!ShellExecuteExW(&sei) || !sei.hProcess) return GetLastError() == ERROR_CANCELLED ? -2 : -1;
        WaitForSingleObject(sei.hProcess, 20000);
        DWORD code = 1;
        GetExitCodeProcess(sei.hProcess, &code);
        CloseHandle(sei.hProcess);
        return static_cast<int>(code);
    }
    std::wstring full = L"netsh.exe " + cmdline;
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, full.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) return -1;
    WaitForSingleObject(pi.hProcess, 8000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<int>(code);
}
int firewall_rule_exists() {
    int rc = run_hidden(std::wstring(L"advfirewall firewall show rule name=\"") + kFirewallRule + L"\"", false);
    return rc < 0 ? -1 : rc == 0 ? 1 : 0;
}
bool add_firewall_rule(std::wstring& message) {
    std::wstring exe = exe_path().wstring();
    std::wstring args = std::wstring(L"advfirewall firewall add rule name=\"") + kFirewallRule +
        L"\" dir=in action=allow program=\"" + exe + L"\" enable=yes profile=any";
    int rc = run_hidden(args, true);
    if (rc == 0) { message = L"Regola del firewall di Windows aggiunta per SeedFlow.exe."; return true; }
    message = rc == -2 ? L"Operazione annullata: serve l'autorizzazione di amministratore."
                       : L"netsh non è riuscito (codice " + std::to_wstring(rc) + L").";
    return false;
}

} // namespace sf

