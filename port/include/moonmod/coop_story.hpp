#pragma once

// What the story's scripts do that two games in a room share, as the game's own units tell it
// (coop_register_moonmod.cpp). Apart from moonmod/coop.hpp so the units that tell need no JSON.

namespace moonmod::coop {

// An event script gave the player an item (the scripts' add_item). In a room the story gives it to both players:
// the watching mod is told ("gift"), and the other game adds it to its own bag. Not what a dungeon's system script
// gives (a chest's content, a floor's prize, a Spheda win): those are each player's own.
void StoryGave(int item_no, int count);
// A dungeon's system script began (a chest opens, the floor is cleared, the hero died, a guide speaks).
void SystemScriptBegan();

} // namespace moonmod::coop
