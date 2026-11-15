// Co-op's towns as they are built (moonmod/coop.hpp, "A town as it is built").
//
// A town's Georama is two things in a running game: its record in the save, and, while the town is on screen, the
// live map built from it. The game writes the map into the record when its player leaves the editor, and builds the
// map from the record when the town is walked into. Two games sharing a town need both sooner:
//
// - The builder's game writes its live map into the record as it is built (TownLiveSync), so the record follows each
//   part placed, moved or taken away, and the register tells the mod the town changed.
// - A game that takes a town its player stands in builds its live map from the record again where it stands
//   (TownRebuildWanted, TownRebuildStep), with the steps of the game's own load of a town (EditDataLoad, as its
//   debug menu uses it): no walk back to the entrance, the part is simply there.

#include "moonmod/coop.hpp"

#include <cstring>

#include "editevent.hpp"
#include "editloop.hpp"
#include "editmap.hpp"
#include "mainloop.hpp"
#include "mapjump.hpp"
#include "mapselect.hpp"
#include "platform/game_loop_state.hpp"
#include "savedata.hpp"
#include "scenesnd.hpp"

namespace moonmod::coop {

namespace {

int g_rebuild_town = -1; // the town whose live map is behind its record, or -1

// The Georama town whose own map is on screen (not a house's inside): its live map and its number, 0-4.
CEditMap *TownMap(int &town) {
    if (GetNowLoopNo() != LOOP_EDIT || InInterior() != 0) return nullptr;
    CScene *scene = GetMainScene();
    if (scene == nullptr || scene->active_map < 0) return nullptr;
    town = scene->GetMainMapNo();
    if (town < 0 || town >= SAVE_EDIT_DATA_MAX || GetMapType(town) != MAP_TYPE_GEORAMA) return nullptr;
    CMap *map = scene->GetMap(scene->active_map);
    if (map == nullptr || std::strcmp(map->Iam(), "CEditMap") != 0) return nullptr;
    return static_cast<CEditMap *>(map);
}

// The game's own load of a town (EditDataLoad) without its last step, the analysis: what the town now fulfils is
// worked out by the game that built the part and comes with the town's record. This game writes nothing.
void RebuildNow() {
    const int town = g_rebuild_town;
    g_rebuild_town = -1;
    int        on_screen = -1;
    CEditMap  *map = TownMap(on_screen);
    CSaveData *save = GetSaveData();
    if (map == nullptr || save == nullptr || on_screen != town) return;
    CEditData *record = save->GetEditData(town);
    if (record == nullptr) return;
    map->ClearAllParts();
    map->LoadData(record);
    map->InitialPlaceParts(record);
    map->GroundBalance(0);
    map->UpdateHouse();
    if (CScene *scene = GetMainScene(); scene != nullptr) GeoUpdateNpcPos(scene);
}

} // namespace

bool TownOnScreen(int town) {
    if (GetNowLoopNo() != LOOP_EDIT) return false;
    CScene *scene = GetMainScene();
    return scene != nullptr && scene->GetMainMapNo() == town;
}

void TownLiveSync() {
    if (GetNowLoopNo() != LOOP_EDIT || IsEditMode() == 0) return;
    int       town = -1;
    CEditMap *map = TownMap(town);
    CSaveData *save = GetSaveData();
    if (map == nullptr || save == nullptr) return;
    if (CEditData *record = save->GetEditData(town); record != nullptr) map->SaveData(record);
}

void TownRebuildWanted(int town) { g_rebuild_town = town; }

bool TownRebuildPending() { return g_rebuild_town >= 0; }

void TownRebuildStep() {
    if (g_rebuild_town < 0) return;
    if (!TownOnScreen(g_rebuild_town)) { // the player left: the town is built from its record when next walked into
        g_rebuild_town = -1;
        return;
    }
    int town = -1;
    if (TownMap(town) == nullptr || town != g_rebuild_town) return; // in a house: when the player comes out
    if (IsEditMode() != 0 || !PortTownFieldControl()) return;         // in a scene or a menu: when it is over
    RebuildNow();
}

void TownBeforeSave() {
    if (g_rebuild_town < 0) return;
    int town = -1;
    if (TownMap(town) == nullptr || town != g_rebuild_town) return;
    RebuildNow(); // the game is about to write its live map over the record: bring the map up to the record first
}

int TownLiveParts() {
    int             town = -1;
    const CEditMap *map = TownMap(town);
    if (map == nullptr) return -1;
    int count = 0;
    for (int i = 0; i < map->edit_parts_max; ++i) {
        const CEditParts &part = map->edit_parts[i];
        if (part.name[0] != '\0' && part.info != nullptr && part.state == EDIT_PARTS_STATE_PLACED) ++count;
    }
    return count;
}

void TownReset() { g_rebuild_town = -1; }

} // namespace moonmod::coop
