#include "common.h"

#include "dng_hud.hpp"

// Retail's definition returns void against the fixed-up declaration (scripts/port/header_fixups.py).
PORT_OVERRIDE int CLockOnModel::Draw() {
    PS2_UNIMPLEMENTED();
}
