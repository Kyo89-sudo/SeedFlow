// Offline GeoLite2 Country lookup. All entry points run on the GUI thread.
#pragma once
#include <string>

namespace sf::geoip {
// Empty directory disables lookup and releases the active database and cache.
void set_directory(std::string directory);
// Starts or collects a cancellable background load once per peer-list refresh.
bool prepare_peer_flags();
// Call after prepare_peer_flags(). ISO 3166-1 alpha-2 code ("IT"), empty when unknown.
// Windows has no colour glyphs for regional-indicator flag emoji, so the GUI draws
// the flag from its own image list (icons::make_flag_imagelist).
std::string peer_country(std::string const& address);
}
