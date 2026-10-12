#include <gtest/gtest.h>

#include <cmath>
#include <libvu0.h>

#include "mg_math.hpp"

TEST(NativeMathContract, BoxClipTreatsTouchingFacesAsOverlap) {
    float first_max[4] = {2.0f, 3.0f, 4.0f, 1.0f};
    float first_min[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float touching_max[4] = {5.0f, 6.0f, 7.0f, 1.0f};
    float touching_min[4] = {2.0f, 3.0f, 4.0f, 1.0f};
    float separated_min[4] = {2.01f, 3.0f, 4.0f, 1.0f};

    EXPECT_EQ(mgClipBox(first_max, first_min, touching_max, touching_min), 1);
    EXPECT_EQ(mgClipBox(first_max, first_min, touching_max, separated_min), 0);
}

TEST(NativeMathContract, Vu0NormalizeWritesSpatialUnitVectorAndZeroW) {
    sceVu0FVECTOR input = {3.0f, 0.0f, 4.0f, 9.0f};
    sceVu0FVECTOR output = {};

    sceVu0Normalize(output, input);

    EXPECT_FLOAT_EQ(output[0], 0.6f);
    EXPECT_FLOAT_EQ(output[1], 0.0f);
    EXPECT_FLOAT_EQ(output[2], 0.8f);
    EXPECT_FLOAT_EQ(output[3], 0.0f);
}

TEST(NativeMathContract, Vu0NormalizeZeroVectorRemainsFiniteZero) {
    sceVu0FVECTOR zero = {0.0f, 0.0f, 0.0f, 7.0f};
    sceVu0FVECTOR output = {};

    sceVu0Normalize(output, zero);

    for (float lane : output) {
        EXPECT_TRUE(std::isfinite(lane));
        EXPECT_FLOAT_EQ(lane, 0.0f);
    }
}
