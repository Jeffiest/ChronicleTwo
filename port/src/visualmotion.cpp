#include "common.h"

#include <cstdint>

#include "mg_texture.hpp"
#include "mglib.hpp"
#include "visualmotion.hpp"

// Retail keeps the packet buffer's address in an int and hands the truncated value straight back
// in as the packet pointer; on the host that address lands low. The address stays a pointer here;
// only the DMA tag word, which the GS reads as a 32-bit address, keeps its low half.
int mgCVisualMotionMDT::DataAssignMotionMDT(MDT_HEADER *header, mgCVMotionData *data, mgCMemory *memory,
                                            mgCMemory *work, mgCTextureManager *textures) {
    mgCFace   *packet;
    int        count;
    int        part_count;
    FACES_ID  *source;
    int        i;
    u_long128 *address;
    int        size;
    MDT_FACES *section;

    if (work == NULL) {
        return false;
    }

    work->stack_used = 0;
    work->lock = 0;

    if (textures == NULL) {
        textures = &mgTexManager;
    }

    texture_manager = textures;
    CopyMDTDataPointer(header, memory);
    frame = data->frame;
    base_matrix = data->base_matrix;
    frame_id = data->frame_id;
    CreateVertexWeight(data->weight_data, data->frame_id, work);
    count = 0;

    do {
        if (bone[count] < 0) {
            break;
        }

        count++;
    } while (count < 0x20);

    vu1_base = count * 4 + 0x3C;
    vu1_offset = 0xB4 - (count * 4) / 2;
    mgCMemory scratch;
    size = work->stack_size - work->stack_used;
    scratch.stSetBuffer(work->stGetTop(), size);
    face_group = 0;
    section = (MDT_FACES *) ((u_char *) header + header->faces_ofs);
    part_count = section->prim_num;
    source = (FACES_ID *) (section + 1);

    for (i = 0; i < part_count; i++) {
        scratch.stack_used = 0;
        scratch.lock = 0;
        source = CreateFace(source, memory, &scratch, &packet);
        address = memory->stGetTop();
        size = CreateFaceMotionPacket((u_int *) address, packet, data);
        packet->packet_tag_word[0] = size | MG_DMA_REF;
        packet->packet_tag_word[1] = (u_int) (uintptr_t) address;
        packet->packet_tag_word[2] = 0;
        packet->packet_tag_word[3] = 0;
        memory->Alloc(size);
    }

    return true;
}
