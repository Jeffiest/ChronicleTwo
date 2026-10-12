#pragma once

#include <cstddef>
#include <cstdint>

namespace port_menu_model_memory {

constexpr std::size_t kSlots = 7;
constexpr std::size_t kBlockBytes = 16;

struct Plan {
    std::size_t aligned_parent_offset;
    std::size_t partition_blocks;
    std::size_t rest_blocks;
    std::size_t slot_offset[kSlots];
    std::size_t slot_blocks[kSlots];
};

// Inputs and outputs are measured in 16-byte mgCMemory blocks. The caller owns the
// backing storage; this planner only describes bounded views into it.
inline bool MakePlan(std::uintptr_t parent_address, std::size_t parent_blocks,
                     std::size_t parent_used, const std::size_t *capacities,
                     std::size_t count, bool monster_alias, Plan *out) {
    if (out == nullptr || capacities == nullptr || count != kSlots || parent_used > parent_blocks) {
        return false;
    }
    if (parent_blocks > (UINTPTR_MAX - parent_address) / kBlockBytes) return false;
    const std::uintptr_t used_address = parent_address + parent_used * kBlockBytes;
    if (used_address < parent_address || used_address > UINTPTR_MAX - 63) {
        return false;
    }
    const std::uintptr_t aligned_address = (used_address + 63) & ~std::uintptr_t(63);
    if (aligned_address < parent_address || (aligned_address - parent_address) % kBlockBytes != 0) {
        return false;
    }
    const std::size_t offset = (aligned_address - parent_address) / kBlockBytes;
    if (offset > parent_blocks) {
        return false;
    }

    Plan plan{};
    plan.aligned_parent_offset = offset;
    std::size_t cursor = offset;
    if (monster_alias) {
        // Retail monster menus expose two intentional views into one 0x67c0-block region.
        if (capacities[0] != 0x6400 || capacities[5] != 0x03c0) return false;
        if (0x67c0 > parent_blocks - offset) return false;
        for (std::size_t i = 0; i < kSlots; ++i) {
            plan.slot_offset[i] = cursor;
            plan.slot_blocks[i] = 0;
        }
        plan.slot_offset[0] = cursor;
        plan.slot_blocks[0] = 0x6400;
        plan.slot_offset[5] = cursor + 0x6400;
        plan.slot_blocks[5] = 0x03c0;
        plan.partition_blocks = 0x67c0;
        cursor += plan.partition_blocks;
    } else {
        for (std::size_t i = 0; i < kSlots; ++i) {
            if (capacities[i] > parent_blocks - cursor) return false;
            plan.slot_offset[i] = cursor;
            plan.slot_blocks[i] = capacities[i];
            cursor += capacities[i];
        }
        plan.partition_blocks = cursor - offset;
    }
    if (cursor > parent_blocks) return false;
    plan.rest_blocks = parent_blocks - cursor;
    *out = plan;
    return true;
}

} // namespace port_menu_model_memory
