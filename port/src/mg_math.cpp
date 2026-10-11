#include "mg_math.hpp"

#include "vu0_ops.hpp"

// The vector unit's routines as C++. ps2/src writes these in assembly, which PORT
// leaves out; the rest of mg_math.cpp is C++ already. Where the unit's arithmetic
// differs from the host's the difference shows through: vmax and vmini order by
// sign and magnitude, a zero divisor gives the largest float rather than an
// infinity, and the box tests answer with the status register's sticky sign bit.

// The unit multiplies a matrix by two others in place, carrying the first product
// in registers across both passes, so the output may be one of the inputs.
void MulMatrix3(float (*matrix)[4], float (*second)[4], float (*third)[4]) {
    float r[4][4];
    float s[4][4];

    for (int i = 0; i < 4; i++) {
        vu0::Apply(r[i], matrix, second[i]);
    }
    for (int i = 0; i < 4; i++) {
        vu0::Apply(s[i], r, third[i]);
    }
    for (int i = 0; i < 4; i++) {
        vu0::Copy(matrix[i], s[i]);
    }
}

// vftoi4.xyzw: four 12.4 fixed-point lanes.
void mgFotI4(int *out, float *in) {
    for (int i = 0; i < 4; i++) {
        out[i] = vu0::Ftoi(in[i], 16.0f);
    }
}

// The unit builds each corner from a copy of the min or the max with one lane
// overwritten, so a corner carries its w from whichever of the two it copied.
void mgCreateBox8(float (*corners)[4], float *max, float *min) {
    vu0::Copy(corners[0], min);
    vu0::Copy(corners[7], max);

    vu0::Copy(corners[1], min);
    corners[1][0] = max[0];
    vu0::Copy(corners[2], min);
    corners[2][1] = max[1];
    vu0::Copy(corners[4], min);
    corners[4][2] = max[2];
    vu0::Copy(corners[6], max);
    corners[6][0] = min[0];
    vu0::Copy(corners[5], max);
    corners[5][1] = min[1];
    vu0::Copy(corners[3], max);
    corners[3][2] = min[2];
}

void mgZeroVectorW(float *vector) {
    vector[0] = 0.0f;
    vector[1] = 0.0f;
    vector[2] = 0.0f;
    vector[3] = 1.0f;
}

// The box tests subtract the corners pairwise on x, y and z and read the status
// register's sticky sign bit: the first negative result - a negative zero counts
// - keeps it set, and the function answers zero once it is.
int mgClipBoxVertex(float *point, float *max, float *min) {
    int status = 0;

    for (int i = 0; i < 3; i++) {
        status |= vu0::StatusFlags(max[i] - point[i]);
        status |= vu0::StatusFlags(point[i] - min[i]);
    }

    return (status & vu0::kStickySign) == 0;
}

int mgClipBox(float *max0, float *min0, float *max1, float *min1) {
    int status = 0;

    for (int i = 0; i < 3; i++) {
        status |= vu0::StatusFlags(max0[i] - min1[i]);
        status |= vu0::StatusFlags(max1[i] - min0[i]);
    }

    return (status & vu0::kStickySign) == 0;
}

int mgClipBoxW(float *max0, float *min0, float *max1, float *min1) {
    static const int lanes[3] = {0, 1, 3};
    int status = 0;

    for (int i = 0; i < 3; i++) {
        status |= vu0::StatusFlags(max0[lanes[i]] - min1[lanes[i]]);
        status |= vu0::StatusFlags(max1[lanes[i]] - min0[lanes[i]]);
    }

    return (status & vu0::kStickySign) == 0;
}

int mgClipInBox(float *max0, float *min0, float *max1, float *min1) {
    int status = 0;

    for (int i = 0; i < 3; i++) {
        status |= vu0::StatusFlags(max1[i] - max0[i]);
        status |= vu0::StatusFlags(min0[i] - min1[i]);
    }

    return (status & vu0::kStickySign) == 0;
}

int mgClipInBoxW(float *max0, float *min0, float *max1, float *min1) {
    static const int lanes[3] = {0, 1, 3};
    int status = 0;

    for (int i = 0; i < 3; i++) {
        status |= vu0::StatusFlags(max1[lanes[i]] - max0[lanes[i]]);
        status |= vu0::StatusFlags(min0[lanes[i]] - min1[lanes[i]]);
    }

    return (status & vu0::kStickySign) == 0;
}

void mgAddVector(float *vector, float *add) {
    for (int i = 0; i < 4; i++) {
        vector[i] += add[i];
    }
}

void mgSubVector(float *vector, float *sub) {
    for (int i = 0; i < 4; i++) {
        vector[i] -= sub[i];
    }
}

void mgVectorMin(float *min, float *a, float *b) {
    vu0::Min(min, a, b);
}

void mgVectorMin(float *min, float *a, float *b, float *c, float *d) {
    float r[4];

    vu0::Min(r, a, b);
    vu0::Min(r, r, c);
    vu0::Min(min, r, d);
}

void mgVectorMaxMin(float *max, float *min, float *a, float *b) {
    float hi[4];
    float lo[4];

    vu0::Max(hi, a, b);
    vu0::Min(lo, a, b);
    vu0::Copy(max, hi);
    vu0::Copy(min, lo);
}

void mgVectorMaxMin(float *max, float *min, float *a, float *b, float *c) {
    float hi[4];
    float lo[4];

    vu0::Max(hi, a, b);
    vu0::Min(lo, a, b);
    vu0::Max(hi, hi, c);
    vu0::Min(lo, lo, c);
    vu0::Copy(max, hi);
    vu0::Copy(min, lo);
}

void mgVectorMaxMin(float *max, float *min, float *a, float *b, float *c, float *d) {
    float hi[4];
    float lo[4];

    vu0::Max(hi, a, b);
    vu0::Min(lo, a, b);
    vu0::Max(hi, hi, c);
    vu0::Min(lo, lo, c);
    vu0::Max(hi, hi, d);
    vu0::Min(lo, lo, d);
    vu0::Copy(max, hi);
    vu0::Copy(min, lo);
}

// Both corners take part in both results, so a box that has grown past its own
// min still bounds correctly.
void mgBoxMaxMin(mgVu0FBOX *box, mgVu0FBOX *other) {
    float hi[4];
    float lo[4];

    vu0::Max(hi, box->max, box->min);
    vu0::Min(lo, box->max, box->min);
    vu0::Max(hi, hi, other->max);
    vu0::Min(lo, lo, other->max);
    vu0::Max(hi, hi, other->min);
    vu0::Min(lo, lo, other->min);
    vu0::Copy(box->max, hi);
    vu0::Copy(box->min, lo);
}

// Retail stores w from a register vopmsub never writes; nothing reads it, so it is zeroed.
void mgPlaneNormal(float *normal, float *v0, float *v1, float *v2) {
    float e0[4] = {v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2], 0.0f};
    float e1[4] = {v2[0] - v0[0], v2[1] - v0[1], v2[2] - v0[2], 0.0f};
    float r[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    vu0::Cross(r, e0, e1);
    vu0::Copy(normal, r);
}

float mgDistVector(float *vector) {
    return vu0::Sqrt(vu0::LengthSquared(vector));
}

// The horizontal length leaves y out: the unit rotates x onto z and adds the
// two squares, (x*x) + (z*z).
float mgDistVectorXZ(float *vector) {
    return vu0::Sqrt(vector[0] * vector[0] + vector[2] * vector[2]);
}

float mgDistVector2(float *vector) {
    return vu0::LengthSquared(vector);
}

float mgDistVector(float *a, float *b) {
    float d[4] = {b[0] - a[0], b[1] - a[1], b[2] - a[2], 0.0f};

    return vu0::Sqrt(vu0::LengthSquared(d));
}

float mgDistVectorXZ(float *a, float *b) {
    float x = b[0] - a[0];
    float z = b[2] - a[2];

    return vu0::Sqrt(x * x + z * z);
}

float mgDistVector2(float *a, float *b) {
    float d[4] = {b[0] - a[0], b[1] - a[1], b[2] - a[2], 0.0f};

    return vu0::LengthSquared(d);
}

float mgDistVectorXZ2(float *a, float *b) {
    float x = b[0] - a[0];
    float z = b[2] - a[2];

    return x * x + z * z;
}

void mgUnitMatrix(float (*matrix)[4]) {
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            matrix[i][j] = i == j ? 1.0f : 0.0f;
        }
    }
}

void mgZeroMatrix(float (*matrix)[4]) {
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            matrix[i][j] = 0.0f;
        }
    }
}

// product[i][j] = sum over k of left[i][k] * right[k][j]: right's columns are
// each transformed by left, and the product may alias either input.
void mgMulMatrix(float (*product)[4], float (*left_matrix)[4], float (*right_matrix)[4]) {
    float r[4][4];

    for (int i = 0; i < 4; i++) {
        vu0::Apply(r[i], left_matrix, right_matrix[i]);
    }
    for (int i = 0; i < 4; i++) {
        vu0::Copy(product[i], r[i]);
    }
}

// The unit's inverse: the cofactors of the upper 3x3 over its determinant, then
// the translation carried back through the result. It is not a rigid-transform
// inverse, so a scaled or sheared matrix inverts whole. Both the determinant and
// the cofactors accumulate in the unit's order; a singular matrix divides to the
// largest float rather than an infinity.
void mgInversMatrix(float (*inverse)[4], float (*matrix)[4]) {
    float r[4][4];
    float det;
    float q;

    r[0][0] = matrix[1][1] * matrix[2][2] - matrix[2][1] * matrix[1][2];
    r[0][1] = matrix[2][1] * matrix[0][2] - matrix[0][1] * matrix[2][2];
    r[0][2] = matrix[0][1] * matrix[1][2] - matrix[1][1] * matrix[0][2];
    r[1][0] = matrix[1][2] * matrix[2][0] - matrix[2][2] * matrix[1][0];
    r[1][1] = matrix[2][2] * matrix[0][0] - matrix[0][2] * matrix[2][0];
    r[1][2] = matrix[0][2] * matrix[1][0] - matrix[1][2] * matrix[0][0];
    r[2][0] = matrix[1][0] * matrix[2][1] - matrix[2][0] * matrix[1][1];
    r[2][1] = matrix[2][0] * matrix[0][1] - matrix[0][0] * matrix[2][1];
    r[2][2] = matrix[0][0] * matrix[1][1] - matrix[1][0] * matrix[0][1];

    det = matrix[0][0] * matrix[1][1] * matrix[2][2];
    det = det + matrix[2][0] * matrix[0][1] * matrix[1][2];
    det = det + matrix[1][0] * matrix[2][1] * matrix[0][2];
    det = det - matrix[0][0] * matrix[2][1] * matrix[1][2];
    det = det - matrix[1][0] * matrix[0][1] * matrix[2][2];
    det = det - matrix[2][0] * matrix[1][1] * matrix[0][2];

    q = vu0::Div(1.0f, det);

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            r[i][j] = vu0::Mul(r[i][j], q);
        }
        r[i][3] = 0.0f;
    }

    for (int i = 0; i < 3; i++) {
        r[3][i] = -(r[0][i] * matrix[3][0] + r[1][i] * matrix[3][1] + r[2][i] * matrix[3][2]);
    }
    r[3][3] = 1.0f;

    for (int i = 0; i < 4; i++) {
        vu0::Copy(inverse[i], r[i]);
    }
}

// The unit composes x, then y, then z: (rotation_z * rotation_y) * rotation_x.
PORT_OVERRIDE void mgRotMatrixXYZ(float (*matrix)[4], float *rotation) {
    sceVu0FMATRIX rotate_x;
    sceVu0FMATRIX rotate_y;

    mgRotMatrixX(rotate_x, rotation[0]);
    mgRotMatrixY(rotate_y, rotation[1]);
    mgRotMatrixZ(matrix, rotation[2]);
    MulMatrix3(matrix, rotate_y, rotate_x);
}

// The loop is a do-while on count - 1, so a count below one still transforms one
// vector, as retail does.
void mgApplyMatrixN(float (*out)[4], float (*matrix)[4], float (*in)[4], int count) {
    int i = 0;

    do {
        vu0::Apply(out[i], matrix, in[i]);
        i++;
    } while (i < count);
}

// The first vector is transformed ahead of the loop and the loop is a do-while on
// count - 2, so a count below two still transforms two vectors, as retail does.
void mgApplyMatrixN_MaxMin(float (*out)[4], float (*matrix)[4], float (*in)[4], int count, float *max, float *min) {
    float hi[4];
    float lo[4];
    int i;
    int remaining;

    vu0::Apply(out[0], matrix, in[0]);
    vu0::Copy(hi, out[0]);
    vu0::Copy(lo, out[0]);

    i = 1;
    remaining = count - 2;
    do {
        vu0::Apply(out[i], matrix, in[i]);
        vu0::Max(hi, hi, out[i]);
        vu0::Min(lo, lo, out[i]);
        i++;
        remaining--;
    } while (remaining >= 0);

    vu0::Copy(max, hi);
    vu0::Copy(min, lo);
}

// The unit loads the second vector into its bounds before the loop tests the run's
// length, and the loop compares it again on its first pass, so a run shorter than
// two still bounds the vector after the first.
void mgVectorMinMaxN(float *max, float *min, float (*vectors)[4], int count) {
    float hi[4];
    float lo[4];
    int last = count - 1;

    if (last < 1) {
        last = 1;
    }

    vu0::Copy(hi, vectors[0]);
    vu0::Copy(lo, vectors[0]);

    for (int i = 1; i <= last; i++) {
        vu0::Max(hi, hi, vectors[i]);
        vu0::Min(lo, lo, vectors[i]);
    }

    vu0::Copy(max, hi);
    vu0::Copy(min, lo);
}
