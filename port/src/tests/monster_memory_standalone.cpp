#include "monster_memory.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>

int main() {
    constexpr int count = 24;
    alignas(64) static u_long128 backing[count * kPortMonsterSlotQuadwords + 4];
    struct Slots {
        std::uint64_t before = 0x123456789abcdef0ULL;
        mgCMemory slot[count];
        std::uint64_t after = 0xfedcba9876543210ULL;
    } slots;
    mgCMemory source;
    source.stSetBuffer(backing, count * kPortMonsterSlotQuadwords + 4);
    u_long128 *const arena_base = source.stack;
    PortSetMonsterBuffers(slots.slot, count, source);
    for (int i = 0; i < count; ++i) {
        assert(slots.slot[i].stack == arena_base + i * kPortMonsterSlotQuadwords);
        assert(slots.slot[i].stack_size == kPortMonsterSlotQuadwords);
        assert(slots.slot[i].stack_used == 0);
        assert(slots.slot[i].lock == 0);
        assert(reinterpret_cast<std::uintptr_t>(slots.slot[i].stack) % 64 == 0);
        assert(slots.slot[i].Alloc(1) == arena_base + i * kPortMonsterSlotQuadwords);
    }
    assert(source.stack_used == count * kPortMonsterSlotQuadwords);
    assert(slots.before == 0x123456789abcdef0ULL);
    assert(slots.after == 0xfedcba9876543210ULL);
    std::puts("PASS: all 24 host monster arenas initialized, distinct, aligned and writable; canaries intact");
}
