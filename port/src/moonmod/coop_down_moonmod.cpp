// Co-op's "down, not out" (moonmod/coop.hpp, "Down, not out").
//
// Alone, a hero whose life runs out dies: the game runs the hero's death and raises its death event, which changes
// to another hero or ends the game. In a room each game plays one hero, so that would be the end at every fall.
// Under the rule (player.down_rule_set) the hero's death still plays, to its last pose, and there it stays: the
// death event is not raised, no new death is asked for while the life is at nothing, nothing strikes the hero, and
// the pad does not move them. The mod decides what follows: another player revives them (player.revive), or they
// give up (player.give_up) and the game's own death event runs after all.

#include "moonmod/coop.hpp"
#include "moonmod/coop_down.hpp"

#include <algorithm>
#include <cmath>

#include "actionchara.hpp"
#include "dng_main.hpp"
#include "mainloop.hpp"
#include "scenesnd.hpp"
#include "userdata.hpp"

namespace moonmod::coop {

namespace {

std::string g_rule_owner; // the mod that set the rule; empty: the game's own death
bool        g_down = false;

bool OnFloorWithHero() { return GetNowLoopNo() == LOOP_DUNGEON && MainChara__2 != nullptr && DngMainScene != nullptr; }

} // namespace

void SetDownRule(std::string_view owner, bool enabled) {
    if (enabled) {
        g_rule_owner = std::string(owner);
        return;
    }
    if (g_rule_owner != owner) return;
    g_rule_owner.clear();
    // A hero lying under the rule when it ends is not left lying: the game's own death goes on, as when the mod
    // that set the rule goes away.
    std::string ignored;
    if (g_down) HeroGiveUp(ignored);
}

bool DownRule() { return !g_rule_owner.empty(); }

bool HeroFalls(const CActionChara *chara) {
    if (g_rule_owner.empty() || !OnFloorWithHero() || chara != MainChara__2) return false;
    g_down = true;
    return true;
}

bool HeroIsDown() { return g_down; }

bool HeroHeldDown(const CActionChara *chara) { return g_down && chara == MainChara__2; }

bool HeroRevive(int hp, const float *at, std::string &error) {
    if (!g_down || !OnFloorWithHero()) {
        error = "the player isn't down";
        return false;
    }
    CBattleCharaInfo *info = GetBattleCharaInfo();
    if (info == nullptr) {
        error = "the hero's state is unavailable";
        return false;
    }
    const int life = std::clamp(hp, 1, std::max(1, info->GetMaxHp_i()));
    info->AddHp_Point(static_cast<float>(life - info->GetNowHp_i()), 0.0f);
    if (at != nullptr) MainChara__2->SetPosition(at[0], at[1], at[2]);
    g_down = false;
    MainChara__2->damage_req = ACTION_DAMAGE_REQ_NONE;
    MainChara__2->prog_no = ACTION_PROG_MAIN; // the hero's normal script takes them from the ground
    MainChara__2->muteki_time = 120;          // two seconds to find their feet
    return true;
}

bool HeroGiveUp(std::string &error) {
    if (!g_down) {
        error = "the player isn't down";
        return false;
    }
    g_down = false;
    // The game's own death, where it left off: its death event (change hero, or game over).
    if (OnFloorWithHero()) DngMainScene->battle_area.script.event_no = 1200;
    return true;
}

void DownStep() {
    if (!g_down) return;
    if (!OnFloorWithHero()) { // the floor was left (the party fell and walked out, a visit ended): on their feet
        g_down = false;
        return;
    }
    CBattleCharaInfo *info = GetBattleCharaInfo();
    if (info != nullptr && info->GetNowHp_i() > 0) { // healed by something of the game's own: up
        g_down = false;
        MainChara__2->prog_no = ACTION_PROG_MAIN;
        return;
    }
    MainChara__2->muteki_time = std::max(MainChara__2->muteki_time, 30); // nothing strikes a hero who is down
}

void DownOwnerUnloaded(std::string_view owner) {
    if (g_rule_owner != owner) return;
    g_rule_owner.clear();
    std::string ignored;
    if (g_down) HeroGiveUp(ignored); // nobody is left to revive them
}

void DownReset() {
    g_rule_owner.clear();
    g_down = false;
}

} // namespace moonmod::coop
