#pragma once
#include <cstdint>
#include "gfx/gfx.hpp"
class mgCTexture;
struct MgTextureHandles {
    gfx::TextureHandle texture = gfx::kNullTexture;
    gfx::TextureHandle palette = gfx::kNullTexture;
};
// TBP0 and CBP of a registered texture hold its registry key, not a VRAM address. Keys are handed out
// downward from the top of the 14-bit field so they stay clear of the frame buffers' addresses.
inline constexpr uint32_t kMgTextureKeyFirst = 0x3FFF;
inline constexpr uint32_t kMgTextureKeyLast = 0x2800;
// Uploads on first use and again once the texture's pixel or CLUT pointers changed; null handles before RendererInit.
MgTextureHandles MgTextureLookup(const mgCTexture *texture);
// Image from TBP0's texture, palette from CBP's.
MgTextureHandles MgTextureLookupTex0(uint64_t tex0);
