#include "common.h"

#include <libgraph.h>
#include <libvu0.h>

#include <cstring>
#include <vector>

#include "gfx/gfx.hpp"
#include "mg_dataset.hpp"
#include "mg_drawenv.hpp"
#include "mg_drawprim.hpp"
#include "mg_memory.hpp"
#include "mg_shadow.hpp"
#include "mg_texture.hpp"
#include "mg_visual.hpp"
#include "mglib.hpp"

// The host side of the cast shadow. The ps2 file of this name hands each face list to the shadow
// microprogram, which flattens it onto the ground through the render info's shadow matrix; the
// port draws the same flattened triangles through the renderer instead. The face lists themselves
// come from the ps2 CreateFace (plain CPU), so only the draw and the packet builders are replaced.

int PortMgCollectGroup(const mgCVisualMDT *visual, const mgFACE_GROUP *group,
                       std::vector<gfx::Vertex3D> &vertices, std::vector<uint32_t> &indices, bool *has_colour);
gfx::DrawState PortMgDrawStateFromEnv(const mgCDrawEnv &env);

namespace {

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

void                    PortMgScreenToClip(float clip[4][4]);

// True and drawn when the visual is a shadow model; false leaves the visual to mg_visual.cpp.
bool PortMgShadowVisualDraw(const mgCVisualMDT *visual, float (*matrix)[4], const mgRENDER_INFO *info) {
    if (dynamic_cast<const mgCShadowMDT *>(visual) == nullptr) {
        return false;
    }

    if (info == nullptr || info->attr == nullptr || visual->face_group == nullptr) {
        return true;
    }

    // model -> world -> the ground plane (info.shadow) -> GS screen, then the renderer's clip.
    float screen_to_clip[4][4];
    float world_to_screen[4][4];
    float shadow[4][4];
    float model_to_screen[4][4];
    float mvp[4][4];

    std::memcpy(world_to_screen, info->world_screen, sizeof(world_to_screen));
    std::memcpy(shadow, info->shadow, sizeof(shadow));
    PortMgScreenToClip(screen_to_clip);
    MulMatrix(model_to_screen, world_to_screen, shadow);
    MulMatrix(model_to_screen, model_to_screen, matrix);
    MulMatrix(mvp, screen_to_clip, model_to_screen);

    // The draw environment the setup packet sends: the visual's own or the frame's, depth tested
    // at or in front of the scene's, never written, no alpha test.
    mgCDrawEnv environment = visual->draw_env != NULL ? *visual->draw_env : info->draw_env[0];
    environment.SetZBuf(MG_ZBUF_NO_WRITE);
    environment.test.bits.zte = 1;
    environment.test.bits.ztst = SCE_GS_ZGEQUAL;
    environment.test.bits.ate = 0;
    environment.test.bits.afail = 0;
    environment.test.bits.date = 0;

    gfx::DrawState     state = PortMgDrawStateFromEnv(environment);
    gfx::MeshConstants constants = {};

    state.fog = false;
    constants.flags = gfx::kMeshShadow;
    // The setup packet's RGBAQ(1, 1, 1, 0x80) as a renderer modulation: near-black at half alpha.
    constants.diffuse[0] = constants.diffuse[1] = constants.diffuse[2] = 1.0f / 128.0f;
    constants.diffuse[3] = 1.0f;
    std::memcpy(constants.mvp, mvp, sizeof(mvp));

    for (mgFACE_GROUP *node = visual->face_group; node != NULL; node = node->next) {
        bool                       has_colour = false;
        std::vector<gfx::Vertex3D> vertices;
        std::vector<uint32_t>      indices;

        if (PortMgCollectGroup(visual, node, vertices, indices, &has_colour) == 0 || indices.empty()) {
            continue;
        }

        gfx::DrawMeshImmediate(vertices, indices, constants, gfx::TextureBinding{}, state, nullptr);
    }

    return true;
}

int mgCShadowMDT::CreateFacePacket(u_int *packet, mgCFace *face) {
    (void) packet;
    (void) face;
    return 0;
}

u_int mgCShadowMDT::CreatePacket(mgCDrawManager *draw_manager) {
    (void) draw_manager;
    return 0;
}

int mgCShadowMDT::CreateRenderInfoPacket(u_int *packet, float (*matrix)[4], mgRENDER_INFO *info) {
    (void) packet;
    (void) matrix;
    (void) info;
    return 0;
}
