// The web's first run (docs/WEB.md): the page stores what the player chose in the browser's storage under the
// game root, which the page starts the game with (--run-dir /opfs/chronicletwo).
//
// - data.zip and data.json, as dc2data extract writes them (or a data/ folder): nothing to do.
// - disc.iso, an image of the PAL Dark Chronicle disc, or disc/, a folder holding its DATA.HD4, DATA.DAT,
//   SOUND.HD3 and SOUND.DAT: the game extracts it here once, with Kazarov's Rust extractor as run.sh runs dc2data
//   on the desktop (docs/PC.md), into the game root. The extraction runs on a thread of its own, so this thread
//   can report its progress to stdout as "extract: <percent> <stage>", which the page shows; "extract: done" or
//   "extract: failed: <why>" ends it. Movies are left out: the browser has no FFmpeg.
//
// Without either the game logs what to put there and exits with kExitNoData, as the desktop does without a
// setup window.

#include "platform/firstrun.hpp"

#include "kazarov_bridge/rust/lib.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <system_error>
#include <thread>

#include "exitcodes.hpp"
#include "kazarov/data_version.hpp"
#include "log/log.hpp"
#include "platform/paths.hpp"

namespace fs = std::filesystem;

namespace {

void Say(const std::string &line) {
    std::printf("%s\n", line.c_str());
    std::fflush(stdout);
}

// The disc the page stored beside the data: disc.iso, any other .iso, or the disc/ folder.
fs::path DiscSource(const fs::path &game_root) {
    std::error_code error;
    if (fs::is_regular_file(game_root / "disc.iso", error)) {
        return game_root / "disc.iso";
    }
    if (fs::is_regular_file(game_root / "disc" / "DATA.HD4", error) ||
        fs::is_regular_file(game_root / "disc" / "data.hd4", error)) {
        return game_root / "disc";
    }
    for (const fs::directory_entry &entry : fs::directory_iterator(game_root, error)) {
        std::string extension = entry.path().extension().string();
        std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return std::tolower(c); });
        if (extension == ".iso" && entry.is_regular_file(error)) {
            return entry.path();
        }
    }
    return {};
}

// Extracts source into root, reporting as it goes; empty when the data is current afterwards.
std::string Extract(const fs::path &source, const fs::path &root) {
    Say("extract: preparing the game data from " + source.filename().string());
    auto worker = std::async(std::launch::async, [source, root]() -> std::string {
        try {
            // The game runs the scripts' Luau modules, so the compiled scripts are not published.
            kazarov::extract_tracked_with_script_policy(source.string(), root.string(), 2,
                                                        kazarov::ScriptPublicationPolicy::LuauOnly);
            return {};
        } catch (const std::exception &error) {
            return error.what();
        }
    });
    std::string last;
    while (worker.wait_for(std::chrono::milliseconds(500)) != std::future_status::ready) {
        const auto        status = kazarov::extract_status();
        const int         percent = static_cast<int>(std::clamp(status.fraction, 0.0f, 1.0f) * 100.0f);
        const std::string line = "extract: " + std::to_string(percent) + " " + std::string(status.stage);
        if (line != last) {
            last = line;
            Say(line);
        }
    }
    std::string failure = worker.get();
    if (failure.empty() && FirstRunDataMissing(root)) {
        failure = "the extraction finished without current game data";
    }
    return failure;
}

} // namespace

bool FirstRunDataMissing(const fs::path &root) {
    // The data is <game-root>/data.zip, or the unified data/ directory, with data.json's version current.
    std::error_code error;
    const auto      game_root = root.parent_path();
    const bool      archive = fs::is_regular_file(game_root / "data.zip", error);
    const bool      directories = fs::is_directory(root, error);
    if ((!archive && !directories) || !fs::is_regular_file(game_root / "data.json", error)) {
        return true;
    }
    std::ifstream marker(game_root / "data.json");
    const auto    parsed = nlohmann::json::parse(marker, nullptr, false);
    return !parsed.is_object() || !parsed.contains("schema_version") || !parsed["schema_version"].is_number_integer() ||
           parsed["schema_version"] != 1 || !parsed.contains("data_version") ||
           !parsed["data_version"].is_number_integer() || parsed["data_version"] != KAZAROV_DATA_VERSION;
}

std::string FirstRunDataMessage(const fs::path &root) {
    const std::string where = PathsDisplay(root.parent_path());
    return "No current Dark Chronicle data at " + where + ".\n\n"
           "On the page, add an image of the PAL Dark Chronicle disc (stored as disc.iso), or the data.zip and data.json "
           "that dc2data extract wrote on a PC; the game extracts a disc once at its next start.\n";
}

void FirstRunRequireData(const fs::path &root, bool) {
    if (!FirstRunDataMissing(root)) {
        return;
    }
    const fs::path source = DiscSource(root.parent_path());
    if (!source.empty()) {
        CT_LOG_INFO(Main, "No current game data at {}: extracting {}", PathsDisplay(root), PathsDisplay(source));
        const std::string failure = Extract(source, root);
        if (failure.empty()) {
            Say("extract: done");
            return;
        }
        // Left on the page over the game, which stops here and tries again next time.
        Say("extract: failed: " + failure);
        CT_LOG_ERROR(Main, "Game data setup: {}", failure);
    }
    const std::string text = FirstRunDataMessage(root);
    // The logger ends each line itself.
    CT_LOG_ERROR(Main, "{}", std::string_view(text).substr(0, text.ends_with('\n') ? text.size() - 1 : text.size()));
    LogFlush();
    std::exit(kExitNoData);
}
