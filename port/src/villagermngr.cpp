#include "common.h"

#include "villagermngr.hpp"
#include "vlgr_info.hpp"

PORT_OVERRIDE int CVillagerMngr::GetAppearVlgr(int progress, int time, int map_no, int *villager_ids,
                                               CVillagerPlaceInfo **places) {
    int                           table_count;
    CVillagerPlace               *entry;
    GAME_PROGRESS_INFO           *current;
    int                           found;
    int                           villager_id;
    int                           handled;
    int                           point;
    int                           selected;
    int                           alternative;
    int                           fallback_alternative;
    CVillagerPlace::ProgressInfo *schedule;
    GAME_PROGRESS_INFO           *point_info;
    CVillagerPlaceInfo           *place;

    if (map_no < 0) {
        return 0;
    }

    entry = GetVlgrPlaceTable(&table_count);
    found = 0;

    if (progress < 2) {
        time = 0;
        progress = 1;
    }

    current = GetGameProgressInfo(progress);

    if (current == NULL) {
        return 0;
    }

    for (villager_id = 0; villager_id < table_count; villager_id++, entry++) {
        if (entry->prog_num > 0) {
            if (entry->prog_info != NULL) {
                point = entry->prog_num - 1;
                handled = 0;

                if (point >= 0) {
                    do {
                        schedule = &entry->prog_info[point];
                        point_info = GetGameProgressInfo(schedule->progress);

                        if (point_info != NULL && current->order >= point_info->order) {
                            if (schedule->progress == progress || (schedule->after != 0 && schedule->after == 1)) {
                                selected = 0;
                                alternative = 0;

                                do {
                                    place = schedule->place[alternative][time];

                                    if (place != NULL) {
                                        handled = 1;
                                        selected = 1;

                                        if (map_no >= 0 && place->map_no == map_no) {
                                            places[found] = place;
                                            villager_ids[found] = villager_id;
                                            found++;
                                        }
                                    }

                                    alternative++;
                                } while (alternative < 4);

                                if (selected != 0) {
                                    break;
                                }
                            } else {
                                break;
                            }
                        }

                        point--;
                    } while (point >= 0);
                }

                if (handled == 0 && entry->prog_num > 0) {
                    schedule = entry->prog_info;

                    if (schedule->progress == 1) {
                        fallback_alternative = 0;

                        do {
                            place = schedule->place[fallback_alternative][time];

                            if (place != NULL && map_no >= 0 && place->map_no == map_no) {
                                places[found] = place;
                                villager_ids[found] = villager_id;
                                found++;
                            }

                            fallback_alternative++;
                        } while (fallback_alternative < 4);
                    }
                }
            }
        }
    }

    return found;
}
