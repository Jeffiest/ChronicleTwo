#pragma once

/**
 * @file
 * MoonMod town villagers for Chronicle Two (`characters.register` kind = "npc"): a mod's character stands in a
 * town map, loaded into a free villager slot after the game's own pick (CScene::LoadVillager), faces a way, and
 * answers the talk button with a native event or the `game.npc.talk` event. In memory only; unloaded with the mod.
 */

#include <string>
#include <string_view>

#include "moonmod/api.hpp"

class CScene;
class mgCMemory;
struct CSceneEventData;

namespace moonmod {

/** Character numbers of mod villagers start here, past every villager the game's tables number. */
inline constexpr int kModVillagerCharaBase = 0x7000;
/** Mod villagers a port session holds. */
inline constexpr int kMaximumModVillagers = 32;

/** `characters.register` with kind = "npc": see CONTENT.md. `definition` is the one definition object. */
ApiCallResult RegisterChronicleTwoNpc(std::string_view mod_id, const Json &definition);

/** The record of a registered villager by qualified id, or null JSON. */
Json ChronicleTwoNpcJson(std::string_view qualified_id);

/** Every registered villager's record. */
Json ChronicleTwoNpcsJson();

/**
 * Loads the villagers registered for `map_no` into the main-map villager slots from `loaded` on, with the
 * texture blocks from `texb + loaded`, as CScene::LoadVillager loads the game's own; returns how many it placed.
 */
int ChronicleTwoLoadModVillagers(CScene *scene, int map_no, int texb, mgCMemory *stack, int loaded);

/**
 * Answers the talk button at a mod villager: runs its native talk event through `scene` when it has one, else
 * queues `game.npc.talk`. Returns false when `event->chara_no` is not a mod villager (the game runs event 1000).
 */
bool ChronicleTwoModVillagerTalk(CScene *scene, CSceneEventData *event);

/** Forgets the villagers a mod registered. */
void UnloadChronicleTwoNpcs(std::string_view owner);

void ResetChronicleTwoNpcsForTests();

} // namespace moonmod
