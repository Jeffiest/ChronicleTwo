#include "monster_memory.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>

int main() {
    constexpr int count = 24;
    alignas(64) static u_long128 backing[count * 4000 + 4];
    struct Slots {
        std::uint64_t before = 0x123456789abcdef0ULL;
        mgCMemory slot[count];
        std::uint64_t after = 0xfedcba9876543210ULL;
    } slots;
    mgCMemory source;
    source.stSetBuffer(backing, count * 4000 + 4);
    PortSetMonsterBuffers(slots.slot, count, source);
    for (int i = 0; i < count; ++i) {
        assert(slots.slot[i].stack == backing + i * 4000);
        assert(slots.slot[i].stack_size == 4000);
        assert(slots.slot[i].stack_used == 0);
        assert(slots.slot[i].lock == 0);
        assert(reinterpret_cast<std::uintptr_t>(slots.slot[i].stack) % 64 == 0);
        assert(slots.slot[i].Alloc(1) == backing + i * 4000);
    }
    assert(source.stack_used == count * 4000);
    assert(slots.before == 0x123456789abcdef0ULL);
    assert(slots.after == 0xfedcba9876543210ULL);
    std::puts("PASS: all 24 host monster arenas initialized, distinct, aligned and writable; canaries intact");
}
