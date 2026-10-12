#include "common.h"

#include "actionchara.hpp"
#include "character.hpp"
#include "dng_hud.hpp"
#include "dng_main.hpp"
#include "maintex.hpp"
#include "mg_drawprim.hpp"
#include "mglib.hpp"
#include "monster.hpp"
#include "prespr.hpp"
#include "scenesnd.hpp"
#include "userdata.hpp"

// Retail's definition returns void against the fixed-up declaration (scripts/port/header_fixups.py).
PORT_OVERRIDE int CLockOnModel::Draw() {
    float target_pos[4];
    int   top_left[4];
    int   bottom_right[4];

    CActionChara *player = (CActionChara *) scene->GetCharacter(0);

    if (player == NULL) {
        return 0;
    }

    name = NULL;

    if (player->target_no == -1) {
        return 0;
    }

    CActiveMonster *target = (CActiveMonster *) scene->GetCharacter(player->target_no);

    if (target == NULL) {
        return 0;
    }

    float size;
    float height = target->body_height;

    if (!(target->target_dist <= target->clip_dist)) {
        return 0;
    }

    if (target->alpha <= 0.0f) {
        return 0;
    }

    CHARA_ENTRY_OBJECT *entry = target->GetEntryObjectPos(CHARA_ENTRY_GROUP_TARGET, 0, target_pos);

    if (entry == NULL) {
        return 0;
    }

    size = entry->size;
    sceVu0CopyVector(pos, target_pos);

    if (!(height <= 85.0f)) {
        height = 85.0f;
    }

    pos[1] += height;
    name = target->tbl->name;
    int monster_id = -1;

    if (DngUserData->active_chr_no == USER_CHARA_MONSTER) {
        monster_id = DngUserData->monster_id;
    }

    if (target->monster_id == monster_id) {
        int message = target->message_no;

        if (message >= 0) {
            message += 5000;
        }

        message_no = message;
    } else {
        message_no = -1;
    }

    if (player->lock_on == 0) {
        target_pos[1] += 5.0f + 10.0f * size;
        SetRotation(0.0f, angle, 0.0f);
        SetPosition(target_pos);
        CObjectFrame::DrawDirect();
        return 0;
    }

    CPreSprite prim;
    prim.Initialize(NULL, NULL);
    prim.Preset2D();
    prim.Coord(1);
    prim.Begin(MG_PRIM_SPRITE);
    prim.Texture(TEX_SystenFrame);
    prim.Color(0x80, 0x80, 0x80, 0x80);

    if (mgTransWorldPrim3DSprite(top_left, bottom_right, target_pos, 20.0f * size, 20.0f * size, 0) != 0) {
        bottom_right[0] -= 0x120;
        bottom_right[1] -= 0x120;
        prim.TextureCrd(0xC0, 0x16);
        prim.Vertex4(top_left[0], top_left[1], 0);
        prim.TextureCrd(0xD2, 0x28);
        prim.Vertex4(top_left[0] + 0x120, top_left[1] + 0x120, 0);
        prim.TextureCrd(0xD2, 0x16);
        prim.Vertex4(bottom_right[0], top_left[1], 0);
        prim.TextureCrd(0xE4, 0x28);
        prim.Vertex4(bottom_right[0] + 0x120, top_left[1] + 0x120, 0);
        prim.TextureCrd(0xC0, 0x26);
        prim.Vertex4(top_left[0], bottom_right[1], 0);
        prim.TextureCrd(0xD2, 0x3A);
        prim.Vertex4(top_left[0] + 0x120, bottom_right[1] + 0x120, 0);
        prim.TextureCrd(0xD2, 0x28);
        prim.Vertex4(bottom_right[0], bottom_right[1], 0);
        prim.TextureCrd(0xE4, 0x3A);
        prim.Vertex4(bottom_right[0] + 0x120, bottom_right[1] + 0x120, 0);
    }

    prim.End();

    return 0;
}
