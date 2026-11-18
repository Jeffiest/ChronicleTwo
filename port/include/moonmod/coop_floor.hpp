#pragma once

// Co-op's hooks around the building of a dungeon floor, for the game units that call them. They are declared apart
// from moonmod/coop.hpp, which brings nlohmann/json and must come before the game's headers; these need nothing.
// What each does is said in moonmod/coop.hpp ("Floors").

namespace moonmod::coop {

void FloorBuildBegin();
void FloorPlacing(int stage);
void FloorPlaced(int stage);
// dng_event.cpp, LoadDungeonMapFile: the floor's map is loading, built by the generator (`generated`) or one of the
// game's own made maps (a boss's room).
void FloorMapLoading(const char *map_name, bool generated);
// The fight waits (monsters.hold): the floor's monsters and this game's hero stand still (dng_main.cpp asks).
bool FightHeld();

} // namespace moonmod::coop
