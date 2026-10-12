#include "common.h"

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstddef>
#include <climits>
#include <array>
#include <cstring>
#include <vector>

#include "mg_memory.hpp"
#include "mg_memory_port.hpp"
#include "arena_diagnostics.hpp"

#if defined(_MSC_VER)
#include <intrin.h>
#define MG_CALLER_ADDRESS() reinterpret_cast<uintptr_t>(_ReturnAddress())
#elif defined(__GNUC__) || defined(__clang__)
#define MG_CALLER_ADDRESS() reinterpret_cast<uintptr_t>(__builtin_return_address(0))
#else
#define MG_CALLER_ADDRESS() uintptr_t{0}
#endif

namespace {

constexpr std::uintptr_t kQuadwordBytes = sizeof(u_long128);
constexpr int kHeaderQuadwords = static_cast<int>((sizeof(mgMEMORY_BLOCK) + kQuadwordBytes - 1) / kQuadwordBytes);

u_long128 *QuadwordsFrom(const void *base, std::ptrdiff_t quadwords) {
    return reinterpret_cast<u_long128 *>(static_cast<unsigned char *>(const_cast<void *>(base)) +
                                         quadwords * static_cast<std::ptrdiff_t>(kQuadwordBytes));
}

std::ptrdiff_t QuadwordsBetween(const void *from, const void *to) {
    return (static_cast<const unsigned char *>(to) - static_cast<const unsigned char *>(from)) /
           static_cast<std::ptrdiff_t>(kQuadwordBytes);
}

mgMEMORY_BLOCK *BlockAt(const void *base, std::ptrdiff_t quadwords) {
    return reinterpret_cast<mgMEMORY_BLOCK *>(QuadwordsFrom(base, quadwords));
}

struct PeakEntry {
    const mgCMemory *memory;
    bool heap;
    int capacity;
    int peak;
    std::array<char, sizeof(mgCMemory::name) + 1> name;
};

std::vector<PeakEntry> &PeakTable() {
    static std::vector<PeakEntry> table;
    return table;
}

PeakEntry *FindPeak(const mgCMemory *memory) {
    for (PeakEntry &entry : PeakTable()) {
        if (entry.memory == memory) return &entry;
    }
    return nullptr;
}

void ResetPeak(const mgCMemory *memory, bool heap, int capacity) {
    PeakEntry *entry = FindPeak(memory);
    PeakEntry fresh{memory, heap, capacity, 0, {}};
    std::memcpy(fresh.name.data(), memory->name, sizeof(memory->name));
    if (entry == nullptr) PeakTable().push_back(fresh);
    else *entry = fresh;
}

int HeapAllocatedQuadwords(const mgCMemory &memory) {
    if (memory.heap_top == nullptr) return 0;
    int total = 0;
    for (const mgMEMORY_BLOCK *block = memory.heap_top->next; block->next != nullptr; block = block->next) {
        total += block->size + (block == memory.stack_block ? memory.stack_used : 0);
    }
    return total;
}

void NotePeak(const mgCMemory &memory) {
    PeakEntry *entry = FindPeak(&memory);
    if (entry == nullptr) return;
    int used = entry->heap ? HeapAllocatedQuadwords(memory) : memory.stack_used;
    if (used > entry->peak) entry->peak = used;
}

// The run an mgCMemory handed out last, which the placement operators below may grow.
struct Run {
    mgCMemory *arena;
    u_char    *block;
    int        quads;
};

Run last_run;

bool ValidArena(const mgCMemory *arena) {
    return arena != nullptr && arena->stack != nullptr && arena->stack_size > 0 &&
           arena->stack_used >= 0 && arena->stack_used <= arena->stack_size;
}

void ArenaDiagnostic(const char *operation, const mgCMemory *arena, int request, const char *reason,
                     uintptr_t caller) {
    port_arena_diagnostics::Failure(operation, arena, request, reason, caller);
}

[[noreturn]] void PlacementFailure(const mgCMemory *arena, int request, const char *reason, uintptr_t caller) {
    port_arena_diagnostics::Failure("placement", arena, request, reason, caller, true);
    fflush(stderr);
    abort();
}

int QuadsForBytes(size_t size) {
    size_t quads = size / 16 + (size % 16 != 0);
    return quads > static_cast<size_t>(INT_MAX) ? -1 : static_cast<int>(quads);
}

void *PlaceRun(size_t size, u_long128 *block, uintptr_t caller) {
    Run run = last_run;
    if (block == nullptr) {
        // Explicit operator new callers use NULL to detect optional allocation failure.
        return nullptr;
    }

    int quads = QuadsForBytes(size);
    if (quads < 0) {
        PlacementFailure(run.arena, INT_MAX, "object size exceeds arena accounting range", caller);
    }

    u_char *bytes = reinterpret_cast<u_char *>(block);
    if (run.arena != nullptr && run.block == bytes && quads > run.quads) {
        if (!ValidArena(run.arena)) {
            PlacementFailure(run.arena, quads, "invalid arena state while growing last run", caller);
        }

        uintptr_t base = reinterpret_cast<uintptr_t>(run.arena->stack);
        uintptr_t used = static_cast<uintptr_t>(run.arena->stack_used);
        if (used > (UINTPTR_MAX - base) / 16) {
            PlacementFailure(run.arena, quads, "arena end address overflow", caller);
        }

        uintptr_t current_end = base + used * 16;
        uintptr_t block_address = reinterpret_cast<uintptr_t>(bytes);
        uintptr_t run_end = block_address + static_cast<uintptr_t>(run.quads) * 16;
        if (current_end != run_end) {
            PlacementFailure(run.arena, quads, "last placement run is no longer at arena end", caller);
        }

        int extra = quads - run.quads;
        // Retail reserves the final quadword; preserve its strict capacity boundary.
        if (extra >= run.arena->stack_size - run.arena->stack_used) {
            PlacementFailure(run.arena, quads, "grown object exceeds available arena tail", caller);
        }

        run.arena->stack_used += extra;
        port_arena_diagnostics::HighWater(run.arena);
        NotePeak(*run.arena);
        last_run = {run.arena, bytes, quads};
    }

    return block;
}

u_long128 *Allocate(mgCMemory *arena, int size, const char *operation, uintptr_t caller) {
    if (arena->lock != 0 || size <= 0) {
        return nullptr;
    }

    if (!ValidArena(arena)) {
        ArenaDiagnostic(operation, arena, size, "uninitialized or invalid arena state", caller);
        return nullptr;
    }

    // Keep the retail strict boundary: one quadword remains reserved at the end.
    port_arena_diagnostics::HighWater(arena);
    int available = arena->stack_size - arena->stack_used;
    if (size >= available) {
        ArenaDiagnostic(operation, arena, size, "request reaches or exceeds reserved tail", caller);
        return nullptr;
    }

    u_long128 *data = &arena->stack[arena->stack_used];
    arena->stack_used += size;
    port_arena_diagnostics::HighWater(arena);
    NotePeak(*arena);
    last_run = {arena, reinterpret_cast<u_char *>(data), size};
    return data;
}

void AlignArena64(mgCMemory *arena, const char *operation, uintptr_t caller) {
    if (arena->lock != 0) {
        return;
    }

    if (!ValidArena(arena)) {
        ArenaDiagnostic(operation, arena, 0, "uninitialized or invalid arena state", caller);
        return;
    }

    uintptr_t base = reinterpret_cast<uintptr_t>(arena->stack);
    uintptr_t used = static_cast<uintptr_t>(arena->stack_used);
    if (used > (UINTPTR_MAX - base) / 16) {
        ArenaDiagnostic(operation, arena, 0, "arena end address overflow", caller);
        return;
    }

    uintptr_t address = base + used * 16;
    size_t misalignment = static_cast<size_t>(address & 63);
    int padding = misalignment == 0 ? 0 : static_cast<int>((64 - misalignment) / 16);
    if (padding > arena->stack_size - arena->stack_used) {
        // Retail clamps failed alignment to the end of the region.
        arena->stack_used = arena->stack_size;
        NotePeak(*arena);
        return;
    }

    arena->stack_used += padding;
    port_arena_diagnostics::HighWater(arena);
    NotePeak(*arena);
}

} // namespace

PORT_OVERRIDE void mgCMemory::Init() {
    heap_size = 0;
    heap = nullptr;
    heap_top = nullptr;
    name[0] = '\0';
    stack = nullptr;
    stack_used = 0;
    stack_size = 0;
    stack_block = nullptr;
    lock = 0;
}

PORT_OVERRIDE void mgCMemory::SetHeapMem(u_long128 *buffer, int size) {
    if (buffer == nullptr || size < 0x10) {
        Init();
        ResetPeak(this, true, 0);
        return;
    }

    heap = buffer;
    heap_size = static_cast<u_int>(size);
    heap_top = reinterpret_cast<mgMEMORY_BLOCK *>(buffer);
    heap_top->data = nullptr;
    heap_top->size = kHeaderQuadwords;
    heap_top->next = BlockAt(buffer, static_cast<std::ptrdiff_t>(heap_size) - kHeaderQuadwords);
    heap_top->next->data = nullptr;
    heap_top->next->size = 0;
    heap_top->next->next = nullptr;
    ResetPeak(this, true, size);
}

PORT_OVERRIDE void mgCMemory::ClearHeapMem() {
    u_long128 *buffer = heap;
    const int size = static_cast<int>(heap_size);
    Init();
    SetHeapMem(buffer, size);
}

PORT_OVERRIDE void mgCMemory::Free(u_long128 *data) {
    if (data == nullptr) return;

    mgMEMORY_BLOCK *prev = nullptr;
    mgMEMORY_BLOCK *found = nullptr;
    for (mgMEMORY_BLOCK *block = heap_top; block != nullptr && block->next != nullptr; block = block->next) {
        if (block->data == data) {
            found = block;
            break;
        }
        prev = block;
    }

    if (found == nullptr || prev == nullptr) {
        std::fprintf(stderr, "Illegal Free Memory %p\n", static_cast<void *>(data));
        std::abort();
    }
    prev->next = found->next;
}

PORT_OVERRIDE u_long128 *mgCMemory::StartStackMode(int mode, int size) {
    stack_block = nullptr;
    mgMEMORY_BLOCK *block = heap_top;
    if (block == nullptr) return nullptr;

    mgMEMORY_BLOCK *chosen = nullptr;
    mgMEMORY_BLOCK *chosen_previous = nullptr;
    std::ptrdiff_t chosen_count = 0;
    std::ptrdiff_t best_count = 0;

    for (mgMEMORY_BLOCK *next; (next = block->next) != nullptr; block = next) {
        mgMEMORY_BLOCK *gap = BlockAt(block, block->size);
        const std::ptrdiff_t count = QuadwordsBetween(gap, next);
        if (mode == 1 && count > kHeaderQuadwords) {
            chosen = gap;
            chosen_previous = block;
            chosen_count = count;
            break;
        }
        if (mode == 2 && count >= kHeaderQuadwords && best_count < count) {
            chosen = gap;
            chosen_previous = block;
            chosen_count = count;
            best_count = count;
        }
        if (mode == 3 && static_cast<std::ptrdiff_t>(size) + kHeaderQuadwords < count) {
            chosen = gap;
            chosen_previous = block;
            chosen_count = count;
            break;
        }
    }

    if (chosen == nullptr) return nullptr;
    stack_block = chosen;
    stack_block->next = chosen_previous->next;
    chosen_previous->next = stack_block;
    stack_block->size = kHeaderQuadwords;
    stack_block->data = QuadwordsFrom(stack_block, kHeaderQuadwords);
    stack = stack_block->data;
    stack_size = static_cast<int>(chosen_count - kHeaderQuadwords);
    stack_used = 0;
    NotePeak(*this);
    return stack_block->data;
}

PORT_OVERRIDE void mgCMemory::EndStackMode() {
    mgMEMORY_BLOCK *block = stack_block;
    if (block != nullptr) {
        block->size += stack_used;
        stack_block = nullptr;
        stack = nullptr;
        stack_size = 0;
        stack_used = 0;
    }
}

void MemoryLogPeaks() {
    for (const port_arena_diagnostics::Record &record : port_arena_diagnostics::records) {
        if (record.owner != nullptr && record.capacity >= 0) {
            std::fprintf(stderr, "arena peak: name='%.16s' arena=%p high_water=%d/%d quadwords\n", record.name,
                         static_cast<const void *>(record.owner), record.high_water, record.capacity);
        }
    }
    for (const PeakEntry &entry : PeakTable()) {
        if (entry.name[0] == '\0') {
            std::printf("mg_memory: ? [%p] peak %d of %d quadwords (%d free)\n",
                        static_cast<const void *>(entry.memory), entry.peak, entry.capacity,
                        entry.capacity - entry.peak);
        } else {
            std::printf("mg_memory: %.16s peak %d of %d quadwords (%d free)\n", entry.name.data(), entry.peak,
                        entry.capacity, entry.capacity - entry.peak);
        }
    }
}

mgMemoryUsage MemoryUsage(const mgCMemory &memory) {
    const PeakEntry *entry = FindPeak(&memory);
    return {memory.name, entry ? entry->capacity : 0, entry ? entry->peak : 0};
}

void MemoryForget(const mgCMemory &memory) {
    std::vector<PeakEntry> &table = PeakTable();
    for (auto it = table.begin(); it != table.end(); ++it) {
        if (it->memory == &memory) {
            table.erase(it);
            return;
        }
    }
}

PORT_OVERRIDE void *operator new(size_t size, u_long128 *block) {
    return PlaceRun(size, block, MG_CALLER_ADDRESS());
}

PORT_OVERRIDE void *operator new[](size_t size, u_long128 *block) {
    return PlaceRun(size, block, MG_CALLER_ADDRESS());
}

PORT_OVERRIDE u_long128 *mgCMemory::Alloc(int size) {
    return Allocate(this, size, "Alloc", MG_CALLER_ADDRESS());
}

PORT_OVERRIDE u_long128 *mgCMemory::stAlloc(int size) {
    return Allocate(this, size, "stAlloc", MG_CALLER_ADDRESS());
}

PORT_OVERRIDE u_long128 *mgCMemory::stAllocTest(int size) {
    if (lock != 0 || size <= 0) {
        return nullptr;
    }

    if (!ValidArena(this)) {
        ArenaDiagnostic("stAllocTest", this, size, "uninitialized or invalid arena state", MG_CALLER_ADDRESS());
        return nullptr;
    }

    int available = stack_size - stack_used;
    if (size >= available) {
        ArenaDiagnostic("stAllocTest", this, size, "request reaches or exceeds reserved tail", MG_CALLER_ADDRESS());
        return nullptr;
    }

    return &stack[stack_used];
}

PORT_OVERRIDE void mgCMemory::stAlign64() {
    AlignArena64(this, "stAlign64", MG_CALLER_ADDRESS());
}

PORT_OVERRIDE void mgCMemory::Align64() {
    AlignArena64(this, "Align64", MG_CALLER_ADDRESS());
}

PORT_OVERRIDE void mgCMemory::stSetBuffer(u_long128 *buffer, int size) {
    stack = buffer;
    stack_used = 0;
    stack_size = size;
    port_arena_diagnostics::Rebind(this, MG_CALLER_ADDRESS());
    ResetPeak(this, false, size);
}
