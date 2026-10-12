#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "common.h"
#include "gfx/gfx.hpp"
#include "gfx_fixture.hpp"
#include "libgraph.h"
#include "mg_memory.hpp"
#include "mg_texture.hpp"
#include "mg_texture_port.hpp"
#include "mglib.hpp"
#include "mglib_port.hpp"
#include "platform/clock.hpp"
#include "platform/window.hpp"
#include "present.hpp"

using namespace dc::test;

namespace {

class PresentRenderer {
public:
    PresentRenderer(int width, int height) {
        SetEnv("VK_LAYER_VALIDATE_SYNC", "1", 0);
        WindowConfig window{width, height, true};
        window.vulkan = false;
        WindowInit(window);

        gfx::RendererConfig config;
        config.app_name = "Dark Chronicle present tests";
        config.logical_width = 512.0f;
        config.logical_height = 480.0f;
        config.display_aspect = 4.0f / 3.0f;
        config.validation = true;
        config.offscreen = true;
        config.pipeline_cache = std::filesystem::temp_directory_path() / "dc_gfx_test" /
                                ("mglib_present_" + std::to_string(ProcessId()) + ".bin");
        gfx::RendererInit(WindowHandle(), config);

        ClockSetUnbounded(true);
        mgInit(MG_SCREEN_MODE_512X480, 3);
    }

    ~PresentRenderer() {
        gfx::RendererShutdown();
        WindowShutdown();
        EXPECT_EQ(gfx::ValidationMessageCount(), 0u);
    }
};

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

size_t PixelOffset(const gfx::LogicalMapping &mapping, uint32_t width, float x, float y) {
    const size_t px = static_cast<size_t>(x * mapping.scale_x + mapping.offset_x);
    const size_t py = static_cast<size_t>(y * mapping.scale_y + mapping.offset_y);
    return (py * width + px) * 4;
}

using Matrix = std::array<float, 16>;

// Column-major, as MeshTransform and MeshConstants hold them.
Matrix Identity() {
    Matrix m{};
    m[0] = m[5] = m[10] = m[15] = 1.0f;
    return m;
}

Matrix Translation(float x) {
    Matrix m = Identity();
    m[12] = x;
    return m;
}

Matrix Multiply(const Matrix &a, const Matrix &b) {
    Matrix out{};
    for (size_t col = 0; col < 4; ++col) {
        for (size_t row = 0; row < 4; ++row) {
            for (size_t k = 0; k < 4; ++k) {
                out[col * 4 + row] += a[k * 4 + row] * b[col * 4 + k];
            }
        }
    }
    return out;
}

// One world unit is four pixels on the 640x480 target, and depth is a constant 0.5.
constexpr float kPixelsPerUnit = 4.0f;

Matrix Projection() {
    Matrix m{};
    m[0] = 2.0f * kPixelsPerUnit / 640.0f;
    m[5] = 2.0f * kPixelsPerUnit / 480.0f;
    m[14] = 0.5f;
    m[15] = 1.0f;
    return m;
}

void Store(const Matrix &m, float (&out)[16]) {
    std::memcpy(out, m.data(), sizeof out);
}

gfx::Vertex3D RedVertex(float x, float y) {
    gfx::Vertex3D vertex = {};
    vertex.position[0] = x;
    vertex.position[1] = y;
    vertex.normal[2] = 1.0f;
    vertex.color[0] = 0xFF;
    vertex.color[3] = 0xFF;
    return vertex;
}

// A 10 by 10 unit red square, centred on its model origin, moved to x by the model translation.
void DrawRedQuad(float x) {
    const std::vector<gfx::Vertex3D> vertices = {
        RedVertex(-5.0f, -5.0f),
        RedVertex(5.0f, -5.0f),
        RedVertex(5.0f, 5.0f),
        RedVertex(-5.0f, 5.0f),
    };
    const std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};

    const Matrix projection = Projection();
    const Matrix view = Identity();
    const Matrix model = Translation(x);

    gfx::MeshTransform transform = gfx::IdentityMeshTransform();
    Store(projection, transform.projection);
    Store(view, transform.view);
    Store(model, transform.model);

    gfx::MeshConstants constants{};
    constants.flags = gfx::kMeshVertexColor;
    Store(Multiply(Multiply(projection, view), model), constants.mvp);
    constants.diffuse[0] = constants.diffuse[1] = constants.diffuse[2] = constants.diffuse[3] = 1.0f;

    gfx::DrawState state;
    state.depth_test = gfx::DepthTest::Always;

    mgPortDrawMeshImmediate(vertices, indices, constants, gfx::TextureBinding{}, state, &transform);
}

// Mean column of the pixels that read as the quad's red.
void RedCentroidX(const std::vector<uint8_t> &rgba, uint32_t width, uint32_t height, double &x) {
    double sum = 0.0;
    size_t count = 0;
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t px = 0; px < width; ++px) {
            const uint8_t *p = &rgba[(static_cast<size_t>(y) * width + px) * 4];
            if (p[0] > 200 && p[1] < 60 && p[2] < 60) {
                sum += px;
                ++count;
            }
        }
    }
    ASSERT_GT(count, 0u);
    x = sum / static_cast<double>(count);
}

void RecordTicks(int count, const void *object, float first_x) {
    for (int tick = 0; tick < count; ++tick) {
        mgBeginFrame(nullptr);
        {
            mgPortInterpScope scope(object, 1000.0f);
            DrawRedQuad(first_x + 10.0f * static_cast<float>(tick));
        }
        mgEndFrame(nullptr);
    }
}

using Quads = std::vector<u_long128>;

constexpr int kTextureCount = 64;
constexpr int kBlockCount = 8;
constexpr size_t kStorageQuads = 4096;

Quads Pack(const std::vector<uint8_t> &bytes) {
    Quads quads((bytes.size() + 15) / 16 + 1, 0);
    std::memcpy(quads.data(), bytes.data(), bytes.size());
    return quads;
}

std::vector<uint8_t> Fill(size_t texels, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    std::vector<uint8_t> out(texels * 4);
    for (size_t t = 0; t < texels; ++t) {
        out[t * 4 + 0] = r;
        out[t * 4 + 1] = g;
        out[t * 4 + 2] = b;
        out[t * 4 + 3] = a;
    }
    return out;
}

// Entry p is (p, 255 - p, 0x10, 0x80).
std::vector<uint8_t> Clut8() {
    std::vector<uint8_t> out(256 * 4);
    for (size_t p = 0; p < 256; ++p) {
        out[p * 4 + 0] = static_cast<uint8_t>(p);
        out[p * 4 + 1] = static_cast<uint8_t>(255 - p);
        out[p * 4 + 2] = 0x10;
        out[p * 4 + 3] = 0x80;
    }
    return out;
}

uint64_t Tex0Bits(const mgCTexture *texture) {
    uint64_t bits = 0;
    std::memcpy(&bits, &texture->tex0, sizeof bits);
    return bits;
}

std::vector<uint8_t> ReadTexture(gfx::TextureHandle texture, uint32_t &width, uint32_t &height) {
    std::vector<uint8_t> pixels;
    EXPECT_TRUE(gfx::ReadbackTexture(texture, pixels, width, height));
    return pixels;
}

// Registers textures the way the game does: the registry key lands in TBP0 and CBP.
class Textures {
public:
    Textures() {
        memory_.stSetBuffer(storage_.data(), static_cast<int>(storage_.size()));
        manager_.SetTableBuffer(kTextureCount, kBlockCount, &memory_);
        manager_.Initialize(0x1000, -1);
    }

    ~Textures() {
        manager_.Initialize(0x1000, -1);
    }

    Textures(const Textures &) = delete;
    Textures &operator=(const Textures &) = delete;

    mgCTexture *Enter(const std::string &name, int width, int height, int bpp, u_long128 *image, u_long128 *clut) {
        std::string owned = name;
        u_long128 *levels[MG_TEXTURE_LEVEL_MAX] = {image};
        return manager_.EnterTexture(0, owned.data(), levels, width, height, bpp, clut, 0, 0);
    }

    void Delete(mgCTexture *texture) {
        manager_.DeleteTexture(texture);
    }

private:
    Quads             storage_ = Quads(kStorageQuads, 0);
    mgCMemory         memory_;
    mgCTextureManager manager_;
};

} // namespace

TEST(MglibPresent, InterpScopeRestoresKey) {
    PresentRenderer renderer(640, 480);

    int some_int = 0;
    int other = 0;

    gfx::SetInterpKey(7);
    {
        mgPortInterpScope scope(&some_int, kMgPortFrameTeleportDistance);
        EXPECT_EQ(gfx::CurrentInterpKey(), reinterpret_cast<uintptr_t>(&some_int));
        {
            mgPortInterpScope inner(&other, 1.0f, false, true);
            EXPECT_TRUE(gfx::CurrentNoInterpolation());
        }
        EXPECT_FALSE(gfx::CurrentNoInterpolation());
    }
    EXPECT_EQ(gfx::CurrentInterpKey(), 7u);
    gfx::SetInterpKey(0);
}

TEST(MglibPresent, TickIsRecordedAndRenderedCanonically) {
    PresentRenderer renderer(640, 480);

    mgSetBackGround(64.0f, 128.0f, 192.0f, 128.0f);
    mgBeginFrame(nullptr);
    EXPECT_TRUE(mgPortRecording());

    const std::array<gfx::Vertex2D, 4> quad = {
        QuadCorner(100.0f, 100.0f),
        QuadCorner(200.0f, 100.0f),
        QuadCorner(200.0f, 200.0f),
        QuadCorner(100.0f, 200.0f),
    };
    mgPortDraw2D(gfx::Primitive::Quads, quad, gfx::TextureBinding{}, mgPortDrawState());

    mgEndFrame(nullptr);
    EXPECT_FALSE(mgPortRecording());

    ASSERT_NE(mgPortLastList(), nullptr);
    EXPECT_EQ(gfx::ListStats(*mgPortLastList()).draws_2d, 1u);

    std::vector<uint8_t> rgba;
    uint32_t             width = 0;
    uint32_t             height = 0;
    ASSERT_TRUE(mgPortCanonicalReadColor(gfx::kMainTarget, rgba, width, height));

    const gfx::LogicalMapping mapping = gfx::GetLogicalMapping(gfx::kMainTarget);

    const size_t inside = PixelOffset(mapping, width, 150.0f, 150.0f);
    EXPECT_NEAR(rgba[inside + 0], 255, 1);
    EXPECT_NEAR(rgba[inside + 1], 255, 1);
    EXPECT_NEAR(rgba[inside + 2], 255, 1);

    const size_t outside = PixelOffset(mapping, width, 50.0f, 50.0f);
    EXPECT_NEAR(rgba[outside + 0], 64, 1);
    EXPECT_NEAR(rgba[outside + 1], 128, 1);
    EXPECT_NEAR(rgba[outside + 2], 192, 1);

    mgBeginFrame(nullptr);
    mgEndFrame(nullptr);
    EXPECT_NE(mgPortPreviousList(), nullptr);
}

TEST(MglibPresent, FrameTargetFlag) {
    PresentRenderer renderer(1280, 720);

    const gfx::TextureHandle framed = mgPortRenderTarget(0x3000u, 512, 480, SCE_GS_PSMCT32, true);
    const std::optional<gfx::TextureInfo> framed_info = gfx::GetTextureInfo(framed);
    ASSERT_TRUE(framed_info.has_value());
    EXPECT_TRUE(framed_info->frame_target);

    const gfx::TextureHandle plain = mgPortRenderTarget(0x3000u, 512, 480, SCE_GS_PSMCT32, false);
    const std::optional<gfx::TextureInfo> plain_info = gfx::GetTextureInfo(plain);
    ASSERT_TRUE(plain_info.has_value());
    EXPECT_FALSE(plain_info->frame_target);

    EXPECT_EQ(mgPortRenderTarget(0x3000u, 512, 480, SCE_GS_PSMCT32, false), plain);
}

TEST(MglibPresent, CutBeforeTickIsAccepted) {
    PresentRenderer renderer(640, 480);

    mgPortCutInterpolation();
    mgBeginFrame(nullptr);
    mgEndFrame(nullptr);

    EXPECT_FALSE(mgPortRecording());
}

TEST(MglibPresent, DisplayFrameAtHalfInterpolatesTaggedMesh) {
    PresentRenderer renderer(640, 480);
    PresentResetStats();
    PresentSetSettings(PresentSettings{.interpolation = true, .display_per_tick = 0});

    static int object;
    mgPortCutInterpolation();

    std::vector<uint8_t> rgba;
    uint32_t             width = 0;
    uint32_t             height = 0;

    RecordTicks(1, &object, 0.0f);
    ASSERT_TRUE(mgPortCanonicalReadColor(gfx::kMainTarget, rgba, width, height));
    double first = 0.0;
    RedCentroidX(rgba, width, height, first);

    RecordTicks(1, &object, 10.0f);
    ASSERT_TRUE(mgPortCanonicalReadColor(gfx::kMainTarget, rgba, width, height));
    double second = 0.0;
    RedCentroidX(rgba, width, height, second);

    ASSERT_NE(mgPortLastList(), nullptr);
    ASSERT_NE(mgPortPreviousList(), nullptr);
    EXPECT_NEAR(second - first, 10.0 * kPixelsPerUnit, 1.0);

    gfx::RenderOptions options;
    options.previous = mgPortPreviousList();
    options.present = true;
    ASSERT_TRUE(gfx::RenderList(*mgPortLastList(), 0.5f, options));
    ASSERT_TRUE(gfx::ReadbackFrame(rgba, width, height));

    double half = 0.0;
    RedCentroidX(rgba, width, height, half);
    EXPECT_NEAR(half, (first + second) / 2.0, 1.0);
}

TEST(MglibPresent, InterpolationOffPresentsOncePerTick) {
    PresentRenderer renderer(640, 480);
    PresentResetStats();
    PresentSetSettings(PresentSettings{.interpolation = false});

    static int object;
    mgPortCutInterpolation();
    RecordTicks(3, &object, 0.0f);

    const PresentStats stats = PresentGetStats();
    EXPECT_EQ(stats.ticks, 3u);
    EXPECT_EQ(stats.display_frames, 0u);
    EXPECT_EQ(stats.canonical_presents, 3u);
    EXPECT_EQ(stats.presented, 3u);
}

TEST(MglibPresent, DisplayPerTickRendersAndDiscards) {
    PresentRenderer renderer(640, 480);
    PresentResetStats();
    PresentSetSettings(PresentSettings{.interpolation = true, .display_per_tick = 4});

    static int object;
    mgPortCutInterpolation();
    RecordTicks(3, &object, 0.0f);

    const PresentStats stats = PresentGetStats();
    EXPECT_EQ(stats.ticks, 3u);
    EXPECT_EQ(stats.display_frames, 12u);
    EXPECT_EQ(stats.canonical_presents, 3u);
    EXPECT_EQ(stats.presented, 3u);
}

TEST(MglibPresent, DisplayPerTickLeavesCanonicalImage) {
    PresentRenderer renderer(640, 480);
    PresentResetStats();

    static int object;
    std::vector<uint8_t> with_display;
    std::vector<uint8_t> without_display;
    uint32_t             width = 0;
    uint32_t             height = 0;

    PresentSetSettings(PresentSettings{.interpolation = true, .display_per_tick = 4});
    mgPortCutInterpolation();
    RecordTicks(3, &object, 0.0f);
    ASSERT_TRUE(mgPortCanonicalReadColor(gfx::kMainTarget, with_display, width, height));

    PresentSetSettings(PresentSettings{.interpolation = true, .display_per_tick = 0});
    mgPortCutInterpolation();
    RecordTicks(3, &object, 0.0f);
    ASSERT_TRUE(mgPortCanonicalReadColor(gfx::kMainTarget, without_display, width, height));

    // Equal images only prove something if the tick's quad is in them.
    double centroid = 0.0;
    RedCentroidX(with_display, width, height, centroid);
    EXPECT_EQ(with_display, without_display);
}

TEST(MglibSeam, CopyPaletteCopiesEntries) {
    PresentRenderer renderer(640, 480);
    Textures        textures;

    Quads indices = Pack(std::vector<uint8_t>(16, 1));
    Quads clut_a = Pack(Clut8());
    Quads clut_b = Pack(std::vector<uint8_t>(256 * 4, 0x66));
    mgCTexture *a = textures.Enter("seam_pal_a", 4, 4, 8, indices.data(), clut_a.data());
    mgCTexture *b = textures.Enter("seam_pal_b", 4, 4, 8, indices.data(), clut_b.data());
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    const gfx::TextureHandle a_palette = mgPortFindTexture(a).palette;
    const gfx::TextureHandle b_palette = mgPortFindTexture(b).palette;
    ASSERT_NE(a_palette, gfx::kNullTexture);
    ASSERT_NE(b_palette, gfx::kNullTexture);

    uint32_t              width = 0;
    uint32_t              height = 0;
    const std::vector<uint8_t> source = ReadTexture(a_palette, width, height);
    const std::vector<uint8_t> before = ReadTexture(b_palette, width, height);
    ASSERT_EQ(source.size(), 256u * 4u);
    ASSERT_NE(before, source);

    mgPortCopyPalette(a, b);
    EXPECT_EQ(ReadTexture(b_palette, width, height), source);
}

TEST(MglibSeam, CopyPaletteByTex0CopiesEntries) {
    PresentRenderer renderer(640, 480);
    Textures        textures;

    Quads indices = Pack(std::vector<uint8_t>(16, 1));
    Quads clut_a = Pack(Clut8());
    Quads clut_b = Pack(std::vector<uint8_t>(256 * 4, 0x66));
    mgCTexture *a = textures.Enter("seam_tex0_a", 4, 4, 8, indices.data(), clut_a.data());
    mgCTexture *b = textures.Enter("seam_tex0_b", 4, 4, 8, indices.data(), clut_b.data());
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    const gfx::TextureHandle a_palette = mgPortFindTexture(a).palette;
    const gfx::TextureHandle b_palette = mgPortFindTexture(b).palette;
    ASSERT_NE(a_palette, gfx::kNullTexture);
    ASSERT_NE(b_palette, gfx::kNullTexture);

    uint32_t                   width = 0;
    uint32_t                   height = 0;
    const std::vector<uint8_t> source = ReadTexture(a_palette, width, height);
    ASSERT_NE(ReadTexture(b_palette, width, height), source);

    mgPortCopyPalette(Tex0Bits(a), Tex0Bits(b));
    EXPECT_EQ(ReadTexture(b_palette, width, height), source);
}

TEST(MglibSeam, DrawIntoRegisteredTextureSeedsAndResolves) {
    PresentRenderer renderer(640, 480);
    Textures        textures;

    Quads       pixels = Pack(Fill(64 * 64, 0x20, 0x40, 0x80, 0x80));
    mgCTexture *texture = textures.Enter("seam_draw", 64, 64, 32, pixels.data(), nullptr);
    ASSERT_NE(texture, nullptr);

    mgBeginFrame(nullptr);
    mgSetPkFrameBuffer(texture);
    const std::array<gfx::Vertex2D, 4> quad = {
        QuadCorner(0.0f, 0.0f),
        QuadCorner(16.0f, 0.0f),
        QuadCorner(16.0f, 16.0f),
        QuadCorner(0.0f, 16.0f),
    };
    mgPortDraw2D(gfx::Primitive::Quads, quad, gfx::TextureBinding{}, mgPortDrawState());
    mgSetPkFrameBuffer(-1, -1, -1, -1);
    mgEndFrame(nullptr);

    const gfx::TextureHandle target = mgPortFindTexture(texture).texture;
    ASSERT_NE(target, gfx::kNullTexture);
    EXPECT_NE(target, gfx::kMainTarget);
    EXPECT_EQ(mgPortFindTexture(Tex0Bits(texture)).texture, target);

    uint32_t                   width = 0;
    uint32_t                   height = 0;
    const std::vector<uint8_t> rgba = ReadTexture(target, width, height);
    const gfx::LogicalMapping  mapping = gfx::GetLogicalMapping(target);

    const size_t inside = PixelOffset(mapping, width, 8.0f, 8.0f);
    EXPECT_NEAR(rgba[inside + 0], 255, 1);
    EXPECT_NEAR(rgba[inside + 1], 255, 1);
    EXPECT_NEAR(rgba[inside + 2], 255, 1);

    const size_t seeded = PixelOffset(mapping, width, 40.0f, 40.0f);
    EXPECT_NEAR(rgba[seeded + 0], 0x20, 1);
    EXPECT_NEAR(rgba[seeded + 1], 0x40, 1);
    EXPECT_NEAR(rgba[seeded + 2], 0x80, 1);
}

TEST(MglibSeam, ReloadedTextureDropsItsTarget) {
    PresentRenderer renderer(640, 480);
    Textures        textures;

    Quads       pixels = Pack(Fill(64 * 64, 0x20, 0x40, 0x80, 0x80));
    mgCTexture *texture = textures.Enter("seam_reload", 64, 64, 32, pixels.data(), nullptr);
    ASSERT_NE(texture, nullptr);
    const gfx::TextureHandle seeded_image = MgTextureLookup(texture).texture;
    ASSERT_NE(seeded_image, gfx::kNullTexture);

    mgBeginFrame(nullptr);
    mgSetPkFrameBuffer(texture);
    const std::array<gfx::Vertex2D, 4> quad = {
        QuadCorner(0.0f, 0.0f),
        QuadCorner(16.0f, 0.0f),
        QuadCorner(16.0f, 16.0f),
        QuadCorner(0.0f, 16.0f),
    };
    mgPortDraw2D(gfx::Primitive::Quads, quad, gfx::TextureBinding{}, mgPortDrawState());
    mgSetPkFrameBuffer(-1, -1, -1, -1);
    mgEndFrame(nullptr);

    const gfx::TextureHandle old_target = mgPortFindTexture(texture).texture;
    ASSERT_NE(old_target, gfx::kNullTexture);
    ASSERT_NE(old_target, gfx::kMainTarget);

    // Re-entering a live name takes a new slot, so the old texture is deleted first.
    textures.Delete(texture);
    Quads reloaded_pixels = Pack(Fill(64 * 64, 0x90, 0x10, 0x10, 0x80));
    ASSERT_EQ(textures.Enter("seam_reload", 64, 64, 32, reloaded_pixels.data(), nullptr), texture);

    const gfx::TextureHandle reloaded_image = MgTextureLookup(texture).texture;
    ASSERT_NE(reloaded_image, gfx::kNullTexture);
    ASSERT_NE(reloaded_image, seeded_image);

    EXPECT_EQ(mgPortFindTexture(texture).texture, reloaded_image);
    EXPECT_NE(mgPortFindTexture(texture).texture, old_target);
}

TEST(MglibSeam, StretchRectIsExclusive) {
    PresentRenderer renderer(640, 480);
    Textures        textures;

    Quads       source_px = Pack(Fill(64 * 64, 0x20, 0x40, 0x80, 0x80));
    Quads       dest_px = Pack(Fill(64 * 64, 0x10, 0x10, 0x10, 0x80));
    mgCTexture *source = textures.Enter("seam_stretch_src", 64, 64, 32, source_px.data(), nullptr);
    mgCTexture *dest = textures.Enter("seam_stretch_dst", 64, 64, 32, dest_px.data(), nullptr);
    ASSERT_NE(source, nullptr);
    ASSERT_NE(dest, nullptr);

    const mgRect<int> eight_texels(0, 0, 8 * 16, 8 * 16);
    mgSetPkMoveImage(source, eight_texels, dest, eight_texels, nullptr);

    const gfx::TextureHandle image = mgPortFindTexture(dest).texture;
    ASSERT_NE(image, gfx::kNullTexture);
    uint32_t                   width = 0;
    uint32_t                   height = 0;
    const std::vector<uint8_t> rgba = ReadTexture(image, width, height);

    const size_t last_inside = (static_cast<size_t>(7) * width + 7) * 4;
    const size_t first_outside = (static_cast<size_t>(8) * width + 8) * 4;
    EXPECT_NEAR(rgba[last_inside + 0], 0x20, 1);
    EXPECT_NEAR(rgba[last_inside + 2], 0x80, 1);
    EXPECT_NEAR(rgba[first_outside + 0], 0x10, 1);
    EXPECT_NEAR(rgba[first_outside + 2], 0x10, 1);
}

TEST(MglibPresent, MoveImageOfTheFrameUsesAFrameTarget) {
    PresentRenderer renderer(1280, 720);
    Textures        textures;

    Quads       frame_dst_px = Pack(Fill(64 * 64, 0x10, 0x10, 0x10, 0x80));
    Quads       plain_dst_px = Pack(Fill(64 * 64, 0x10, 0x10, 0x10, 0x80));
    Quads       plain_src_px = Pack(Fill(64 * 64, 0x20, 0x40, 0x80, 0x80));
    mgCTexture *frame_dst = textures.Enter("move_frame_dst", 64, 64, 32, frame_dst_px.data(), nullptr);
    mgCTexture *plain_dst = textures.Enter("move_plain_dst", 64, 64, 32, plain_dst_px.data(), nullptr);
    mgCTexture *plain_src = textures.Enter("move_plain_src", 64, 64, 32, plain_src_px.data(), nullptr);
    ASSERT_NE(frame_dst, nullptr);
    ASSERT_NE(plain_dst, nullptr);
    ASSERT_NE(plain_src, nullptr);

    mgSetPkFrameBuffer(-1, -1, -1, -1);
    mgCTexture frame;
    mgGetFrameBuffer(&frame);
    const mgRect<int> whole(0, 0, (mgScreenWidth - 1) * 16, (mgScreenHeight - 1) * 16);

    mgBeginFrame(nullptr);
    mgSetPkMoveImage(&frame, whole, frame_dst, 0, 0, 0);
    mgSetPkMoveImage(plain_src, whole, plain_dst, 0, 0, 0);
    mgEndFrame(nullptr);

    const gfx::TextureHandle target = mgPortFindTexture(frame_dst).texture;
    ASSERT_NE(target, gfx::kNullTexture);
    ASSERT_NE(target, gfx::kMainTarget);
    EXPECT_EQ(mgPortFindTexture(Tex0Bits(frame_dst)).texture, target);

    const std::optional<gfx::TextureInfo> frame_info = gfx::GetTextureInfo(target);
    ASSERT_TRUE(frame_info.has_value());
    EXPECT_TRUE(frame_info->render_target);
    EXPECT_TRUE(frame_info->frame_target);

    const gfx::TextureHandle registered = mgPortRenderTarget(static_cast<uint32_t>(frame_dst->tex0.TBP0), 64, 64,
                                                             static_cast<int>(frame_dst->tex0.PSM), true);
    EXPECT_EQ(registered, target);

    const std::optional<gfx::TextureInfo> plain_info = gfx::GetTextureInfo(mgPortFindTexture(plain_dst).texture);
    ASSERT_TRUE(plain_info.has_value());
    EXPECT_FALSE(plain_info->frame_target);
}
