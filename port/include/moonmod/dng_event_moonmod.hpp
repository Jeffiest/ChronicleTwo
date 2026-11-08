#pragma once

// Mod hooks around the dungeon event unit; dng_event.hpp includes this in the port build.

class CScene;
class CMonsterMan;
class mgCMemory;

/** Returns true once the active dungeon's native monster manager has finished initialization. */
bool IsMonsterManagerReadyForModSpawn(const CScene *scene);

namespace moonmod {
int ChronicleTwoRequestedDungeonEntryFloor(int requested_floor);
int ChronicleTwoDungeonFloorSource(int dungeon, int floor);
bool InheritChronicleTwoTreasureFloor(TRESURE_BOX_FLOOR_INFO *table, int dungeon, int floor);
void PreloadChronicleTwoCharacterTemplates(CMonsterMan *manager, mgCMemory *memory);
}
