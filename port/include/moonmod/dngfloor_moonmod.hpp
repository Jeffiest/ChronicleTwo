#pragma once

// Mod floors appended to a dungeon's floor graph (port/src/moonmod/dngfloor_moonmod.cpp).

class CDngFloorManager;

constexpr int MOONMOD_FLOOR_CAPACITY = 8;                               /**< Mod floors per dungeon map. */
constexpr int MOONMOD_FLOOR_CELL_CAPACITY = MOONMOD_FLOOR_CAPACITY * 2; /**< Grid cells: a root and a room each. */

/** Records the grid the floor script allocated, with room for MOONMOD_FLOOR_CELL_CAPACITY extra cells. */
void MoonModDngFloorMapInfo(CDngFloorManager *manager, int cell_capacity);

/** Forgets the mod floors of the previous map; called by CDngFloorManager::Initialize. */
void ResetMoonModDngFloors();
