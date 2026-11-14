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
//   The page offers two ways and names the player's choice in CT_WEB_UNPACK. "whole" (or nothing) is the above:
//   the game starts when the whole disc is unpacked. "play" starts the game as soon as the extraction can serve
//   reads (Kazarov's progressive session, platform/progressive_mount.cpp): until then the lines are the same,
//   with the percentage counting to that moment; after "extract: done" the rest is converted behind the game, a
//   read of something not converted yet waiting for it, and the page hears "unpack: <percent>" and then
//   "unpack: done" or "unpack: failed: <why>". data.zip and data.json take their places at the end, so a page
//   closed before that starts again from the disc.
//
// Without either the game logs what to put there and exits with kExitNoData, as the desktop does without a
// setup window.

#include "platform/firstrun.hpp"

#include "kazarov_bridge/rust/lib.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

#include <emscripten/heap.h>

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

// How many conversions run at once: half the device's processors, two to eight (four on a phone with eight);
// CT_WEB_EXTRACT_JOBS (1 to 8) sets another. The disc in desktop Chrome took 220 s with two, 122 s with four and
// 78 s with eight, and the memory the page reached was the same with each: the planning before the first
// conversion is what holds it.
std::size_t ExtractJobs() {
    if (const char *asked = std::getenv("CT_WEB_EXTRACT_JOBS")) {
        const long value = std::strtol(asked, nullptr, 10);
        if (value >= 1 && value <= 8) {
            return static_cast<std::size_t>(value);
        }
    }
    return std::clamp<std::size_t>(std::thread::hardware_concurrency() / 2, 2, 8);
}

// Extracts source into root, reporting as it goes; empty when the data is current afterwards.
std::string Extract(const fs::path &source, const fs::path &root) {
    const auto began = std::chrono::steady_clock::now();
    Say("extract: preparing the game data from " + source.filename().string());
    // The extraction's temporary files (Kazarov's spill of model data, over a gigabyte for the disc) go to the
    // browser's storage beside the data: /tmp is in memory here, and memory is 4 GiB.
    std::error_code temporary_error;
    const fs::path  temporary = root.parent_path() / ".extract-tmp";
    fs::remove_all(temporary, temporary_error);
    fs::create_directories(temporary, temporary_error);
    if (!temporary_error) {
        setenv("TMPDIR", temporary.c_str(), 1);
    }
    const std::size_t jobs = ExtractJobs();
    CT_LOG_INFO(Main, "Extracting with {} conversions at a time", jobs);
    auto worker = std::async(std::launch::async, [source, root, jobs]() -> std::string {
        try {
            // The game runs the scripts' Luau modules, so the compiled scripts are not published.
            kazarov::extract_tracked_with_script_policy(source.string(), root.string(), jobs,
                                                        kazarov::ScriptPublicationPolicy::LuauOnly);
            return {};
        } catch (const std::exception &error) {
            return error.what();
        }
    });
    // Each stage goes to the log with the time and the size of the program's memory when it began, so a page the
    // browser ended says how far it got and how much it held.
    const auto seconds = [began] {
        return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - began).count();
    };
    const auto memory_mib = [] { return emscripten_get_heap_size() >> 20; };
    std::string last;
    std::string last_stage;
    while (worker.wait_for(std::chrono::milliseconds(500)) != std::future_status::ready) {
        const auto        status = kazarov::extract_status();
        const int         percent = static_cast<int>(std::clamp(status.fraction, 0.0f, 1.0f) * 100.0f);
        const std::string stage(status.stage);
        const std::string line = "extract: " + std::to_string(percent) + " " + stage;
        if (line != last) {
            last = line;
            Say(line);
        }
        if (stage != last_stage) {
            last_stage = stage;
            CT_LOG_INFO(Main, "Extraction, {} s: {} (memory {} MiB)", seconds(), stage, memory_mib());
        }
    }
    std::string failure = worker.get();
    CT_LOG_INFO(Main, "The extraction ran for {} s (memory {} MiB)", seconds(), memory_mib());
    fs::remove_all(temporary, temporary_error);
    if (failure.empty() && FirstRunDataMissing(root)) {
        failure = "the extraction finished without current game data";
    }
    return failure;
}

// Set once the game has been let through to play while the disc is still being unpacked.
std::atomic<bool> playing_while_unpacking{false};

// Starts the extraction of source into root and returns when the game can read from it, the rest going on behind
// the game; empty then, and otherwise why it could not.
std::string ExtractWhilePlaying(const fs::path &source, const fs::path &root) {
    const auto began = std::chrono::steady_clock::now();
    Say("extract: preparing the game data from " + source.filename().string());
    std::error_code temporary_error;
    const fs::path  temporary = root.parent_path() / ".extract-tmp";
    fs::remove_all(temporary, temporary_error);
    fs::create_directories(temporary, temporary_error);
    if (!temporary_error) {
        setenv("TMPDIR", temporary.c_str(), 1);
    }
    const std::size_t jobs = ExtractJobs();
    CT_LOG_INFO(Main, "Extracting with {} conversions at a time, playing meanwhile", jobs);
    const auto seconds = [began] {
        return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - began).count();
    };
    const auto memory_mib = [] { return emscripten_get_heap_size() >> 20; };
    kazarov::progressive_begin();
    std::thread([source, root, jobs, temporary, seconds, memory_mib] {
        std::string failure;
        try {
            kazarov::progressive_run(source.string(), root.string(), jobs, kazarov::ScriptPublicationPolicy::LuauOnly);
        } catch (const std::exception &error) {
            failure = error.what();
        }
        std::error_code error;
        fs::remove_all(temporary, error);
        if (failure.empty() && FirstRunDataMissing(root)) {
            failure = "the extraction finished without current game data";
        }
        CT_LOG_INFO(Main, "The extraction ran for {} s (memory {} MiB)", seconds(), memory_mib());
        // Before the game began, the wait below reports a failure; after, the page does.
        if (!playing_while_unpacking.load()) {
            return;
        }
        if (failure.empty()) {
            Say("unpack: done");
        } else {
            Say("unpack: failed: " + failure);
            CT_LOG_ERROR(Main, "Game data setup: {}", failure);
        }
    }).detach();
    const float ready = std::max(kazarov::progressive_ready_fraction(), 0.01f);
    std::string last;
    std::string last_stage;
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        const auto status = kazarov::progressive_status();
        if (status.phase == 4) {
            return std::string(status.error).empty() ? "the extraction stopped" : std::string(status.error);
        }
        if (status.phase >= 2) {
            playing_while_unpacking.store(true);
            break;
        }
        const int         percent = static_cast<int>(std::clamp(status.fraction / ready, 0.0f, 0.99f) * 100.0f);
        const std::string stage(status.stage);
        const std::string line = "extract: " + std::to_string(percent) + " " + stage;
        if (line != last) {
            last = line;
            Say(line);
        }
        if (stage != last_stage) {
            last_stage = stage;
            CT_LOG_INFO(Main, "Extraction, {} s: {} (memory {} MiB)", seconds(), stage, memory_mib());
        }
    }
    CT_LOG_INFO(Main, "The game can start after {} s (memory {} MiB); the rest is extracted behind it", seconds(),
                memory_mib());
    // What is left, for the page: the conversions done of those the game may ask for.
    std::thread([] {
        int last_percent = -1;
        while (true) {
            const auto status = kazarov::progressive_status();
            if (status.phase != 2) {
                return;
            }
            const int percent =
                status.total == 0 ? 0 : static_cast<int>(std::min<std::uint64_t>(status.done * 100 / status.total, 99));
            if (percent != last_percent) {
                last_percent = percent;
                Say("unpack: " + std::to_string(percent));
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }).detach();
    return {};
}

// Whether the page asked for the game to start while the disc is still being unpacked.
bool PlayWhileUnpacking() {
    const char *asked = std::getenv("CT_WEB_UNPACK");
    return asked != nullptr && std::string_view(asked) == "play";
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
        const std::string failure = PlayWhileUnpacking() ? ExtractWhilePlaying(source, root) : Extract(source, root);
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
