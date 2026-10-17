// F-key tuning of the aerial dungeon map (automap.cpp). Kept apart from the game headers, whose
// macros SDL's headers clash with.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>

#include "platform/config.hpp"

// Live tuning while the map shows, as on the first game's port: F5/F6 model size (x0.8/x1.25),
// F1/F2 map size (-/+0.1), F7 zoom +0.25 (Shift+F7 -0.25), Shift+F5/F6 ceiling cut -/+10. Each
// press is saved to config.json at once.
void AerialTuneKeys() {
    struct Key {
        SDL_Scancode code;
        bool         shift;
        int          field;
        float        step;
    };
    static const Key keys[] = {
        {SDL_SCANCODE_F6, false, 0, 1.25f}, {SDL_SCANCODE_F5, false, 0, 0.8f},
        {SDL_SCANCODE_F2, false, 1, 0.1f},  {SDL_SCANCODE_F1, false, 1, -0.1f},
        {SDL_SCANCODE_F7, false, 2, 0.25f}, {SDL_SCANCODE_F7, true, 2, -0.25f},
        {SDL_SCANCODE_F6, true, 3, 10.0f},  {SDL_SCANCODE_F5, true, 3, -10.0f},
    };
    static bool held[sizeof keys / sizeof keys[0]] = {};
    int         count = 0;
    const bool *state = SDL_GetKeyboardState(&count);

    if (state == nullptr) {
        return;
    }

    const bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
    Config     config = ConfigGet();
    bool       changed = false;

    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        const bool down = keys[i].shift == shift && keys[i].code < count && state[keys[i].code];

        if (down && !held[i]) {
            const float step = keys[i].step;
            switch (keys[i].field) {
                case 0: config.aerial_map_model_scale = std::clamp(config.aerial_map_model_scale * step, 0.5f, 40.0f); break;
                case 1: config.aerial_map_size = std::clamp(config.aerial_map_size + step, 0.5f, 1.5f); break;
                case 2: config.aerial_map_zoom = std::clamp(config.aerial_map_zoom + step, 0.5f, 8.0f); break;
                default: config.aerial_map_ceiling = std::clamp(config.aerial_map_ceiling + step, 0.0f, 5000.0f); break;
            }
            changed = true;
        }

        held[i] = down;
    }

    if (changed) {
        std::fprintf(stderr, "aerial map: model_scale %g size %g zoom %g ceiling %g\n", config.aerial_map_model_scale,
                     config.aerial_map_size, config.aerial_map_zoom, config.aerial_map_ceiling);
        ConfigChange(config);
    }
}

