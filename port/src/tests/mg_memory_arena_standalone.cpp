#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>

#include "mg_memory.hpp"
#include "arena_diagnostics.hpp"

// Keep this focused executable independent of the full game link. These two methods are the
// retail implementations needed by the host allocator under test.
void mgCMemory::Init() {
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

u_long128 *mgCMemory::stAlloc64(int size) {
    stAlign64();
    return stAlloc(size);
}

namespace {

struct WideObject {
    unsigned char bytes[40];
};

bool Check(bool condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        return false;
    }
    return true;
}

int Run(const char *case_name) {
    if (strcmp(case_name, "growth-failure") == 0 || strcmp(case_name, "suppressed-growth-failure") == 0) {
        mgCMemory arena;
        std::array<u_long128, 3> storage{};
        arena.stSetBuffer(storage.data(), static_cast<int>(storage.size()));
        u_long128 *run = arena.Alloc(1);
        if (run == nullptr) return 2;
        if (strcmp(case_name, "suppressed-growth-failure") == 0) arena.Alloc(3);
        // A correct host placement allocator terminates before this constructor can run.
        new (run) WideObject;
        return 3;
    }

    if (strcmp(case_name, "diagnostics-lifecycle") == 0) {
        mgCMemory arena;
        std::array<u_long128, 8> storage{};
        std::memcpy(arena.name, "fixture-owner", 14);
        arena.stSetBuffer(storage.data(), static_cast<int>(storage.size()));
        arena.Alloc(3);
        auto &record = port_arena_diagnostics::Bind(&arena);
        if (record.high_water != 3 || std::strcmp(record.name, "fixture-owner") != 0) return 1;
        arena.stAllocTest(8);
        arena.stAllocTest(8);
        if (record.failures != 2) return 1;
        arena.lock = 1;
        int before_locked_alloc = arena.stack_used;
        if (arena.Alloc(1) != nullptr || arena.stack_used != before_locked_alloc) return 1;
        arena.lock = 0;
        arena.stack_used = 0; // Exercise a direct retail-style reset that cannot call a host hook.
        arena.Alloc(1);
        if (record.resets != 1 || record.high_water != 1 || record.generation != 3) return 1;
        arena.stSetBuffer(storage.data(), static_cast<int>(storage.size()));
        auto &rebound = port_arena_diagnostics::Bind(&arena);
        if (rebound.generation != 4 || rebound.rebinds != 2 || rebound.resets != 1 ||
            rebound.high_water != 0 || rebound.failures != 0) return 1;
        std::array<u_long128, 12> other_storage{};
        arena.stSetBuffer(other_storage.data(), static_cast<int>(other_storage.size()));
        if (record.generation != 5 || record.rebinds != 3 || record.resets != 1 ||
            record.buffer != other_storage.data() || record.capacity != 12) return 1;
        std::memcpy(arena.name, "renamed-owner", 14);
        arena.Alloc(4);
        if (std::strcmp(record.name, "renamed-owner") != 0) return 1;
        arena.stack_used = 0;
        arena.Alloc(6);
        if (record.resets != 2 || record.generation != 6 || record.high_water != 6) return 1;
        std::puts("PASS: diagnostics-lifecycle");
        return 0;
    }

    bool ok = true;
    if (strcmp(case_name, "bounds-growth") == 0) {
        mgCMemory arena;
        std::array<u_long128, 8> storage{};
        arena.stSetBuffer(storage.data(), static_cast<int>(storage.size()));
        u_long128 *run = arena.Alloc(1);
        auto *object = new (run) WideObject;
        ok &= Check(object == reinterpret_cast<WideObject *>(storage.data()), "placement address");
        ok &= Check(arena.stack_used == 3, "placement growth accounts for the complete object");
        ok &= Check(arena.Alloc(4) != nullptr, "allocation after grown object");
        ok &= Check(arena.stack_used == 7, "final usable quadword reaches reserved boundary");
        ok &= Check(arena.Alloc(1) == nullptr, "reserved tail rejects a further allocation");
        ok &= Check(arena.stack_used == 7, "failed allocation leaves used count unchanged");
    } else if (strcmp(case_name, "invalid-reset-align") == 0) {
        mgCMemory arena;
        ok &= Check(arena.Alloc(1) == nullptr, "zero-capacity allocation returns null");
        std::array<u_long128, 8> storage{};
        arena.stSetBuffer(storage.data(), static_cast<int>(storage.size()));
        arena.stack_used = -1;
        ok &= Check(arena.stAlloc(1) == nullptr, "negative used count is rejected");
        arena.stSetBuffer(storage.data(), static_cast<int>(storage.size()));
        arena.stAlign64();
        uintptr_t aligned = reinterpret_cast<uintptr_t>(&arena.stack[arena.stack_used]);
        ok &= Check((aligned & 63) == 0, "alignment reaches a 64-byte boundary");
        ok &= Check(arena.stAlloc64(1) != nullptr, "64-byte allocation succeeds after alignment");
    } else if (strcmp(case_name, "optional-null") == 0) {
        void *result = operator new(sizeof(WideObject), static_cast<u_long128 *>(nullptr));
        ok &= Check(result == nullptr, "explicit optional placement allocation preserves null fallback");
    } else if (strcmp(case_name, "reset-growth") == 0) {
        mgCMemory arena;
        std::array<u_long128, 8> storage{};
        arena.stSetBuffer(storage.data(), static_cast<int>(storage.size()));
        ok &= Check(arena.Alloc(1) != nullptr, "initial run succeeds");
        arena.stSetBuffer(storage.data(), static_cast<int>(storage.size()));
        u_long128 *run = arena.stAlloc(1);
        ok &= Check(run != nullptr, "rebound run succeeds");
        if (run != nullptr) new (run) WideObject;
        ok &= Check(arena.stack_used == 3, "rebound run can grow to host object size");
    } else {
        fprintf(stderr, "unknown case: %s\n", case_name);
        return 2;
    }

    if (ok) fprintf(stdout, "PASS: %s\n", case_name);
    return ok ? 0 : 1;
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: mg_memory_arena_standalone <case>\n");
        return 2;
    }
    return Run(argv[1]);
}
