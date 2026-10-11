#include "menudraw.hpp"

PORT_OVERRIDE void CRepairManager::Generate(int x, int y) {
    int            slot = -1;
    CRepairEffect *effect;

    for (int i = 0; i < 8; i++) {
        if (this->effect[i] == NULL) {
            slot = i;
            break;
        }
    }

    if (0 <= slot) {
        mgCMemory *memory = &this->effect_stack[slot];

        memory->stack_used = 0;
        memory->lock = 0;
        this->effect[slot] = new (memory->Alloc(5)) CRepairEffect;

        if ((effect = this->effect[slot]) != NULL) {
            effect->Initialize();
            effect->x = x;
            effect->y = y;
            effect->sub_tex = sub_tex;
            effect->tex = tex;
            effect->Generate(memory, 0x20);
        }
    }
}
