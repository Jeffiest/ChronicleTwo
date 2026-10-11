#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#include <SDL3/SDL.h>
#include "color_capture.hpp"
#include "mglib.hpp"
#include "menudraw.hpp"
#include "platform/clock.hpp"
#include "platform/window.hpp"

namespace {
void PixelPackingAndBounds() {
    const uint8_t pattern[] = {
        248, 0, 0, 255, 0, 248, 0, 128,
        0, 0, 248, 0, 248, 248, 248, 255,
    };
    uint8_t out[20];
    std::memset(out, 0xA5, sizeof(out));
    assert(!port_capture::PackColor(pattern, sizeof(pattern), 2, 2, 32, out + 2, 15));
    for (uint8_t value : out) assert(value == 0xA5);
    assert(port_capture::PackColor(pattern, sizeof(pattern), 2, 2, 32, out + 2, 16));
    assert(out[0] == 0xA5 && out[1] == 0xA5 && out[18] == 0xA5 && out[19] == 0xA5);
    assert(out[5] == 128 && out[9] == 64 && out[13] == 0 && out[17] == 128);
    assert(port_capture::PackColor(pattern, sizeof(pattern), 2, 2, 16, out, sizeof(out)));
    assert(out[0] == 0x1F && out[1] == 0x80); // red, alpha 1
    assert(out[2] == 0xE0 && out[3] == 0x03); // native alpha128 -> GS64 -> bit7 zero
    assert(out[4] == 0 && out[5] == 0x7C); // blue, alpha 0
    assert(port_capture::PackColor(pattern, sizeof(pattern), 2, 2, 24, out, sizeof(out)));
    assert(out[0] == 248 && out[3] == 0 && out[4] == 248 && out[8] == 248);
    assert(!port_capture::PackColor(pattern, sizeof(pattern), 2, 2, 8, out, sizeof(out)));
    uint8_t half[16];
    assert(port_capture::HalfColor(pattern, sizeof(pattern), 2, 2, half, sizeof(half), 2, 2));
    assert(half[0] == 124 && half[1] == 124 && half[2] == 124 && half[3] == 128);
    for (size_t i = 4; i < sizeof(half); ++i) assert(half[i] == 0);
    std::memset(half, 0xA5, sizeof(half));
    assert(!port_capture::HalfColor(pattern, sizeof(pattern), 2, 2, half, 15, 2, 2));
    for (uint8_t value : half) assert(value == 0xA5);
}

void CanonicalPattern(int width, int height) {
    const uint8_t colors[4][4] = {{248, 0, 0, 128}, {0, 248, 0, 128},
                                  {0, 0, 248, 128}, {248, 248, 0, 128}};
    gfx::BeginRecording();
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x) {
        gfx::LogicalRect rect{float(x * width / 2), float(y * height / 2), float(width / 2), float(height / 2)};
        gfx::Clear(true, colors[y * 2 + x], true, 0, &rect);
    }
    auto list = gfx::EndRecording();
    gfx::RenderOptions canonical;
    canonical.canonical = true;
    assert(list && gfx::RenderList(*list, 1, canonical));
}

void RendererCapture() {
    WindowConfig window{80, 60, true};
    window.vulkan = false;
    WindowInit(window);
    gfx::RendererConfig config;
    config.app_name = "Color capture component test";
    config.logical_width = 8;
    config.logical_height = 8;
    config.display_aspect = 1;
    config.validation = false;
    config.offscreen = true;
    config.pipeline_cache.clear();
    gfx::RendererInit(WindowHandle(), config);
    mgDBuff.draw0.frame1.FBP = 0;
    mgDBuff.draw1.frame1.FBP = 1;
    mgCTexture frame;
    frame.Initialize();
    frame.width = frame.height = 8;
    frame.bpp = 32;
    frame.tex0.value = 0;
    frame.tex0.PSM = SCE_GS_PSMCT32;
    std::vector<u_long128> storage(8 * 8 / 4 + 2);
    auto *bytes = reinterpret_cast<uint8_t *>(storage.data());
    auto check = [&]() {
        std::memset(bytes, 0xA5, storage.size() * 16);
        assert(mgStoreImage(&frame, storage.data() + 1) == 8 * 8 * 32);
        for (int i = 0; i < 16; ++i) assert(bytes[i] == 0xA5 && bytes[16 + 256 + i] == 0xA5);
        const uint8_t *pixels = bytes + 16;
        assert(pixels[0] == 248 && pixels[1] == 0 && pixels[2] == 0 && pixels[3] == 128);
        assert(pixels[7 * 4 + 1] == 248); // top-right green
        assert(pixels[7 * 8 * 4 + 2] == 248); // bottom-left blue, no row inversion
        assert(pixels[(7 * 8 + 7) * 4] == 248 && pixels[(7 * 8 + 7) * 4 + 1] == 248);
    };
    CanonicalPattern(8, 8);
    check();
    uint8_t bounded[16];
    std::memset(bounded, 0xA5, sizeof(bounded));
    assert(!port_capture::ReadColor(gfx::kPreviousFrame, {7, 0, 2, 2}, 2, 2, 32, bounded, sizeof(bounded)));
    for (uint8_t value : bounded) assert(value == 0xA5);
    // A second canonical tick at a different physical extent must retain logical coordinates.
    SDL_SetWindowSize(WindowHandle(), 120, 90);
    gfx::RendererResize();
    CanonicalPattern(8, 8);
    check();

    alignas(16) uint8_t source[16] = {8, 16, 24, 128, 32, 40, 48, 64,
                                      56, 64, 72, 0, 80, 88, 96, 128};
    mgCTexture texture;
    texture.Initialize();
    texture.width = texture.height = 2;
    texture.bpp = 32;
    texture.tex0.value = 0;
    texture.tex0.PSM = SCE_GS_PSMCT32;
    texture.image[0] = reinterpret_cast<u_long128 *>(source);
    alignas(16) uint8_t copied[16]{};
    assert(mgStoreImage(&texture, reinterpret_cast<u_long128 *>(copied)) == 128);
    assert(std::memcmp(source, copied, sizeof(source)) == 0); // explicit texture, not the main image

    // The bounded backend refuses deferred reads and leaves the destination untouched.
    gfx::BeginRecording();
    std::memset(copied, 0xA5, sizeof(copied));
    assert(!port_capture::ReadColor(gfx::kPreviousFrame, {0, 0, 8, 8}, 2, 2, 32, copied, sizeof(copied)));
    for (uint8_t value : copied) assert(value == 0xA5);
    gfx::EndRecording();

    // Execute the actual menu transition with real game objects and a tiny manager pool. The
    // capture must observe this completed tick, reserve padded pixels, then leave a new list open.
    mgInit(MG_SCREEN_MODE_512X448, 0);
    ClockSetUnbounded(true);
    mgCTextureBlock blocks[1];
    mgCTexture texture_pool[2];
    mgCTexture *texture_stack[2];
    mgTEXTURE_HASH hashes[2];
    mgTEXTURE_HASH *hash_stack[2];
    mgTexManager.blocks = blocks;
    mgTexManager.block_max = 1;
    mgTexManager.texture_buf = texture_pool;
    mgTexManager.texture_stack = texture_stack;
    mgTexManager.texture_max = 2;
    mgTexManager.hash_buf = hashes;
    mgTexManager.hash_stack = hash_stack;
    mgTexManager.hash_max = 2;
    mgTexManager.Initialize(256, 0x3FE0);
    mgBeginFrame(NULL);
    mgSetPkFrameBuffer(-1, 8, 8, SCE_GS_PSMCT32);
    const uint8_t menu_colors[4][4] = {{248, 0, 0, 128}, {0, 248, 0, 128},
                                       {0, 0, 248, 128}, {248, 248, 0, 128}};
    for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x) {
        gfx::LogicalRect rect{float(x * 4), float(y * 4), 4, 4};
        gfx::Clear(true, menu_colors[y * 2 + x], true, 0, &rect);
    }
    std::vector<u_long128> menu_storage(1024 + 8);
    std::memset(menu_storage.data(), 0xA5, menu_storage.size() * 16);
    mgCMemory menu_stack;
    menu_stack.stSetBuffer(menu_storage.data(), static_cast<int>(menu_storage.size()));
    assert(MenuCapture(0, &menu_stack, 0));
    assert(gfx::Recording());
    assert(MenuFrameTex && MenuFrameTex->width == 64 && MenuFrameTex->height == 64);
    assert(menu_stack.stack_used >= 1024 && menu_stack.stack_used <= 1027);
    const uint8_t *menu = reinterpret_cast<const uint8_t *>(MenuFrameTex->image[0]);
    assert(menu[0] == 248 && menu[1] == 0 && menu[2] == 0 && menu[3] == 128);
    assert(menu[3 * 4 + 1] == 248);
    assert(menu[3 * 64 * 4 + 2] == 248);
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) {
        if (x >= 4 || y >= 4) for (int c = 0; c < 4; ++c) assert(menu[(y * 64 + x) * 4 + c] == 0);
    }
    const uint8_t *unused = reinterpret_cast<const uint8_t *>(menu_storage.data()) + menu_stack.stack_used * 16;
    for (int i = 0; i < 16; ++i) assert(unused[i] == 0xA5);
    gfx::EndRecording();
    mgTexManager.DeleteBlock(0);
    MenuFrameTex = NULL;
    mgTexManager.blocks = NULL;
    mgTexManager.block_max = 0;
    gfx::RendererShutdown();
    WindowShutdown();
}
}

int main() {
    PixelPackingAndBounds();
    RendererCapture();
    std::puts("color capture: packing, bounds, averaging, canonical orientation, resize, texture source, menu transition passed");
}
