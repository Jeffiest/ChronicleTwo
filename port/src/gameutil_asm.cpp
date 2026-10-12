#include "common.h"

#include <cstring>

#include "gameutil.hpp"
#include "mg_math.hpp"
#include "stubs/gameutil.hpp"
#include "vu0_lanes.hpp"

// Retail clears OldSkinFrame after each skin pass; DeformMesh is its only caller, so clearing at the motion's
// first skin list starts every pass from the same state.
static mgCFrame *cached_skin_frame;

void testVUnew(float (*matrix)[4], float *vertex, float *weight, float *accum, float *out) {
    float transformed[4];
    vu0::Transform(transformed, matrix, vertex);

    float result[4];

    for (int lane = 0; lane < 3; lane++) {
        result[lane] = accum[lane] + transformed[lane] * weight[0];
    }

    // The xyz-masked vmulx leaves w holding matrix row 2 from the lqc2 above, and the full-width vadd adds that
    // into the accumulator's w.
    result[3] = accum[3] + matrix[2][3];

    std::memcpy(accum, result, sizeof(result));
    std::memcpy(out, result, sizeof(result));
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

    if (list == motion->skin_list) {
        cached_skin_frame = NULL;
    }

    if (list->key_count == 0) {
        return list->next;
    }

    bone = root->GetFrame(list->target);
    skin = root->GetFrame(list->frame);

    if (cached_skin_frame != skin) {
        cached_skin_frame = skin;
        vert = ((mgCVisualMDT *) skin->visual)->vertex;

        {
            const float origin[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            int         remaining;
            float (*accum)[4];

            remaining = frame_info[list->frame].vertex_count;
            accum = def_vrtx;

            for (; remaining > 0; remaining--, accum++) {
                std::memcpy(accum, origin, sizeof(origin));
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

        if (list->type == MOTION_KEY_SKIN_WEIGHTED) {
            testVUnew(deform, frame_info[list->frame].base_vertices[vertex], weight, def_vrtx[vertex], vert[vertex]);
        } else {
            sceVu0ApplyMatrix(moved, deform, frame_info[list->frame].base_vertices[vertex]);
            moved[3] = 0.0f;
            def_vrtx[vertex][0] += moved[0];
            def_vrtx[vertex][1] += moved[1];
            def_vrtx[vertex][2] += moved[2];
            def_vrtx[vertex][0] = vu0::Div(def_vrtx[vertex][0], def_vrtx[vertex][3]);
            def_vrtx[vertex][1] = vu0::Div(def_vrtx[vertex][1], def_vrtx[vertex][3]);
            def_vrtx[vertex][2] = vu0::Div(def_vrtx[vertex][2], def_vrtx[vertex][3]);
            sceVu0CopyVectorXYZ(vert[vertex], def_vrtx[vertex]);
            def_vrtx[vertex][3] += 1.0f;
        }
    }

    return list->next;
}

int CheckHit(CollisionInfo *collision, float *from, float *to, float *hit, int closest, int mask) {
    float   point[4];
    float   diff[4];
    float   poly_min[4];
    float   poly_max[4];
    float   seg_max[4];
    float   seg_min[4];
    float   offset[4];
    float   best_dist;
    float   d0;
    float   d1;
    float   dist;
    int     i;
    int     best;
    CCPoly *poly;
    int     count;
    int     has_best;

    if (collision == NULL) {
        return 0;
    }

    best = -1;
    has_best = false;
    mgVectorMaxMin(seg_max, seg_min, from, to);

    poly = collision->polys;
    count = collision->count;

    if (poly == NULL || count == 0) {
        return -1;
    }

    for (i = 0; i < count; i++, poly++) {
        if (poly->ignore_mask & mask) {
            continue;
        }

        mgVectorMaxMin(poly_max, poly_min, poly->vertex[0], poly->vertex[1], poly->vertex[2]);

        if (seg_max[0] < poly_min[0] || seg_max[1] < poly_min[1] || seg_max[2] < poly_min[2]) {
            continue;
        }

        if (!(seg_min[0] <= poly_max[0]) || !(seg_min[1] <= poly_max[1]) ||
            !(seg_min[2] <= poly_max[2])) {
            continue;
        }

        sceVu0SubVector(offset, from, poly->vertex[0]);
        d0 = sceVu0InnerProduct(poly->normal, offset);
        sceVu0SubVector(offset, to, poly->vertex[0]);
        d1 = sceVu0InnerProduct(poly->normal, offset);

        if (!(d0 <= 0.0f || d1 <= 0.0f)) {
            continue;
        }

        if (d0 < 0.0f && d1 < 0.0f) {
            continue;
        }

        if (mgIntersectionPoint_line_poly3(from, to, poly->vertex[0], poly->vertex[1],
                                           poly->vertex[2], poly->normal, point) == 0) {
            continue;
        }

        if (closest == 0) {
            best = i;
            sceVu0CopyVector(hit, point);
            break;
        }

        diff[0] = from[0] - point[0];
        diff[1] = from[1] - point[1];
        diff[2] = from[2] - point[2];
        dist = (diff[0] * diff[0]) + (diff[1] * diff[1]) + (diff[2] * diff[2]);

        if (!has_best) {
            best_dist = dist;
            best = i;
            sceVu0CopyVector(hit, point);
        } else if (!(best_dist <= dist)) {
            best_dist = dist;
            best = i;
            sceVu0CopyVector(hit, point);
        }

        has_best = true;
    }

    return best;
}

int CheckHits(CollisionInfo *info, float *from, float *to, int max_hits, int *hit_polys, float (*hit_points)[4], int sort, int ignore_mask) {
    float   point[4];
    float   poly_min[4];
    float   poly_max[4];
    float   seg_max[4];
    float   seg_min[4];
    float   offset[4];
    float   swap[4];
    float   d0;
    float   d1;
    int     i;
    int     j;
    int     hits;
    int     index;
    CCPoly *poly;
    int     count;

    hits = 0;
    mgVectorMaxMin(seg_max, seg_min, from, to);

    poly = info->polys;
    count = info->count;

    for (i = 0; i < count; i++, poly++) {
        if (poly->ignore_mask & ignore_mask) {
            continue;
        }

        mgVectorMaxMin(poly_max, poly_min, poly->vertex[0], poly->vertex[1], poly->vertex[2]);

        if (seg_max[0] < poly_min[0] || seg_max[1] < poly_min[1] || seg_max[2] < poly_min[2]) {
            continue;
        }

        if (!(seg_min[0] <= poly_max[0]) || !(seg_min[1] <= poly_max[1]) ||
            !(seg_min[2] <= poly_max[2])) {
            continue;
        }

        sceVu0SubVector(offset, from, poly->vertex[0]);
        d0 = sceVu0InnerProduct(poly->normal, offset);
        sceVu0SubVector(offset, to, poly->vertex[0]);
        d1 = sceVu0InnerProduct(poly->normal, offset);

        if (!(d0 <= 0.0f || d1 <= 0.0f)) {
            continue;
        }

        if (d0 < 0.0f && d1 < 0.0f) {
            continue;
        }

        if (mgIntersectionPoint_line_poly3(from, to, poly->vertex[0], poly->vertex[1],
                                           poly->vertex[2], poly->normal, point) == 0) {
            continue;
        }

        if (hits >= max_hits) {
            break;
        }

        hit_polys[hits] = i;
        sceVu0CopyVector(hit_points[hits], point);
        hit_points[hits][3] = mgDistVector(from, point);
        hits++;
    }

    if (sort == 0) {
        return hits;
    }

    // A descending sort was never written; retail sorts ascending for either sign.
    for (i = 0; i < hits - 1; i++) {
        for (j = i + 1; j < hits; j++) {
            if (!(hit_points[i][3] <= hit_points[j][3])) {
                index = hit_polys[i];
                hit_polys[i] = hit_polys[j];
                hit_polys[j] = index;
                sceVu0CopyVector(swap, hit_points[i]);
                sceVu0CopyVector(hit_points[i], hit_points[j]);
                sceVu0CopyVector(hit_points[j], swap);
            }
        }
    }

    return hits;
}
