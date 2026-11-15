#pragma once

// Co-op's hero rule: a room gives each game one hero (party.room_hero_set), Max or Monica. While it holds, that hero
// is this game's player whatever the story's party says: Monica can be played before the story has her, the other
// hero cannot be changed to, and the hero's own second shape (the ridepod for Max, a monster form for Monica) is
// open when the story has opened it. Nothing is written into the save: the story's own party fields stay as they
// are, the world a host shares stays the story's, and the rule is a filter where the game reads them.
//
// Declared apart from moonmod/coop.hpp (which brings nlohmann/json and must come before the game's headers) for the
// game units that call these.

#include <optional>

namespace moonmod::coop {

// The room's hero, a USER_CHARA (Max 0 or Monica 1), or none.
std::optional<int> RoomHero();
void               SetRoomHero(std::optional<int> hero);

// userdata.cpp, CUserDataManager::GetNowPartyMember: the party as this game's player has it. With a room's hero it
// is that hero, and its second shape if the story's party has it; else the story's party unchanged.
int RoomHeroParty(int story_party);

// userdata.cpp, CUserDataManager::GetEnableCharaChangeFlag: who can be changed to. With a room's hero: the hero, and
// its second shape when the story allows the change (story_change, the story's own flags) and the party has it;
// a floor that would disable the hero does not (a player would be left with nobody). Without one: none, and the
// game works it out as always.
std::optional<unsigned> RoomHeroChangeFlags(int story_change, int party);

// event_func.cpp, where the story's own scripts ask who is in the party: while one of these is alive
// RoomHeroParty answers with the story's party, so a scene asks about the story and not about a room's hero.
class StoryPartyScope {
public:
    StoryPartyScope();
    ~StoryPartyScope();
    StoryPartyScope(const StoryPartyScope &) = delete;
    StoryPartyScope &operator=(const StoryPartyScope &) = delete;
};

// dngfloor.cpp, CDngFloorManager::IsSealFloor: a seal locks one hero out of a floor, and in a room each player has
// one hero, so a room's floors carry no seal.
bool RoomHeroActive();

// dng_main.cpp (the floor's change buttons) and editloop.cpp (the town's): the change the room's rule asks for now,
// when the hero being played is not the room's (a swap, a visit begun as the other hero, a scene that put the other
// forward): on a floor a DNG_CHANGE_UNIT (back out of the ridepod or a monster form first, then to the hero), or
// -1; in a town true. Asked once the change can happen and not again for a second, so a change the game refuses
// does not open its menu every frame.
int  RoomHeroFloorChange();
bool RoomHeroTownChange();

} // namespace moonmod::coop
