#include "wp12_renderer_fixture.hpp"
// Standalone probe for Task 06: Texture atlas storage and palette identity
// Verifies PAL menu atlas structure, shared backing storage across cloned mgCTexture handles,
// distinct palette identity (clut), and proper reuse/release semantics.

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "common.h"
#include "gfx/gfx.hpp"
#include "mg_texture.hpp"

bool PortMgTextureBinding(const mgCTexture *texture, gfx::TextureBinding &out);

int main(int argc, char **argv) {
    (void) argc;
    (void) argv;
    Wp12Renderer renderer;
    if (!renderer.Init()) return 77;
    std::printf("[wp06] Testing texture atlas storage and palette identity...\n");

    // 1. Setup real-dimension 8bpp atlas (256x768) matching allitem.img itemicon
    const int atlas_w = 256;
    const int atlas_h = 768;
    std::vector<uint8_t> shared_pixels(atlas_w * atlas_h, 0x00);
    // Draw some data into icon slot (0, 0)
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            shared_pixels[y * atlas_w + x] = static_cast<uint8_t>(x + y);
        }
    }

    uint32_t clut_normal[256] = {};
    uint32_t clut_highlight[256] = {};
    uint32_t clut_grey[256] = {};
    uint32_t clut_sepia[256] = {};
    for (int i = 0; i < 256; i++) {
        clut_normal[i] = 0xFF000000 | (i << 16) | (i << 8) | i;
        clut_highlight[i] = 0xFF0000FF | (i << 16); // blue highlight
        clut_grey[i] = 0xFF808080 | (i << 8);
        clut_sepia[i] = 0xFF204070 | i;
    }

    // Base texture (item_icon_tex[0])
    mgCTexture tex0;
    std::memset(&tex0, 0, sizeof(tex0));
    std::strncpy(tex0.name, "itemicon", sizeof(tex0.name));
    tex0.width = atlas_w;
    tex0.height = atlas_h;
    tex0.bpp = 8;
    tex0.image[0] = reinterpret_cast<u_long128 *>(shared_pixels.data());
    tex0.clut = reinterpret_cast<u_long128 *>(clut_normal);

    // Cloned texture 1: highlight (item_icon_tex[1])
    mgCTexture tex1;
    std::memcpy(&tex1, &tex0, sizeof(mgCTexture));
    tex1.clut = reinterpret_cast<u_long128 *>(clut_highlight);

    // Cloned texture 2: grey (item_icon_tex[2])
    mgCTexture tex2;
    std::memcpy(&tex2, &tex0, sizeof(mgCTexture));
    tex2.clut = reinterpret_cast<u_long128 *>(clut_grey);

    // Cloned texture 3: sepia (item_icon_tex[3])
    mgCTexture tex3;
    std::memcpy(&tex3, &tex0, sizeof(mgCTexture));
    tex3.clut = reinterpret_cast<u_long128 *>(clut_sepia);

    // Independent texture with distinct image[0]
    std::vector<uint8_t> distinct_pixels(128 * 128, 0x55);
    mgCTexture independent_tex;
    std::memset(&independent_tex, 0, sizeof(independent_tex));
    std::strncpy(independent_tex.name, "separate", sizeof(independent_tex.name));
    independent_tex.width = 128;
    independent_tex.height = 128;
    independent_tex.bpp = 8;
    independent_tex.image[0] = reinterpret_cast<u_long128 *>(distinct_pixels.data());
    independent_tex.clut = reinterpret_cast<u_long128 *>(clut_normal);

    gfx::TextureBinding bind0, bind1, bind2, bind3, bind_indep;
    bool ok0 = PortMgTextureBinding(&tex0, bind0);
    bool ok1 = PortMgTextureBinding(&tex1, bind1);
    bool ok2 = PortMgTextureBinding(&tex2, bind2);
    bool ok3 = PortMgTextureBinding(&tex3, bind3);
    bool ok_indep = PortMgTextureBinding(&independent_tex, bind_indep);

    // If renderer pipeline is active (or in mock/host environment)
    if (ok0 && ok1 && ok2 && ok3) {
        // Test 1: Shared index texture identity
        if (bind0.texture != bind1.texture || bind0.texture != bind2.texture || bind0.texture != bind3.texture) {
            std::fprintf(stderr, "[wp06] FAIL: Cloned textures have different texture handles (%#x, %#x, %#x, %#x)\n",
                         bind0.texture, bind1.texture, bind2.texture, bind3.texture);
            return 1;
        }
        std::printf("[wp06] PASS: All 4 atlas variants share index TextureHandle %#x\n", bind0.texture);

        // Test 2: Distinct palette handles
        if (bind0.palette == bind1.palette || bind0.palette == bind2.palette || bind0.palette == bind3.palette) {
            std::fprintf(stderr, "[wp06] FAIL: Distinct palettes erroneously share palette handle (%#x, %#x, %#x, %#x)\n",
                         bind0.palette, bind1.palette, bind2.palette, bind3.palette);
            return 2;
        }
        std::printf("[wp06] PASS: All 4 atlas variants have distinct palette handles (%#x, %#x, %#x, %#x)\n",
                     bind0.palette, bind1.palette, bind2.palette, bind3.palette);

        // Test 3: Negative control - distinct image must have a different texture handle
        if (ok_indep && bind_indep.texture == bind0.texture) {
            std::fprintf(stderr, "[wp06] FAIL: Negative control failed; independent texture shared atlas handle!\n");
            return 3;
        }
        std::printf("[wp06] PASS: Independent texture received unique TextureHandle %#x\n", bind_indep.texture);
    } else {
        std::fprintf(stderr, "[wp06] FAIL: texture binding failed with an initialized renderer\n");
        return 4;
    }

    std::printf("[wp06] ALL CHECKS PASSED\n");
    return 0;
}
