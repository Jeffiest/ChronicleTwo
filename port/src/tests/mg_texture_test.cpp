#include <gtest/gtest.h>

#include <cstring>

#include "common.h"
#include "gfx/gfx.hpp"
#include "gfx_fixture.hpp"
#include "mg_texture.hpp"

bool PortMgTextureBinding(const mgCTexture *texture, gfx::TextureBinding &out);
bool PortMgFrameBufferBinding(const mgCTexture *texture, gfx::TextureBinding &out);

TEST(MgTexture, FrameBufferPromotionPreservesWideFillSides) {
    dc::test::GfxFixture fixture(1280, 720);
    gfx::SetFrameLayout({gfx::AspectMode::Fill, 1.0f});

    mgCTexture texture{};
    std::strncpy(texture.name, "frame-capture-test", sizeof(texture.name) - 1);
    texture.width = 512;
    texture.height = 480;
    texture.bpp = 32;

    gfx::TextureBinding ordinary;
    ASSERT_TRUE(PortMgTextureBinding(&texture, ordinary));
    auto ordinary_info = gfx::GetTextureInfo(ordinary.texture);
    ASSERT_TRUE(ordinary_info.has_value());
    EXPECT_FALSE(ordinary_info->frame_target);

    gfx::TextureBinding framebuffer;
    ASSERT_TRUE(PortMgFrameBufferBinding(&texture, framebuffer));
    auto framebuffer_info = gfx::GetTextureInfo(framebuffer.texture);
    ASSERT_TRUE(framebuffer_info.has_value());
    EXPECT_TRUE(framebuffer_info->frame_target);
    EXPECT_EQ(framebuffer_info->pixel_width, 1280u);

    const gfx::LogicalRect visible = gfx::VisibleLogicalRect(framebuffer.texture);
    EXPECT_LT(visible.x, 0.0f);
    EXPECT_GT(visible.x + visible.w, 512.0f);

    // mgGetFrameBuffer returns a copy; resolving it by the same name must keep the promoted shape.
    mgCTexture captured_frame{};
    std::strncpy(captured_frame.name, texture.name, sizeof(captured_frame.name) - 1);
    captured_frame.width = texture.width;
    captured_frame.height = texture.height;
    captured_frame.bpp = texture.bpp;
    gfx::TextureBinding captured;
    ASSERT_TRUE(PortMgTextureBinding(&captured_frame, captured));
    EXPECT_EQ(captured.texture, framebuffer.texture);
    auto captured_info = gfx::GetTextureInfo(captured.texture);
    ASSERT_TRUE(captured_info.has_value());
    EXPECT_TRUE(captured_info->frame_target);

    // The regular texture path must keep the frame-buffer target shape after promotion.
    gfx::TextureBinding sampled;
    ASSERT_TRUE(PortMgTextureBinding(&texture, sampled));
    EXPECT_EQ(sampled.texture, framebuffer.texture);
    auto sampled_info = gfx::GetTextureInfo(sampled.texture);
    ASSERT_TRUE(sampled_info.has_value());
    EXPECT_TRUE(sampled_info->frame_target);
}
