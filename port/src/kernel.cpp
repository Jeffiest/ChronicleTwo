#include <mw_runtime.h>

#include <cstddef>
#include <cstring>

#include "cameracontrol.hpp"
#include "mg_memory.hpp"
#include "vu0_ops.hpp"

// The odds and ends the link still asked for after the kernel and the SDK stubs landed: the two
// CodeWarrior runtime helpers the game calls by name, the two globals the retail image held but
// every decompiled unit only declares, and the collision triangle transform whose VU bodies
// ps2/src writes in assembly. The collision pair would sit better in a port/src/collision.cpp once
// that unit is replaced wholesale; it lives here until then.

/**
 *
 * The Metrowerks runtime's float-to-unsigned-integer helper, alongside fptosi in runtime.cpp.
 * MWCC used it for conversions its inline sequences could not express; the host cast truncates
 * toward zero exactly like the MIPS conversion it stands in for.
 *
 */
extern "C" unsigned int fptoui(float value) {
    return static_cast<unsigned int>(value);
}

/**
 *
 * CodeWarrior's placement-array constructor: the compiler allocates count * element_size + 16
 * bytes, this runs the constructor (when there is one) over each element and returns the first.
 * The leading quadword pair is the array's header - the element size and the count - which
 * `operator delete[]` reads back; the game's arrays come from its quadword stacks and are never
 * freed individually, but the layout must still match the runtime's.
 *
 */
extern "C" void *__construct_new_array(void *buffer, void *(*constructor)(void *), void *destructor, unsigned int element_size,
                                       int count) {
    (void) destructor;

    if (buffer == nullptr) {
        return nullptr;
    }

    unsigned int *header = static_cast<unsigned int *>(buffer);
    header[0] = element_size;
    header[1] = static_cast<unsigned int>(count);

    char *array = static_cast<char *>(buffer) + 16;

    if (constructor != nullptr) {
        for (int i = 0; i < count; i++) {
            constructor(array + static_cast<std::size_t>(i) * element_size);
        }
    }

    return array;
}

// The retail image carried both of these; the decompiled units only declare them (dng_object.hpp
// reads BuffWorkData__2, dng_main.hpp and sphida.cpp read EventCamera__2). Their sizes match the
// PS2 symbol table.
STATIC_ASSERT(sizeof(mgCMemory) == 0x30);
STATIC_ASSERT(sizeof(CCameraControl) == 0x1F0);

mgCMemory BuffWorkData__2;
CCameraControl EventCamera__2;

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
