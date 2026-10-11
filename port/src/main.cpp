#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "audio/mixer.hpp"
#include "exitcodes.hpp"
#include "gfx/gfx.hpp"
#include "platform/audio.hpp"
#include "platform/clock.hpp"
#include "platform/config.hpp"
#include "platform/input.hpp"
#include "platform/input_script.hpp"
#include "platform/paths.hpp"
#include "platform/window.hpp"

// The game headers keep MWCC's pragmas, which clang names but ignores.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-pragmas"
#include "dataread.hpp"
#include "mainloop.hpp"
#pragma clang diagnostic pop

/**
 *
 * The port's entry point. It takes the place of ps2/src/main.cpp's main
 * through the weak-merge: the port's definition is strong and the game's
 * becomes weak, so the linker keeps this one.
 *
 * It is retail main's work without the hardware: parse the command line, set
 * up the platform layer and the renderer, open the data tree, and run the
 * game's own MainLoop. The parts of the boot that belong to a mode - the
 * debug start menu, the language, the save pick - are MainLoop's.
 *
 */

namespace fs = std::filesystem;

namespace {

/**
 *
 * Raised by the pump hook when the run has to end: the frame budget is spent
 * or the window was closed. DC2's MainLoop owns its loop and has no stop seam
 * of its own yet (DC1's GameRequestStop), so unwinding it back to main is how
 * the driver stops the game. The throw happens inside ClockPump, at the tick
 * that ended the frame, so the frame's own work is already done.
 *
 */
struct StopGame {};

struct Options {
    bool         headless = false;
    bool         offscreen = false;
    std::int64_t frames = -1;
    const char  *screenshot = nullptr;
    const char  *input = nullptr;
    int          width = 0;
    int          height = 0;
    const char  *jump = nullptr;
    bool         fast_load = false;
};

/**
 * Ticks of the game clock the run stops after; negative is no budget. One
 * tick is one VSync - the unit MainLoop waits in, mgFrameRate ticks a frame -
 * so the budget is exact until the loop grows a seam that counts frames.
 */
std::int64_t g_frame_budget = -1;

[[noreturn]] void Usage(const char *program) {
    std::fprintf(stderr,
                 "usage: %s [--data DIR] [--save DIR] [--headless] [--offscreen]\n"
                 "          [--frames N] [--screenshot PATH] [--input FILE]\n"
                 "          [--width W] [--height H] [--jump MODE[:MAP]] [--fast-load]\n"
                 "  --data DIR         the extracted game data (default: DC_DATA, then ./data, then data/\n"
                 "                     beside the executable)\n"
                 "  --save DIR         saves, config.json and the pipeline cache (default: DC_SAVE, then\n"
                 "                     ./save, then save/ beside the executable)\n"
                 "  --headless         render offscreen (SDL's offscreen driver, VK_EXT_headless_surface),\n"
                 "                     no audio output, the game clock unbounded\n"
                 "  --offscreen        --headless without a Vulkan surface: render to an image only\n"
                 "  --frames N         stop after N ticks of the game clock (one tick is one VSync; the\n"
                 "                     main loop runs mgFrameRate of them per frame)\n"
                 "  --screenshot PATH  write the last tick's image to PATH as a PNG on exit\n"
                 "  --input FILE       drive pad 1 from a script (default: DC_INPUT); see\n"
                 "                     platform/input_script.hpp. The script's frames are clock ticks.\n"
                 "  --width, --height  window size in pixels (default: config.json, then the monitor's)\n"
                 "test hooks (the game-side seams have not landed; a value warns and is ignored):\n"
                 "  --jump MODE[:MAP]  start in a mode, skipping the warm-up (DC_JUMP)\n"
                 "  --fast-load        cut the loading screens' holds and fades (DC_FAST_LOAD=1)\n",
                 program);
    std::exit(kExitUsage);
}

Options ParseOptions(int argc, char **argv) {
    Options options;
    for (int i = 1; i < argc; i++) {
        std::string_view arg = argv[i];
        auto             value = [&]() -> const char * {
            if (i + 1 >= argc) {
                Usage(argv[0]);
            }
            return argv[++i];
        };
        auto number = [&]() {
            char     *end = nullptr;
            long long result = std::strtoll(value(), &end, 10);
            if (end == nullptr || *end != '\0' || result < 0) {
                Usage(argv[0]);
            }
            return static_cast<std::int64_t>(result);
        };
        if (arg == "--headless") {
            options.headless = true;
        } else if (arg == "--offscreen") {
            options.headless = true;
            options.offscreen = true;
        } else if (arg == "--frames") {
            options.frames = number();
        } else if (arg == "--screenshot") {
            options.screenshot = value();
        } else if (arg == "--input") {
            options.input = value();
        } else if (arg == "--width") {
            options.width = static_cast<int>(number());
        } else if (arg == "--height") {
            options.height = static_cast<int>(number());
        } else if (arg == "--jump") {
            options.jump = value();
        } else if (arg == "--fast-load") {
            options.fast_load = true;
        } else {
            Usage(argv[0]);
        }
    }
    return options;
}

// InitCDFile stops on the same conditions, but checking first gives the one-line message and its
// own exit status before a window or a Vulkan device exists.
void RequireData() {
    const fs::path &root = PathsDataRoot();
    std::error_code error;
    const char     *why = nullptr;
    if (!fs::is_directory(root, error)) {
        why = "is not a directory";
    } else {
        why = "is empty";
        auto options = fs::directory_options::follow_directory_symlink |
                       fs::directory_options::skip_permission_denied;
        for (fs::recursive_directory_iterator it(root, options, error), end; !error && it != end;
             it.increment(error)) {
            std::error_code entry_error;
            if (it->is_regular_file(entry_error)) {
                why = nullptr;
                break;
            }
        }
    }
    if (why != nullptr) {
        std::fprintf(stderr,
                     "no game data: %s %s; point --data (or DC_DATA) at the extracted disc, the "
                     "directory holding DATA.HD4 and DATA.DAT\n",
                     root.c_str(), why);
        std::exit(kExitNoData);
    }
}

[[noreturn]] void RequestStop() {
    throw StopGame{};
}

// The clock's pump hook: the host side of every VSync the game waits on. A pump runs at most once
// per elapsed tick and never from inside another pump, so this is where the driver can both feed
// the platform and decide the run is over.
void PumpHost() {
    if (!WindowPollEvents()) {
        RequestStop();
    }
    InputPoll();
    InputScriptApply(ClockTickCount());
    if (g_frame_budget >= 0 && ClockTickCount() >= g_frame_budget) {
        RequestStop();
    }
}

void LoadInputScript(const char *path) {
    if (path == nullptr) {
        path = std::getenv("DC_INPUT");
    }
    if (path == nullptr || *path == '\0') {
        return;
    }
    InputScript script;
    std::string error;
    if (!InputScriptLoad(path, script, error)) {
        std::fprintf(stderr, "bad input script: %s\n", error.c_str());
        std::exit(kExitUsage);
    }
    InputScriptInstall(std::move(script));
}

void ReportShaderProgress(uint32_t done, uint32_t total) {
    static uint32_t    reported = 0;
    constexpr uint32_t kSteps = 4;
    if (total == 0) {
        return;
    }
    uint32_t step = done * kSteps / total;
    if (done == 0 || step > reported) {
        reported = step;
        std::fprintf(stderr, "compiling shaders %u/%u\n", done, total);
    }
}

gfx::PresentMode PresentMode(ConfigPresentMode mode) {
    switch (mode) {
        case ConfigPresentMode::Mailbox:
            return gfx::PresentMode::Mailbox;
        case ConfigPresentMode::Immediate:
            return gfx::PresentMode::Immediate;
        default:
            return gfx::PresentMode::Fifo;
    }
}

int Screenshot(const char *path) {
    std::vector<uint8_t> pixels;
    uint32_t             width = 0;
    uint32_t             height = 0;
    if (!gfx::ReadbackFrame(pixels, width, height) || !gfx::WritePng(path, pixels.data(), width, height)) {
        std::fprintf(stderr, "cannot write the screenshot to %s\n", path);
        return kExitFailure;
    }
    return kExitOk;
}

// The sound manager starts the output device itself (CSound::Init, sound.cpp), so the driver's
// only lever is the setting it reads. A headless run wants no device; an explicit DC_AUDIO or
// DC_AUDIO_WAV from the caller wins.
void DisableAudioForHeadless() {
    if (std::getenv("DC_AUDIO") != nullptr || std::getenv("DC_AUDIO_WAV") != nullptr) {
        return;
    }
#if defined(_WIN32)
    _putenv_s("DC_AUDIO", "off");
#else
    setenv("DC_AUDIO", "off", 0);
#endif
}

} // namespace

int main(int argc, char **argv) {
    argc = PathsConsumeArgs(argc, argv);
    Options options = ParseOptions(argc, argv);
    if (options.jump == nullptr) {
        options.jump = std::getenv("DC_JUMP");
    }
    if (options.jump != nullptr && *options.jump != '\0') {
        // The hook belongs to the loop: MainLoop picks its start mode itself and takes no start
        // mode yet, so the option is reported rather than half-honoured.
        std::fprintf(stderr, "--jump %s: no start-mode seam in this build; starting normally\n", options.jump);
    }
    const char *fast_load = std::getenv("DC_FAST_LOAD");
    if (options.fast_load || (fast_load != nullptr && *fast_load != '\0' && *fast_load != '0')) {
        std::fprintf(stderr, "--fast-load: no loading-screen seam in this build; holds unchanged\n");
    }
    RequireData();
    LoadInputScript(options.input);

    ConfigLoad();
    const Config &config = ConfigGet();
    if (options.headless) {
        DisableAudioForHeadless();
    }

    WindowConfig window;
    window.width = options.width > 0 ? options.width : config.window_width;
    window.height = options.height > 0 ? options.height : config.window_height;
    window.fullscreen = config.fullscreen;
    window.headless = options.headless;
    bool offscreen = options.offscreen || (options.headless && !gfx::HeadlessSurfaceAvailable());
    window.vulkan = !offscreen;
    WindowInit(window);
    InputInit();

    gfx::RendererConfig renderer;
    renderer.present_mode = PresentMode(config.present_mode);
    renderer.pipeline_cache = PathsSaveRoot() / "pipeline_cache.bin";
    renderer.progress = ReportShaderProgress;
    renderer.offscreen = offscreen;
    renderer.layout.aspect =
        config.aspect == ConfigAspect::Auto ? gfx::AspectMode::Fill : gfx::AspectMode::Letterbox;
    renderer.layout.ui_scale = config.ui_scale;
    gfx::RendererInit(WindowHandle(), renderer);

    audio::DefaultMixer().SetMasterGain(config.master_volume);

    ClockSetTickRate(config.tick_rate);
    ClockSetUnbounded(options.headless);
    ClockAddPumpHook(PumpHost);
    g_frame_budget = options.frames;

    InitCDFile();
    try {
        MainLoop();
    } catch (const StopGame &) {
        // The frame budget ran out or the window closed; the loop is abandoned where the frame
        // that was running ended, and the run finishes below.
    }

    int status = kExitOk;
    if (options.screenshot != nullptr) {
        status = Screenshot(options.screenshot);
    }

    ClockRemovePumpHook(PumpHost);
    AudioOutputStop();
    InputShutdown();
    gfx::RendererShutdown();
    WindowShutdown();
    return status;
}
