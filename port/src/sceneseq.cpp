#include "sceneseq.hpp"

#include "mg_math.hpp"

namespace {

void SampleSpline(const SPLINE_KEY &key, float frame, float *out) {
    // The final marker has zero duration. Its constant polynomial is the endpoint;
    // evaluating 0/0 on the host would poison the path's measured length.
    if (key.length == 0) {
        for (int axis = 0; axis < 3; axis++) {
            out[axis] = key.d[axis];
        }
        return;
    }

    const float time = (frame - key.frame) / key.length;
    const float square = time * time;
    const float cube = square * time;

    for (int axis = 0; axis < 3; axis++) {
        out[axis] = cube * key.a[axis] + square * key.b[axis] + time * key.c[axis] + key.d[axis];
    }
}

int FinishSpline(C3DSpline &spline) {
    spline.now_key = spline.key_num - 1;
    const SPLINE_KEY &last = spline.key[spline.now_key];
    for (int axis = 0; axis < 3; axis++) {
        spline.now_pos[axis] = last.d[axis];
    }
    spline.now_frame = last.frame + last.length + 100.0f;
    return 1;
}

} // namespace

PORT_OVERRIDE int C3DSpline::Step() {
    if (key_num < 2) {
        return 1;
    }

    now_frame += 1.0f;

    if (!(now_frame < key[now_key].frame + key[now_key].length)) {
        now_key++;

        if (now_key >= key_num) {
            return FinishSpline(*this);
        }
    }

    SampleSpline(key[now_key], now_frame, now_pos);
    return 0;
}

PORT_OVERRIDE int C3DSpline::StepS() {
    float previous[4];
    float next[4];

    if (key_num < 2 || speed <= 0.0f) {
        return 1;
    }

    do {
        now_frame += 0.001f;

        if (!(now_frame < key[now_key].frame + key[now_key].length)) {
            now_key++;

            if (now_key >= key_num) {
                return FinishSpline(*this);
            }
        }

        for (int axis = 0; axis < 3; axis++) {
            previous[axis] = now_pos[axis];
        }
        SampleSpline(key[now_key], now_frame, next);
        previous[3] = 1.0f;
        next[3] = 1.0f;
    } while (mgDistVector(previous, next) < speed);

    for (int axis = 0; axis < 3; axis++) {
        now_pos[axis] = next[axis];
    }
    return 0;
}
