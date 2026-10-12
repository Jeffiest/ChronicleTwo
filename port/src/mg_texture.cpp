#include "common.h"

#include "mg_texture.hpp"
#include "mg_texture_port.hpp"
#include "mglib.hpp"

#include "gfx/tim2.hpp"

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

// The rendering seam is installed only once the mglib stream has landed its header.
#if __has_include("mglib_port.hpp")
#include "mglib_port.hpp"
#define MG_TEXTURE_HAS_SEAM 1
#endif

namespace {

static_assert(static_cast<uint32_t>(MG_TEXTURE_LEVEL_MAX) == gfx::tim2::kMaxLevels);

struct Entry {
    const mgCTextureManager *owner = nullptr;
    uint32_t                 key = 0;
    gfx::TextureHandle       image = gfx::kNullTexture;
    gfx::TextureHandle       palette = gfx::kNullTexture;
    u_long128               *levels[MG_TEXTURE_LEVEL_MAX] = {};
    u_long128               *clut = nullptr;
    int                      width = 0;
    int                      height = 0;
    int                      bpp = 0;
    int                      swizzled = 0;
    int                      tw = 0;
    int                      th = 0;
    bool                     uploaded = false;
};

struct TextureRegistry {
    std::unordered_map<const mgCTexture *, Entry> entries;
    std::unordered_map<uint32_t, const mgCTexture *> textures_by_key;
    std::vector<uint32_t> free_keys;
    uint32_t              next_key = kMgTextureKeyFirst;
};

// Function-local so that a manager initialised during static construction reaches a live registry.
TextureRegistry &Registry() {
    static TextureRegistry registry;
    return registry;
}

uint32_t AllocateKey() {
    TextureRegistry &registry = Registry();

    if (!registry.free_keys.empty()) {
        const uint32_t key = registry.free_keys.back();
        registry.free_keys.pop_back();
        return key;
    }

    if (registry.next_key < kMgTextureKeyLast) {
        return 0;
    }

    return registry.next_key--;
}

void DestroyHandle(gfx::TextureHandle &handle) {
    if (handle != gfx::kNullTexture) {
        gfx::DestroyTexture(handle);
        handle = gfx::kNullTexture;
    }
}

bool RendererUp() {
    return gfx::PipelineCount() != 0;
}

u_long128 *AsQuadwords(const uint8_t *bytes) {
    return reinterpret_cast<u_long128 *>(const_cast<uint8_t *>(bytes));
}

const uint8_t *AsBytes(const u_long128 *quadwords) {
    return reinterpret_cast<const uint8_t *>(quadwords);
}

int Log2Ceil(int value) {
    int bits = 0;

    for (int power = 1; power < value; power <<= 1) {
        bits++;
    }

    return bits;
}

u_int QuadwordsFor(u_int bytes) {
    return (bytes + 15) / 16;
}

int PsmForBpp(int bpp) {
    switch (bpp) {
        case 4:
            return SCE_GS_PSMT4;
        case 8:
            return SCE_GS_PSMT8;
        case 16:
            return SCE_GS_PSMCT16;
        case 24:
            return SCE_GS_PSMCT24;
        case 32:
            return SCE_GS_PSMCT32;
        default:
            return -1;
    }
}

gfx::tim2::Source BuildSource(const mgCTexture *texture) {
    gfx::tim2::Source source;

    switch (texture->bpp) {
        case 4:
            source.psm = gfx::tim2::Psm::T4;
            break;
        case 8:
            source.psm = gfx::tim2::Psm::T8;
            break;
        case 16:
            source.psm = gfx::tim2::Psm::Ct16;
            break;
        case 24:
            source.psm = gfx::tim2::Psm::Ct24;
            break;
        default:
            source.psm = gfx::tim2::Psm::Ct32;
            break;
    }

    source.width = static_cast<uint32_t>(texture->width);
    source.height = static_cast<uint32_t>(texture->height);

    for (int i = 0; i < MG_TEXTURE_LEVEL_MAX; i++) {
        source.levels[i] = AsBytes(texture->image[i]);
    }

    source.clut = AsBytes(texture->clut);
    source.clut16 = texture->tex0.CPSM == SCE_GS_PSMCT16 || texture->tex0.CPSM == SCE_GS_PSMCT16S;
    source.csm1 = texture->tex0.CSM == 0;
    source.swizzled = texture->swizzled != 0 && texture->bpp == 8;
    return source;
}

gfx::tim2::Image EmptyImage(const mgCTexture *texture, const gfx::tim2::Source &source) {
    const bool   indexed = texture->bpp <= 8;
    const size_t texel_bytes = indexed ? 1 : 4;

    gfx::tim2::Level level;
    level.width = static_cast<uint32_t>(texture->width);
    level.height = static_cast<uint32_t>(texture->height);
    level.pixels.assign(static_cast<size_t>(level.width) * level.height * texel_bytes, 0);

    gfx::tim2::Image image;
    image.format = indexed ? gfx::TextureFormat::Index8 : gfx::TextureFormat::Rgba8;
    image.has_alpha = texture->bpp != 24;
    image.levels.push_back(std::move(level));
    image.palette = gfx::tim2::DecodePalette(source);
    return image;
}

void TakeSnapshot(const mgCTexture *texture, Entry &entry) {
    for (int i = 0; i < MG_TEXTURE_LEVEL_MAX; i++) {
        entry.levels[i] = texture->image[i];
    }

    entry.clut = texture->clut;
    entry.width = texture->width;
    entry.height = texture->height;
    entry.bpp = texture->bpp;
    entry.swizzled = texture->swizzled;
    entry.tw = static_cast<int>(texture->tex0.TW);
    entry.th = static_cast<int>(texture->tex0.TH);
}

bool SnapshotChanged(const mgCTexture *texture, const Entry &entry) {
    for (int i = 0; i < MG_TEXTURE_LEVEL_MAX; i++) {
        if (entry.levels[i] != texture->image[i]) {
            return true;
        }
    }

    return entry.clut != texture->clut || entry.width != texture->width || entry.height != texture->height ||
           entry.bpp != texture->bpp || entry.swizzled != texture->swizzled ||
           entry.tw != static_cast<int>(texture->tex0.TW) || entry.th != static_cast<int>(texture->tex0.TH);
}

void Rebuild(const mgCTexture *texture, Entry &entry) {
    DestroyHandle(entry.image);
    DestroyHandle(entry.palette);
    TakeSnapshot(texture, entry);
    // A picture the decoder rejects is not retried until its pointers change; retrying would decode the
    // same bytes every frame.
    entry.uploaded = true;

    const gfx::tim2::Source source = BuildSource(texture);
    const uint32_t          canvas_width = 1u << texture->tex0.TW;
    const uint32_t          canvas_height = 1u << texture->tex0.TH;
    gfx::tim2::Uploaded     uploaded;

    if (texture->image[0] != nullptr) {
        if (std::optional<gfx::tim2::Image> image = gfx::tim2::Decode(source)) {
            uploaded = gfx::tim2::Upload(*image, canvas_width, canvas_height);
        }
    } else {
        // Render targets and animation destinations are entered without pixels, yet the renderer samples them.
        uploaded = gfx::tim2::Upload(EmptyImage(texture, source), canvas_width, canvas_height);
    }

    entry.image = uploaded.texture;
    entry.palette = uploaded.palette;
}

void Resolve(const mgCTexture *texture, Entry &entry, bool palette_changed) {
    if (!RendererUp()) {
        return;
    }

    if (!entry.uploaded || SnapshotChanged(texture, entry)) {
        Rebuild(texture, entry);
        return;
    }

    if (palette_changed && entry.palette != gfx::kNullTexture) {
        const gfx::tim2::Source source = BuildSource(texture);
        gfx::UpdatePalette(entry.palette, gfx::tim2::DecodePalette(source).data(), 0, 256);
    }
}

Entry &Register(const mgCTextureManager *owner, mgCTexture *texture) {
    Entry entry;
    entry.owner = owner;
    entry.key = AllocateKey();
    texture->tex0.TBP0 = entry.key;
    texture->tex0.CBP = entry.key;

    TextureRegistry &registry = Registry();

    if (entry.key != 0) {
        registry.textures_by_key[entry.key] = texture;
    }

    return registry.entries[texture] = entry;
}

void ReleaseTexture(const mgCTexture *texture) {
    TextureRegistry &registry = Registry();
    const auto       found = registry.entries.find(texture);

    if (found == registry.entries.end()) {
        return;
    }

    Entry &entry = found->second;
    DestroyHandle(entry.image);
    DestroyHandle(entry.palette);

    if (entry.key != 0) {
        const auto by_key = registry.textures_by_key.find(entry.key);

        if (by_key != registry.textures_by_key.end() && by_key->second == texture) {
            registry.textures_by_key.erase(by_key);
        }

        registry.free_keys.push_back(entry.key);
    }

    registry.entries.erase(found);
}

void ReleaseOwned(const mgCTextureManager *owner) {
    std::vector<const mgCTexture *> owned;

    for (const auto &[texture, entry] : Registry().entries) {
        if (entry.owner == owner) {
            owned.push_back(texture);
        }
    }

    for (const mgCTexture *texture : owned) {
        ReleaseTexture(texture);
    }
}

void ResolveTexture(const mgCTexture *texture, bool palette_changed) {
    TextureRegistry &registry = Registry();
    const auto       found = registry.entries.find(texture);

    if (found != registry.entries.end()) {
        Resolve(texture, found->second, palette_changed);
    }
}

void ResolveBlock(const mgCTextureBlock &block) {
    for (const mgCTexture *texture = block.texture; texture != nullptr; texture = texture->next) {
        ResolveTexture(texture, false);
    }
}

const mgCTexture *TextureForKey(uint32_t key) {
    const auto &by_key = Registry().textures_by_key;
    const auto  found = by_key.find(key);
    return found == by_key.end() ? nullptr : found->second;
}

#ifdef MG_TEXTURE_HAS_SEAM
mgPortTexture SeamTexture(const mgCTexture *texture) {
    const MgTextureHandles handles = MgTextureLookup(texture);
    return {handles.texture, handles.palette};
}

mgPortTexture SeamTex0(uint64_t tex0) {
    const MgTextureHandles handles = MgTextureLookupTex0(tex0);
    return {handles.texture, handles.palette};
}

void InstallSeam() {
    mgPortSetTextureLookup(SeamTexture, SeamTex0);
}
#else
void InstallSeam() {}
#endif

} // namespace

MgTextureHandles MgTextureLookup(const mgCTexture *texture) {
    if (texture == nullptr) {
        return {};
    }

    TextureRegistry &registry = Registry();
    const auto       found = registry.entries.find(texture);

    if (found == registry.entries.end()) {
        return {};
    }

    Resolve(texture, found->second, false);
    return {found->second.image, found->second.palette};
}

MgTextureHandles MgTextureLookupTex0(uint64_t tex0) {
    sceGsTex0 reg;
    reg.value = tex0;

    MgTextureHandles handles;

    if (const mgCTexture *texture = TextureForKey(reg.TBP0)) {
        Entry &entry = Registry().entries.at(texture);
        Resolve(texture, entry, false);
        handles.texture = entry.image;
    }

    if (const mgCTexture *owner = TextureForKey(reg.CBP)) {
        Entry &entry = Registry().entries.at(owner);
        Resolve(owner, entry, false);
        handles.palette = entry.palette;
    }

    return handles;
}

PORT_OVERRIDE void mgCTextureManager::SetTableBuffer(int texture_count, int block_total, mgCMemory *memory) {
    int   slot;
    int   node;
    int   count;
    u_int bytes;
    block_max = block_total;
    count = block_max;
    bytes = count * sizeof(mgCTextureBlock);
    blocks = new (memory->Alloc(QuadwordsFor(bytes) + 2)) mgCTextureBlock[count];

    if (blocks == 0) {
        block_max = 0;
    }

    texture_max = texture_count;
    count = texture_max;
    bytes = count * sizeof(mgCTexture);
    texture_buf = new (memory->Alloc(QuadwordsFor(bytes) + 2)) mgCTexture[count];
    bytes = texture_max * sizeof(mgCTexture *);
    texture_stack = (mgCTexture **) memory->Alloc(QuadwordsFor(bytes) + 2);

    for (slot = 0; slot < texture_max; slot++) {
        texture_stack[slot] = &texture_buf[slot];
    }

    texture_num = 0;
    hash_max = texture_count;
    bytes = hash_max * sizeof(mgTEXTURE_HASH);
    hash_buf = (mgTEXTURE_HASH *) operator new[](
        hash_max * sizeof(mgTEXTURE_HASH), memory->Alloc(QuadwordsFor(bytes) + 2));
    bytes = hash_max * sizeof(mgTEXTURE_HASH *);
    hash_stack = (mgTEXTURE_HASH **) memory->Alloc(QuadwordsFor(bytes) + 2);

    for (node = 0; node < hash_max; node++) {
        hash_stack[node] = hash_buf + node;
    }

    hash_num = 0;
}

PORT_OVERRIDE void mgCTextureManager::Initialize(int start, int end) {
    ReleaseOwned(this);

    vram_top = start;
    vram_fix = end;

    if (vram_fix < 0) {
        vram_fix = MG_TEXTURE_VRAM_FIX_DEFAULT;
    }

    if (blocks != nullptr) {
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
        name_suffix[0] = '\0';

        for (mgTEXTURE_HASH *&bucket : hash_table) {
            bucket = nullptr;
        }

        for (int node = 0; node < hash_max; node++) {
            hash_stack[node] = hash_buf + node;
        }

        hash_num = 0;
    }

    InstallSeam();
}

PORT_OVERRIDE mgCTexture *mgCTextureManager::EnterTexture(int block, char *name, u_long128 **image, int width,
                                                          int height, int bpp, u_long128 *clut, u_long tex1,
                                                          int swizzled) {
    if (name == nullptr) {
        return nullptr;
    }

    // Retail concatenated into the 0x20 buffer unchecked; a longer name is cut to fit instead.
    char full_name[0x20];
    std::snprintf(full_name, sizeof full_name, "%s%s", name, name_suffix);

    if (full_name[0] == '\0') {
        return nullptr;
    }

    mgCTextureBlock *texture_block = GetTextureBlock(block);

    if (texture_block == nullptr) {
        return nullptr;
    }

    const int psm = PsmForBpp(bpp);

    if (psm < 0) {
        return nullptr;
    }

    u_long128 *no_image[MG_TEXTURE_LEVEL_MAX] = {};

    if (image == nullptr) {
        image = no_image;
    }

    mgCTexture *texture = SearchTexture(full_name);

    if (texture == nullptr) {
        return nullptr;
    }

    // A pool slot can come back after Initialize without DeleteTexture; its old registration must not outlive it.
    ReleaseTexture(texture);
    texture->Initialize();

    const int tw = Log2Ceil(width);
    const int th = Log2Ceil(height);
    const int tbw = std::max(1, (width + 63) / 64);

    int level_blocks = bpp == 24 ? 32 : bpp;
    level_blocks = level_blocks * (width * height) / 256 / 8;

    int levels = 0;

    while (levels < MG_TEXTURE_LEVEL_MAX && image[levels] != nullptr) {
        levels++;
    }

    texture->image_blocks = levels == 0 ? level_blocks : 0;

    for (int i = 0; i < levels; i++) {
        texture->image[i] = image[i];
        texture->image_blocks += level_blocks;
        level_blocks /= 4;
    }

    const bool indexed = psm == SCE_GS_PSMT8 || psm == SCE_GS_PSMT4;
    texture->clut = indexed ? clut : nullptr;
    texture->clut_size = indexed ? MG_TEXTURE_CLUT_BLOCKS : 0;
    texture->tex0.value = SCE_GS_SET_TEX0(0, tbw, psm, tw, th, 1, 0, 0, 0, 0, 0, indexed ? 1 : 0);

    texture->vram_size = texture->image_blocks;

    if (const int rest = texture->vram_size % MG_TEXTURE_PAGE_BLOCKS; rest != 0) {
        texture->vram_size += MG_TEXTURE_PAGE_BLOCKS - rest;
    }

    std::memcpy(texture->name, full_name, sizeof full_name);
    texture->width = static_cast<short>(width);
    texture->height = static_cast<short>(height);
    texture->bpp = static_cast<short>(bpp);
    texture->block = static_cast<short>(block);
    texture->swizzled = swizzled;

    if (levels > 1) {
        // Retail's default mip setup fixes MXL at 2 whatever the level count; kept for parity.
        texture->tex1_bits = SCE_GS_SET_TEX1(0, 2, 1, 5, 1, 0, -120);

        if (tex1 != 0) {
            sceGsTex1 caller;
            std::memcpy(&caller, &tex1, sizeof caller);
            texture->tex1_bits = SCE_GS_SET_TEX1(0, levels - 1, 1, 5, 1, caller.L, caller.K);
        }
    } else {
        texture->tex1_bits = SCE_GS_SET_TEX1(0, 0, 1, 1, 1, 0, 0);
    }

    texture_block->Add(texture);
    AddHash(texture);

    Resolve(texture, Register(this, texture), false);

    if (mgGetPerformanceMeterFlag()) {
        std::printf("b = %d,%s vram = %d\n", block, texture->name, texture->vram_size);
    }

    return texture;
}

PORT_OVERRIDE mgCTexture *mgCTextureManager::EnterTexture(int id, char *name, TM2_head *head, int reload,
                                                          int no_image) {
    int bpp;

    switch (head->picture.image_type) {
        case TIM2_RGB16:
            bpp = 16;
            break;
        case TIM2_RGB24:
            bpp = 24;
            break;
        case TIM2_RGB32:
            bpp = 32;
            break;
        case TIM2_IDTEX4:
            bpp = 4;
            break;
        case TIM2_IDTEX8:
            bpp = 8;
            break;
        default:
            return nullptr;
    }

    u_long128 *images[MG_TEXTURE_LEVEL_MAX] = {};
    u_long128 *clut = nullptr;

    if (no_image == 0) {
        const std::span<const uint8_t> bytes(reinterpret_cast<const uint8_t *>(&head->picture),
                                             static_cast<size_t>(head->picture.total_size));
        const std::optional<gfx::tim2::Picture> picture = gfx::tim2::ParsePicture(bytes);

        if (!picture) {
            return nullptr;
        }

        for (uint32_t level = 0; level < picture->level_count; level++) {
            images[level] = AsQuadwords(picture->levels[level]);
        }

        clut = AsQuadwords(picture->clut);
    }

    return EnterTexture(id, name, images, head->picture.width, head->picture.height, bpp, clut,
                        head->picture.tex1, reload);
}

PORT_OVERRIDE int mgCTextureManager::GetRemainVRAM(int block) {
    if (block < 0 || block >= block_max) {
        return 0;
    }

    return INT_MAX;
}

PORT_OVERRIDE int mgLoadImage(u_int *, int, int, int, u_long128 *, int, int, int, int, int) {
    return 0;
}

PORT_OVERRIDE void mgCTextureManager::ReloadTexture(int index, sceVif1Packet *packet) {
    if (index < 0 || index >= block_max) {
        last_block = -1;
        return;
    }

    ResolveBlock(blocks[index]);

    if (last_block != index && blocks[index].anime != nullptr) {
        blocks[index].anime->TexAnime(index, packet != nullptr ? packet : mgVif1Packet);
    }

    last_block = index;
}

PORT_OVERRIDE int mgCTextureManager::ReloadTexture(int block, u_int *packet) {
    const bool in_range = block >= 0 && block < block_max;

    if (in_range) {
        ResolveBlock(blocks[block]);
    }

    if (packet != nullptr) {
        last_block = in_range ? block : -1;
    }

    return 0;
}

PORT_OVERRIDE int mgCTextureManager::ReloadCLUT(mgCTexture *texture, u_int *) {
    if (texture != nullptr) {
        ResolveTexture(texture, true);
    }

    return 0;
}

PORT_OVERRIDE void mgCTextureManager::ReloadCLUT(mgCTexture *texture, sceVif1Packet *) {
    if (texture != nullptr) {
        ResolveTexture(texture, true);
    }
}

PORT_OVERRIDE void mgCTextureManager::DeleteTexture(mgCTexture *texture) {
    if (texture == nullptr) {
        return;
    }

    ReleaseTexture(texture);

    mgCTextureBlock *texture_block = GetTextureBlock(texture->block);

    if (texture_block == nullptr) {
        return;
    }

    DelHash(texture);
    texture_block->Delete(texture);

    if (texture_num > 0) {
        texture_num--;
        texture_stack[texture_num] = texture;
    }

    texture->Initialize();
}
