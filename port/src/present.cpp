#include "present.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <utility>

#include "gfx/gfx.hpp"
#include "mglib_port.hpp"
#include "platform/clock.hpp"
#include "platform/input.hpp"
#include "platform/overlay.hpp"

void mgTimeWaitTicks(int start, int frames, ClockWaitHook hook);

namespace {

using Clock = std::chrono::steady_clock;

struct FpsOverlay {
    bool on = false;
    OverlayRate frames;
    OverlayRate ticks;
    std::string text;
    gfx::LogicalMapping mapping{};
    gfx::DisplayListRef list;
};

PresentSettings settings;
PresentStats stats;
bool tick_shown = true;
Clock::time_point last_present;
Clock::time_point last_log;
FpsOverlay overlay;
std::int64_t counted_tick = 0;
int wait_start = 0;
int wait_frames = 0;

void PollFpsToggle() {
    if (InputHostPressed(InputHostAction::FpsToggle)) {
        overlay.on = !overlay.on;
    }
}

bool SameMapping(const gfx::LogicalMapping &a, const gfx::LogicalMapping &b) {
    return a.scale_x == b.scale_x && a.scale_y == b.scale_y && a.offset_x == b.offset_x &&
           a.offset_y == b.offset_y && a.pixel_width == b.pixel_width && a.pixel_height == b.pixel_height;
}

const gfx::DisplayList *FpsOverlayList() {
    if (!overlay.on) {
        return nullptr;
    }

    std::string text = PresentFpsText();
    const gfx::LogicalMapping mapping = gfx::GetLogicalMapping(gfx::kMainTarget);

    if (!overlay.list || text != overlay.text || !SameMapping(mapping, overlay.mapping)) {
        overlay.list = OverlayRecord(text);
        overlay.text = std::move(text);
        overlay.mapping = mapping;
    }

    return overlay.list.get();
}

void NotePresented() {
    ++stats.presented;
    const Clock::time_point now = Clock::now();
    overlay.frames.Count(now);

    if (settings.log_fps && now - last_log >= std::chrono::seconds(1)) {
        last_log = now;
        std::fprintf(stderr, "present: %s\n", PresentFpsText().c_str());
    }
}

bool DisplayFrame(float alpha, bool present) {
    const gfx::DisplayList *list = mgPortLastList();

    if (list == nullptr) {
        return false;
    }

    gfx::RenderOptions options;
    options.previous = mgPortPreviousList();
    options.present = present;
    options.overlay = present ? FpsOverlayList() : nullptr;
    const bool shown = gfx::RenderList(*list, alpha, options);

    if (shown) {
        ++stats.display_frames;

        if (present) {
            NotePresented();
        }
    }

    return shown;
}

bool PresentCanonicalFrame() {
    const gfx::DisplayList *overlay_list = FpsOverlayList();
    bool shown = false;

    if (overlay_list != nullptr) {
        gfx::RenderOptions options;
        options.present = true;
        options.host = true;
        shown = gfx::RenderList(*overlay_list, 1.0f, options);
    } else {
        shown = gfx::PresentCanonical();
    }

    if (shown) {
        ++stats.canonical_presents;
        NotePresented();
    }

    return shown;
}

bool BetweenTicks(double fraction, Clock::time_point next_tick) {
    if (mgPortLastList() == nullptr) {
        return false;
    }

    PollFpsToggle();

    if (!settings.interpolation) {
        if (!tick_shown) {
            PresentCanonicalFrame();
            tick_shown = true;
        }

        return false;
    }

    if (settings.max_fps > 0.0) {
        const Clock::time_point due =
            last_present + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / settings.max_fps));
        const Clock::time_point now = Clock::now();

        if (now < due) {
            if (due >= next_tick && tick_shown) {
                return false;
            }

            std::this_thread::sleep_until(std::min(due, next_tick));
            return true;
        }
    }

    // A game frame spans mgFrameRate ticks, so alpha is the fraction of the game frame elapsed, not of the tick.
    const double elapsed = static_cast<double>(ClockTickCount() - wait_start) + fraction;
    const double alpha = wait_frames > 0 ? elapsed / wait_frames : fraction;

    if (!DisplayFrame(static_cast<float>(std::clamp(alpha, 0.0, std::nextafter(1.0, 0.0))), true)) {
        return false;
    }

    last_present = Clock::now();
    tick_shown = true;
    return true;
}

} // namespace

void PresentSetSettings(const PresentSettings &value) {
    settings = value;
    overlay.on = value.show_fps;
}

const PresentSettings &PresentGetSettings() {
    return settings;
}

PresentStats PresentGetStats() {
    return stats;
}

void PresentResetStats() {
    stats = PresentStats{};
}

void PresentTickRendered() {
    const gfx::DisplayList *list = mgPortLastList();

    if (list == nullptr) {
        return;
    }

    ++stats.ticks;
    const gfx::DisplayListStats draws = gfx::ListStats(*list);
    stats.last_draws = draws.mesh_draws + draws.draws_2d;

    // Capped so a long stall does not replay an unbounded number of ticks into the rate window.
    const Clock::time_point now = Clock::now();
    const std::int64_t now_tick = ClockTickCount();

    for (std::int64_t t = counted_tick; t < now_tick && t < counted_tick + 16; ++t) {
        overlay.ticks.Count(now);
    }

    counted_tick = now_tick;
    tick_shown = false;
}

void PresentWaitFrame(int start, int frames) {
    wait_start = start;
    wait_frames = frames;
    mgTimeWaitTicks(start, frames, BetweenTicks);
}

void PresentTickEnd() {
    if (tick_shown || mgPortLastList() == nullptr) {
        return;
    }

    PollFpsToggle();

    if (ClockUnbounded()) {
        for (int k = 0; k < settings.display_per_tick; ++k) {
            DisplayFrame(static_cast<float>(k) / static_cast<float>(settings.display_per_tick), false);
        }
    }

    PresentCanonicalFrame();
    last_present = Clock::now();
    tick_shown = true;
}

std::string PresentFpsText() {
    char piece[64];
    std::snprintf(piece, sizeof piece, "FPS %.1f", overlay.frames.PerSecond());
    std::string text = piece;

    if (settings.fps_detail != ConfigFpsDetail::Fps) {
        std::snprintf(piece, sizeof piece, "  TICK %.1f/%g", overlay.ticks.PerSecond(), ClockTickRate());
        text += piece;
    }

    if (settings.fps_detail == ConfigFpsDetail::All) {
        std::snprintf(piece, sizeof piece, "  DRAWS %u", stats.last_draws);
        text += piece;
    }

    return text;
}
