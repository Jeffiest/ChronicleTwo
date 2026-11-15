// Co-op's hero rule (moonmod/coop_party.hpp): a room gives each game one hero, as a filter where the game reads its
// party; the save's own party fields are never changed.

#include "moonmod/coop.hpp"
#include "moonmod/coop_party.hpp"

#include "dng_main.hpp"
#include "userdata.hpp"

namespace moonmod::coop {

namespace {

std::optional<int> g_room_hero;
int                g_story_scopes = 0;   // StoryPartyScope objects alive
std::uint64_t      g_change_asked = 0;   // the runtime frame the rule last asked for a change
bool               g_change_asked_once = false;

constexpr std::uint64_t kChangeEvery = 60; // frames between two asks

int BaseHero(int character) {
    if (character == USER_CHARA_ROBO) return USER_CHARA_MAX;
    if (character == USER_CHARA_MONSTER) return USER_CHARA_MONICA;
    return character;
}

int ShapeOf(int hero) { return hero == USER_CHARA_MAX ? USER_CHARA_ROBO : USER_CHARA_MONSTER; }

// Whether the rule may ask now: not twice within a second.
bool MayAsk() {
    const std::uint64_t frame = SharedRuntime().Frame();
    if (g_change_asked_once && frame >= g_change_asked && frame - g_change_asked < kChangeEvery) return false;
    g_change_asked = frame;
    g_change_asked_once = true;
    return true;
}

} // namespace

std::optional<int> RoomHero() { return g_room_hero; }

void SetRoomHero(std::optional<int> hero) {
    g_room_hero = hero;
    g_change_asked_once = false;
}

bool RoomHeroActive() { return g_room_hero.has_value(); }

int RoomHeroParty(int story_party) {
    if (!g_room_hero || g_story_scopes > 0) return story_party;
    return (story_party & (1 << ShapeOf(*g_room_hero))) | (1 << *g_room_hero);
}

std::optional<unsigned> RoomHeroChangeFlags(int story_change, int party) {
    if (!g_room_hero) return std::nullopt;
    const int shape = ShapeOf(*g_room_hero);
    unsigned  flags = 1u << *g_room_hero;
    if ((story_change & (1 << shape)) != 0 && (party & (1 << shape)) != 0) flags |= 1u << shape;
    return flags;
}

StoryPartyScope::StoryPartyScope() { ++g_story_scopes; }
StoryPartyScope::~StoryPartyScope() { --g_story_scopes; }

int RoomHeroFloorChange() {
    if (!g_room_hero) return -1;
    CUserDataManager *user = GetUserDataMan();
    if (user == nullptr) return -1;
    const int playing = user->active_chr_no;
    if (BaseHero(playing) == *g_room_hero) return -1;
    // Out of the other hero's second shape first (R3's change), then across to the room's hero (L3's).
    const bool shaped = playing == USER_CHARA_ROBO || playing == USER_CHARA_MONSTER;
    const int  target = shaped ? BaseHero(playing) : *g_room_hero;
    if ((user->CheckQuickChange(target, nullptr) & QUICK_CHANGE_STATE_ENABLED) == 0) return -1;
    if (!MayAsk()) return -1;
    return shaped ? DNG_CHANGE_UNIT_RIDE : DNG_CHANGE_UNIT_PARTNER;
}

bool RoomHeroTownChange() {
    if (!g_room_hero) return false;
    CUserDataManager *user = GetUserDataMan();
    if (user == nullptr || BaseHero(user->active_chr_no) == *g_room_hero) return false;
    return MayAsk();
}

} // namespace moonmod::coop
