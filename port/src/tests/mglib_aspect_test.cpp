#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "common.h"
#include "gfx/gfx.hpp"
#include "gfx_fixture.hpp"
#include "mglib.hpp"
#include "mglib_port.hpp"
#include "platform/clock.hpp"
#include "platform/window.hpp"

using namespace dc::test;

namespace {

class AspectRenderer {
public:
    AspectRenderer(int width, int height) {
        SetEnv("VK_LAYER_VALIDATE_SYNC", "1", 0);
        WindowConfig window{width, height, true};
        window.vulkan = false;
        WindowInit(window);

        gfx::RendererConfig config;
        config.app_name = "Dark Chronicle aspect tests";
        config.logical_width = 512.0f;
        config.logical_height = 480.0f;
        config.display_aspect = 4.0f / 3.0f;
        config.validation = true;
        config.offscreen = true;
        config.pipeline_cache = std::filesystem::temp_directory_path() / "dc_gfx_test" /
                                ("mglib_aspect_" + std::to_string(ProcessId()) + ".bin");
        gfx::RendererInit(WindowHandle(), config);

        ClockSetUnbounded(true);
        mgInit(MG_SCREEN_MODE_512X480, 3);
    }

    ~AspectRenderer() {
        gfx::RendererShutdown();
        WindowShutdown();
        EXPECT_EQ(gfx::ValidationMessageCount(), 0u);
    }
};

bool Inside(float x) {
    const mgPortExtent e = mgPortVisibleExtent();
    return x >= e.left && x <= e.right;
}

gfx::Vertex2D QuadCorner(float x, float y) {
    gfx::Vertex2D vertex = {};
    vertex.x = x;
    vertex.y = y;
    vertex.z = 1.0f;
    vertex.color[0] = 0xFF;
    vertex.color[1] = 0xFF;
    vertex.color[2] = 0xFF;
    vertex.color[3] = 0xFF;
    return vertex;
}

// Pixel-snapped edges can land one device pixel off, and a device pixel is 1 / scale logical units.
void ExpectQuadAtLogicalPlace(int width, int height) {
    mgSetBackGround(0.0f, 0.0f, 0.0f, 128.0f);
    mgBeginFrame(nullptr);

    const std::array<gfx::Vertex2D, 4> quad = {
        QuadCorner(100.0f, 200.0f),
        QuadCorner(164.0f, 200.0f),
        QuadCorner(164.0f, 264.0f),
        QuadCorner(100.0f, 264.0f),
    };
    mgPortDraw2D(gfx::Primitive::Quads, quad, gfx::TextureBinding{}, mgPortDrawState());

    mgEndFrame(nullptr);

    std::vector<uint8_t> rgba;
    uint32_t             out_width = 0;
    uint32_t             out_height = 0;
    ASSERT_TRUE(mgPortCanonicalReadColor(gfx::kMainTarget, rgba, out_width, out_height));
    EXPECT_EQ(static_cast<int>(out_width), width);
    EXPECT_EQ(static_cast<int>(out_height), height);

    uint32_t min_x = out_width;
    uint32_t max_x = 0;
    uint32_t min_y = out_height;
    uint32_t max_y = 0;
    for (uint32_t y = 0; y < out_height; ++y) {
        for (uint32_t x = 0; x < out_width; ++x) {
            const uint8_t *p = &rgba[(static_cast<size_t>(y) * out_width + x) * 4];
            if (p[0] > 200 && p[1] > 200 && p[2] > 200) {
                min_x = std::min(min_x, x);
                max_x = std::max(max_x, x);
                min_y = std::min(min_y, y);
                max_y = std::max(max_y, y);
            }
        }
    }
    ASSERT_LE(min_x, max_x);
    ASSERT_LE(min_y, max_y);

    const gfx::LogicalMapping mapping = gfx::GetLogicalMapping(gfx::kMainTarget);
    const double tol_x = std::max<double>(1.0, 1.0 / mapping.scale_x);
    const double tol_y = std::max<double>(1.0, 1.0 / mapping.scale_y);

    const double left = (static_cast<double>(min_x) - mapping.offset_x) / mapping.scale_x;
    const double right = (static_cast<double>(max_x + 1) - mapping.offset_x) / mapping.scale_x;
    const double top = (static_cast<double>(min_y) - mapping.offset_y) / mapping.scale_y;
    const double bottom = (static_cast<double>(max_y + 1) - mapping.offset_y) / mapping.scale_y;

    EXPECT_NEAR(left, 100.0, tol_x);
    EXPECT_NEAR(right, 164.0, tol_x);
    EXPECT_NEAR(top, 200.0, tol_y);
    EXPECT_NEAR(bottom, 264.0, tol_y);
}

} // namespace

TEST(MglibAspect, LogicalScreenAt640x480) {
    AspectRenderer renderer(640, 480);

    EXPECT_EQ(mgScreenWidth, 512);
    EXPECT_EQ(mgScreenHeight, 480);

    mgBeginFrame(nullptr);
    mgEndFrame(nullptr);

    EXPECT_EQ(mgScreenWidth, 512);
    EXPECT_EQ(mgScreenHeight, 480);
}

TEST(MglibAspect, LogicalScreenAt1280x720) {
    AspectRenderer renderer(1280, 720);

    EXPECT_EQ(mgScreenWidth, 512);
    EXPECT_EQ(mgScreenHeight, 480);

    mgBeginFrame(nullptr);
    mgEndFrame(nullptr);

    EXPECT_EQ(mgScreenWidth, 512);
    EXPECT_EQ(mgScreenHeight, 480);
}

TEST(MglibAspect, LogicalScreenAt800x1200) {
    AspectRenderer renderer(800, 1200);

    EXPECT_EQ(mgScreenWidth, 512);
    EXPECT_EQ(mgScreenHeight, 480);

    mgBeginFrame(nullptr);
    mgEndFrame(nullptr);

    EXPECT_EQ(mgScreenWidth, 512);
    EXPECT_EQ(mgScreenHeight, 480);
}

TEST(MglibAspect, ExtentIsScreenAtFourThree) {
    AspectRenderer renderer(1024, 768);

    const mgPortExtent e = mgPortVisibleExtent();
    EXPECT_NEAR(e.left, 0.0f, 0.01f);
    EXPECT_NEAR(e.right, 512.0f, 0.01f);
    EXPECT_NEAR(e.top, 0.0f, 0.01f);
    EXPECT_NEAR(e.bottom, 480.0f, 0.01f);
    EXPECT_NEAR(e.half_width, 256.0f, 0.01f);
    EXPECT_NEAR(e.half_height, 240.0f, 0.01f);

    EXPECT_EQ(mgScreenOffx, 2048 - 256);
    EXPECT_FLOAT_EQ(e.gs_left, static_cast<float>(mgScreenOffx));
    EXPECT_FLOAT_EQ(e.gs_right, static_cast<float>(mgScreenOffx) + 512.0f);
}

TEST(MglibAspect, ExtentIsWiderAtSixteenNine) {
    AspectRenderer renderer(1280, 720);

    const mgPortExtent e = mgPortVisibleExtent();
    EXPECT_LT(e.left, -40.0f);
    EXPECT_GT(e.right, 552.0f);
    EXPECT_NEAR(e.top, 0.0f, 0.5f);
    EXPECT_NEAR(e.bottom, 480.0f, 0.5f);
    EXPECT_GT(e.half_width, 296.0f);
    EXPECT_NEAR(e.half_height, 240.0f, 0.5f);

    EXPECT_TRUE(-40.0f >= e.left && -40.0f <= e.right);
}

TEST(MglibAspect, ExtentKeepsWidthWhenNarrow) {
    AspectRenderer renderer(800, 1200);

    const mgPortExtent e = mgPortVisibleExtent();
    EXPECT_NEAR(e.left, 0.0f, 0.5f);
    EXPECT_NEAR(e.right, 512.0f, 0.5f);
    EXPECT_LT(e.top, 0.0f);
    EXPECT_GT(e.bottom, 480.0f);
}

TEST(MglibAspect, CullAtMinusFortyWhenWider) {
    AspectRenderer renderer(1280, 720);

    EXPECT_TRUE(Inside(-40.0f));
}

TEST(MglibAspect, CullAtMinusFortyAtFourThree) {
    AspectRenderer renderer(1024, 768);

    EXPECT_FALSE(Inside(-40.0f));
}

TEST(MglibAspect, QuadAtSameLogicalPlaceAt1280x720) {
    AspectRenderer renderer(1280, 720);
    ExpectQuadAtLogicalPlace(1280, 720);
}

TEST(MglibAspect, QuadAtSameLogicalPlaceAt3840x2160) {
    AspectRenderer renderer(3840, 2160);
    ExpectQuadAtLogicalPlace(3840, 2160);
}

TEST(MglibAspect, QuadAtSameLogicalPlaceAt800x1200) {
    AspectRenderer renderer(800, 1200);
    ExpectQuadAtLogicalPlace(800, 1200);
}
