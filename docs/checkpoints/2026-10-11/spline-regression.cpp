#include "sceneseq.hpp"

#include <cmath>
#include <cstdio>

static float paths[2][4][4] = {
    {{-692.07f, -0.36f, 990.78f, 1}, {-689.40f, -0.36f, 780.32f, 1},
     {-662.72f, -0.36f, 615.33f, 1}, {-627.87f, -0.36f, 503.76f, 1}},
    {{-633.39f, -0.17f, 1210.41f, 1}, {-698.31f, -0.17f, 1037.39f, 1},
     {-732.28f, -0.17f, 924.78f, 1}, {-729.93f, -0.17f, 758.07f, 1}}
};

static bool Endpoint(const C3DSpline &spline, const float *point) {
    for (int axis = 0; axis < 3; axis++) {
        if (!std::isfinite(spline.now_pos[axis]) || std::fabs(spline.now_pos[axis] - point[axis]) > 0.001f) {
            return false;
        }
    }
    return true;
}

int main() {
    for (int path = 0; path < 2; path++) {
        C3DSpline frames;
        int durations[4] = {19, 19, 19, 0};
        frames.SetUpSpline(paths[path], durations, 4, 0);
        int ticks = 0;
        while (!frames.Step() && ++ticks < 100) {
            for (float coordinate : frames.now_pos) {
                if (!std::isfinite(coordinate)) {
                    std::printf("FAIL path %d frame step %d is nonfinite\n", path, ticks);
                    return 1;
                }
            }
        }
        if (ticks != 57 || !Endpoint(frames, paths[path][3])) {
            std::printf("FAIL path %d frame completion at %d\n", path, ticks);
            return 1;
        }

        CCharaPas character;
        for (auto &point : paths[path]) {
            character.AddCharaPas(point);
        }
        character.SetFrame(58);
        character.Setup();
        if (!std::isfinite(character.spline.speed) || character.spline.speed <= 0) {
            std::printf("FAIL path %d setup speed %f\n", path, character.spline.speed);
            return 1;
        }
        ticks = 0;
        while (!character.spline.StepS() && ++ticks < 100) {}
        if (ticks >= 100 || !Endpoint(character.spline, paths[path][3])) {
            std::printf("FAIL path %d distance completion at %d\n", path, ticks);
            return 1;
        }
        std::printf("PASS path %d speed %.6f completed in %d distance steps\n", path, character.spline.speed, ticks + 1);
    }

    CCameraPas camera;
    camera.SetFrame(58);
    for (int point = 0; point < 4; point++) {
        camera.AddCameraPas(paths[0][point], paths[1][point]);
    }
    camera.Setup();
    if (!std::isfinite(camera.pos_spline.speed) || !std::isfinite(camera.ref_spline.speed)) {
        std::puts("FAIL camera setup speed");
        return 1;
    }
    camera.Run();
    float eye[4], look[4];
    for (int tick = 0; tick < 100 && !camera.CheckEnd(); tick++) {
        camera.Step(eye, look);
    }
    if (!camera.CheckEnd() || !Endpoint(camera.pos_spline, paths[0][3]) || !Endpoint(camera.ref_spline, paths[1][3])) {
        std::puts("FAIL camera completion");
        return 1;
    }
    std::puts("PASS camera position and reference paths");
    return 0;
}
