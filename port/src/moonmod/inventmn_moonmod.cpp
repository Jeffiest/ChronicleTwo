#include "common.h"

#include <span>

#include "dataread.hpp"
#include "inventmn.hpp"
#include "mg_memory.hpp"
#include "moonmod/native_events.hpp"
#include "stubs/inventmn.hpp"

// A private copy of the invention recipe table for MoonMod, loaded once from menu/inv6.lst.
CInventDataManage *MoonModGetNativeInventDataManage() {
    static CInventDataManage manager;
    static bool             loaded = false;
    alignas(16) static char recipe_file[0x7800];
    alignas(16) static u_long128 recipe_pool[0x4000];

    if (!loaded) {
        loaded = true;
        int file_size = 0;

        char path[] = "menu/inv6.lst";
        if (LoadFile2(path, std::span(reinterpret_cast<u8 *>(recipe_file), sizeof recipe_file), &file_size, 0) != 0 &&
            file_size > 0) {
            // The recipe parser allocates from InventTeigiStack; lend it this pool and give the menu's back.
            u_long128      *saved_stack = InventTeigiStack.stack;
            const int       saved_used = InventTeigiStack.stack_used;
            const int       saved_size = InventTeigiStack.stack_size;
            mgMEMORY_BLOCK *saved_block = InventTeigiStack.stack_block;
            InventTeigiStack.stSetBuffer(recipe_pool, 0x4000);
            manager.LoadAnalyzeInventFile(recipe_file, file_size);
            InventTeigiStack.stack = saved_stack;
            InventTeigiStack.stack_used = saved_used;
            InventTeigiStack.stack_size = saved_size;
            InventTeigiStack.stack_block = saved_block;
        }
    }

    return &manager;
}

void MoonModNotifyInventionCreated(int item_id) {
    INVENT_DATA_INFO *created = InventManagePt->GetInventDataInfoByItemID(item_id);
    int               ideas[3] = {0, 0, 0};
    if (created != NULL) {
        ideas[0] = created->neta_id[0];
        ideas[1] = created->neta_id[1];
        ideas[2] = created->neta_id[2];
    }
    moonmod::ChronicleTwoInventionCreated(item_id, ideas[0], ideas[1], ideas[2]);
}
