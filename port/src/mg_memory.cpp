#include "common.h"

#include <cstdio>

#include "mg_memory.hpp"

// Retail writes an object's run size by hand, in the PS2's quadwords, and hands that run to a
// placement new: new (memory->Alloc(4)) CCollisionMDT. A host object is larger wherever it holds
// a pointer or a vtable, so the hand-written run would leave the object's tail under whatever is
// allocated next. The run is still the arena's last when the object is placed onto it, so the
// placement operators grow it to the object's size first. DC1's port grows its arenas the same way
// in dataalloc2_1.cpp.

namespace {

// The run an mgCMemory handed out last, which the placement operators below may grow.
struct Run {
    mgCMemory *arena;
    u_char    *block;
    int        quads;
};

Run last_run;

void *PlaceRun(size_t size, u_long128 *block) {
    Run     run = last_run;
    u_char *bytes = (u_char *) block;
    int     quads = (int) ((size + 15) / 16);

    if (run.arena != NULL && run.block == bytes && quads > run.quads &&
        (u_char *) run.arena->stack + run.arena->stack_used * 16 == bytes + run.quads * 16) {
        int extra = quads - run.quads;

        if (run.arena->stack_used + extra < run.arena->stack_size) {
            run.arena->stack_used += extra;
            last_run = {run.arena, bytes, quads};
        } else {
            printf("stack over %d/%d at %s\n", run.arena->stack_used + extra, run.arena->stack_size, run.arena->name);
        }
    }

    return block;
}

} // namespace

void *operator new(size_t size, u_long128 *block) {
    return PlaceRun(size, block);
}

void *operator new[](size_t size, u_long128 *block) {
    return PlaceRun(size, block);
}

u_long128 *mgCMemory::Alloc(int size) {
    if (lock != 0) {
        return 0;
    }

    if (size <= 0) {
        return 0;
    }

    int old_used = stack_used;
    int old_capacity = stack_size;
    int new_used = old_used + size;

    if (new_used >= old_capacity) {
        printf("stack over %d/%d at %s\n", new_used, old_capacity, name);
        return 0;
    }

    u_long128 *data = &stack[old_used];
    stack_used = new_used;
    last_run = {this, (u_char *) data, size};
    return data;
}

u_long128 *mgCMemory::stAlloc(int size) {
    if (lock != 0) {
        return 0;
    }

    if (size <= 0) {
        return 0;
    }

    int old_used = stack_used;
    int old_capacity = stack_size;
    int new_used = old_used + size;

    if (new_used >= old_capacity) {
        printf("stack over %d/%d at %s\n", new_used, old_capacity, name);
        return 0;
    }

    u_long128 *data = &stack[old_used];
    stack_used = new_used;
    last_run = {this, (u_char *) data, size};
    return data;
}
