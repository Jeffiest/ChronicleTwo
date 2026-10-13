#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "gfx/gfx.hpp"
#include "gfx_fixture.hpp"
#include "mg_drawprim.hpp"
#include "mg_drawprim_port.hpp"
#include "mg_texture.hpp"
#include "mglib.hpp"
#include "mglib_port.hpp"

namespace {

constexpr uint32_t kTextureWidth = 64;
constexpr uint32_t kTextureHeight = 8;
constexpr uint32_t kHalfWidth = 32;

class PrimSinkReset {
public:
    ~PrimSinkReset() {
        mgPortSetPrimSink({});
    }
};

void DrawTexturedSprite(mgCDrawPrim &prim, mgCTexture &source, std::array<float, 4> dest,
                        std::array<int, 4> uv) {
    prim.Coord(1);
    prim.TextureMapEnable(1);
    prim.Bilinear(0);
    prim.Texture(&source);
    prim.Begin(MG_PRIM_SPRITE);
    prim.TextureCrd(uv[0], uv[1]);
    prim.Vertex(dest[0], dest[1], 0.0f);
    prim.TextureCrd(uv[2], uv[3]);
    prim.Vertex(dest[2], dest[3], 0.0f);
    prim.End2();
}

} // namespace

// Two 1:1 nearest tiles meet at logical x 47, where a 2.5x window puts a pixel centre on the seam. The left
// half of the texture is red, so any red pixel is a texel the seam sampled from the wrong side.
TEST(MgDrawPrimRender, TileSeamHasNoForeignTexel) {
    dc::test::GfxFixture gfx(1600, 1200);

    const gfx::LogicalMapping mapping = gfx::GetLogicalMapping(gfx::kMainTarget);
    ASSERT_FLOAT_EQ(mapping.scale_x, 2.5f);
    ASSERT_FLOAT_EQ(mapping.offset_x, 0.0f);

    const gfx::TextureHandle texture = gfx::CreateTexture({kTextureWidth, kTextureHeight, gfx::TextureFormat::Rgba8});
    ASSERT_NE(texture, gfx::kNullTexture);
    std::vector<uint8_t> texels(static_cast<size_t>(kTextureWidth) * kTextureHeight * 4);
    for (uint32_t y = 0; y < kTextureHeight; ++y) {
        for (uint32_t x = 0; x < kTextureWidth; ++x) {
            uint8_t *p = &texels[(static_cast<size_t>(y) * kTextureWidth + x) * 4];
            const bool left = x < kHalfWidth;
            p[0] = left ? 0xFF : 0x00;
            p[1] = left ? 0x00 : 0xFF;
            p[2] = 0x00;
            p[3] = 0xFF;
        }
    }
    ASSERT_TRUE(gfx::UpdateTextureNow(texture, 0, 0, 0, kTextureWidth, kTextureHeight, texels.data()));

    mgCMemory        memory;
    sceVif1Packet    vif_packet{};
    mgRENDER_INFO    render_info{};
    mgCDrawManager   manager;
    mgCDrawPrim      prim;
    mgCTexture       source;
    prim.draw_manager = &manager;
    manager.render_info = &render_info;
    prim.Initialize(&memory, &vif_packet);
    prim.write = nullptr;

    PrimSinkReset sink_reset;
    int           sprites = 0;
    mgPortSetPrimSink([texture, &sprites](gfx::Primitive primitive, std::span<const gfx::Vertex2D> vertices,
                                          const gfx::TextureBinding &binding, const gfx::DrawState &state) {
        EXPECT_EQ(binding.filter, gfx::Filter::Nearest);
        gfx::TextureBinding bound = binding;
        bound.texture = texture;
        gfx::Draw2D(primitive, vertices, bound, state);
        sprites++;
    });

    gfx.Frame({0x00, 0x00, 0x00, 0xFF}, [&] {
        DrawTexturedSprite(prim, source, {15.0f, 0.0f, 47.0f, 8.0f}, {32, 0, 64, 8});
        DrawTexturedSprite(prim, source, {47.0f, 0.0f, 79.0f, 8.0f}, {32, 0, 64, 8});
    });

    ASSERT_EQ(sprites, 2);

    uint32_t green = 0;
    uint32_t red = 0;
    uint32_t first_red_x = 0;
    uint32_t first_red_y = 0;
    for (uint32_t y = 0; y < gfx.height; ++y) {
        for (uint32_t x = 0; x < gfx.width; ++x) {
            const std::array<uint8_t, 4> p = gfx.Pixel(x, y);
            if (p[0] >= 0x80 && p[1] < 0x80) {
                if (red++ == 0) {
                    first_red_x = x;
                    first_red_y = y;
                }
            } else if (p[1] >= 0x80 && p[0] < 0x80) {
                green++;
            }
        }
    }
    EXPECT_EQ(red, 0u) << "first red pixel at window " << first_red_x << "," << first_red_y;
    EXPECT_GT(green, 0u);
}
