#include "common.h"

#include "scenesnd.hpp"

// Retail indexes the stack array from a raw offset with the PS2's four-byte pointers; on the host
// the member itself is used. Everything else is the retail routine.
PORT_OVERRIDE void CScene::ClearStack(int index) {
    for (int i = index; i < stack_num; i++) {
        mgCMemory *memory = stack[i];

        if (memory != NULL) {
            memory->stack_used = 0;
            memory->lock = 0;

            if (index < i) {
                memory->stSetBuffer(NULL, 0);
            }
        }
    }
}
