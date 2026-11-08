#pragma once

// Mod floor save records kept beside CSaveDataDungeon; savedatadungeon.hpp includes this in the port build.

#include <cstddef>

enum {
    SAVE_DUNGEON_FLOOR_ID_MAX = 40 /**< Largest floor id held by the native dungeon graph. */
};

/**
 * Gives the number of floor records the original save format allocates to a dungeon.
 *
 * Mod-added floors must stay within this range until the game has an extended save backend.
 */
int GetSavedDungeonFloorCount(int dungeon);

/** Gives whether the original save format can persist this dungeon/floor pair. */
bool HasSavedDungeonFloorRecord(int dungeon, int floor);

enum {
    MOONMOD_DUNGEON_SAVE_RECORD_MAX = 8,
    MOONMOD_DUNGEON_SAVE_ID_SIZE = 256,
};

/** Stable-id keyed progress for mod floors that do not fit the retail save table. */
struct MOONMOD_DUNGEON_SAVE_RECORD {
    char           id[MOONMOD_DUNGEON_SAVE_ID_SIZE];
    DNG_FLOOR_SAVE data;
};

/** Versioned sidecar payload; kept outside CSaveData so the retail save ABI stays fixed. */
struct MOONMOD_DUNGEON_SAVE_EXTENSION {
    char                          magic[8];
    u32                           version;
    u32                           record_count;
    MOONMOD_DUNGEON_SAVE_RECORD   records[MOONMOD_DUNGEON_SAVE_RECORD_MAX];
};

bool RegisterMoonModDungeonSaveRecord(CSaveDataDungeon *owner, int dungeon, int floor, const char *stable_id);
DNG_FLOOR_SAVE *GetMoonModDungeonSaveRecord(CSaveDataDungeon *owner, int dungeon, int floor);
void ResetMoonModDungeonSaveExtension(CSaveDataDungeon *owner);
bool ExportMoonModDungeonSaveExtension(const CSaveDataDungeon *owner, MOONMOD_DUNGEON_SAVE_EXTENSION *extension);
bool ImportMoonModDungeonSaveExtension(CSaveDataDungeon *owner, const MOONMOD_DUNGEON_SAVE_EXTENSION *extension);

STATIC_ASSERT(sizeof(MOONMOD_DUNGEON_SAVE_RECORD) == 0x114);
STATIC_ASSERT(sizeof(MOONMOD_DUNGEON_SAVE_EXTENSION) == 0x8B0);
