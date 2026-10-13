#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "menuchr.hpp"
#include "menu_model_memory_plan.hpp"

namespace {

// Retail capacities from MenuMemoryDivide, including its per-partition 64-block
// rounding. Native model loading omits the hardware packets that exhausted slot 1.
constexpr std::size_t kHumanBlocks[MENU_CHARA_LOAD_MAX] = {
    0x1B80, 0x9AC0, 0x1CC0, 0x11C0, 0x0BC0, 0x26C0, 0x0740};
constexpr std::size_t kRoboBlocks[MENU_CHARA_LOAD_MAX] = {
    0x5240, 0x4540, 0x6E80, 0x1240, 0x0600, 0, 0};
constexpr std::size_t kMonsterBlocks[MENU_CHARA_LOAD_MAX] = {
    0x6400, 0, 0, 0, 0, 0x03C0, 0};

void Name(mgCMemory *memory, const char *name) {
    if (memory != nullptr && std::strlen(name) < sizeof(memory->name)) {
        std::strcpy(memory->name, name);
    }
}

} // namespace

// Native owner for the retail menu model partitions. It validates the complete parent
// extent after alignment before exposing any child view, then gives the remaining tail
// to file/work loading. Child stacks borrow the pool's backing bytes and remain aliases
// of that owner across reset/reload; no child owns or frees the parent allocation.
PORT_OVERRIDE void MenuMemoryAdjust(mgCMemory *pool, mgCMemory *rest, mgCMemory *buffers, int chara) {
    if (pool == nullptr || rest == nullptr || buffers == nullptr || pool->stack == nullptr ||
        pool->stack_size < 0 || pool->stack_used < 0) {
        std::fputs("menu model arena has invalid parent owner\n", stderr);
        std::abort();
    }

    const std::size_t count = MENU_CHARA_LOAD_MAX;
    const std::size_t *capacities = nullptr;
    bool monster_alias = false;
    switch (chara) {
        case USER_CHARA_MAX:
        case USER_CHARA_MONICA:
            capacities = kHumanBlocks;
            break;
        case USER_CHARA_ROBO:
            capacities = kRoboBlocks;
            break;
        case USER_CHARA_MONSTER:
            capacities = kMonsterBlocks;
            monster_alias = true;
            break;
        default:
            std::fprintf(stderr, "menu model arena has unknown character owner %d\n", chara);
            std::abort();
    }

    port_menu_model_memory::Plan plan{};
    if (!port_menu_model_memory::MakePlan(reinterpret_cast<std::uintptr_t>(pool->stack),
                                         static_cast<std::size_t>(pool->stack_size),
                                         static_cast<std::size_t>(pool->stack_used), capacities, count,
                                         monster_alias, &plan)) {
        std::fprintf(stderr, "menu model partitions exceed parent (%d blocks, used %d)\n",
                     pool->stack_size, pool->stack_used);
        std::abort();
    }

    // Match the retail owner's alignment state transition so any later parent
    // allocation begins after the padding consumed by the child views.
    pool->stack_used = static_cast<int>(plan.aligned_parent_offset);
    const auto parent = reinterpret_cast<std::uintptr_t>(pool->stack);
    for (std::size_t i = 0; i < count; ++i) {
        buffers[i].stSetBuffer(reinterpret_cast<u_long128 *>(parent + plan.slot_offset[i] * 16),
                               static_cast<int>(plan.slot_blocks[i]));
        buffers[i].stack_used = 0;
        buffers[i].lock = 0;
        char name[0x10];
        std::snprintf(name, sizeof(name), "stack %u", static_cast<unsigned>(i));
        Name(&buffers[i], name);
    }
    rest->stSetBuffer(reinterpret_cast<u_long128 *>(parent +
                        (plan.aligned_parent_offset + plan.partition_blocks) * 16),
                      static_cast<int>(plan.rest_blocks));
    rest->stack_used = 0;
    rest->lock = 0;
    Name(rest, "LOAD STACK");
}
