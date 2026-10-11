#include <cstdio>
#include <cmath>
#include <algorithm>
#include "common.h"

#include <libvu0.h>

#include <cstdlib>
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
        // Weights index the palette by position in the bone list, as retail writes it, not by frame number.
        mgMulMatrix(skin_matrix[i], frame_matrix, slot_inverse);
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

    // The skinned normals come out wrong (clothes draw black) because the model's normals are indexed by
    // face rather than by vertex, so they keep their base pose until that mapping is done properly.
    // DC_SKIN_NORMALS=1 turns the broken skinning back on for comparison.
    static const bool skip_normals = std::getenv("DC_SKIN_NORMALS") == nullptr;

    float skin_matrix[32][4][4];

    for (int slot = 0; slot < 32; slot++) {
        sceVu0UnitMatrix(skin_matrix[slot]);
    }

    BuildSkinMatrices(visual, frame, slot_count, skin_matrix);

    // Diagnostic: DC_DUMP_DRAWS=1 prints the bone matrices' health every few skin passes.
    static const bool dump_skin = std::getenv("DC_DUMP_DRAWS") != nullptr;
    static int        skin_calls;
    static int        skin_printed;
    const bool        report = dump_skin && skin_printed < 160 && (skin_calls++ % 15) == 0;

    if (report) {
        skin_printed++;
        int  nonfinite = 0;
        char dets[256] = "";
        int  at = 0;
        for (int k = 0; k < slot_count; k++) {
            const float (*m)[4] = skin_matrix[k];
            float det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                        m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
            for (int r = 0; r < 4; r++) {
                for (int c = 0; c < 4; c++) {
                    nonfinite += !std::isfinite(m[r][c]);
                }
            }
            if (at < 230) {
                at += std::snprintf(dets + at, sizeof(dets) - at, " b%d=%d:%.2f", k, visual->bone[k], det);
            }
        }
        std::fprintf(stderr, "skin visual %p slots %d verts %d weights %d nonfinite %d dets%s\n", (const void *) visual,
                     slot_count, visual->vertex_num, visual->weight_num, nonfinite, dets);
    }

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

        // A vertex that no bone claims keeps its base position, as VU1 left it.
        if (out[0] == 0.0f && out[1] == 0.0f && out[2] == 0.0f && out[3] == 0.0f) {
            float total = 0.0f;
            for (int influence = 0; influence < 4; influence++) {
                total += vertex_weight.weight[influence];
            }
            if (total == 0.0f) {
                vu0::Copy(out, base_vertex[i]);
            }
        }

        vu0::Copy(visual->vertex[i], out);

        // Normals are indexed by face, not by vertex, so a model can hold fewer of them than vertices; the
        // ones past the end keep their base pose.
        if (base_normal != NULL && visual->normal != NULL && i < base.normal_num && !skip_normals) {
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

    if (report) {
        float lo[3] = {1e30f, 1e30f, 1e30f};
        float hi[3] = {-1e30f, -1e30f, -1e30f};
        int   bad = 0;
        for (int i = 0; i < visual->vertex_num; i++) {
            for (int k = 0; k < 3; k++) {
                float v = visual->vertex[i][k];
                if (!std::isfinite(v)) {
                    bad++;
                    continue;
                }
                lo[k] = std::min(lo[k], v);
                hi[k] = std::max(hi[k], v);
            }
        }
        float bo[3] = {1e30f, 1e30f, 1e30f};
        float bh[3] = {-1e30f, -1e30f, -1e30f};
        for (int i = 0; i < base.vertex_num; i++) {
            for (int k = 0; k < 3; k++) {
                bo[k] = std::min(bo[k], base.vertex[i * 4 + k]);
                bh[k] = std::max(bh[k], base.vertex[i * 4 + k]);
            }
        }
        std::fprintf(stderr,
                     "skin   out bbox [%.1f %.1f %.1f]..[%.1f %.1f %.1f] nonfinite %d   base bbox [%.1f %.1f %.1f]..[%.1f %.1f %.1f]\n",
                     lo[0], lo[1], lo[2], hi[0], hi[1], hi[2], bad, bo[0], bo[1], bo[2], bh[0], bh[1], bh[2]);
    }
}

} // namespace

/**
 * Skins every motion model in a frame's subtree, leaving the other visuals alone. A frame's own
 * matrix is the draw matrix A7's mg_visual.cpp will hand the visual.
 */
// A model loaded again into the same memory with the same vertex count would otherwise be skinned from
// the previous model's base pose, which is what an outfit change does.
void PortMgForgetMotionBase(const void *vertex) {
    g_motion_bases.erase(vertex);
}

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
