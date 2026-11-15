#pragma once

// Co-op's hooks around the building of a dungeon floor, for the game units that call them. They are declared apart
// from moonmod/coop.hpp, which brings nlohmann/json and must come before the game's headers; these need nothing.
// What each does is said in moonmod/coop.hpp ("Floors").

namespace moonmod::coop {

void FloorBuildBegin();
void FloorPlacing(int stage);
void FloorPlaced(int stage);

} // namespace moonmod::coop
