#include "common.h"

#include <cstdio>
#include <cstring>

#include "log/game_output.hpp"
#include "savedatadungeon.hpp"

/**
 *
 * Number of saved floor records in each dungeon.
 *
 */
static short limmit_table[SAVE_DUNGEON_NUM] = {9, 16, 25, 21, 23, 29, 39};

DNG_FLOOR_SAVE *CSaveDataDungeon::GetFloorInfoPtr(int dungeon, int floor) {
    // Floors registered by mods resolve to the save extension records; native floors keep retail's layout.
    if (DNG_FLOOR_SAVE *mod_floor = GetMoonModDungeonSaveRecord(this, dungeon, floor)) {
        return mod_floor;
    }

    if (dungeon < 0 || dungeon >= SAVE_DUNGEON_NUM) {
        return nullptr;
    }

    if (floor < 0) {
        return nullptr;
    }

    // Floors past the dungeon's retail count take the port's extra records, laid out after retail's 162 in
    // SAVE_DUNGEON_FLOOR_EXTRA records per dungeon, so a retail-count save keeps every record where it was.
    if (floor >= limmit_table[dungeon]) {
        if (floor - limmit_table[dungeon] >= SAVE_DUNGEON_FLOOR_EXTRA) {
            return nullptr;
        }

        return &floor_info[SAVE_DUNGEON_RETAIL_FLOOR_NUM + dungeon * SAVE_DUNGEON_FLOOR_EXTRA + (floor - limmit_table[dungeon])];
    }

    int index = 0;

    for (int i = 0; i < dungeon; i++) {
        index += limmit_table[i];
    }

    index += floor;

    return &floor_info[index];
}

void CSaveDataDungeon::Initialize() {
    ResetMoonModDungeonSaveExtension(this);
    memset(floor_info, 0, sizeof(floor_info));

    for (int i = 0; i < SAVE_DUNGEON_NUM; i++) {
        DNG_FLOOR_SAVE *info = GetFloorInfoPtr(i, 0);

        if (info) {
            info->visit_count = 1;
            info->flag = 3;
        }

        info = GetFloorInfoPtr(i, 1);

        if (info) {
            info->flag = 1;
        }
    }

    for (int i = 0; i < SAVE_DUNGEON_NUM; i++) {
        prev_floor_id[i] = -1;
    }

    for (int i = 0; i < SAVE_DUNGEON_NUM; i++) {
        floor_id[i] = 1;
    }

    stage_id = 0;
}

void CSaveDataDungeon::SetFloorID(int floor) {
    prev_floor_id[stage_id] = floor_id[stage_id];
    floor_id[stage_id] = floor;
    dc2::log::PrintGame("[%d] FLOOR :::    %d ----->> %d \n", stage_id, prev_floor_id[stage_id], floor);
}
