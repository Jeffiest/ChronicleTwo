#include "common.h"

#include <cmath>
#include <cstdio>

#include "dataread.hpp"
#include "dng_event.hpp"

#include "dng_main.hpp"
#include "monster.hpp"
#include "monster_memory.hpp"
#include "scenesnd.hpp"
#include "snd_mngr.hpp"

// Retail's definition returns void against the fixed-up declaration (scripts/port/header_fixups.py).
PORT_OVERRIDE int CRedMarkModel::Draw() {
    float position[4];
    float saved_position[4];

    if (draw_request != 0) {
        GetPosition(position);
        GetPosition(saved_position);
        position[1] += 2.0f * sinf(angle);
        SetPosition(position);
        CObjectFrame::DrawDirect();
        SetPosition(saved_position);
        draw_request = false;
    }

    return 0;
}

PORT_OVERRIDE void LoadMonsterFile() {
    CScene *scene = DngMainScene;
    if (ActiveMonster == nullptr) {
        return;
    }

    ActiveMonster->Initialize(scene);
    scene->AssignStack(3);
    scene->ClearStack(3);
    mgCMemory *memory = scene->GetStack(3);
    if (memory == nullptr) {
        return;
    }

    PortSetMonsterBuffers(ActiveMonster->memory, MONSTER_ACTIVE_MAX, *memory);
    sndInitPort(5);

    const int stage_id = DngSaveDataDungeon->stage_id;
    CMonsterLocateInfo *locate = &ActiveMonster->locate;
    locate->num = 0;
    locate->put_num = 0;
    locate->put_flag = 0;

    for (int index = 0; index < MONSTER_LOCATE_MAX; ++index) {
        locate->param[index] = -1;
        locate->monster_id[index] = -1;
    }

    char path[76];
    int size;
    std::sprintf(path, "dungeon/cfg_file/mos_place%d.cfg", stage_id);
    LoadFile(path, BuffReadData, &size);
    CreatMonsterFloorInfo(reinterpret_cast<char *>(BuffReadData), size);
    const int monster_count = locate->num;

    for (int entry = 0; entry < monster_count; ++entry) {
        const int monster_id = locate->monster_id[entry];
        if (ActiveMonster->SearchBaseIndex(monster_id) < 0) {
            ActiveMonster->EntryRefer(monster_id, memory);
        }
    }
}

PORT_OVERRIDE void LoadMonsterFile(int monster_id, int initialize) {
    CScene *scene = DngMainScene;
    if (ActiveMonster == nullptr) {
        return;
    }

    mgCMemory *memory;
    if (initialize != 0) {
        ActiveMonster->Initialize(scene);
        scene->AssignStack(3);
        scene->ClearStack(3);
        memory = scene->GetStack(3);
        if (memory == nullptr) {
            return;
        }

        // The retail member offset and 48-byte arena stride belong to the PS2 layout.
        PortSetMonsterBuffers(ActiveMonster->memory, MONSTER_ACTIVE_MAX, *memory);
        sndInitPort(5);
    } else {
        memory = scene->GetStack(3);
        if (memory == nullptr) {
            return;
        }
    }

    if (ActiveMonster->SearchBaseIndex(monster_id) < 0) {
        ActiveMonster->EntryRefer(monster_id, memory);
    }
}
