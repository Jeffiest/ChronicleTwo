#include "common.h"

#include <cmath>

#include "automap.hpp"
#include "dng_main.hpp"
#include "maintex.hpp"
#include "mglib.hpp"
#include "scenesnd.hpp"

#include "gfx/gfx.hpp"

namespace {

constexpr gfx::UiAnchor kMiniMapAnchor = gfx::UiAnchor::Side(1, -1, true);

} // namespace

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
    sceVu0SubVector(pos, pos, center);
    int        screen_x = x + (int) (16.0f * (pos[0] / cell_w)) + 8;
    int        screen_y = y + (int) (16.0f * (pos[2] / cell_d)) + 8;
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
