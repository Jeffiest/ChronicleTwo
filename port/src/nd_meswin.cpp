#include "common.h"

#include "mg_drawprim.hpp"
#include "nd_meswin.hpp"

// Callers place the backing one pixel right of and below the gaiji frame's opening, which leaves the opening's
// top and left edges undarkened. Drawing one pixel up and left covers them.
PORT_OVERRIDE void FillRect(int x, int y, int w, int h, int r, int g, int b, int a) {
    mgCDrawPrim drawer;
    drawer.Initialize(NULL, NULL);
    drawer.AlphaBlendEnable(1);
    drawer.AlphaBlend(MG_ALPHA_BLEND_NORMAL);
    drawer.AlphaTestEnable(1);
    drawer.AlphaTest(1, 0);
    drawer.DepthTestEnable(0);
    drawer.ZMask(MG_Z_MASK_MASKED);
    drawer.Bilinear(0);
    drawer.TextureMapEnable(0);
    drawer.Begin(MG_PRIM_SPRITE);
    drawer.Color(r, g, b, a);
    drawer.Vertex(x - 1, y - 1, 0);
    drawer.Vertex(x - 1 + w, y - 1 + h, 0);
    drawer.End();
}
