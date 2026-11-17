// Co-op's towns as they are built (moonmod/coop.hpp, "A town as it is built").
//
// A town's Georama is two things in a running game: its record in the save, and, while the town is on screen, the
// live map built from it. The game writes the map into the record when its player leaves the editor, and builds the
// map from the record when the town is walked into. Two games sharing a town need both sooner:
//
// - The builder's game writes its live map into the record as it is built (TownLiveSync), so the record follows each
//   part placed, moved or taken away, and the register tells the mod the town changed. Outside the editor too, while
//   the player walks free: a town loaded and changed without its record written (by the story, or a town loaded
//   before the game first saved it) stands ahead of its record, and the record is what the other game starts from
//   and what its change is merged into (TownRecordFromLiveMap, as a town is sent or taken).
// - A game that takes a town its player stands in builds its live map from the record again where it stands
//   (TownRebuildWanted, TownRebuildStep), with the steps of the game's own load of a town (EditDataLoad, as its
//   debug menu uses it): no walk back to the entrance, the part is simply there.
// - A player in that town's editor gets the other player's parts too, the moment the editor is between actions
//   (TownEditorReady): nothing held against a wall, nothing animating, no menu opening. Their own parts are written
//   into the record first, so the two are merged, and the editor's undo is pointed at the part's new slot.

#include "moonmod/coop.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "editevent.hpp"
#include "editloop.hpp"
#include "editmap.hpp"
#include "editmode.hpp"
#include "mainloop.hpp"
#include "mapjump.hpp"
#include "mapselect.hpp"
#include "platform/game_loop_state.hpp"
#include "save_json.hpp"
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

// After a rebuild, a player standing where a part just landed is set down on the nearest spot the game would let
// them walk out of Georama mode onto (CheckEditToWalk's test): rings of 40 to 800 units around them, 16 spots each.
// A spot that was not walkable before the rebuild (a bridge, a part's top) is left alone: the part did not cover it.
void MovePlayerAside(CScene *scene, CCharacter2 *player, const float *stood) {
    float ground[4];
    if (CheckEditWalkSpot(scene, stood, ground) != 0) return;
    for (int ring = 1; ring <= 20; ++ring) {
        const float radius = 40.0f * static_cast<float>(ring);
        for (int turn = 0; turn < 16; ++turn) {
            const float angle = static_cast<float>(turn) * 3.14159265f / 8.0f;
            const float spot[4] = {stood[0] + radius * std::cos(angle), stood[1], stood[2] + radius * std::sin(angle), 1.0f};
            if (CheckEditWalkSpot(scene, spot, ground) == 0) continue;
            float to[4] = {ground[0], ground[1] + 0.01f, ground[2], 1.0f}; // as EndEditMode sets the player down
            player->SetPosition(to);
            std::fprintf(stderr, "co-op: a part landed where the player stood (%.0f,%.0f,%.0f): moved to %.0f,%.0f,%.0f\n", stood[0],
                         stood[1], stood[2], to[0], to[1], to[2]);
            return;
        }
    }
    std::fprintf(stderr, "co-op: a part landed where the player stood (%.0f,%.0f,%.0f) and no free spot is within 800\n", stood[0],
                 stood[1], stood[2]);
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
    // Where the player stands, and whether they could walk there before the town changed.
    CScene      *shown = GetMainScene();
    const bool   editing = IsEditMode() != 0; // the player stands where they left the field; the cursor is what moves
    CCharacter2 *player = shown != nullptr && !editing ? shown->GetCharacter(shown->player_chara) : nullptr;
    float        stood[4] = {};
    bool         stood_free = false;
    if (player != nullptr) {
        float ground[4];
        player->GetPosition(stood);
        stood[3] = 1.0f;
        stood_free = CheckEditWalkSpot(shown, stood, ground) != 0;
    }
    std::fprintf(stderr, "co-op: town %d built again from its record (%d parts)%s\n", town, TownRecordParts(town),
                 IsEditMode() != 0 ? " in its editor" : "");
    map->ClearAllParts();
    map->LoadData(record);
    map->InitialPlaceParts(record);
    map->GroundBalance(0);
    map->UpdateHouse();
    if (CScene *scene = GetMainScene(); scene != nullptr) GeoUpdateNpcPos(scene);
    if (player != nullptr && stood_free) MovePlayerAside(shown, player, stood);
    if (editing) EditModeAfterRebuild(map);
}

} // namespace

bool TownOnScreen(int town) {
    if (GetNowLoopNo() != LOOP_EDIT) return false;
    CScene *scene = GetMainScene();
    return scene != nullptr && scene->GetMainMapNo() == town;
}

void TownLiveSync() {
    if (GetNowLoopNo() != LOOP_EDIT) return;
    if (IsEditMode() == 0 && !PortTownFieldControl()) return; // a scene, a menu, a house: when the player walks free
    TownRecordFromLiveMap();
}

// What the game itself does as its player leaves the editor or the town (EditDataSave), without the analysis: the
// town that worked out what it fulfils sends that with the record.
void TownRecordFromLiveMap() {
    int        town = -1;
    CEditMap  *map = TownMap(town);
    CSaveData *save = GetSaveData();
    if (map == nullptr || save == nullptr || !TownRecordWritable(town)) return;
    if (CEditData *record = save->GetEditData(town); record != nullptr) map->SaveData(record);
}

// A live map behind its record (another player's change not yet built here) is never written over the record:
// that would take the other player's part away, here and, sent back, in their game too.
bool TownRecordWritable(int town) { return g_rebuild_town < 0 || g_rebuild_town != town; }

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
    if (IsEditMode() != 0 ? !TownEditorReady() : !PortTownFieldControl()) return; // mid-action, a scene, a menu: after
    RebuildNow();
}

bool TownEditorReady() { return IsEditModeTool() != 0 && EditModeBetweenActions() != 0; }

void TownBeforeSave() {
    if (int shown = -1; TownMap(shown) != nullptr) {
        std::fprintf(stderr, "co-op: town %d is written from its live map (%d parts; its record %d)%s\n", shown, TownLiveParts(),
                     TownRecordParts(shown), g_rebuild_town == shown ? ", built again from the record first" : "");
    }
    if (g_rebuild_town < 0) return;
    int town = -1;
    if (TownMap(town) == nullptr || town != g_rebuild_town) return;
    RebuildNow(); // the game is about to write its live map over the record: bring the map up to the record first
}

int TownRecordParts(int town) {
    CSaveData *save = GetSaveData();
    if (save == nullptr || town < 0 || town >= SAVE_EDIT_DATA_MAX) return -1;
    const TownLayout *layout = PortFindTownLayout(save->edit_data[town]);
    if (layout == nullptr) return -1;
    int count = 0;
    for (const TownPart &part : layout->parts) {
        if (part.id != 0) ++count;
    }
    return count;
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
