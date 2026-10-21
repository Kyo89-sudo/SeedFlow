// SeedFlow - network diagnostics helpers (online port test, firewall, adapters).
#pragma once
#include "engine.hpp"
#include <string>
#include <vector>
#include <ctime>

namespace sf {

enum class NetState { Starting, Open, Firewalled, NoConnection, NotListening };

struct PortCheck {
    bool done = false, success = false, reachable = false;
    int port = 0; // A result is valid only for the port that was actually tested.
    std::string ip, status;
    std::time_t when = 0;
};

// Evaluates the globe state from libtorrent data and the last online test.
NetState evaluate_network(NetView const& n, PortCheck const& last, std::wstring& text, std::wstring& tip);

// Blocking HTTPS request to https://ifconfig.co/port/<port> (echoip): the service
// connects back to the public IP on that TCP port. Run on a worker thread.
PortCheck check_port_online(int port);

bool is_cgnat(std::string const& ip);
bool is_private_ipv4(std::string const& ip);
std::vector<std::wstring> local_adapters();

extern wchar_t const* const kFirewallRule;
int firewall_rule_exists();               // 1 yes, 0 no, -1 unknown
bool add_firewall_rule(std::wstring& message); // UAC prompt (netsh)

} // namespace sf
