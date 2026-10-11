#include <cstdio>
#include <limits>
#include <vector>
#include "menudraw.hpp"
#include "menumain.hpp"
#include "mglib.hpp"
#include "color_capture.hpp"

PORT_OVERRIDE void CRepairManager::Generate(int x, int y) {
    int            slot = -1;
    CRepairEffect *effect;

    for (int i = 0; i < 8; i++) {
        if (this->effect[i] == NULL) {
            slot = i;
            break;
        }
    }

    if (0 <= slot) {
        mgCMemory *memory = &this->effect_stack[slot];

        memory->stack_used = 0;
        memory->lock = 0;
        this->effect[slot] = new (memory->Alloc(5)) CRepairEffect;

        if ((effect = this->effect[slot]) != NULL) {
            effect->Initialize();
            effect->x = x;
            effect->y = y;
            effect->sub_tex = sub_tex;
            effect->tex = tex;
            effect->Generate(memory, 0x20);
        }
    }
}

PORT_OVERRIDE int MenuCapture(int block, mgCMemory *stack, int draw) {
    if (stack == NULL || mgScreenWidth < 2 || mgScreenHeight < 2) return false;
    const int width = mgScreenWidth, height = mgScreenHeight;
    const int half_width = width / 2, half_height = height / 2;
    const int tex_width = (half_width + 63) & ~63;
    const int tex_height = (half_height + 63) & ~63;
    const size_t bytes = port_capture::ImageBytes(tex_width, tex_height, 32);
    const size_t source_bytes = port_capture::ImageBytes(width, height, 32);
    if (!bytes || !source_bytes || bytes / 16 > static_cast<size_t>(std::numeric_limits<int>::max())) return false;
    stack->Align64();
    // Reserve the full padded texture; retail reserved only its visible half-sized rectangle and
    // wrote the full-size readback into an unreserved stack tail.
    auto *destination = reinterpret_cast<uint8_t *>(stack->Alloc(static_cast<int>(bytes / 16)));
    if (destination == NULL) return false;
    std::vector<uint8_t> source(source_bytes);
    mgCTexture frame;
    mgGetFrameBuffer(&frame);
    mgEndFrame(NULL);
    // Host readback must finish before the next display list starts (and clears the main image).
    bool captured = frame.width == width && frame.height == height && frame.bpp == 32 &&
                    mgStoreImage(&frame, reinterpret_cast<u_long128 *>(source.data())) != 0;
    mgBeginFrame(NULL);
    if (!captured || !port_capture::HalfColor(source.data(), source.size(), width, height,
                                              destination, bytes, tex_width, tex_height)) {
        std::fprintf(stderr, "MenuCapture: completed-frame color readback failed\n");
        return false;
    }
    mgTexManager.DeleteBlock(block);
    MenuBGTextureBlock = block;
    MenuFrameTex = mgTexManager.EnterTexture(block, "menuwork2", NULL, tex_width, tex_height, 32, NULL, 0, 0);
    if (MenuFrameTex == NULL) return false;
    MenuFrameTex->tex0.bits.tcc = 0;
    MenuFrameTex->image[0] = reinterpret_cast<u_long128 *>(destination);
    if (draw != 0) {
        mgTexManager.ReloadTexture(block, static_cast<sceVif1Packet *>(NULL));
        mgCDrawPrim *prim = GetMenuPrim();
        prim->Initialize(NULL, NULL);
        prim->TextureMapEnable(1);
        prim->Bilinear(1);
        prim->AlphaTestEnable(0);
        prim->DepthTestEnable(0);
        prim->ZMask(MG_Z_MASK_MASKED);
        prim->Begin(MG_PRIM_SPRITE);
        prim->Texture(MenuFrameTex);
        prim->Color(0x80, 0x80, 0x80, 0x80);
        prim->TextureCrd(0, 0);
        prim->Vertex(0, 0, 0);
        prim->TextureCrd(half_width, half_height);
        prim->Vertex(width, height, 0);
        prim->End();
    }
    return true;
}
