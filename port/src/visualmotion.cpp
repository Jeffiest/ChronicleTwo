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

// Native draws retain indices after retail scratch is reused. Reloading the same model
// replaces its set; the model arena retains its retail capacity.
std::unordered_map<const void *, std::vector<std::unique_ptr<int[]>>> kept_indices;

// The same for the skin weights: retail builds them in the scratch arena only to fill its VU1 packets, but
// the host skins from them every frame, and the arena is reused by the next model that loads.
std::unordered_map<const void *, std::unique_ptr<mgVertexWeight[]>> kept_weights;

} // namespace

// Native draws use CPU geometry and retained skin weights, without VIF packet allocation.
PORT_OVERRIDE int mgCVisualMotionMDT::DataAssignMotionMDT(MDT_HEADER *header, mgCVMotionData *data, mgCMemory *memory,
                                            mgCMemory *work, mgCTextureManager *textures) {
    mgCFace   *packet;
    int        count;
    int        part_count;
    FACES_ID  *source;
    int        i;
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
        // Native draws use retained indices and CPU skin weights, as the fixed-model path does.
        // No host consumer reads the VIF packet; emitting it wasted arena space and wrote before Alloc.
        packet->packet_tag = 0;

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

PORT_OVERRIDE int mgCVisualMotionMDT::CreateFaceMotionPacket(u_int *packet, mgCFace *face,
                                                           mgCVMotionData *motion) {
    (void) packet;
    (void) face;
    (void) motion;
    return 0;
}
