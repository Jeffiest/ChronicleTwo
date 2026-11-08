// Mod floors appended beside an existing floor of a dungeon map. object/dun/dngfloor.cpp allocates each map's grid
// with room for them and reports it through MoonModDngFloorMapInfo.

#include "common.h"

#include <cstdio>
#include <cstring>

#include "dngfloor.hpp"
#include "mainloop.hpp"
#include "moonmod/dngfloor_moonmod.hpp"
#include "savedata.hpp"
#include "savedatadungeon.hpp"

namespace {
constexpr int kMoonModFloorCapacity = MOONMOD_FLOOR_CAPACITY;
constexpr int kMoonModFloorTitleSize = 96;

/** The manager whose grid was last allocated, and the base and capacity of that allocation. */
CDngFloorManager *tree_moonmod_manager;
GLID_INFO        *tree_glid_base;
int        tree_glid_capacity;
int        tree_moonmod_floor_count;
int        tree_moonmod_floor_ids[kMoonModFloorCapacity];
char       tree_moonmod_floor_titles[kMoonModFloorCapacity][kMoonModFloorTitleSize];
} // namespace

void MoonModDngFloorMapInfo(CDngFloorManager *manager, int cell_capacity) {
    tree_moonmod_manager = manager;
    tree_glid_base = manager->glid_info;
    tree_glid_capacity = cell_capacity;
}

void ResetMoonModDngFloors() {
    tree_moonmod_manager = NULL;
    tree_glid_base = NULL;
    tree_glid_capacity = 0;
    tree_moonmod_floor_count = 0;
    for (int i = 0; i < kMoonModFloorCapacity; ++i) {
        tree_moonmod_floor_ids[i] = -1;
    }
    memset(tree_moonmod_floor_titles, 0, sizeof(tree_moonmod_floor_titles));
}

bool CDngFloorManager::RegisterMoonModFloor(int source_floor, const char *title, const char *stable_id, int *out_floor_id,
                                            char *error, size_t error_size) {
    auto fail = [error, error_size](const char *message) {
        if (error != NULL && error_size > 0) {
            std::snprintf(error, error_size, "%s", message);
        }
        return false;
    };
    if (out_floor_id != NULL) {
        *out_floor_id = -1;
    }
    if (tree_moonmod_manager != this || tree_glid_base != glid_info || glid_info == NULL ||
        title == NULL || title[0] == '\0' || stable_id == NULL || stable_id[0] == '\0' || out_floor_id == NULL) {
        return fail("dungeon map is not ready for mod floors");
    }
    if (std::strlen(title) >= kMoonModFloorTitleSize) {
        return fail("floor title is longer than 95 bytes");
    }
    if (tree_moonmod_floor_count >= kMoonModFloorCapacity || glid_num + 2 > tree_glid_capacity) {
        return fail("this dungeon's mod floor capacity is exhausted");
    }
    DNGMAP_ROOM_INFO *source_info = GetDngMapFloorInfo(source_floor);
    GLID_INFO *source_glid = GetDngMapFloorGlidInfo(source_floor);
    if (source_info == NULL || source_glid == NULL) {
        return fail("source_floor is not present in this dungeon map");
    }
    if (source_info->order >= 126) {
        return fail("source floor order cannot be extended safely");
    }

    int floor_id = 0;
    for (; floor_id < DNGMAP_FLOOR_NUM; ++floor_id) {
        if (GetDngMapFloorInfo(floor_id) == NULL) {
            break;
        }
    }
    if (floor_id >= DNGMAP_FLOOR_NUM) {
        return fail("no free native floor ids remain in this dungeon");
    }

    static constexpr int kDirections[GLID_DIR_NUM] = {GLID_DIR_RIGHT, GLID_DIR_DOWN, GLID_DIR_LEFT, GLID_DIR_UP};
    static constexpr int kDeltaX[GLID_DIR_NUM] = {0, 0, -1, 1};
    static constexpr int kDeltaY[GLID_DIR_NUM] = {-1, 1, 0, 0};
    static constexpr int kOpposite[GLID_DIR_NUM] = {GLID_DIR_DOWN, GLID_DIR_UP, GLID_DIR_RIGHT, GLID_DIR_LEFT};
    int selected_direction = -1;
    int root_x = 0;
    int root_y = 0;
    int room_x = 0;
    int room_y = 0;
    for (int i = 0; i < GLID_DIR_NUM; ++i) {
        const int dir = kDirections[i];
        if (source_info->link[dir] >= 0) {
            continue;
        }
        const int dx = kDeltaX[dir];
        const int dy = kDeltaY[dir];
        const int candidate_root_x = source_glid->x + dx;
        const int candidate_root_y = source_glid->y + dy;
        const int candidate_room_x = source_glid->x + 2 * dx;
        const int candidate_room_y = source_glid->y + 2 * dy;
        if (candidate_root_x < 0 || candidate_root_y < 0 || candidate_room_x < 0 || candidate_room_y < 0) {
            continue;
        }
        bool root_occupied = false;
        bool room_occupied = false;
        for (int cell = 0; cell < glid_num; ++cell) {
            root_occupied |= glid_info[cell].x == candidate_root_x && glid_info[cell].y == candidate_root_y;
            room_occupied |= glid_info[cell].x == candidate_room_x && glid_info[cell].y == candidate_room_y;
        }
        if (!root_occupied && !room_occupied) {
            selected_direction = dir;
            root_x = candidate_root_x;
            root_y = candidate_root_y;
            room_x = candidate_room_x;
            room_y = candidate_room_y;
            break;
        }
    }
    if (selected_direction < 0) {
        return fail("source floor has no free adjacent route for a new room");
    }

    CSaveData *save = GetSaveData();
    if (save == NULL || !RegisterMoonModDungeonSaveRecord(&save->save_dungeon, dng_no, floor_id, stable_id)) {
        return fail("no persistent MoonFactory floor-save record is available");
    }

    char *stable_title = tree_moonmod_floor_titles[tree_moonmod_floor_count];
    memcpy(stable_title, title, std::strlen(title) + 1);
    GLID_INFO *root = &glid_info[glid_num];
    GLID_INFO *room = &glid_info[glid_num + 1];
    std::memset(root, 0, sizeof(*root));
    std::memset(room, 0, sizeof(*room));
    root->type = GLID_TYPE_ROOT;
    root->x = static_cast<s16>(root_x);
    root->y = static_cast<s16>(root_y);
    root->root.type = 0;
    root->root.shape = 0;
    root->root.show_mark = 0;
    room->type = GLID_TYPE_ROOM;
    room->x = static_cast<s16>(room_x);
    room->y = static_cast<s16>(room_y);
    room->room.floor_id = static_cast<s8>(floor_id);
    room->room.order = static_cast<s8>(source_info->order + 1);
    room->room.flag = DNGMAP_ROOM_FLAG_ROOM;
    room->room.unk_0 = source_info->unk_0;
    room->room.title = stable_title;
    room->room.practice_type = -1;
    room->room.tex_no = source_info->tex_no;
    room->room.selectable = true;
    for (int direction = 0; direction < GLID_DIR_NUM; ++direction) {
        room->room.link[direction] = -1;
    }
    room->room.link[kOpposite[selected_direction]] = static_cast<s16>(source_floor);
    room->room.key_room[0] = -1;
    room->room.key_room[1] = -1;
    room->room.key_room[2] = -1;
    room->room.key_room[3] = -1;
    source_info->link[selected_direction] = static_cast<s16>(floor_id);

    glid_num += 2;
    if (glid_w <= room_x) {
        glid_w = static_cast<s16>(room_x + 1);
    }
    if (glid_h <= room_y) {
        glid_h = static_cast<s16>(room_y + 1);
    }
    tree_moonmod_floor_ids[tree_moonmod_floor_count] = floor_id;
    ++tree_moonmod_floor_count;
    RelationGlid();
    *out_floor_id = floor_id;
    if (error != NULL && error_size > 0) {
        error[0] = '\0';
    }
    return true;
}

bool CDngFloorManager::IsMoonModFloor(int floor_id, const char *title) const {
    if (tree_moonmod_manager != this || title == NULL) {
        return false;
    }
    for (int i = 0; i < tree_moonmod_floor_count; ++i) {
        if (tree_moonmod_floor_ids[i] != floor_id) {
            continue;
        }
        DNGMAP_ROOM_INFO *info = const_cast<CDngFloorManager *>(this)->GetDngMapFloorInfo(floor_id);
        return info != NULL && info->title != NULL && std::strcmp(info->title, title) == 0;
    }
    return false;
}
