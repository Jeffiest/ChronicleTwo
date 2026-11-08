#include "common.h"
#include "mw_runtime.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "actionchara.hpp"
#include "actscript.hpp"
#include "automap.hpp"
#include "cameracontrol.hpp"
#include "character.hpp"
#include "collision.hpp"
#include "colprim.hpp"
#include "dataread.hpp"
#include "dng_debug.hpp"
#include "dng_effect.hpp"
#include "dng_event.hpp"
#include "dng_main.hpp"
#include "dng_status.hpp"
#include "dngfloor.hpp"
#include "effscript.hpp"
#include "gameutil.hpp"
#include "mainloop.hpp"
#include "maintex.hpp"
#include "map.hpp"
#include "mapinfo.hpp"
#include "mapparts.hpp"
#include "menucommon.hpp"
#include "mg_camera.hpp"
#include "mg_drawenv.hpp"
#include "mg_frame.hpp"
#include "mg_math.hpp"
#include "mg_memory.hpp"
#include "mg_texture.hpp"
#include "mglib.hpp"
#include "monster.hpp"
#include "runscript_opcodes.hpp"
#include "savedata.hpp"
#include "scene.hpp"
#include "sceneload.hpp"
#include "scenesnd.hpp"
#include "scriptinterpreter.hpp"
#include "snd_mngr.hpp"
#include "sound.hpp"
#include "swordeffect.hpp"
#include "text/bounded.hpp"
#include "userdata.hpp"

// MoonMod's monster additions for the ChronicleTwo port: data-only monster variants, per-slot generations that
// let mods hold stale-safe handles, a despawn path for mod-spawned monsters, and the active-monster limit.
// GetMonsterTable, CMonsterMan::GetReferPtr2, CMonsterLocateInfo::SetPutFlag and CMonsterMan::Initialize in
// object/dun/monster.cpp call into these.

// 0 keeps the port's own limit (retail's 5, or more with the more_monsters setting); a mod sets 1..MONSTER_ACTIVE_MAX.
int MonsterActiveLimit = 0;

struct MOONMOD_MONSTER_VARIANT {
    int used;
    BASE_MONSTER_TBL definition;
};

static MOONMOD_MONSTER_VARIANT moonmod_variants[32];

BASE_MONSTER_TBL *FindMoonModMonsterVariant(int monster_id) {
    for (int variant = 0; variant < 32; variant++) {
        if (moonmod_variants[variant].used && moonmod_variants[variant].definition.id == monster_id) {
            return &moonmod_variants[variant].definition;
        }
    }
    return nullptr;
}
static unsigned int moonmod_monster_slot_generation[MONSTER_ACTIVE_MAX];

void BumpMoonModMonsterSlotGeneration(int slot) {
    if (slot < 0 || slot >= MONSTER_ACTIVE_MAX) {
        return;
    }
    moonmod_monster_slot_generation[slot]++;
    if (moonmod_monster_slot_generation[slot] == 0) {
        moonmod_monster_slot_generation[slot]++;
    }
}

unsigned int GetMoonModMonsterSlotGeneration(int slot) {
    if (slot < 0 || slot >= MONSTER_ACTIVE_MAX) {
        return 0;
    }
    return moonmod_monster_slot_generation[slot];
}

unsigned int EnsureMoonModMonsterSlotGeneration(int slot) {
    if (slot < 0 || slot >= MONSTER_ACTIVE_MAX) {
        return 0;
    }
    if (moonmod_monster_slot_generation[slot] == 0) {
        moonmod_monster_slot_generation[slot] = 1;
    }
    return moonmod_monster_slot_generation[slot];
}

bool RegisterMoonModMonsterVariant(int template_id, const char *display_name, int hp, int attack,
                                   int defense, int reward_exp, int reward_money,
                                   const char *model, const char *script, int *out_id) {
    BASE_MONSTER_TBL *template_definition = GetMonsterTable(template_id);
    if (template_definition == NULL || display_name == NULL || display_name[0] == '\0' ||
        model == NULL || model[0] == '\0' || strlen(model) >= sizeof(BASE_MONSTER_TBL::model) ||
        script == NULL || script[0] == '\0' || strlen(script) >= sizeof(BASE_MONSTER_TBL::script) || out_id == NULL) {
        return false;
    }

    int slot = -1;
    for (int variant = 0; variant < 32; variant++) {
        if (!moonmod_variants[variant].used) {
            slot = variant;
            break;
        }
    }
    if (slot < 0) {
        return false;
    }

    int native_id = 0x400 + slot;
    while (GetMonsterTable(native_id) != NULL) {
        native_id++;
    }
    memcpy(&moonmod_variants[slot].definition, template_definition, sizeof(BASE_MONSTER_TBL));
    moonmod_variants[slot].definition.id = native_id;
    memset(moonmod_variants[slot].definition.name, 0, sizeof(moonmod_variants[slot].definition.name));
    dc2::text::Copy(moonmod_variants[slot].definition.name, dc2::text::View(display_name));
    memset(moonmod_variants[slot].definition.model, 0, sizeof(moonmod_variants[slot].definition.model));
    memcpy(moonmod_variants[slot].definition.model, model, strlen(model));
    memset(moonmod_variants[slot].definition.script, 0, sizeof(moonmod_variants[slot].definition.script));
    memcpy(moonmod_variants[slot].definition.script, script, strlen(script));
    moonmod_variants[slot].definition.life = hp;
    moonmod_variants[slot].definition.attack = attack;
    moonmod_variants[slot].definition.defense = defense;
    moonmod_variants[slot].definition.reward_exp = reward_exp;
    moonmod_variants[slot].definition.reward_money = reward_money;
    moonmod_variants[slot].used = 1;
    *out_id = native_id;
    return true;
}

void ResetMoonModMonsterVariants() {
    memset(moonmod_variants, 0, sizeof(moonmod_variants));
}

bool CMonsterMan::DespawnMoonModMonster(int slot, unsigned int generation) {
    if (slot < 0 || slot >= MONSTER_ACTIVE_MAX || generation == 0 ||
        GetMoonModMonsterSlotGeneration(slot) != generation || GetActiveMonster(slot) == NULL || GetScene() == NULL) {
        return false;
    }

    CScene         *scene = GetScene();
    CActiveMonster *monster = GetActiveMonster(slot);
    if (monster->state == ACTIVE_MONSTER_NONE || monster->catch_state == ACTION_CATCH_HELD) {
        return false;
    }

    const int chara_id = slot + DNG_CHARA_SLOT_MONSTER;
    CActionChara *player = static_cast<CActionChara *>(scene->GetCharacter(scene->player_chara));
    if (player != NULL && player->target_no == chara_id) {
        player->target_no = -1;
        player->lock_on = false;
    }

    for (int other_slot = 0; other_slot < MONSTER_ACTIVE_MAX; ++other_slot) {
        CActiveMonster *other = GetActiveMonster(other_slot);
        if (other != NULL && other_slot != slot && other->target_no == chara_id) {
            other->target_no = -1;
        }
    }

    if (effect_man != NULL) {
        effect_man->ClearEffectFromChrid(chara_id);
    }
    ColPrimMan.Delete(chara_id);

    if (monster->tbl != NULL && monster->tbl->boss != 0) {
        boss_max_life -= monster->max_life;
        if (boss_max_life < 0) {
            boss_max_life = 0;
        }
    }

    locate.SetPutFlag(slot, false);
    monster->Initialize();
    monster->target_no = -1;
    monster->tbl = NULL;
    monster->base_tbl = NULL;
    monster->refer_no = -1;
    memory[slot].stReset();

    BumpMoonModMonsterSlotGeneration(slot);
    return true;
}
