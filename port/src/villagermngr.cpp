#include "common.h"

#include "villagermngr.hpp"
#include "vlgr_info.hpp"

// Retail walks place[alternative][time] and the output arrays with 4-byte strides.
PORT_OVERRIDE int CVillagerMngr::GetAppearVlgr(int progress, int time, int map_no, int *villager_ids,
                                               CVillagerPlaceInfo **places) {
    int                 table_count;
    int                 found;
    GAME_PROGRESS_INFO *current;

    if (map_no < 0) {
        return 0;
    }

    CVillagerPlace *entry = GetVlgrPlaceTable(&table_count);
    found = 0;

    if (progress < 2) {
        time = 0;
        progress = 1;
    }

    current = GetGameProgressInfo(progress);

    if (current == NULL) {
        return 0;
    }

    for (int villager_id = 0; villager_id < table_count; villager_id++, entry++) {
        if (entry->prog_num > 0) {
            if (entry->prog_info != NULL) {
                int  point = entry->prog_num - 1;
                bool handled = false;

                if (point >= 0) {
                    do {
                        CVillagerPlace::ProgressInfo *schedule = &entry->prog_info[point];
                        GAME_PROGRESS_INFO           *point_info = GetGameProgressInfo(schedule->progress);

                        if (point_info != NULL && current->order >= point_info->order) {
                            if (schedule->progress == progress || (schedule->after != 0 && schedule->after == 1)) {
                                bool selected = false;

                                for (int alternative = 0; alternative < 4; alternative++) {
                                    CVillagerPlaceInfo *place = schedule->place[alternative][time];

                                    if (place != NULL) {
                                        handled = true;
                                        selected = true;

                                        if (map_no >= 0 && place->map_no == map_no) {
                                            places[found] = place;
                                            villager_ids[found] = villager_id;
                                            found++;
                                        }
                                    }
                                }

                                if (selected) {
                                    break;
                                }
                            } else {
                                break;
                            }
                        }

                        point--;
                    } while (point >= 0);
                }

                if (!handled && entry->prog_num > 0) {
                    CVillagerPlace::ProgressInfo *schedule = entry->prog_info;

                    if (schedule->progress == 1) {
                        for (int alternative = 0; alternative < 4; alternative++) {
                            CVillagerPlaceInfo *place = schedule->place[alternative][time];

                            if (place != NULL && map_no >= 0 && place->map_no == map_no) {
                                places[found] = place;
                                villager_ids[found] = villager_id;
                                found++;
                            }
                        }
                    }
                }
            }
        }
    }

    return found;
}
