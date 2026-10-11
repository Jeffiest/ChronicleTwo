#include "common.h"

#include <libgraph.h>
#include <libvu0.h>

#include <cmath>
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
    std::memset(clip, 0, sizeof(float) * 16);
    clip[0][0] = kx / 320.0f;
    clip[3][0] = -static_cast<float>(mgScreenOffx) * kx / 320.0f - 1.0f;
    clip[1][1] = ky / 240.0f;
    clip[3][1] = -static_cast<float>(mgScreenOffy) * ky / 240.0f - 1.0f;
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

int mgCVisualMDT::Draw(u_int *tag, float (*matrix)[4], mgCDrawManager *draw_manager) {
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
    bool lit = !(info->attr->no_light != 0 || info->lighting_enabled == 0 || ambient_boost);
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
        gfx::DrawMeshImmediate(vertices, indices, constants, binding, state, &transform);
    }

    return 1;
}

u_int mgCVisualMDT::CreatePacket(mgCDrawManager *draw_manager) {
    (void) draw_manager;
    return 0;
}

u_int mgCVisualFixMDT::CreatePacket(mgCDrawManager *draw_manager) {
    (void) draw_manager;
    return 0;
}

// The PS2 FixMDT loader packs each face into a GS packet and lets the index array it builds die
// on the stack: the DMA tag carried the geometry afterwards. The host collects from face->index,
// so the arrays have to live in the model arena instead, as the plain MDT path's do. The packet
// the retail loop built has no host counterpart (CreateFacePacket is the no-op above), so the
// loop is only the face build.
int mgCVisualFixMDT::DataAssignMDT(MDT_HEADER *header, mgCMemory *memory,
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

int mgCVisualMDT::CreateFacePacket(u_int *packet, mgCFace *face) {
    (void) packet;
    (void) face;
    return 0;
}

int mgCVisualMDT::CreateRenderInfoPacket(u_int *packet, float (*matrix)[4], mgRENDER_INFO *info) {
    (void) packet;
    (void) matrix;
    (void) info;
    return 0;
}

int mgCVisualPrim::CreateRenderInfoPacket(u_int *packet, float (*matrix)[4], mgRENDER_INFO *info) {
    (void) packet;
    (void) matrix;
    (void) info;
    return 0;
}

// Retail keeps each data stream's address in an int, and sizes the material block by the PS2's
// 0x30-byte material. On the host the header's high half would be lost and the streams would land
// low, and a host material is wider than 0x30, so both the addresses and the block follow the
// host.
void mgCVisualMDT::CopyMDTDataPointer(MDT_HEADER *header, mgCMemory *memory) {
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
    vertex = (sceVu0FVECTOR *) vertex_address;
    normal = (sceVu0FVECTOR *) normal_address;
    colour = (sceVu0FVECTOR *) colour_address;
    uv = (sceVu0FVECTOR *) uv_address;
    material = (mgMaterial *) memory->Alloc((material_num * sizeof(mgMaterial) + 15) / 16);

    if (material != NULL) {
        for (int i = 0; i < material_num; i++) {
            CopyMaterial(&material[i], &file_materials[i], textures);
        }
    }
}

// The PS2's scratchpad has no host address; a static buffer with the same two halves stands in, so
// code that still builds scratchpad packets writes real memory instead of the EE's 0x70000000.
u_int *GetScrPad() {
    static u_int scratchpad[2][0x2000 / 4] __attribute__((aligned(64)));

    return scratchpad[buff_id ? 1 : 0];
}

// The DMA transfer to the GS has no host counterpart, and neither have the EE's DMA registers the
// retail routine writes; the packet is discarded.
void SendDMA(void *packet, int size) {
    (void) packet;
    (void) size;
}
