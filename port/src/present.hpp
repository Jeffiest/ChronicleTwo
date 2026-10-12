#pragma once

#include <cstdint>
#include <string>

#include "platform/settings.hpp"

struct PresentSettings {
    bool interpolation = true;
    // 0 leaves the pace to the present mode.
    double max_fps = 0.0;
    // An unbounded clock has no real time to fill, so display renders are made per tick at evenly spaced alphas and discarded.
    int display_per_tick = 0;
    bool show_fps = false;
    ConfigFpsDetail fps_detail = ConfigFpsDetail::All;
    bool log_fps = false;
};

struct PresentStats {
    std::uint64_t ticks = 0;
    std::uint64_t display_frames = 0;
    std::uint64_t presented = 0;
    std::uint64_t canonical_presents = 0;
    std::uint32_t last_draws = 0;
};

void PresentSetSettings(const PresentSettings &settings);
const PresentSettings &PresentGetSettings();
PresentStats PresentGetStats();
void PresentResetStats();
void PresentTickRendered();
void PresentWaitFrame(int start, int frames);
void PresentTickEnd();
std::string PresentFpsText();
