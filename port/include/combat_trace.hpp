#pragma once
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace port_combat_trace {
inline void Record(int stage, int pressed, int held, int eye, int pause, int debug,
                   int program, int ended, int history, int accumulated) {
    static const bool enabled = [] {
        const char *value = std::getenv("DC2_TRACE_COMBAT");
        return value != nullptr && value[0] != 0 && std::strcmp(value, "0") != 0;
    }();
    if (!enabled || stage < 0 || stage > 5) return;
    static unsigned records = 0;
    static std::array<std::array<int, 9>, 6> previous{};
    static std::array<bool, 6> seen{};
    const std::array<int, 9> state{pressed, held, eye, pause, debug, program, ended, history, accumulated};
    if (records == 256 || (seen[stage] && state == previous[stage])) return;
    seen[stage] = true;
    previous[stage] = state;
    std::fprintf(stderr, "combat %u stage=%d press=%d hold=%d eye=%d pause=%#x debug=%d prog=%d end=%d history=%d accum=%d\n",
                 records++, stage, pressed, held, eye, pause, debug, program, ended, history, accumulated);
}
} // namespace port_combat_trace
