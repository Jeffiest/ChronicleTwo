#pragma once

#include <string>
#include <string_view>

#include "moonmod/runtime.hpp"

class CDngFloorManager;
class CActiveMonster;
class CEditMap;
class CSphida;
class mgCMemory;

namespace moonmod {

void ConfigureChronicleTwoApi(RuntimeConfig &config);
void ChronicleTwoNativeItemPickedUp(int item_no, int count);
std::string ChronicleTwoDebugOverlayText();
void ApplyChronicleTwoContent();
void ApplyChronicleTwoDungeons(::CDngFloorManager *manager);
void ApplyChronicleTwoGeorama(::CEditMap *map, ::mgCMemory *memory);
int ChronicleTwoGeoramaNativePartId(std::string_view piece_id, int town_id);
void ChronicleTwoTrackModSpawnedEnemy(std::string_view owner, std::string_view character_id, int slot,
                                      unsigned int generation, int native_monster_id);
void ChronicleTwoEnemyDefeated(::CActiveMonster *monster, const char *source);
float ApplyChronicleTwoCombatDamageFormula(float damage);
void ResetChronicleTwoCombatFormulasForTests();
void ResetChronicleTwoContentForTests();
void ChronicleTwoPhotoTaken(int neta_id, int map_no, int npc_no, int monster_no, bool used);
void ChronicleTwoInventionCreated(int item_id, int neta0, int neta1, int neta2);
void ChronicleTwoSphedaCompleted(int dungeon_id, int floor_id, int clear_value);
void ResetChronicleTwoSignatureForTests();
void SetChronicleTwoSphedaSetupRoutineForTests(void (*routine)(::CSphida *, int));

} // namespace moonmod
