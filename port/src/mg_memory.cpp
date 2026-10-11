#include "common.h"

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <climits>

#include "mg_memory.hpp"
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
        return;
    }

    arena->stack_used += padding;
    port_arena_diagnostics::HighWater(arena);
}

} // namespace

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
}
