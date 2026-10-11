#include "common.h"

#include <libgraph.h>
#include <libvu0.h>

#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "gfx/gfx.hpp"
#include "mg_drawenv.hpp"
#include "mg_drawprim.hpp"
#include "mg_frame.hpp"
#include "mg_sprite.hpp"
#include "mg_texture.hpp"
#include "mg_visual.hpp"
#include "mglib.hpp"

// The host side of the sprites. The ps2 file of this name writes GS sprite packets: the screen
// sprite one register list, the 3D sprite a VU1 packet of camera-facing quads. Both become
// renderer 2D draws here - the 3D sprite through the depth it projects to, which is how the port
// keeps a torch's flame testing against the scene - and the packet builders are replaced with
// no-ops. The board the game builds a 3D sprite on is kept per sprite until it is drawn.

bool  PortMgTextureBinding(const mgCTexture *texture, gfx::TextureBinding &out);
float PortMgLogicalX(float gs_x);
float PortMgLogicalY(float gs_y);
float PortMgDepthFromGs(float gs_z);
gfx::DrawState PortMgDrawStateFromEnv(const mgCDrawEnv &env);

namespace {

float ScreenScaleX() {
    return mgScreenWidth > 0 ? 640.0f / static_cast<float>(mgScreenWidth) : 1.0f;
}

float ScreenScaleY() {
    return mgScreenHeight > 0 ? 480.0f / static_cast<float>(mgScreenHeight) : 1.0f;
}

uint8_t ColourByte(float value) {
    return static_cast<uint8_t>(value <= 0.0f ? 0 : value >= 255.0f ? 255 : value + 0.5f);
}

struct Billboard {
    float position[4];
    float size[4];
    float colour[4];
    float uv0[4];
    float uv1[4];
};

struct SpriteState {
    std::vector<Billboard> billboards;
    const mgCTexture      *texture = nullptr;
    bool                   has_env = false;
    mgCDrawEnv             env;
    int                    mode = 0;
};

std::unordered_map<const mgC3DSprite *, SpriteState> g_sprites;

// A column-major [column][row] matrix applied to a point.
void ApplyMatrix(float out[4], const float matrix[4][4], const float *point) {
    for (int row = 0; row < 4; row++) {
        out[row] = matrix[0][row] * point[0] + matrix[1][row] * point[1] + matrix[2][row] * point[2] +
                   matrix[3][row] * point[3];
    }
}

// The frame's fog colour and switch, as the sprite's setup packet writes them.
void SpriteFog(gfx::DrawState &state, const mgRENDER_INFO *info) {
    state.fog = info->fog_enable != 0 && info->attr != nullptr && info->attr->fog != MG_FOG_OFF;

    if (info->attr != nullptr && info->attr->fog == MG_FOG_BLACK) {
        state.fog_color[0] = state.fog_color[1] = state.fog_color[2] = 0;
    } else if (info->attr != nullptr && info->attr->fog == MG_FOG_WHITE) {
        state.fog_color[0] = state.fog_color[1] = state.fog_color[2] = 255;
    } else {
        state.fog_color[0] = info->fog.r;
        state.fog_color[1] = info->fog.g;
        state.fog_color[2] = info->fog.b;
    }
}

uint8_t VertexFog(const mgRENDER_INFO *info, bool fog, float eye_w) {
    if (!fog || eye_w <= 0.0f) {
        return 0xFF;
    }
    float value = info->fog.offset + info->fog.scale / eye_w;
    value = value < info->fog.far_value ? info->fog.far_value : value;
    value = value > info->fog.near_value ? info->fog.near_value : value;
    return static_cast<uint8_t>(value <= 0.0f ? 0 : value >= 255.0f ? 255 : value + 0.5f);
}

} // namespace

// ---- mgCSprite ---------------------------------------------------------------------------------

PORT_OVERRIDE int mgCSprite::Draw(u_int *tag, float (*matrix)[4], mgCDrawManager *manager) {
    (void) tag;
    (void) matrix;

    if (manager == NULL) {
        manager = &mgDrawManager;
    }

    mgRENDER_INFO *info = manager->render_info;

    if (info == NULL) {
        return 0;
    }

    texture_manager = manager->texture_manager;

    // The sprite's rectangle is in sixteenths of a pixel from the screen's own origin; uv in
    // sixteenths of a texel.
    float kx = ScreenScaleX();
    float ky = ScreenScaleY();
    float x0 = screen.left / 16.0f * kx;
    float y0 = screen.top / 16.0f * ky;
    float x1 = screen.right / 16.0f * kx;
    float y1 = screen.bottom / 16.0f * ky;
    float u0 = uv.left / 16.0f;
    float v0 = uv.top / 16.0f;
    float u1 = uv.right / 16.0f;
    float v1 = uv.bottom / 16.0f;

    // Drawn at depth zero unless a view depth is given and lands on the screen.
    float z = 0.0f;

    if (depth >= 1.0f) {
        float position[4] = {0.0f, 0.0f, depth, 1.0f};
        int   projected[4];

        if (mgTransViewPrim(projected, position) != 0) {
            z = PortMgDepthFromGs(static_cast<float>(projected[2]));
        }
    }

    mgCDrawEnv environment;

    if (draw_env != NULL) {
        environment = *draw_env;
    } else {
        SetDrawEnv(&environment, &attr, &info->draw_env[0]);
    }

    gfx::DrawState state = PortMgDrawStateFromEnv(environment);
    state.fog = false;

    gfx::Vertex2D quad[4];
    quad[0].x = x0;
    quad[0].y = y0;
    quad[0].z = z;
    quad[0].u = u0;
    quad[0].v = v0;
    quad[1].x = x1;
    quad[1].y = y0;
    quad[1].z = z;
    quad[1].u = u1;
    quad[1].v = v0;
    quad[2].x = x1;
    quad[2].y = y1;
    quad[2].z = z;
    quad[2].u = u1;
    quad[2].v = v1;
    quad[3].x = x0;
    quad[3].y = y1;
    quad[3].z = z;
    quad[3].u = u0;
    quad[3].v = v1;

    for (gfx::Vertex2D &vertex : quad) {
        vertex.color[0] = color.bytes.red;
        vertex.color[1] = color.bytes.green;
        vertex.color[2] = color.bytes.blue;
        vertex.color[3] = color.bytes.alpha;
        vertex.fog = 0xFF;
    }

    gfx::TextureBinding binding;
    PortMgTextureBinding(texture, binding);
    gfx::Draw2D(gfx::Primitive::Quads, quad, binding, state);
    return 0;
}

PORT_OVERRIDE u_int mgCSprite::CreatePacket(mgCDrawManager *manager) {
    (void) manager;
    return 0;
}

// ---- mgC3DSprite -------------------------------------------------------------------------------

PORT_OVERRIDE int mgC3DSprite::CreateRenderInfoPacket(u_int *packet, float (*matrix)[4], mgRENDER_INFO *info) {
    (void) packet;
    (void) matrix;
    (void) info;
    return 0;
}

PORT_OVERRIDE void mgC3DSprite::BeginCreatePacket(int mode, mgCDrawManager *manager) {
    (void) manager;
    SpriteState &state = g_sprites[this];
    state.billboards.clear();
    state.texture = nullptr;
    state.has_env = false;
    state.mode = mode;
    this->mode = mode;
    packet = 0;
    sprite_num = 0;
    prog_started = 0;
}

PORT_OVERRIDE void mgC3DSprite::CPSetDrawEnv(mgCDrawEnv *env) {
    if (env != NULL) {
        SpriteState &state = g_sprites[this];
        state.env = *env;
        state.has_env = true;
    }
}

PORT_OVERRIDE void mgC3DSprite::CPSetTexture(mgCTexture *texture) {
    g_sprites[this].texture = texture;
}

PORT_OVERRIDE void mgC3DSprite::BeginCPSprite() {}

PORT_OVERRIDE void mgC3DSprite::CPSetSprite(float *first, float *second, float *third, float *fourth, float *fifth) {
    SpriteState  &state = g_sprites[this];
    Billboard     billboard = {};
    std::memcpy(billboard.position, first, sizeof(billboard.position));
    std::memcpy(billboard.size, second, sizeof(billboard.size));
    std::memcpy(billboard.colour, third, sizeof(billboard.colour));
    std::memcpy(billboard.uv0, fourth, sizeof(billboard.uv0));
    std::memcpy(billboard.uv1, fifth, sizeof(billboard.uv1));
    state.billboards.push_back(billboard);
    sprite_num++;
}

PORT_OVERRIDE void mgC3DSprite::EndCPSprite() {}

PORT_OVERRIDE void mgC3DSprite::EndCreatePacket() {
    packet_start = nullptr;
    packet_cur = nullptr;
}

PORT_OVERRIDE int mgC3DSprite::Draw(u_int *tag, float (*matrix)[4], mgCDrawManager *manager) {
    (void) tag;

    if (manager == NULL) {
        manager = &mgDrawManager;
    }

    mgRENDER_INFO *info = manager->render_info;

    if (info == NULL) {
        return 0;
    }

    texture_manager = manager->texture_manager;

    SpriteState &state = g_sprites[this];

    gfx::DrawState draw_state;

    if (state.has_env) {
        draw_state = PortMgDrawStateFromEnv(state.env);
    } else {
        draw_state = PortMgDrawStateFromEnv(info->draw_env[0]);
    }

    SpriteFog(draw_state, info);

    gfx::TextureBinding binding;
    PortMgTextureBinding(state.texture, binding);

    for (const Billboard &billboard : state.billboards) {
        float world[4];
        float projected[4];
        ApplyMatrix(world, matrix, billboard.position);
        ApplyMatrix(projected, info->world_screen, world);

        if (projected[3] < 1.0f) {
            continue;
        }

        float reciprocal = 1.0f / projected[3];
        float centre_x = projected[0] * reciprocal;
        float centre_y = projected[1] * reciprocal;
        float depth = PortMgDepthFromGs(projected[2] * reciprocal);
        float half_w = 0.5f * billboard.size[0] * info->view_screen[0][0] * reciprocal;
        float half_h = 0.5f * billboard.size[1] * info->view_screen[1][1] * reciprocal;
        float angle = state.mode == MG_3DSPRITE_MODE_ROTATE ? billboard.size[2] : 0.0f;
        float cosine = std::cos(angle);
        float sine = std::sin(angle);

        float        fog = VertexFog(info, draw_state.fog, projected[3]);
        gfx::Vertex2D quad[4];

        for (int corner = 0; corner < 4; corner++) {
            float dx = (corner == 0 || corner == 3) ? -half_w : half_w;
            float dy = (corner < 2) ? -half_h : half_h;
            float rx = dx * cosine - dy * sine;
            float ry = dx * sine + dy * cosine;
            quad[corner].x = PortMgLogicalX(16.0f * (centre_x + rx));
            quad[corner].y = PortMgLogicalY(16.0f * (centre_y + ry));
            quad[corner].z = depth;
            quad[corner].u = corner == 0 || corner == 3 ? billboard.uv0[0] : billboard.uv1[0];
            quad[corner].v = corner < 2 ? billboard.uv0[1] : billboard.uv1[1];
            quad[corner].color[0] = ColourByte(billboard.colour[0]);
            quad[corner].color[1] = ColourByte(billboard.colour[1]);
            quad[corner].color[2] = ColourByte(billboard.colour[2]);
            quad[corner].color[3] = ColourByte(billboard.colour[3]);
            quad[corner].fog = fog;
        }

        gfx::Draw2D(gfx::Primitive::Quads, quad, binding, draw_state);
    }

    g_sprites.erase(this);
    return 0;
}
