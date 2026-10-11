#include "common.h"

#include <libgraph.h>
#include <libpkt.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "gfx/gfx.hpp"
#include "mg_memory.hpp"
#include "mg_texture.hpp"
#include "mglib.hpp"

// The host side of the texture manager. The ps2 bookkeeping - EnterTexture, EnterIMGFile, the
// name hash, the block lists, the TIM2 parse - is plain CPU work and still links from ps2/src.
// What cannot come from there is the VRAM transfer: the ps2 ReloadTexture writes VIF1 upload
// packets, and this unit replaces it with a decode of each texture block into renderer images,
// keyed by the mgCTexture the game hands around. The palette and the format live on the manager's
// own texture records, so every later TEX0 the engine builds resolves back to one image here.

namespace {

struct Entry {
    gfx::TextureHandle texture = gfx::kNullTexture;
    gfx::TextureHandle palette = gfx::kNullTexture;
    bool               dirty = true;
    bool               target = false; // a named render target: the renderer keeps it alive
    uint32_t           width = 0;
    uint32_t           height = 0;
    uint32_t           mips = 0;
    gfx::TextureFormat format = static_cast<gfx::TextureFormat>(0);
};

std::unordered_map<const mgCTexture *, Entry> g_entries;

bool RendererUp() {
    return gfx::PipelineCount() != 0;
}

void Release(Entry &entry) {
    if (!entry.target) {
        if (entry.texture != gfx::kNullTexture) {
            gfx::DestroyTexture(entry.texture);
        }
        if (entry.palette != gfx::kNullTexture) {
            gfx::DestroyTexture(entry.palette);
        }
    }
    entry = Entry{};
}

// 8-bit unswizzle tables: BlockConv32to8's lut inverted, per column parity. A swizzled 8-bit
// texture in an IMG archive is the image in PS2 page order, the same layout DC1's tables invert.
struct InverseLut {
    uint8_t to_output[2][64];

    constexpr InverseLut() : to_output{} {
        constexpr uint8_t lut[128] = {
            0,  36, 8,  44, 1,  37, 9,  45, 2,  38, 10, 46, 3,  39, 11, 47, 4,  32, 12, 40, 5,  33, 13, 41,
            6,  34, 14, 42, 7,  35, 15, 43, 16, 52, 24, 60, 17, 53, 25, 61, 18, 54, 26, 62, 19, 55, 27, 63,
            20, 48, 28, 56, 21, 49, 29, 57, 22, 50, 30, 58, 23, 51, 31, 59, 4,  32, 12, 40, 5,  33, 13, 41,
            6,  34, 14, 42, 7,  35, 15, 43, 0,  36, 8,  44, 1,  37, 9,  45, 2,  38, 10, 46, 3,  39, 11, 47,
            20, 48, 28, 56, 21, 49, 29, 57, 22, 50, 30, 58, 23, 51, 31, 59, 16, 52, 24, 60, 17, 53, 25, 61,
            18, 54, 26, 62, 19, 55, 27, 63};
        for (unsigned parity = 0; parity < 2; parity++) {
            for (unsigned m = 0; m < 64; m++) {
                to_output[parity][lut[parity * 64 + m]] = static_cast<uint8_t>(m);
            }
        }
    }
};

constexpr InverseLut kInverseLut;

// Where Conv8to32 puts the 8-bit texel (x, y) of a width x height image, or -1 where its
// page-sized work buffers drop it (images narrower than a block).
std::int64_t SwizzledOffset(int width, int height, int x, int y) {
    int pages_x = ((width - 1) >> 7) + 1;
    int pages_y = ((height - 1) >> 6) + 1;
    int page_width = pages_x == 1 ? width : 128;
    int row_bytes = pages_x == 1 ? width * 2 : 256;
    int row_count = pages_y == 1 ? height >> 1 : 32;
    int page_x = x / page_width;
    int page_y = y >> 6;
    int local_x = x - page_x * page_width;
    int local_y = y & 63;
    int block_x = local_x >> 4;
    int block_y = local_y >> 4;
    int source = (local_y & 15) * 16 + (local_x & 15);
    int column = source >> 6;
    int output = column * 64 + kInverseLut.to_output[column & 1][source & 63];
    int page_row = block_y * 8 + (output >> 5);
    int page_column = block_x * 32 + (output & 31);
    if (page_row >= row_count || page_column >= row_bytes) {
        return -1;
    }
    return static_cast<std::int64_t>(page_y) * pages_x * row_bytes * row_count + static_cast<std::int64_t>(row_bytes) * page_x +
           static_cast<std::int64_t>(page_row) * row_bytes * pages_x + page_column;
}

void Unswizzle8(int width, int height, const u_char *swizzled, u_char *linear) {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            std::int64_t offset = SwizzledOffset(width, height, x, y);
            linear[static_cast<size_t>(y) * width + x] = offset < 0 ? 0 : swizzled[offset];
        }
    }
}

uint32_t Load32(const u_char *p) {
    uint32_t value;
    std::memcpy(&value, p, 4);
    return value;
}

// Which palette entry a CLUT image position holds (CSM1, bits 3 and 4 swapped), or -1 off it.
unsigned ClutPosition(int bpp, unsigned position) {
    if (bpp == 4) {
        return position;
    }
    return (position & ~0x18u) | ((position & 0x08u) << 1) | ((position & 0x10u) >> 1);
}

void DecodeLevel(int bpp, unsigned width, unsigned height, const u_char *src, bool swizzled,
                 std::vector<uint8_t> &out) {
    size_t texels = static_cast<size_t>(width) * height;
    switch (bpp) {
        case 4:
            out.resize(texels);
            for (size_t i = 0; i < texels; i++) {
                u_char pair = src[i >> 1];
                out[i] = (i & 1) ? pair >> 4 : pair & 0xF;
            }
            break;
        case 8:
            out.resize(texels);
            if (swizzled) {
                Unswizzle8(static_cast<int>(width), static_cast<int>(height), src, out.data());
            } else {
                std::memcpy(out.data(), src, texels);
            }
            break;
        case 16:
            // ABGR1555. The GS takes alpha from TEXA by the A bit for a TCC 0 texture, so the
            // usual TA0 0 / TA1 0x80 is baked in.
            out.resize(texels * 4);
            for (size_t i = 0; i < texels; i++) {
                unsigned pixel = src[i * 2] | src[i * 2 + 1] << 8;
                out[i * 4 + 0] = static_cast<uint8_t>((pixel & 0x1F) << 3);
                out[i * 4 + 1] = static_cast<uint8_t>(((pixel >> 5) & 0x1F) << 3);
                out[i * 4 + 2] = static_cast<uint8_t>(((pixel >> 10) & 0x1F) << 3);
                out[i * 4 + 3] = (pixel & 0x8000) ? 0x80 : 0;
            }
            gfx::ConvertPs2Alpha(reinterpret_cast<uint32_t *>(out.data()), texels);
            break;
        case 24:
            out.resize(texels * 4);
            for (size_t i = 0; i < texels; i++) {
                out[i * 4 + 0] = src[i * 3 + 0];
                out[i * 4 + 1] = src[i * 3 + 1];
                out[i * 4 + 2] = src[i * 3 + 2];
                out[i * 4 + 3] = 0xFF;
            }
            break;
        case 32:
            out.assign(src, src + texels * 4);
            gfx::ConvertPs2Alpha(reinterpret_cast<uint32_t *>(out.data()), texels);
            break;
        default:
            break;
    }
}

// Where the Z buffer sits and how far it reaches; 8-bit textures are placed over it while it
// holds nothing, as the ps2 manager did.
int ZBufVram(int *size) {
    mgCTexture frame;
    mgGetTextureZ(0);
    mgGetFrameBuffer(&frame);
    *size = frame.width * frame.width * 4 / 256;
    return mgZBUF_1.bits.zbp << 5;
}

int ZBufBorrowable(const mgCTexture *texture, int *size) {
    *size = 0;
    if (texture->swizzled == 0 && texture->bpp == 8 && texture->image[0] != NULL) {
        *size = texture->vram_size * 4;
        return 1;
    }
    return 0;
}

bool DecodeTexture(const mgCTexture *texture, gfx::TextureDesc &desc, std::vector<std::vector<uint8_t>> &levels) {
    int width = texture->width;
    int height = texture->height;
    if (width <= 0 || height <= 0) {
        return false;
    }
    if (texture->bpp <= 8) {
        desc.format = gfx::TextureFormat::Index8;
    } else {
        desc.format = gfx::TextureFormat::Rgba8;
    }
    desc.width = static_cast<uint32_t>(width);
    desc.height = static_cast<uint32_t>(height);
    desc.has_alpha = texture->bpp != 24;
    for (int level = 0; level < MG_TEXTURE_LEVEL_MAX && texture->image[level] != NULL; level++) {
        unsigned level_width = static_cast<unsigned>(width) >> level;
        unsigned level_height = static_cast<unsigned>(height) >> level;
        levels.emplace_back();
        DecodeLevel(texture->bpp, level_width ? level_width : 1, level_height ? level_height : 1,
                    reinterpret_cast<const u_char *>(texture->image[level]), texture->swizzled != 0,
                    levels.back());
    }
    desc.mip_levels = static_cast<uint32_t>(levels.empty() ? 0 : levels.size());
    return !levels.empty();
}

void Sync(const mgCTexture *texture, Entry &entry) {
    if (!entry.dirty) {
        return;
    }
    if (!RendererUp()) {
        return;
    }
    entry.dirty = false;

    gfx::TextureDesc desc;
    std::vector<std::vector<uint8_t>> levels;

    if (texture->image[0] == NULL) {
        // A texture the game draws into or uploads into without pixels of its own: a named
        // render target for 16 bits and up, an index image with a palette of zeroes below.
        bool has_alpha = texture->bpp != 24;
        if (texture->bpp > 8) {
            Release(entry);
            entry.texture = gfx::NamedRenderTarget(texture->name, static_cast<uint32_t>(texture->width),
                                                   static_cast<uint32_t>(texture->height), has_alpha);
            entry.target = entry.texture != gfx::kNullTexture;
            return;
        }
        desc.width = static_cast<uint32_t>(texture->width);
        desc.height = static_cast<uint32_t>(texture->height);
        desc.format = gfx::TextureFormat::Index8;
        desc.has_alpha = has_alpha;
        desc.mip_levels = 1;
        levels.emplace_back(static_cast<size_t>(texture->width) * texture->height, 0);
    } else if (!DecodeTexture(texture, desc, levels)) {
        entry.dirty = true;
        return;
    }

    // The loaded textures are re-synced every time the game rewrites them, which the title
    // and loading screens do every frame. Releasing and recreating the renderer's image each
    // time churns through slots faster than the deferred releases return them, so an entry
    // whose shape has not changed keeps its image and only the pixels are re-uploaded.
    bool reuse = entry.texture != gfx::kNullTexture && entry.width == desc.width &&
                 entry.height == desc.height && entry.mips == desc.mip_levels && entry.format == desc.format;
    if (!reuse) {
        Release(entry);
        entry.texture = gfx::CreateTexture(desc);
        if (entry.texture == gfx::kNullTexture) {
            return;
        }
        entry.width = desc.width;
        entry.height = desc.height;
        entry.mips = desc.mip_levels;
        entry.format = desc.format;
    }
    for (size_t level = 0; level < levels.size(); level++) {
        uint32_t level_width = std::max(1u, desc.width >> level);
        uint32_t level_height = std::max(1u, desc.height >> level);
        gfx::UpdateTexture(entry.texture, static_cast<uint32_t>(level), 0, 0, level_width, level_height,
                           levels[level].data());
    }

    if (desc.format == gfx::TextureFormat::Index8) {
        std::array<uint32_t, 256> rgba = {};
        const u_char *clut = reinterpret_cast<const u_char *>(texture->clut);
        if (clut != nullptr) {
            unsigned colors = texture->bpp == 4 ? 16 : 256;
            for (unsigned position = 0; position < colors; position++) {
                rgba[ClutPosition(texture->bpp, position)] = Load32(clut + position * 4);
            }
            gfx::ConvertPs2Alpha(rgba.data(), rgba.size());
        }
        if (entry.palette == gfx::kNullTexture) {
            entry.palette = gfx::CreatePalette();
        }
        if (entry.palette != gfx::kNullTexture) {
            gfx::UpdatePalette(entry.palette, rgba.data());
        }
    }
}

void MarkDirty(const mgCTexture *texture) {
    g_entries[texture].dirty = true;
}

void Forget(const mgCTexture *texture) {
    auto it = g_entries.find(texture);
    if (it != g_entries.end()) {
        Release(it->second);
        g_entries.erase(it);
    }
}

} // namespace

// Resolves one of the manager's textures to what the renderer samples. False when the texture has
// no image yet; the caller then draws it untextured. Creates or refreshes the renderer's copy on
// first use, never inside the loading screen's own frame.
bool PortMgTextureBinding(const mgCTexture *texture, gfx::TextureBinding &out) {
    out = gfx::TextureBinding{};
    if (texture == nullptr) {
        return false;
    }
    Entry &entry = g_entries[texture];
    if (entry.dirty) {
        Sync(texture, entry);
    }
    if (entry.texture == gfx::kNullTexture) {
        return false;
    }
    if (texture->bpp <= 8 && entry.palette == gfx::kNullTexture) {
        return false;
    }
    out.texture = entry.texture;
    out.palette = entry.palette;
    out.filter = (texture->tex1_bits >> 5) & 1 ? gfx::Filter::Linear : gfx::Filter::Nearest;
    out.wrap_u = texture->clamp.WMS == 0 ? gfx::Wrap::Repeat : gfx::Wrap::Clamp;
    out.wrap_v = texture->clamp.WMT == 0 ? gfx::Wrap::Repeat : gfx::Wrap::Clamp;
    out.ignore_alpha = texture->tex0.TCC == 0;
    return true;
}

PORT_OVERRIDE int mgCTextureManager::ReloadTexture(int block, u_int *packet) {
    // The ps2 version writes the VIF1 chain that uploads a block into VRAM. On the host the
    // upload happens when the renderer first samples the texture, so this only has to keep the
    // ps2's placement of TBP0 and CBP (which the engine's other units still speak in) and mark
    // the block's textures for a decode.
    int vram;
    int fix;
    int zbuf_size;
    int zbuf;
    int zbuf_end;

    if (packet != NULL && (block < 0 || block >= block_max)) {
        last_block = -1;
        return 0;
    }
    if (block < 0 || block >= block_max) {
        return 0;
    }

    vram = vram_top;
    fix = vram_fix;
    zbuf = ZBufVram(&zbuf_size);
    zbuf_end = zbuf + zbuf_size;

    for (mgCTexture *texture = blocks[block].texture; texture != NULL; texture = texture->next) {
        if (last_block != block) {
            int size;
            int to_zbuf = 0;

            if (ZBufBorrowable(texture, &size)) {
                if (zbuf + size < zbuf_end) {
                    to_zbuf = 1;
                }
            }

            if (to_zbuf) {
                texture->tex0.TBP0 = zbuf;
                texture->tex0.PSM = SCE_GS_PSMT8H;
                zbuf += texture->vram_size * 4;
            } else {
                texture->tex0.TBP0 = vram;
                vram += texture->vram_size;

                if (texture->bpp == 8) {
                    texture->tex0.PSM = SCE_GS_PSMT8;
                }
            }

            if (texture->bpp <= 8) {
                fix -= MG_TEXTURE_CLUT_BLOCKS;
                texture->tex0.CBP = fix;
            }
        }

        MarkDirty(texture);
    }

    last_block = block;
    return 0;
}

PORT_OVERRIDE void mgCTextureManager::DeleteTexture(mgCTexture *texture) {
    if (texture != NULL) {
        mgCTextureBlock *block = GetTextureBlock(texture->block);

        if (block != NULL) {
            Forget(texture);
            DelHash(texture);
            block->Delete(texture);

            if (texture_num > 0) {
                texture_num--;
                texture_stack[texture_num] = texture;
            }

            texture->Initialize();
        }
    }
}

PORT_OVERRIDE void mgCTextureManager::Initialize(int start, int end) {
    for (auto &pair : g_entries) {
        Release(pair.second);
    }
    g_entries.clear();

    vram_top = start;
    vram_fix = end;

    if (vram_fix < 0) {
        vram_fix = 0x3FE0;
    }

    if (blocks != 0) {
        for (int i = 0; i < block_max; i++) {
            blocks[i].Initialize();
        }

        fix_block.Initialize();

        for (int slot = 0; slot < texture_max; slot++) {
            texture_stack[slot] = &texture_buf[slot];
        }

        texture_num = 0;

        for (int i = 0; i < block_max; i++) {
            blocks[i].unk_0 = vram_top;
            blocks[i].unk_4 = vram_top;
        }

        last_block = -1;
        name_suffix[0] = 0;

        for (int bucket = 0; bucket < MG_TEXTURE_HASH_SIZE; bucket++) {
            hash_table[bucket] = 0;
        }

        for (int node = 0; node < hash_max; node++) {
            hash_stack[node] = hash_buf + node;
        }

        hash_num = 0;
    }
}

PORT_OVERRIDE mgCTexture *mgCTextureManager::SearchHash(char *name, int mode) {
    int             bucket = hash(name);
    mgTEXTURE_HASH *node;

    for (node = hash_table[bucket]; node != NULL; node = node->next) {
        if (strcmp(name, node->texture->name) == 0) {
            if (mode < 0 || node->texture->block == mode) {
                return node->texture;
            }
        }
    }

    return NULL;
}
