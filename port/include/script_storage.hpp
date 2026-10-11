#pragma once

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <limits>

#include "mg_memory.hpp"

namespace port_script_storage {

// mgCMemory::Alloc counts 16-byte units; script structs contain host pointers.
template <typename T>
inline int blocks_for(std::size_t count) {
    constexpr std::size_t block_size = 16;
    const std::size_t max = std::numeric_limits<std::size_t>::max();
    if (count != 0 && sizeof(T) > (max - (block_size - 1)) / count) {
        std::fputs("script storage size overflow\n", stderr);
        std::abort();
    }

    const std::size_t blocks = (count * sizeof(T) + block_size - 1) / block_size;
    if (blocks > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        std::fputs("script storage exceeds mgCMemory block limit\n", stderr);
        std::abort();
    }
    return static_cast<int>(blocks);
}

template <typename T>
inline T *allocate(mgCMemory *memory, std::size_t count, const char *owner) {
    if (memory == nullptr) {
        std::fprintf(stderr, "%s: script storage has no arena\n", owner);
        std::abort();
    }
    void *storage = memory->Alloc(blocks_for<T>(count));
    if (storage == nullptr) {
        std::fprintf(stderr, "%s: script storage allocation failed (%zu entries)\n", owner, count);
        std::abort();
    }
    return static_cast<T *>(storage);
}

} // namespace port_script_storage
