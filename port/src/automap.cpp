#include "common.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>
#include <cstdio>
#include <cstring>
#include <iterator>

#include "automap.hpp"
#include "dng_main.hpp"
#include "gfx/gfx.hpp"
#include "maintex.hpp"
#include "mglib.hpp"
#include "scenesnd.hpp"

#include "dng_event.hpp"
#include "mg_memory.hpp"
#include "monster.hpp"

#include <cstring>

#include "gfx/gfx.hpp"
#include "mglib_port.hpp"
#include "platform/config.hpp"


// aerial_keys.cpp: the F-key tuning, apart from the game headers SDL clashes with.
void AerialTuneKeys();

namespace {

constexpr gfx::UiAnchor kMiniMapAnchor = gfx::UiAnchor::Side(1, -1, true);

// Retail's table (automap.cpp, static there): the symbols the aerial map still draws as icons use it.
MINIMAP_SYMBOL_INFO symbol_table[] = {
    {MINIMAP_SYMBOL_MONSTER_BLINK, 255, 0,   0,   10, 10, 1, 1},
    {MINIMAP_SYMBOL_MONSTER,       255, 0,   0,   10, 10, 0, 1},
    {MINIMAP_SYMBOL_TREASURE_BOX,  96,  64,  0,   10, 10, 0, 1},
    {MINIMAP_SYMBOL_RANDOM_CIRCLE, 255, 255, 0,   10, 10, 0, 1},
    {MINIMAP_SYMBOL_GEOSTONE,      255, 0,   180, 10, 10, 0, 1},
    {MINIMAP_SYMBOL_SPHIDA_6,      255, 0,   0,   10, 10, 1, 0},
    {MINIMAP_SYMBOL_SPHIDA_7,      0,   0,   255, 10, 10, 1, 0},
    {MINIMAP_SYMBOL_SPHIDA_4,      255, 0,   0,   14, 14, 0, 0},
    {MINIMAP_SYMBOL_SPHIDA_5,      0,   64,  255, 14, 14, 0, 0},
    {MINIMAP_SYMBOL_END,           0,   0,   0,   0,  0,  0, 0}
};

// ---- Aerial map ------------------------------------------------------------------------------
//
// The dungeon map is the live floor seen from straight above the player. The near clip plane sits
// aerial_map_ceiling above the player's feet, so ceilings, roofs and upper walls are cut away. Only
// the cells the retail map would show are copied onto the map; monsters and chests appear as their
// own models under the same rule the retail symbols follow (revealed cell or the reveal item).

constexpr float kAerialHeight = 8000.0f; // camera height above the player
constexpr float kAerialDepth = 4000.0f;  // how far below the player the floor is still drawn
constexpr float kRetailCellPixels = 16.0f;

struct AerialFrame {
    bool  valid = false;
    float cell_x = 0.0f; // target logical pixels per cell along X
    float cell_y = 0.0f; // and along Z
    float center_x = 0.0f;
    float center_y = 0.0f;
    float zoom = 1.0f;
};

AerialFrame aerial;
u_long128   aerial_buffer[0x10000] __attribute__((aligned(64)));
mgCMemory   aerial_memory;

bool AerialEnabled() {
    return ConfigGet().aerial_map;
}

bool CellRevealed(const CMiniMapSymbol &mm, const float *pos) {
    if (BattleAreaScene->minimap_reveal & MINIMAP_REVEAL_SYMBOLS) {
        return true;
    }

    if (mm.grid == NULL) {
        return false;
    }

    int cell_x = (int) ((pos[0] + 0.5f * mm.cell_w) / mm.cell_w);
    int cell_z = (int) ((pos[2] + 0.5f * mm.cell_d) / mm.cell_d);

    if (cell_x < 0 || cell_z < 0 || cell_x >= mm.grid_w || cell_z >= mm.grid_h) {
        return false;
    }

    return mm.grid[cell_z * mm.grid_w + cell_x].visible != 0;
}

// ---- Models ----------------------------------------------------------------------------------
//
// Monsters, chests and the player are drawn into the floor image itself, so the floor and the
// models share one camera and stay together. As on the first game's port, each is drawn through a
// copy of the aerial camera scaled about the model, so it reads at map size without rescaling the
// model. The pivot is the model's drawn root (its frame's local-to-world translation, cached by
// the scene draw this frame), not its logical position: a gap between the two would be magnified.

sceVu0FMATRIX aerial_view;
sceVu0FVECTOR aerial_eye;

// mgSetViewMatrix(view * T(pivot) * S(scale) * T(-pivot)); pivot NULL puts the plain camera back.
void SetAerialView(const float *pivot, float scale) {
    sceVu0FMATRIX view;
    sceVu0CopyMatrix(view, aerial_view);

    if (pivot != NULL) {
        for (int row = 0; row < 3; row++) {
            float moved = aerial_view[0][row] * pivot[0] + aerial_view[1][row] * pivot[1] + aerial_view[2][row] * pivot[2];

            for (int col = 0; col < 3; col++) {
                view[col][row] = aerial_view[col][row] * scale;
            }

            view[3][row] = aerial_view[3][row] + moved * (1.0f - scale);
        }
    }

    mgSetViewMatrix(view, aerial_eye);
}

// The drawn root of a model, falling back to its position before its first draw.
void ModelPivot(CCharacter2 *chara, float *out) {
    chara->GetPosition(out);
    mgCFrame *root = chara->CObjectFrame::frame;

    if (root != NULL) {
        sceVu0FMATRIX lw;
        root->GetLWMatrix(lw);
        out[0] = lw[3][0];
        out[1] = lw[3][1];
        out[2] = lw[3][2];
    }
}

void DrawFloorModels(const CMiniMapSymbol &mm, float scale) {
    mgCTextureManager *tex_man = &mgTexManager;
    int                old = mgActiveLighting(1, 1);
    sceVu0FMATRIX      ld;
    sceVu0FMATRIX      lc;
    sceVu0FVECTOR      amb;
    float              pos[4];
    float              pivot[4];

    mgGetLight(ld, lc);
    mgGetAmbient(amb);
    DngMainScene->GetCharaLighting(lc, amb);
    mgSetLight(ld, lc);
    mgSetAmbient(amb);

    if (TreasureBoxMan != NULL) {
        tex_man->ReloadTexture(TreasureBoxMan->tex_block, (sceVif1Packet *) NULL);

        for (int i = 0; i < TREASURE_BOX_MAX; i++) {
            CTreasureBox *slot = &TreasureBoxMan->box[i];

            if (slot->state != TREASURE_BOX_STATE_UNOPENED || slot->frame == NULL || slot->model == NULL) {
                continue;
            }

            slot->GetPosition(pos);

            if (!CellRevealed(mm, pos)) {
                continue;
            }

            SetAerialView(pos, scale);
            // Its own position as the view position passes CTreasureBox::Draw's distance test.
            slot->Draw(pos);
        }
    }

    if (ActiveMonster != NULL && !(BattleAreaScene->pause_flag & DNG_PAUSE_MONSTER_DRAW)) {
        int show_all = (BattleAreaScene->minimap_reveal & MINIMAP_REVEAL_SYMBOLS) != 0;

        for (int i = 0; i < MONSTER_ACTIVE_MAX; i++) {
            CActiveMonster *monster = ActiveMonster->active[i];

            // CMonsterMan::DrawMiniMapSymbol's test, then DrawSymbol's revealed-cell test.
            if (monster == NULL || monster->chara_kind != ACTION_KIND_SCRIPT ||
                monster->catch_state == ACTION_CATCH_HELD ||
                !(monster->target_dist <= monster->clip_dist || show_all)) {
                continue;
            }

            monster->GetPosition(pos);

            if (!CellRevealed(mm, pos)) {
                continue;
            }

            tex_man->ReloadTexture(monster->refer_no + MONSTER_TEXB_REFER, (sceVif1Packet *) NULL);
            ModelPivot(monster, pivot);
            SetAerialView(pivot, scale);
            monster->DrawDirect();
        }
    }

    if (MainChara__2 != NULL && DngMainScene->CheckDrawChara(0)) {
        tex_man->ReloadTexture(16, (sceVif1Packet *) NULL);
        ModelPivot(MainChara__2, pivot);
        SetAerialView(pivot, scale);
        MainChara__2->DrawDirect();
    }

    SetAerialView(NULL, 1.0f);
    mgActiveLighting(old, 0);
}

// Renders the floor around center from above into the "aerial_map" target.
gfx::TextureHandle RenderAerial(const CMiniMapSymbol &mm, const float *center, float screen_zoom, float render_zoom) {
    aerial.valid = false;

    if (!mgPortRendererReady() || !mgPortFrameOpen() || DngMainMap == NULL ||
        (BattleAreaScene->pause_flag & DNG_PAUSE_MAP_DRAW)) {
        return gfx::kNullTexture;
    }

    const Config &config = ConfigGet();
    const uint32_t width = static_cast<uint32_t>(mgScreenWidth);
    const uint32_t height = static_cast<uint32_t>(mgScreenHeight);
    gfx::TextureHandle target = gfx::NamedRenderTarget("aerial_map", width, height, true, false);

    if (target == gfx::kNullTexture) {
        return gfx::kNullTexture;
    }

    alignas(16) static unsigned char saved_info[sizeof(mgRENDER_INFO)];
    std::memcpy(saved_info, (const void *) &mgRenderInfo, sizeof saved_info);
    const mgPortGsState saved_gs = mgPortGs();

    aerial.zoom = screen_zoom;
    // Pixels per world unit at the floor: eye / height. A cell is zoom * 16 pixels on the map; the
    // target keeps two cells of margin past the large map.
    const float cell_pixels = kRetailCellPixels * render_zoom;
    const float eye = cell_pixels * kAerialHeight / mm.cell_w;
    const float near_dist = kAerialHeight - config.aerial_map_ceiling;
    mgSetRenderInfo(eye, near_dist > 1.0f ? near_dist : 1.0f, kAerialHeight + kAerialDepth);

    sceVu0FMATRIX view_mat;
    sceVu0FVECTOR cam_pos = {center[0], center[1] + kAerialHeight, center[2], 1.0f};
    sceVu0FVECTOR dir = {0.0f, -1.0f, 0.0f, 0.0f};
    sceVu0FVECTOR down = {0.0f, 0.0f, 1.0f, 0.0f}; // screen down is +Z, as on the retail map
    sceVu0CameraMatrix(view_mat, cam_pos, dir, down);
    mgSetViewMatrix(view_mat, cam_pos);
    sceVu0CopyMatrix(aerial_view, view_mat);
    sceVu0CopyVector(aerial_eye, cam_pos);

    // The floor is kAerialHeight from this camera, past where the scene's fog is full. Fog stays
    // on (the map meshes read their fog from these distances either way) but starts beyond the floor,
    // so the floor gets the near value, as it has around the player. mgRenderInfo is restored after.
    mgSetFogParam(kAerialHeight * 4.0f, kAerialHeight * 5.0f, mgRenderInfo.fog.r, mgRenderInfo.fog.g,
                  mgRenderInfo.fog.b, mgRenderInfo.fog.values[1], mgRenderInfo.fog.values[2]);

    {
    mgPortTargetScope target_scope(target);
    // Where the floor lands in the image, through the world-to-clip transform the renderer draws
    // this target with: the camera centre, then one cell along X and one along Z. A negative step
    // is a mirrored axis.
    {
        float                     clip[16];
        const gfx::LogicalMapping mapping = gfx::GetLogicalMapping(target);
        mgPortWorldToClip(clip);
        const auto project = [&](float wx, float wy, float wz, float &lx, float &ly) {
            const float cx = clip[0] * wx + clip[4] * wy + clip[8] * wz + clip[12];
            const float cy = clip[1] * wx + clip[5] * wy + clip[9] * wz + clip[13];
            const float cw = clip[3] * wx + clip[7] * wy + clip[11] * wz + clip[15];
            const float px = (cx / cw + 1.0f) * 0.5f * (float) mapping.pixel_width;
            const float py = (cy / cw + 1.0f) * 0.5f * (float) mapping.pixel_height;
            lx = (px - mapping.offset_x) / mapping.scale_x;
            ly = (py - mapping.offset_y) / mapping.scale_y;
        };
        float step_x, step_y, unused;
        project(center[0], center[1], center[2], aerial.center_x, aerial.center_y);
        project(center[0] + mm.cell_w, center[1], center[2], step_x, unused);
        project(center[0], center[1], center[2] + mm.cell_d, unused, step_y);
        aerial.cell_x = step_x - aerial.center_x;
        aerial.cell_y = step_y - aerial.center_y;

        static int logged = 0;

        if (logged < 3) {
            logged++;
            std::fprintf(stderr, "aerial_map: center %.1f,%.1f cell %.2f x %.2f (eye %.1f, cell_w %.1f)\n",
                         aerial.center_x, aerial.center_y, aerial.cell_x, aerial.cell_y, eye, mm.cell_w);
        }
    }
    // The aerial draws are not matched with the scene's draws of the same objects between ticks.
    mgPortNoInterpolationScope interp_scope;
    const uint8_t black[4] = {0, 0, 0, 0x80};
    gfx::Clear(true, black, true, 0.0f);

    aerial_memory.stSetBuffer(aerial_buffer, sizeof aerial_buffer / sizeof aerial_buffer[0]);
    aerial_memory.stReset();
    int list[16];

    for (int i = 0; i < 15; i++) {
        list[i] = i;
    }

    list[15] = -1;
    mgBeginDraw(&aerial_memory, list, NULL);
    float view[4] = {center[0], center[1], center[2], 0.0f};
    // The occluders are set up for the follow camera; seen from above they can hide the floor.
    const s32 saved_occlusion = DngMainMap->occlusion_num;
    DngMainMap->occlusion_num = 0;
    // Each part far-clips by its distance from the camera, which is kAerialHeight here: lift the
    // near and far distances and the fade for this pass only, then put them back.
    struct PartClip {
        float far_dist, near_dist, fade_alpha;
        s32   fade;
    };
    static PartClip saved_clip[1024];
    const int clip_num = DngMainMap->place_parts_num < 1024 ? DngMainMap->place_parts_num : 1024;

    for (int i = 0; i < clip_num; i++) {
        CMapParts *part = &DngMainMap->place_parts[i];
        saved_clip[i] = {part->far_dist, part->near_dist, part->fade_alpha, part->fade};
        part->far_dist = 0.0f;
        part->near_dist = 0.0f;
        part->fade = 0;
    }

    DngMainMap->PreDraw(view);
    const int parts_listed = DngMainMap->draw_parts_num;
    const int parts_drawn = DngMainMap->Draw();
    DngMainMap->occlusion_num = saved_occlusion;

    for (int i = 0; i < clip_num; i++) {
        CMapParts *part = &DngMainMap->place_parts[i];
        part->far_dist = saved_clip[i].far_dist;
        part->near_dist = saved_clip[i].near_dist;
        part->fade_alpha = saved_clip[i].fade_alpha;
        part->fade = saved_clip[i].fade;
    }

    {
        static int logged = 0;

        if (logged < 3) {
            logged++;
            std::fprintf(stderr, "aerial_map: parts placed %d listed %d drawn %d (occluders %d)\n",
                         DngMainMap->place_parts_num, parts_listed, parts_drawn, (int) saved_occlusion);
        }
    }
    mgPreEndDraw(NULL);

    for (int g = 0; g < 6; g++) {
        int blocks[128];
        int num = DngMainScene->GetTextureBlockNo(g, blocks, 128);

        for (int b = 0; b < num; b++) {
            int block = blocks[num - b - 1];
            mgEndDrawReloadTexture(block, NULL);
            mgEndDraw(block, NULL);
        }
    }

    mgSetPkTextureRepeat(0);
    DrawFloorModels(mm, config.aerial_map_model_scale);
    }

    std::memcpy((void *) &mgRenderInfo, saved_info, sizeof saved_info);
    mgPortGs() = saved_gs;
    aerial.valid = true;
    return target;
}

// Copies the revealed cells of the aerial image onto the map, one retail tile at a time.
void CompositeAerial(const CMiniMapSymbol &mm, gfx::TextureHandle target, int dimmed, uint8_t alpha) {
    const float zoom = aerial.zoom;
    const float tile = kRetailCellPixels * zoom;
    const float map_left = (float) (mm.x - mm.w / 2);
    const float map_top = (float) (mm.y - mm.h / 2);
    // The player sits at the retail map's (x + 8, y + 8).
    const float origin_x = (float) mm.x + 8.0f;
    const float origin_y = (float) mm.y + 8.0f;

    gfx::TextureBinding binding;
    binding.texture = target;
    binding.filter = gfx::Filter::Linear;
    binding.ignore_alpha = true;

    gfx::DrawState state;
    state.blend = alpha < 0x80;
    state.scissor = true;
    state.scissor_rect = {map_left, map_top, (float) mm.w, (float) mm.h};

    std::vector<gfx::Vertex2D> quads;
    CMapParts *parts = mm.parts_table;
    float part_pos[4];

    for (int i = 0; i < mm.parts_num; i++, parts++) {
        if (parts->name[0] == 0 || parts->minimap_tile == -1) {
            continue;
        }

        parts->GetPosition(part_pos);
        uint8_t shade = 0x80;

        if (mm.grid != NULL) {
            CAutoMapParts *cell = &mm.grid[mm.grid_w * (int) (part_pos[2] / mm.cell_d) + (int) (part_pos[0] / mm.cell_d)];

            if (cell->attr & AUTOMAP_ATTR_HIDE) {
                continue;
            }

            if (!cell->visible) {
                if (!dimmed) {
                    continue;
                }

                shade = 0x30;
            }
        }

        const float rate_x = (part_pos[0] - mm.center[0]) / mm.cell_w;
        const float rate_z = (part_pos[2] - mm.center[2]) / mm.cell_d;
        const float dx = origin_x + tile * rate_x - tile / 2.0f;
        const float dy = origin_y + tile * rate_z - tile / 2.0f;
        const float u = aerial.center_x + aerial.cell_x * rate_x - aerial.cell_x / 2.0f;
        const float v = aerial.center_y + aerial.cell_y * rate_z - aerial.cell_y / 2.0f;
        const float uw = aerial.cell_x;
        const float vh = aerial.cell_y;

        if (dx > map_left + mm.w || dy > map_top + mm.h || dx + tile < map_left || dy + tile < map_top) {
            continue;
        }

        const gfx::Vertex2D corners[4] = {
            {dx, dy, 0.0f, u, v, {shade, shade, shade, alpha}},
            {dx + tile, dy, 0.0f, u + uw, v, {shade, shade, shade, alpha}},
            {dx + tile, dy + tile, 0.0f, u + uw, v + vh, {shade, shade, shade, alpha}},
            {dx, dy + tile, 0.0f, u, v + vh, {shade, shade, shade, alpha}},
        };
        quads.insert(quads.end(), corners, corners + 4);
    }

    if (!quads.empty()) {
        mgPortDraw2D(gfx::Primitive::Quads, quads, binding, state);
    }
}

} // namespace

PORT_OVERRIDE void CMiniMapSymbol::SetMapInfo(CMap *new_map, CAutoMapParts *new_grid, int width, int height,
                                              float cell_width, float cell_depth) {
    if (!new_map) {
        return;
    }
    map = new_map;
    grid = new_grid;
    grid_w = width;
    grid_h = height;
    cell_w = cell_width;
    cell_d = cell_depth;
    int capacity = 0;
    parts_table = new_map->GetPlacPartsTable(&capacity);
    parts_num = 0;
    while (parts_table && parts_num < capacity && parts_table[parts_num].name[0]) {
        ++parts_num;
    }
    texture = mgTexManager.GetTexture("minimap1", -1);
    info = nullptr;
    char floor_name[sizeof(BattleAreaScene->map_name)];
    std::memcpy(floor_name, BattleAreaScene->map_name, sizeof floor_name);
    floor_name[sizeof floor_name - 1] = 0;
    if (std::strlen(floor_name) == 7) {
        floor_name[6] = 0;
    }
    for (auto &entry : MiniMapInfoData) {
        if (std::strcmp(entry.name, floor_name) == 0) {
            info = &entry;
            break;
        }
    }
    if (!info) {
        return;
    }
    for (int i = 0; i < parts_num; ++i) {
        auto &part = parts_table[i];
        part.minimap_tile = -1;
        // The final entry has an empty string, not a null pointer.
        for (std::size_t j = 0; j < std::size(PartsInfoData) && j < std::size(info->tile); ++j) {
            const char *name = PartsInfoData[j].name;
            if (!name || !*name) {
                break;
            }
            if (std::strcmp(name, part.parts_name) == 0) {
                part.minimap_tile = info->tile[j];
            }
        }
    }
}

PORT_OVERRIDE void CMiniMapSymbol::DrawSymbolOpen() {
    prim.Initialize(0, 0);
    prim.Preset2D();
    prim.Begin(MG_PRIM_SPRITE);
    prim.Color(0x80, 0x80, 0x80, 0x60);
    prim.Texture(TEX_SystenFrame);
    prim.SetScirror(x - w / 2, y - h / 2, w, h);
    // Symbols other objects draw between Open and Close are part of the map, so they stay anchored with it.
    gfx::SetUiAnchor(kMiniMapAnchor);
}

PORT_OVERRIDE void CMiniMapSymbol::DrawSymbolClose() {
    prim.SetScirror(0, 0, mgScreenWidth - 1, mgScreenHeight - 1);
    prim.End();
    blink_cnt++;

    if (blink_cnt > 30) {
        blink_cnt = 0;
    }

    gfx::SetUiAnchor({});
}

PORT_OVERRIDE void CMiniMapSymbol::DrawSymbol(float *pos, int symbol) {
    float delta[4];

    if (BattleAreaScene->boss_map != 0) {
        return;
    }

    // The models of monsters and chests are in the aerial image; their markers stay hidden.
    if (aerial.valid && (symbol == MINIMAP_SYMBOL_MONSTER || symbol == MINIMAP_SYMBOL_MONSTER_BLINK ||
                         symbol == MINIMAP_SYMBOL_TREASURE_BOX)) {
        return;
    }

    const float cell_pixels = aerial.valid ? kRetailCellPixels * aerial.zoom : kRetailCellPixels;
    sceVu0SubVector(delta, pos, center);
    int screen_x = x + (int) (cell_pixels * (delta[0] / cell_w));
    int screen_y = y + (int) (cell_pixels * (delta[2] / cell_d));
    int revealed = CellRevealed(*this, pos);

    for (MINIMAP_SYMBOL_INFO *symbol_info = symbol_table; symbol_info->symbol != MINIMAP_SYMBOL_END; symbol_info++) {
        if (symbol_info->symbol == symbol) {
            if ((!symbol_info->need_visible || revealed) && (!symbol_info->blink || blink_cnt < 16)) {
                prim.Color(symbol_info->r, symbol_info->g, symbol_info->b, 0x80);
                prim.SetIStretch(screen_x - symbol_info->w / 2 + 8, screen_y - symbol_info->h / 2 + 8, symbol_info->w,
                                 symbol_info->w, 0xBA, 0xF6, 10, 10);
            }

            break;
        }
    }
}

PORT_OVERRIDE void CMiniMapSymbol::DrawSymbol_Chara(CCharacter2 *chara) {
    gfx::UiAnchorScope anchor(kMiniMapAnchor);

    float pos[4];
    float rot[4];

    if (chara == NULL) {
        return;
    }

    if (BattleAreaScene->boss_map != 0) {
        return;
    }

    chara->GetPosition(pos);
    chara->GetRotation(rot);
    const float cell_pixels = aerial.valid ? kRetailCellPixels * aerial.zoom : kRetailCellPixels;

    // The player's model, in the aerial image, replaces the arrow.
    if (aerial.valid) {
        return;
    }

    sceVu0SubVector(pos, pos, center);
    int        screen_x = x + (int) (cell_pixels * (pos[0] / cell_w)) + 8;
    int        screen_y = y + (int) (cell_pixels * (pos[2] / cell_d)) + 8;
    float      angle = rot[1];
    CPreSprite sprite;
    sprite.Initialize(NULL, NULL);
    sprite.Preset2D();
    sprite.Begin(MG_PRIM_TRIANGLE_STRIP);
    sprite.Color(0x80, 0x80, 0x80, 0x60);
    sprite.Texture(TEX_SystenFrame);
    sprite.SetScirror(x - w / 2, y - h / 2, w, h);
    int off_x = (int) (-7.0f * sinf(angle) - -6.0f * cosf(angle));
    int off_y = (int) (-6.0f * sinf(angle) + -7.0f * cosf(angle));
    sprite.TextureCrd(0xC4, 0xF2);
    sprite.Vertex(screen_x + off_x, screen_y + off_y, 0);
    off_x = (int) (-7.0f * sinf(angle) - 6.0f * cosf(angle));
    off_y = (int) (6.0f * sinf(angle) + -7.0f * cosf(angle));
    sprite.TextureCrd(0xD0, 0xF2);
    sprite.Vertex(screen_x + off_x, screen_y + off_y, 0);
    off_x = (int) (7.0f * sinf(angle) - -6.0f * cosf(angle));
    off_y = (int) (-6.0f * sinf(angle) + 7.0f * cosf(angle));
    sprite.TextureCrd(0xC4, 0x100);
    sprite.Vertex(screen_x + off_x, screen_y + off_y, 0);
    off_x = (int) (7.0f * sinf(angle) - 6.0f * cosf(angle));
    off_y = (int) (6.0f * sinf(angle) + 7.0f * cosf(angle));
    sprite.TextureCrd(0xD0, 0x100);
    sprite.Vertex(screen_x + off_x, screen_y + off_y, 0);
    sprite.SetScirror(0, 0, mgScreenWidth - 1, mgScreenHeight - 1);
    sprite.End();
}

PORT_OVERRIDE void CMiniMapSymbol::Draw(float *pos) {
    gfx::UiAnchorScope anchor(kMiniMapAnchor);

    if (map == NULL) {
        return;
    }

    CMapParts *parts = parts_table;

    if (parts == NULL) {
        return;
    }

    if (BattleAreaScene->boss_map != 0) {
        return;
    }

    sceVu0CopyVector(center, pos);
    int dimmed = false;

    if (BattleAreaScene->minimap_reveal & MINIMAP_REVEAL_ROOMS) {
        dimmed = true;
    }

    if (AerialEnabled()) {
        AerialTuneKeys();
        // The caller sets x, y, w and h every frame, so this scales them only for this frame.
        const Config &config = ConfigGet();
        const float   size = config.aerial_map_size;
        float         screen_zoom = config.aerial_map_zoom;
        float         render_zoom = screen_zoom;
        uint8_t       alpha = 0x80;

        if (large) {
            // As the first game's full map: the whole explored floor, fitted to most of the screen,
            // centred on the floor, see-through over the scene.
            float lo_x = 1e30f, lo_z = 1e30f, hi_x = -1e30f, hi_z = -1e30f;
            float part_pos[4];
            CMapParts *part = parts_table;

            for (int i = 0; i < parts_num; i++, part++) {
                if (part->name[0] == 0 || part->minimap_tile == -1) {
                    continue;
                }

                part->GetPosition(part_pos);

                if (grid != NULL) {
                    CAutoMapParts *cell = &grid[grid_w * (int) (part_pos[2] / cell_d) + (int) (part_pos[0] / cell_d)];

                    if ((cell->attr & AUTOMAP_ATTR_HIDE) || !(cell->visible || dimmed)) {
                        continue;
                    }
                }

                lo_x = std::min(lo_x, part_pos[0]);
                hi_x = std::max(hi_x, part_pos[0]);
                lo_z = std::min(lo_z, part_pos[2]);
                hi_z = std::max(hi_z, part_pos[2]);
            }

            const float box_w = (mgScreenWidth - 64) * size;
            const float box_h = (mgScreenHeight - 96) * size;

            if (lo_x <= hi_x) {
                const float span_x = (hi_x - lo_x) / cell_w + 1.0f;
                const float span_z = (hi_z - lo_z) / cell_d + 1.0f;
                screen_zoom = std::min(box_w / span_x, box_h / span_z) / kRetailCellPixels;
                screen_zoom = std::min(screen_zoom, config.aerial_map_zoom * 2.0f);
                // The image is the screen's size; a floor wider than that is rendered smaller and stretched.
                render_zoom = std::min(screen_zoom, std::min((mgScreenWidth - 32) / span_x,
                                                             (mgScreenHeight - 32) / span_z) / kRetailCellPixels);
                center[0] = (lo_x + hi_x) / 2.0f;
                center[2] = (lo_z + hi_z) / 2.0f;
            }

            x = (s16) (mgScreenWidth / 2 - 8);
            y = (s16) (mgScreenHeight / 2 - 8);
            w = (s16) box_w;
            h = (s16) box_h;
            alpha = 0x60;
        } else {
            w = (s16) (w * size);
            h = (s16) (h * size);
        }

        gfx::TextureHandle target = RenderAerial(*this, center, screen_zoom, render_zoom);

        if (target != gfx::kNullTexture) {
            CompositeAerial(*this, target, dimmed, alpha);
            return;
        }
    }

    aerial.valid = false;
    CPreSprite sprite;
    CPreSprite spare;
    float      part_pos[4];
    float      delta[4];
    sprite.Initialize(NULL, NULL);
    sprite.Preset2D();
    sprite.Begin(MG_PRIM_SPRITE);
    sprite.Texture(texture);
    sprite.Color(0x80, 0x80, 0x80, 0x60);
    sprite.SetScirror(x - w / 2, y - h / 2, w, h);

    for (int i = 0; i < parts_num; i++) {
        if ((parts->name[0] == 0) == 0) {
            int tile = parts->minimap_tile;

            if (tile == -1) {
                parts++;
                continue;
            }

            int tile_u = (tile % 16) * 16;
            int tile_v = (tile / 16) * 16;
            parts->GetPosition(part_pos);
            sceVu0SubVector(delta, part_pos, center);
            float          rate_z = delta[2] / cell_d;
            float          rate_x = delta[0] / cell_w;
            float          screen_x = (float) x + 16.0f * rate_x;
            float          screen_y = y + rate_z * 16.0f;
            float          size_d = cell_d;
            CAutoMapParts *cells = grid;

            if (cells != NULL) {
                s16            width = grid_w;
                CAutoMapParts *cell = &cells[width * (int) (part_pos[2] / size_d) + (int) (part_pos[0] / size_d)];

                if (cell->attr & AUTOMAP_ATTR_HIDE) {
                    parts++;
                    continue;
                }

                if (cell->visible) {
                    sprite.Color(0x80, 0x80, 0x80, 0x80);
                } else if (dimmed) {
                    sprite.Color(0x30, 0x30, 0x30, 0x80);
                } else {
                    sprite.Color(0x30, 0x30, 0x30, 0);
                }
            }

            int rect_x = (int) screen_x;
            sprite.SetIRect(rect_x, (int) screen_y, 16, 16, tile_u, tile_v);
        }

        parts++;
    }

    sprite.SetScirror(0, 0, mgScreenWidth - 1, mgScreenHeight - 1);
    sprite.End();
}
