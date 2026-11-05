// Offline GeoLite2 Country lookup. All entry points run on the GUI thread.
#pragma once
#include <string>

namespace sf::geoip {
// Empty directory disables lookup and releases the active database and cache.
void set_directory(std::string directory);
// Starts or collects a cancellable background load once per peer-list refresh.
bool prepare_peer_flags();
// Call after prepare_peer_flags(). Returns empty for unknown addresses.
std::wstring peer_flag(std::string const& address);
}
