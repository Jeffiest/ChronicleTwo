#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include "mg_memory.hpp"

#if defined(_WIN32)
extern "C" unsigned char __ImageBase;
#endif

namespace port_arena_diagnostics {

struct Record {
    const mgCMemory *owner = nullptr;
    const void *buffer = nullptr;
    int capacity = -1;
    unsigned generation = 0;
    unsigned rebinds = 0;
    unsigned resets = 0;
    uintptr_t binding_caller = 0;
    int high_water = 0;
    uintptr_t first_failure_caller = 0;
    unsigned failures = 0;
    char name[17]{};
};

constexpr unsigned kRecordCount = 128;
inline Record records[kRecordCount]{};

inline unsigned Increment(unsigned n) { return n == UINT32_MAX ? n : n + 1; }

inline uintptr_t ImageBase() {
#if defined(_WIN32)
    return reinterpret_cast<uintptr_t>(&__ImageBase);
#else
    return 0;
#endif
}

inline void Summary(const Record &r) {
    if (r.failures == 0) return;
    std::fprintf(stderr, "arena lifecycle: name='%s' arena=%p generation=%u resets=%u rebinds=%u high_water=%d/%d failures=%u first_caller=%p binding_caller=%p image_base=%p\n",
                 r.name, static_cast<const void *>(r.owner), r.generation, r.resets, r.rebinds,
                 r.high_water, r.capacity, r.failures, reinterpret_cast<void *>(r.first_failure_caller),
                 reinterpret_cast<void *>(r.binding_caller), reinterpret_cast<void *>(ImageBase()));
}

inline void CopyName(Record &r, const mgCMemory *arena) {
    std::memcpy(r.name, arena->name, 16);
    r.name[16] = '\0';
}

inline Record &Lookup(const mgCMemory *arena) {
    Record *free_record = nullptr;
    for (Record &r : records) {
        if (r.owner == arena) return r;
        if (r.owner == nullptr && free_record == nullptr) free_record = &r;
    }
    // Diagnostics are bounded; an evicted owner loses history, never allocator state.
    if (free_record == nullptr) free_record = &records[(reinterpret_cast<uintptr_t>(arena) >> 4) % kRecordCount];
    Summary(*free_record);
    *free_record = Record{};
    free_record->owner = arena;
    free_record->buffer = arena->stack;
    free_record->capacity = arena->stack_size;
    free_record->generation = 1;
    CopyName(*free_record, arena);
    return *free_record;
}

inline void NewGeneration(Record &r, const mgCMemory *arena) {
    Summary(r);
    r.generation = Increment(r.generation);
    r.buffer = arena->stack;
    r.capacity = arena->stack_size;
    r.high_water = 0;
    r.failures = 0;
    r.first_failure_caller = 0;
    CopyName(r, arena);
}

inline Record &Bind(const mgCMemory *arena) {
    Record &r = Lookup(arena);
    if (r.buffer != arena->stack || r.capacity != arena->stack_size) {
        NewGeneration(r, arena);
        r.rebinds = Increment(r.rebinds);
        r.binding_caller = 0;
    } else if (arena->stack_used < r.high_water) {
        NewGeneration(r, arena);
        r.resets = Increment(r.resets);
    }
    CopyName(r, arena);
    return r;
}

inline void HighWater(const mgCMemory *arena) {
    if (!arena) return;
    Record &r = Bind(arena);
    if (arena->stack_used > r.high_water) r.high_water = arena->stack_used;
}

inline void Failure(const char *operation, const mgCMemory *arena, int request, const char *reason,
                    uintptr_t caller, bool fatal = false) {
    if (!arena) {
        std::fprintf(stderr, "arena %s failed: name='<null>' request=%d reason=%s caller=%p image_base=%p\n",
                     operation, request, reason, reinterpret_cast<void *>(caller), reinterpret_cast<void *>(ImageBase()));
        return;
    }
    Record &r = Bind(arena);
    r.failures = Increment(r.failures);
    if (r.failures == 1) r.first_failure_caller = caller;
    if (r.failures != 1 && !fatal) return;
    std::fprintf(stderr,
                 "arena %s failed: name='%.16s' arena=%p buffer=%p request=%d used=%d capacity=%d high_water=%d generation=%u resets=%u rebinds=%u reason=%s caller=%p binding_caller=%p image_base=%p failures=%u\n",
                 operation, r.name, static_cast<const void *>(arena), arena->stack, request, arena->stack_used,
                 arena->stack_size, r.high_water, r.generation, r.resets, r.rebinds, reason,
                 reinterpret_cast<void *>(caller), reinterpret_cast<void *>(r.binding_caller),
                 reinterpret_cast<void *>(ImageBase()), r.failures);
}

inline void Rebind(const mgCMemory *arena, uintptr_t caller) {
    if (!arena) return;
    Record &r = Lookup(arena);
    NewGeneration(r, arena);
    r.rebinds = Increment(r.rebinds);
    r.binding_caller = caller;
}

} // namespace port_arena_diagnostics
