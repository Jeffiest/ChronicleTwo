#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "common.h"
#include "gfx/gfx.hpp"
#include "gfx_fixture.hpp"
#include "libgraph.h"
#include "mg_texture.hpp"
#include "mglib.hpp"
#include "mglib_port.hpp"
#include "platform/clock.hpp"
#include "platform/window.hpp"

using namespace dc::test;

namespace {

class PhotoRenderer {
public:
    PhotoRenderer() {
        SetEnv("VK_LAYER_VALIDATE_SYNC", "1", 0);
        WindowConfig window{512, 480, true};
        window.vulkan = false;
        WindowInit(window);

        gfx::RendererConfig config;
        config.app_name = "Dark Chronicle photo tests";
        config.logical_width = 512.0f;
        config.logical_height = 480.0f;
        config.display_aspect = 512.0f / 480.0f;
        config.validation = true;
        config.offscreen = true;
        config.pipeline_cache = std::filesystem::temp_directory_path() / "dc_gfx_test" /
                                ("photo_" + std::to_string(ProcessId()) + ".bin");
        gfx::RendererInit(WindowHandle(), config);

        ClockSetUnbounded(true);
        mgInit(MG_SCREEN_MODE_512X480, 3);
    }

    ~PhotoRenderer() {
        gfx::RendererShutdown();
        WindowShutdown();
        EXPECT_EQ(gfx::ValidationMessageCount(), 0u);
    }
};

gfx::Vertex2D Corner(float x, float y, float u, float v, uint8_t red, uint8_t green, uint8_t blue) {
    gfx::Vertex2D vertex{};
    vertex.x = x;
    vertex.y = y;
    vertex.z = 1.0f;
    vertex.u = u;
    vertex.v = v;
    vertex.color[0] = red;
    vertex.color[1] = green;
    vertex.color[2] = blue;
    vertex.color[3] = 0x80;
    return vertex;
}

gfx::DrawState OpaqueState() {
    gfx::DrawState state = mgPortDrawState();
    state.depth_test = gfx::DepthTest::Always;
    state.depth_write = false;
    state.alpha_test = false;
    state.blend = false;
    state.cull = gfx::CullMode::None;
    return state;
}

// Left half red, right half blue, on the frame being drawn.
void DrawHalves() {
    const float width = static_cast<float>(mgScreenWidth);
    const float height = static_cast<float>(mgScreenHeight);
    const float middle = width / 2.0f;

    const std::vector<gfx::Vertex2D> red = {
        Corner(0.0f, 0.0f, 0.0f, 0.0f, 0xFF, 0, 0),
        Corner(middle, 0.0f, 0.0f, 0.0f, 0xFF, 0, 0),
        Corner(middle, height, 0.0f, 0.0f, 0xFF, 0, 0),
        Corner(0.0f, height, 0.0f, 0.0f, 0xFF, 0, 0),
    };
    const std::vector<gfx::Vertex2D> blue = {
        Corner(middle, 0.0f, 0.0f, 0.0f, 0, 0, 0xFF),
        Corner(width, 0.0f, 0.0f, 0.0f, 0, 0, 0xFF),
        Corner(width, height, 0.0f, 0.0f, 0, 0, 0xFF),
        Corner(middle, height, 0.0f, 0.0f, 0, 0, 0xFF),
    };

    mgPortDraw2D(gfx::Primitive::Quads, red, gfx::TextureBinding{}, OpaqueState());
    mgPortDraw2D(gfx::Primitive::Quads, blue, gfx::TextureBinding{}, OpaqueState());
}

} // namespace

TEST(Photo, ShutterSpriteCapturesTheFrame) {
    PhotoRenderer renderer;

    mgCTexture work{};
    work.Initialize();
    work.width = 64;
    work.height = 64;
    work.bpp = 32;
    work.tex0.TBP0 = 256 << 5;
    work.tex0.TBW = 1;
    work.tex0.PSM = SCE_GS_PSMCT32;

    mgBeginFrame(nullptr);
    DrawHalves();

    mgCTexture frame{};
    mgGetFrameBuffer(&frame);
    mgSetPkFrameBuffer(&work);

    const float work_width = static_cast<float>(work.width);
    const float work_height = static_cast<float>(work.height);
    const float frame_width = static_cast<float>(frame.width);
    const float frame_height = static_cast<float>(frame.height);
    const std::vector<gfx::Vertex2D> sprite = {
        Corner(0.0f, 0.0f, 0.0f, 0.0f, 0x80, 0x80, 0x80),
        Corner(work_width, 0.0f, frame_width, 0.0f, 0x80, 0x80, 0x80),
        Corner(work_width, work_height, frame_width, frame_height, 0x80, 0x80, 0x80),
        Corner(0.0f, work_height, 0.0f, frame_height, 0x80, 0x80, 0x80),
    };

    gfx::DrawState state = OpaqueState();
    mgPortDraw2D(gfx::Primitive::Quads, sprite, mgPortBinding(&frame), state);

    mgSetPkFrameBuffer(-1, -1, -1, -1);
    mgEndFrame(nullptr);

    const gfx::TextureHandle target = mgPortFindTexture(&work).texture;
    ASSERT_NE(target, gfx::kNullTexture);

    std::vector<uint8_t> rgba;
    uint32_t width = 0;
    uint32_t height = 0;
    ASSERT_TRUE(mgPortCanonicalReadColor(target, rgba, width, height));

    const gfx::LogicalMapping mapping = gfx::GetLogicalMapping(target);
    const auto at = [&](float x, float y) {
        const size_t px = static_cast<size_t>(x * mapping.scale_x + mapping.offset_x);
        const size_t py = static_cast<size_t>(y * mapping.scale_y + mapping.offset_y);
        return &rgba[(py * width + px) * 4];
    };

    const uint8_t *left = at(16.0f, 32.0f);
    EXPECT_GT(left[0], 200);
    EXPECT_LT(left[1], 60);
    EXPECT_LT(left[2], 60);

    const uint8_t *right = at(48.0f, 32.0f);
    EXPECT_LT(right[0], 60);
    EXPECT_LT(right[1], 60);
    EXPECT_GT(right[2], 200);
}
