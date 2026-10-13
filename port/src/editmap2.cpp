#include "common.h"

#include "stubs/editmap2.hpp"

void PlaneNormalXZ(float *normal, float *p0, float *p1, float *p2) {
    const float edge0[4] = {p1[0] - p0[0], 0.0f, p1[2] - p0[2], 0.0f};
    const float edge1[4] = {p2[0] - p0[0], 0.0f, p2[2] - p0[2], 0.0f};

    normal[0] = edge0[1] * edge1[2] - edge1[1] * edge0[2];
    normal[1] = edge0[2] * edge1[0] - edge1[2] * edge0[0];
    normal[2] = edge0[0] * edge1[1] - edge1[0] * edge0[1];
    // vopmsub.xyz never writes w, so the retail store takes a stale lane; zero is the deterministic stand-in.
    normal[3] = 0.0f;
}
