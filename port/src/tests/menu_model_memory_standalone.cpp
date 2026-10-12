#include <cassert>
#include <cstdint>

#include "menu_model_memory_plan.hpp"

using port_menu_model_memory::MakePlan;
using port_menu_model_memory::Plan;

int main() {
    constexpr std::size_t human[7] = {0x1B80, 0x9AC0, 0x1CC0, 0x11C0, 0x0BC0, 0x26C0, 0x0740};
    constexpr std::size_t monster[7] = {0x6400, 0, 0, 0, 0, 0x03C0, 0};
    Plan plan{};

    // Starting at an unaligned owner offset consumes four blocks before partitioning.
    assert(MakePlan(0x1000, 100000, 1, human, 7, false, &plan));
    assert(plan.aligned_parent_offset == 4);
    assert(plan.slot_offset[0] == 4);
    assert(plan.slot_blocks[1] == 0x9AC0);
    assert(plan.rest_blocks + plan.partition_blocks + plan.aligned_parent_offset == 100000);

    // Exact fit succeeds; one block less rejects before any child view is emitted.
    const std::size_t total = 4 + plan.partition_blocks;
    assert(MakePlan(0x1000, total, 1, human, 7, false, &plan));
    assert(!MakePlan(0x1000, total - 1, 1, human, 7, false, &plan));
    assert(!MakePlan(0x1000, 100000, 100001, human, 7, false, &plan));

    // Monster slot 5 is intentionally adjacent to slot 0 within the same owner range.
    assert(MakePlan(0x1000, 0x67C0 + 4, 1, monster, 7, true, &plan));
    assert(plan.slot_offset[5] == plan.slot_offset[0] + 0x6400);
    assert(plan.slot_blocks[5] == 0x03C0);
    assert(plan.rest_blocks == 0);
    return 0;
}
