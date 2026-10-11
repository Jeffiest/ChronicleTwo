#include "common.h"

#include "dataread.hpp"
#include "effscript.hpp"

// Retail keeps the load buffer's address in an int, and the second file's offset arithmetic runs
// on that int: new (memory->Alloc()) CEffectScriptMan or not, on the host the high half of the
// pointer is lost and the file read lands low. The two buffers stay pointers here; everything
// else is the retail routine.
PORT_OVERRIDE int CEffectScriptMan::LoadBaseEffSpt(int base_no, mgCMemory *memory, int level) {
    char path[0x80];
    char pack[0x80];
    int  path_size;
    int  pack_size;
    char *path_buffer;
    char *pack_buffer;

    for (int i = 0; i < EFF_SPT_BASE_MAX; i++) {
        if (base[i] != 0 && base[i]->base_no == base_no) {
            return 0;
        }
    }

    path_buffer = (char *) load_buffer;

    if (path_buffer == NULL) {
        return -1;
    }

    if (GetNeedFilePath(base_no, path, pack) == 0) {
        return -1;
    }

    if (LoadFile2(path, path_buffer, &path_size, 0) == 0) {
        path_size = 0;
        pack_buffer = (char *) load_buffer;
        path_buffer = NULL;
    } else {
        int rest = path_size & 0x3F;
        int pad = rest != 0 ? 0x40 - rest : 0;
        pack_buffer = (char *) ((u_char *) path_buffer + ((path_size + pad) & -0x10));
    }

    if (LoadFile2(pack, pack_buffer, &pack_size, 0) == 0) {
        return -1;
    }

    return BuildBase(base_no, (u_long128 *) path_buffer, path_size, (u_long128 *) pack_buffer, pack_size, memory,
                     level);
}
