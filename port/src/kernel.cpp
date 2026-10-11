#include <mw_runtime.h>

#include <cstddef>
#include <cstring>

#include "cameracontrol.hpp"
#include "mg_memory.hpp"
#include "vu0_ops.hpp"

// The remaining host definitions: the collision triangle transform whose VU bodies ps2/src writes
// in assembly. The collision pair would sit better in a port/src/collision.cpp once that unit is
// replaced wholesale; it lives here until then.

STATIC_ASSERT(sizeof(mgCMemory) == 0x30);
STATIC_ASSERT(sizeof(CCameraControl) == 0x1F0);

namespace {

// pre_trance_normal parks its matrix in VU0 registers for the trance_normal calls that follow; on
// the host it parks it in memory.
float g_pick_matrix[4][4];

} // namespace

void pre_trance_normal(float (*matrix)[4]) {
    std::memcpy(g_pick_matrix, matrix, sizeof(g_pick_matrix));
}

// Transforms a triangle's corners in place by the matrix pre_trance_normal loaded and writes the
// unnormalised normal of the transformed triangle. Retail loads all three corners from p0 onwards
// (the caller's corners are contiguous) but stores them through p0, p1 and p2. The plane's w is a
// lane vopmsub never writes; it is zeroed.
void trance_normal(float *v0, float *v1, float *v2, float *normal) {
    float a[4];
    float b[4];
    float c[4];
    vu0::Apply(a, g_pick_matrix, v0);
    vu0::Apply(b, g_pick_matrix, v0 + 4);
    vu0::Apply(c, g_pick_matrix, v0 + 8);

    float e0[4] = {b[0] - a[0], b[1] - a[1], b[2] - a[2], b[3] - a[3]};
    float e1[4] = {c[0] - a[0], c[1] - a[1], c[2] - a[2], c[3] - a[3]};
    float n[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    vu0::Cross(n, e0, e1);

    vu0::Copy(v0, a);
    vu0::Copy(v1, b);
    vu0::Copy(v2, c);
    vu0::Copy(normal, n);
}
