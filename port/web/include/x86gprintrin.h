// Stands for clang's x86gprintrin.h on wasm32. Quill's Rdtsc.h (quill 13) includes it for __rdtsc on every
// platform that is not Arm, PowerPC, RISC-V, s390x or LoongArch, and clang ships the header for every target,
// where it compiles for x86 alone. The browser has no cycle counter: the steady clock in nanoseconds stands for
// it, which quill's calibration reads as a 1 GHz counter. On the web alone: web.cmake puts this directory on
// the include path (ct_web_options).
#pragma once

#include <stdint.h>
#include <time.h>

static inline uint64_t __rdtsc(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}
