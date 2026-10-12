#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "gfx/gfx.hpp"
#include "mg_drawprim.hpp"
#include "mg_drawprim_port.hpp"
#include "mglib.hpp"
#include "mglib_port.hpp"
#include "nd_meswin.hpp"

namespace {

struct Batch {
    gfx::Primitive             primitive;
    std::vector<gfx::Vertex2D> vertices;
    gfx::TextureBinding        texture;
    gfx::DrawState             state;
};

// FillRect initialises its builder with NULL, so it binds to the global draw manager and packet.
class GlobalDrawRig {
public:
    GlobalDrawRig() : saved_render_info_(mgDrawManager.render_info), saved_packet_(mgVif1Packet) {
        mgDrawManager.render_info = &render_info_;
        mgVif1Packet = &vif_packet_;
        mgPortSetPrimSink([this](gfx::Primitive primitive, std::span<const gfx::Vertex2D> vertices,
                                 const gfx::TextureBinding &texture, const gfx::DrawState &state) {
            batches.push_back({primitive, {vertices.begin(), vertices.end()}, texture, state});
        });
    }

    ~GlobalDrawRig() {
        mgPortSetPrimSink({});
        mgDrawManager.render_info = saved_render_info_;
        mgVif1Packet = saved_packet_;
    }

    GlobalDrawRig(const GlobalDrawRig &) = delete;
    GlobalDrawRig &operator=(const GlobalDrawRig &) = delete;

    std::vector<Batch> batches;

private:
    mgRENDER_INFO       render_info_{};
    sceVif1Packet       vif_packet_{};
    mgRENDER_INFO      *saved_render_info_;
    sceVif1Packet      *saved_packet_;
};

int Gs(float pixels) {
    return static_cast<int>(pixels * 16.0f);
}

void ExpectAt(const gfx::Vertex2D &v, float x, float y) {
    EXPECT_FLOAT_EQ(v.x, mgPortLogicalX(Gs(x)));
    EXPECT_FLOAT_EQ(v.y, mgPortLogicalY(Gs(y)));
}

std::array<uint8_t, 4> Rgba(const gfx::Vertex2D &v) {
    return {v.color[0], v.color[1], v.color[2], v.color[3]};
}

} // namespace

TEST(MesWinFillRect, BackingCoversFrameOpening) {
    GlobalDrawRig rig;

    FillRect(159, 346, 197, 47, 0, 0, 0, 54);

    ASSERT_EQ(rig.batches.size(), 1u);
    const Batch &batch = rig.batches[0];
    EXPECT_EQ(batch.primitive, gfx::Primitive::Quads);
    EXPECT_EQ(batch.texture.texture, gfx::kNullTexture);
    EXPECT_TRUE(batch.state.blend);

    ASSERT_EQ(batch.vertices.size(), 4u);
    ExpectAt(batch.vertices[0], 158.0f, 345.0f);
    ExpectAt(batch.vertices[1], 355.0f, 345.0f);
    ExpectAt(batch.vertices[2], 355.0f, 392.0f);
    ExpectAt(batch.vertices[3], 158.0f, 392.0f);
    for (const gfx::Vertex2D &v : batch.vertices) {
        EXPECT_EQ(Rgba(v), (std::array<uint8_t, 4>{0, 0, 0, 54}));
    }
}
