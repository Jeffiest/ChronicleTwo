#pragma once

// PlaneNormalXZ is VU0 assembly, so PORT leaves out its definition in editmap2.cpp but not the call
// in GetEditPartsAlt. Only the y component survives the projection onto the horizontal plane: the
// cross product of (p1 - p0) and (p2 - p0), as the opmula/opmsub pair computes it.
inline void PlaneNormalXZ(float *normal, float *p0, float *p1, float *p2) {
    float ax = p1[0] - p0[0];
    float az = p1[2] - p0[2];
    float bx = p2[0] - p0[0];
    float bz = p2[2] - p0[2];
    normal[0] = 0.0f;
    normal[1] = az * bx - bz * ax;
    normal[2] = 0.0f;
}
