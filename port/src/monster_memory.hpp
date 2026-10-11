#pragma once

#include "mg_memory.hpp"

#include <cstdio>
#include <cstdlib>

inline void PortSetMonsterBuffers(mgCMemory *slots, int count, mgCMemory &source) {
    for (int i = 0; i < count; ++i) {
        u_long128 *buffer = source.stAlloc64(4000);
        if (buffer == nullptr) {
            std::fprintf(stderr, "monster arena setup failed: slot=%d source=%p used=%d capacity=%d\n",
                         i, static_cast<void *>(&source), source.stack_used, source.stack_size);
            std::abort();
        }
        slots[i].stSetBuffer(buffer, 4000);
        slots[i].stack_used = 0;
        slots[i].lock = 0;
    }
}
