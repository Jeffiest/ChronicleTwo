#include "common.h"

#include "menudraw.hpp"
#include "menumain.hpp"
#include "mg_drawprim.hpp"
#include "mg_memory.hpp"
#include "mg_texture.hpp"
#include "mglib.hpp"
#include "mglib_port.hpp"

PORT_OVERRIDE int MenuCapture(int block, mgCMemory *stack, int draw) {
    u8                *dst;
    mgCTextureManager *tex_manager;
    int                half_w;
    int                half_h;
    int                tex_w;
    int                tex_h;
    int                pad_w;
    int                pad_h;
    mgCDrawPrim       *prim;

    stack->Align64();
    tex_manager = &mgTexManager;
    dst = stack->stack_bytes + (stack->stack_used << 4);
    half_w = mgScreenWidth >> 1;
    half_h = mgScreenHeight >> 1;
    tex_w = half_w;
    MenuBGTextureBlock = block;
    tex_h = half_h;
    pad_w = half_w % 64;
    pad_h = half_h % 64;

    if (pad_w != 0) {
        tex_w += 64 - pad_w;
    }

    if (pad_h != 0) {
        tex_h += 64 - pad_h;
    }

    static char frame_name[] = "menuwork2";
    tex_manager->DeleteBlock(block);
    MenuFrameTex = tex_manager->EnterTexture(block, frame_name, NULL, tex_w, tex_h, 32, NULL, 0, 0);

    if (MenuFrameTex != NULL) {
        MenuFrameTex->tex0.bits.tcc = 0;
        MenuFrameTex->image[0] = (u_long128 *) dst;
        stack->Alloc(half_w * half_h * 4 / 16 + 1);
    }

    // The grab costs a frame on PS2 too: the tick that just ended is gfx::kPreviousFrame in the next one.
    mgEndFrame(NULL);
    mgBeginFrame(NULL);

    if (MenuFrameTex != NULL) {
        // Frame target: the window's sides past the logical frame are copied in and carried by the full-frame draw.
        const gfx::TextureHandle target = mgPortRenderTarget(MenuFrameTex, true);
        // Registered before the copy: until a target exists under its TBP0 the texture binds its stack image.
        // Linear at 2:1 samples each 2x2 block's midpoint, which is the box filter retail ran on the CPU.
        gfx::BlitTexture(gfx::kPreviousFrame, gfx::Rect{0, 0, mgScreenWidth, mgScreenHeight}, target,
                         gfx::Rect{0, 0, half_w, half_h}, gfx::Filter::Linear);
    }

    if (draw != 0 && MenuFrameTex != NULL) {
        tex_manager->ReloadTexture(block, (sceVif1Packet *) NULL);
        prim = GetMenuPrim();
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
        prim->TextureCrd(half_w, half_h);
        prim->Vertex(mgScreenWidth, mgScreenHeight, 0);
        prim->End();
    }

    return true;
}
