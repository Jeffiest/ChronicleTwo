#include <gtest/gtest.h>

#include "occlusion.hpp"

namespace {

// A 2x2 occluder at z = 10 seen from the eye at the origin. Its winding picks one of Setup's two
// plane branches; both must hide the same cone.
void SetQuad(COcclusion &occluder, bool woundToTheEye) {
    const float to_face[4][4] = {
        {-1.0f, -1.0f, 10.0f, 1.0f},
        {1.0f, -1.0f, 10.0f, 1.0f},
        {1.0f, 1.0f, 10.0f, 1.0f},
        {-1.0f, 1.0f, 10.0f, 1.0f},
    };
    const float to_back[4][4] = {
        {-1.0f, -1.0f, 10.0f, 1.0f},
        {-1.0f, 1.0f, 10.0f, 1.0f},
        {1.0f, 1.0f, 10.0f, 1.0f},
        {1.0f, -1.0f, 10.0f, 1.0f},
    };

    occluder.enable = 1;

    for (int corner = 0; corner < 4; corner++) {
        for (int lane = 0; lane < 4; lane++) {
            occluder.vertex[corner][lane] = woundToTheEye ? to_face[corner][lane] : to_back[corner][lane];
        }
    }
}

void Identity(float (*matrix)[4]) {
    for (int column = 0; column < 4; column++) {
        for (int row = 0; row < 4; row++) {
            matrix[column][row] = column == row ? 1.0f : 0.0f;
        }
    }
}

// The occluder's cone, in view space: the quad spans x, y in [-1, 1] at z = 10, so at z = 20 the
// cone spans x, y in [-2, 2].
TEST(Occlusion, SphereBehindThePlaneIsHidden) {
    for (bool woundToTheEye : {true, false}) {
        COcclusion occluder;
        SetQuad(occluder, woundToTheEye);
        float view[4][4];
        Identity(view);
        occluder.Setup(view);

        float sphere[4] = {0.5f, 0.5f, 20.0f, 0.5f};
        EXPECT_EQ(occluder.CheckSphere(sphere), 1) << "woundToTheEye=" << woundToTheEye;
    }
}

TEST(Occlusion, SphereInFrontOfThePlaneIsNotHidden) {
    COcclusion occluder;
    SetQuad(occluder, true);
    float view[4][4];
    Identity(view);
    occluder.Setup(view);

    float sphere[4] = {0.5f, 0.5f, 5.0f, 0.5f};
    EXPECT_EQ(occluder.CheckSphere(sphere), 0);
}

TEST(Occlusion, SphereOutsideASidePlaneIsNotHidden) {
    COcclusion occluder;
    SetQuad(occluder, true);
    float view[4][4];
    Identity(view);
    occluder.Setup(view);

    float beside[4] = {3.0f, 0.5f, 20.0f, 0.5f};
    EXPECT_EQ(occluder.CheckSphere(beside), 0);

    float above[4] = {0.5f, -3.0f, 20.0f, 0.5f};
    EXPECT_EQ(occluder.CheckSphere(above), 0);
}

TEST(Occlusion, SphereNearerThanTheCornersIsNotHidden) {
    COcclusion occluder;
    SetQuad(occluder, true);
    float view[4][4];
    Identity(view);
    occluder.Setup(view);

    // z - r = 9.9 is nearer than the quad's nearest corner at z = 10.
    float sphere[4] = {0.5f, 0.5f, 10.4f, 0.5f};
    EXPECT_EQ(occluder.CheckSphere(sphere), 0);
}

TEST(Occlusion, DisabledOrUnsetupHidesNothing) {
    COcclusion occluder;
    SetQuad(occluder, true);
    float view[4][4];
    Identity(view);
    float sphere[4] = {0.5f, 0.5f, 20.0f, 0.5f};

    EXPECT_EQ(occluder.CheckSphere(sphere), 0);

    occluder.enable = 0;
    occluder.Setup(view);
    EXPECT_EQ(occluder.CheckSphere(sphere), 0);

    occluder.enable = 1;
    occluder.Setup(view);
    EXPECT_EQ(occluder.CheckSphere(sphere), 1);
}

TEST(Occlusion, SetupTransformsTheCornersIntoViewSpace) {
    COcclusion occluder;
    SetQuad(occluder, true);
    float view[4][4];
    Identity(view);
    view[3][2] = -5.0f; // the eye five units ahead: the quad sits at z = 5 in view space
    occluder.Setup(view);

    float hidden[4] = {0.5f, 0.5f, 15.0f, 0.5f};
    EXPECT_EQ(occluder.CheckSphere(hidden), 1);

    float in_front[4] = {0.5f, 0.5f, 2.0f, 0.5f};
    EXPECT_EQ(occluder.CheckSphere(in_front), 0);
}

TEST(Occlusion, DegenerateQuadHidesNothing) {
    COcclusion occluder;
    occluder.enable = 1;
    for (int corner = 0; corner < 4; corner++) {
        occluder.vertex[corner][0] = 1.0f;
        occluder.vertex[corner][1] = 1.0f;
        occluder.vertex[corner][2] = 10.0f;
        occluder.vertex[corner][3] = 1.0f;
    }
    float view[4][4];
    Identity(view);
    occluder.Setup(view);

    float sphere[4] = {0.5f, 0.5f, 20.0f, 0.5f};
    EXPECT_EQ(occluder.CheckSphere(sphere), 0);
}

} // namespace
