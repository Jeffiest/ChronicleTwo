#include "common.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>

#include "gfx/gfx.hpp"
#include "mg_texture.hpp"

// The host side of the texture animation. Retail walks every playing group's chain of records and
// moves rectangles between the block's textures with a GS image transfer (BITBLTBUF/TRXPOS/
// TRXREG), or draws a record's source into a true-colour destination as though that destination
// were the frame buffer (mgSetPkFrameBuffer over its TBP0). On the host the moves become renderer
// copies between the destination texture's image and the source's, and the framed draws become 2D
// draws into the destination's render target; the TEXFLUSH and VIF1 packet around them has no
// counterpart. The group, record, wait and phase bookkeeping is retail's and is kept here. The ps2
// unit's plain-CPU parts (the group lists, the record constructors, the script loader and its
// tags) still link from ps2/src, so this file replaces only the animation player and the block
// reload that steps it.
//
// Every texture is reached through PortMgTextureBinding, the seam the sprite and visual units
// sample through: its texture is the image a move lands in, its palette the entries a CLUT copy
// replaces.

bool PortMgTextureBinding(const mgCTexture *texture, gfx::TextureBinding &out);

namespace {

// Subtixels per texel as a float; clang rejects dividing one by the enum constant directly.
constexpr float kSubtexel = static_cast<float>(MG_TEX_ANIME_SUBTEXEL);

void ReportOnce(bool &reported, const char *what) {
    if (!reported) {
        reported = true;
        std::fprintf(stderr, "mg_tanime: %s\n", what);
    }
}

// Where the PS2's CSM1 palette layout keeps entry `position` in a texture's palette image (bits 3
// and 4 swapped), as mg_texture.cpp builds its renderer palettes.
unsigned ClutPosition(int bpp, unsigned position) {
    if (bpp == 4) {
        return position;
    }
    return (position & ~0x18u) | ((position & 0x08u) << 1) | ((position & 0x10u) >> 1);
}

// An image a same-texture move or draw is staged through: the renderer cannot copy an image onto
// itself or sample the target it renders into, while scripts name the same texture on both sides
// often (a region scrolled or flipped inside one image). One scratch per format, grown as needed
// and kept, since the animated images are small.
gfx::TextureHandle ScratchImage(bool colour, uint32_t width, uint32_t height) {
    struct Scratch {
        gfx::TextureHandle texture = gfx::kNullTexture;
        uint32_t           width = 0;
        uint32_t           height = 0;
    };

    static Scratch index_scratch;
    static Scratch colour_scratch;
    Scratch &scratch = colour ? colour_scratch : index_scratch;

    if (scratch.texture != gfx::kNullTexture && scratch.width >= width && scratch.height >= height) {
        return scratch.texture;
    }

    gfx::TextureDesc desc;
    desc.width = width > scratch.width ? width : scratch.width;
    desc.height = height > scratch.height ? height : scratch.height;
    desc.format = colour ? gfx::TextureFormat::Rgba8 : gfx::TextureFormat::Index8;
    desc.mip_levels = 1;
    gfx::TextureHandle grown = gfx::CreateTexture(desc);

    if (grown == gfx::kNullTexture) {
        return gfx::kNullTexture;
    }

    if (scratch.texture != gfx::kNullTexture) {
        gfx::DestroyTexture(scratch.texture);
    }

    scratch.texture = grown;
    scratch.width = desc.width;
    scratch.height = desc.height;
    return scratch.texture;
}

// One mgSetPkMoveImage: a subtixel rectangle of the source image moved into the destination at a
// subtixel offset, both taken down to pixels the way the TRXPOS/TRXREG chain did.
bool MoveImage(const mgCTexture *source, int left, int top, int right, int bottom,
               const mgCTexture *destination, int dest_x, int dest_y) {
    int x = left / MG_TEX_ANIME_SUBTEXEL;
    int y = top / MG_TEX_ANIME_SUBTEXEL;
    int width = right / MG_TEX_ANIME_SUBTEXEL - x + 1;
    int height = bottom / MG_TEX_ANIME_SUBTEXEL - y + 1;

    if (width <= 0 || height <= 0) {
        return false;
    }

    gfx::TextureBinding source_binding;
    gfx::TextureBinding destination_binding;

    if (!PortMgTextureBinding(source, source_binding) ||
        !PortMgTextureBinding(destination, destination_binding)) {
        static bool reported;
        ReportOnce(reported, "a moved texture has no image or palette yet");
        return false;
    }

    gfx::TextureHandle source_image = source_binding.texture;

    if (source_image == destination_binding.texture) {
        source_image = ScratchImage(false, static_cast<uint32_t>(x + width),
                                    static_cast<uint32_t>(y + height));

        if (source_image == gfx::kNullTexture ||
            !gfx::CopyTexture(source_binding.texture, {x, y, width, height}, source_image, x, y)) {
            static bool reported;
            ReportOnce(reported, "a same-texture move had nowhere to stage through");
            return false;
        }
    }

    return gfx::CopyTexture(source_image, {x, y, width, height}, destination_binding.texture,
                            dest_x >> 4, dest_y >> 4);
}

// The CLUT_COPY step: retail moves a palette image (the source's CBP, a 16x16 CLUT) over the
// destination's. The host copies the entries from the source's main-memory palette into the
// renderer palette the destination samples.
bool MoveClut(const mgCTexture *source, const mgCTexture *destination) {
    gfx::TextureBinding destination_binding;

    if (source->clut == NULL || !PortMgTextureBinding(destination, destination_binding) ||
        destination_binding.palette == gfx::kNullTexture) {
        return false;
    }

    unsigned colours = source->bpp == 4 ? 16 : 256;
    std::array<uint32_t, 256> rgba = {};
    const u_char *clut = reinterpret_cast<const u_char *>(source->clut);

    for (unsigned position = 0; position < colours; position++) {
        uint32_t entry;
        std::memcpy(&entry, clut + position * 4, sizeof entry);
        rgba[ClutPosition(source->bpp, position)] = entry;
    }

    gfx::ConvertPs2Alpha(rgba.data(), colours);
    return gfx::UpdatePalette(destination_binding.palette, rgba.data(), 0, colours);
}

// The GS ALPHA register a record's alpha_blend mode writes, as mgCDrawEnv::SetAlpha takes it.
u_long AlphaRegister(int mode) {
    switch (mode) {
        case 1:
            return 0x44;
        case 2:
            return 0x48;
        case 3:
            return 0x42;
        case 4:
            return 0x800000002A;
        case 5:
            return 0x8000000068;
        default:
            // mgCDrawEnv::Initialize's Cs/Cd/As/Cd default, which the other modes keep.
            return 0x44;
    }
}

// The state a drawn record uses: no depth, the record's blending and testing, the pass's TEXA.
gfx::DrawState RecordState(const mgCTexAnimeData *data) {
    gfx::DrawState state;

    state.depth_test = gfx::DepthTest::Always;
    state.depth_write = false;

    if (data->alpha_blend != MG_TEX_ANIME_ALPHA_BLEND_OFF) {
        u_long alpha = AlphaRegister(data->alpha_blend);
        state.blend = true;
        state.alpha.a = static_cast<uint8_t>(alpha & 3);
        state.alpha.b = static_cast<uint8_t>((alpha >> 2) & 3);
        state.alpha.c = static_cast<uint8_t>((alpha >> 4) & 3);
        state.alpha.d = static_cast<uint8_t>((alpha >> 6) & 3);
        state.alpha.fix = static_cast<uint8_t>((alpha >> 32) & 0xFF);
    }

    if (data->alpha_test != MG_TEX_ANIME_ALPHA_TEST_OFF) {
        state.alpha_test = true;
        state.alpha_func = static_cast<gfx::AlphaFunc>(data->alpha_test);
        state.alpha_ref = data->alpha_ref;
    }

    // TexAnime opens the pass with TEXA TA0 = TA1 = 0x80 and AEM clear.
    state.texa_aem = false;
    state.texa_ta0 = 0x80;
    return state;
}

// One mgCDrawPrim sprite: two opposite corners of the destination rectangle, each carrying its
// texel of the source. Coordinates are subtixels of the texture's texels.
void DrawSprite(const gfx::DrawState &state, const gfx::TextureBinding &source, const u_char colour[4],
                int ax, int ay, int au, int av, int bx, int by, int bu, int bv) {
    float x0 = static_cast<float>(ax) / kSubtexel;
    float y0 = static_cast<float>(ay) / kSubtexel;
    float x1 = static_cast<float>(bx) / kSubtexel;
    float y1 = static_cast<float>(by) / kSubtexel;
    float u0 = static_cast<float>(au) / kSubtexel;
    float v0 = static_cast<float>(av) / kSubtexel;
    float u1 = static_cast<float>(bu) / kSubtexel;
    float v1 = static_cast<float>(bv) / kSubtexel;

    gfx::Vertex2D vertices[4] = {
        {x0, y0, 0.0f, u0, v0, {colour[0], colour[1], colour[2], colour[3]}, 0xFF, {}},
        {x1, y0, 0.0f, u1, v0, {colour[0], colour[1], colour[2], colour[3]}, 0xFF, {}},
        {x1, y1, 0.0f, u1, v1, {colour[0], colour[1], colour[2], colour[3]}, 0xFF, {}},
        {x0, y1, 0.0f, u0, v1, {colour[0], colour[1], colour[2], colour[3]}, 0xFF, {}},
    };

    gfx::Draw2D(gfx::Primitive::Quads, vertices, source, state);
}

// Binds a true-colour record's destination as the target, as mgSetPkFrameBuffer did, and fills in
// the source binding. False, with a one-time report, when the record cannot be drawn: the
// destination keeps its pixels from the previous frame.
bool BeginDrawRecord(const mgCTexAnimeData *data, gfx::TextureBinding &source,
                     gfx::TextureHandle &previous) {
    if (!gfx::InFrame()) {
        static bool reported;
        ReportOnce(reported, "a true-colour record drawn outside a frame");
        return false;
    }

    gfx::TextureBinding destination;

    if (!PortMgTextureBinding(data->src_tex, source) ||
        !PortMgTextureBinding(data->dest_tex, destination)) {
        static bool reported;
        ReportOnce(reported, "a drawn texture has no image or palette yet");
        return false;
    }

    std::optional<gfx::TextureInfo> info = gfx::GetTextureInfo(destination.texture);

    if (!info || !info->render_target) {
        static bool reported;
        ReportOnce(reported, "a true-colour record whose destination is not a render target");
        return false;
    }

    // prim.Texture(src) made the source sample with the record's bilinear flag.
    source.filter = data->bilinear != 0 ? gfx::Filter::Linear : gfx::Filter::Nearest;

    if (source.texture == destination.texture) {
        std::optional<gfx::TextureInfo> info = gfx::GetTextureInfo(source.texture);
        gfx::TextureHandle scratch = gfx::kNullTexture;

        if (info) {
            scratch = ScratchImage(true, info->width, info->height);
        }

        if (!info || scratch == gfx::kNullTexture ||
            !gfx::CopyTexture(source.texture,
                              {0, 0, static_cast<int32_t>(info->width),
                               static_cast<int32_t>(info->height)},
                              scratch, 0, 0)) {
            static bool reported;
            ReportOnce(reported, "a same-texture draw had nowhere to stage through");
            return false;
        }

        source.texture = scratch;
        source.palette = gfx::kNullTexture;
    }

    previous = gfx::CurrentRenderTarget();
    gfx::SetRenderTarget(destination.texture);
    return true;
}

// The true-colour MG_TEX_ANIME_TYPE_COPY: mgSetPkFrameBuffer to the destination, then one sprite
// of the source over it.
void DrawCopyRecord(const mgCTexAnimeData *data) {
    gfx::TextureBinding source;
    gfx::TextureHandle  previous;

    if (!BeginDrawRecord(data, source, previous)) {
        return;
    }

    const u_char colour[4] = {data->r, data->g, data->b, data->a};

    DrawSprite(RecordState(data), source, colour, data->dest_x, data->dest_y, data->src_x, data->src_y,
               data->dest_x + data->dest_w, data->dest_y + data->dest_h, data->src_x + data->src_w,
               data->src_y + data->src_h);
    gfx::SetRenderTarget(previous);
}

// The true-colour scroll or wave: the source is split where the movement has reached and the four
// quarters are drawn crosswise so that the image wraps round.
void DrawScrollRecord(const mgCTexAnimeData *data) {
    int src_end_x;
    int src_end_y;
    int dest_end_x;
    int dest_end_y;
    int src_split_x;
    int src_split_y;
    int dest_split_x;
    int dest_split_y;
    int offset_x;
    int offset_y;

    if (data->dest_w == 0 || data->dest_h == 0) {
        return;
    }

    if (data->type == MG_TEX_ANIME_TYPE_SCROLL) {
        float period_x = data->period_x;
        period_x = period_x < 0.0f ? -period_x : period_x;
        offset_x = static_cast<float>(data->dest_w * data->phase_x) / period_x;
        float numerator_y = static_cast<float>(data->dest_h * data->phase_y);
        float period_y = data->period_y;
        period_y = period_y < 0.0f ? -period_y : period_y;
        offset_y = numerator_y / period_y;
    } else {
        offset_x = static_cast<float>(data->dest_w) *
                   (static_cast<float>(data->amplitude_x) *
                    ((1.0f + std::sin(TWO_PI * static_cast<float>(data->phase_x) /
                                      static_cast<float>(data->period_x))) /
                     2.0f) /
                    static_cast<float>(MG_TEX_ANIME_AMPLITUDE_FULL));
        offset_y = static_cast<float>(data->dest_h) *
                   (static_cast<float>(data->amplitude_y) *
                    ((1.0f + std::sin(TWO_PI * static_cast<float>(data->phase_y) /
                                      static_cast<float>(data->period_y))) /
                     2.0f) /
                    static_cast<float>(MG_TEX_ANIME_AMPLITUDE_FULL));
    }

    src_end_x = data->src_x + data->src_w;
    src_end_y = data->src_y + data->src_h;
    dest_end_x = data->dest_x + data->dest_w;
    dest_end_y = data->dest_y + data->dest_h;

    src_split_x = data->src_x + data->src_w - offset_x * data->src_w / data->dest_w;
    src_split_y = data->src_y + data->src_h - offset_y * data->src_h / data->dest_h;
    dest_split_x = data->dest_x + offset_x;
    dest_split_y = data->dest_y + offset_y;

    gfx::TextureBinding source;
    gfx::TextureHandle  previous;

    if (!BeginDrawRecord(data, source, previous)) {
        return;
    }

    const gfx::DrawState state = RecordState(data);
    const u_char         colour[4] = {data->r, data->g, data->b, data->a};

    DrawSprite(state, source, colour, dest_split_x, dest_split_y, data->src_x, data->src_y, dest_end_x,
               dest_end_y, src_split_x, src_split_y);
    DrawSprite(state, source, colour, dest_split_x, data->dest_y, data->src_x, src_split_y, dest_end_x,
               dest_split_y, src_split_x, src_end_y);
    DrawSprite(state, source, colour, data->dest_x, dest_split_y, src_split_x, data->src_y, dest_split_x,
               dest_end_y, src_end_x, src_split_y);
    DrawSprite(state, source, colour, data->dest_x, data->dest_y, src_split_x, src_split_y, dest_split_x,
               dest_split_y, src_end_x, src_end_y);

    gfx::SetRenderTarget(previous);
}

// The indexed scroll or wave: the same split, moved with image transfers instead of drawn.
void MoveScrollRecord(const mgCTexAnimeData *data) {
    int src_left = data->src_x / MG_TEX_ANIME_SUBTEXEL;
    int src_top = data->src_y / MG_TEX_ANIME_SUBTEXEL;
    int src_width = data->src_w / MG_TEX_ANIME_SUBTEXEL;
    int src_height = data->src_h / MG_TEX_ANIME_SUBTEXEL;
    int dest_left = data->dest_x / MG_TEX_ANIME_SUBTEXEL;
    int dest_top = data->dest_y / MG_TEX_ANIME_SUBTEXEL;
    int dest_width = data->dest_w / MG_TEX_ANIME_SUBTEXEL;
    int dest_height = data->dest_h / MG_TEX_ANIME_SUBTEXEL;
    int offset_x;
    int offset_y;

    if (data->type == MG_TEX_ANIME_TYPE_SCROLL) {
        float period_x = data->period_x;
        period_x = period_x < 0.0f ? -period_x : period_x;
        offset_x = static_cast<float>((dest_width - 1) * data->phase_x) / period_x;
        float numerator_y = static_cast<float>((dest_height - 1) * data->phase_y);
        float period_y = data->period_y;
        period_y = period_y < 0.0f ? -period_y : period_y;
        offset_y = numerator_y / period_y;
    } else {
        offset_y = 0;
        offset_x = 0;
    }

    int src_end_x = src_left + src_width - 1;
    int src_end_y = src_top + src_height - 1;
    int dest_end_x = dest_left + dest_width - 1;
    int dest_end_y = dest_top + dest_height - 1;

    int src_split_x = src_left + src_width - offset_x - 1;
    int src_split_y = src_top + src_height - offset_y - 1;
    int dest_split_x = dest_left + offset_x + 1;
    int dest_split_y = dest_top + offset_y + 1;

    if (src_split_x < src_left) {
        src_split_x = src_left;
    }

    if (src_split_y < src_top) {
        src_split_y = src_top;
    }

    if (dest_split_x < dest_left) {
        dest_split_x = dest_left;
    }

    if (dest_split_y < dest_top) {
        dest_split_y = dest_top;
    }

    if (src_split_x > src_end_x) {
        src_split_x = src_end_x;
    }

    if (src_split_y > src_end_y) {
        src_split_y = src_end_y;
    }

    if (dest_split_x > dest_end_x) {
        dest_split_x = dest_end_x;
    }

    if (dest_split_y > dest_end_y) {
        dest_split_y = dest_end_y;
    }

    src_split_x *= MG_TEX_ANIME_SUBTEXEL;
    src_split_y *= MG_TEX_ANIME_SUBTEXEL;
    dest_split_x *= MG_TEX_ANIME_SUBTEXEL;
    dest_split_y *= MG_TEX_ANIME_SUBTEXEL;
    src_end_x *= MG_TEX_ANIME_SUBTEXEL;
    src_end_y *= MG_TEX_ANIME_SUBTEXEL;
    dest_end_x *= MG_TEX_ANIME_SUBTEXEL;
    dest_end_y *= MG_TEX_ANIME_SUBTEXEL;

    MoveImage(data->src_tex, data->src_x, data->src_y, src_split_x, src_split_y, data->dest_tex,
              dest_split_x - MG_TEX_ANIME_SUBTEXEL, dest_split_y - MG_TEX_ANIME_SUBTEXEL);
    MoveImage(data->src_tex, data->src_x, src_split_y, src_split_x, src_end_y - MG_TEX_ANIME_SUBTEXEL,
              data->dest_tex, dest_split_x - MG_TEX_ANIME_SUBTEXEL, data->dest_y);
    MoveImage(data->src_tex, src_split_x, data->src_y, src_end_x - MG_TEX_ANIME_SUBTEXEL, src_split_y,
              data->dest_tex, data->dest_x, dest_split_y - MG_TEX_ANIME_SUBTEXEL);
    MoveImage(data->src_tex, src_split_x, src_split_y, src_end_x - MG_TEX_ANIME_SUBTEXEL,
              src_end_y - MG_TEX_ANIME_SUBTEXEL, data->dest_tex, data->dest_x, data->dest_y);
}

// One record's play: the palette copy, the type's move or draw, and a scroll's phase step.
void PlayRecord(mgCTexAnimeData *data) {
    if (data->src_tex == NULL || data->dest_tex == NULL) {
        return;
    }

    const bool true_colour = data->dest_tex->bpp >= MG_TEX_ANIME_BPP_TRUE_COLOUR;

    if (data->src_tex->bpp == MG_TEX_ANIME_BPP_INDEXED &&
        data->dest_tex->bpp == MG_TEX_ANIME_BPP_INDEXED &&
        (data->clut_copy != 0 ||
         (data->src_w == data->src_tex->width && data->src_h == data->src_tex->height))) {
        MoveClut(data->src_tex, data->dest_tex);
    }

    if (data->type == MG_TEX_ANIME_TYPE_COPY) {
        if (true_colour) {
            DrawCopyRecord(data);
        } else {
            MoveImage(data->src_tex, data->src_x, data->src_y,
                      data->src_x + data->src_w - MG_TEX_ANIME_SUBTEXEL,
                      data->src_y + data->src_h - MG_TEX_ANIME_SUBTEXEL, data->dest_tex, data->dest_x,
                      data->dest_y);
        }
    }

    if (data->type == MG_TEX_ANIME_TYPE_SCROLL || data->type == MG_TEX_ANIME_TYPE_WAVE) {
        if (true_colour) {
            DrawScrollRecord(data);
        } else {
            MoveScrollRecord(data);
        }

        if (mgCTextureAnime::stop_anime == 0) {
            // A negative period runs the phase backwards, from -period down to 1.
            if (data->period_x != 0) {
                if (data->period_x > 0) {
                    data->phase_x++;

                    if (data->phase_x >= data->period_x) {
                        data->phase_x = 0;
                    }
                } else {
                    data->phase_x--;

                    if (data->phase_x <= 0) {
                        data->phase_x = -data->period_x;
                    }
                }
            }

            if (data->period_y != 0) {
                if (data->period_y > 0) {
                    data->phase_y++;

                    if (data->phase_y >= data->period_y) {
                        data->phase_y = 0;
                    }
                } else {
                    data->phase_y--;

                    if (data->phase_y <= 0) {
                        data->phase_y = -data->period_y;
                    }
                }
            }
        }
    }
}

} // namespace

void mgCTextureAnime::TexAnime(int texb, sceVif1Packet *packet) {
    (void) packet;

    // A playing record may name a group to run alongside it.
    for (int i = 0; i < group_num; i++) {
        if (enable[i] != 0) {
            CList<mgCTexAnimeData> *linked_node = now[i];

            if (linked_node != NULL) {
                mgCTexAnimeData *linked_data = linked_node->pGetData();

                if (linked_data != NULL && linked_data->link_group >= 0) {
                    Enable(linked_data->link_group);
                }
            }
        }
    }

    for (int group = 0; group < group_num; group++) {
        if (enable[group] == 0) {
            continue;
        }

        if (now[group] == NULL) {
            continue;
        }

        CList<mgCTexAnimeData> *node = now[group];
        mgCTexAnimeData        *data = node->pGetData();

        if (data == NULL || data->src_tex == NULL || data->dest_tex == NULL) {
            continue;
        }

        if (data->src_tex->block != texb || data->dest_tex->block != texb) {
            continue;
        }

        // Records that wait no frames play together with the record after them.
        for (;;) {
            PlayRecord(data);

            if (data->wait != 0 || node->next == NULL) {
                break;
            }

            node = node->next;
            data = node->pGetData();
        }

        if (stop_anime == 0) {
            frame[group]++;
        }

        int wait = data->wait;

        if (wait < 0) {
            frame[group] = 0;
        } else if (data->bug_patch != 0) {
            if (wait <= frame[group]) {
                frame[group] = 0;
                CList<mgCTexAnimeData> *next = node->next;
                now[group] = next;

                if (now[group] == NULL) {
                    now[group] = list[group];
                }
            }
        } else {
            if (frame[group] > wait) {
                frame[group] = 0;
                CList<mgCTexAnimeData> *next = node->next;
                now[group] = next;

                if (now[group] == NULL) {
                    now[group] = list[group];
                }
            }
        }
    }
}

void mgCTextureManager::ReloadTexture(int index, sceVif1Packet *packet) {
    // Retail writes the block's VIF1 upload chain here. The host's images already live in the
    // renderer, so the reload is the block bookkeeping plus the block's animation step.
    (void) packet;

    if (index < 0 || index >= block_max) {
        last_block = -1;
        return;
    }

    int previous = last_block;

    ReloadTexture(index, (u_int *) NULL);

    if (previous != index) {
        mgCTextureBlock *block = blocks + index;

        if (block->anime != NULL) {
            block->anime->TexAnime(index, NULL);
        }
    }

    last_block = index;
}

CList<mgCTexAnimeData> *mgCTextureAnime::NewTexAnimeGroupData(int group, mgCMemory *stack) {
    if (group < 0 || group >= group_num) {
        return NULL;
    }

    CList<mgCTexAnimeData> *node = NewTexAnimeData(stack);

    if (node == NULL) {
        return NULL;
    }

    if (node->pGetData() == NULL) {
        return NULL;
    }

    node->Initialize();

    // Retail indexes the list array at its PS2 offset with four-byte pointers; on the host the
    // member itself is used, whose offset and stride follow the wider pointers.
    CList<mgCTexAnimeData> **slot = &list[group];
    CList<mgCTexAnimeData>  *p = *slot;
    CList<mgCTexAnimeData>  *tail;
    CList<mgCTexAnimeData>  *next;

    if (p == NULL) {
        *slot = node;
        now[group] = *slot;
    } else {
        tail = p;

        while (tail != NULL) {
            next = tail->next;

            if (next == NULL) {
                break;
            }

            tail = next;
        }

        tail->next = node;

        if (node != NULL) {
            node->prev = tail;
        }
    }

    return node;
}
