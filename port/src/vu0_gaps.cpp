#include "common.h"

#include <libvu0.h>

#include "collision.hpp"
#include "gameutil.hpp"
#include "mg_frame.hpp"
#include "mg_math.hpp"

// Functions ps2/src's guard leaves out of the port whole, with nothing else in port/src to stand in
// for them yet. All but mgNormalizeVector are VU0 assembly: reaching one is a port gap and says so.

int CCollisionMDT::PickUpNearPoly(CCPoly *out, const mgVu0FBOX &box, int max) {
    PS2_UNIMPLEMENTED();
}

int ClipBoxXZ(float *max_a, float *min_a, float *max_b, float *min_b) {
    PS2_UNIMPLEMENTED();
}

void mgNormalizeVector(float *out, float *in, float length) {
    sceVu0FVECTOR unit;

    sceVu0Normalize(unit, in);
    sceVu0ScaleVector(out, unit, length);
}

int CheckHit(CollisionInfo *collision, float *from, float *to, float *hit, int closest, int mask) {
    PS2_UNIMPLEMENTED();
}

int CheckHits(CollisionInfo *info, float *from, float *to, int max_hits, int *hit_polys, float (*hit_points)[4], int sort, int ignore_mask) {
    PS2_UNIMPLEMENTED();
}

int mgInsideScreen(float (*corners)[4], float (*matrix)[4], float *out_max, float *out_min) {
    PS2_UNIMPLEMENTED();
}

int mgCFrame::GetDrawRect(mgVu0FBOX *rect, mgCDrawManager *manager) {
    PS2_UNIMPLEMENTED();
}
