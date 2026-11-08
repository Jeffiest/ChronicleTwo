// Mod floor save records for CSaveDataDungeon (moved out of ps2/src/savedatadungeon.cpp).

#include "common.h"

#include <cstdio>
#include <cstring>

#include "savedatadungeon.hpp"

namespace {
/** Saved floor records in each dungeon; mirrors limmit_table in ps2/src/savedatadungeon.cpp. */
constexpr short limmit_table[SAVE_DUNGEON_NUM] = {9, 16, 25, 21, 23, 29, 39};
} // namespace

namespace {
struct MoonModFloorBinding {
    int dungeon = -1;
    int floor = -1;
    int record = -1;
};

CSaveDataDungeon                         *moonmod_save_owner;
MOONMOD_DUNGEON_SAVE_RECORD              moonmod_saved_records[MOONMOD_DUNGEON_SAVE_RECORD_MAX];
bool                                      moonmod_saved_record_used[MOONMOD_DUNGEON_SAVE_RECORD_MAX];
MoonModFloorBinding                      moonmod_floor_bindings[MOONMOD_DUNGEON_SAVE_RECORD_MAX];
int                                       moonmod_floor_binding_count;

void ClearMoonModDungeonSaveExtension(CSaveDataDungeon *owner) {
    moonmod_save_owner = owner;
    memset(moonmod_saved_records, 0, sizeof(moonmod_saved_records));
    memset(moonmod_saved_record_used, 0, sizeof(moonmod_saved_record_used));
    for (int index = 0; index < MOONMOD_DUNGEON_SAVE_RECORD_MAX; ++index) {
        moonmod_floor_bindings[index] = {-1, -1, -1};
    }
    moonmod_floor_binding_count = 0;
}

int FindMoonModRecord(const char *stable_id) {
    for (int index = 0; index < MOONMOD_DUNGEON_SAVE_RECORD_MAX; ++index) {
        if (moonmod_saved_record_used[index] && std::strcmp(moonmod_saved_records[index].id, stable_id) == 0) {
            return index;
        }
    }
    return -1;
}
} // namespace

int GetSavedDungeonFloorCount(int dungeon) {
    if (dungeon < 0 || dungeon >= SAVE_DUNGEON_NUM) {
        return 0;
    }
    return limmit_table[dungeon];
}

bool HasSavedDungeonFloorRecord(int dungeon, int floor) {
    return floor >= 0 && floor < GetSavedDungeonFloorCount(dungeon);
}

bool RegisterMoonModDungeonSaveRecord(CSaveDataDungeon *owner, int dungeon, int floor, const char *stable_id) {
    if (owner == NULL || dungeon < 0 || dungeon >= SAVE_DUNGEON_NUM || floor < 0 ||
        floor >= SAVE_DUNGEON_FLOOR_ID_MAX || stable_id == NULL || stable_id[0] == '\0' ||
        std::strlen(stable_id) >= MOONMOD_DUNGEON_SAVE_ID_SIZE) {
        return false;
    }
    if (moonmod_save_owner != owner) {
        ClearMoonModDungeonSaveExtension(owner);
    }

    int record = FindMoonModRecord(stable_id);
    if (record < 0) {
        for (int index = 0; index < MOONMOD_DUNGEON_SAVE_RECORD_MAX; ++index) {
            if (!moonmod_saved_record_used[index]) {
                record = index;
                moonmod_saved_record_used[index] = true;
                std::snprintf(moonmod_saved_records[index].id, sizeof(moonmod_saved_records[index].id), "%s", stable_id);
                break;
            }
        }
    }
    if (record < 0) {
        return false;
    }

    for (int index = 0; index < moonmod_floor_binding_count; ++index) {
        MoonModFloorBinding &binding = moonmod_floor_bindings[index];
        if (binding.dungeon == dungeon && binding.floor == floor) {
            return binding.record == record;
        }
        if (binding.record == record) {
            binding.dungeon = dungeon;
            binding.floor = floor;
            return true;
        }
    }
    if (moonmod_floor_binding_count >= MOONMOD_DUNGEON_SAVE_RECORD_MAX) {
        return false;
    }
    moonmod_floor_bindings[moonmod_floor_binding_count++] = {dungeon, floor, record};
    return true;
}

DNG_FLOOR_SAVE *GetMoonModDungeonSaveRecord(CSaveDataDungeon *owner, int dungeon, int floor) {
    if (owner == NULL || owner != moonmod_save_owner) {
        return NULL;
    }
    for (int index = 0; index < moonmod_floor_binding_count; ++index) {
        const MoonModFloorBinding &binding = moonmod_floor_bindings[index];
        if (binding.dungeon == dungeon && binding.floor == floor && binding.record >= 0 &&
            binding.record < MOONMOD_DUNGEON_SAVE_RECORD_MAX) {
            return &moonmod_saved_records[binding.record].data;
        }
    }
    return NULL;
}

void ResetMoonModDungeonSaveExtension(CSaveDataDungeon *owner) { ClearMoonModDungeonSaveExtension(owner); }

bool ExportMoonModDungeonSaveExtension(const CSaveDataDungeon *owner, MOONMOD_DUNGEON_SAVE_EXTENSION *extension) {
    if (owner == NULL || extension == NULL) {
        return false;
    }
    memset(extension, 0, sizeof(*extension));
    memcpy(extension->magic, "MFDNGSV1", 8);
    extension->version = 1;
    if (owner != moonmod_save_owner) {
        return true;
    }
    for (int index = 0; index < MOONMOD_DUNGEON_SAVE_RECORD_MAX; ++index) {
        if (moonmod_saved_record_used[index]) {
            extension->records[extension->record_count++] = moonmod_saved_records[index];
        }
    }
    return true;
}

bool ImportMoonModDungeonSaveExtension(CSaveDataDungeon *owner, const MOONMOD_DUNGEON_SAVE_EXTENSION *extension) {
    if (owner == NULL || extension == NULL || std::memcmp(extension->magic, "MFDNGSV1", 8) != 0 ||
        extension->version != 1 || extension->record_count > MOONMOD_DUNGEON_SAVE_RECORD_MAX) {
        return false;
    }
    ClearMoonModDungeonSaveExtension(owner);
    for (u32 source = 0; source < extension->record_count; ++source) {
        const MOONMOD_DUNGEON_SAVE_RECORD &entry = extension->records[source];
        if (entry.id[0] == '\0' || memchr(entry.id, '\0', sizeof(entry.id)) == NULL || FindMoonModRecord(entry.id) >= 0) {
            ClearMoonModDungeonSaveExtension(owner);
            return false;
        }
        moonmod_saved_records[source] = entry;
        moonmod_saved_record_used[source] = true;
    }
    return true;
}

