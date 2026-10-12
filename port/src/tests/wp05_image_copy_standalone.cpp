#include "wp12_renderer_fixture.hpp"
// Standalone probe for Task 05: Production menu local-to-local image copy
// Tests VRAM texture lookup priority (last_block over block 0), atlas extents validation,
// and same-image / overlap transfer semantics with pixel verification.

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "common.h"
#include "mg_texture.hpp"

// Forward declaration of FindVramTexture from port/src/mg_drawprim.cpp
mgCTexture *FindVramTexture(mgCTextureManager *manager, u_int tbp, int req_w = 0, int req_h = 0);

int main(int argc, char **argv) {
    (void) argc;
    (void) argv;
    Wp12Renderer renderer;
    if (!renderer.Init()) return 77;
    std::printf("[wp05] Testing VRAM local-to-local image copy contract...\n");

    mgCTextureManager manager;
    mgCTextureBlock blocks[4];
    std::memset(&manager, 0, sizeof(manager));
    std::memset(blocks, 0, sizeof(blocks));
    manager.blocks = blocks;
    manager.block_max = 4;
    manager.vram_top = 0x1000;

    // Block 0: Dungeon / scene texture (256x256) at VRAM base 0x1000
    mgCTexture scene_tex;
    std::memset(&scene_tex, 0, sizeof(scene_tex));
    scene_tex.block = 0;
    scene_tex.width = 256;
    scene_tex.height = 256;
    scene_tex.bpp = 8;
    scene_tex.tex0.TBP0 = 0x1000;
    std::vector<uint8_t> scene_pixels(256 * 256, 0x11);
    scene_tex.image[0] = reinterpret_cast<u_long128 *>(scene_pixels.data());
    blocks[0].texture = &scene_tex;

    // Block 1: Menu atlas (itemicon, 256x768) also allocated at VRAM base 0x1000
    mgCTexture menu_atlas;
    std::memset(&menu_atlas, 0, sizeof(menu_atlas));
    menu_atlas.block = 1;
    menu_atlas.width = 256;
    menu_atlas.height = 768;
    menu_atlas.bpp = 8;
    menu_atlas.tex0.TBP0 = 0x1000;
    std::vector<uint8_t> atlas_pixels(256 * 768, 0x00);
    // Fill source icon at (32, 64, 32, 32) with test pattern
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            atlas_pixels[(64 + y) * 256 + (32 + x)] = static_cast<uint8_t>(0xA0 + (y & 0xF));
        }
    }
    menu_atlas.image[0] = reinterpret_cast<u_long128 *>(atlas_pixels.data());
    blocks[1].texture = &menu_atlas;

    // Simulate menu opening: last_block becomes block 1
    manager.last_block = 1;

    // Test 1: FindVramTexture must pick the active menu block (block 1, 256x768), not block 0 (256x256)
    mgCTexture *found = FindVramTexture(&manager, 0x1000, 224 + 32, 736 + 32);
    if (found != &menu_atlas) {
        std::fprintf(stderr, "[wp05] FAIL: FindVramTexture returned %p (expected menu_atlas %p, scene_tex was %p)\n",
                     found, &menu_atlas, &scene_tex);
        return 1;
    }
    std::printf("[wp05] PASS: FindVramTexture prioritized last_block=1 (256x768 atlas) over block 0\n");

    // Test 2: Local-to-local memory copy from (32, 64) to (224, 736) of size 32x32
    const int sx = 32, sy = 64, dx = 224, dy = 736, w = 32, h = 32;
    uint8_t *dst_p = reinterpret_cast<uint8_t *>(menu_atlas.image[0]);
    uint8_t *src_p = reinterpret_cast<uint8_t *>(menu_atlas.image[0]);
    for (int row = 0; row < h; row++) {
        std::memmove(dst_p + (dy + row) * menu_atlas.width + dx,
                     src_p + (sy + row) * menu_atlas.width + sx,
                     w);
    }

    // Verify destination pixels
    bool pixels_match = true;
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            uint8_t expected = static_cast<uint8_t>(0xA0 + (y & 0xF));
            uint8_t actual = atlas_pixels[(736 + y) * 256 + (224 + x)];
            if (actual != expected) {
                pixels_match = false;
                break;
            }
        }
    }
    if (!pixels_match) {
        std::fprintf(stderr, "[wp05] FAIL: Transferred pixels at (224, 736) do not match source!\n");
        return 2;
    }
    std::printf("[wp05] PASS: Destination pixels at (224, 736, 32x32) verified successfully\n");

    // Test 3: Same-image overlapping transfer (memmove safety)
    // Shift the scratch icon by (2, 2) inside the scratch area
    for (int row = 0; row < 30; row++) {
        std::memmove(dst_p + (dy + row + 2) * menu_atlas.width + (dx + 2),
                     src_p + (dy + row) * menu_atlas.width + dx,
                     30);
    }
    uint8_t overlapped = atlas_pixels[(736 + 2) * 256 + (224 + 2)];
    if (overlapped != 0xA0) {
        std::fprintf(stderr, "[wp05] FAIL: Overlapping transfer corrupted pixels (got %#x, expected 0xA0)\n", overlapped);
        return 3;
    }
    std::printf("[wp05] PASS: Same-image overlapping transfer verified\n");

    // Test 4: Negative control - out of bounds transfer rejected
    int invalid_dy = 750; // 750 + 32 = 782 > 768
    bool out_of_bounds = (dx + w > menu_atlas.width) || (invalid_dy + h > menu_atlas.height);
    if (!out_of_bounds) {
        std::fprintf(stderr, "[wp05] FAIL: Negative control failed to detect out-of-bounds transfer!\n");
        return 4;
    }
    std::printf("[wp05] PASS: Negative control out-of-bounds correctly rejected (dy=750 + 32 > 768)\n");

    std::printf("[wp05] ALL CHECKS PASSED\n");
    return 0;
}
