#include "common.h"

#include "charasetup.hpp"
#include "character.hpp"
#include "mg_texture.hpp"
#include "mglib.hpp"

// The CPU skinning of the motion models lives in dynamicanime.cpp beside the dynamic animation,
// which poses the same models; the character's draw paths run it here just before the model is
// drawn. On the PS2 the skinning happened in VU1 as each model was drawn, and this function only
// remade the bounds.
void PortMgSkinMotionTree(mgCFrame *frame);

PORT_OVERRIDE void CCharacter2::DeleteExtMotion() {
    for (int i = 1; i < CHARA_MOTION_SET_MAX; i++) {
        motion[i].frame_info = NULL;
        shadow_motion[i].frame_info = NULL;
        key_list[i] = NULL;
        key_num[i] = 0;
        seq_list[i] = NULL;
        CHRINFO_KEY_SET *first = GetKeyListIndexPtr(0, NULL);

        if (first != NULL) {
            SetMotion(first->name, CHARA_MOTION_RESTART);
        }

        if (next_key != NULL) {
            posed_key = next_key;
            frame = (float) next_key->start_frame;
        }
    }

    mgCTextureManager *tex = &mgTexManager;
    int group = tex_anime_group_start;

    if (group > 0) {
        for (; group < tex_anime_group_num; group++) {
            tex->DeleteTexAnimeGroup(texture_block, group);
        }
    } else {
        tex->DeleteTexAnime(texture_block);
    }

    // Retail's this + 0x2C4 + j * 4 names images[j] only in the PS2 layout.
    for (int j = 1; j < CHARA_IMAGE_MAX; j++) {
        mgIMG_FILE_HEADER *archive = images[j];

        if (archive != NULL) {
            mgIMG_HEADER *image = (mgIMG_HEADER *) (archive + 1);

            for (u32 k = 0; k < archive->num3; k++) {
                if ((u8) image->name[0] != '#') {
                    tex->DeleteTexture(image->name, texture_block);
                }

                image++;
            }

            images[j] = NULL;
        }
    }

    for (int i = 1; i < CHARA_MOTION_SET_MAX; i++) {
        se_list[i] = NULL;
        se_num[i] = 0;
    }
}

PORT_OVERRIDE void CCharacter2::SetDeformMesh() {
    sceVu0FVECTOR max;
    sceVu0FVECTOR min;
    int           i;

    PortMgSkinMotionTree(CObjectFrame::frame);

    // A standalone level of detail is a model of its own, drawn in place of the main one and not
    // linked under it, so it is skinned here too.
    if (lod != NULL && lod_no >= 0 && lod_no < lod_num && lod[lod_no].standalone) {
        PortMgSkinMotionTree(lod[lod_no].frame);
    }

    for (i = 0; i < deform_frame_num; i++) {
        if (deform_frame[i] != NULL) {
            deform_frame[i]->RemakeBBox(max, min);
        }
    }
}

// charasetup.cpp's mem_table sizes the character stacks in PS2 16-byte blocks, but the host's
// mgCFrame and mgCFace are wider than the PS2's 0x110-byte frame and 3-block face. Monica's skin
// overran its 40000-block stack (stack over 40025/40000), mgCMemory::Alloc returned NULL and
// CreateFace dereferenced it. The table keeps retail's shape with every size doubled; the pool
// GetCharaMemAllocSize hands BuffCharacter grows from ~2.3 MB to ~4.6 MB, which the main buffer holds.
namespace {

const int host_mem_table[CHARA_MEM_LAYOUT_NUM][CHARA_MEM_STACK_NUM] = {
    {137000, 80000, 14000, 9300, 6000, 19800, 9000},
    {60000, 60000, 120000, 20000, 20000, 0, 0},
    {150000, 80000, 14000, 9300, 6000, 19800, 0},
    {240000, 0, 0, 0, 0, 11000, 0}
};

} // namespace

PORT_OVERRIDE int GetCharaMemAllocSize() {
    int maximum = 0;

    for (int row = 0; row < CHARA_MEM_LAYOUT_NUM; ++row) {
        int size = 0;

        for (int column = 0; column < CHARA_MEM_STACK_NUM; ++column) {
            size += host_mem_table[row][column];
        }

        if (size > maximum) {
            maximum = size;
        }
    }

    return maximum + 16;
}

PORT_OVERRIDE int GetCharaMemAllocPtr(mgCMemory *memory, mgCMemory *stacks, int chara_type, int edit_mode) {
    int row;
    int count;

    switch (chara_type) {
        case USER_CHARA_MAX:
        case USER_CHARA_MONICA:
            row = CHARA_MEM_LAYOUT_HUMAN;
            count = CHARA_MEM_STACK_NUM;

            if (edit_mode != 0) {
                row = CHARA_MEM_LAYOUT_EDIT;
            }

            break;
        case USER_CHARA_ROBO:
            row = CHARA_MEM_LAYOUT_ROBO;
            count = 5;
            break;
        case USER_CHARA_MONSTER:
            row = CHARA_MEM_LAYOUT_HUMAN;
            count = 6;
            break;
    }

    memory->stack_used = 0;
    memory->lock = false;
    int index;
    int size;

    for (index = 0; index < count; index++) {
        size = host_mem_table[row][index];

        if (size < 0) {
            break;
        }

        u_long128 *buffer = memory->stAllocTest(size);

        if (buffer == NULL) {
            return false;
        }

        stacks[index].stSetBuffer(buffer, size);
        stacks[index].stack_used = 0;
        stacks[index].lock = false;
        memory->stAlloc64(size);
    }

    return true;
}
