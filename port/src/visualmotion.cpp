#include "common.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

#include "mg_texture.hpp"
#include "mglib.hpp"
#include "visualmotion.hpp"

void PortMgForgetMotionBase(const void *vertex);

namespace {

// A motion model's face indices, kept per model. They cannot go back into the model's arena: that is
// sized for retail's layout, the copy overran it, and CreateFace does not check Alloc for failure.
// Loading a model again at the same address replaces its set, so the store stays bounded.
std::unordered_map<const void *, std::vector<std::unique_ptr<int[]>>> kept_indices;

// The same for the skin weights: retail builds them in the scratch arena only to fill its VU1 packets, but
// the host skins from them every frame, and the arena is reused by the next model that loads.
std::unordered_map<const void *, std::unique_ptr<mgVertexWeight[]>> kept_weights;

} // namespace

// Retail keeps the packet buffer's address in an int and hands the truncated value straight back
// in as the packet pointer; on the host that address lands low. The address stays a pointer here;
// only the DMA tag word, which the GS reads as a 32-bit address, keeps its low half.
PORT_OVERRIDE int mgCVisualMotionMDT::DataAssignMotionMDT(MDT_HEADER *header, mgCVMotionData *data, mgCMemory *memory,
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
    kept_indices[this].clear();
    kept_weights.erase(this);

    if (textures == NULL) {
        textures = &mgTexManager;
    }

    texture_manager = textures;
    CopyMDTDataPointer(header, memory);
    PortMgForgetMotionBase(vertex);
    frame = data->frame;
    base_matrix = data->base_matrix;
    frame_id = data->frame_id;
    CreateVertexWeight(data->weight_data, data->frame_id, work);

    if (weight != NULL && weight_num > 0) {
        auto copy = std::make_unique<mgVertexWeight[]>(weight_num);

        std::memcpy(copy.get(), weight, (size_t) weight_num * sizeof(mgVertexWeight));
        weight = copy.get();
        kept_weights[this] = std::move(copy);
    }
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

        // Retail rebuilds each part's indices in scratch and drops them once the packet exists; the host
        // draws from them later, by which time the next part and the skin weights have overwritten scratch.
        if (packet->index_num > 0) {
            auto keep = std::make_unique<int[]>(packet->index_num);

            std::memcpy(keep.get(), packet->index, (size_t) packet->index_num * sizeof(int));
            packet->index = keep.get();
            kept_indices[this].push_back(std::move(keep));
        }
    }

    return true;
}
