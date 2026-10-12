#include "common.h"

#include <libvu0.h>

#include "gameutil.hpp"
#include "mg_frame.hpp"
#include "mg_math.hpp"
#include "mg_visual.hpp"
#include "vu0_ops.hpp"

// ps2/src/gameutil.cpp's skinning kernels lose half their bodies under PORT: testVUnew is inline
// VU0 assembly with no C path, and MotionProc2 leaves the deformation accumulators to VU0's vf0
// store. Both functions are replaced here with the same bodies carried in C, the way DC1's port
// replaced its gameutil skinning (dc1port-read/port/src/gameutil_motion.cpp). These paths deform
// the shadow models and the shape-animated models; the motion models skinned by VU1 weights are
// handled in port/src/dynamicanime.cpp.

namespace {

// The sizes MotionProc3 checks its counts against; retail declares def_vrtx[800] and def_nml[1]
// and overruns the latter, which the host must not. Counts over the bound are clamped.
constexpr int kMaxDeformVertices = 800;

mgCFrame *g_old_skin_frame;
float     g_deformed_vertex[kMaxDeformVertices][4];
float     g_deformed_normal[kMaxDeformVertices][4];

} // namespace

/**
 *
 * Accumulates a weighted transformed vertex and writes the resulting vertex. Retail is VU0 code:
 * vmulx.xyz leaves the product's w lane holding the matrix's third row w, which the add keeps.
 *
 */
void testVUnew(float (*matrix)[4], float *vertex, float *weight, float *accum, float *out) {
    float transformed[4];
    float result[4];

    vu0::Apply(transformed, matrix, vertex);

    result[0] = accum[0] + transformed[0] * weight[0];
    result[1] = accum[1] + transformed[1] * weight[0];
    result[2] = accum[2] + transformed[2] * weight[0];
    result[3] = accum[3] + matrix[2][3];

    vu0::Copy(accum, result);
    vu0::Copy(out, result);
}

Mot_List *MotionProc2(mgCFrame *root, tagMOTION_TYPE *motion, tagFRAME_INF *frame_info, Mot_List *list) {
    static sceVu0FVECTOR *vert;
    static sceVu0FMATRIX  tmp_SkinMatrix;
    static sceVu0FMATRIX  tmp_SkinMatrix_inv;
    static sceVu0FMATRIX  tmp_ChrMatrix;
    static sceVu0FMATRIX  tmp_BaseSkinMatrix;
    static sceVu0FMATRIX  tmp_BaseSkinMatrix_inv;
    float                 deform[4][4];
    float                 bone_matrix[4][4];
    float                 bone_in_skin[4][4];
    float                 skin_bone[4][4];
    float                 bone_in_skin_inv[4][4];
    float                 bone_base[4][4];
    float                 moved[4];
    float                 weight[4];
    mgCFrame             *bone;
    mgCFrame             *skin;
    unsigned int          i;
    int                   vertex;

    if (list->key_count == 0) {
        return list->next;
    }

    bone = root->GetFrame(list->target);
    skin = root->GetFrame(list->frame);

    if (g_old_skin_frame != skin) {
        g_old_skin_frame = root->GetFrame(list->frame);
        vert = ((mgCVisualMDT *) skin->visual)->vertex;

        {
            int    remaining;
            float (*accum)[4];

            remaining = frame_info[list->frame].vertex_count;

            if (remaining > kMaxDeformVertices) {
                remaining = kMaxDeformVertices;
            }

            accum = g_deformed_vertex;

            // vf0 always holds (0, 0, 0, 1); store it into each accumulator.
            for (; remaining > 0; remaining--, accum++) {
                accum[0][0] = 0.0f;
                accum[0][1] = 0.0f;
                accum[0][2] = 0.0f;
                accum[0][3] = 1.0f;
            }
        }

        skin->GetLWMatrix(tmp_SkinMatrix);
        root->GetLWMatrix(tmp_ChrMatrix);
        mgInversMatrix(tmp_SkinMatrix_inv, tmp_SkinMatrix);
        mgMulMatrix(tmp_BaseSkinMatrix, tmp_ChrMatrix, motion->base_matrices[list->frame]);
        mgInversMatrix(tmp_BaseSkinMatrix_inv, tmp_BaseSkinMatrix);
    }

    sceVu0UnitMatrix(bone_matrix);
    sceVu0UnitMatrix(tmp_SkinMatrix);
    bone->GetLWMatrix(bone_matrix);
    mgMulMatrix(bone_base, tmp_ChrMatrix, motion->base_matrices[list->target]);
    mgMulMatrix(bone_in_skin, tmp_BaseSkinMatrix_inv, bone_base);
    mgInversMatrix(bone_in_skin_inv, bone_in_skin);
    mgMulMatrix(skin_bone, tmp_SkinMatrix_inv, bone_matrix);
    mgMulMatrix(deform, skin_bone, bone_in_skin_inv);

    for (i = 0; i < list->key_count; i++) {
        weight[0] = 0.01f * list->values[i][0];
        vertex = list->key_frames[i];

        if (vertex < 0 || vertex >= kMaxDeformVertices) {
            continue;
        }

        if (list->type == MOTION_KEY_SKIN_WEIGHTED) {
            testVUnew(deform, frame_info[list->frame].base_vertices[vertex], weight, g_deformed_vertex[vertex],
                      vert[vertex]);
        } else {
            sceVu0ApplyMatrix(moved, deform, frame_info[list->frame].base_vertices[vertex]);
            moved[3] = 0.0f;
            g_deformed_vertex[vertex][0] += moved[0];
            g_deformed_vertex[vertex][1] += moved[1];
            g_deformed_vertex[vertex][2] += moved[2];
            g_deformed_vertex[vertex][0] /= g_deformed_vertex[vertex][3];
            g_deformed_vertex[vertex][1] /= g_deformed_vertex[vertex][3];
            g_deformed_vertex[vertex][2] /= g_deformed_vertex[vertex][3];
            sceVu0CopyVectorXYZ(vert[vertex], g_deformed_vertex[vertex]);
            g_deformed_vertex[vertex][3] += 1.0f;
        }
    }

    return list->next;
}

PORT_OVERRIDE Mot_List *MotionProc3(mgCFrame *root, tagMOTION_TYPE *motion, tagFRAME_INF *frame_info, Mot_List *list) {
    static sceVu0FVECTOR *vert;
    static sceVu0FVECTOR *nml;
    static sceVu0FMATRIX  tmp_SkinMatrix;
    static sceVu0FMATRIX  tmp_SkinMatrix_inv;
    static sceVu0FMATRIX  tmp_ChrMatrix;
    static sceVu0FMATRIX  tmp_BaseSkinMatrix;
    static sceVu0FMATRIX  tmp_BaseSkinMatrix_inv;
    float                 deform[4][4];
    float                 rotate[4][4];
    float                 bone_matrix[4][4];
    float                 bone_in_skin[4][4];
    float                 skin_bone[4][4];
    float                 bone_in_skin_inv[4][4];
    float                 bone_base[4][4];
    float                 normal[4];
    mgCFrame             *bone;
    mgCFrame             *skin;
    int                   vertex;
    unsigned int          i;

    if (list->type != MOTION_KEY_SKIN_WEIGHTED) {
        return list->next;
    }

    bone = root->GetFrame(list->target);
    skin = root->GetFrame(list->frame);

    if (g_old_skin_frame != skin) {
        g_old_skin_frame = root->GetFrame(list->frame);
        mgCVisualMDT *visual = (mgCVisualMDT *) skin->visual;
        vert = visual->vertex;
        nml = visual->normal;

        for (i = 0; i < frame_info[list->frame].vertex_count && i < kMaxDeformVertices; i++) {
            g_deformed_vertex[i][0] = 0.0f;
            g_deformed_vertex[i][1] = 0.0f;
            g_deformed_vertex[i][2] = 0.0f;
            g_deformed_vertex[i][3] = 1.0f;
        }

        for (unsigned int normal_index = 0;
             normal_index < frame_info[list->frame].normal_count && normal_index < kMaxDeformVertices;
             normal_index++) {
            g_deformed_normal[normal_index][0] = 0.0f;
            g_deformed_normal[normal_index][1] = 0.0f;
            g_deformed_normal[normal_index][2] = 0.0f;
            g_deformed_normal[normal_index][3] = 1.0f;
        }

        skin->attr->unk_28 = 1;
        skin->GetLWMatrix(tmp_SkinMatrix);
        root->GetLWMatrix(tmp_ChrMatrix);
        sceVu0InversMatrix(tmp_SkinMatrix_inv, tmp_SkinMatrix);
        mgMulMatrix(tmp_BaseSkinMatrix, tmp_ChrMatrix, motion->base_matrices[list->frame]);
        mgInversMatrix(tmp_BaseSkinMatrix_inv, tmp_BaseSkinMatrix);
    }

    sceVu0UnitMatrix(bone_matrix);
    sceVu0UnitMatrix(tmp_SkinMatrix);
    bone->GetLWMatrix(bone_matrix);
    mgMulMatrix(bone_base, tmp_ChrMatrix, motion->base_matrices[list->target]);
    mgMulMatrix(bone_in_skin, tmp_BaseSkinMatrix_inv, bone_base);
    mgInversMatrix(bone_in_skin_inv, bone_in_skin);
    mgMulMatrix(skin_bone, tmp_SkinMatrix_inv, bone_matrix);
    mgMulMatrix(deform, skin_bone, bone_in_skin_inv);

    sceVu0CopyMatrix(rotate, deform);
    rotate[3][0] = 0.0f;
    rotate[3][1] = 0.0f;
    rotate[3][2] = 0.0f;

    for (unsigned int index = 0; index < list->key_count; index++) {
        float weight[4] = {0.0f, 0.0f, 0.0f, 0.0f};

        weight[0] = 0.01f * list->values[index][0];

        if (!(weight[0] <= 0.0f)) {
            vertex = list->key_frames[index];

            if (vertex < 0 || vertex >= kMaxDeformVertices) {
                continue;
            }

            testVUnew(deform, frame_info[list->frame].base_vertices[vertex], weight, g_deformed_vertex[vertex],
                      vert[vertex]);
            sceVu0ApplyMatrix(normal, rotate, frame_info[list->frame].base_normals[vertex]);
            sceVu0InterVectorXYZ(nml[vertex], normal, frame_info[list->frame].base_normals[vertex], weight[0]);
        }
    }

    return list->next;
}
