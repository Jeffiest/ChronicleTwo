#include "common.h"

#include "character.hpp"
#include "monster.hpp"
#include "runscript.hpp"
#include "runscript_opcodes.hpp"

static int GetStackInt(RS_STACKDATA *stack) {
    if (stack->type == RS_FLOAT) {
        return (int) stack->val.f;
    }

    return stack->val.i;
}

static void SetStack(RS_STACKDATA *stack, float value) {
    if (stack->type == RS_PTR) {
        stack->val.p->val.f = value;
    }
}

// Retail sums the entry address into an int; the uptr form of that sum in ps2/src does not compile identically under MWCC.
PORT_OVERRIDE int _GET_INDEXOBJ_SIZE(RS_STACKDATA *stack, int argument_count) {
    if (argument_count != 2) {
        return 0;
    }

    int object_index = GetStackInt(stack++);

    if (object_index == -1) {
        object_index = 0;
    }

    CHARA_ENTRY_OBJECT *object = &nowMonster->entry_object[object_index];
    SetStack(stack, object->size);
    return 1;
}
