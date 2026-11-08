// Mod map identities appended after the native map catalog (moved out of ps2/src/mapselect.cpp).

#include "common.h"

#include <cstring>

#include "mapselect.hpp"
#include "text/bounded.hpp"

extern int            PortMapNameNum asm("MapSelect_MapNameNum");
extern MAP_NAME_INFO *PortMapName asm("MapSelect_MapName");

namespace {
constexpr int MOONMOD_MAP_NAME_MAX = 64;
struct MoonModMapNameInfo {
    MAP_NAME_INFO info;
    char name[16];
    char title[128];
};
MoonModMapNameInfo MoonModMapNames[MOONMOD_MAP_NAME_MAX];
int MoonModMapNameNum;
} // namespace

MAP_NAME_INFO *GetMoonModMapNameInfo(int index) {
    if (index < 0 || index >= MoonModMapNameNum) {
        return NULL;
    }
    return &MoonModMapNames[index].info;
}

int GetMapNameCount() {
    const int maximum = MAP_NAME_BUFF_SIZE * 16 / static_cast<int>(sizeof(MAP_NAME_INFO)) - 1;
    if (PortMapNameNum < 0 || PortMapNameNum > maximum) return 0;
    if (PortMapNameNum + MoonModMapNameNum > maximum + MOONMOD_MAP_NAME_MAX) return 0;
    return PortMapNameNum + MoonModMapNameNum;
}

int RegisterMoonModMapName(const char *name, const char *title) {
    if (name == NULL || title == NULL) return -1;
    const int name_length = strlen(name);
    const int title_length = strlen(title);
    if (name_length < 3 || name_length > 15 || title_length < 1 || title_length >= 128) return -1;
    for (int index = 0; index < name_length; ++index) {
        const unsigned char character = static_cast<unsigned char>(name[index]);
        const bool alpha_numeric = (character >= 'a' && character <= 'z') ||
                                  (character >= '0' && character <= '9');
        if (!alpha_numeric && character != '_' && character != '-') return -1;
    }
    for (int index = 0; index < MoonModMapNameNum; ++index) {
        MoonModMapNameInfo &entry = MoonModMapNames[index];
        if (strcmp(entry.name, name) != 0) continue;
        return strcmp(entry.title, title) == 0 ? PortMapNameNum + index : -1;
    }
    for (int index = 0; index < PortMapNameNum; ++index) {
        if (PortMapName[index].name != NULL && strcmp(PortMapName[index].name, name) == 0) return -1;
    }
    if (MoonModMapNameNum >= MOONMOD_MAP_NAME_MAX || GetMapNameCount() <= 0) return -1;

    MoonModMapNameInfo &entry = MoonModMapNames[MoonModMapNameNum];
    memset(&entry, 0, sizeof(entry));
    dc2::text::Copy(entry.name, dc2::text::View(name));
    dc2::text::Copy(entry.title, dc2::text::View(title));
    entry.info.name = entry.name;
    entry.info.title = entry.title;
    entry.info.add_path = NULL;
    entry.info.type = MAP_TYPE_INTERIOR;
    entry.info.sel_type = MAP_SEL_SPECIAL;
    entry.info.snd_data_id = -1;
    entry.info.area_no = -1;
    return PortMapNameNum + MoonModMapNameNum++;
}

int IsMoonModMapName(int map_no) {
    return map_no >= PortMapNameNum && map_no - PortMapNameNum >= 0 && map_no - PortMapNameNum < MoonModMapNameNum;
}

void ResetMoonModMapNamesForTests() {
    memset(MoonModMapNames, 0, sizeof(MoonModMapNames));
    MoonModMapNameNum = 0;
}
