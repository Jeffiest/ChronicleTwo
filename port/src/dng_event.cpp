#include "dng_event.hpp"

#include "dng_main.hpp"
#include "monster.hpp"
#include "monster_memory.hpp"
#include "scenesnd.hpp"
#include "snd_mngr.hpp"

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
