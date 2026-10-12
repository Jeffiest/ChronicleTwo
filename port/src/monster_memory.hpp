#pragma once

#include "mg_memory.hpp"

#include <cstdio>
#include <cstdlib>

// The retail 4,000-quadword monster pool covers PS2-sized character copies and scripts. The host
// copy needs additional room for its wider runtime frames and mesh objects.
constexpr int kPortMonsterSlotQuadwords = 8192;

inline void PortSetMonsterBuffers(mgCMemory *slots, int count, mgCMemory &source) {
    for (int i = 0; i < count; ++i) {
        u_long128 *buffer = source.stAlloc64(kPortMonsterSlotQuadwords);
        if (buffer == nullptr) {
            std::fprintf(stderr, "monster arena setup failed: slot=%d source=%p used=%d capacity=%d\n",
                         i, static_cast<void *>(&source), source.stack_used, source.stack_size);
            std::abort();
        }
        slots[i].stSetBuffer(buffer, kPortMonsterSlotQuadwords);
        slots[i].stack_used = 0;
        slots[i].lock = 0;
    }
}
