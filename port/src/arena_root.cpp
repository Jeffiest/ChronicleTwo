#include "arena_root.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <vector>

#include "platform/arena.hpp"
#include "platform/memory.hpp"

u_long128 *ArenaRoot(const void *key, int quadwords, const char *name) {
    const std::size_t bytes = static_cast<std::size_t>(quadwords) * sizeof(u_long128) * kArenaHeadroom;
    unsigned char    *base = ArenaBlock(key, bytes);

    static std::vector<const void *> logged;

    if (std::find(logged.begin(), logged.end(), base) == logged.end()) {
        logged.push_back(base);
        std::printf("arena: %s %zu bytes at %p%s\n", name != nullptr && name[0] != '\0' ? name : "?", bytes,
                    static_cast<void *>(base), IsAbove4GiB(base) ? " (above 4 GiB)" : " (below 4 GiB)");
        // A PS2_UNIMPLEMENTED abort drops buffered stdout, and the mapping is what a failed run is read for.
        std::fflush(stdout);
    }

    return reinterpret_cast<u_long128 *>(base);
}
