#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include "common.h"

#include <libvu0.h>

#include "collision.hpp"
#include "gameutil.hpp"
#include "visualmotion.hpp"
#include "mg_drawenv.hpp"
#include "mg_frame.hpp"
#include "mg_math.hpp"
#include "mglib.hpp"
#include "vu0_ops.hpp"

// Functions ps2/src's guard leaves out of the port whole. Each is VU0 assembly in retail, or a C++
// body that was compiled together with register preloads; here they are plain float code, with the
// unit's arithmetic (vu0_ops.hpp) where it shows through: saturating divides, ordered vmax/vmini
// and the sticky-sign box tests. Not tagged PORT_OVERRIDE: ps2/src already leaves them out under
// PORT, and the strip step refuses a definition it finds inside that conditional.

namespace {

// Eight corners through a matrix, x and y divided by |w| as the unit does (a zero w gives the
// largest float, not an infinity), and the box around the results across all four lanes: z stays
// the transformed z and w the transformed w, which callers read as depth.
void ProjectBox(float *out_max, float *out_min, const float (*matrix)[4], const float (*corners)[4]) {
    float hi[4];
    float lo[4];

    for (int i = 0; i < 8; i++) {
        float p[4];

        vu0::Apply(p, matrix, corners[i]);

        float q = vu0::Div(1.0f, std::fabs(p[3]));

        p[0] = vu0::Mul(p[0], q);
        p[1] = vu0::Mul(p[1], q);

        if (i == 0) {
            vu0::Copy(hi, p);
            vu0::Copy(lo, p);
        } else {
            vu0::Max(hi, hi, p);
            vu0::Min(lo, lo, p);
        }
    }

    vu0::Copy(out_max, hi);
    vu0::Copy(out_min, lo);
}

} // namespace

int CCollisionMDT::PickUpNearPoly(CCPoly *out, const mgVu0FBOX &box, int max) {
    sceVu0FVECTOR box_max = {box.max[0], box.max[1], box.max[2], 1.0f};
    sceVu0FVECTOR box_min = {box.min[0], box.min[1], box.min[2], 1.0f};
    sceVu0FVECTOR poly_max;
    sceVu0FVECTOR poly_min;
    int           count = 0;

    if (poly == NULL) {
        return 0;
    }

    // Written as the negated tests retail uses so a NaN bound rejects.
    if (!(box.min[0] <= bbox.max[0]) || !(box.min[1] <= bbox.max[1]) || !(box.min[2] <= bbox.max[2])) {
        return 0;
    }

    if (box.max[0] < bbox.min[0] || box.max[1] < bbox.min[1] || box.max[2] < bbox.min[2]) {
        return 0;
    }

    CCPoly *polygon = poly;

    for (int i = 0; i < poly_count; i++, polygon++) {
        mgVectorMaxMin(poly_max, poly_min, polygon->vertex[0], polygon->vertex[1], polygon->vertex[2]);

        if (mgClipBox(box_max, box_min, poly_max, poly_min)) {
            sceVu0CopyVector(out->vertex[0], polygon->vertex[0]);
            sceVu0CopyVector(out->vertex[1], polygon->vertex[1]);
            sceVu0CopyVector(out->vertex[2], polygon->vertex[2]);
            out->ground_kind = polygon->ground_kind;
            out->foot_sound = polygon->foot_sound;
            out->area_kind = polygon->area_kind;
            out->ignore_mask = polygon->ignore_mask;
            out->parts_no = polygon->parts_no;
            out->attr = polygon->attr;
            out->attr_value = polygon->attr_value;
            sceVu0CopyVector(out->normal, polygon->normal);
            max--;
            count++;
            out++;

            // The first match is stored before the limit is looked at, so a limit of zero still yields one.
            if (max <= 0) {
                break;
            }
        }
    }

    return count;
}

// Overlap on x and z only: the sticky sign bit after both subtractions of the pair on those lanes.
int ClipBoxXZ(float *max_a, float *min_a, float *max_b, float *min_b) {
    int status = 0;

    for (int lane = 0; lane < 3; lane += 2) {
        status |= vu0::StatusFlags(max_a[lane] - min_b[lane]);
        status |= vu0::StatusFlags(max_b[lane] - min_a[lane]);
    }

    return (status & vu0::kStickySign) == 0;
}

void mgNormalizeVector(float *out, float *in, float length) {
    sceVu0FVECTOR unit;

    sceVu0Normalize(unit, in);
    sceVu0ScaleVector(out, unit, length);
}

int CheckHit(CollisionInfo *collision, float *from, float *to, float *hit, int closest, int mask) {
    float   point[4];
    float   poly_min[4];
    float   poly_max[4];
    float   seg_max[4];
    float   seg_min[4];
    float   offset[4];
    float   best_dist = 0.0f;
    int     best = -1;
    bool    has_best = false;
    CCPoly *poly;

    if (collision == NULL) {
        return 0;
    }

    mgVectorMaxMin(seg_max, seg_min, from, to);
    poly = collision->polys;

    if (poly == NULL || collision->count == 0) {
        return -1;
    }

    for (int i = 0; i < collision->count; i++, poly++) {
        if (poly->ignore_mask & mask) {
            continue;
        }

        mgVectorMaxMin(poly_max, poly_min, poly->vertex[0], poly->vertex[1], poly->vertex[2]);

        if (seg_max[0] < poly_min[0] || seg_max[1] < poly_min[1] || seg_max[2] < poly_min[2]) {
            continue;
        }

        if (!(seg_min[0] <= poly_max[0]) || !(seg_min[1] <= poly_max[1]) || !(seg_min[2] <= poly_max[2])) {
            continue;
        }

        // Both ends strictly behind the plane cannot cross it, and one end must not be in front.
        sceVu0SubVector(offset, from, poly->vertex[0]);
        float d0 = sceVu0InnerProduct(poly->normal, offset);
        sceVu0SubVector(offset, to, poly->vertex[0]);
        float d1 = sceVu0InnerProduct(poly->normal, offset);

        if (!(d0 <= 0.0f || d1 <= 0.0f)) {
            continue;
        }

        if (d0 < 0.0f && d1 < 0.0f) {
            continue;
        }

        if (mgIntersectionPoint_line_poly3(from, to, poly->vertex[0], poly->vertex[1], poly->vertex[2], poly->normal,
                                           point) == 0) {
            continue;
        }

        if (closest == 0) {
            best = i;
            sceVu0CopyVector(hit, point);
            break;
        }

        float dx = from[0] - point[0];
        float dy = from[1] - point[1];
        float dz = from[2] - point[2];
        float dist = (dx * dx) + (dy * dy) + (dz * dz);

        if (!has_best || !(best_dist <= dist)) {
            best_dist = dist;
            best = i;
            sceVu0CopyVector(hit, point);
        }

        has_best = true;
    }

    return best;
}

int CheckHits(CollisionInfo *info, float *from, float *to, int max_hits, int *hit_polys,
                            float (*hit_points)[4], int sort, int ignore_mask) {
    float   point[4];
    float   poly_min[4];
    float   poly_max[4];
    float   seg_max[4];
    float   seg_min[4];
    float   offset[4];
    float   swap[4];
    int     hits = 0;
    CCPoly *poly;

    mgVectorMaxMin(seg_max, seg_min, from, to);
    poly = info->polys;

    for (int i = 0; i < info->count; i++, poly++) {
        if (poly->ignore_mask & ignore_mask) {
            continue;
        }

        mgVectorMaxMin(poly_max, poly_min, poly->vertex[0], poly->vertex[1], poly->vertex[2]);

        if (seg_max[0] < poly_min[0] || seg_max[1] < poly_min[1] || seg_max[2] < poly_min[2]) {
            continue;
        }

        if (!(seg_min[0] <= poly_max[0]) || !(seg_min[1] <= poly_max[1]) || !(seg_min[2] <= poly_max[2])) {
            continue;
        }

        sceVu0SubVector(offset, from, poly->vertex[0]);
        float d0 = sceVu0InnerProduct(poly->normal, offset);
        sceVu0SubVector(offset, to, poly->vertex[0]);
        float d1 = sceVu0InnerProduct(poly->normal, offset);

        if (!(d0 <= 0.0f || d1 <= 0.0f)) {
            continue;
        }

        if (d0 < 0.0f && d1 < 0.0f) {
            continue;
        }

        if (mgIntersectionPoint_line_poly3(from, to, poly->vertex[0], poly->vertex[1], poly->vertex[2], poly->normal,
                                           point) == 0) {
            continue;
        }

        if (hits >= max_hits) {
            break;
        }

        // w carries the distance for the sort and is left in the caller's array.
        hit_polys[hits] = i;
        sceVu0CopyVector(hit_points[hits], point);
        hit_points[hits][3] = mgDistVector(from, point);
        hits++;
    }

    // Retail never wrote a descending order: a negative sort orders ascending like a positive one.
    if (sort != 0) {
        for (int i = 0; i < hits - 1; i++) {
            for (int j = i + 1; j < hits; j++) {
                if (!(hit_points[i][3] <= hit_points[j][3])) {
                    int index = hit_polys[i];

                    hit_polys[i] = hit_polys[j];
                    hit_polys[j] = index;
                    sceVu0CopyVector(swap, hit_points[i]);
                    sceVu0CopyVector(hit_points[i], hit_points[j]);
                    sceVu0CopyVector(hit_points[j], swap);
                }
            }
        }
    }

    return hits;
}

// The matrix goes through the screen transform first, as the unit composes them (matrix x screen),
// so each corner is transformed once.
int mgInsideScreen(float (*corners)[4], float (*matrix)[4], float *out_max, float *out_min) {
    mgRENDER_INFO *render_info = &mgRenderInfo;
    float          combined[4][4];

    for (int i = 0; i < 4; i++) {
        vu0::Apply(combined[i], render_info->world_screen_rel, matrix[i]);
    }

    ProjectBox(out_max, out_min, combined, corners);
    return mgClipBoxW(out_max, out_min, render_info->screen_box_max, render_info->screen_box_min);
}

// Diagnostic: DC_DUMP_DRAWS=1 prints the rectangle each top-level GetDrawRect returns.
static int LogDrawRect(const mgCFrame *frame, mgVu0FBOX *rect, int visible, int depth) {
    // Diagnostic: DC_WIDE_DRAWRECT=1 grows every top-level rectangle by 60 pixels, to see whether the outline
    // composite is cutting parts off.
    static const bool widen = std::getenv("DC_WIDE_DRAWRECT") != nullptr;
    if (widen && depth == 1 && visible) {
        for (int k = 0; k < 2; k++) {
            rect->max[k] += 60.0f;
            rect->min[k] -= 60.0f;
        }
    }

    static const bool dump = std::getenv("DC_DUMP_DRAWS") != nullptr;
    static int        printed;

    if (dump && depth == 1 && printed < 600) {
        printed++;
        sceVu0FMATRIX lw;
        const_cast<mgCFrame *>(frame)->GetLWMatrixTopBottom(lw);
        std::fprintf(stderr, "  lw rot rows [%.2f %.2f %.2f] [%.2f %.2f %.2f] [%.2f %.2f %.2f] det3 %.2f\n", lw[0][0], lw[0][1],
                     lw[0][2], lw[1][0], lw[1][1], lw[1][2], lw[2][0], lw[2][1], lw[2][2],
                     lw[0][0] * (lw[1][1] * lw[2][2] - lw[1][2] * lw[2][1]) -
                         lw[0][1] * (lw[1][0] * lw[2][2] - lw[1][2] * lw[2][0]) +
                         lw[0][2] * (lw[1][0] * lw[2][1] - lw[1][1] * lw[2][0]));
        std::fprintf(stderr, "  frame data use_srt %d rot_type %d rotation [%.4f %.4f %.4f] scale [%.3f %.3f %.3f] position [%.2f %.2f %.2f]\n",
                     (int) frame->use_srt, (int) frame->rot_type, frame->rotation[0], frame->rotation[1],
                     frame->rotation[2], frame->scale[0], frame->scale[1], frame->scale[2], frame->position[0],
                     frame->position[1], frame->position[2]);
        const mgCVisual *visual = frame->visual;
        std::fprintf(stderr,
                     "drawrect frame %p vis %d max [%.1f %.1f %.1f %.1f] min [%.1f %.1f %.1f %.1f] visual %p kind %d "
                     "world [%.1f %.1f %.1f]\n",
                     (const void *) frame, visible, rect->max[0], rect->max[1], rect->max[2], rect->max[3],
                     rect->min[0], rect->min[1], rect->min[2], rect->min[3], (const void *) visual,
                     visual != NULL ? (int) const_cast<mgCVisual *>(visual)->Iam() : -1, lw[3][0], lw[3][1], lw[3][2]);

        if (visual != NULL) {
            int kind = (int) const_cast<mgCVisual *>(visual)->Iam();
            if (kind == MG_VISUAL_KIND_MOTION_MDT || kind == MG_VISUAL_KIND_FIX_MDT) {
                const mgCVisualMDT *mdt = static_cast<const mgCVisualMDT *>(visual);
                float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
                double sum = 0.0;
                for (int i = 0; i < mdt->vertex_num; i++) {
                    for (int k = 0; k < 3; k++) {
                        lo[k] = std::min(lo[k], mdt->vertex[i][k]);
                        hi[k] = std::max(hi[k], mdt->vertex[i][k]);
                    }
                    sum += mdt->vertex[i][0] * 1.0 + mdt->vertex[i][1] * 3.0 + mdt->vertex[i][2] * 7.0;
                }
                const void *weight = kind == MG_VISUAL_KIND_MOTION_MDT
                                         ? (const void *) static_cast<const mgCVisualMotionMDT *>(visual)->weight
                                         : NULL;
                std::fprintf(stderr,
                             "  visual %p verts %d vertex %p weight %p sum %.2f bbox [%.1f %.1f %.1f]..[%.1f %.1f %.1f]\n",
                             (const void *) visual, mdt->vertex_num, (const void *) mdt->vertex, weight, sum, lo[0],
                             lo[1], lo[2], hi[0], hi[1], hi[2]);
            }
        }
    }
    return visible;
}

int mgCFrame::GetDrawRect(mgVu0FBOX *rect, mgCDrawManager *manager) {
    static int depth;
    struct Depth {
        Depth() { depth++; }
        ~Depth() { depth--; }
    } depth_guard;
    sceVu0FMATRIX  lw;
    sceVu0FVECTOR  rect_max = {0.0f, 0.0f, 0.0f, 0.0f};
    sceVu0FVECTOR  rect_min = {0.0f, 0.0f, 0.0f, 0.0f};
    sceVu0FMATRIX  screen_matrix;
    mgVu0FBOX      child_rect;
    mgRENDER_INFO *info;
    int            visible;
    int            billboard = MG_FRAME_BILLBOARD_NONE;
    // Retail points an absent attribute at a zeroed one: not visible, children not skipped.
    int            draw = attr != NULL ? attr->draw : 0;

    if (manager == NULL) {
        manager = &mgDrawManager;
    }

    info = manager->render_info;

    if (attr != NULL) {
        billboard = attr->billboard;
    }

    if (billboard != MG_FRAME_BILLBOARD_NONE) {
        GetBBoardMatrix(billboard, lw, info);
    } else {
        GetLWMatrixTopBottom(lw);
    }

    visible = (draw & MG_FRAME_DRAW_VISIBLE) != 0;

    if (visual == NULL || bound == NULL) {
        visible = 0;
    }

    if (visible) {
        mgMulMatrix(screen_matrix, info->world_screen_rel, lw);
        ProjectBox(rect_max, rect_min, screen_matrix, bound->corner);

        // The box counts only when it overlaps the screen, whose coordinates are relative to its
        // centre, and does not lie behind the near plane; it is then moved to screen coordinates.
        visible = 0;

        float left = 0.5f * -mgScreenWidth;
        float top = 0.5f * -mgScreenHeight;
        float right = left + mgScreenWidth;
        float bottom = top + mgScreenHeight;

        if (rect_min[0] <= right && rect_max[0] >= left && rect_min[1] <= bottom && rect_max[1] >= top &&
            rect_max[3] >= info->clip_min[2]) {
            visible = 1;
            rect_min[0] += mgScreenWidth / 2;
            rect_max[0] += mgScreenWidth / 2;
            rect_min[1] += mgScreenHeight / 2;
            rect_max[1] += mgScreenHeight / 2;
        }
    }

    if (visible) {
        sceVu0CopyVector(rect->max, rect_max);
        sceVu0CopyVector(rect->min, rect_min);
    }

    if (draw & MG_FRAME_DRAW_SKIP_CHILDREN) {
        return LogDrawRect(this, rect, visible, depth);
    }

    for (mgCFrame *frame = child; frame != NULL; frame = frame->brother) {
        if (frame->attr != NULL && (frame->attr->draw & MG_FRAME_DRAW_SKIP_BY_PARENT)) {
            continue;
        }

        if (frame->GetDrawRect(&child_rect, NULL)) {
            if (!visible) {
                sceVu0CopyVector(rect_max, child_rect.max);
                sceVu0CopyVector(rect_min, child_rect.min);
            } else {
                mgVectorMaxMin(rect_max, rect_min, rect_max, rect_min, child_rect.max, child_rect.min);
            }

            visible = 1;
        }
    }

    sceVu0CopyVector(rect->max, rect_max);
    sceVu0CopyVector(rect->min, rect_min);
    return LogDrawRect(this, rect, visible, depth);
}
