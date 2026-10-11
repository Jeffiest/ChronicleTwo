#include "common.h"

#include <bit>
#include <cfloat>
#include <cmath>

#include "occlusion.hpp"

// The ps2 unit computes its planes through the vector unit's helpers (mgApplyMatrixN, mgVectorMin
// and mgPlaneNormal in mg_math.cpp, then libvu0's normalise). The port spells the same maths out
// in scalars: the occluder runs for every placed part of a map, so its result must not depend on
// how the VU's replacements round or order their operands.

namespace {

u_int OrderKey(float value) {
    u_int bits = std::bit_cast<u_int>(value);
    return (bits & 0x80000000u) != 0 ? ~bits : bits | 0x80000000u;
}

// vmini and vmax order floats by sign and magnitude, so -0 sorts below +0.
float Min(float a, float b) {
    return OrderKey(a) <= OrderKey(b) ? a : b;
}

// mgApplyMatrixN: one column of the matrix per output lane.
void ApplyMatrix(float *out, const float (*matrix)[4], const float *in) {
    for (int lane = 0; lane < 4; lane++) {
        out[lane] = matrix[0][lane] * in[0] + matrix[1][lane] * in[1] + matrix[2][lane] * in[2] +
                    matrix[3][lane] * in[3];
    }
}

// mgPlaneNormal: (v1 - v0) x (v2 - v0); the unit's cross leaves w alone, so it is zeroed.
void PlaneNormal(float *normal, const float *v0, const float *v1, const float *v2) {
    float e0[4] = {v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2], 0.0f};
    float e1[4] = {v2[0] - v0[0], v2[1] - v0[1], v2[2] - v0[2], 0.0f};

    normal[0] = e0[1] * e1[2] - e0[2] * e1[1];
    normal[1] = e0[2] * e1[0] - e0[0] * e1[2];
    normal[2] = e0[0] * e1[1] - e0[1] * e1[0];
    normal[3] = 0.0f;
}

// sceVu0Normalize: a zero-length vector normalises to zero (the unit's divide gives its largest
// value, which the zero operands scale away), and w is cleared.
void Normalize(float *out, const float *in) {
    float length = std::sqrt(in[0] * in[0] + in[1] * in[1] + in[2] * in[2]);
    float scale = length == 0.0f ? FLT_MAX : 1.0f / length;

    out[0] = in[0] * scale;
    out[1] = in[1] * scale;
    out[2] = in[2] * scale;
    out[3] = 0.0f;
}

float Dot(const float *a, const float *b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

} // namespace

PORT_OVERRIDE void COcclusion::Setup(float (*view_matrix)[4]) {
    float points[4][4];
    float origin[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    if (enable == 0) {
        return;
    }

    setup = 1;

    for (int i = 0; i < 4; i++) {
        ApplyMatrix(points[i], view_matrix, vertex[i]);
    }

    // mgVectorMin of the four corners, every lane, so z is the nearest depth.
    for (int lane = 0; lane < 4; lane++) {
        float value = Min(points[0][lane], points[1][lane]);
        value = Min(value, points[2][lane]);
        view_min[lane] = Min(value, points[3][lane]);
    }

    float *p1 = points[1];
    float *p2 = points[2];
    float *p3 = points[3];

    PlaneNormal(plane, p2, p1, points[0]);
    Normalize(plane, plane);
    plane[3] = -Dot(plane, points[0]);

    // The plane faces away from the eye; when it does not, the corners wind the other way and
    // every plane is rebuilt from the other side.
    if (!(plane[3] <= 0.0f)) {
        PlaneNormal(plane, points[0], p1, p2);
        Normalize(plane, plane);
        plane[3] = -Dot(plane, points[0]);
        PlaneNormal(side_plane[0], origin, p1, p2);
        PlaneNormal(side_plane[1], origin, p3, points[0]);
        PlaneNormal(side_plane[2], origin, points[0], p1);
        PlaneNormal(side_plane[3], origin, p2, p3);
    } else {
        PlaneNormal(side_plane[0], origin, p2, p1);
        PlaneNormal(side_plane[1], origin, points[0], p3);
        PlaneNormal(side_plane[2], origin, p1, points[0]);
        PlaneNormal(side_plane[3], origin, p3, p2);
    }

    Normalize(side_plane[0], side_plane[0]);
    side_plane[0][3] = 0;
    Normalize(side_plane[1], side_plane[1]);
    side_plane[1][3] = 0;
    Normalize(side_plane[2], side_plane[2]);
    side_plane[2][3] = 0;
    Normalize(side_plane[3], side_plane[3]);
    side_plane[3][3] = 0;
}

PORT_OVERRIDE int COcclusion::CheckSphere(float *sphere) {
    if (enable == 0 || setup == 0) {
        return 0;
    }

    if (!(view_min[2] <= sphere[2] - sphere[3])) {
        return 0;
    }

    float distance = Dot(plane, sphere);
    distance += plane[3];

    if (distance < sphere[3]) {
        return 0;
    }

    if (Dot(side_plane[0], sphere) < sphere[3]) {
        return 0;
    }

    if (Dot(side_plane[1], sphere) < sphere[3]) {
        return 0;
    }

    if (Dot(side_plane[2], sphere) < sphere[3]) {
        return 0;
    }

    if (Dot(side_plane[3], sphere) < sphere[3]) {
        return 0;
    }

    return 1;
}
