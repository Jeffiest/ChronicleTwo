#include <tuple>
#include <set>
#include <cstdio>
#include <algorithm>
#include "common.h"

#include <libgraph.h>
#include <libvu0.h>

#include <cmath>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <span>
#include <vector>

#include "gfx/gfx.hpp"
#include "mg_dataset.hpp"
#include "mg_drawenv.hpp"
#include "mg_drawprim.hpp"
#include "mg_frame.hpp"
#include "mg_memory.hpp"
#include "mg_texture.hpp"
#include "mg_visual.hpp"
#include "mglib.hpp"

// The host side of the MDT visual: the ps2 file of this name builds VU1 packets, one chain per
// material group, that a later DMA pass sends to the GS. The port records renderer draws instead.
// The original packet builders are replaced with no-ops here so the same strong definitions win
// wherever a virtual call lands; everything plain CPU in the ps2 file (data assignment, face
// lists, copies) still links and is used as it is.

// ---- Shared with the other rendering replacements ----------------------------------------------

int  PortMgCollectGroup(const mgCVisualMDT *visual, const mgFACE_GROUP *group,
                        std::vector<gfx::Vertex3D> &vertices, std::vector<uint32_t> &indices, bool *has_colour);
bool PortMgTextureBinding(const mgCTexture *texture, gfx::TextureBinding &out);
bool PortMgShadowVisualDraw(const mgCVisualMDT *visual, float (*matrix)[4], const mgRENDER_INFO *info);

namespace {
[[noreturn]] void ModelStorageFailure(const char *what) {
    std::fprintf(stderr, "model storage: %s\n", what);
    std::abort();
}

void *RequiredModelStorage(mgCMemory *memory, uint64_t bytes) {
    const uint64_t blocks = (bytes + 15) / 16;
    if (memory == nullptr || bytes == 0 || bytes > static_cast<uint64_t>(INT_MAX) * 16 ||
        blocks > INT_MAX) ModelStorageFailure("invalid allocation size or owner");
    void *result = memory->Alloc(static_cast<int>(blocks));
    if (result == nullptr) ModelStorageFailure("required allocation failed");
    return result;
}
} // namespace

PORT_OVERRIDE FACES_ID *mgCVisualMDT::CreateFace(FACES_ID *faces, mgCMemory *memory,
                                               mgCMemory *index_memory, mgCFace **out_face) {
    if (faces == nullptr) ModelStorageFailure("missing face record");
    int stride = 3 + ((faces->type & MG_FACE_COLOUR) != 0) -
                 ((faces->type & MG_FACE_NO_NORMAL) != 0) -
                 ((faces->type & MG_FACE_NO_TEXTURE) != 0);
    const uint64_t indices = static_cast<uint64_t>(faces->vertex_num) * stride;
    if (faces->vertex_num > SHRT_MAX || indices > SHRT_MAX || faces->material > SHRT_MAX)
        ModelStorageFailure("face counts exceed runtime fields");

    auto *face = static_cast<mgCFace *>(RequiredModelStorage(memory, sizeof(mgCFace)));
    std::memset(face, 0, sizeof(*face));
    face->vertex_num = static_cast<short>(faces->vertex_num);
    face->type = static_cast<u_short>(faces->type);
    face->index_stride = static_cast<short>(stride);
    face->index_num = static_cast<short>(indices);
    face->material = static_cast<short>(faces->material);
    // Retain the retail spare index block, including for an empty primitive.
    face->index = static_cast<int *>(RequiredModelStorage(index_memory, (indices / 4 + 1) * 16));
    std::memcpy(face->index, faces->index, indices * sizeof(int));

    mgFACE_GROUP *previous = face_group;
    if (previous != nullptr) {
        while (previous->next != nullptr) {
            if (previous->material == face->material && previous->vu_program == 0) break;
            previous = previous->next;
        }
    }
    mgFACE_GROUP *group;
    if (previous == nullptr || previous->next == nullptr) {
        group = static_cast<mgFACE_GROUP *>(RequiredModelStorage(memory, sizeof(mgFACE_GROUP)));
        std::memset(group, 0, sizeof(*group));
        group->material = face->material;
        if (previous == nullptr) face_group = group;
        else previous->next = group;
    } else group = previous;
    mgCFace **tail = &group->face;
    while (*tail != nullptr) tail = &(*tail)->next;
    *tail = face;
    if (out_face != nullptr) *out_face = face;
    return reinterpret_cast<FACES_ID *>(faces->index + indices);
}

namespace {

constexpr float kGsDepthRange = 1.67e7f;

void MulMatrix(float out[4][4], const float a[4][4], const float b[4][4]) {
    float result[4][4];
    for (int column = 0; column < 4; column++) {
        for (int row = 0; row < 4; row++) {
            result[column][row] = a[0][row] * b[column][0] + a[1][row] * b[column][1] + a[2][row] * b[column][2] +
                                  a[3][row] * b[column][3];
        }
    }
    std::memcpy(out, result, sizeof(result));
}

} // namespace

// GS screen coordinates (the projected pixels of mgTransWorldPrim's output, homogeneous in w) into
// the renderer's clip space. mgScreenOffx/mgScreenOffy are the game screen's top-left corner in
// those pixels; the game's screen is scaled onto the renderer's 640x480 logical frame. The GS
// depth runs near 1.67e7 to far 0; the renderer's reverse-Z is the affine map below.
void PortMgScreenToClip(float clip[4][4]) {
    float kx = mgScreenWidth > 0 ? 640.0f / static_cast<float>(mgScreenWidth) : 1.0f;
    float ky = mgScreenHeight > 0 ? 480.0f / static_cast<float>(mgScreenHeight) : 1.0f;
    const gfx::TextureHandle target = gfx::CurrentRenderTarget();
    const std::optional<gfx::TextureInfo> target_info = gfx::GetTextureInfo(target);
    const gfx::LogicalRect visible = gfx::VisibleLogicalRect(target);
    const float frame_width = target_info ? static_cast<float>(target_info->width) : gfx::LogicalWidth();
    const float frame_height = target_info ? static_cast<float>(target_info->height) : gfx::LogicalHeight();
    // Keep the native frame centered inside the expanded Fill target instead of clipping its sides.
    const float fill_x = visible.w > frame_width ? frame_width / visible.w : 1.0f;
    const float fill_y = visible.h > frame_height ? frame_height / visible.h : 1.0f;
    std::memset(clip, 0, sizeof(float) * 16);
    clip[0][0] = (kx / 320.0f) * fill_x;
    clip[3][0] = -(static_cast<float>(mgScreenOffx) + mgScreenWidth * 0.5f) * clip[0][0];
    clip[1][1] = (ky / 240.0f) * fill_y;
    clip[3][1] = -(static_cast<float>(mgScreenOffy) + mgScreenHeight * 0.5f) * clip[1][1];
    clip[2][2] = 1.0f / kGsDepthRange;
    clip[3][2] = -1.0f / kGsDepthRange;
    clip[3][3] = 1.0f;
}

// A GS screen coordinate in 12.4 units (mgTransWorldPrim's out, mgCSprite's screen rectangle) on
// the renderer's logical frame.
float PortMgLogicalX(float gs_x) {
    float scale = mgScreenWidth > 0 ? 640.0f / static_cast<float>(mgScreenWidth) : 1.0f;
    return (gs_x / 16.0f - static_cast<float>(mgScreenOffx)) * scale;
}

float PortMgLogicalY(float gs_y) {
    float scale = mgScreenHeight > 0 ? 480.0f / static_cast<float>(mgScreenHeight) : 1.0f;
    return (gs_y / 16.0f - static_cast<float>(mgScreenOffy)) * scale;
}

// A GS 24-bit depth (near 16700000, far 0) as the renderer's reverse-Z.
float PortMgDepthFromGs(float gs_z) {
    float depth = (gs_z - 1.0f) / kGsDepthRange;
    return depth < 0.0f ? 0.0f : depth > 1.0f ? 1.0f : depth;
}

// The drawing state one draw environment describes. The caller has already merged the visual's
// attributes into it (SetDrawEnvGifTag for a model, SetDrawEnv for a primitive); only the fog is
// left to the caller, which owns the frame's attributes.
gfx::DrawState PortMgDrawStateFromEnv(const mgCDrawEnv &env) {
    gfx::DrawState state;
    state.blend = true;
    state.depth_test = env.test.bits.zte ? static_cast<gfx::DepthTest>(env.test.bits.ztst) : gfx::DepthTest::Always;
    state.depth_write = env.zbuf.bits.zmsk == 0;
    state.alpha_test = env.test.bits.ate != 0;
    state.alpha_func = static_cast<gfx::AlphaFunc>(env.test.bits.atst);
    state.alpha_ref = static_cast<uint8_t>(env.test.bits.aref);
    state.alpha.a = static_cast<uint8_t>(env.alpha.bits.a);
    state.alpha.b = static_cast<uint8_t>(env.alpha.bits.b);
    state.alpha.c = static_cast<uint8_t>(env.alpha.bits.c);
    state.alpha.d = static_cast<uint8_t>(env.alpha.bits.d);
    state.alpha.fix = static_cast<uint8_t>(env.alpha.bits.fix);
    return state;
}

namespace {

// The frame's fog, an mgFOG_PARAM: F = clamp(offset + scale / w, far_value, near_value).
void FillFog(gfx::MeshConstants &constants, gfx::DrawState &state, const mgRENDER_INFO *info) {
    constants.fog[0] = info->fog.offset;
    constants.fog[1] = info->fog.scale;
    constants.fog[2] = info->fog.far_value;
    constants.fog[3] = info->fog.near_value;

    const mgCFrameAttr *attr = info->attr;
    state.fog = attr != nullptr && attr->fog != MG_FOG_OFF && info->fog_enable != 0;

    if (attr != nullptr && attr->fog == MG_FOG_BLACK) {
        state.fog_color[0] = state.fog_color[1] = state.fog_color[2] = 0;
    } else if (attr != nullptr && attr->fog == MG_FOG_WHITE) {
        state.fog_color[0] = state.fog_color[1] = state.fog_color[2] = 255;
    } else {
        state.fog_color[0] = info->fog.r;
        state.fog_color[1] = info->fog.g;
        state.fog_color[2] = info->fog.b;
    }
}

// One material group's constants. The material's diffuse is a fraction of one, as the game keeps
// it (1.0 is the GS's 0x80); the scene's ambient alpha and the frame's obj_alpha fade it.
void FillMaterial(gfx::MeshConstants &constants, const mgRENDER_INFO *info, const mgLIGHT_INFO *lighting,
                  const mgMaterial *material, bool ambient_boost) {
    static const float kWhite[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    const float       *diffuse = material != nullptr ? material->diffuse : kWhite;

    for (int channel = 0; channel < 4; channel++) {
        constants.specular[channel] = 0.0f;
    }

    if (ambient_boost) {
        // The VU sends the frame's boosted ambient as a flat object colour ('T' names).
        for (int channel = 0; channel < 3; channel++) {
            constants.diffuse[channel] = (lighting->ambient[channel] + 0.3f * lighting->light_color[0][channel]) / 128.0f;
            constants.ambient_material[channel] = 1.0f;
        }
        constants.diffuse[3] = diffuse[3] * (lighting->ambient[3] / 128.0f) * info->attr->obj_alpha;
        return;
    }

    for (int channel = 0; channel < 4; channel++) {
        constants.diffuse[channel] = diffuse[channel];
        constants.ambient_material[channel] = diffuse[channel];
    }
    constants.diffuse[3] = diffuse[3] * (lighting->ambient[3] / 128.0f) * info->attr->obj_alpha;
}

} // namespace

// ---- mgCVisualMDT ------------------------------------------------------------------------------

PORT_OVERRIDE int mgCVisualMDT::Draw(u_int *tag, float (*matrix)[4], mgCDrawManager *draw_manager) {
    (void) tag;
    if (draw_manager == NULL) {
        draw_manager = &mgDrawManager;
    }

    mgRENDER_INFO *info = draw_manager->render_info;

    if (info == NULL || info->attr == NULL) {
        return 0;
    }

    // Shadow models are flattened by their own projection; mg_shadow.cpp owns that draw.
    if (PortMgShadowVisualDraw(this, matrix, info)) {
        return 1;
    }

    // world_screen's homogeneous output divides to GS screen pixels; the frame separates into the
    // pure camera (world_view, kept for interpolation) and the projection built from the two.
    float view[4][4];
    float inverse_view[4][4];
    float world_to_screen[4][4];
    float projection[4][4];

    std::memcpy(view, info->world_view, sizeof(view));
    std::memcpy(world_to_screen, info->world_screen, sizeof(world_to_screen));
    PortMgScreenToClip(projection);

    if (gfx::InvertAffineTransform(&view[0][0], &inverse_view[0][0])) {
        float view_to_screen[4][4];
        MulMatrix(view_to_screen, world_to_screen, inverse_view);
        MulMatrix(projection, projection, view_to_screen);
    }

    float model_to_clip[4][4];
    MulMatrix(model_to_clip, projection, view);
    MulMatrix(model_to_clip, model_to_clip, matrix);

    gfx::MeshTransform transform = gfx::IdentityMeshTransform();
    std::memcpy(transform.projection, projection, sizeof(projection));
    std::memcpy(transform.view, view, sizeof(view));
    std::memcpy(transform.model, matrix, sizeof(float) * 16);

    // The draw environment the ps2 path sends at the head of the setup packet, merged with the
    // frame's own attributes, and the state it describes.
    mgCDrawEnv *environment = draw_env != NULL ? draw_env : &info->draw_env[0];
    mgCDrawEnv  merged;
    SetDrawEnvGifTag(reinterpret_cast<u_long128 *>(&merged), info, environment);

    gfx::DrawState state = PortMgDrawStateFromEnv(merged);

    // Diagnostic: DC2_RTT_NODEPTH=1 ignores depth for models drawn into an off-screen texture.
    static const bool rtt_no_depth = std::getenv("DC2_RTT_NODEPTH") != nullptr;
    if (rtt_no_depth && gfx::CurrentRenderTarget() != gfx::kMainTarget) {
        state.depth_test = gfx::DepthTest::Always;
    }

    gfx::MeshConstants constants = {};
    std::memcpy(constants.mvp, model_to_clip, sizeof(model_to_clip));

    for (int axis = 0; axis < 3; axis++) {
        float length2 = matrix[axis][0] * matrix[axis][0] + matrix[axis][1] * matrix[axis][1] +
                        matrix[axis][2] * matrix[axis][2];
        float inverse = length2 > 0.0f ? 1.0f / std::sqrt(length2) : 0.0f;
        for (int row = 0; row < 3; row++) {
            constants.normal_matrix[axis * 4 + row] = matrix[axis][row] * inverse;
        }
        constants.normal_matrix[axis * 4 + 3] = 0.0f;
    }

    mgLIGHT_INFO *lighting = info->GetpLightInfo();
    for (int light = 0; light < 4; light++) {
        for (int row = 0; row < 3; row++) {
            constants.light_direction[light][row] = -lighting->light_dir[row][light];
        }
        constants.light_direction[light][3] = 0.0f;
        for (int channel = 0; channel < 3; channel++) {
            constants.light_color[light][channel] = lighting->light_color[light][channel] / 128.0f;
        }
        constants.light_color[light][3] = 0.0f;
    }
    constants.light_count = 4;
    for (int channel = 0; channel < 4; channel++) {
        constants.ambient[channel] = lighting->ambient[channel] / 128.0f;
    }

    bool ambient_boost = info->attr->ambient_boost != 0;
    // Diagnostic: DC2_NO_LIGHT=1 draws every model unlit, to tell bad lighting from bad textures.
    static const bool force_unlit = std::getenv("DC2_NO_LIGHT") != nullptr;
    bool lit = !(info->attr->no_light != 0 || info->lighting_enabled == 0 || ambient_boost || force_unlit);
    FillFog(constants, state, info);

    for (mgFACE_GROUP *node = face_group; node != NULL; node = node->next) {
        const mgMaterial *entry = NULL;

        if (this->material != NULL && node->material >= 0 && node->material < material_num) {
            entry = &this->material[node->material];
        }

        bool                       has_colour = false;
        std::vector<gfx::Vertex3D> vertices;
        std::vector<uint32_t>      indices;

        if (PortMgCollectGroup(this, node, vertices, indices, &has_colour) == 0 || indices.empty()) {
            continue;
        }

        FillMaterial(constants, info, lighting, entry, ambient_boost);
        constants.flags = (lit ? gfx::kMeshLit : 0u) | (has_colour ? gfx::kMeshVertexColor : 0u) |
                          (state.fog ? gfx::kMeshFog : 0u);

        gfx::TextureBinding binding;
        PortMgTextureBinding(entry != NULL ? entry->texture : NULL, binding);

        // Diagnostic: DC2_DUMP_DRAWS=1 prints each distinct material group once.
        static const bool dump = std::getenv("DC2_DUMP_DRAWS") != nullptr;
        static int        dumped;
        static std::set<std::tuple<const void *, int, size_t, size_t>> seen;

        if (dump && dumped < 600 && seen.insert({this, node->material, vertices.size(), indices.size()}).second) {
            dumped++;
            int   bad = 0;
            float lo[3] = {1e30f, 1e30f, 1e30f};
            float hi[3] = {-1e30f, -1e30f, -1e30f};
            float uvlo[2] = {1e30f, 1e30f};
            float uvhi[2] = {-1e30f, -1e30f};

            for (const gfx::Vertex3D &v : vertices) {
                for (int k = 0; k < 3; k++) {
                    if (!std::isfinite(v.position[k]) || !std::isfinite(v.normal[k])) {
                        bad++;
                        continue;
                    }
                    lo[k] = std::min(lo[k], v.position[k]);
                    hi[k] = std::max(hi[k], v.position[k]);
                }
                if (!std::isfinite(v.uv[0]) || !std::isfinite(v.uv[1])) {
                    bad++;
                } else {
                    for (int k = 0; k < 2; k++) {
                        uvlo[k] = std::min(uvlo[k], v.uv[k]);
                        uvhi[k] = std::max(uvhi[k], v.uv[k]);
                    }
                }
            }
            std::fprintf(stderr, "drawmat visual %p rows [%.2f %.2f %.2f] [%.2f %.2f %.2f] [%.2f %.2f %.2f] pos [%.1f %.1f %.1f] v0 [%.2f %.2f %.2f]\n",
                         (const void *) this, matrix[0][0], matrix[0][1], matrix[0][2], matrix[1][0], matrix[1][1],
                         matrix[1][2], matrix[2][0], matrix[2][1], matrix[2][2], matrix[3][0], matrix[3][1],
                         matrix[3][2], vertices.empty() ? 0.0f : vertices[0].position[0],
                         vertices.empty() ? 0.0f : vertices[0].position[1],
                         vertices.empty() ? 0.0f : vertices[0].position[2]);
            std::fprintf(stderr,
                         "draw %p mat %d verts %zu idx %zu bad %d bbox [%.1f %.1f %.1f]..[%.1f %.1f %.1f] tex 0x%x "
                         "pal 0x%x entry %d diff %.2f %.2f %.2f a %.2f lit %d colour %d uv [%.2f %.2f]..[%.2f %.2f] tex0 0x%llx\n",
                         (const void *) this, node->material, vertices.size(), indices.size(), bad, lo[0], lo[1], lo[2],
                         hi[0], hi[1], hi[2], binding.texture, binding.palette, entry != NULL, constants.diffuse[0],
                         constants.diffuse[1], constants.diffuse[2], constants.diffuse[3], lit, has_colour, uvlo[0], uvlo[1], uvhi[0],
                         uvhi[1], (unsigned long long) (entry != NULL && entry->texture != NULL ? entry->texture->tex0_bits : 0));
        }

        gfx::DrawMeshImmediate(vertices, indices, constants, binding, state, &transform);
    }

    return 1;
}

PORT_OVERRIDE u_int mgCVisualMDT::CreatePacket(mgCDrawManager *draw_manager) {
    (void) draw_manager;
    return 0;
}

PORT_OVERRIDE u_int mgCVisualFixMDT::CreatePacket(mgCDrawManager *draw_manager) {
    (void) draw_manager;
    return 0;
}

// The PS2 FixMDT loader packs each face into a GS packet and lets the index array it builds die
// on the stack: the DMA tag carried the geometry afterwards. The host collects from face->index,
// so the arrays have to live in the model arena instead, as the plain MDT path's do. The packet
// the retail loop built has no host counterpart (CreateFacePacket is the no-op above), so the
// loop is only the face build.
PORT_OVERRIDE int mgCVisualFixMDT::DataAssignMDT(MDT_HEADER *header, mgCMemory *memory,
                                   mgCTextureManager *textures) {
    if (header == NULL) {
        return 0;
    }

    if (textures == NULL) {
        textures = &mgTexManager;
    }

    texture_manager = textures;
    CopyMDTDataPointer(header, memory);
    face_group = 0;
    MDT_FACES *faces = (MDT_FACES *) ((u_char *) header + header->faces_ofs);
    int        count = faces->prim_num;
    FACES_ID  *cursor = (FACES_ID *) (faces + 1);

    for (int i = 0; i < count; i++) {
        cursor = CreateFace(cursor, memory, memory, NULL);
    }

    return 1;
}

PORT_OVERRIDE int mgCVisualMDT::CreateFacePacket(u_int *packet, mgCFace *face) {
    (void) packet;
    (void) face;
    return 0;
}

PORT_OVERRIDE int mgCVisualMDT::CreateRenderInfoPacket(u_int *packet, float (*matrix)[4], mgRENDER_INFO *info) {
    (void) packet;
    (void) matrix;
    (void) info;
    return 0;
}

PORT_OVERRIDE int mgCVisualPrim::CreateRenderInfoPacket(u_int *packet, float (*matrix)[4], mgRENDER_INFO *info) {
    (void) packet;
    (void) matrix;
    (void) info;
    return 0;
}

namespace {

// Retail's fixed models keep their streams in the packets built at load time, so the file image
// can be reused once the model is assigned; the host draws from the streams themselves and has to
// own them. A required stream cannot fall back to a file image that the loader will reuse.
sceVu0FVECTOR *OwnStream(u_char *source, int count, mgCMemory *memory) {
    if (count < 0) ModelStorageFailure("negative stream count");
    if (count <= 0) {
        return (sceVu0FVECTOR *) source;
    }

    auto *copy = static_cast<u_long128 *>(RequiredModelStorage(memory, static_cast<uint64_t>(count) * 16));

    std::memcpy(copy, source, (size_t) count * 16);
    return (sceVu0FVECTOR *) copy;
}

} // namespace

// Retail keeps each data stream's address in an int, and sizes the material block by the PS2's
// 0x30-byte material. On the host the header's high half would be lost and the streams would land
// low, and a host material is wider than 0x30, so both the addresses and the block follow the
// host.
PORT_OVERRIDE void mgCVisualMDT::CopyMDTDataPointer(MDT_HEADER *header, mgCMemory *memory) {
    mgCTextureManager *textures = GetTextureManager();
    u_char            *vertex_address = (u_char *) header + header->vertex_ofs;
    u_char            *normal_address = (u_char *) header + header->normal_ofs;
    u_char            *colour_address = (u_char *) header + header->colour_ofs;
    u_char            *uv_address = (u_char *) header + header->uv_ofs;
    MDT_MATERIAL_     *file_materials = (MDT_MATERIAL_ *) ((u_char *) header + header->material_ofs);
    vertex_num = header->vertex_num;
    normal_num = header->normal_num;
    colour_num = header->colour_num;
    uv_num = header->uv_num;
    material_num = header->material_num;
    vertex = OwnStream(vertex_address, vertex_num, memory);
    normal = OwnStream(normal_address, normal_num, memory);
    colour = OwnStream(colour_address, colour_num, memory);
    uv = OwnStream(uv_address, uv_num, memory);
    if (material_num < 0) ModelStorageFailure("negative material count");
    material = material_num == 0 ? nullptr : static_cast<mgMaterial *>(
        RequiredModelStorage(memory, static_cast<uint64_t>(material_num) * sizeof(mgMaterial)));

    if (material != NULL) {
        for (int i = 0; i < material_num; i++) {
            CopyMaterial(&material[i], &file_materials[i], textures);
        }
    }
}

// The PS2's scratchpad has no host address; a static buffer with the same two halves stands in, so
// code that still builds scratchpad packets writes real memory instead of the EE's 0x70000000.
PORT_OVERRIDE u_int *GetScrPad() {
    static u_int scratchpad[2][0x2000 / 4] __attribute__((aligned(64)));

    return scratchpad[buff_id ? 1 : 0];
}

// The DMA transfer to the GS has no host counterpart, and neither have the EE's DMA registers the
// retail routine writes; the packet is discarded.
void SendDMA(void *packet, int size) {
    (void) packet;
    (void) size;
}
