#pragma once

// MoonMod: floors built bigger and fuller, and monsters made tougher, for the mods that ask (co-op's scaling for two
// players): dungeons.set_modifiers and dungeons.set_enemy_scale on Chronicle Two (dungeon_scale_moonmod.cpp; the
// operations themselves are in dungeon_scale_api.hpp). This header is the game's side and needs nothing of MoonMod's.
//
// The floor's numbers are read as it is built, so both games of a co-op room that ask alike build one floor from one
// seed. The monsters' are read as a monster is put on the floor and as a blow aimed at the players lands; a change of
// scale reaches the monsters already out at once, each keeping its share of life.

class CColPrim;
struct BASE_MONSTER_TBL;

namespace moonmod::scale {

// ---- dungeons.set_modifiers: the owners' bonuses added up, in percent (0 when none asks) ----------------------------
int RoomBonus();
int MonsterBonus();
// A random floor's room goal and grid side with the room bonus (the native numbers when there is none).
int RoomGoal(int goal);
int BuildArea(int area);
// How many more monsters a floor whose list puts `count` on it gets.
int ExtraMonsters(int count);
// dng_main.cpp, each step of a floor: what a mod's unloading left to do to the monsters out (their life, rescaled).
void StepFloor();
bool RescaleDue();
// dng_event.cpp: how many more it got, as the last floor was built (world.get().monsters_added).
void ExtraMonstersPlaced(int count);
int  ExtraMonstersPlacedLast();

// ---- dungeons.set_enemy_scale: percent of the native numbers (100: as the game has them) -----------------------------
struct EnemyScale {
    int life = 100;       // an ordinary monster's life
    int blows = 100;      // how hard an ordinary monster hits
    int boss_life = 100;  // a boss's
    int boss_blows = 100; // and a boss's blows (and any blow on a boss's floor that no monster dealt)
};
EnemyScale Enemies();
// The life a monster of this kind starts with on the floor now.
int SpawnLife(const BASE_MONSTER_TBL &table);
// A blow of `damage` aimed at the players' side as it lands now: the hero's own and the other side's are as they are.
float Blow(float damage, const CColPrim &prim);

} // namespace moonmod::scale
