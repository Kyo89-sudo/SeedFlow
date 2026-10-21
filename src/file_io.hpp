#pragma once
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>
#include <utility>

namespace sf::file_io {

// Read the size before allocating. Failed reads leave the caller's buffer intact.
inline bool read_bounded(std::filesystem::path const& path, std::vector<char>& out,
    std::size_t limit) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return false;
    auto const size = input.tellg();
    if (size < 0 || static_cast<std::uintmax_t>(size) > limit ||
        static_cast<std::uintmax_t>(size) > static_cast<std::uintmax_t>((std::numeric_limits<std::streamsize>::max)())) return false;
    std::vector<char> data(static_cast<std::size_t>(size));
    input.seekg(0);
    if (!data.empty()) input.read(data.data(), static_cast<std::streamsize>(data.size()));
    if (!input) return false;
    out.swap(data);
    return true;
}

// Replace only after the temporary file is closed and, if requested, backed up.
inline bool write_atomic(std::filesystem::path const& path, std::vector<char> const& data,
    std::string& error, bool backup = false) {
    namespace fs = std::filesystem;
    error.clear();
    fs::path temporary = path; temporary += L".tmp";
    auto fail = [&](std::string message) {
        error = std::move(message);
        std::error_code ignored;
        fs::remove(temporary, ignored);
        return false;
    };
    try {
        if (data.size() > static_cast<std::uintmax_t>((std::numeric_limits<std::streamsize>::max)()))
            return fail("Dati troppo grandi per il salvataggio");
        if (!path.parent_path().empty()) fs::create_directories(path.parent_path());
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output) return fail("Impossibile aprire il file temporaneo");
            output.write(data.data(), static_cast<std::streamsize>(data.size()));
            output.flush();
            bool const written = static_cast<bool>(output);
            output.close();
            if (!written || !output) return fail("Scrittura o chiusura del file non riuscita");
        }
        std::error_code ec;
        bool const exists = fs::exists(path, ec);
        if (ec) return fail("Controllo file non riuscito: " + ec.message());
        if (backup && exists) {
            fs::path copy = path; copy += L".bak";
            fs::copy_file(path, copy, fs::copy_options::overwrite_existing, ec);
            if (ec) return fail("Backup non riuscito: " + ec.message());
        }
        fs::rename(temporary, path, ec);
        if (ec) return fail("Sostituzione file non riuscita: " + ec.message());
        return true;
    } catch (std::exception const& ex) { return fail(ex.what()); }
}
} // namespace sf::file_io
