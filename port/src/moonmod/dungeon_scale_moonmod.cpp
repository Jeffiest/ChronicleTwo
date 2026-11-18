// Floors built bigger and fuller, and monsters made tougher (moonmod/dungeon_scale.hpp).
//
// dungeons.set_modifiers {rooms, monsters}: each mod's bonus in percent, the mods' added up. A random floor's room
// goal grows by the rooms bonus (automap.cpp, CAutoMapGen::Build) on a grid grown to hold them, and its list of
// monsters is gone round again for the monsters bonus (dng_event.cpp, AutoSetMonster).
//
// dungeons.set_enemy_scale {life, blows, boss_life, boss_blows}: each mod's percent of the native numbers, the mods'
// multiplied. A monster put on the floor starts with its kind's life so scaled (monster.cpp, SetActiveMonster), a
// blow aimed at the players lands so scaled (actionchara.cpp, CheckDamage, and co-op's blows on another player's
// hero), and a change reaches the monsters already out at once, each keeping its share of life. A co-op copy of a
// monster is left alone: its life is the game's that runs it.

// nlohmann/json (through the MoonMod headers) must precede the game headers, whose macros it would otherwise see.
#include "moonmod/coop.hpp"
#include "moonmod/dungeon_scale.hpp"
#include "moonmod/dungeon_scale_api.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>

#include "automap.hpp"
#include "colprim.hpp"
#include "dng_main.hpp"
#include "gamedata.hpp"
#include "mainloop.hpp"
#include "monster.hpp"
#include "scenesnd.hpp"

namespace moonmod::scale {

namespace {

struct Modifiers {
    int rooms = 0;
    int monsters = 0;
};

std::map<std::string, Modifiers, std::less<>>  g_modifiers; // by owner
std::map<std::string, EnemyScale, std::less<>> g_enemies;   // by owner

constexpr int kBonusMax = 300;
int           g_extra_placed = 0; // monsters the last floor built got over its list
bool          g_rescale_due = false; // a mod that scaled the monsters unloaded: StepFloor rescales them
constexpr int kScaleMin = 10, kScaleMax = 1000;

ApiCallResult Invalid(std::string message) { return {ApiCallStatus::InvalidArgument, nullptr, std::move(message)}; }
ApiCallResult Ok(Json value) { return {ApiCallStatus::Ok, std::move(value), {}}; }

bool ReadPercent(const Json &object, const char *key, int minimum, int maximum, int &out) {
    const auto field = object.find(key);
    if (field == object.end()) return true;
    if (!field->is_number_integer()) return false;
    const std::int64_t value = field->get<std::int64_t>();
    if (value < minimum || value > maximum) return false;
    out = static_cast<int>(value);
    return true;
}

bool OnlyFields(const Json &object, std::initializer_list<std::string_view> allowed, std::string &unknown) {
    for (auto field = object.begin(); field != object.end(); ++field) {
        if (std::ranges::find(allowed, field.key()) == allowed.end()) {
            unknown = field.key();
            return false;
        }
    }
    return true;
}

Json ModifiersJson() { return {{"rooms", RoomBonus()}, {"monsters", MonsterBonus()}}; }

Json EnemiesJson() {
    const EnemyScale s = Enemies();
    return {{"life", s.life}, {"blows", s.blows}, {"boss_life", s.boss_life}, {"boss_blows", s.boss_blows}};
}

int Scaled(int value, int percent) {
    if (percent == 100 || value <= 0) return value;
    return std::max(1, static_cast<int>(std::lround(static_cast<double>(value) * percent / 100.0)));
}

// The monsters out on the floor now take the new scale, each keeping its share of life (a boss's share of the bosses'
// gauge too). A copy of another game's monster is that game's to scale.
void RescaleLive() {
    if (GetNowLoopNo() != LOOP_DUNGEON || ActiveMonster == nullptr || ActiveMonster->GetScene() == nullptr) return;
    const EnemyScale s = Enemies();
    for (int slot = 0; slot < MONSTER_ACTIVE_MAX; ++slot) {
        CActiveMonster *monster = ActiveMonster->GetActiveMonster(slot);
        if (monster == nullptr || monster->state == ACTIVE_MONSTER_NONE || monster->tbl == nullptr) continue;
        if (coop::IsPuppet(slot)) continue;
        const bool boss = monster->tbl->boss != 0;
        const int  was = monster->max_life;
        const int  now = Scaled(monster->tbl->life, boss ? s.boss_life : s.life);
        if (was <= 0 || now == was) continue;
        if (monster->life > 0) monster->life = std::max(1, static_cast<int>(std::lround(static_cast<double>(monster->life) * now / was)));
        monster->max_life = now;
        if (boss) ActiveMonster->boss_max_life = std::max(0, ActiveMonster->boss_max_life + now - was);
    }
}

ApiCallResult SetModifiers(std::string_view owner, const Json &args) {
    static constexpr const char *kUsage = "expected {rooms = 0-300, monsters = 0-300} (percent more), or {} to clear this mod's";
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) return Invalid(kUsage);
    const Json &request = args[0];
    std::string unknown;
    if (!OnlyFields(request, {"rooms", "monsters"}, unknown)) {
        if (unknown == "chests" || unknown == "chest_value") return Invalid("Chronicle Two's floors take rooms and monsters; " + unknown + " is not here yet");
        return Invalid("unknown field: " + unknown);
    }
    Modifiers m;
    if (!ReadPercent(request, "rooms", 0, kBonusMax, m.rooms) || !ReadPercent(request, "monsters", 0, kBonusMax, m.monsters)) return Invalid(kUsage);
    if (m.rooms == 0 && m.monsters == 0) {
        g_modifiers.erase(std::string(owner));
    } else {
        g_modifiers[std::string(owner)] = m;
    }
    return Ok(ModifiersJson());
}

ApiCallResult SetEnemyScale(std::string_view owner, const Json &args) {
    static constexpr const char *kUsage =
        "expected {life, blows, boss_life, boss_blows} in percent of the game's own (10-1000, 100 as it is), or {} to clear this mod's";
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) return Invalid(kUsage);
    const Json &request = args[0];
    std::string unknown;
    if (!OnlyFields(request, {"life", "blows", "boss_life", "boss_blows"}, unknown)) return Invalid("unknown field: " + unknown);
    EnemyScale s;
    if (!ReadPercent(request, "life", kScaleMin, kScaleMax, s.life) || !ReadPercent(request, "blows", kScaleMin, kScaleMax, s.blows) ||
        !ReadPercent(request, "boss_life", kScaleMin, kScaleMax, s.boss_life) || !ReadPercent(request, "boss_blows", kScaleMin, kScaleMax, s.boss_blows)) {
        return Invalid(kUsage);
    }
    const bool native = s.life == 100 && s.blows == 100 && s.boss_life == 100 && s.boss_blows == 100;
    const auto found = g_enemies.find(owner);
    const bool same = native ? found == g_enemies.end()
                             : found != g_enemies.end() && found->second.life == s.life && found->second.blows == s.blows &&
                                   found->second.boss_life == s.boss_life && found->second.boss_blows == s.boss_blows;
    if (!same) {
        if (native) {
            g_enemies.erase(found);
        } else {
            g_enemies[std::string(owner)] = s;
        }
        RescaleLive();
    }
    return Ok(EnemiesJson());
}

} // namespace

int RoomBonus() {
    int sum = 0;
    for (const auto &[owner, m] : g_modifiers) sum += m.rooms;
    return std::min(sum, kBonusMax);
}

int MonsterBonus() {
    int sum = 0;
    for (const auto &[owner, m] : g_modifiers) sum += m.monsters;
    return std::min(sum, kBonusMax);
}

int RoomGoal(int goal) {
    const int bonus = RoomBonus();
    if (bonus <= 0) return goal;
    return std::clamp(goal + (goal * bonus + 50) / 100, 2, DNG_ROOM_MAX);
}

int BuildArea(int area) {
    const int bonus = RoomBonus();
    if (bonus <= 0) return area;
    // the grid's side grows as the rooms do: rooms are placed at random and the builder gives up on a room it can't
    // fit, so ground grown only as much as the rooms need (its area by the bonus) held fewer of them than asked (a
    // floor of 6 asked for, on 17 squares, came out with 5). Two players have more ground to cover, too.
    return std::clamp(static_cast<int>(std::lround(area * (1.0 + bonus / 100.0))), area, DNG_GRID);
}

int ExtraMonsters(int count) {
    const int bonus = MonsterBonus();
    if (bonus <= 0 || count <= 0) return 0;
    return (count * bonus + 50) / 100;
}

void ExtraMonstersPlaced(int count) { g_extra_placed = count; }

int ExtraMonstersPlacedLast() { return g_extra_placed; }

EnemyScale Enemies() {
    double life = 1.0, blows = 1.0, boss_life = 1.0, boss_blows = 1.0;
    for (const auto &[owner, s] : g_enemies) {
        life *= s.life / 100.0;
        blows *= s.blows / 100.0;
        boss_life *= s.boss_life / 100.0;
        boss_blows *= s.boss_blows / 100.0;
    }
    const auto percent = [](double f) { return std::clamp(static_cast<int>(std::lround(f * 100.0)), kScaleMin, kScaleMax); };
    return {percent(life), percent(blows), percent(boss_life), percent(boss_blows)};
}

int SpawnLife(const BASE_MONSTER_TBL &table) {
    if (g_enemies.empty()) return table.life;
    const EnemyScale s = Enemies();
    return Scaled(table.life, table.boss != 0 ? s.boss_life : s.life);
}

float Blow(float damage, const CColPrim &prim) {
    if (g_enemies.empty() || prim.owner == 0 || (prim.target & DAMAGE_TARGET_PLAYER) == 0) return damage;
    const EnemyScale s = Enemies();
    bool             boss = false;
    const int        slot = prim.owner - DNG_CHARA_SLOT_MONSTER;
    if (slot >= 0 && slot < MONSTER_ACTIVE_MAX && ActiveMonster != nullptr) {
        const CActiveMonster *monster = ActiveMonster->GetActiveMonster(slot);
        boss = monster != nullptr && monster->tbl != nullptr && monster->tbl->boss != 0;
    } else {
        // (a blow no monster dealt: a boss's breath or a trap of the floor; on a boss's floor it is the boss's)
        boss = BattleAreaScene != nullptr && BattleAreaScene->boss_map != 0;
    }
    const int percent = boss ? s.boss_blows : s.blows;
    return percent == 100 ? damage : damage * static_cast<float>(percent) / 100.0f;
}

std::optional<ApiCallResult> Call(std::string_view mod_id, std::string_view path, const Json &args) {
    if (path == "dungeons.set_modifiers") return SetModifiers(mod_id, args);
    if (path == "dungeons.set_enemy_scale") return SetEnemyScale(mod_id, args);
    return std::nullopt;
}

void AddCapabilities(RuntimeConfig &config) {
    config.api_capabilities.push_back({"dungeons.set_modifiers", true, {}});
    config.api_capabilities.push_back({"dungeons.set_enemy_scale", true, {}});
}

void OwnerUnloaded(std::string_view mod_id) {
    g_modifiers.erase(std::string(mod_id));
    // (not here and now: a mod unloads as the game shuts down too, when the floor's monsters are already gone; the
    // floor's next step rescales them)
    if (g_enemies.erase(std::string(mod_id)) > 0) g_rescale_due = true;
}

void StepFloor() {
    if (!g_rescale_due) return;
    g_rescale_due = false;
    RescaleLive();
}

bool RescaleDue() { return g_rescale_due; }

void ResetForTests() {
    g_modifiers.clear();
    g_enemies.clear();
    g_extra_placed = 0;
    g_rescale_due = false;
}

} // namespace moonmod::scale
