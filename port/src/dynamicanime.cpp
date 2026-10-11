#include "common.h"

#include <libvu0.h>

#include <cstring>
#include <unordered_map>
#include <vector>

#include "dynamicanime.hpp"
#include "mg_dataset.hpp"
#include "mg_frame.hpp"
#include "mg_math.hpp"
#include "mg_visual.hpp"
#include "mglib.hpp"
#include "visualmotion.hpp"
#include "vu0_ops.hpp"

// On the PS2 a motion MDT is skinned by VU1 as it is drawn: each vertex carries up to four bone
// slots and weights, and the microprogram blends the bones' matrices into it. The port replaces
// that with CPU skinning here, in the same two places the game deforms its models: the dynamic
// animation poses the cloth and hair frames a character draws, and CCharacter2::SetDeformMesh
// runs right before a character's draw (port/src/character.cpp). Both call PortMgSkinMotionTree,
// which walks a model's frames and rewrites the vertices of every motion visual from its base
// pose, so the model draw A7's mg_visual.cpp already does reads the posed vertices.

namespace {

/**
 * The undeformed vertices and normals of one motion model, kept aside because the visual's own
 * arrays are overwritten in place with the skinned result every frame. Keyed by the vertex array
 * itself: two visuals copied from one model share their vertex data (mgCVisualMotionMDT::Copy),
 * and each draws immediately after its own skin pass, so one base serves both.
 */
struct MotionBase {
    int                vertex_num;
    int                normal_num;
    std::vector<float> vertex;
    std::vector<float> normal;
};

std::unordered_map<const void *, MotionBase> g_motion_bases;

MotionBase &MotionBaseOf(const mgCVisualMotionMDT *visual) {
    MotionBase &base = g_motion_bases[visual->vertex];

    if (base.vertex_num != visual->vertex_num || base.normal_num != visual->normal_num) {
        base.vertex_num = visual->vertex_num;
        base.normal_num = visual->normal_num;
        base.vertex.resize(base.vertex_num * 4);
        base.normal.resize(base.normal_num * 4);

        if (visual->vertex != NULL && base.vertex_num > 0) {
            std::memcpy(base.vertex.data(), visual->vertex, base.vertex_num * sizeof(sceVu0FVECTOR));
        }

        if (visual->normal != NULL && base.normal_num > 0) {
            std::memcpy(base.normal.data(), visual->normal, base.normal_num * sizeof(sceVu0FVECTOR));
        }
    }

    return base;
}

// The bone matrices CreateExtRenderInfoPacket sends to VU1, in the same order: each slot is
// inverse(frame) * bone_world * inverse(inverse(root_world * base[frame]) * root_world * base[bone]).
// The outputs are in the frame's local space, which is the space the visual's vertices live in.
void BuildSkinMatrices(const mgCVisualMotionMDT *visual, mgCFrame *frame, int slot_count,
                       float (*skin_matrix)[4][4]) {
    float world[4][4];
    float inverse_world[4][4];
    float root_matrix[4][4];
    float inverse_root[4][4];
    float root_local[4][4];

    frame->GetLWMatrix(world);
    mgInversMatrix(inverse_world, world);
    visual->frame[0]->GetLWMatrix(root_matrix);
    mgMulMatrix(inverse_root, root_matrix, visual->base_matrix[visual->frame_id]);
    mgInversMatrix(root_local, inverse_root);

    for (int i = 0; i < slot_count; i++) {
        int slot = visual->bone[i];
        float frame_matrix[4][4];
        float slot_matrix[4][4];
        float slot_inverse[4][4];

        if (slot < 0 || slot >= 32 || visual->frame[slot] == NULL) {
            continue;
        }

        visual->frame[slot]->GetLWMatrix(frame_matrix);
        mgMulMatrix(slot_matrix, root_matrix, visual->base_matrix[slot]);
        mgMulMatrix(slot_matrix, root_local, slot_matrix);
        mgInversMatrix(slot_inverse, slot_matrix);
        mgMulMatrix(frame_matrix, inverse_world, frame_matrix);
        mgMulMatrix(skin_matrix[slot], frame_matrix, slot_inverse);
    }
}

void SkinMotionVisual(mgCVisualMotionMDT *visual, mgCFrame *frame) {
    if (visual->vertex == NULL || visual->vertex_num <= 0) {
        return;
    }

    if (visual->frame == NULL || visual->base_matrix == NULL || visual->weight == NULL ||
        visual->weight_num <= 0) {
        return;
    }

    int slot_count = 0;

    while (slot_count < 32 && visual->bone[slot_count] >= 0) {
        slot_count++;
    }

    if (slot_count == 0) {
        return;
    }

    MotionBase &base = MotionBaseOf(visual);

    if (base.vertex_num <= 0 || base.vertex.empty()) {
        return;
    }

    const float (*base_vertex)[4] = reinterpret_cast<const float (*)[4]>(base.vertex.data());
    const float (*base_normal)[4] =
        base.normal_num > 0 ? reinterpret_cast<const float (*)[4]>(base.normal.data()) : NULL;

    float skin_matrix[32][4][4];

    for (int slot = 0; slot < 32; slot++) {
        sceVu0UnitMatrix(skin_matrix[slot]);
    }

    BuildSkinMatrices(visual, frame, slot_count, skin_matrix);

    // A vertex without weights keeps its base position, as VU1 left it.
    int count = visual->vertex_num < visual->weight_num ? visual->vertex_num : visual->weight_num;

    for (int i = 0; i < count; i++) {
        const mgVertexWeight &vertex_weight = visual->weight[i];
        float                 out[4] = {0.0f, 0.0f, 0.0f, 0.0f};

        for (int influence = 0; influence < 4; influence++) {
            float weight = vertex_weight.weight[influence];

            if (weight == 0.0f) {
                continue;
            }

            int slot = vertex_weight.matrix[influence] / 4;

            if (slot < 0 || slot >= 32) {
                continue;
            }

            float moved[4];
            vu0::Apply(moved, skin_matrix[slot], base_vertex[i]);

            for (int lane = 0; lane < 4; lane++) {
                out[lane] = out[lane] + moved[lane] * weight;
            }
        }

        vu0::Copy(visual->vertex[i], out);

        if (base_normal != NULL && visual->normal != NULL) {
            float normal[4] = {0.0f, 0.0f, 0.0f, 0.0f};

            for (int influence = 0; influence < 4; influence++) {
                float weight = vertex_weight.weight[influence];

                if (weight == 0.0f) {
                    continue;
                }

                int slot = vertex_weight.matrix[influence] / 4;

                if (slot < 0 || slot >= 32) {
                    continue;
                }

                float direction[4] = {base_normal[i][0], base_normal[i][1], base_normal[i][2], 0.0f};
                float moved[4];
                vu0::Apply(moved, skin_matrix[slot], direction);

                for (int lane = 0; lane < 3; lane++) {
                    normal[lane] = normal[lane] + moved[lane] * weight;
                }
            }

            vu0::Copy(visual->normal[i], normal);
        }
    }
}

} // namespace

/**
 * Skins every motion model in a frame's subtree, leaving the other visuals alone. A frame's own
 * matrix is the draw matrix A7's mg_visual.cpp will hand the visual.
 */
void PortMgSkinMotionTree(mgCFrame *frame) {
    if (frame == NULL) {
        return;
    }

    mgCVisual *visual = frame->visual;

    if (visual != NULL && visual->Iam() == MG_VISUAL_KIND_MOTION_MDT) {
        SkinMotionVisual(static_cast<mgCVisualMotionMDT *>(visual), frame);
    }

    for (mgCFrame *node = frame->child; node != NULL; node = node->brother) {
        PortMgSkinMotionTree(node);
    }
}

PORT_OVERRIDE int CDynamicAnime::DrawSub(int direct) {
    int sum = 0;

    for (int i = 0; i < draw_frame_num; i++) {
        mgCFrame *frame = GetDrawFrame(i);
        PortMgSkinMotionTree(frame);

        if (direct) {
            sum += mgDrawDirect(frame);
        } else {
            sum += mgDraw(frame);
        }
    }

    return sum;
}
