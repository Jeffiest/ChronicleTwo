#include "common.h"

#include <algorithm>
#include <cstdio>

#include "dataread.hpp"
#include "effscript.hpp"
#include "mg_memory.hpp"

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

int SetEffectScript(CRunScript *script, char *program, mgCMemory *memory);

namespace {
constexpr int Quadwords(std::size_t bytes) {
    return static_cast<int>((bytes + 15) / 16);
}
} // namespace

PORT_OVERRIDE _EFF_SCRIPT *CEffectScriptMan::CreateEffSpt(int base_no, int group, int register_in_group) {
    if (base_num <= 0) {
        std::printf("--- effect script (non base)!!! ---\n");
        return nullptr;
    }

    EFF_SPT_BASE *definition = nullptr;
    for (auto *candidate : base) {
        if (candidate != nullptr && candidate->base_no == base_no) {
            definition = candidate;
            break;
        }
    }
    if (definition == nullptr || work_memory == nullptr) {
        now = nullptr;
        std::printf("--- effect script (missing base or work stack)!!! ---\n");
        return nullptr;
    }

    int owner_slot = -1;
    if (register_in_group == 1) {
        if (group < 0 || group >= EFF_SPT_OWNER_MAX) {
            return nullptr;
        }
        for (int i = 0; i < EFF_SPT_OWNER_SLOT_MAX; ++i) {
            if (slot[group][i] == nullptr) {
                owner_slot = i;
                break;
            }
        }
        if (owner_slot < 0) {
            now = nullptr;
            std::printf("--- effect script (ent_tbl max)!!! ---\n");
            return nullptr;
        }
    }

    // Account for native object, interpreter-array and final 64-byte alignment sizes before
    // selecting a gap. Character frame/model copies already report their native quadword size.
    int required = Quadwords(sizeof(_EFF_SCRIPT)) + 2 + Quadwords(64 * sizeof(RS_STACKDATA)) +
                   Quadwords(2 * sizeof(RS_CALLDATA)) + 4;
    if (definition->chara != nullptr) {
        required += Quadwords(sizeof(CCharacter2)) + 2 + std::max(0, definition->chara->GetCopySize());
    }

    definition->work_size = required;
    u_long128 *token = work_memory->StartStackMode(MG_STACK_MODE_FIT, required);
    if (token == nullptr) {
        now = nullptr;
        std::printf("--- effect script work max[%d]!!! ---\n", work_memory->stack_size - work_memory->stack_used);
        return nullptr;
    }

    auto *effect = new (work_memory->Alloc(Quadwords(sizeof(_EFF_SCRIPT)) + 2)) _EFF_SCRIPT{};
    effect->work = token;
    effect->texb = definition->texb;
    effect->level = definition->level;
    if (definition->chara != nullptr) {
        effect->chara = new (work_memory->Alloc(Quadwords(sizeof(CCharacter2)) + 2)) CCharacter2;
        effect->chara->Initialize();
        definition->chara->Copy(*effect->chara, work_memory);
        effect->chara->SetPosition(0.0f, -10000.0f, 0.0f);
        effect->chara->SetRotation(0.0f, 0.0f, 0.0f);
    }
    SetEffectScript(&effect->run, definition->script, work_memory);
    effect->prog_no = 200;
    effect->user_id = group;
    effect->slot = owner_slot;
    effect->work_vect1[3] = effect->work_vect2[3] = 1.0f;
    effect->target_id = -1;
    work_memory->stAlign64();
    work_memory->EndStackMode();

    if (register_in_group == 1) {
        slot[group][owner_slot] = effect;
    }
    _EFF_SCRIPT *previous = nullptr;
    _EFF_SCRIPT *following = head;
    while (following != nullptr && following->texb <= effect->texb) {
        previous = following;
        following = following->next;
    }
    effect->prev = previous;
    effect->next = following;
    if (previous != nullptr) previous->next = effect;
    else head = effect;
    if (following != nullptr) following->prev = effect;
    else tail = effect;
    now = effect;
    return effect;
}
