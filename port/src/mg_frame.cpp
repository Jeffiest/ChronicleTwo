#include <cstring>
#include "mg_frame.hpp"
#include <bit>
#include <cfloat>
#include <cmath>

#include "stubs/mg_frame.hpp"

namespace {

// test1 leaves the corners it transformed in VU0's vf10-vf17, where test2 reads them; on the host
// they travel through this array instead. The two calls always come as a pair.
float g_screen_corners[8][4];

// The vmulax/vmadday/vmaddaz/vmaddw chain, in its order of additions: out = v * matrix, with every
// product rounded separately (the port compiles with -ffp-contract=off).
void Apply(float out[4], const float matrix[4][4], const float v[4]) {
    float r[4];
    for (int i = 0; i < 4; i++) {
        r[i] = matrix[0][i] * v[0];
        r[i] = r[i] + matrix[1][i] * v[1];
        r[i] = r[i] + matrix[2][i] * v[2];
        r[i] = r[i] + matrix[3][i] * v[3];
    }
    for (int i = 0; i < 4; i++) {
        out[i] = r[i];
    }
}

// vmax and vmini order floats by their bits as sign and magnitude, so -0 sorts below +0.
u32 OrderKey(float value) {
    u32 bits = std::bit_cast<u32>(value);
    return (bits & 0x80000000u) != 0 ? ~bits : bits | 0x80000000u;
}

float Max(float a, float b) {
    return OrderKey(a) >= OrderKey(b) ? a : b;
}

float Min(float a, float b) {
    return OrderKey(a) <= OrderKey(b) ? a : b;
}

// The unit has no infinities: an overflow saturates at the largest finite value.
float Saturate(float value) {
    if (std::isinf(value)) {
        return std::copysign(FLT_MAX, value);
    }
    return value;
}

// vdiv: a zero (or denormal, which the unit flushes) divisor gives the largest value, signed by
// both operands.
float Div(float numerator, float denominator) {
    if (std::fabs(denominator) < FLT_MIN) {
        return std::signbit(numerator) != std::signbit(denominator) ? -FLT_MAX : FLT_MAX;
    }
    return Saturate(numerator / denominator);
}

float Mul(float a, float b) {
    return Saturate(a * b);
}

} // namespace

// Retail's test1: the combined transform is the matrix argument multiplied by the screen argument
// (matrix * screen), and the corners go through it row by row; the box over the eight results, w
// included, is the output, and the transformed corners stay behind for test2.
void test1(float (*corners)[4], float (*screen)[4], float (*matrix)[4], float *out_max, float *out_min) {
    float combined[4][4];

    for (int i = 0; i < 4; i++) {
        Apply(combined[i], screen, matrix[i]);
    }
    for (int i = 0; i < 8; i++) {
        Apply(g_screen_corners[i], combined, corners[i]);
    }

    float hi[4];
    float lo[4];
    for (int i = 0; i < 4; i++) {
        hi[i] = g_screen_corners[0][i];
        lo[i] = g_screen_corners[0][i];
    }
    for (int i = 1; i < 8; i++) {
        for (int j = 0; j < 4; j++) {
            hi[j] = Max(hi[j], g_screen_corners[i][j]);
            lo[j] = Min(lo[j], g_screen_corners[i][j]);
        }
    }
    for (int i = 0; i < 4; i++) {
        out_max[i] = hi[i];
        out_min[i] = lo[i];
    }
}

// Retail's test2: the eight corners test1 left in vf10-vf17 are divided through by the magnitude of
// their w, x and y only, as vmulq.xy writes, and the box around the results is the output.
void test2(float *out_max, float *out_min) {
    float hi[4];
    float lo[4];

    for (int i = 0; i < 8; i++) {
        float corner[4];
        float q = Div(1.0f, std::fabs(g_screen_corners[i][3]));
        corner[0] = Mul(g_screen_corners[i][0], q);
        corner[1] = Mul(g_screen_corners[i][1], q);
        corner[2] = g_screen_corners[i][2];
        corner[3] = g_screen_corners[i][3];

        if (i == 0) {
            for (int j = 0; j < 4; j++) {
                hi[j] = corner[j];
                lo[j] = corner[j];
            }
        } else {
            for (int j = 0; j < 4; j++) {
                hi[j] = Max(hi[j], corner[j]);
                lo[j] = Min(lo[j], corner[j]);
            }
        }
    }

    for (int i = 0; i < 4; i++) {
        out_max[i] = hi[i];
        out_min[i] = lo[i];
    }
}

// float* callers need four components, but need not have 16-byte alignment.
PORT_OVERRIDE void mgCFrame::GetWorldPosition0(float *out_position) {
    sceVu0FMATRIX world;
    GetLWMatrix(world);
    std::memcpy(out_position, world[3], sizeof(sceVu0FVECTOR));
}
