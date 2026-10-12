#include "common.h"

#include <cmath>

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
