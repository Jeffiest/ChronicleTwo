#include "common.h"

#include <algorithm>
#include <vector>

#include "mg_drawprim.hpp"
#include "mg_drawprim_port.hpp"
#include "mglib.hpp"

namespace {

gfx::Vertex2D EdgeVertex(const mgCDrawPrim &prim, float x, float y, const uint8_t rgba[4]) {
    return mgPortPrimVertex(prim, x, y, 0.0f, x, y, rgba, 0);
}

// Corner order matches the builder's own sprite assembly: top-left, top-right, bottom-right, bottom-left.
void AppendSprite(std::vector<gfx::Vertex2D> &out, const mgCDrawPrim &prim, float x0, float y0, float x1, float y1,
                  const uint8_t rgba[4]) {
    out.push_back(EdgeVertex(prim, x0, y0, rgba));
    out.push_back(EdgeVertex(prim, x1, y0, rgba));
    out.push_back(EdgeVertex(prim, x1, y1, rgba));
    out.push_back(EdgeVertex(prim, x0, y1, rgba));
}

} // namespace

PORT_OVERRIDE void DivSpriteScreen(mgCDrawPrim &prim) {
    uint8_t rgba[4];
    mgPortPrimColor(prim, rgba);

    std::vector<gfx::Vertex2D> quads;

    for (int x = 0; x < mgScreenWidth; x += 64) {
        for (int y = 0; y < mgScreenHeight; y += 32) {
            AppendSprite(quads, prim, static_cast<float>(x), static_cast<float>(y), static_cast<float>(x + 64),
                         static_cast<float>(y + 32), rgba);
        }
    }

    mgPortPrimSubmit(prim, gfx::Primitive::Quads, quads);
}

PORT_OVERRIDE void DivSpriteScreen(mgCDrawPrim &prim, int left, int right, int jagged_left) {
    uint8_t rgba[4];
    mgPortPrimColor(prim, rgba);

    const int row_height = mgScreenHeight / 16;
    const int edge_offset[2] = {-10, 10};
    std::vector<gfx::Vertex2D> strip;

    for (int row = 0; row < 17; row++) {
        const float y = static_cast<float>(row * row_height);
        const int   left_x = std::max(0, jagged_left ? left + edge_offset[row % 2] : left);
        const int   right_x = std::max(0, jagged_left ? right : right + edge_offset[row % 2]);
        strip.push_back(EdgeVertex(prim, static_cast<float>(left_x), y, rgba));
        strip.push_back(EdgeVertex(prim, static_cast<float>(right_x), y, rgba));
    }

    std::vector<gfx::Vertex2D> triangles;
    mgPortStripTriangles(strip, triangles);
    mgPortPrimSubmit(prim, gfx::Primitive::Triangles, triangles);
}
