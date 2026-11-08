// Data-only mod items and weapons appended to the live item tables (moved out of ps2/src/gamedata.cpp).

#include "common.h"

#include <cstdio>
#include <cstring>

#include "gamedata.hpp"
#include "moonmod/gamedata_moonmod.hpp"

// The item number table of ps2/src/gamedata.cpp, named for the port by port/include/stubs/gamedata.hpp.
extern short local_itemdatano_converttable[ITEM_NO_MAX] asm("GameData_ItemIndex");

namespace {
constexpr int kMoonModItemCapacity = MOONMOD_ITEM_CAPACITY;
constexpr int kMoonModWeaponCapacity = MOONMOD_WEAPON_CAPACITY;
constexpr int kMoonModItemNameSize = 96;
constexpr int kCommonItemStorageCapacity = ITEM_COMMON_DATA_MAX + kMoonModItemCapacity + kMoonModWeaponCapacity;

/**
 * Display names for data-only mod items. CDataCommon stores a borrowed pointer, so these buffers
 * stay at fixed addresses for the lifetime of the game process.
 */
char local_moonmod_item_names[kMoonModItemCapacity][kMoonModItemNameSize];
int  local_moonmod_item_count;
char local_moonmod_weapon_names[kMoonModWeaponCapacity][kMoonModItemNameSize];
int  local_moonmod_weapon_count;
} // namespace

void ResetMoonModGameData() {
    memset(local_itemdatano_converttable, -1, sizeof(local_itemdatano_converttable));
    local_moonmod_item_count = 0;
    memset(local_moonmod_item_names, 0, sizeof(local_moonmod_item_names));
    local_moonmod_weapon_count = 0;
    memset(local_moonmod_weapon_names, 0, sizeof(local_moonmod_weapon_names));
}

bool CGameData::RegisterMoonModItem(int template_item_no, const char *display_name, int max_num, int stack_num,
                                    int effect_flags, int effect_amount, int *out_item_no, char *error, size_t error_size) {
    auto fail = [error, error_size](const char *message) {
        if (error != NULL && error_size > 0) {
            std::snprintf(error, error_size, "%s", message);
        }
        return false;
    };

    if (out_item_no != NULL) {
        *out_item_no = -1;
    }
    if (display_name == NULL || display_name[0] == '\0') {
        return fail("item name is required");
    }
    if (max_num <= 0 || max_num > 999 || stack_num <= 0 || stack_num > max_num) {
        return fail("item limits must satisfy 1 <= stack_num <= max_num <= 999");
    }
    const unsigned int cure_all_effects = ITEM_USE_FLAG_CURE_STOP | ITEM_USE_FLAG_CURE_POISON | ITEM_USE_FLAG_CURE_CURSE |
                                          ITEM_USE_FLAG_CURE_SLOW | ITEM_USE_FLAG_CURE_STONE | ITEM_USE_FLAG_CURE_DRY;
    const unsigned int allowed_effects = ITEM_USE_FLAG_RESTORE_HP | ITEM_USE_FLAG_ADD_POWER | cure_all_effects;
    const unsigned int requested_effects = static_cast<unsigned int>(effect_flags);
    const bool one_effect = requested_effects != 0 && (requested_effects & (requested_effects - 1)) == 0;
    if (effect_flags < 0 || (requested_effects & ~allowed_effects) != 0 ||
        (requested_effects != 0 && !one_effect && requested_effects != cure_all_effects)) {
        return fail("item effect override must select one supported native recovery/status effect or the complete cure-all set");
    }
    if ((requested_effects == ITEM_USE_FLAG_RESTORE_HP && (effect_amount < 1 || effect_amount > 32767)) ||
        (requested_effects != 0 && requested_effects != ITEM_USE_FLAG_RESTORE_HP && effect_amount != 0) ||
        (requested_effects == 0 && effect_amount != 0)) {
        return fail("item effect amount does not match the selected native effect");
    }
    if (out_item_no == NULL || common_data == NULL || item_data == NULL || common_num < 0 || item_num < 0 ||
        common_num >= kCommonItemStorageCapacity || item_num >= 162 + kMoonModItemCapacity) {
        return fail("item table capacity is exhausted or not initialized");
    }
    if (local_moonmod_item_count >= kMoonModItemCapacity) {
        return fail("MoonMod item capacity is exhausted (32 items)");
    }

    CDataCommon *template_common = NULL;
    for (int i = 0; i < common_num; ++i) {
        if (common_data[i].item_no == template_item_no) {
            template_common = &common_data[i];
            break;
        }
    }
    if (template_common == NULL) {
        return fail("template item does not exist in the loaded game data");
    }
    if (ConvertUsedItemType(template_common->type) != USED_ITEM_TYPE_ITEM || template_common->list_no < 0 ||
        template_common->list_no >= item_num || (template_common->attribute & ITEM_ATTRIBUTE_USABLE) == 0) {
        return fail("template must be a usable item with a native item-data record");
    }
    if (std::strlen(display_name) >= kMoonModItemNameSize) {
        return fail("item name is longer than 95 bytes");
    }

    int item_no = ITEM_NO_MAX - 1;
    while (item_no > 0 && local_itemdatano_converttable[item_no] >= 0) {
        --item_no;
    }
    if (item_no <= 0) {
        return fail("no free item numbers remain");
    }

    const CDataItem template_item = item_data[template_common->list_no];
    CDataCommon *new_common = &common_data[common_num];
    CDataItem *new_item = &item_data[item_num];
    *new_common = *template_common;
    *new_item = template_item;

    new_common->item_no = static_cast<s16>(item_no);
    new_common->list_no = static_cast<s16>(item_num);
    new_common->max_num = static_cast<u16>(max_num);
    new_common->stack_num = static_cast<s16>(stack_num);
    new_common->name = local_moonmod_item_names[local_moonmod_item_count];
    memcpy(local_moonmod_item_names[local_moonmod_item_count], display_name, std::strlen(display_name) + 1);
    if (effect_flags != 0) {
        new_item->use_flags = static_cast<u32>(effect_flags);
        new_item->value[0] = static_cast<s16>(effect_amount);
        new_item->value[1] = 0;
        new_item->value[2] = 0;
    }

    local_itemdatano_converttable[item_no] = static_cast<short>(common_num);
    ++common_num;
    ++item_num;
    ++local_moonmod_item_count;
    if (item_no > max_item_no) {
        max_item_no = item_no;
    }
    *out_item_no = item_no;
    if (error != NULL && error_size > 0) {
        error[0] = '\0';
    }
    return true;
}

bool CGameData::RegisterMoonModWeapon(int template_item_no, const char *display_name, int max_num, int stack_num,
                                      int attack, int durability_stat, int attack_cap, int durability_cap,
                                      int durability_capacity, int levelup_exp, const int *attributes,
                                      const int *attribute_caps, int *out_item_no, char *error, size_t error_size) {
    auto fail = [error, error_size](const char *message) {
        if (error != NULL && error_size > 0) {
            std::snprintf(error, error_size, "%s", message);
        }
        return false;
    };

    if (out_item_no != NULL) {
        *out_item_no = -1;
    }
    if (display_name == NULL || display_name[0] == '\0') {
        return fail("weapon name is required");
    }
    if (max_num <= 0 || max_num > 999 || stack_num <= 0 || stack_num > max_num) {
        return fail("weapon limits must satisfy 1 <= stack_num <= max_num <= 999");
    }
    if (out_item_no == NULL || common_data == NULL || weapon_data == NULL || common_num < 0 || weapon_num < 0 ||
        common_num >= kCommonItemStorageCapacity || weapon_num >= 116 + kMoonModWeaponCapacity) {
        return fail("weapon table capacity is exhausted or not initialized");
    }
    if (local_moonmod_weapon_count >= kMoonModWeaponCapacity) {
        return fail("MoonMod weapon capacity is exhausted (32 weapons)");
    }
    if (std::strlen(display_name) >= kMoonModItemNameSize) {
        return fail("weapon name is longer than 95 bytes");
    }
    const int optional_values[] = {attack, durability_stat, attack_cap, durability_cap, durability_capacity, levelup_exp};
    for (int value : optional_values) {
        if (value < -1 || value > 32767) {
            return fail("weapon stat overrides must be -1 (unchanged) or from 0 to 32767");
        }
    }
    if ((attributes == NULL) != (attribute_caps == NULL)) {
        return fail("weapon attributes and attribute_caps must be supplied together");
    }
    if (attributes != NULL) {
        for (int index = 0; index < 8; ++index) {
            if (attributes[index] < 0 || attributes[index] > 32767 || attribute_caps[index] < 0 ||
                attribute_caps[index] > 32767) {
                return fail("weapon attribute values and caps must be from 0 to 32767");
            }
        }
    }

    CDataCommon *template_common = NULL;
    for (int index = 0; index < common_num; ++index) {
        if (common_data[index].item_no == template_item_no) {
            template_common = &common_data[index];
            break;
        }
    }
    if (template_common == NULL) {
        return fail("template weapon does not exist in the loaded game data");
    }
    if (ConvertUsedItemType(template_common->type) != USED_ITEM_TYPE_WEAPON || template_common->list_no < 0 ||
        template_common->list_no >= weapon_num) {
        return fail("template must be a weapon with a native weapon-data record");
    }

    int item_no = ITEM_NO_MAX - 1;
    while (item_no > 0 && local_itemdatano_converttable[item_no] >= 0) {
        --item_no;
    }
    if (item_no <= 0) {
        return fail("no free item numbers remain");
    }

    const CDataWeapon template_weapon = weapon_data[template_common->list_no];
    CDataCommon *new_common = &common_data[common_num];
    CDataWeapon *new_weapon = &weapon_data[weapon_num];
    *new_common = *template_common;
    *new_weapon = template_weapon;

    new_common->item_no = static_cast<s16>(item_no);
    new_common->list_no = static_cast<s16>(weapon_num);
    new_common->max_num = static_cast<u16>(max_num);
    new_common->stack_num = static_cast<s16>(stack_num);
    new_common->name = local_moonmod_weapon_names[local_moonmod_weapon_count];
    memcpy(local_moonmod_weapon_names[local_moonmod_weapon_count], display_name, std::strlen(display_name) + 1);
    if (attack >= 0) new_weapon->status[0] = static_cast<s16>(attack);
    if (durability_stat >= 0) new_weapon->status[1] = static_cast<s16>(durability_stat);
    if (attack_cap >= 0) new_weapon->status_max[0] = static_cast<s16>(attack_cap);
    if (durability_cap >= 0) new_weapon->status_max[1] = static_cast<s16>(durability_cap);
    if (durability_capacity >= 0) new_weapon->durability = static_cast<s16>(durability_capacity);
    if (levelup_exp >= 0) new_weapon->levelup_exp = static_cast<s16>(levelup_exp);
    if (attributes != NULL) {
        for (int index = 0; index < 8; ++index) {
            new_weapon->attribute[index] = static_cast<s16>(attributes[index]);
            new_weapon->attribute_max[index] = static_cast<s16>(attribute_caps[index]);
        }
    }

    local_itemdatano_converttable[item_no] = static_cast<short>(common_num);
    ++common_num;
    ++weapon_num;
    ++local_moonmod_weapon_count;
    if (item_no > max_item_no) {
        max_item_no = item_no;
    }
    *out_item_no = item_no;
    if (error != NULL && error_size > 0) {
        error[0] = '\0';
    }
    return true;
}

