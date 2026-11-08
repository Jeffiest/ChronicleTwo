#include "moonmod/chronicle_two_adapter.hpp"
#include "moonmod/audio_api.hpp"
#include "moonmod/combat_formula.hpp"
#include "moonmod/native_resource_overrides.hpp"
#include "moonmod/runtime.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "dngfloor.hpp"
#include "dng_event.hpp"
#include "dng_main.hpp"
#include "save_json.hpp"
#include "cameracontrol.hpp"
#include "editanalyze.hpp"
#include "editmap.hpp"
#include "gamedata.hpp"
#include "inventmn.hpp"
#include "mainloop.hpp"
#include "menucls1.hpp"
#include "monster.hpp"
#include "mapselect.hpp"
#include "character.hpp"
#include "photo.hpp"
#include "savedata.hpp"
#include "sphida.hpp"
#include "userdata.hpp"
#include "scenesnd.hpp"
#include "platform/input.hpp"
#include "texture_source.hpp"

class CInventDataManage;
CInventDataManage *MoonModGetNativeInventDataManage();

namespace {
/** Placed parts of one definition in a town's saved layout, as CEditData::GetPartsNumID counted the record's arrays. */
int SavedTownPartCount(const CEditData &town, int parts_id) {
    const TownLayout *layout = PortFindTownLayout(town);
    if (layout == nullptr) {
        return 0;
    }
    return static_cast<int>(std::ranges::count_if(
        layout->parts, [parts_id](const TownPart &part) { return part.id != 0 && part.id == parts_id && part.state != 0; }));
}
} // namespace

namespace moonmod {
namespace {

CombatDamageFormulaRegistry g_combat_damage_formulas;

std::string CurrentLanguageTag() {
    switch (LanguageCode) {
        case LANG_JAPANESE: return "ja";
        case LANG_ENGLISH: return "en";
        case LANG_FRENCH: return "fr";
        case LANG_GERMAN: return "de";
        case LANG_ITALIAN: return "it";
        case LANG_SPANISH: return "es";
        case LANG_CHINESE: return "zh";
        case LANG_KOREAN: return "ko";
        default: return "en";
    }
}

struct InputContextFrame {
    std::string token;
    std::string owner;
    std::string id;
};

struct InputContextRegistry {
    std::vector<InputContextFrame> stack;
    std::map<std::string, std::string, std::less<>> action_contexts;
    std::uint64_t next_token = 1;

    std::string Active() const { return stack.empty() ? "default" : stack.back().id; }

    std::string ContextFor(std::string_view action) const {
        const auto found = action_contexts.find(action);
        return found == action_contexts.end() ? "default" : found->second;
    }

    bool IsActive(std::string_view action) const { return ContextFor(action) == Active(); }

    bool Push(std::string_view owner, std::string_view local_id, std::string &token, std::string &qualified_id) {
        const auto owned = std::ranges::count_if(stack, [owner](const InputContextFrame &frame) {
            return frame.owner == owner;
        });
        if (stack.size() >= 64 || owned >= 16 || next_token == 0) return false;
        token = "input-context-" + std::to_string(next_token++);
        qualified_id = std::string(owner) + ":" + std::string(local_id);
        stack.push_back({token, std::string(owner), qualified_id});
        ClearPresses();
        return true;
    }

    bool Pop(std::string_view owner, std::string_view token, std::string &removed_id) {
        const auto found = std::ranges::find_if(stack, [owner, token](const InputContextFrame &frame) {
            return frame.owner == owner && frame.token == token;
        });
        if (found == stack.end()) return false;
        removed_id = found->id;
        stack.erase(found);
        ClearPresses();
        return true;
    }

    void BindAction(std::string_view action, std::string context) { action_contexts[std::string(action)] = std::move(context); }
    void UnbindAction(std::string_view action) { action_contexts.erase(std::string(action)); }

    void UnloadOwner(std::string_view owner) {
        std::erase_if(stack, [owner](const InputContextFrame &frame) { return frame.owner == owner; });
        const std::string prefix = std::string(owner) + ":";
        auto action = action_contexts.begin();
        while (action != action_contexts.end()) {
            if (std::string_view(action->first).starts_with(prefix)) {
                while (InputModActionConsumePress(action->first)) {}
                action = action_contexts.erase(action);
            } else {
                ++action;
            }
        }
        ClearPresses();
    }

    void ClearPresses() const {
        for (const auto &[action, context] : action_contexts) {
            while (InputModActionConsumePress(action)) {}
        }
    }
};

InputContextRegistry g_input_contexts;

constexpr std::size_t kMaximumRegisteredItems = 32;
constexpr std::size_t kMaximumRegisteredWeapons = 32;
constexpr std::size_t kMaximumRegisteredCharacters = 32;
constexpr std::size_t kMaximumRegisteredModels = 32;
constexpr std::size_t kMaximumRegisteredDungeonFloors = 8;
constexpr std::size_t kMaximumRegisteredGeoramaPieces = 32;
constexpr std::size_t kMaximumRegisteredLocations = 32;
constexpr int kGeoramaAngleSteps = 24;
constexpr int kNativeWeaponTableCapacity = 116 + static_cast<int>(kMaximumRegisteredWeapons);
constexpr int kNativeCommonTableCapacity = ITEM_COMMON_DATA_MAX + static_cast<int>(kMaximumRegisteredItems + kMaximumRegisteredWeapons);
constexpr int kNativeItemTableCapacity = 162 + static_cast<int>(kMaximumRegisteredItems);

struct ModItem {
    std::string id;
    std::string owner;
    std::string name;
    int template_item = 0;
    int max_num = 99;
    int stack_num = 10;
    std::string effect_kind;
    std::string effect_status;
    int effect_amount = 0;
    int effect_flags = 0;
    int native_item_no = -1;
};

struct ModWeapon {
    std::string id;
    std::string owner;
    std::string name;
    int template_item = 0;
    int max_num = 1;
    int stack_num = 1;
    int attack = -1;
    int durability_stat = -1;
    int attack_cap = -1;
    int durability_cap = -1;
    int durability_capacity = -1;
    int levelup_exp = -1;
    std::array<int, 8> attributes{};
    std::array<int, 8> attribute_caps{};
    bool has_attributes = false;
    int native_item_no = -1;
};

struct ModCharacter {
    std::string id;
    std::string owner;
    std::string name;
    std::string model;
    std::string script;
    std::string model_id;
    int template_monster_id = -1;
    int native_monster_id = -1;
    int hp = 0;
    int attack = 0;
    int defense = 0;
    int reward_exp = 0;
    int reward_money = 0;
};

struct ModModel {
    std::string id;
    std::string owner;
    std::string model;
    std::string script;
    int template_monster_id = -1;
    bool custom_script = false;
};

struct ModAnimation {
    std::string id;
    std::string owner;
    std::string model_id;
    int motion_index = -1;
};

struct ModAnimationPlayback {
    std::string owner;
    std::string animation_id;
    std::string previous_motion;
    const CHRINFO_KEY_SET *motion_key = nullptr;
};

struct ModModelReplacement {
    std::string owner;
    std::string original_model;
};

struct ModCharacterSpawn {
    std::string handle;
    std::string character_id;
    std::string owner;
    int slot = -1;
    unsigned int generation = 0;
    int native_monster_id = -1;
};

struct ModDungeonFloor {
    std::string id;
    std::string local_id;
    std::string name;
    int source_floor = -1;
    std::string source_floor_ref;
    std::uint32_t room_flags = 0;
    int seal = -1;
    int native_floor_id = -1;
};

struct ModDungeon {
    std::string id;
    std::string owner;
    std::string name;
    int dungeon = -1;
    std::vector<ModDungeonFloor> floors;
};

struct ModGeoramaPiece {
    std::string id;
    std::string owner;
    std::string name;
    int template_part_id = -1;
    std::array<int, 5> native_part_ids{{-1, -1, -1, -1, -1}};
    std::array<std::string, 5> apply_errors{};
};

struct ModLocation {
    std::string id;
    std::string owner;
    std::string map_name;
    std::string name;
    std::string texture_source_map;
    std::vector<std::string> resources;
    int native_map_id = -1;
    std::string registration_error;
};

std::map<std::string, ModItem> g_items;
std::map<std::string, ModWeapon> g_weapons;
std::map<std::string, ModCharacter> g_characters;
std::map<std::string, ModModel> g_models;
std::map<std::string, ModAnimation> g_animations;
std::map<std::string, ModAnimationPlayback> g_animation_plays;
std::map<int, ModModelReplacement> g_model_replacements;
std::map<std::string, ModCharacterSpawn> g_character_spawns;
std::array<unsigned int, MONSTER_ACTIVE_MAX> g_enemy_defeat_event_generations{};
std::map<std::string, ModDungeon> g_dungeons;
std::map<std::string, ModGeoramaPiece> g_georama_pieces;
std::map<std::string, ModLocation> g_locations;
struct ChronicleTwoDebugOverlay {
    std::string id;
    std::string owner;
    std::string text;
    bool render_layer = false;
    bool render_bar = false;
    std::string label;
    double current = 0.0;
    double maximum = 0.0;
};
std::map<std::string, ChronicleTwoDebugOverlay, std::less<>> g_debug_overlays;
constexpr std::size_t kMaximumDebugOverlays = 4;
constexpr std::size_t kMaximumDebugOverlayText = 48;
constexpr std::size_t kMaximumDebugOverlayLine = 64;
int g_pending_dungeon_entry = -1;
int g_pending_dungeon_entry_floor = -1;

struct ChronicleTwoWeaponBuildupBackup {
    std::array<s16, 3> buildup_weapon{};
};
std::map<int, ChronicleTwoWeaponBuildupBackup> g_original_weapon_buildups;
std::map<int, std::vector<int>> g_mod_weapon_buildups;
std::map<int, std::vector<int>> g_synthesized_weapon_attachments;

struct ChronicleTwoShop {
    int shop_no = 0;
    std::string currency = "gilda";
    std::vector<int> goods;
};
struct ChronicleTwoShopPrice {
    int buy_price = 100;
    int sell_price = 50;
};
bool g_shops_initialized = false;
std::array<ChronicleTwoShop, 34> g_shops{};
std::map<int, ChronicleTwoShopPrice> g_shop_prices;

struct ChronicleTwoFloorRuleOverride {
    int seal = DNGMAP_SEAL_NONE;
    bool spheda = false;
    int fast_destroy_time = 0;
};
std::map<std::pair<int, int>, ChronicleTwoFloorRuleOverride> g_floor_rule_overrides;

struct ChronicleTwoMonsterDropBackup {
    std::array<s16, 3> drop_item{};
};
std::map<int, ChronicleTwoMonsterDropBackup> g_original_monster_drops;

std::array<Json, 5> g_georama_native_templates = [] {
    std::array<Json, 5> result;
    result.fill(Json::array());
    return result;
}();

ApiCallStatus ResolveItemId(std::string_view id, int &item_no, std::string &error);

ApiCallResult Invalid(std::string message) {
    return {ApiCallStatus::InvalidArgument, nullptr, std::move(message)};
}

ApiCallResult GetCharacter(const Json &args);
bool ReadNativeMonsterHandle(std::string_view handle, int &slot, unsigned int &generation);
bool IsSpawnHandleCurrent(const ModCharacterSpawn &record);
bool IsSpawnHandleLive(const ModCharacterSpawn &record);

ApiCallResult Failed(std::string message) {
    return {ApiCallStatus::Failed, nullptr, std::move(message)};
}

bool IsIdentifier(std::string_view value) {
    if (value.empty() || value.size() > 64) {
        return false;
    }
    for (unsigned char c : value) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
              c == '.')) {
            return false;
        }
    }
    return true;
}

bool QualifyOwnerScopedId(std::string_view owner, std::string_view value, std::string &qualified_id) {
    const std::size_t separator = value.find(':');
    if (separator == std::string_view::npos) {
        if (!IsIdentifier(value)) return false;
        qualified_id = std::string(owner) + ":" + std::string(value);
        return true;
    }
    if (value.find(':', separator + 1) != std::string_view::npos || value.substr(0, separator) != owner ||
        !IsIdentifier(value.substr(separator + 1))) {
        return false;
    }
    qualified_id = value;
    return true;
}

bool IsNativeMonsterAssetStem(std::string_view value) {
    if (value.empty() || value.size() > 15 ||
        !((value.front() >= 'a' && value.front() <= 'z') || (value.front() >= '0' && value.front() <= '9'))) {
        return false;
    }
    for (unsigned char c : value) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) {
            return false;
        }
    }
    return true;
}

bool ReadInt(const Json &object, const char *key, int default_value, int minimum, int maximum, int &out) {
    const auto found = object.find(key);
    if (found == object.end()) {
        out = default_value;
        return true;
    }
    if (!found->is_number_integer()) {
        return false;
    }
    if (found->is_number_unsigned()) {
        const unsigned long long value = found->get<unsigned long long>();
        if (maximum < 0 || value > static_cast<unsigned long long>(maximum)) return false;
        out = static_cast<int>(value);
    } else {
        const long long value = found->get<long long>();
        if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()) return false;
        out = static_cast<int>(value);
    }
    return minimum <= out && out <= maximum;
}

bool ReadVector3(const Json &value, std::array<float, 4> &out, float limit) {
    const Json *components[3] = {nullptr, nullptr, nullptr};
    if (value.is_array() && value.size() == 3) {
        components[0] = &value[0];
        components[1] = &value[1];
        components[2] = &value[2];
    } else if (value.is_object() && value.size() == 3 && value.contains("x") && value.contains("y") && value.contains("z")) {
        components[0] = &value["x"];
        components[1] = &value["y"];
        components[2] = &value["z"];
    } else {
        return false;
    }
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (!components[axis]->is_number()) return false;
        const double component = components[axis]->get<double>();
        if (!std::isfinite(component) || component < -limit || component > limit) return false;
        out[axis] = static_cast<float>(component);
    }
    out[3] = 1.0f;
    return true;
}

bool ReadId(const Json &argument, std::string &out) {
    if (argument.is_string()) {
        out = argument.get<std::string>();
        return !out.empty();
    }
    if (argument.is_object()) {
        const auto found = argument.find("id");
        if (found != argument.end() && found->is_string()) {
            out = found->get<std::string>();
            return !out.empty();
        }
    }
    return false;
}

Json ItemJson(const ModItem &item) {
    const Json effect = item.effect_kind == "heal" ? Json{{"kind", item.effect_kind}, {"amount", item.effect_amount}}
                        : item.effect_kind == "cure_status" ? Json{{"kind", item.effect_kind}, {"status", item.effect_status}}
                        : item.effect_kind == "cure_all" ? Json{{"kind", item.effect_kind}}
                        : item.effect_kind == "power" ? Json{{"kind", item.effect_kind}}
                                                          : Json(nullptr);
    return { {"id", item.id},
             {"name", item.name},
             {"template_item", item.template_item},
             {"max_count", item.max_num},
             {"max_stack", item.stack_num},
             {"effect", effect},
             {"native_item_no", item.native_item_no < 0 ? Json(nullptr) : Json(item.native_item_no)},
             {"status", item.native_item_no < 0 ? "pending" : "registered"},
             {"mod_owned", true},
             {"assets", Json::object()} };
}

std::string BoundedString(const char *text, std::size_t capacity);

std::string NativeItemId(int item_no) {
    return "chronicle-two:item:" + std::to_string(item_no);
}

bool NativeItemTablesValid() {
    const CGameData &data = GameItemDataManage;
    return data.common_num <= kNativeCommonTableCapacity &&
           data.item_num <= kNativeItemTableCapacity &&
           (data.common_num == 0 || data.common_data != nullptr) &&
           (data.item_num == 0 || data.item_data != nullptr);
}

bool ReadNativeItemId(const Json &value, int &item_no) {
    if (!value.is_string()) return false;
    const std::string &id = value.get_ref<const std::string &>();
    constexpr std::string_view prefix = "chronicle-two:item:";
    if (!std::string_view(id).starts_with(prefix)) return false;
    const std::string_view suffix = std::string_view(id).substr(prefix.size());
    int parsed = 0;
    const auto [end, error] = std::from_chars(suffix.data(), suffix.data() + suffix.size(), parsed);
    if (error != std::errc() || end != suffix.data() + suffix.size() || parsed <= 0 || parsed >= ITEM_NO_MAX) {
        return false;
    }
    item_no = parsed;
    return true;
}

Json NativeItemJson(const CDataCommon &common, bool include_state) {
    const CGameData &data = GameItemDataManage;
    const int item_family = ConvertUsedItemType(common.type);
    const bool uses_item_table = item_family == USED_ITEM_TYPE_ITEM || item_family == USED_ITEM_TYPE_GIFT_BOX ||
                                 item_family == USED_ITEM_TYPE_BOILED;
    const CDataItem *item_data = uses_item_table && common.list_no >= 0 && common.list_no < data.item_num &&
                                         data.item_data != nullptr
                                     ? data.item_data + common.list_no
                                     : nullptr;
    Json result = {
        {"id", NativeItemId(common.item_no)},
        {"native_item_no", common.item_no},
        {"name", common.name != nullptr ? common.name : ""},
        {"data_type", common.type},
        {"item_family", item_family},
        {"model_key", BoundedString(common.file_name, sizeof(common.file_name))},
        {"icon_no", common.icon_no},
        {"icon_texture_no", common.icon_texture_no},
        {"max_count", common.max_num},
        {"max_stack", common.stack_num},
        {"item_attributes", common.attribute},
        {"usable", (common.attribute & ITEM_ATTRIBUTE_USABLE) != 0},
        {"native_item_data", item_data == nullptr ? Json(nullptr) : Json{
            {"action_flags", item_data->action_flags},
            {"use_flags", item_data->use_flags},
            {"target_flags", item_data->target_flags},
            {"values", {item_data->value[0], item_data->value[1], item_data->value[2]}}
        }},
        {"mod_owned", false},
        {"status", "registered"},
    };
    if (!include_state) return result;
    CUserDataManager *user_data = GetUserDataMan();
    result["owned_count"] = user_data == nullptr ? Json(nullptr) : Json(std::max(0, user_data->GetNumSameItem(common.item_no)));
    return result;
}

Json LiveItemJson(const CDataCommon &common, bool include_state) {
    Json result = NativeItemJson(common, include_state);
    for (const auto &[id, item] : g_items) {
        if (item.native_item_no == common.item_no) {
            result["id"] = id;
            result["mod_owned"] = true;
            result["status"] = "registered";
            result["template_item"] = item.template_item;
            result["effect"] = ItemJson(item).at("effect");
            break;
        }
    }
    return result;
}

Json ModWeaponJson(const ModWeapon &weapon) {
    Json result = { {"id", weapon.id},
                    {"name", weapon.name},
                    {"template_weapon", weapon.template_item},
                    {"max_count", weapon.max_num},
                    {"stack_size", weapon.stack_num},
                    {"native_item_no", weapon.native_item_no < 0 ? Json(nullptr) : Json(weapon.native_item_no)},
                    {"status", weapon.native_item_no < 0 ? "pending" : "registered"},
                    {"assets", Json::object()} };
    return result;
}

bool ReadWeaponAttributeArray(const Json &value, std::array<int, 8> &out) {
    if (!value.is_array() || value.size() != out.size()) return false;
    for (std::size_t index = 0; index < out.size(); ++index) {
        if (!value[index].is_number_integer()) return false;
        const auto number = value[index].get<std::int64_t>();
        if (number < 0 || number > 32767) return false;
        out[index] = static_cast<int>(number);
    }
    return true;
}

Json WeaponJson(const CDataCommon &common, const CDataWeapon &weapon, bool include_state);

Json NativeWeaponJson(const CDataCommon &common, const CDataWeapon &weapon, bool include_state) {
    Json result = WeaponJson(common, weapon, include_state);
    for (const auto &[id, mod_weapon] : g_weapons) {
        if (mod_weapon.native_item_no == common.item_no) {
            result["id"] = id;
            result["mod_owned"] = true;
            result["status"] = "registered";
            break;
        }
    }
    return result;
}

Json DungeonJson(const ModDungeon &dungeon) {
    Json floors = Json::array();
    for (const ModDungeonFloor &floor : dungeon.floors) {
        Json flags = Json::array();
        if (floor.room_flags & DNGMAP_ROOM_FLAG_EXIT) flags.push_back("exit");
        if (floor.room_flags & DNGMAP_ROOM_FLAG_BOSS) flags.push_back("boss");
        if (floor.room_flags & DNGMAP_ROOM_FLAG_SUB) flags.push_back("sub");
        Json seal = nullptr;
        if (floor.seal == DNGMAP_SEAL_MONICA) seal = "monica";
        else if (floor.seal == DNGMAP_SEAL_MAX) seal = "max";
        else if (floor.seal == DNGMAP_SEAL_NONE) seal = "none";
        floors.push_back({{"id", floor.id},
                          {"source_floor", floor.source_floor_ref.empty() ? Json(floor.source_floor)
                                                                           : Json(floor.source_floor_ref)},
                          {"name", floor.name},
                          {"flags", std::move(flags)},
                          {"seal", std::move(seal)},
                          {"native_floor_id", floor.native_floor_id < 0 ? Json(nullptr) : Json(floor.native_floor_id)},
                          {"status", floor.native_floor_id < 0 ? "pending" : "registered"}});
    }
    return {{"id", dungeon.id}, {"name", dungeon.name}, {"dungeon", dungeon.dungeon}, {"floors", std::move(floors)}};
}

Json GeoramaPieceJson(const ModGeoramaPiece &piece, int town_id = -1) {
    const bool town_known = town_id >= 0 && town_id < static_cast<int>(piece.native_part_ids.size());
    const int native_part_id = town_known ? piece.native_part_ids[static_cast<std::size_t>(town_id)] : -1;
    const std::string reason = town_known ? piece.apply_errors[static_cast<std::size_t>(town_id)] : std::string();
    return {{"id", piece.id},
            {"name", piece.name},
            {"template_part_id", piece.template_part_id},
            {"native_part_id", native_part_id < 0 ? Json(nullptr) : Json(native_part_id)},
            {"status", native_part_id >= 0 ? "registered" : (reason.empty() ? "pending" : "failed")},
            {"reason", reason}};
}

std::string BoundedString(const char *text, std::size_t capacity) {
    const char *end = std::find(text, text + capacity, '\0');
    return std::string(text, end);
}

std::string NativeWeaponId(int item_no) {
    return "chronicle-two:weapon:" + std::to_string(item_no);
}

bool NativeWeaponTablesValid() {
    const CGameData &data = GameItemDataManage;
    return data.common_num <= kNativeCommonTableCapacity && data.weapon_num <= kNativeWeaponTableCapacity &&
           (data.common_num == 0 || data.common_data != nullptr) &&
           (data.weapon_num == 0 || data.weapon_data != nullptr);
}

bool IsWeaponCommon(const CDataCommon &common) {
    return ConvertUsedItemType(common.type) == USED_ITEM_TYPE_WEAPON;
}

const CDataWeapon *WeaponDataFor(const CDataCommon &common) {
    if (!IsWeaponCommon(common) || common.list_no < 0 || common.list_no >= GameItemDataManage.weapon_num ||
        GameItemDataManage.weapon_data == nullptr) {
        return nullptr;
    }
    return GameItemDataManage.weapon_data + common.list_no;
}

const CDataCommon *FindWeaponCommon(int item_no) {
    const CGameData &data = GameItemDataManage;
    for (int index = 0; index < data.common_num; ++index) {
        const CDataCommon &common = data.common_data[index];
        if (common.item_no == item_no && IsWeaponCommon(common)) return &common;
    }
    return nullptr;
}

CGameDataUsed *FindOwnedWeapon(CUserDataManager &user_data, int item_no) {
    CGameDataUsed *owned = user_data.SearchAllHaveItem(item_no);
    if (owned != nullptr && owned->used_type == USED_ITEM_TYPE_WEAPON) return owned;
    for (int character = 0; character < USER_CHARA_ROBO; ++character) {
        for (CGameDataUsed &equipment : user_data.chara_data[character].equip) {
            if (equipment.item_no == item_no && equipment.used_type == USED_ITEM_TYPE_WEAPON) return &equipment;
        }
    }
    return nullptr;
}

Json WeaponJson(const CDataCommon &common, const CDataWeapon &weapon, bool include_state) {
    Json attributes = Json::array();
    Json attribute_caps = Json::array();
    for (int index = 0; index < 8; ++index) {
        attributes.push_back(weapon.attribute[index]);
        attribute_caps.push_back(weapon.attribute_max[index]);
    }
    Json buildup_weapons = Json::array();
    Json buildup_monsters = Json::array();
    for (int index = 0; index < 3; ++index) {
        if (weapon.buildup_weapon[index] > 0) buildup_weapons.push_back(weapon.buildup_weapon[index]);
        if (weapon.buildup_monster[index] >= 0) buildup_monsters.push_back(weapon.buildup_monster[index]);
    }
    Json result = {
        {"id", NativeWeaponId(common.item_no)},
        {"native_item_no", common.item_no},
        {"name", common.name != nullptr ? common.name : ""},
        {"data_type", common.type},
        {"model_key", BoundedString(common.file_name, sizeof(common.file_name))},
        {"icon_no", common.icon_no},
        {"max_count", common.max_num},
        {"stack_size", common.stack_num},
        {"item_attributes", common.attribute},
        {"durability_capacity", weapon.durability},
        {"level_up_exp", weapon.levelup_exp},
        {"stats", {{"attack", weapon.status[0]}, {"durability", weapon.status[1]}, {"attributes", attributes}}},
        {"stat_caps", {{"attack", weapon.status_max[0]}, {"durability", weapon.status_max[1]},
                        {"attributes", attribute_caps}}},
        {"special_mask", weapon.special},
        {"initial_fusion_points", weapon.initial_fusion_point},
        {"fusion_points_per_level", weapon.fusion_point},
        {"builds_into", std::move(buildup_weapons)},
        {"required_monster_defeats", std::move(buildup_monsters)},
        {"attack_type", weapon.attack_type},
        {"model_no", weapon.model_no},
        {"palette", weapon.pallet_color},
    };
    if (!include_state) return result;

    result["owned_count"] = nullptr;
    result["state"] = nullptr;
    CUserDataManager *user_data = GetUserDataMan();
    if (user_data == nullptr) return result;
    result["owned_count"] = std::max(0, user_data->GetNumSameItem(common.item_no));
    CGameDataUsed *owned = FindOwnedWeapon(*user_data, common.item_no);
    if (owned == nullptr || owned->item_no != common.item_no) return result;
    const WEAPON_USED &state = owned->data.weapon;
    Json current_stats = Json::array();
    for (s16 value : state.status) current_stats.push_back(value);
    Json current_attributes = Json::array();
    for (s16 value : state.attribute) current_attributes.push_back(value);
    const std::string owned_name = BoundedString(state.name, sizeof(state.name));
    result["state"] = {
        {"level", state.level},
        {"durability", {{"current", state.whp.now}, {"maximum", state.whp.max}}},
        {"absorption", {{"current", state.abs.now}, {"maximum", state.abs.max}}},
        {"stats", std::move(current_stats)},
        {"attributes", std::move(current_attributes)},
        {"special_mask", state.special},
        {"fusion_points", state.fusion_point},
        {"custom_name", owned_name.empty() ? Json(nullptr) : Json(owned_name)},
    };
    return result;
}

bool ReadNativeWeaponId(const Json &value, int &item_no) {
    if (value.is_number_unsigned()) {
        const auto number = value.get<Json::number_unsigned_t>();
        if (number == 0 || number >= ITEM_NO_MAX) return false;
        item_no = static_cast<int>(number);
        return true;
    }
    if (value.is_number_integer()) {
        const auto number = value.get<std::int64_t>();
        if (number <= 0 || number >= ITEM_NO_MAX) return false;
        item_no = static_cast<int>(number);
        return true;
    }
    if (!value.is_string()) return false;
    const std::string &id = value.get_ref<const std::string &>();
    constexpr std::string_view prefix = "chronicle-two:weapon:";
    if (!std::string_view(id).starts_with(prefix)) return false;
    const std::string_view suffix = std::string_view(id).substr(prefix.size());
    int parsed = 0;
    const auto [end, error] = std::from_chars(suffix.data(), suffix.data() + suffix.size(), parsed);
    if (error != std::errc() || end != suffix.data() + suffix.size() || parsed <= 0 || parsed >= ITEM_NO_MAX) {
        return false;
    }
    item_no = parsed;
    return true;
}

bool ReadWeaponSelector(const Json &selector, int &item_no) {
    if (selector.is_number_integer() || selector.is_string()) return ReadNativeWeaponId(selector, item_no);
    if (!selector.is_object()) return false;
    const auto id = selector.find("id");
    const auto native_no = selector.find("item_no");
    if (id == selector.end() && native_no == selector.end()) return false;
    int id_no = 0;
    int native_item_no = 0;
    if (id != selector.end() && !ReadNativeWeaponId(*id, id_no)) return false;
    if (native_no != selector.end() && !ReadNativeWeaponId(*native_no, native_item_no)) return false;
    if (id != selector.end() && native_no != selector.end() && id_no != native_item_no) return false;
    item_no = id != selector.end() ? id_no : native_item_no;
    return true;
}

ApiCallResult ListWeapons(const Json &args) {
    if (!args.is_array() || !args.empty()) return Invalid("weapons.list takes no arguments");
    if (!NativeWeaponTablesValid()) return Failed("the native weapon tables are unavailable or out of range");
    Json result = Json::array();
    const CGameData &data = GameItemDataManage;
    for (int index = 0; index < data.common_num; ++index) {
        const CDataCommon &common = data.common_data[index];
        if (!IsWeaponCommon(common)) continue;
        const CDataWeapon *weapon = WeaponDataFor(common);
        if (weapon == nullptr) return Failed("a native weapon definition has an invalid data-table index");
        result.push_back(NativeWeaponJson(common, *weapon, false));
    }
    return {ApiCallStatus::Ok, std::move(result), {}};
}

ApiCallResult GetWeapon(const Json &args) {
    if (!args.is_array() || args.size() != 1) return Invalid("weapons.get expects one native weapon id or selector");
    int item_no = 0;
    const Json *stable_id = nullptr;
    const Json *native_item_no = nullptr;
    if (args[0].is_string()) {
        stable_id = &args[0];
    } else if (args[0].is_object()) {
        const auto id = args[0].find("id");
        const auto number = args[0].find("item_no");
        if (id != args[0].end()) stable_id = &*id;
        if (number != args[0].end()) native_item_no = &*number;
    }
    const ModWeapon *mod_weapon = nullptr;
    if (stable_id != nullptr && stable_id->is_string()) {
        const auto found = g_weapons.find(stable_id->get<std::string>());
        if (found != g_weapons.end()) {
            mod_weapon = &found->second;
            if (mod_weapon->native_item_no < 0) {
                return {ApiCallStatus::PhaseViolation, nullptr, "weapon registration is waiting for the game weapon tables"};
            }
            item_no = mod_weapon->native_item_no;
            int selected_item_no = 0;
            if (native_item_no != nullptr &&
                (!ReadNativeWeaponId(*native_item_no, selected_item_no) || selected_item_no != item_no)) {
                return Invalid("id and item_no must identify the same registered weapon");
            }
        }
    }
    if (mod_weapon == nullptr && !ReadWeaponSelector(args[0], item_no)) {
        return Invalid("expected a stable mod weapon id, native item number or {id = stable_id, item_no = optional_number}");
    }
    if (!NativeWeaponTablesValid()) return Failed("the native weapon tables are unavailable or out of range");
    const CDataCommon *common = FindWeaponCommon(item_no);
    if (common == nullptr) return Failed("no native weapon has that item number");
    const CDataWeapon *weapon = WeaponDataFor(*common);
    if (weapon == nullptr) return Failed("the native weapon definition has an invalid data-table index");
    return {ApiCallStatus::Ok, NativeWeaponJson(*common, *weapon, true), {}};
}

const char *PlayerCharacterName(int character_id) {
    switch (character_id) {
        case USER_CHARA_MAX: return "max";
        case USER_CHARA_MONICA: return "monica";
        case USER_CHARA_ROBO: return "ridepod";
        case USER_CHARA_MONSTER: return "monster";
        default: return nullptr;
    }
}

ApiCallResult EquipWeapon(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) {
        return Invalid("expected {id = weapon_id, character = optional 'max' or 'monica'}");
    }

    const Json &selector = args[0];
    int character_id = -1;
    const auto character_field = selector.find("character");
    if (character_field == selector.end()) {
        CUserDataManager *user_data = GetUserDataMan();
        if (user_data == nullptr) return Failed("the game has no active save data");
        character_id = user_data->GetActiveChrNo();
    } else if (character_field->is_string() && character_field->get_ref<const std::string &>() == "max") {
        character_id = USER_CHARA_MAX;
    } else if (character_field->is_string() && character_field->get_ref<const std::string &>() == "monica") {
        character_id = USER_CHARA_MONICA;
    } else {
        return Invalid("character must be 'max' or 'monica'");
    }
    if (character_id != USER_CHARA_MAX && character_id != USER_CHARA_MONICA) {
        return {ApiCallStatus::Failed, nullptr,
                "weapons.equip requires Max or Monica to be the active character, or an explicit character id"};
    }

    const ApiCallResult definition = GetWeapon(args);
    if (definition.status != ApiCallStatus::Ok) return definition;
    const int item_no = definition.value.value("native_item_no", 0);
    if (item_no <= 0 || FindWeaponCommon(item_no) == nullptr) {
        return Failed("weapon definition is unavailable in the native weapon tables");
    }
    CUserDataManager *user_data = GetUserDataMan();
    if (user_data == nullptr) return Failed("the game has no active save data");
    if (!user_data->SetChrEquip(character_id, item_no) || user_data->SearchEquip(character_id, item_no) == nullptr) {
        return Failed("weapon is not in the inventory, is already equipped, or is incompatible with that character");
    }
    return {ApiCallStatus::Ok,
            {{"id", definition.value.value("id", NativeWeaponId(item_no))},
             {"native_item_no", item_no}, {"character", PlayerCharacterName(character_id)},
             {"equipped", true}, {"changed", true}},
            {}};
}

ApiCallResult UpgradeWeapon(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) {
        return Invalid("expected {id = owned_weapon_id}");
    }

    const ApiCallResult definition = GetWeapon(args);
    if (definition.status != ApiCallStatus::Ok) return definition;
    const int item_no = definition.value.value("native_item_no", 0);
    if (item_no <= 0) return Failed("weapon definition is unavailable in the native weapon tables");
    CUserDataManager *user_data = GetUserDataMan();
    if (user_data == nullptr) return Failed("the game has no active save data");
    CGameDataUsed *owned = FindOwnedWeapon(*user_data, item_no);
    if (owned == nullptr || owned->used_type != USED_ITEM_TYPE_WEAPON || owned->item_no != item_no) {
        return Failed("weapon is not owned by the active save");
    }

    const std::string id = definition.value.value("id", NativeWeaponId(item_no));
    const int previous_level = owned->data.weapon.level;
    if (!owned->IsLevelUp()) {
        return {ApiCallStatus::Ok,
                {{"id", id}, {"native_item_no", item_no}, {"level", previous_level},
                 {"upgraded", false}, {"changed", false}, {"ready", false}},
                {}};
    }

    owned->LevelUp();
    const int level = owned->data.weapon.level;
    if (level != previous_level + 1) {
        return Failed("the native weapon progression routine did not advance the owned weapon");
    }
    const WEAPON_USED &state = owned->data.weapon;
    return {ApiCallStatus::Ok,
            {{"id", id}, {"native_item_no", item_no}, {"previous_level", previous_level}, {"level", level},
             {"upgraded", true}, {"changed", true}, {"ready", true}, {"absorption", state.abs.now},
             {"durability", {{"current", state.whp.now}, {"maximum", state.whp.max}}},
             {"stats", {{"attack", state.status[WEAPON_STAT_ATTACK]},
                         {"durability", state.status[WEAPON_STAT_DURABILITY]}}},
             {"fusion_points", state.fusion_point}},
            {}};
}

COMMON_GAGE *ActivePlayerGauge(CUserDataManager *user_data, int &character_id) {
    character_id = user_data->GetActiveChrNo();
    if (character_id == USER_CHARA_MONSTER) {
        MOS_CHANGE_PARAM *monster = user_data->GetMonsterBajjiDataPtrMosId(user_data->GetMonsterID());
        return monster == nullptr ? nullptr : &monster->hp;
    }
    if (character_id >= USER_CHARA_MAX && character_id <= USER_CHARA_ROBO) {
        return user_data->GetCharaHpGage(character_id);
    }
    return nullptr;
}

Json CharacterDefinitionJson(CUserDataManager &user_data, int character_id, int active_character_id, int party_mask) {
    const char *name = PlayerCharacterName(character_id);
    Json result = {{"id", name},
                   {"character_id", character_id},
                   {"kind", character_id == USER_CHARA_ROBO ? "vehicle"
                            : character_id == USER_CHARA_MONSTER ? "transformation"
                                                                 : "playable"},
                   {"in_party", (party_mask & (1 << character_id)) != 0},
                   {"active", character_id == active_character_id}};
    COMMON_GAGE *hp = nullptr;
    if (character_id == USER_CHARA_MONSTER) {
        if (active_character_id == USER_CHARA_MONSTER) {
            int active_id = -1;
            hp = ActivePlayerGauge(&user_data, active_id);
        }
    } else {
        hp = user_data.GetCharaHpGage(character_id);
    }
    if (hp != nullptr) {
        result["hp"] = hp->now;
        result["max_hp"] = hp->max;
    }
    return result;
}

Json ModCharacterJson(const ModCharacter &character) {
    Json result = {{"id", character.id}, {"kind", "enemy"}, {"name", character.name},
                   {"template_monster_id", character.template_monster_id},
                   {"native_monster_id", character.native_monster_id}, {"status", "registered"},
                   {"mod_owned", true}, {"model", character.model}, {"script", character.script},
                   {"stats", {{"hp", character.hp}, {"attack", character.attack}, {"defense", character.defense},
                              {"reward_exp", character.reward_exp}, {"reward_money", character.reward_money}}},
                   {"assets", Json::object()}};
    if (!character.model_id.empty()) result["model_id"] = character.model_id;
    return result;
}

std::string NativeMonsterAssetPath(std::string_view stem, std::string_view extension) {
    return "dungeon/monster/" + std::string(stem) + "." + std::string(extension);
}

bool NativeMonsterAssetReady(std::string_view owner, std::string_view game_path) {
    const auto file = FindNativeResourceOverride(game_path);
    if (!file || !NativeResourceOverrideOwnedBy(owner, game_path)) return false;
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(*file, error);
    return !error && size > 0 && size <= 3u * 1024u * 1024u;
}

bool NativeMonsterModelNameInUse(std::string_view model_name) {
    for (int index = 0; index < 344; ++index) {
        const BASE_MONSTER_TBL &entry = base_monster_define[index];
        if (entry.name[0] != '\0' && BoundedString(entry.model, sizeof(entry.model)) == model_name) return true;
    }
    for (const auto &[id, character] : g_characters) {
        (void)id;
        if (character.model == model_name) return true;
    }
    return std::ranges::any_of(g_models, [model_name](const auto &entry) {
        return entry.second.model == model_name;
    });
}

Json ModModelJson(const ModModel &model) {
    const std::string model_path = NativeMonsterAssetPath(model.model, "chr");
    const std::string script_path = NativeMonsterAssetPath(model.script, "stb");
    const bool model_owned = NativeResourceOverrideOwnedBy(model.owner, model_path);
    const bool script_overridden = FindNativeResourceOverride(script_path).has_value();
    return {{"id", model.id}, {"owner", model.owner}, {"model", model.model}, {"script", model.script},
            {"template_monster_id", model.template_monster_id},
            {"status", model_owned ? "registered" : "unavailable"},
            {"reason", model_owned ? "" : "the owning mod's model override is not active"},
            {"native_monster_id", nullptr}, {"source", model_owned ? "package_override" : "missing"},
            {"assets", Json::array({{{"slot", "model"}, {"game_path", model_path}, {"overridden", model_owned}},
                                    {{"slot", "script"}, {"game_path", script_path},
                                     {"overridden", script_overridden}}})}};
}

ApiCallResult RegisterModel(std::string_view mod_id, const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) {
        return Invalid("expected {id, template_monster_id, model, and optional script}");
    }
    const Json &definition = args[0];
    for (auto field = definition.begin(); field != definition.end(); ++field) {
        if (field.key() != "id" && field.key() != "template_monster_id" && field.key() != "model" &&
            field.key() != "script") {
            return Invalid("model definitions only support id, template_monster_id, model, and script");
        }
    }
    const auto id_field = definition.find("id");
    const auto model_field = definition.find("model");
    if (id_field == definition.end() || !id_field->is_string() || model_field == definition.end() ||
        !model_field->is_string()) {
        return Invalid("model definition requires string id and model fields");
    }
    const std::string local_id = id_field->get<std::string>();
    std::string qualified_id;
    const std::size_t separator = local_id.find(':');
    if (separator == std::string::npos) {
        if (!IsIdentifier(local_id)) return Invalid("model id must use letters, digits, '.', '_' or '-'");
        qualified_id = std::string(mod_id) + ":" + local_id;
    } else {
        if (local_id.find(':', separator + 1) != std::string::npos || local_id.substr(0, separator) != mod_id ||
            !IsIdentifier(std::string_view(local_id).substr(separator + 1))) {
            return Invalid("fully qualified model ids must use this mod's namespace");
        }
        qualified_id = local_id;
    }
    const std::string &model_name = model_field->get_ref<const std::string &>();
    if (!IsNativeMonsterAssetStem(model_name)) {
        return Invalid("model must be a lowercase native monster asset stem of 1-15 safe characters");
    }
    int template_id = -1;
    if (!ReadInt(definition, "template_monster_id", -1, 0, 32767, template_id)) {
        return Invalid("template_monster_id must name an existing native monster definition");
    }
    BASE_MONSTER_TBL *template_definition = GetMonsterTable(template_id);
    if (template_definition == nullptr) return Invalid("template_monster_id does not identify a native monster");
    const std::string inherited_script = BoundedString(template_definition->script, sizeof(template_definition->script));
    const auto script_field = definition.find("script");
    const bool custom_script = script_field != definition.end();
    if (custom_script && !script_field->is_string()) return Invalid("script must be a native monster asset stem string");
    const std::string script_name = custom_script ? script_field->get<std::string>() : inherited_script;
    if (!IsNativeMonsterAssetStem(script_name)) {
        return Invalid("script must be a lowercase native monster asset stem of 1-15 safe characters");
    }
    if (g_models.contains(qualified_id)) return Invalid("a model with this stable id is already registered");
    if (g_models.size() >= kMaximumRegisteredModels) {
        return Failed("Chronicle Two currently supports up to 32 registered custom model bindings");
    }
    if (NativeMonsterModelNameInUse(model_name)) {
        return Invalid("model asset stem is already used by a native or registered model; additions need a new stem");
    }
    const std::string model_path = NativeMonsterAssetPath(model_name, "chr");
    if (!NativeResourceOverrideOwnedBy(mod_id, model_path)) {
        return Failed("models.register requires this mod to own a resources.override for " + model_path);
    }
    if (custom_script) {
        const std::string script_path = NativeMonsterAssetPath(script_name, "stb");
        if (!NativeResourceOverrideOwnedBy(mod_id, script_path)) {
            return Failed("a custom script requires this mod to own a resources.override for " + script_path);
        }
    }
    ModModel model;
    model.id = qualified_id;
    model.owner = mod_id;
    model.model = model_name;
    model.script = script_name;
    model.template_monster_id = template_id;
    model.custom_script = custom_script;
    const std::string id = model.id;
    g_models.emplace(id, std::move(model));
    return {ApiCallStatus::Ok, ModModelJson(g_models.at(id)), {}};
}

ApiCallResult UnregisterModel(std::string_view mod_id, const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || args[0].size() != 1 ||
        !args[0].contains("id") || !args[0]["id"].is_string()) {
        return Invalid("expected {id = local_model_id}");
    }
    const std::string local_id = args[0]["id"].get<std::string>();
    std::string id;
    const std::size_t separator = local_id.find(':');
    if (separator == std::string::npos) {
        if (!IsIdentifier(local_id)) return Invalid("id must use letters, digits, '.', '_' or '-'");
        id = std::string(mod_id) + ":" + local_id;
    } else {
        if (local_id.find(':', separator + 1) != std::string::npos || local_id.substr(0, separator) != mod_id ||
            !IsIdentifier(std::string_view(local_id).substr(separator + 1))) {
            return Invalid("fully qualified model ids must use this mod's namespace");
        }
        id = local_id;
    }
    const auto found = g_models.find(id);
    if (found == g_models.end() || found->second.owner != mod_id) {
        return Invalid("model binding is missing or owned by another mod");
    }
    if (std::ranges::any_of(g_characters, [&id](const auto &entry) {
            return entry.second.model_id == id;
        })) {
        return Failed("a registered character uses this model binding and must be removed first");
    }
    if (std::ranges::any_of(g_animations, [&id](const auto &entry) {
            return entry.second.model_id == id;
        })) {
        return Failed("a registered embedded-motion definition uses this model binding and must be removed first");
    }
    g_models.erase(found);
    return {ApiCallStatus::Ok, {{"id", id}, {"type", "model"}, {"removed", true}}, {}};
}

bool ChronicleTwoNativeMonsterModelLoaded(int model_id) {
    if (GetNowLoopNo() != LOOP_DUNGEON || DngSaveData == nullptr || ActiveMonster == nullptr ||
        DngMainScene == nullptr || ActiveMonster->GetScene() != DngMainScene || DngStatus.mode == DNG_STATUS_EXIT) {
        return false;
    }
    const int refer_index = ActiveMonster->SearchBaseIndex(model_id);
    return refer_index >= 0 && ActiveMonster->refer[refer_index].chara.GetFrame() != nullptr;
}

bool SameNativeResourcePath(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        unsigned char left_char = static_cast<unsigned char>(left[index]);
        unsigned char right_char = static_cast<unsigned char>(right[index]);
        if (left_char == '\\') left_char = '/';
        if (right_char == '\\') right_char = '/';
        if (std::tolower(left_char) != std::tolower(right_char)) return false;
    }
    return true;
}

std::string ChronicleTwoModelOverrideRemovalGuard(std::string_view owner, const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || !args[0].contains("game_path") ||
        !args[0]["game_path"].is_string()) {
        return {};
    }
    const std::string &requested_path = args[0]["game_path"].get_ref<const std::string &>();
    for (const auto &[id, model] : g_models) {
        (void)id;
        if (model.owner != owner) continue;
        if (SameNativeResourcePath(requested_path, NativeMonsterAssetPath(model.model, "chr")) ||
            (model.custom_script &&
             SameNativeResourcePath(requested_path, NativeMonsterAssetPath(model.script, "stb")))) {
            return "resources.unoverride cannot remove an asset referenced by a registered model; model definitions remain registered until the game process exits";
        }
    }
    for (const auto &[model_id, replacement] : g_model_replacements) {
        if (replacement.owner != owner) continue;
        const BASE_MONSTER_TBL *definition = GetMonsterTable(model_id);
        if (definition == nullptr) continue;
        const std::string model_name = BoundedString(definition->model, sizeof(definition->model));
        if (model_name.empty()) continue;
        if (SameNativeResourcePath(requested_path, NativeMonsterAssetPath(model_name, "chr"))) {
            return "resources.unoverride cannot remove the .chr asset used by an active native model replacement; call models.remove first";
        }
    }
    return {};
}

void RestoreChronicleTwoModelReplacements(std::string_view owner = {}) {
    for (auto replacement = g_model_replacements.begin(); replacement != g_model_replacements.end();) {
        if (!owner.empty() && replacement->second.owner != owner) {
            ++replacement;
            continue;
        }
        if (BASE_MONSTER_TBL *definition = GetMonsterTable(replacement->first); definition != nullptr) {
            std::memset(definition->model, 0, sizeof(definition->model));
            std::memcpy(definition->model, replacement->second.original_model.data(),
                        replacement->second.original_model.size());
        }
        replacement = g_model_replacements.erase(replacement);
    }
}

ApiCallResult ReplaceNativeModel(std::string_view mod_id, const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || args[0].size() != 2 ||
        !args[0].contains("monster_id") || !args[0].contains("model") || !args[0]["model"].is_string()) {
        return Invalid("expected {monster_id = native monster id, model = owned replacement model stem}");
    }
    int model_id = -1;
    if (!ReadInt(args[0], "monster_id", model_id, 0, 343, model_id)) {
        return Invalid("monster_id must identify a native Chronicle Two model from 0 to 343");
    }
    const std::string &replacement_model = args[0]["model"].get_ref<const std::string &>();
    if (!IsNativeMonsterAssetStem(replacement_model)) {
        return Invalid("model must be a lowercase native monster asset stem of 1-15 safe characters");
    }
    const std::string replacement_path = NativeMonsterAssetPath(replacement_model, "chr");
    if (!NativeMonsterAssetReady(mod_id, replacement_path)) {
        return Failed("models.replace requires this mod to own a 1 byte through 3 MiB resources.override for " +
                      replacement_path);
    }
    if (ChronicleTwoNativeMonsterModelLoaded(model_id)) {
        return Failed("the native monster model is loaded on the current floor; replace it after leaving that floor");
    }
    BASE_MONSTER_TBL *definition = GetMonsterTable(model_id);
    if (definition == nullptr || definition->name[0] == '\0') {
        return Invalid("monster_id does not identify a native Chronicle Two model");
    }
    const std::string current_model = BoundedString(definition->model, sizeof(definition->model));
    auto replacement = g_model_replacements.find(model_id);
    if (replacement != g_model_replacements.end() && replacement->second.owner != mod_id) {
        return {ApiCallStatus::PermissionDenied, nullptr,
                "another mod already owns the replacement binding for this native model"};
    }
    if (current_model == replacement_model) {
        return Invalid("replacement model stem must differ from the current native model; use models.remove to restore it");
    }
    if (replacement == g_model_replacements.end()) {
        g_model_replacements.emplace(model_id, ModModelReplacement{std::string(mod_id), current_model});
    }
    std::memset(definition->model, 0, sizeof(definition->model));
    std::memcpy(definition->model, replacement_model.data(), replacement_model.size());
    return {ApiCallStatus::Ok,
            {{"monster_id", model_id}, {"previous_model", current_model}, {"model", replacement_model},
             {"game_path", replacement_path}, {"changed", true}},
            {}};
}

ApiCallResult RemoveNativeModelReplacement(std::string_view mod_id, const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || args[0].size() != 1 ||
        !args[0].contains("monster_id")) {
        return Invalid("expected {monster_id = native Chronicle Two model id}");
    }
    int model_id = -1;
    if (!ReadInt(args[0], "monster_id", model_id, 0, 343, model_id)) {
        return Invalid("monster_id must identify a native Chronicle Two model from 0 to 343");
    }
    const auto replacement = g_model_replacements.find(model_id);
    if (replacement == g_model_replacements.end()) return Failed("this native model has no active MoonMod replacement");
    if (replacement->second.owner != mod_id) {
        return {ApiCallStatus::PermissionDenied, nullptr,
                "a mod can only remove its own native model replacement"};
    }
    if (ChronicleTwoNativeMonsterModelLoaded(model_id)) {
        return Failed("the native monster model is loaded on the current floor; remove it after leaving that floor");
    }
    BASE_MONSTER_TBL *definition = GetMonsterTable(model_id);
    if (definition == nullptr) return Failed("the native model table entry is no longer available");
    const std::string previous_model = BoundedString(definition->model, sizeof(definition->model));
    const std::string restored_model = replacement->second.original_model;
    std::memset(definition->model, 0, sizeof(definition->model));
    std::memcpy(definition->model, restored_model.data(), restored_model.size());
    g_model_replacements.erase(replacement);
    return {ApiCallStatus::Ok,
            {{"monster_id", model_id}, {"previous_model", previous_model}, {"model", restored_model},
             {"removed", true}, {"changed", true}},
            {}};
}

Json ModAnimationJson(const ModAnimation &animation) {
    return {{"id", animation.id}, {"model", animation.model_id}, {"motion_index", animation.motion_index},
            {"status", "registered"}, {"motion_validation", "deferred_until_model_is_loaded"}};
}

ApiCallResult RegisterAnimation(std::string_view mod_id, const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || args[0].size() != 3 ||
        !args[0].contains("id") || !args[0]["id"].is_string() || !args[0].contains("model") ||
        !args[0]["model"].is_string() || !args[0].contains("motion_index")) {
        return Invalid("expected {id = local animation id, model = owned registered model id, motion_index = 0-255}");
    }
    std::string animation_id;
    std::string model_id;
    if (!QualifyOwnerScopedId(mod_id, args[0]["id"].get_ref<const std::string &>(), animation_id)) {
        return Invalid("animation id must be a stable identifier in this mod's namespace");
    }
    if (!QualifyOwnerScopedId(mod_id, args[0]["model"].get_ref<const std::string &>(), model_id)) {
        return Invalid("model must identify a registered model owned by this mod");
    }
    int motion_index = -1;
    if (!ReadInt(args[0], "motion_index", -1, 0, 255, motion_index)) {
        return Invalid("motion_index must be an integer from 0 to 255");
    }
    const auto model = g_models.find(model_id);
    if (model == g_models.end()) return Invalid("model does not identify a registered custom model");
    if (model->second.owner != mod_id) {
        return {ApiCallStatus::PermissionDenied, nullptr, "a mod can only bind animations to its own model"};
    }
    const std::string model_path = NativeMonsterAssetPath(model->second.model, "chr");
    if (!NativeResourceOverrideOwnedBy(mod_id, model_path)) {
        return Failed("the selected custom model's native .chr override is not active");
    }
    if (g_animations.contains(animation_id)) return Invalid("an animation with this stable id is already registered");
    if (g_animations.size() >= 256) return Failed("the runtime has reached its 256 registered animation limit");
    ModAnimation animation{animation_id, std::string(mod_id), model_id, motion_index};
    g_animations.emplace(animation_id, std::move(animation));
    return {ApiCallStatus::Ok, ModAnimationJson(g_animations.at(animation_id)), {}};
}

ApiCallResult PlayAnimation(std::string_view mod_id, const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || args[0].size() != 2 ||
        !args[0].contains("animation") || !args[0].contains("entity") || !args[0]["animation"].is_string()) {
        return Invalid("expected {animation = registered animation id, entity = owned live entity handle}");
    }
    std::string animation_id;
    if (!QualifyOwnerScopedId(mod_id, args[0]["animation"].get_ref<const std::string &>(), animation_id)) {
        return Invalid("animation must identify an animation registered by this mod");
    }
    std::string handle;
    if (!ReadId(args[0]["entity"], handle)) return Invalid("entity must be a generation-checked handle");
    const auto animation = g_animations.find(animation_id);
    if (animation == g_animations.end()) return Invalid("animation is not registered by this mod");
    if (animation->second.owner != mod_id) {
        return {ApiCallStatus::PermissionDenied, nullptr, "a mod can only play its own registered animations"};
    }
    const auto spawn = g_character_spawns.find(handle);
    if (spawn == g_character_spawns.end()) {
        return Failed("animations.play only supports mod-owned enemies created by characters.spawn");
    }
    const ModCharacterSpawn &record = spawn->second;
    if (record.owner != mod_id) return {ApiCallStatus::PermissionDenied, nullptr, "a mod can only animate entities that it spawned"};
    if (!IsSpawnHandleCurrent(record)) return Failed("the entity handle is stale because its monster slot was reused");
    if (!IsSpawnHandleLive(record)) return Failed("animations.play requires a living entity");
    const auto character = g_characters.find(record.character_id);
    if (character == g_characters.end() || character->second.owner != mod_id ||
        character->second.model_id != animation->second.model_id) {
        return Failed("the spawned character does not use the model bound to this animation");
    }
    CActiveMonster *monster = ActiveMonster->GetActiveMonster(record.slot);
    if (monster == nullptr || monster->GetFrame() == nullptr) return Failed("the entity's native model is not loaded");
    CHRINFO_KEY_SET *key = monster->GetKeyListIndexPtr(animation->second.motion_index, nullptr);
    if (key == nullptr || key->name[0] == '\0') {
        return Failed("the loaded model does not contain this registered motion_index");
    }
    int chain_count = 0;
    for (CActionChara *part = monster; part != nullptr; part = part->next) {
        if (++chain_count > 16) return Failed("the native character part chain is invalid");
        if (part->GetKeyListIndexPtr(animation->second.motion_index, nullptr) == nullptr) {
            return Failed("a linked model part does not contain this motion_index");
        }
    }
    std::string previous_motion;
    const char *previous_name = monster->GetNowMotionName();
    if (previous_name != nullptr) previous_motion = BoundedString(previous_name, sizeof(key->name));
    const auto previous_playback = g_animation_plays.find(handle);
    if (previous_playback != g_animation_plays.end() && previous_playback->second.owner == mod_id &&
        (monster->now_key == previous_playback->second.motion_key ||
         monster->next_key == previous_playback->second.motion_key)) {
        previous_motion = previous_playback->second.previous_motion;
    }
    monster->SetMotion(animation->second.motion_index, CHARA_MOTION_RESTART);
    g_animation_plays[handle] = ModAnimationPlayback{std::string(mod_id), animation_id, previous_motion, key};
    return {ApiCallStatus::Ok,
            {{"animation", animation_id}, {"entity", handle}, {"motion_index", animation->second.motion_index},
             {"has_previous_motion", !previous_motion.empty()}, {"status", "playing"}},
            {}};
}

ApiCallResult StopAnimation(std::string_view mod_id, const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || args[0].size() != 1 ||
        !args[0].contains("entity")) {
        return Invalid("expected {entity = handle with a mod-started animation}");
    }
    std::string handle;
    if (!ReadId(args[0]["entity"], handle)) return Invalid("entity must be a generation-checked handle");
    const auto playback = g_animation_plays.find(handle);
    if (playback == g_animation_plays.end()) return Failed("this entity has no active MoonMod animation");
    if (playback->second.owner != mod_id) {
        return {ApiCallStatus::PermissionDenied, nullptr, "a mod can only stop animations that it started"};
    }
    const ModAnimationPlayback record = playback->second;
    g_animation_plays.erase(playback);
    const auto spawn = g_character_spawns.find(handle);
    if (spawn == g_character_spawns.end() || !IsSpawnHandleCurrent(spawn->second) || !IsSpawnHandleLive(spawn->second)) {
        return {ApiCallStatus::Ok, {{"entity", handle}, {"status", "expired"}, {"changed", false}}, {}};
    }
    CActiveMonster *monster = ActiveMonster->GetActiveMonster(spawn->second.slot);
    if (monster == nullptr) return {ApiCallStatus::Ok, {{"entity", handle}, {"status", "expired"}, {"changed", false}}, {}};
    if (monster->now_key != record.motion_key && monster->next_key != record.motion_key) {
        return {ApiCallStatus::Ok,
                {{"entity", handle}, {"status", "stopped"}, {"changed", false},
                 {"restored_previous_motion", false}},
                {}};
    }
    if (!record.previous_motion.empty() && monster->GetKeyListPtr(const_cast<char *>(record.previous_motion.c_str()), nullptr) != nullptr) {
        monster->SetMotion(const_cast<char *>(record.previous_motion.c_str()), CHARA_MOTION_RESTART, 1);
        return {ApiCallStatus::Ok,
                {{"entity", handle}, {"status", "stopped"}, {"changed", true},
                 {"restored_previous_motion", true}},
                {}};
    }
    monster->ResetMotion();
    return {ApiCallStatus::Ok,
            {{"entity", handle}, {"status", "stopped"}, {"changed", true},
             {"restored_previous_motion", false}},
            {}};
}

void UnloadChronicleTwoAnimations(std::string_view owner) {
    std::vector<std::string> handles;
    for (const auto &[handle, playback] : g_animation_plays) {
        if (playback.owner == owner) handles.push_back(handle);
    }
    for (const std::string &handle : handles) {
        (void)StopAnimation(owner, Json::array({Json{{"entity", handle}}}));
    }
    std::erase_if(g_animations, [owner](const auto &entry) { return entry.second.owner == owner; });
    std::erase_if(g_animation_plays, [owner](const auto &entry) { return entry.second.owner == owner; });
}

ApiCallResult RegisterCharacter(std::string_view mod_id, const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) {
        return Invalid("expected one enemy definition object");
    }
    const Json &definition = args[0];
    const auto id_it = definition.find("id");
    const auto name_it = definition.find("name");
    if (id_it == definition.end() || !id_it->is_string() || name_it == definition.end() || !name_it->is_string()) {
        return Invalid("enemy definition requires string id and name fields");
    }
    const std::string local_id = id_it->get<std::string>();
    std::string qualified_id;
    const std::size_t separator = local_id.find(':');
    if (separator == std::string::npos) {
        if (!IsIdentifier(local_id)) return Invalid("enemy id must use letters, digits, '.', '_' or '-'");
        qualified_id = std::string(mod_id) + ":" + local_id;
    } else {
        if (local_id.find(':', separator + 1) != std::string::npos || local_id.substr(0, separator) != mod_id ||
            !IsIdentifier(std::string_view(local_id).substr(separator + 1))) {
            return Invalid("fully qualified enemy ids must use this mod's namespace");
        }
        qualified_id = local_id;
    }
    const std::string &name = name_it->get_ref<const std::string &>();
    if (name.empty() || name.size() > 31) return Invalid("enemy name must contain 1-31 UTF-8 bytes for the native table");
    const auto kind_it = definition.find("kind");
    if (kind_it != definition.end() && (!kind_it->is_string() || kind_it->get<std::string>() != "enemy")) {
        return Invalid("Chronicle Two data-defined characters currently support kind = 'enemy'");
    }
    const auto assets_it = definition.find("assets");
    if (assets_it != definition.end() &&
        ((!assets_it->is_object() && !assets_it->is_array()) || !assets_it->empty())) {
        return Invalid("enemy registration inherits a native model and AI script; asset overrides are unsupported");
    }
    const ModModel *model_binding = nullptr;
    const auto model_ref = definition.find("model");
    if (model_ref != definition.end()) {
        if (!model_ref->is_string()) return Invalid("model must identify a model registered by this mod");
        const auto found_model = g_models.find(model_ref->get<std::string>());
        if (found_model == g_models.end()) return Invalid("model does not identify a registered custom model");
        if (found_model->second.owner != mod_id) return Failed("a mod can only use custom models that it owns");
        model_binding = &found_model->second;
        if (!NativeResourceOverrideOwnedBy(mod_id, NativeMonsterAssetPath(model_binding->model, "chr"))) {
            return Failed("the selected custom model's native .chr override is not active");
        }
        if (model_binding->custom_script &&
            !NativeResourceOverrideOwnedBy(mod_id, NativeMonsterAssetPath(model_binding->script, "stb"))) {
            return Failed("the selected custom model's native .stb override is not active");
        }
    }
    int template_id = model_binding == nullptr ? -1 : model_binding->template_monster_id;
    const auto template_field = definition.find("template_monster_id");
    if (template_field != definition.end()) {
        if (!ReadInt(definition, "template_monster_id", -1, 0, 32767, template_id)) {
            return Invalid("template_monster_id must name an existing native monster definition");
        }
        if (model_binding != nullptr && template_id != model_binding->template_monster_id) {
            return Invalid("template_monster_id must match the selected registered model's template");
        }
    } else if (model_binding == nullptr) {
        return Invalid("template_monster_id is required when the character does not select a registered model");
    }
    BASE_MONSTER_TBL *template_definition = GetMonsterTable(template_id);
    if (template_definition == nullptr) return Invalid("template_monster_id does not identify a native monster");
    const auto stats_it = definition.find("stats");
    if (stats_it != definition.end() && !stats_it->is_object()) return Invalid("stats must be an object");
    const Json empty_object = Json::object();
    const Json &stats = stats_it == definition.end() ? empty_object : *stats_it;
    int hp = template_definition->life;
    int attack = template_definition->attack;
    int defense = template_definition->defense;
    int reward_exp = template_definition->reward_exp;
    int reward_money = template_definition->reward_money;
    if (!ReadInt(stats, "hp", hp, 1, 1000000, hp) || !ReadInt(stats, "attack", attack, 0, 65535, attack) ||
        !ReadInt(stats, "defense", defense, 0, 255, defense) ||
        !ReadInt(stats, "reward_exp", reward_exp, 0, 65535, reward_exp) ||
        !ReadInt(stats, "reward_money", reward_money, 0, 65535, reward_money)) {
        return Invalid("enemy hp, attack, defense, reward_exp, and reward_money values are out of native range");
    }
    for (auto field = stats.begin(); field != stats.end(); ++field) {
        if (field.key() != "hp" && field.key() != "attack" && field.key() != "defense" &&
            field.key() != "reward_exp" && field.key() != "reward_money") {
            return Invalid("unsupported enemy stat field '" + field.key() + "'");
        }
    }
    if (g_characters.contains(qualified_id)) return Invalid("a character with this stable id is already registered");
    if (g_characters.size() >= kMaximumRegisteredCharacters) {
        return Failed("Chronicle Two's current character adapter supports up to 32 mod enemy definitions");
    }

    int native_id = -1;
    const std::string model_name = model_binding == nullptr
                                       ? BoundedString(template_definition->model, sizeof(template_definition->model))
                                       : model_binding->model;
    const std::string script_name = model_binding == nullptr
                                        ? BoundedString(template_definition->script, sizeof(template_definition->script))
                                        : model_binding->script;
    if (!RegisterMoonModMonsterVariant(template_id, name.c_str(), hp, attack, defense, reward_exp, reward_money,
                                       model_name.c_str(), script_name.c_str(), &native_id)) {
        return Failed("the native monster definition table has no available mod variant slot");
    }
    BASE_MONSTER_TBL *registered = GetMonsterTable(native_id);
    if (registered == nullptr) return Failed("the native monster definition could not be resolved after registration");
    ModCharacter character;
    character.id = qualified_id;
    character.owner = mod_id;
    character.name = name;
    character.model = BoundedString(registered->model, sizeof(registered->model));
    character.script = BoundedString(registered->script, sizeof(registered->script));
    if (model_binding != nullptr) character.model_id = model_binding->id;
    character.template_monster_id = template_id;
    character.native_monster_id = native_id;
    character.hp = hp;
    character.attack = attack;
    character.defense = defense;
    character.reward_exp = reward_exp;
    character.reward_money = reward_money;
    const std::string id = character.id;
    g_characters.emplace(id, std::move(character));
    return {ApiCallStatus::Ok, ModCharacterJson(g_characters.at(id)), {}};
}

bool ReadCharacterId(const Json &selector, int &character_id) {
    std::string id;
    if (!ReadId(selector, id)) return false;
    if (id == "max") character_id = USER_CHARA_MAX;
    else if (id == "monica") character_id = USER_CHARA_MONICA;
    else if (id == "ridepod") character_id = USER_CHARA_ROBO;
    else if (id == "monster") character_id = USER_CHARA_MONSTER;
    else return false;
    return true;
}

ApiCallResult ListCharacters(const Json &args) {
    if (!args.is_array() || !args.empty()) return Invalid("characters.list takes no arguments");
    CUserDataManager *user_data = GetUserDataMan();
    if (user_data == nullptr) return Failed("the game has no active save data");
    const int active_character_id = user_data->GetActiveChrNo();
    const int party_mask = user_data->GetNowPartyMember();
    Json result = Json::array();
    for (int character_id = USER_CHARA_MAX; character_id <= USER_CHARA_MONSTER; ++character_id) {
        result.push_back(CharacterDefinitionJson(*user_data, character_id, active_character_id, party_mask));
    }
    for (const auto &[id, character] : g_characters) {
        (void)id;
        result.push_back(ModCharacterJson(character));
    }
    for (const auto &[handle, spawn] : g_character_spawns) {
        (void)spawn;
        ApiCallResult instance = GetCharacter(Json::array({Json{{"id", handle}}}));
        if (instance.status == ApiCallStatus::Ok) result.push_back(std::move(instance.value));
    }
    return {ApiCallStatus::Ok, std::move(result), {}};
}

ApiCallResult GetCharacter(const Json &args) {
    if (!args.is_array() || args.size() != 1) return Invalid("expected one character id or {id = character_id}");
    std::string stable_id;
    if (ReadId(args[0], stable_id) && stable_id.find(':') != std::string::npos) {
        const auto spawned = g_character_spawns.find(stable_id);
        if (spawned != g_character_spawns.end()) {
            const ModCharacterSpawn &record = spawned->second;
            const ModCharacter *definition = nullptr;
            const auto registered = g_characters.find(record.character_id);
            if (registered != g_characters.end()) definition = &registered->second;
            Json result = definition == nullptr ? Json{{"id", record.character_id}, {"kind", "enemy"}}
                                                : ModCharacterJson(*definition);
            result["entity_handle"] = record.handle;
            result["owner"] = record.owner;
            result["native_monster_id"] = record.native_monster_id;
            result["slot"] = record.slot;
            result["generation"] = record.generation;
            CActiveMonster *monster = ActiveMonster != nullptr && DngMainScene != nullptr &&
                                              ActiveMonster->GetScene() == DngMainScene && record.slot >= 0 && record.slot < MONSTER_ACTIVE_MAX
                                          ? ActiveMonster->GetActiveMonster(record.slot)
                                          : nullptr;
            if (monster == nullptr || GetMoonModMonsterSlotGeneration(record.slot) != record.generation ||
                monster->monster_id != record.native_monster_id) {
                result["status"] = "expired";
            } else if (monster->state != ACTIVE_MONSTER_LIVE || monster->life <= 0) {
                result["status"] = "dead";
            } else {
                float position[4] = {};
                monster->GetPosition(position);
                result["status"] = "alive";
                result["position"] = {{"x", position[0]}, {"y", position[1]}, {"z", position[2]}};
                result["hp"] = monster->life;
                result["max_hp"] = monster->max_life;
                result["attack"] = monster->attack;
                result["defense"] = monster->defense;
            }
            return {ApiCallStatus::Ok, std::move(result), {}};
        }
        const auto found = g_characters.find(stable_id);
        if (found == g_characters.end()) return Invalid("expected a known stable character id");
        return {ApiCallStatus::Ok, ModCharacterJson(found->second), {}};
    }
    int character_id = -1;
    if (!ReadCharacterId(args[0], character_id)) {
        return Invalid("expected a Chronicle Two character id: max, monica, ridepod, or monster");
    }
    CUserDataManager *user_data = GetUserDataMan();
    if (user_data == nullptr) return Failed("the game has no active save data");
    const int active_character_id = user_data->GetActiveChrNo();
    const int party_mask = user_data->GetNowPartyMember();
    return {ApiCallStatus::Ok,
            CharacterDefinitionJson(*user_data, character_id, active_character_id, party_mask), {}};
}

bool IsSpawnHandleCurrent(const ModCharacterSpawn &record) {
    if (ActiveMonster == nullptr || DngMainScene == nullptr || ActiveMonster->GetScene() != DngMainScene ||
        record.slot < 0 || record.slot >= MONSTER_ACTIVE_MAX ||
        GetMoonModMonsterSlotGeneration(record.slot) != record.generation) {
        return false;
    }
    CActiveMonster *monster = ActiveMonster->GetActiveMonster(record.slot);
    return monster != nullptr && monster->monster_id == record.native_monster_id;
}

bool IsSpawnHandleLive(const ModCharacterSpawn &record) {
    if (!IsSpawnHandleCurrent(record)) return false;
    const CActiveMonster *monster = ActiveMonster->GetActiveMonster(record.slot);
    return monster != nullptr && monster->state == ACTIVE_MONSTER_LIVE && monster->life > 0;
}

ApiCallResult SpawnCharacter(std::string_view mod_id, const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) {
        return Invalid("expected {character = registered_enemy_id, position = {x, y, z}, rotation = optional {x, y, z}}");
    }
    const Json &request = args[0];
    for (auto field = request.begin(); field != request.end(); ++field) {
        if (field.key() != "character" && field.key() != "position" && field.key() != "rotation") {
            return Invalid("characters.spawn only supports character, position, and rotation fields");
        }
    }
    const auto character_field = request.find("character");
    if (character_field == request.end() || !character_field->is_string()) {
        return Invalid("character must be a registered stable enemy id");
    }
    const std::string character_id = character_field->get<std::string>();
    const auto definition = g_characters.find(character_id);
    if (definition == g_characters.end()) return Invalid("character does not identify a registered mod enemy");
    if (definition->second.owner != mod_id) return Failed("a mod can only spawn enemy definitions that it owns");

    std::array<float, 4> position{};
    std::array<float, 4> rotation{{0.0f, 0.0f, 0.0f, 1.0f}};
    const auto position_field = request.find("position");
    if (position_field == request.end() || !ReadVector3(*position_field, position, 100000.0f)) {
        return Invalid("position must contain finite x, y, and z coordinates within 100000 game units");
    }
    const auto rotation_field = request.find("rotation");
    if (rotation_field != request.end() && !ReadVector3(*rotation_field, rotation, 10000.0f)) {
        return Invalid("rotation must contain finite x, y, and z angles within 10000 radians");
    }

    CScene *scene = DngMainScene;
    CMonsterMan *manager = ActiveMonster;
    if (scene == nullptr || manager == nullptr || manager->GetScene() != scene) {
        return Failed("characters.spawn requires an active Chronicle Two dungeon");
    }
    int refer_index = manager->SearchBaseIndex(definition->second.native_monster_id);
    if (refer_index < 0) {
        if (!definition->second.model_id.empty()) {
            if (!IsMonsterManagerReadyForModSpawn(scene)) {
                return Failed("the dungeon's native monster table is still loading; retry characters.spawn next tick");
            }
            mgCMemory *memory = scene->GetStack(3);
            if (memory == nullptr || !manager->EntryRefer(definition->second.native_monster_id, memory)) {
                return Failed("Chronicle Two could not load the registered custom model through its native monster loader");
            }
            refer_index = manager->SearchBaseIndex(definition->second.native_monster_id);
            if (refer_index < 0) return Failed("the custom model did not enter the native monster reference table");
        } else {
            int template_refer = manager->SearchBaseIndex(definition->second.template_monster_id);
            if (template_refer < 0) {
                const bool ready = IsMonsterManagerReadyForModSpawn(scene);
                if (!ready) {
                    return Failed("the dungeon's native monster table is still loading; retry characters.spawn next tick");
                }
                return Failed("the enemy's template was not preloaded during floor setup; register characters before entering the floor");
            }
            refer_index = manager->SearchReferBlock();
            if (refer_index < 0) return Failed("the dungeon monster reference table is full");
            manager->refer[refer_index] = manager->refer[template_refer];
            manager->refer[refer_index].id = definition->second.native_monster_id;
            manager->refer[refer_index].chara.tbl = GetMonsterTable(definition->second.native_monster_id);
        }
    }
    if (refer_index < 0) return Failed("the enemy was not added to the dungeon's loaded monster table");
    if (manager->refer[refer_index].chara.GetFrame() == nullptr) {
        if (!definition->second.model_id.empty()) {
            return Failed("the registered custom model was not usable by the native monster loader");
        }
        return Failed("the enemy's inherited model is unavailable in the current game data");
    }

    for (auto entry = g_character_spawns.begin(); entry != g_character_spawns.end();) {
        if (!IsSpawnHandleCurrent(entry->second)) entry = g_character_spawns.erase(entry);
        else ++entry;
    }
    if (g_character_spawns.size() >= 256) {
        for (auto entry = g_character_spawns.begin(); entry != g_character_spawns.end() && g_character_spawns.size() >= 256;) {
            if (!IsSpawnHandleLive(entry->second)) entry = g_character_spawns.erase(entry);
            else ++entry;
        }
    }
    if (g_character_spawns.size() >= 256) return Failed("the session has reached its 256 live mod enemy handle limit");

    CActiveMonster *monster = manager->SetActiveMonster(refer_index, position.data(), rotation.data(), -1);
    if (monster == nullptr || monster->state != ACTIVE_MONSTER_LIVE || monster->life <= 0) {
        return Failed("the native dungeon manager could not place the enemy in an available slot");
    }
    const int slot = monster->chara_type - DNG_CHARA_SLOT_MONSTER;
    if (slot < 0 || slot >= MONSTER_ACTIVE_MAX) return Failed("the native dungeon manager returned an invalid enemy slot");
    const unsigned int generation = GetMoonModMonsterSlotGeneration(slot);
    if (generation == 0) return Failed("the native dungeon manager did not assign an entity generation");

    ChronicleTwoTrackModSpawnedEnemy(mod_id, character_id, slot, generation, definition->second.native_monster_id);
    const std::string handle = "chronicle-two:monster:" + std::to_string(slot) + ":" + std::to_string(generation);
    Json result = ModCharacterJson(definition->second);
    result["entity_handle"] = handle;
    result["status"] = "alive";
    result["slot"] = slot;
    result["generation"] = generation;
    result["position"] = {{"x", position[0]}, {"y", position[1]}, {"z", position[2]}};
    result["hp"] = monster->life;
    result["max_hp"] = monster->max_life;
    SharedRuntime().QueueEvent("game.enemy_spawned",
        {{"entity", handle}, {"character_id", character_id}, {"slot", slot}, {"generation", generation},
         {"native_monster_id", definition->second.native_monster_id}, {"position", result["position"]},
         {"source_mod", mod_id}, {"frame", SharedRuntime().Frame()}});
    return {ApiCallStatus::Ok, std::move(result), {}};
}

ApiCallResult GetEconomy(const Json &args) {
    if (!args.is_array() || !args.empty()) return Invalid("economy.get takes no arguments");
    CUserDataManager *user_data = GetUserDataMan();
    if (user_data == nullptr) return Failed("the game has no active save data");
    return {ApiCallStatus::Ok, {{"currency", "gilda"}, {"amount", user_data->money}, {"max_amount", 999999}}, {}};
}

ApiCallResult AddEconomy(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) {
        return Invalid("expected {amount = integer from 1 to 32767}");
    }
    int amount = 0;
    if (!ReadInt(args[0], "amount", 0, 1, 32767, amount)) {
        return Invalid("amount must be an integer from 1 to 32767");
    }
    CUserDataManager *user_data = GetUserDataMan();
    if (user_data == nullptr) return Failed("the game has no active save data");
    const int before = user_data->money;
    const int after = user_data->AddMoney(amount);
    return {ApiCallStatus::Ok,
            {{"currency", "gilda"}, {"previous", before}, {"amount", after}, {"added", after - before}}, {}};
}

ApiCallResult SpendEconomy(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) {
        return Invalid("expected {amount = integer from 1 to 32767}");
    }
    int amount = 0;
    if (!ReadInt(args[0], "amount", 0, 1, 32767, amount)) {
        return Invalid("amount must be an integer from 1 to 32767");
    }
    CUserDataManager *user_data = GetUserDataMan();
    if (user_data == nullptr) return Failed("the game has no active save data");
    const int before = user_data->money;
    if (amount > before) return Failed("the wallet does not contain enough currency");
    const int after = before - amount;
    user_data->money = after;
    return {ApiCallStatus::Ok,
            {{"currency", "gilda"}, {"previous", before}, {"amount", after}, {"spent", before - after}}, {}};
}

Json PlayerJson(int character_id, const COMMON_GAGE &hp) {
    return {{"character", PlayerCharacterName(character_id)},
            {"character_id", character_id},
            {"hp", hp.now},
            {"max_hp", hp.max}};
}

Json PartyMemberJson(int character_id, int active_character_id) {
    return {{"id", PlayerCharacterName(character_id)},
            {"character_id", character_id},
            {"active", character_id == active_character_id}};
}

std::string PartyNpcId(int npc_id) {
    return "chronicle-two:npc:" + std::to_string(npc_id);
}

Json PartyNpcJson(int npc_id, int status) {
    const char *status_name = (status & PARTY_CHARA_STATUS_IN_PARTY) != 0 ? "in_party"
                              : (status & PARTY_CHARA_STATUS_JOINED) != 0 ? "joined"
                              : (status & PARTY_CHARA_STATUS_IN_HOUSE) != 0 ? "in_house"
                                                                            : "pending";
    return {{"id", PartyNpcId(npc_id)}, {"npc_id", npc_id}, {"kind", "townsperson"},
            {"status", status_name}, {"active", false}};
}

ApiCallResult GetPlayer(const Json &args) {
    if (!args.is_array() || !args.empty()) {
        return Invalid("player.get takes no arguments");
    }
    CUserDataManager *user_data = GetUserDataMan();
    if (user_data == nullptr) {
        return Failed("the game has no active save data");
    }
    int character_id = -1;
    COMMON_GAGE *hp = ActivePlayerGauge(user_data, character_id);
    if (hp == nullptr || PlayerCharacterName(character_id) == nullptr) {
        return Failed("the active character's health gauge is unavailable");
    }
    Json result = PlayerJson(character_id, *hp);
    if (GetNowLoopNo() == LOOP_DUNGEON && MainChara__2 != nullptr) {
        float position[4]{};
        MainChara__2->GetPosition(position);
        for (int axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(position[axis])) {
                return Failed("the active player position contains a non-finite coordinate");
            }
        }
        result["position"] = {{"x", position[0]}, {"y", position[1]}, {"z", position[2]}};
    }
    return {ApiCallStatus::Ok, std::move(result), {}};
}

bool ReadPlayerHealthTarget(const Json &definition, CUserDataManager &user_data, int &character_id,
                           COMMON_GAGE *&hp) {
    character_id = user_data.GetActiveChrNo();
    const auto character = definition.find("character");
    if (character == definition.end()) {
        hp = ActivePlayerGauge(&user_data, character_id);
        return hp != nullptr;
    }
    if (!character->is_string()) return false;
    const std::string &target = character->get_ref<const std::string &>();
    if (target == "active") {
        hp = ActivePlayerGauge(&user_data, character_id);
        return hp != nullptr;
    }
    if (target == "max") character_id = USER_CHARA_MAX;
    else if (target == "monica") character_id = USER_CHARA_MONICA;
    else if (target == "ridepod") character_id = USER_CHARA_ROBO;
    else return false;
    hp = user_data.GetCharaHpGage(character_id);
    return hp != nullptr;
}

ApiCallResult HealPlayer(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) {
        return Invalid("expected {amount = integer, character = optional 'active', 'max', 'monica', or 'ridepod'}");
    }
    const Json &definition = args[0];
    int amount = 0;
    if (!ReadInt(definition, "amount", 0, 1, 32767, amount)) {
        return Invalid("amount must be an integer from 1 to 32767");
    }
    CUserDataManager *user_data = GetUserDataMan();
    if (user_data == nullptr) {
        return Failed("the game has no active save data");
    }
    int character_id = -1;
    COMMON_GAGE *hp = nullptr;
    if (!ReadPlayerHealthTarget(definition, *user_data, character_id, hp)) {
        return Invalid("character must be 'active', 'max', 'monica', or 'ridepod'");
    }
    if (PlayerCharacterName(character_id) == nullptr) {
        return Failed("the player health target is unavailable");
    }
    const float before = hp->now;
    hp->AddPoint(static_cast<float>(amount));
    Json result = PlayerJson(character_id, *hp);
    result["healed"] = hp->now - before;
    return {ApiCallStatus::Ok, std::move(result), {}};
}

ApiCallResult DamagePlayer(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) {
        return Invalid("expected {amount = integer, character = optional 'active', 'max', 'monica', or 'ridepod'}");
    }
    const Json &definition = args[0];
    int amount = 0;
    if (!ReadInt(definition, "amount", 0, 1, 32767, amount)) {
        return Invalid("amount must be an integer from 1 to 32767");
    }
    CUserDataManager *user_data = GetUserDataMan();
    if (user_data == nullptr) {
        return Failed("the game has no active save data");
    }
    int character_id = -1;
    COMMON_GAGE *hp = nullptr;
    if (!ReadPlayerHealthTarget(definition, *user_data, character_id, hp)) {
        return Invalid("character must be 'active', 'max', 'monica', or 'ridepod'");
    }
    if (PlayerCharacterName(character_id) == nullptr) {
        return Failed("the player health target is unavailable");
    }
    const float before = hp->now;
    hp->AddPoint(-static_cast<float>(amount));
    Json result = PlayerJson(character_id, *hp);
    result["damaged"] = before - hp->now;
    return {ApiCallStatus::Ok, std::move(result), {}};
}

struct PlayerStatusSpec {
    const char *name;
    unsigned int attribute;
    int timer_index;
};

constexpr PlayerStatusSpec kPlayerStatuses[] = {
    {"poison", CHARA_STATUS_POISON, -1}, {"slow", CHARA_STATUS_SLOW, 1},
    {"curse", CHARA_STATUS_CURSE, -1},  {"stop", CHARA_STATUS_STOP, 2},
    {"power", CHARA_STATUS_POWER, 0},   {"stone", CHARA_STATUS_STONE, 3},
    {"dry", CHARA_STATUS_DRY, -1},
};

const PlayerStatusSpec *FindPlayerStatus(std::string_view name) {
    for (const PlayerStatusSpec &status : kPlayerStatuses) {
        if (name == status.name) return &status;
    }
    return nullptr;
}

bool ReadPlayerStatusRequest(const Json &args, CUserDataManager &user_data, const PlayerStatusSpec *&status,
                             int &character_id, int &duration_frames, bool adding) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) return false;
    const Json &request = args[0];
    const auto name = request.find("status");
    if (name == request.end() || !name->is_string()) return false;
    status = FindPlayerStatus(name->get_ref<const std::string &>());
    if (status == nullptr) return false;

    character_id = user_data.GetActiveChrNo();
    const auto character = request.find("character");
    if (character != request.end()) {
        if (!character->is_string()) return false;
        const std::string id = character->get<std::string>();
        if (id == "active") character_id = user_data.GetActiveChrNo();
        else if (id == "max") character_id = USER_CHARA_MAX;
        else if (id == "monica") character_id = USER_CHARA_MONICA;
        else return false;
    }
    if (character_id != USER_CHARA_MAX && character_id != USER_CHARA_MONICA) return false;

    const auto duration = request.find("duration_frames");
    if (status->timer_index < 0) {
        if (duration != request.end()) return false;
        duration_frames = 0;
        return true;
    }
    if (!adding) {
        if (duration != request.end()) return false;
        duration_frames = 0;
        return true;
    }
    if (duration == request.end() || !duration->is_number_integer()) return false;
    if (duration->is_number_unsigned()) {
        const unsigned long long value = duration->get<unsigned long long>();
        if (value < 1 || value > 32767) return false;
        duration_frames = static_cast<int>(value);
    } else {
        const long long value = duration->get<long long>();
        if (value < 1 || value > 32767) return false;
        duration_frames = static_cast<int>(value);
    }
    return true;
}

ApiCallResult AddPlayerStatus(const Json &args) {
    CUserDataManager *user_data = GetUserDataMan();
    if (user_data == nullptr) return Failed("the game has no active save data");
    const PlayerStatusSpec *status = nullptr;
    int character_id = -1;
    int duration_frames = 0;
    if (!ReadPlayerStatusRequest(args, *user_data, status, character_id, duration_frames, true)) {
        return Invalid("expected a supported status and character; duration_frames is required for timed statuses");
    }
    CHARA_DATA *character = user_data->GetCharaDataPtr(character_id);
    if (character == nullptr) return Failed("the native character status target is unavailable");
    const bool was_active = (character->status_attr & status->attribute) != 0;
    const int previous_duration = status->timer_index < 0 ? 0 : character->status_time[status->timer_index];
    if (status->timer_index < 0) {
        user_data->SetCharaStatusAttirbute(character_id, status->attribute, CHARA_STATUS_ATTR_MODE_ADD);
    } else {
        user_data->SetCharaStatusAttirbuteVol(character_id, status->attribute, duration_frames);
    }
    const bool active = (character->status_attr & status->attribute) != 0;
    if (!active) {
        if (status->timer_index >= 0) character->status_time[status->timer_index] = previous_duration;
        return Failed("the native status rules rejected this effect for the selected character");
    }
    return {ApiCallStatus::Ok,
            {{"status", status->name}, {"character", PlayerCharacterName(character_id)},
             {"applied", !was_active || status->timer_index >= 0}, {"active", active},
             {"duration_frames", status->timer_index < 0 ? Json(nullptr) : Json(character->status_time[status->timer_index])}},
            {}};
}

ApiCallResult SetPlayer(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) {
        return Invalid("expected {hp = integer from 0 through max_hp, character = optional 'active', 'max', 'monica', or 'ridepod'}");
    }
    const Json &definition = args[0];
    for (auto field = definition.begin(); field != definition.end(); ++field) {
        if (field.key() != "hp" && field.key() != "character") return Invalid("player.set only supports the hp field");
    }
    const auto hp_it = definition.find("hp");
    if (hp_it == definition.end() || !hp_it->is_number_integer()) {
        return Invalid("hp must be an integer from 0 through the selected character's max_hp");
    }
    CUserDataManager *user_data = GetUserDataMan();
    if (user_data == nullptr) return Failed("the game has no active save data");
    int character_id = -1;
    COMMON_GAGE *hp = nullptr;
    if (!ReadPlayerHealthTarget(definition, *user_data, character_id, hp)) {
        return Invalid("character must be 'active', 'max', 'monica', or 'ridepod'");
    }
    if (PlayerCharacterName(character_id) == nullptr) return Failed("the player health target is unavailable");
    if (!std::isfinite(hp->max) || !std::isfinite(hp->now) || hp->max < 0.0f || hp->max > 32767.0f ||
        hp->now < 0.0f || hp->now > hp->max) {
        return Failed("the native health gauge is outside the supported setter range");
    }
    int requested = -1;
    if (hp_it->is_number_unsigned()) {
        const unsigned long long value = hp_it->get<unsigned long long>();
        if (value > hp->max || value > 32767.0f) {
            return Invalid("hp must be within the selected character's native health range");
        }
        requested = static_cast<int>(value);
    } else {
        const long long value = hp_it->get<long long>();
        if (value < 0 || static_cast<double>(value) > hp->max || value > 32767) {
            return Invalid("hp must be within the selected character's native health range");
        }
        requested = static_cast<int>(value);
    }
    const float before = hp->now;
    hp->AddPoint(static_cast<float>(requested) - hp->now);
    Json result = PlayerJson(character_id, *hp);
    result["previous_hp"] = before;
    result["changed"] = hp->now != before;
    return {ApiCallStatus::Ok, std::move(result), {}};
}

ApiCallResult RemovePlayerStatus(const Json &args) {
    CUserDataManager *user_data = GetUserDataMan();
    if (user_data == nullptr) return Failed("the game has no active save data");
    const PlayerStatusSpec *status = nullptr;
    int character_id = -1;
    int duration_frames = 0;
    if (!ReadPlayerStatusRequest(args, *user_data, status, character_id, duration_frames, false)) {
        return Invalid("expected a supported status and character");
    }
    CHARA_DATA *character = user_data->GetCharaDataPtr(character_id);
    if (character == nullptr) return Failed("the native character status target is unavailable");
    const bool removed = (character->status_attr & status->attribute) != 0;
    user_data->SetCharaStatusAttirbute(character_id, status->attribute, CHARA_STATUS_ATTR_MODE_CLEAR);
    if (status->timer_index >= 0) character->status_time[status->timer_index] = 0;
    return {ApiCallStatus::Ok,
            {{"status", status->name}, {"character", PlayerCharacterName(character_id)}, {"removed", removed},
             {"active", (character->status_attr & status->attribute) != 0},
             {"duration_frames", status->timer_index < 0 ? Json(nullptr) : Json(character->status_time[status->timer_index])}},
            {}};
}

ApiCallResult UseNativeItem(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) {
        return Invalid("expected {id = stable_item_id, target = 'active'|'max'|'monica'}");
    }
    std::string id;
    if (!ReadId(args[0], id)) return Invalid("expected a stable item id");
    int item_no = 0;
    std::string error;
    const ApiCallStatus resolve_status = ResolveItemId(id, item_no, error);
    if (resolve_status != ApiCallStatus::Ok) return {resolve_status, nullptr, std::move(error)};

    CDataCommon *common = GameItemDataManage.GetCommonData(item_no);
    CDataItem *definition = GameItemDataManage.GetItemData(item_no);
    if (common == nullptr || definition == nullptr || ConvertUsedItemType(common->type) != USED_ITEM_TYPE_ITEM ||
        (common->attribute & ITEM_ATTRIBUTE_USABLE) == 0 || (definition->action_flags & ITEM_ACTION_USE) == 0) {
        return Failed("the selected item does not support native character use");
    }

    const auto target_argument = args[0].find("target");
    if (target_argument != args[0].end() && !target_argument->is_string()) {
        return Invalid("target must be 'active', 'max', or 'monica'");
    }
    CUserDataManager *user_data = GetUserDataMan();
    if (user_data == nullptr) return Failed("the game has no active save inventory");
    int target_id = USER_CHARA_MAX;
    if (target_argument != args[0].end()) {
        const std::string target_name = target_argument->get<std::string>();
        if (target_name == "active") {
            target_id = user_data->GetActiveChrNo();
        } else if (target_name == "max") {
            target_id = USER_CHARA_MAX;
        } else if (target_name == "monica") {
            target_id = USER_CHARA_MONICA;
        } else {
            return Invalid("target must be 'active', 'max', or 'monica'");
        }
    }
    const int target_flag = target_id == USER_CHARA_MAX ? ITEM_TARGET_MAX
                            : target_id == USER_CHARA_MONICA ? ITEM_TARGET_MONICA
                                                            : 0;
    if (target_flag == 0 || (definition->target_flags & target_flag) == 0) {
        return Invalid("the native item rules do not allow this target");
    }
    CHARA_DATA *target = user_data->GetCharaDataPtr(target_id);
    if (target == nullptr) return Failed("the selected character data is unavailable");
    CGameDataUsed *owned_item = user_data->SearchAllHaveItem(item_no);
    if (owned_item == nullptr || owned_item->used_type != USED_ITEM_TYPE_ITEM || owned_item->GetNum() <= 0) {
        return Failed("the active save does not contain this item");
    }

    const float hp_before = target->hp.now;
    const unsigned int status_before = target->status_attr;
    const int count_before = user_data->GetNumSameItem(item_no);
    CMenuItemUse use;
    const int effects_applied = use.UseItem(owned_item, ITEM_USE_TARGET_CHARA, target);
    if (effects_applied <= 0) return Failed("the native game rules rejected this item use");
    const float hp_after = target->hp.now;
    const int count_after = user_data->GetNumSameItem(item_no);
    return {ApiCallStatus::Ok,
            {{"id", id},
             {"item_no", item_no},
             {"target", target_id == USER_CHARA_MAX ? "max" : "monica"},
             {"effects_applied", effects_applied},
             {"consumed", std::max(0, count_before - count_after)},
             {"remaining", std::max(0, count_after)},
             {"hp_before", hp_before},
             {"hp_after", hp_after},
             {"healed", std::max(0.0f, hp_after - hp_before)},
             {"status_flags_before", status_before},
             {"status_flags_after", target->status_attr},
             {"status_changed", status_before != target->status_attr},
             {"power_duration_frames", target->status_time[0]}},
            {}};
}

ApiCallResult ListParty(const Json &args) {
    if (!args.is_array() || !args.empty()) {
        return Invalid("party.list takes no arguments");
    }
    CUserDataManager *user_data = GetUserDataMan();
    if (user_data == nullptr) {
        return Failed("the game has no active save data");
    }
    const int active_character_id = user_data->GetActiveChrNo();
    const int party_mask = user_data->GetNowPartyMember();
    Json result = Json::array();
    for (int character_id = USER_CHARA_MAX; character_id <= USER_CHARA_MONSTER; ++character_id) {
        if ((party_mask & (1 << character_id)) != 0 && PlayerCharacterName(character_id) != nullptr) {
            result.push_back(PartyMemberJson(character_id, active_character_id));
        }
    }
    for (int npc_id = 1; npc_id <= 32; ++npc_id) {
        const int npc_status = user_data->GetPartyCharaStatus(npc_id);
        if ((npc_status & (PARTY_CHARA_STATUS_IN_PARTY | PARTY_CHARA_STATUS_JOINED | PARTY_CHARA_STATUS_IN_HOUSE |
                           PARTY_CHARA_STATUS_JOIN)) != 0) {
            result.push_back(PartyNpcJson(npc_id, npc_status));
        }
    }
    return {ApiCallStatus::Ok, std::move(result), {}};
}

ApiCallResult GetActivePartyMember(const Json &args) {
    if (!args.is_array() || !args.empty()) {
        return Invalid("party.active takes no arguments");
    }
    CUserDataManager *user_data = GetUserDataMan();
    if (user_data == nullptr) {
        return Failed("the game has no active save data");
    }
    const int active_character_id = user_data->GetActiveChrNo();
    if (PlayerCharacterName(active_character_id) == nullptr) {
        return Failed("the active party character id is not recognized");
    }
    return {ApiCallStatus::Ok, PartyMemberJson(active_character_id, active_character_id), {}};
}

ApiCallResult SetActivePartyMember(const Json &args) {
    if (!args.is_array() || args.size() != 1) return Invalid("expected {id = max|monica|ridepod}");
    std::string id;
    if (!ReadId(args[0], id)) return Invalid("expected a player character id");
    int target_id = -1;
    if (id == "max") target_id = USER_CHARA_MAX;
    else if (id == "monica") target_id = USER_CHARA_MONICA;
    else if (id == "ridepod") target_id = USER_CHARA_ROBO;
    else return Invalid("Chronicle Two only supports switching to max, monica, or ridepod");

    CUserDataManager *user_data = GetUserDataMan();
    if (user_data == nullptr) return Failed("the game has no active save data");
    const int previous_id = user_data->GetActiveChrNo();
    if (PlayerCharacterName(previous_id) == nullptr) return Failed("the active party character id is not recognized");
    if (previous_id == target_id) {
        return {ApiCallStatus::Ok,
                {{"previous", PlayerCharacterName(previous_id)}, {"active", PlayerCharacterName(target_id)},
                 {"changed", false}},
                {}};
    }
    const int quick_change = user_data->CheckQuickChange(target_id, nullptr);
    if ((quick_change & QUICK_CHANGE_STATE_IN_PARTY) == 0) {
        return Failed("the selected character is not in the native party");
    }
    if ((quick_change & QUICK_CHANGE_STATE_ENABLED) == 0) {
        return Failed("the native game rules currently prevent switching to this character");
    }
    user_data->SetActiveChrNo(target_id);
    return {ApiCallStatus::Ok,
            {{"previous", PlayerCharacterName(previous_id)}, {"active", PlayerCharacterName(target_id)},
             {"changed", true}},
            {}};
}

ApiCallResult RecruitPartyNpc(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) {
        return Invalid("expected {npc_id = integer from 1 to 32}");
    }
    int npc_id = 0;
    if (!ReadInt(args[0], "npc_id", 0, 1, 32, npc_id)) {
        return Invalid("npc_id must be an integer from 1 to 32");
    }
    CUserDataManager *user_data = GetUserDataMan();
    if (user_data == nullptr) return Failed("the game has no active save data");
    const int previous_status = user_data->GetPartyCharaStatus(npc_id);
    if ((previous_status & PARTY_CHARA_STATUS_IN_PARTY) != 0) {
        return {ApiCallStatus::Ok,
                {{"id", PartyNpcId(npc_id)}, {"npc_id", npc_id}, {"status", "in_party"},
                 {"recruited", false}, {"changed", false}, {"already_in_party", true}, {"replaced", nullptr}},
                {}};
    }

    const int previous_companion = user_data->NowPartyCharaID();
    user_data->JoinPartyChara(npc_id, PARTY_CHARA_STATUS_JOIN, 1);
    user_data->SetPartyCharaStatus(npc_id, PARTY_CHARA_STATUS_IN_PARTY);
    const int current_status = user_data->GetPartyCharaStatus(npc_id);
    if ((current_status & PARTY_CHARA_STATUS_IN_PARTY) == 0) {
        return Failed("the native party routine did not recruit this townsperson");
    }
    return {ApiCallStatus::Ok,
            {{"id", PartyNpcId(npc_id)}, {"npc_id", npc_id}, {"status", "in_party"},
             {"recruited", true}, {"changed", true}, {"already_in_party", false},
             {"replaced", previous_companion < 1 || previous_companion == npc_id
                               ? Json(nullptr)
                               : Json(PartyNpcId(previous_companion))}},
            {}};
}

ApiCallResult GetWorld(const Json &args) {
    if (!args.is_array() || !args.empty()) {
        return Invalid("world.get takes no arguments");
    }
    CSaveData *save = GetSaveData();
    if (save == nullptr) {
        return Failed("the game has no active save data");
    }
    if (!std::isfinite(save->now_time) || save->now_time < 0.0f || save->now_time >= 24.0f) {
        return Failed("the saved world time is out of range");
    }
    const int loop = GetNowLoopNo();
    const bool in_dungeon = loop == LOOP_DUNGEON;
    const char *context = "map";
    if (in_dungeon) {
        context = "dungeon";
    } else if (loop == LOOP_EDIT) {
        context = "georama";
    } else if (loop == LOOP_TITLE) {
        context = "title";
    } else if (loop == LOOP_MENU) {
        context = "menu";
    }
    Json result = { {"context", context},
                    {"area_id", save->area_no},
                    {"map_id", save->map_no},
                    {"sub_map_id", save->sub_map_no < 0 ? Json(nullptr) : Json(save->sub_map_no)},
                    {"dungeon_id", nullptr},
                    {"floor_id", nullptr},
                    {"time", {{"fraction", save->now_time / 24.0f}, {"native_time", save->now_time},
                               {"native_cycle_hours", 24}}} };
    if (in_dungeon) {
        const int dungeon_id = DngStatus.dungeon_no;
        if (dungeon_id < 0 || dungeon_id >= SAVE_DUNGEON_NUM) {
            return Failed("the active dungeon id is out of range");
        }
        const int floor_id = save->save_dungeon.floor_id[dungeon_id];
        result["dungeon_id"] = dungeon_id;
        result["floor_id"] = floor_id < 0 ? Json(nullptr) : Json(floor_id);
    }
    return {ApiCallStatus::Ok, std::move(result), {}};
}

ApiCallResult InspectNativeRuntime(const Json &args) {
    if (!args.is_array() || !args.empty()) return Invalid("debug.inspect takes no arguments");
    const int loop = GetNowLoopNo();
    const char *scene_id = "other";
    switch (loop) {
        case LOOP_TITLE: scene_id = "title"; break;
        case LOOP_EDIT: scene_id = "edit"; break;
        case LOOP_DUNGEON: scene_id = "dungeon"; break;
        case LOOP_MENU: scene_id = "menu"; break;
        default: break;
    }
    const INIT_LOOP_ARG *current = GetNowInitArg();
    const bool active_map_scene = loop == LOOP_EDIT || loop == LOOP_DUNGEON;
    const bool active_dungeon = loop == LOOP_DUNGEON;
    const Json map_id = active_map_scene && current != nullptr && current->map_no >= 0
                            ? Json(current->map_no)
                            : Json(nullptr);
    const Json floor_id = active_dungeon && current != nullptr && current->floor_no >= 0
                              ? Json(current->floor_no)
                              : Json(nullptr);
    return {ApiCallStatus::Ok,
            {{"scene_id", scene_id}, {"native_mode", loop}, {"native_map_id", map_id},
             {"native_floor_id", floor_id}},
            {}};
}

const char *NativeMapKind(int native_type) {
    switch (native_type) {
        case MAP_TYPE_GEORAMA: return "georama";
        case MAP_TYPE_GEORAMA_INTERIOR: return "georama_interior";
        case MAP_TYPE_INTERIOR: return "interior";
        case MAP_TYPE_FUTURE: return "future";
        case MAP_TYPE_FUTURE_INTERIOR: return "future_interior";
        default: return "other";
    }
}

bool IsAuthoredMapName(std::string_view name) {
    if (name.size() < 3 || name.size() > 15) return false;
    for (unsigned char character : name) {
        if (!((character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
              character == '_' || character == '-')) {
            return false;
        }
    }
    return true;
}

std::string NativeMapAssetPath(std::string_view map_name, std::string_view extension) {
    return dc2::world::MapPath(map_name) + std::string(extension);
}

ModLocation *FindModLocationByMapName(std::string_view map_name) {
    for (auto &[id, location] : g_locations) {
        (void)id;
        if (location.map_name == map_name) return &location;
    }
    return nullptr;
}

void ApplyChronicleTwoLocations() {
    if (GetMapNameCount() <= 0) return;
    for (auto &[id, location] : g_locations) {
        if (!location.registration_error.empty()) continue;
        if (!location.texture_source_map.empty()) {
            char source_name[16] = {};
            std::memcpy(source_name, location.texture_source_map.data(), location.texture_source_map.size());
            const int source_map_id = SearchMapNo(source_name);
            if (source_map_id < 0 || IsMoonModMapName(source_map_id)) {
                location.registration_error = "texture_source_map must name an existing native map";
                continue;
            }
            for (std::string_view extension : {".ipk", ".mpk"}) {
                if (!TextureSourceAliasRegister(NativeMapAssetPath(location.map_name, extension),
                                                NativeMapAssetPath(location.texture_source_map, extension))) {
                    location.registration_error = "texture_source_map conflicts with an existing texture source alias";
                    break;
                }
            }
            if (!location.registration_error.empty()) continue;
        }
        char lookup_name[16] = {};
        std::memcpy(lookup_name, location.map_name.data(), location.map_name.size());
        const int existing = SearchMapNo(lookup_name);
        if (existing >= 0 && !IsMoonModMapName(existing)) {
            location.registration_error = "map_name conflicts with a native map catalog entry";
            continue;
        }
        const int map_id = RegisterMoonModMapName(location.map_name.c_str(), location.name.c_str());
        if (map_id < 0) {
            location.registration_error = "native map catalog rejected this name or reached its extension limit";
            continue;
        }
        location.native_map_id = map_id;
        bool persistent = true;
        for (const std::string &resource : location.resources) {
            if (!SetNativeResourceOverridePersistent(location.owner, resource, true)) {
                persistent = false;
                break;
            }
        }
        if (!persistent) {
            location.registration_error = "a required package map resource is no longer owned by this mod";
            continue;
        }
        (void)id;
    }
}

ApiCallResult RegisterLocation(std::string_view mod_id, const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() ||
        (args[0].size() != 3 && args[0].size() != 4) ||
        !args[0].contains("id") || !args[0]["id"].is_string() || !args[0].contains("map_name") ||
        !args[0]["map_name"].is_string() || !args[0].contains("name") || !args[0]["name"].is_string()) {
        return Invalid("expected {id = local_id, map_name = native map stem, name = display name, texture_source_map? = native map stem}");
    }
    for (auto field = args[0].begin(); field != args[0].end(); ++field) {
        if (field.key() != "id" && field.key() != "map_name" && field.key() != "name" &&
            field.key() != "texture_source_map") {
            return Invalid("locations.register received an unsupported field: " + field.key());
        }
    }
    std::string id;
    if (!QualifyOwnerScopedId(mod_id, args[0]["id"].get_ref<const std::string &>(), id)) {
        return Invalid("id must be a local identifier owned by the calling mod");
    }
    const std::string &map_name = args[0]["map_name"].get_ref<const std::string &>();
    const std::string &name = args[0]["name"].get_ref<const std::string &>();
    std::string texture_source_map;
    if (args[0].contains("texture_source_map")) {
        if (!args[0]["texture_source_map"].is_string()) return Invalid("texture_source_map must be a native map stem");
        texture_source_map = args[0]["texture_source_map"].get<std::string>();
        if (!IsAuthoredMapName(texture_source_map)) {
            return Invalid("texture_source_map must be 3-15 lowercase letters, digits, underscores, or hyphens");
        }
        if (texture_source_map == map_name) return Invalid("texture_source_map must refer to a different native map");
    }
    if (!IsAuthoredMapName(map_name)) {
        return Invalid("map_name must be 3-15 lowercase letters, digits, underscores, or hyphens");
    }
    if (name.empty() || name.size() >= 128 || name.find('\0') != std::string::npos ||
        std::ranges::any_of(name, [](unsigned char character) { return character < 0x20; })) {
        return Invalid("name must contain 1-127 printable UTF-8 bytes");
    }
    if (const auto existing = g_locations.find(id); existing != g_locations.end()) {
        const ModLocation &location = existing->second;
        if (location.owner != mod_id) return Failed("another mod owns this location id");
        if (location.map_name != map_name || location.name != name ||
            location.texture_source_map != texture_source_map) {
            return Failed("location id is already registered with a different definition");
        }
        ApplyChronicleTwoLocations();
        if (!location.registration_error.empty()) return Failed(location.registration_error);
        return {ApiCallStatus::Ok,
                {{"id", "chronicle-two:location:" + id}, {"map_name", map_name}, {"name", name},
                 {"texture_source_map", location.texture_source_map.empty() ? Json(nullptr) : Json(location.texture_source_map)},
                 {"native_map_id", location.native_map_id < 0 ? Json(nullptr) : Json(location.native_map_id)},
                 {"status", location.native_map_id < 0 ? "pending" : "registered"}},
                {}};
    }
    if (g_locations.size() >= kMaximumRegisteredLocations) return Failed("registered location limit reached");
    if (FindModLocationByMapName(map_name) != nullptr) return Failed("map_name is already used by a registered location");

    constexpr std::array<std::string_view, 7> supported_extensions = {
        ".map", ".cfg", ".mpk", ".ipk", ".efp", ".sky", ".stb"};
    constexpr std::array<std::string_view, 2> required_extensions = {".map", ".cfg"};
    std::vector<std::string> resources;
    for (std::string_view extension : supported_extensions) {
        const std::string game_path = NativeMapAssetPath(map_name, extension);
        const bool required = std::ranges::find(required_extensions, extension) != required_extensions.end() ||
                              (texture_source_map.empty() && (extension == ".mpk" || extension == ".ipk"));
        const bool owned = NativeResourceOverrideOwnedBy(mod_id, game_path);
        const bool exists = FindNativeResourceOverride(game_path).has_value();
        if (required && !owned) {
            return Failed(texture_source_map.empty()
                              ? "register owned .map/.cfg files and .mpk/.ipk packs, or set texture_source_map to reuse native packs: " + game_path
                              : "register an owned resources.override for each required map asset: " + game_path);
        }
        if (exists && !owned) {
            return Failed("optional map asset is overridden by another mod: " + game_path);
        }
        if (owned) {
            const std::optional<std::uintmax_t> size = NativeResourceOverrideSize(game_path);
            if (!size || *size > 3ull * 1024ull * 1024ull) {
                return Failed("native map asset is unreadable or exceeds the 3 MiB padded reader limit: " + game_path);
            }
            resources.push_back(game_path);
        }
    }
    if (GetMapNameCount() > 0) {
        char lookup_name[16] = {};
        std::memcpy(lookup_name, map_name.data(), map_name.size());
        const int existing_map = SearchMapNo(lookup_name);
        if (existing_map >= 0 && !IsMoonModMapName(existing_map)) {
            return Failed("map_name conflicts with a native map catalog entry");
        }
    }

    ModLocation location;
    location.id = id;
    location.owner = std::string(mod_id);
    location.map_name = map_name;
    location.name = name;
    location.texture_source_map = texture_source_map;
    location.resources = std::move(resources);
    auto [registered, inserted] = g_locations.emplace(id, std::move(location));
    if (!inserted) return Failed("location id is already registered");
    ApplyChronicleTwoLocations();
    if (!registered->second.registration_error.empty()) return Failed(registered->second.registration_error);
    return {ApiCallStatus::Ok,
            {{"id", "chronicle-two:location:" + id}, {"map_name", map_name}, {"name", name},
             {"texture_source_map", texture_source_map.empty() ? Json(nullptr) : Json(texture_source_map)},
             {"native_map_id", registered->second.native_map_id < 0 ? Json(nullptr) : Json(registered->second.native_map_id)},
             {"native_type", MAP_TYPE_INTERIOR},
             {"status", registered->second.native_map_id < 0 ? "pending" : "registered"},
             {"resources", registered->second.resources}},
            {}};
}

Json NativeLocationJson(int map_id) {
    char *title = nullptr;
    char *map_name = GetMapName(map_id, &title);
    char *additional_path = GetAddMapPath(map_id);
    const int native_type = GetMapType(map_id);
    ModLocation *mod_location = FindModLocationByMapName(map_name);
    if (mod_location != nullptr &&
        (!IsMoonModMapName(map_id) || mod_location->native_map_id != map_id ||
         !mod_location->registration_error.empty())) {
        mod_location = nullptr;
    }
    const std::string id = mod_location != nullptr ? "chronicle-two:location:" + mod_location->id
                                                    : std::string("chronicle-two:location:") + map_name;
    Json result = {{"id", id}, {"map_name", map_name}, {"name", title == nullptr ? Json(map_name) : Json(title)},
                   {"title", title == nullptr ? Json(nullptr) : Json(title)}, {"kind", NativeMapKind(native_type)},
                   {"native_map_id", map_id}, {"native_type", native_type},
                   {"selection_category", GetMapSelType(map_id)}, {"area_id", GetMapAreaNo(map_id)},
                   {"sound_data_id", GetMapSndDataID(map_id)},
                   {"additional_path", additional_path == nullptr ? Json(nullptr) : Json(additional_path)},
                   {"source", mod_location == nullptr ? "native_map_table" : "mod_package"}};
    if (mod_location != nullptr) {
        result["owner"] = mod_location->owner;
        result["status"] = mod_location->registration_error.empty() ? "registered" : "failed";
        result["resources"] = mod_location->resources;
        result["texture_source_map"] = mod_location->texture_source_map.empty()
                                            ? Json(nullptr)
                                            : Json(mod_location->texture_source_map);
    }
    return result;
}

ApiCallResult ListLocations(const Json &args) {
    if (!args.is_array() || !args.empty()) return Invalid("locations.list takes no arguments");
    ApplyChronicleTwoLocations();
    const int map_count = GetMapNameCount();
    if (map_count <= 0) return Failed("the native map catalog is not loaded");
    Json result = Json::array();
    for (int map_id = 0; map_id < map_count; ++map_id) {
        char *map_name = GetMapName(map_id, nullptr);
        if (map_name == nullptr || map_name[0] == '\0' || GetMapType(map_id) < 0) continue;
        result.push_back(NativeLocationJson(map_id));
    }
    return {ApiCallStatus::Ok, std::move(result), {}};
}

ApiCallResult GetLocation(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || args[0].size() != 1 ||
        !args[0].contains("id")) {
        return Invalid("expected {id = stable Chronicle Two map id}");
    }
    std::string requested_id;
    if (!ReadId(args[0]["id"], requested_id)) return Invalid("expected a stable Chronicle Two location id");
    ApplyChronicleTwoLocations();
    const int map_count = GetMapNameCount();
    if (map_count <= 0) return Failed("the native map catalog is not loaded");
    for (int map_id = 0; map_id < map_count; ++map_id) {
        char *map_name = GetMapName(map_id, nullptr);
        if (map_name == nullptr || map_name[0] == '\0' || GetMapType(map_id) < 0) continue;
        const Json location = NativeLocationJson(map_id);
        if (requested_id == location["id"].get<std::string>() || requested_id == map_name) {
            return {ApiCallStatus::Ok, location, {}};
        }
    }
    return Failed("no native Chronicle Two location has this id");
}

ApiCallResult TransitionWorld(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || args[0].size() != 1 ||
        !args[0].contains("id")) {
        return Invalid("expected {id = stable native location id}");
    }
    std::string requested_id;
    if (!ReadId(args[0]["id"], requested_id)) return Invalid("expected a stable native location id");
    ApplyChronicleTwoLocations();

    for (const auto &entry : g_dungeons) {
        const ModDungeon &dungeon = entry.second;
        const auto floor = std::ranges::find(dungeon.floors, requested_id, &ModDungeonFloor::id);
        if (floor == dungeon.floors.end()) continue;
        if (floor->native_floor_id < 0) {
            return Failed("the registered floor has not been materialized in a loaded native dungeon map");
        }
        if (dungeon.dungeon < 0 || dungeon.dungeon >= SAVE_DUNGEON_NUM) {
            return Failed("the registered floor points outside the native dungeon table");
        }
        CSaveData *save = GetSaveData();
        if (save == nullptr) return Failed("the game has no active save data");
        if (GetNowLoopNo() != LOOP_DUNGEON || DngStatus.dungeon_no != dungeon.dungeon) {
            return Failed("a registered floor transition requires the same active native dungeon");
        }
        if (save->save_dungeon.floor_id[dungeon.dungeon] == floor->native_floor_id) {
            return {ApiCallStatus::Ok,
                    {{"id", floor->id}, {"dungeon_id", dungeon.id}, {"native_dungeon_id", dungeon.dungeon},
                     {"native_floor_id", floor->native_floor_id}, {"queued", false}, {"changed", false}},
                    {}};
        }
        CScene *scene = GetMainScene();
        if (scene == nullptr) return Failed("the active dungeon scene is unavailable");
        INIT_LOOP_ARG destination{};
        destination.map_no = dungeon.dungeon;
        destination.floor_no = floor->native_floor_id;
        destination.event_no = 0;
        g_pending_dungeon_entry = dungeon.dungeon;
        g_pending_dungeon_entry_floor = floor->native_floor_id;
        NextLoop(LOOP_DUNGEON, destination);
        scene->exit_flag = true;
        return {ApiCallStatus::Ok,
                {{"id", floor->id}, {"dungeon_id", dungeon.id}, {"native_dungeon_id", dungeon.dungeon},
                 {"native_floor_id", floor->native_floor_id}, {"context", "dungeon_floor"},
                 {"queued", true}, {"changed", true}},
                {}};
    }

    const int map_count = GetMapNameCount();
    if (map_count <= 0) return Failed("the native map catalog is not loaded");

    int destination_map = -1;
    for (int map_id = 0; map_id < map_count; ++map_id) {
        char *map_name = GetMapName(map_id, nullptr);
        if (map_name == nullptr || map_name[0] == '\0' || GetMapType(map_id) < 0) continue;
        const Json location = NativeLocationJson(map_id);
        if (requested_id == location["id"].get<std::string>() || requested_id == map_name) {
            if (destination_map >= 0) return Failed("the native location id is ambiguous in the loaded map catalog");
            destination_map = map_id;
        }
    }
    if (destination_map < 0) return Failed("no native Chronicle Two location has this id");
    char *destination_name = GetMapName(destination_map, nullptr);
    ModLocation *authored_location = FindModLocationByMapName(destination_name == nullptr ? "" : destination_name);
    if (authored_location != nullptr && !authored_location->registration_error.empty()) {
        return Failed(authored_location->registration_error);
    }
    const bool authored_map = authored_location != nullptr && IsMoonModMapName(destination_map);
    if (GetMapType(destination_map) != MAP_TYPE_GEORAMA &&
        !(authored_map && GetMapType(destination_map) == MAP_TYPE_INTERIOR)) {
        return Failed("world.transition accepts native Georama towns or registered MoonMod interior maps");
    }

    const int current_loop = GetNowLoopNo();
    if (current_loop != LOOP_EDIT && current_loop != LOOP_DUNGEON) {
        return Failed("a native town transition requires an active town or dungeon scene");
    }
    if (current_loop == LOOP_EDIT) {
        const INIT_LOOP_ARG *current = GetNowInitArg();
        if (current != nullptr && current->map_no == destination_map) {
            return {ApiCallStatus::Ok,
                    {{"id", requested_id}, {"native_map_id", destination_map}, {"queued", false}, {"changed", false}},
                    {}};
        }
    }

    INIT_LOOP_ARG destination{};
    destination.map_no = destination_map;
    destination.floor_no = -1;
    destination.event_no = 100;
    CScene *scene = GetMainScene();
    if (scene == nullptr) return Failed("the active map scene is unavailable");
    NextLoop(LOOP_EDIT, destination);
    scene->exit_flag = true;
    return {ApiCallStatus::Ok,
            {{"id", authored_location == nullptr ? std::string("chronicle-two:location:") + destination_name
                                                   : "chronicle-two:location:" + authored_location->id},
             {"native_map_id", destination_map}, {"context", authored_map ? "mod_location" : "georama"},
             {"queued", true}, {"changed", true}},
            {}};
}

ApiCallResult GetGeorama(const Json &args) {
    if (args.size() > 1 || (args.size() == 1 && !args[0].is_object())) {
        return Invalid("expected no arguments or {town_id = integer from 0 to 4}");
    }

    CSaveData *save = GetSaveData();
    if (save == nullptr) return Failed("the game has no active save data");

    int town_id = -1;
    if (args.empty()) {
        if (GetNowLoopNo() != LOOP_EDIT) return Failed("there is no active Georama town");
        town_id = save->area_no;
        if (town_id < 0 || town_id >= SAVE_EDIT_DATA_MAX) {
            return Failed("the active Georama town id is outside the supported range");
        }
    } else {
        const Json &query = args[0];
        if (query.size() != 1 || !query.contains("town_id") || !query["town_id"].is_number_integer()) {
            return Invalid("expected {town_id = integer from 0 to 4}");
        }
        const std::int64_t requested = query["town_id"].get<std::int64_t>();
        if (requested < 0 || requested >= SAVE_EDIT_DATA_MAX) {
            return Invalid("town_id must be an integer from 0 to 4");
        }
        town_id = static_cast<int>(requested);
    }

    CEditData *town = save->GetEditData(town_id);
    if (town == nullptr) {
        return Failed("the saved Georama town data is outside the supported range");
    }
    // The town's layout is kept beside its save record, not in the record's arrays (save_json.hpp).
    static const TownLayout empty_layout;
    const TownLayout *layout_found = PortFindTownLayout(*town);
    const TownLayout &layout = layout_found != nullptr ? *layout_found : empty_layout;
    const int house_count = static_cast<int>(layout.houses.size());

    Json river_grids = Json::array();
    for (std::size_t grid_index = 0; grid_index < layout.grids.size(); ++grid_index) {
        const TownGrid &grid = layout.grids[grid_index];
        if (grid.num_x <= 0 || grid.num_z <= 0 ||
            grid.cells.size() != static_cast<std::size_t>(grid.num_x) * static_cast<std::size_t>(grid.num_z)) {
            return Failed("the saved Georama river-grid dimensions are invalid");
        }
        Json cells = Json::array();
        for (int x = 0; x < grid.num_x; ++x) {
            Json row = Json::array();
            for (int z = 0; z < grid.num_z; ++z) {
                row.push_back((grid.cells[static_cast<std::size_t>(x) * static_cast<std::size_t>(grid.num_z) +
                                          static_cast<std::size_t>(z)] & 1) != 0);
            }
            cells.push_back(std::move(row));
        }
        river_grids.push_back({{"index", grid_index},
                               {"size", {{"x", grid.num_x}, {"z", grid.num_z}}},
                               {"position", {{"x", grid.pos[0]}, {"y", grid.pos[1]}, {"z", grid.pos[2]}}},
                               {"cells", std::move(cells)}});
    }

    Json placed_parts = Json::array();
    for (std::size_t index = 0; index < layout.parts.size(); ++index) {
        const TownPart &part = layout.parts[index];
        if (part.id == 0 || part.state == 0) continue;
        if (part.id < 0 || part.state < 0 || part.state > 2 || part.angle < 0 ||
            part.angle >= kGeoramaAngleSteps || (part.house_no < 0 || part.house_no > house_count)) {
            return Failed("the saved Georama placement list contains an invalid record");
        }
        Json colors = Json::array();
        for (int color_index = 0; color_index < EDIT_DATA_COLOR_MAX; ++color_index) {
            colors.push_back({part.color[color_index][0], part.color[color_index][1], part.color[color_index][2]});
        }
        placed_parts.push_back({{"index", index},
                                {"id", "chronicle-two:georama-placement:" + std::to_string(town_id) + ":" +
                                           std::to_string(index)},
                                {"part_id", part.id},
                                {"state", part.state},
                                {"rotation_steps", part.angle},
                                {"position", {{"x", part.pos[0]}, {"y", part.pos[1]}, {"z", part.pos[2]}}},
                                {"colors", std::move(colors)},
                                {"house_id", part.house_no > 0 ? Json(part.house_no - 1) : Json(nullptr)}});
    }

    Json houses = Json::array();
    for (int index = 0; index < house_count; ++index) {
        const int npc_id = layout.houses[static_cast<std::size_t>(index)].npc_no;
        if (npc_id <= 0) continue;
        houses.push_back({{"house_id", index}, {"npc_id", npc_id}});
    }

    Json known_requests = Json::array();
    for (int index = 0; index < EDIT_ANALYZE_DATA_MAX; ++index) {
        if (town->analyze.data_open[index] != 0) known_requests.push_back(index);
    }
    Json met_conditions = Json::array();
    Json known_conditions = Json::array();
    for (int index = 0; index < EDIT_ANALYZE_CONDITION_MAX; ++index) {
        if (town->analyze.condition[index] != 0) met_conditions.push_back(index);
        if (town->analyze.condition_open[index] != 0) known_conditions.push_back(index);
    }
    Json mod_pieces = Json::array();
    for (const auto &[id, piece] : g_georama_pieces) {
        (void)id;
        mod_pieces.push_back(GeoramaPieceJson(piece, town_id));
    }
    const Json native_piece_templates = g_georama_native_templates[static_cast<std::size_t>(town_id)];

    Json placement_anchors = Json::array();
    if (GetNowLoopNo() == LOOP_EDIT) {
        CScene *scene = GetMainScene();
        if (scene != nullptr && scene->active_map >= 0) {
            CMap *active_map = scene->GetMap(scene->active_map);
            if (active_map != nullptr && std::strcmp(active_map->Iam(), "CEditMap") == 0) {
                auto *edit_map = static_cast<CEditMap *>(active_map);
                if (edit_map->area_no == town_id && edit_map->edit_parts_max >= 0 &&
                    edit_map->edit_parts_max <= 2048) {
                    for (int slot = 0; slot < edit_map->edit_parts_max && placement_anchors.size() < 512; ++slot) {
                        CEditParts *part = edit_map->GetePlaceParts(slot);
                        if (part == nullptr || part->name[0] == 0 || part->state != EDIT_PARTS_STATE_PLACED ||
                            part->info == nullptr || part->info->edit_name == nullptr) {
                            continue;
                        }
                        const float floor_area = part->info->col_floor.AreaXZ();
                        if (!std::isfinite(floor_area) || floor_area <= 0.01f) continue;
                        float position[4]{};
                        part->GetPosition(position);
                        if (!std::isfinite(position[0]) || !std::isfinite(position[1]) ||
                            !std::isfinite(position[2])) {
                            continue;
                        }
                        placement_anchors.push_back(
                            {{"part_id", part->GetInfoID()},
                             {"name", part->info->edit_name},
                             {"position", {{"x", position[0]}, {"y", position[1]}, {"z", position[2]}}},
                             {"surface_y", position[1] + part->info->col_floor.bbox.max[1]},
                             {"floor_area", floor_area},
                             {"footprint", {{"min_x", part->info->col_floor.bbox.min[0]},
                                             {"max_x", part->info->col_floor.bbox.max[0]},
                                             {"min_z", part->info->col_floor.bbox.min[2]},
                                             {"max_z", part->info->col_floor.bbox.max[2]}}}});
                    }
                }
            }
        }
    }

    return {ApiCallStatus::Ok,
            {{"town_id", town_id},
             {"id", "chronicle-two:town:" + std::to_string(town_id)},
             {"save_count", town->save_count},
             {"culture_points", town->culture_point},
             {"placed_parts", std::move(placed_parts)},
             {"river_grids", std::move(river_grids)},
             {"houses", std::move(houses)},
             {"native_piece_templates", native_piece_templates},
             {"placement_anchors", std::move(placement_anchors)},
             {"mod_pieces", std::move(mod_pieces)},
             {"known_requests", std::move(known_requests)},
             {"met_conditions", std::move(met_conditions)},
             {"known_conditions", std::move(known_conditions)}},
            {}};
}

ApiCallResult SetGeoramaCondition(const Json &args) {
    if (args.size() != 1 || !args[0].is_object() || args[0].size() != 3 ||
        !args[0].contains("town_id") || !args[0].contains("condition_id") || !args[0].contains("met") ||
        !args[0]["met"].is_boolean()) {
        return Invalid("expected {town_id = 0-4, condition_id = 0-63, met = boolean}");
    }
    int town_id = 0;
    int condition_id = 0;
    if (!ReadInt(args[0], "town_id", 0, 0, SAVE_EDIT_DATA_MAX - 1, town_id) ||
        !ReadInt(args[0], "condition_id", 0, 0, EDIT_ANALYZE_CONDITION_MAX - 1, condition_id)) {
        return Invalid("town_id must be 0-4 and condition_id must be 0-63");
    }

    CSaveData *save = GetSaveData();
    if (save == nullptr) return Failed("the game has no active save data");
    CEditData *town = save->GetEditData(town_id);
    if (town == nullptr) return Failed("the saved Georama town data is unavailable");

    const bool previous = town->analyze.condition[condition_id] != 0;
    const bool met = args[0]["met"].get<bool>();
    town->dbgSetContintionFlag(town_id, condition_id, met ? 1 : 0);
    const bool resulting = town->analyze.condition[condition_id] != 0;
    return {ApiCallStatus::Ok,
            {{"town_id", town_id}, {"condition_id", condition_id}, {"previous", previous},
             {"met", resulting}, {"changed", previous != resulting}},
            {}};
}

ApiCallResult SetGeoramaRequest(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || args[0].size() != 3 ||
        !args[0].contains("town_id") || !args[0].contains("request_id") || !args[0].contains("completed") ||
        !args[0]["completed"].is_boolean()) {
        return Invalid("expected {town_id = 0-4, request_id = 0-15, completed = boolean}");
    }
    int town_id = 0;
    int request_id = 0;
    if (!ReadInt(args[0], "town_id", 0, 0, SAVE_EDIT_DATA_MAX - 1, town_id) ||
        !ReadInt(args[0], "request_id", 0, 0, EDIT_ANALYZE_DATA_MAX - 1, request_id)) {
        return Invalid("town_id must be 0-4 and request_id must be 0-15");
    }

    CSaveData *save = GetSaveData();
    if (save == nullptr) return Failed("the game has no active save data");
    CEditData *town = save->GetEditData(town_id);
    if (town == nullptr) return Failed("the saved Georama town data is unavailable");
    EditAnalyzeDataSrc *definition = town->GetAnalyzeData(town_id, request_id);
    if (definition == nullptr || definition->message == nullptr) {
        return Failed("the selected town request is not defined in the loaded native analysis data");
    }

    const bool previous = town->GetAnalyzeFlag(town_id, request_id) != 0;
    const bool requested = args[0]["completed"].get<bool>();
    if (previous != requested) town->dbgSetAnalyzeFlag(town_id, request_id, requested ? 1 : 0);
    const bool resulting = town->GetAnalyzeFlag(town_id, request_id) != 0;
    return {ApiCallStatus::Ok,
            {{"town_id", town_id}, {"request_id", request_id}, {"previous", previous},
             {"completed", resulting}, {"changed", previous != resulting},
             {"progress_percent", town->GetAnalyzePercent(town_id)}},
            {}};
}

ApiCallResult SetGeoramaRequestKnown(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || args[0].size() != 3 ||
        !args[0].contains("town_id") || !args[0].contains("request_id") || !args[0].contains("known") ||
        !args[0]["known"].is_boolean()) {
        return Invalid("expected {town_id = 0-4, request_id = 0-15, known = boolean}");
    }
    int town_id = 0;
    int request_id = 0;
    if (!ReadInt(args[0], "town_id", 0, 0, SAVE_EDIT_DATA_MAX - 1, town_id) ||
        !ReadInt(args[0], "request_id", 0, 0, EDIT_ANALYZE_DATA_MAX - 1, request_id)) {
        return Invalid("town_id must be 0-4 and request_id must be 0-15");
    }
    CSaveData *save = GetSaveData();
    if (save == nullptr) return Failed("the game has no active save data");
    CEditData *town = save->GetEditData(town_id);
    if (town == nullptr) return Failed("the saved Georama town data is unavailable");

    const bool previous = town->analyze.data_open[request_id] != 0;
    const bool known = args[0]["known"].get<bool>();
    town->analyze.data_open[request_id] = known ? 1 : 0;
    const bool resulting = town->analyze.data_open[request_id] != 0;
    return {ApiCallStatus::Ok,
            {{"town_id", town_id}, {"request_id", request_id}, {"previous", previous},
             {"known", resulting}, {"changed", previous != resulting}},
            {}};
}

ApiCallResult RegisterGeoramaPiece(std::string_view mod_id, const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) {
        return Invalid("expected one data-only piece variant definition");
    }
    const Json &definition = args[0];
    const auto id_it = definition.find("id");
    const auto name_it = definition.find("name");
    if (id_it == definition.end() || !id_it->is_string() || name_it == definition.end() || !name_it->is_string()) {
        return Invalid("piece definition requires string id and name fields");
    }
    const auto template_it = definition.find("template_part_id");
    int template_part_id = -1;
    if (template_it == definition.end() || !ReadInt(definition, "template_part_id", -1, 1, 255, template_part_id)) {
        return Invalid("template_part_id must identify a native Georama piece from 1 through 255");
    }
    const std::string &display_name = name_it->get_ref<const std::string &>();
    if (display_name.empty() || display_name.size() > 47 || display_name.find('\0') != std::string::npos) {
        return Invalid("piece name must contain 1-47 bytes and fit the native Georama menu");
    }
    const auto assets_it = definition.find("assets");
    if (assets_it != definition.end() &&
        ((!assets_it->is_object() && !assets_it->is_array()) || !assets_it->empty())) {
        return Invalid("Georama piece variants inherit the native template model and collision; asset overrides are unsupported");
    }
    const std::size_t expected_fields = assets_it == definition.end() ? 3u : 4u;
    if (definition.size() != expected_fields) {
        return Invalid("piece definition accepts only id, name, template_part_id, and optional empty assets");
    }

    std::string local_id = id_it->get<std::string>();
    std::string qualified_id;
    const std::size_t separator = local_id.find(':');
    if (separator == std::string::npos) {
        if (!IsIdentifier(local_id)) return Invalid("piece id must be a short identifier using letters, digits, '.', '_' or '-'");
        qualified_id = std::string(mod_id) + ":" + local_id;
    } else {
        if (local_id.find(':', separator + 1) != std::string::npos || local_id.substr(0, separator) != mod_id ||
            !IsIdentifier(std::string_view(local_id).substr(separator + 1))) {
            return Invalid("fully qualified piece ids must use this mod's namespace");
        }
        qualified_id = std::move(local_id);
    }
    if (g_georama_pieces.contains(qualified_id)) return Invalid("a Georama piece with this stable id is already registered");
    if (g_georama_pieces.size() >= kMaximumRegisteredGeoramaPieces) {
        return Failed("Chronicle Two's current Georama adapter supports up to 32 mod piece variants");
    }

    ModGeoramaPiece piece;
    piece.id = qualified_id;
    piece.owner = mod_id;
    piece.name = display_name;
    piece.template_part_id = template_part_id;
    g_georama_pieces.emplace(qualified_id, std::move(piece));
    return {ApiCallStatus::Ok, GeoramaPieceJson(g_georama_pieces.at(qualified_id)), {}};
}

ApiCallResult RegisterItem(std::string_view mod_id, const Json &args);
ApiCallResult RegisterWeapon(std::string_view mod_id, const Json &args);
ApiCallResult RegisterDungeon(std::string_view mod_id, const Json &args);

ApiCallResult ListContent(const Json &args) {
    if (!args.is_array() || !args.empty()) return Invalid("content.list takes no arguments");
    Json entries = Json::array();
    auto append = [&entries](std::string_view type, Json definition) {
        definition["content_type"] = type;
        entries.push_back(std::move(definition));
    };
    for (const auto &[id, item] : g_items) { (void)id; append("item", ItemJson(item)); }
    for (const auto &[id, weapon] : g_weapons) { (void)id; append("weapon", ModWeaponJson(weapon)); }
    for (const auto &[id, character] : g_characters) { (void)id; append("character", ModCharacterJson(character)); }
    for (const auto &[id, dungeon] : g_dungeons) { (void)id; append("dungeon", DungeonJson(dungeon)); }
    for (const auto &[id, piece] : g_georama_pieces) { (void)id; append("georama_piece", GeoramaPieceJson(piece)); }
    return {ApiCallStatus::Ok, {{"count", entries.size()}, {"entries", std::move(entries)}}, {}};
}

ApiCallResult RegisterContent(std::string_view mod_id, const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || args[0].size() != 2 ||
        !args[0].contains("type") || !args[0]["type"].is_string() ||
        !args[0].contains("definition") || !args[0]["definition"].is_object()) {
        return Invalid("expected {type = 'item'|'weapon'|'character'|'dungeon'|'georama_piece', definition = {...}}");
    }
    const std::string type = args[0]["type"].get<std::string>();
    const Json wrapped = Json::array({args[0]["definition"]});
    if (type == "item") return RegisterItem(mod_id, wrapped);
    if (type == "weapon") return RegisterWeapon(mod_id, wrapped);
    if (type == "character") return RegisterCharacter(mod_id, wrapped);
    if (type == "dungeon") return RegisterDungeon(mod_id, wrapped);
    if (type == "georama_piece") return RegisterGeoramaPiece(mod_id, wrapped);
    return Invalid("content.register type must be item, weapon, character, dungeon, or georama_piece in Chronicle Two");
}

ApiCallResult UnregisterContent(std::string_view mod_id, const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || args[0].size() != 2 ||
        !args[0].contains("type") || !args[0]["type"].is_string() ||
        !args[0].contains("id") || !args[0]["id"].is_string()) {
        return Invalid("expected {type = 'item'|'weapon'|'dungeon'|'georama_piece', id = local_content_id}");
    }

    const std::string type = args[0]["type"].get<std::string>();
    std::string id;
    if (!QualifyOwnerScopedId(mod_id, args[0]["id"].get_ref<const std::string &>(), id)) {
        return Invalid("id must be a local identifier owned by this mod");
    }

    auto removed = [&id, &type]() {
        return ApiCallResult{ApiCallStatus::Ok, {{"id", id}, {"type", type}, {"removed", true}}, {}};
    };
    if (type == "item") {
        const auto found = g_items.find(id);
        if (found == g_items.end() || found->second.owner != mod_id) {
            return Invalid("item registration is missing or owned by another mod");
        }
        if (found->second.native_item_no >= 0) {
            return Failed("item is already materialized in native tables and cannot be unregistered safely");
        }
        g_items.erase(found);
        return removed();
    }
    if (type == "weapon") {
        const auto found = g_weapons.find(id);
        if (found == g_weapons.end() || found->second.owner != mod_id) {
            return Invalid("weapon registration is missing or owned by another mod");
        }
        if (found->second.native_item_no >= 0) {
            return Failed("weapon is already materialized in native tables and cannot be unregistered safely");
        }
        g_weapons.erase(found);
        return removed();
    }
    if (type == "dungeon") {
        const auto found = g_dungeons.find(id);
        if (found == g_dungeons.end() || found->second.owner != mod_id) {
            return Invalid("dungeon registration is missing or owned by another mod");
        }
        const bool materialized = std::ranges::any_of(found->second.floors, [](const ModDungeonFloor &floor) {
            return floor.native_floor_id >= 0;
        });
        if (materialized) {
            return Failed("dungeon floors are already materialized in native tables and cannot be unregistered safely");
        }
        g_dungeons.erase(found);
        return removed();
    }
    if (type == "georama_piece") {
        const auto found = g_georama_pieces.find(id);
        if (found == g_georama_pieces.end() || found->second.owner != mod_id) {
            return Invalid("Georama piece registration is missing or owned by another mod");
        }
        const bool materialized = std::ranges::any_of(found->second.native_part_ids, [](int native_id) {
            return native_id >= 0;
        });
        if (materialized) {
            return Failed("Georama piece is already materialized in a town and cannot be unregistered safely");
        }
        g_georama_pieces.erase(found);
        return removed();
    }
    if (type == "character") {
        const auto found = g_characters.find(id);
        if (found == g_characters.end() || found->second.owner != mod_id) {
            return Invalid("character registration is missing or owned by another mod");
        }
        return Failed("character registration immediately adds a native monster variant and cannot be unregistered safely");
    }
    return Invalid("Chronicle Two content.unregister supports item, weapon, dungeon, georama_piece, and character ids only");
}

ApiCallResult PlaceGeoramaPiece(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || args[0].size() != 3 ||
        !args[0].contains("id") || !args[0].contains("position") || !args[0].contains("rotation_steps")) {
        return Invalid("expected {id = registered_piece_id, position = {x, y, z}, rotation_steps = 0-23}");
    }

    std::string id;
    if (!ReadId(args[0], id)) return Invalid("id must be a registered Georama piece id");
    const auto piece_it = g_georama_pieces.find(id);
    if (piece_it == g_georama_pieces.end()) {
        return {ApiCallStatus::Failed, nullptr, "no registered Georama piece has that stable id"};
    }

    const Json &position_arg = args[0]["position"];
    if (!position_arg.is_object() || position_arg.size() != 3 || !position_arg.contains("x") ||
        !position_arg.contains("y") || !position_arg.contains("z")) {
        return Invalid("position must contain only numeric x, y, and z coordinates");
    }
    auto read_coordinate = [&position_arg](const char *key, float minimum, float maximum, float &out) {
        const Json &value = position_arg[key];
        if (!value.is_number()) return false;
        const double number = value.get<double>();
        if (!std::isfinite(number) || number < minimum || number > maximum) return false;
        out = static_cast<float>(number);
        return true;
    };

    sceVu0FVECTOR requested_position{};
    requested_position[3] = 1.0f;
    int rotation_steps = -1;
    if (!read_coordinate("x", -32768.0f, 32767.0f, requested_position[0]) ||
        !read_coordinate("y", -4096.0f, 4096.0f, requested_position[1]) ||
        !read_coordinate("z", -32768.0f, 32767.0f, requested_position[2]) ||
        !ReadInt(args[0], "rotation_steps", -1, 0, kGeoramaAngleSteps - 1, rotation_steps)) {
        return Invalid("position coordinates must be finite and bounded; rotation_steps must be an integer from 0 to 23");
    }

    if (GetNowLoopNo() != LOOP_EDIT) return Failed("Georama placement requires an active town edit scene");
    CScene *scene = GetMainScene();
    if (scene == nullptr || scene->active_map < 0) return Failed("the active Georama map is unavailable");
    CMap *active_map = scene->GetMap(scene->active_map);
    if (active_map == nullptr || std::strcmp(active_map->Iam(), "CEditMap") != 0) {
        return Failed("the active scene map is not a Georama edit map");
    }
    auto *map = static_cast<CEditMap *>(active_map);
    if (map->area_no < 0 || map->area_no >= SAVE_EDIT_DATA_MAX) {
        return Failed("the active Georama town id is outside the supported range");
    }

    CSaveData *save = GetSaveData();
    if (save == nullptr) return Failed("the game has no active save data");
    const int town_id = map->area_no;
    CEditData *town = save->GetEditData(town_id);
    if (town == nullptr) return Failed("the saved Georama town data is unavailable");

    ModGeoramaPiece &piece = piece_it->second;
    const int native_id = piece.native_part_ids[static_cast<std::size_t>(town_id)];
    if (native_id < 0) return Failed("this registered piece has not been materialized in the active town");
    CEditPartsInfo *info = map->GetePartsInfoAtID(native_id);
    if (info == nullptr || info->id != native_id) {
        return Failed("the registered piece definition is unavailable in the active town");
    }
    if ((info->attr & EDIT_PARTS_ATR_RIVER) != 0 || info->GetPartsType() == EDIT_PARTS_TYPE_RIVER) {
        return Failed("API placement currently supports solid Georama pieces; river pieces require the native grid editor");
    }

    // The current town may have unsaved editor changes. Count its live parts and use saved counts for the other towns.
    const int saved_town_count = SavedTownPartCount(*town, native_id);
    const int live_town_count = map->GetePlacePartsAtInfoID(native_id, nullptr, 0);
    const int placed_count = std::max(0, save->GetPlaceEditPartsNum(native_id) - saved_town_count + live_town_count);
    const int built_count = save->GetBuildPartsNum(native_id);
    if (placed_count >= info->max_num) return Failed("the native placement limit for this piece has been reached");
    if (built_count <= 0) return Failed("the player has no built piece available to place");

    sceVu0FVECTOR position{};
    map->GetEditPos(position, requested_position);
    EP_PLACE_INFO placement{};
    const float rotation_y = map->GetEditAngle(rotation_steps);
    if (map->CheckEditParts(info, position, rotation_y, &placement) == 0) {
        return Failed("the native Georama placement rules reject the requested position");
    }

    const int part_slot = map->BuildEditParts(native_id);
    if (part_slot < 0) return Failed("the native Georama map could not build a placement instance");
    sceVu0FVECTOR rotation{0.0f, rotation_y, 0.0f, 0.0f};
    CEditParts *placed = map->PlaceEditParts(part_slot, &placement, position, rotation, nullptr);
    if (placed == nullptr) {
        map->DeleteEditParts(part_slot);
        return Failed("the native Georama map rejected the built placement instance");
    }

    save->AddBuildPartsNum(native_id, -1);
    map->GroundBalance(0);
    map->UpdateHouse();
    map->SaveData(town);
    town->culture_point = map->CultureAnalyze(0);
    town->save_count += 1;
    AnalyzeEditMap(town_id, map);

    int saved_index = -1;
    int saved_count = 0;
    for (int slot = 0; slot < map->edit_parts_max; ++slot) {
        CEditParts *candidate = map->GetePlaceParts(slot);
        if (candidate == nullptr || candidate->name[0] == 0 || candidate->info == nullptr) continue;
        if (slot == part_slot) {
            saved_index = saved_count;
            break;
        }
        ++saved_count;
    }
    const TownLayout *saved_layout = PortFindTownLayout(*town);
    if (saved_layout == nullptr || saved_index < 0 || std::cmp_greater_equal(saved_index, saved_layout->parts.size()) ||
        saved_layout->parts[static_cast<std::size_t>(saved_index)].id != native_id) {
        return Failed("the native save did not retain the new Georama placement");
    }

    const TownPart &saved_part = saved_layout->parts[static_cast<std::size_t>(saved_index)];
    return {ApiCallStatus::Ok,
            {{"id", id},
             {"town_id", town_id},
             {"part_id", native_id},
             {"placement_index", saved_index},
             {"placement_id", "chronicle-two:georama-placement:" + std::to_string(town_id) + ":" +
                                  std::to_string(saved_index)},
             {"position", {{"x", saved_part.pos[0]}, {"y", saved_part.pos[1]}, {"z", saved_part.pos[2]}}},
             {"rotation_steps", saved_part.angle},
             {"remaining_built", save->GetBuildPartsNum(native_id)},
             {"changed", true}},
            {}};
}

ApiCallResult RemoveGeoramaPiece(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || args[0].size() != 1 ||
        !args[0].contains("placement_id") || !args[0]["placement_id"].is_string()) {
        return Invalid("expected {placement_id = value.id from game.georama.get()}");
    }

    const std::string &placement_id = args[0]["placement_id"].get_ref<const std::string &>();
    constexpr std::string_view prefix = "chronicle-two:georama-placement:";
    if (!placement_id.starts_with(prefix)) return Invalid("placement_id is not a Chronicle Two Georama placement id");
    const std::string_view suffix(placement_id.data() + prefix.size(), placement_id.size() - prefix.size());
    const std::size_t separator = suffix.find(':');
    if (separator == std::string_view::npos || suffix.find(':', separator + 1) != std::string_view::npos) {
        return Invalid("placement_id must identify one town and saved placement index");
    }
    int town_id = -1;
    int placement_index = -1;
    const auto town_parse = std::from_chars(suffix.data(), suffix.data() + separator, town_id);
    const auto index_parse = std::from_chars(suffix.data() + separator + 1, suffix.data() + suffix.size(), placement_index);
    if (town_parse.ec != std::errc{} || town_parse.ptr != suffix.data() + separator || town_id < 0 ||
        town_id >= SAVE_EDIT_DATA_MAX || index_parse.ec != std::errc{} ||
        index_parse.ptr != suffix.data() + suffix.size() || placement_index < 0) {
        return Invalid("placement_id contains an invalid town or placement index");
    }

    if (GetNowLoopNo() != LOOP_EDIT) return Failed("Georama removal requires an active town edit scene");
    CScene *scene = GetMainScene();
    if (scene == nullptr || scene->active_map < 0) return Failed("the active Georama map is unavailable");
    CMap *active_map = scene->GetMap(scene->active_map);
    if (active_map == nullptr || std::strcmp(active_map->Iam(), "CEditMap") != 0) {
        return Failed("the active scene map is not a Georama edit map");
    }
    auto *map = static_cast<CEditMap *>(active_map);
    if (map->area_no != town_id) return Failed("placement_id does not belong to the active Georama town");

    CSaveData *save = GetSaveData();
    if (save == nullptr) return Failed("the game has no active save data");
    CEditData *town = save->GetEditData(town_id);
    const TownLayout *saved_layout = town != nullptr ? PortFindTownLayout(*town) : nullptr;
    if (saved_layout == nullptr || placement_index < 0 || std::cmp_greater_equal(placement_index, saved_layout->parts.size())) {
        return Failed("placement_id is stale or the saved Georama layout is unavailable");
    }
    const TownPart &saved_part = saved_layout->parts[static_cast<std::size_t>(placement_index)];
    if (saved_part.id <= 0 || saved_part.id >= EDIT_REMOVE_INFO_ID_MAX || saved_part.state == 0) {
        return Failed("placement_id does not identify a placed Georama piece");
    }

    CEditParts *target = nullptr;
    int target_slot = -1;
    int saved_ordinal = 0;
    for (int slot = 0; slot < map->edit_parts_max; ++slot) {
        CEditParts *candidate = map->GetePlaceParts(slot);
        if (candidate == nullptr || candidate->name[0] == 0 || candidate->info == nullptr) continue;
        if (saved_ordinal == placement_index) {
            target = candidate;
            target_slot = slot;
            break;
        }
        ++saved_ordinal;
    }
    if (target == nullptr || target->GetInfoID() != saved_part.id || target->state != saved_part.state) {
        return Failed("placement_id no longer matches the live Georama layout");
    }
    if (target->info->attr & 0x1) return Failed("the selected Georama piece is fixed and the game does not allow it to be removed");
    if ((target->info->attr & EDIT_PARTS_ATR_RIVER) != 0 || target->GetPartsType() == EDIT_PARTS_TYPE_RIVER ||
        saved_part.state == EDIT_PARTS_STATE_RIVER) {
        return Failed("API removal currently supports solid Georama pieces; river pieces require the native grid editor");
    }

    float local_position[4]{};
    float world_position[4]{};
    float rotation[4]{};
    target->GetLocalPos(local_position);
    map->GetEditPos(world_position, local_position);
    target->GetRotation(rotation);
    if (map->ConvEditAngle(rotation[1]) != saved_part.angle ||
        static_cast<int>(world_position[0]) != saved_part.pos[0] ||
        static_cast<int>(world_position[1]) != saved_part.pos[1] ||
        static_cast<int>(world_position[2]) != saved_part.pos[2]) {
        return Failed("placement_id no longer matches the live Georama transform");
    }

    CEditMap::RemoveInfo removed{};
    float remove_position[4]{};
    const int removed_part_id = saved_part.id;
    target->GetPosition(remove_position);
    if (map->RemoveEditParts(target_slot, remove_position, &removed) == 0) {
        return Failed("the native Georama rules rejected removal of this piece");
    }

    map->focus_parts = -1;
    map->frame = 0;
    map->GroundBalance(1);
    map->UpdateHouse();

    Json removed_parts = Json::array();
    Json returned_stock = Json::array();
    for (int part_id = 0; part_id < EDIT_REMOVE_INFO_ID_MAX; ++part_id) {
        const int count = removed.parts_num[part_id];
        if (count <= 0) continue;
        removed_parts.push_back({{"part_id", part_id}, {"count", count}});
        CEditPartsInfo *info = map->GetePartsInfoAtID(part_id);
        if (info == nullptr || (info->attr & EDIT_PARTS_ATR_NO_REFUND) != 0) continue;
        const int previous = save->GetBuildPartsNum(part_id);
        const int resulting = save->AddBuildPartsNum(part_id, count);
        const int returned = std::max(0, resulting - previous);
        if (returned > 0) returned_stock.push_back({{"part_id", part_id}, {"count", returned}});
    }

    Json released_villagers = Json::array();
    const int house_count = std::clamp(removed.house_num, 0, static_cast<int>(EDIT_REMOVE_HOUSE_MAX));
    for (int index = 0; index < house_count; ++index) {
        const int npc_id = removed.house_npc[index];
        if (npc_id <= 0) continue;
        save->user_data.LeaveHouse(npc_id);
        const int character_index = scene->SearchCharaID(npc_id);
        if (character_index >= 0) scene->ResetActive(SCENE_DATA_CHARA, character_index);
        released_villagers.push_back(npc_id);
    }

    map->SaveData(town);
    town->culture_point = map->CultureAnalyze(0);
    town->save_count += 1;
    AnalyzeEditMap(town_id, map);
    return {ApiCallStatus::Ok,
            {{"placement_id", placement_id}, {"town_id", town_id}, {"part_id", removed_part_id},
             {"removed", true}, {"removed_parts", std::move(removed_parts)},
             {"returned_stock", std::move(returned_stock)}, {"released_villagers", std::move(released_villagers)},
             {"changed", true}},
            {}};
}

ApiCallResult SetGeoramaRiverCell(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || args[0].size() != 4 ||
        !args[0].contains("grid_index") || !args[0].contains("x") || !args[0].contains("z") ||
        !args[0].contains("has_river") || !args[0]["has_river"].is_boolean()) {
        return Invalid("expected {grid_index = 0-3, x = cell index, z = cell index, has_river = boolean}");
    }
    int grid_index = -1;
    int x = -1;
    int z = -1;
    if (!ReadInt(args[0], "grid_index", -1, 0, EDIT_MAP_GRID_MAX - 1, grid_index) ||
        !ReadInt(args[0], "x", -1, 0, 255, x) || !ReadInt(args[0], "z", -1, 0, 255, z)) {
        return Invalid("grid_index must be 0-3 and x/z must be nonnegative cell indices");
    }

    if (GetNowLoopNo() != LOOP_EDIT) return Failed("Georama river edits require an active town edit scene");
    CScene *scene = GetMainScene();
    if (scene == nullptr || scene->active_map < 0) return Failed("the active Georama map is unavailable");
    CMap *active_map = scene->GetMap(scene->active_map);
    if (active_map == nullptr || std::strcmp(active_map->Iam(), "CEditMap") != 0) {
        return Failed("the active scene map is not a Georama edit map");
    }
    auto *map = static_cast<CEditMap *>(active_map);
    if (map->area_no < 0 || map->area_no >= SAVE_EDIT_DATA_MAX || map->grid_max < 0 ||
        map->grid_max > EDIT_MAP_GRID_MAX || grid_index >= map->grid_max) {
        return Failed("the active Georama map has no river grid at that index");
    }
    CEditGrid *grid = map->grid[grid_index];
    if (grid == nullptr || grid->data == nullptr || grid->num_x <= 0 || grid->num_z <= 0 ||
        !grid->Check(x, z)) {
        return Failed("the selected Georama river cell is outside its loaded grid");
    }
    CSaveData *save = GetSaveData();
    if (save == nullptr) return Failed("the game has no active save data");
    CEditData *town = save->GetEditData(map->area_no);
    if (town == nullptr) return Failed("the saved Georama town data is unavailable");

    const bool previous = grid->River(x, z) != 0;
    const bool requested = args[0]["has_river"].get<bool>();
    if (previous == requested) {
        return {ApiCallStatus::Ok,
                {{"town_id", map->area_no}, {"grid_index", grid_index}, {"x", x}, {"z", z},
                 {"previous", previous}, {"has_river", previous}, {"changed", false}},
                {}};
    }
    const int changed = requested ? grid->SetRiver(x, z) : grid->ResetRiver(x, z);
    if (changed == 0 || (grid->River(x, z) != 0) != requested) {
        return Failed("the native Georama river grid rejected the cell update");
    }

    map->SaveData(town);
    town->culture_point = map->CultureAnalyze(0);
    town->save_count += 1;
    AnalyzeEditMap(map->area_no, map);
    return {ApiCallStatus::Ok,
            {{"town_id", map->area_no}, {"grid_index", grid_index}, {"x", x}, {"z", z},
             {"previous", previous}, {"has_river", requested}, {"changed", true}},
            {}};
}

ApiCallResult SetWorldTime(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || args[0].size() != 1 ||
        !args[0].contains("fraction") || !args[0]["fraction"].is_number()) {
        return Invalid("expected {fraction = number from 0 (inclusive) to 1 (exclusive)}");
    }
    const double fraction = args[0]["fraction"].get<double>();
    if (!std::isfinite(fraction) || fraction < 0.0 || fraction >= 1.0) {
        return Invalid("fraction must be finite and in the range [0, 1)");
    }
    CSaveData *save = GetSaveData();
    if (save == nullptr) return Failed("the game has no active save data");

    const float native_time = static_cast<float>(fraction * 24.0);
    const int loop = GetNowLoopNo();
    if (loop == LOOP_DUNGEON) {
        if (DngMainScene == nullptr) return Failed("the active dungeon scene is unavailable");
        DngMainScene->SetTime(native_time);
    } else if (loop == LOOP_EDIT) {
        CScene *scene = GetMainScene();
        if (scene != nullptr) {
            scene->SetTime(native_time);
        } else {
            save->now_time = native_time;
        }
    } else {
        save->now_time = native_time;
    }
    return {ApiCallStatus::Ok,
            {{"time", {{"fraction", fraction}, {"native_time", native_time}, {"native_cycle_hours", 24}}}},
            {}};
}

ApiCallResult GetStoryFlag(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || args[0].size() != 1 ||
        !args[0].contains("id")) {
        return Invalid("expected {id = story_flag_id from 0 to 2047}");
    }
    int flag_id = 0;
    if (!ReadInt(args[0], "id", 0, 0, SAVE_BIT_FLAG_MAX - 1, flag_id)) {
        return Invalid("story flag id must be an integer from 0 to 2047");
    }
    CSaveData *save = GetSaveData();
    if (save == nullptr) return Failed("the game has no active save data");
    return {ApiCallStatus::Ok, {{"id", flag_id}, {"value", save->GetBitFlag(flag_id) != 0}}, {}};
}

ApiCallResult SetStoryFlag(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || args[0].size() != 2 ||
        !args[0].contains("id") || !args[0].contains("value") || !args[0]["value"].is_boolean()) {
        return Invalid("expected {id = story_flag_id from 0 to 2047, value = boolean}");
    }
    int flag_id = 0;
    if (!ReadInt(args[0], "id", 0, 0, SAVE_BIT_FLAG_MAX - 1, flag_id)) {
        return Invalid("story flag id must be an integer from 0 to 2047");
    }
    CSaveData *save = GetSaveData();
    if (save == nullptr) return Failed("the game has no active save data");
    const bool previous = save->GetBitFlag(flag_id) != 0;
    const bool value = args[0]["value"].get<bool>();
    save->SetBitFlag(flag_id, value ? 1 : 0);
    return {ApiCallStatus::Ok,
            {{"id", flag_id}, {"previous", previous}, {"value", save->GetBitFlag(flag_id) != 0},
             {"changed", previous != value}},
            {}};
}

ApiCallResult GetStoryValue(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || args[0].size() != 1 ||
        !args[0].contains("id")) {
        return Invalid("expected {id = story_value_id from 0 to 127}");
    }
    int value_id = 0;
    if (!ReadInt(args[0], "id", 0, 0, SAVE_SHORT_FLAG_MAX - 1, value_id)) {
        return Invalid("story value id must be an integer from 0 to 127");
    }
    CSaveData *save = GetSaveData();
    if (save == nullptr) return Failed("the game has no active save data");
    return {ApiCallStatus::Ok, {{"id", value_id}, {"value", save->GetShortFlag(value_id)}}, {}};
}

ApiCallResult SetStoryValue(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || args[0].size() != 2 ||
        !args[0].contains("id") || !args[0].contains("value")) {
        return Invalid("expected {id = story_value_id from 0 to 127, value = signed 16-bit integer}");
    }
    int value_id = 0;
    int value = 0;
    if (!ReadInt(args[0], "id", 0, 0, SAVE_SHORT_FLAG_MAX - 1, value_id) ||
        !ReadInt(args[0], "value", 0, -32768, 32767, value)) {
        return Invalid("story value id must be 0-127 and value must fit a signed 16-bit integer");
    }
    CSaveData *save = GetSaveData();
    if (save == nullptr) return Failed("the game has no active save data");
    const int previous = save->GetShortFlag(value_id);
    save->SetShortFlag(value_id, static_cast<short>(value));
    return {ApiCallStatus::Ok,
            {{"id", value_id}, {"previous", previous}, {"value", save->GetShortFlag(value_id)},
             {"changed", previous != value}},
            {}};
}

ApiCallResult GetMapFlag(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || args[0].size() != 2 ||
        !args[0].contains("map_id") || !args[0].contains("flag_id")) {
        return Invalid("expected {map_id = 0-255, flag_id = 0-127}");
    }
    int map_id = 0;
    int flag_id = 0;
    if (!ReadInt(args[0], "map_id", 0, 0, SAVE_MAP_FLAG_MAX - 1, map_id) ||
        !ReadInt(args[0], "flag_id", 0, 0, MAP_FLAG_MAX - 1, flag_id)) {
        return Invalid("map_id must be 0-255 and flag_id must be 0-127");
    }
    CSaveData *save = GetSaveData();
    if (save == nullptr) return Failed("the game has no active save data");
    CMapFlagData *map_flags = save->GetMapFlag(map_id);
    if (map_flags == nullptr) return Failed("the native map flag record is unavailable");
    return {ApiCallStatus::Ok,
            {{"map_id", map_id}, {"flag_id", flag_id}, {"value", map_flags->GetFlag(flag_id) != 0}}, {}};
}

ApiCallResult SetMapFlag(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || args[0].size() != 3 ||
        !args[0].contains("map_id") || !args[0].contains("flag_id") || !args[0].contains("value") ||
        !args[0]["value"].is_boolean()) {
        return Invalid("expected {map_id = 0-255, flag_id = 0-127, value = boolean}");
    }
    int map_id = 0;
    int flag_id = 0;
    if (!ReadInt(args[0], "map_id", 0, 0, SAVE_MAP_FLAG_MAX - 1, map_id) ||
        !ReadInt(args[0], "flag_id", 0, 0, MAP_FLAG_MAX - 1, flag_id)) {
        return Invalid("map_id must be 0-255 and flag_id must be 0-127");
    }
    CSaveData *save = GetSaveData();
    if (save == nullptr) return Failed("the game has no active save data");
    CMapFlagData *map_flags = save->GetMapFlag(map_id);
    if (map_flags == nullptr) return Failed("the native map flag record is unavailable");
    const bool previous = map_flags->GetFlag(flag_id) != 0;
    const bool value = args[0]["value"].get<bool>();
    map_flags->SetFlag(flag_id, value ? 1 : 0);
    return {ApiCallStatus::Ok,
            {{"map_id", map_id}, {"flag_id", flag_id}, {"previous", previous},
             {"value", map_flags->GetFlag(flag_id) != 0}, {"changed", previous != value}},
            {}};
}

ApiCallStatus ResolveItemId(std::string_view id, int &item_no, std::string &error) {
    const auto mod_item = g_items.find(std::string(id));
    if (mod_item != g_items.end()) {
        if (mod_item->second.native_item_no < 0) {
            error = "item registration is waiting for the game item tables";
            return ApiCallStatus::PhaseViolation;
        }
        item_no = mod_item->second.native_item_no;
        return ApiCallStatus::Ok;
    }
    const auto mod_weapon = g_weapons.find(std::string(id));
    if (mod_weapon != g_weapons.end()) {
        if (mod_weapon->second.native_item_no < 0) {
            error = "weapon registration is waiting for the game weapon tables";
            return ApiCallStatus::PhaseViolation;
        }
        item_no = mod_weapon->second.native_item_no;
        return ApiCallStatus::Ok;
    }
    if (!NativeItemTablesValid()) {
        error = "the native item tables are unavailable or out of range";
        return ApiCallStatus::Failed;
    }
    if (ReadNativeItemId(Json(std::string(id)), item_no)) {
        CDataCommon *common = GameItemDataManage.GetCommonData(item_no);
        if (common == nullptr || IsWeaponCommon(*common)) {
            error = "no non-weapon item has that stable id";
            return ApiCallStatus::Failed;
        }
        return ApiCallStatus::Ok;
    }
    if (!ReadNativeWeaponId(Json(std::string(id)), item_no) || !NativeWeaponTablesValid() ||
        FindWeaponCommon(item_no) == nullptr) {
        error = "no built-in or mod item or weapon is registered with that stable id";
        return ApiCallStatus::Failed;
    }
    return ApiCallStatus::Ok;
}

ApiCallResult RegisterItem(std::string_view mod_id, const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) {
        return Invalid("expected one item definition object");
    }
    const Json &definition = args[0];
    const auto id_it = definition.find("id");
    const auto name_it = definition.find("name");
    if (id_it == definition.end() || !id_it->is_string() || name_it == definition.end() || !name_it->is_string()) {
        return Invalid("item definition requires string id and name fields");
    }

    std::string local_id = id_it->get<std::string>();
    std::string qualified_id;
    const std::size_t separator = local_id.find(':');
    if (separator == std::string::npos) {
        if (!IsIdentifier(local_id)) {
            return Invalid("item id must be a short identifier using letters, digits, '.', '_' or '-'");
        }
        qualified_id = std::string(mod_id) + ":" + local_id;
    } else {
        if (local_id.find(':', separator + 1) != std::string::npos || local_id.substr(0, separator) != mod_id ||
            !IsIdentifier(std::string_view(local_id).substr(separator + 1))) {
            return Invalid("fully qualified item ids must use this mod's namespace");
        }
        qualified_id = std::move(local_id);
    }
    const std::string &display_name = name_it->get_ref<const std::string &>();
    if (display_name.empty() || display_name.size() > 95) {
        return Invalid("item name must contain 1-95 UTF-8 bytes");
    }
    const auto kind_it = definition.find("kind");
    if (kind_it != definition.end() && (!kind_it->is_string() || kind_it->get<std::string>() != "consumable")) {
        return Invalid("Chronicle Two currently accepts data-only consumable items");
    }
    const auto assets_it = definition.find("assets");
    if (assets_it != definition.end() &&
        ((!assets_it->is_object() && !assets_it->is_array()) || !assets_it->empty())) {
        return Invalid("this item registration reuses a vanilla asset template; asset overrides are not accepted");
    }

    ModItem item;
    item.id = qualified_id;
    item.owner = mod_id;
    item.name = display_name;
    if (!ReadInt(definition, "template_item", 0, 1, ITEM_NO_MAX - 1, item.template_item)) {
        return Invalid("template_item must be an existing item number");
    }
    if (!ReadInt(definition, "max_count", 99, 1, 999, item.max_num) ||
        !ReadInt(definition, "max_stack", std::min(10, item.max_num), 1, item.max_num, item.stack_num)) {
        return Invalid("max_count and max_stack must satisfy 1 <= max_stack <= max_count <= 999");
    }
    const auto effect_it = definition.find("effect");
    if (effect_it != definition.end() && !effect_it->is_null()) {
        if (!effect_it->is_object()) {
            return Invalid("effect must be an object");
        }
        const auto effect_kind = effect_it->find("kind");
        if (effect_kind == effect_it->end() || !effect_kind->is_string()) {
            return Invalid("effect.kind must be 'heal', 'power', 'cure_status', or 'cure_all'");
        }
        item.effect_kind = effect_kind->get<std::string>();
        if (item.effect_kind == "heal") {
            if (!ReadInt(*effect_it, "amount", 0, 1, 32767, item.effect_amount) || effect_it->size() != 2) {
                return Invalid("healing effect requires only an integer amount from 1 to 32767");
            }
            item.effect_flags = ITEM_USE_FLAG_RESTORE_HP;
        } else if (item.effect_kind == "cure_status") {
            const auto status = effect_it->find("status");
            if (status == effect_it->end() || !status->is_string() || effect_it->size() != 2) {
                return Invalid("cure_status effect requires only one supported status name");
            }
            item.effect_status = status->get<std::string>();
            if (item.effect_status == "poison") item.effect_flags = ITEM_USE_FLAG_CURE_POISON;
            else if (item.effect_status == "slow") item.effect_flags = ITEM_USE_FLAG_CURE_SLOW;
            else if (item.effect_status == "stop") item.effect_flags = ITEM_USE_FLAG_CURE_STOP;
            else if (item.effect_status == "curse") item.effect_flags = ITEM_USE_FLAG_CURE_CURSE;
            else if (item.effect_status == "stone") item.effect_flags = ITEM_USE_FLAG_CURE_STONE;
            else if (item.effect_status == "dry") item.effect_flags = ITEM_USE_FLAG_CURE_DRY;
            else return Invalid("cure_status status must be poison, slow, stop, curse, stone, or dry");
        } else if (item.effect_kind == "cure_all") {
            if (effect_it->size() != 1) return Invalid("cure_all takes no fields");
            item.effect_flags = ITEM_USE_FLAG_CURE_POISON | ITEM_USE_FLAG_CURE_SLOW | ITEM_USE_FLAG_CURE_STOP |
                                ITEM_USE_FLAG_CURE_CURSE | ITEM_USE_FLAG_CURE_STONE | ITEM_USE_FLAG_CURE_DRY;
        } else if (item.effect_kind == "power") {
            if (effect_it->size() != 1) return Invalid("power effect takes no fields");
            item.effect_flags = ITEM_USE_FLAG_ADD_POWER;
        } else {
            return Invalid("the native item adapter supports effect.kind = 'heal', 'power', 'cure_status', or 'cure_all'");
        }
    }
    if (g_items.contains(item.id)) {
        return Invalid("an item with this stable id is already registered");
    }
    if (g_items.size() >= kMaximumRegisteredItems) {
        return Failed("Chronicle Two's current item adapter supports up to 32 mod items");
    }

    const std::string id = item.id;
    g_items.emplace(id, std::move(item));
    Json result = ItemJson(g_items.at(id));
    result["id"] = id;
    return {ApiCallStatus::Ok, std::move(result), {}};
}

ApiCallResult RegisterWeapon(std::string_view mod_id, const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) {
        return Invalid("expected one weapon definition object");
    }
    const Json &definition = args[0];
    const auto id_it = definition.find("id");
    const auto name_it = definition.find("name");
    if (id_it == definition.end() || !id_it->is_string() || name_it == definition.end() || !name_it->is_string()) {
        return Invalid("weapon definition requires string id and name fields");
    }

    std::string local_id = id_it->get<std::string>();
    std::string qualified_id;
    const std::size_t separator = local_id.find(':');
    if (separator == std::string::npos) {
        if (!IsIdentifier(local_id)) {
            return Invalid("weapon id must be a short identifier using letters, digits, '.', '_' or '-'");
        }
        qualified_id = std::string(mod_id) + ":" + local_id;
    } else {
        if (local_id.find(':', separator + 1) != std::string::npos || local_id.substr(0, separator) != mod_id ||
            !IsIdentifier(std::string_view(local_id).substr(separator + 1))) {
            return Invalid("fully qualified weapon ids must use this mod's namespace");
        }
        qualified_id = std::move(local_id);
    }
    const std::string &display_name = name_it->get_ref<const std::string &>();
    if (display_name.empty() || display_name.size() > 95) {
        return Invalid("weapon name must contain 1-95 UTF-8 bytes");
    }
    const auto assets_it = definition.find("assets");
    if (assets_it != definition.end() &&
        ((!assets_it->is_object() && !assets_it->is_array()) || !assets_it->empty())) {
        return Invalid("this weapon registration reuses a vanilla asset template; asset overrides are not accepted");
    }

    ModWeapon weapon;
    weapon.id = qualified_id;
    weapon.owner = mod_id;
    weapon.name = display_name;
    if (!ReadInt(definition, "template_weapon", 0, 1, ITEM_NO_MAX - 1, weapon.template_item)) {
        return Invalid("template_weapon must be an existing weapon item number");
    }
    if (!ReadInt(definition, "max_count", 1, 1, 999, weapon.max_num) ||
        !ReadInt(definition, "stack_size", 1, 1, weapon.max_num, weapon.stack_num)) {
        return Invalid("max_count and stack_size must satisfy 1 <= stack_size <= max_count <= 999");
    }
    if (!ReadInt(definition, "durability_capacity", -1, -1, 32767, weapon.durability_capacity) ||
        !ReadInt(definition, "level_up_exp", -1, -1, 32767, weapon.levelup_exp)) {
        return Invalid("durability_capacity and level_up_exp must be integers from 0 to 32767");
    }

    const auto stats_it = definition.find("stats");
    const auto caps_it = definition.find("stat_caps");
    if ((stats_it != definition.end() && !stats_it->is_object()) ||
        (caps_it != definition.end() && !caps_it->is_object())) {
        return Invalid("stats and stat_caps must be objects");
    }
    const Json empty_object = Json::object();
    const Json &stats = stats_it == definition.end() ? empty_object : *stats_it;
    const Json &caps = caps_it == definition.end() ? empty_object : *caps_it;
    if (!ReadInt(stats, "attack", -1, -1, 32767, weapon.attack) ||
        !ReadInt(stats, "durability", -1, -1, 32767, weapon.durability_stat) ||
        !ReadInt(caps, "attack", -1, -1, 32767, weapon.attack_cap) ||
        !ReadInt(caps, "durability", -1, -1, 32767, weapon.durability_cap)) {
        return Invalid("weapon attack and durability values must be integers from 0 to 32767");
    }
    const auto attributes_it = stats.find("attributes");
    const auto attribute_caps_it = caps.find("attributes");
    if ((attributes_it == stats.end()) != (attribute_caps_it == caps.end())) {
        return Invalid("stats.attributes and stat_caps.attributes must be supplied together");
    }
    if (attributes_it != stats.end()) {
        if (!ReadWeaponAttributeArray(*attributes_it, weapon.attributes) ||
            !ReadWeaponAttributeArray(*attribute_caps_it, weapon.attribute_caps)) {
            return Invalid("weapon attributes and caps must each contain eight integers from 0 to 32767");
        }
        weapon.has_attributes = true;
    }
    if (g_weapons.contains(weapon.id)) {
        return Invalid("a weapon with this stable id is already registered");
    }
    if (g_weapons.size() >= kMaximumRegisteredWeapons) {
        return Failed("Chronicle Two's current weapon adapter supports up to 32 mod weapons");
    }

    const std::string id = weapon.id;
    g_weapons.emplace(id, std::move(weapon));
    return {ApiCallStatus::Ok, ModWeaponJson(g_weapons.at(id)), {}};
}

ApiCallResult RegisterDungeon(std::string_view mod_id, const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) {
        return Invalid("expected one dungeon definition object");
    }
    const Json &definition = args[0];
    const auto id_it = definition.find("id");
    const auto name_it = definition.find("name");
    const auto floors_it = definition.find("floors");
    if (id_it == definition.end() || !id_it->is_string() || name_it == definition.end() || !name_it->is_string() ||
        floors_it == definition.end() || !floors_it->is_array() || floors_it->empty()) {
        return Invalid("dungeon definition requires string id/name and a non-empty floors array");
    }
    std::string local_id = id_it->get<std::string>();
    std::string qualified_id;
    const std::size_t separator = local_id.find(':');
    if (separator == std::string::npos) {
        if (!IsIdentifier(local_id)) {
            return Invalid("dungeon id must be a short identifier using letters, digits, '.', '_' or '-'");
        }
        qualified_id = std::string(mod_id) + ":" + local_id;
    } else {
        if (local_id.find(':', separator + 1) != std::string::npos || local_id.substr(0, separator) != mod_id ||
            !IsIdentifier(std::string_view(local_id).substr(separator + 1))) {
            return Invalid("fully qualified dungeon ids must use this mod's namespace");
        }
        qualified_id = std::move(local_id);
    }
    const std::string &display_name = name_it->get_ref<const std::string &>();
    if (display_name.empty() || display_name.size() > 95) {
        return Invalid("dungeon name must contain 1-95 UTF-8 bytes");
    }
    const auto assets_it = definition.find("assets");
    if (assets_it != definition.end() &&
        ((!assets_it->is_object() && !assets_it->is_array()) || !assets_it->empty())) {
        return Invalid("the first floor adapter reuses an existing dungeon and room resource; asset overrides are not accepted");
    }
    int dungeon_no = -1;
    if (!ReadInt(definition, "dungeon", -1, 0, DNGMAP_DUNGEON_MAX, dungeon_no)) {
        return Invalid("dungeon must identify an existing Chronicle Two dungeon from 0 through 6");
    }
    if (floors_it->size() > kMaximumRegisteredDungeonFloors) {
        return Failed("Chronicle Two's current dungeon adapter supports up to 8 added floors per registration");
    }
    if (g_dungeons.contains(qualified_id)) {
        return Invalid("a mod dungeon with this stable id is already registered");
    }

    ModDungeon dungeon;
    dungeon.id = qualified_id;
    dungeon.owner = mod_id;
    dungeon.name = display_name;
    dungeon.dungeon = dungeon_no;
    for (const Json &floor_definition : *floors_it) {
        if (!floor_definition.is_object()) {
            return Invalid("each floor definition must be an object");
        }
        const auto floor_id_it = floor_definition.find("id");
        if (floor_id_it == floor_definition.end() || !floor_id_it->is_string()) {
            return Invalid("each added floor requires a local id");
        }
        const std::string local_floor_id = floor_id_it->get<std::string>();
        if (!IsIdentifier(local_floor_id)) {
            return Invalid("floor ids must use letters, digits, '.', '_' or '-'");
        }
        const std::string floor_name = floor_definition.value("name", display_name + " / " + local_floor_id);
        if (floor_name.empty() || floor_name.size() > 95) {
            return Invalid("floor name must contain 1-95 UTF-8 bytes");
        }
        int source_floor = -1;
        std::string source_floor_ref;
        const auto source_floor_it = floor_definition.find("source_floor");
        if (source_floor_it == floor_definition.end()) {
            return Invalid("each added floor requires a native floor number or earlier local floor id in source_floor");
        }
        if (source_floor_it->is_number_integer()) {
            if (!ReadInt(floor_definition, "source_floor", -1, 0, DNGMAP_FLOOR_NUM - 1, source_floor)) {
                return Invalid("numeric source_floor must identify an existing dungeon floor from 0 through the native maximum");
            }
        } else if (source_floor_it->is_string()) {
            source_floor_ref = source_floor_it->get<std::string>();
            const bool earlier_floor_exists = IsIdentifier(source_floor_ref) &&
                std::ranges::any_of(dungeon.floors, [&source_floor_ref](const ModDungeonFloor &floor) {
                    return floor.local_id == source_floor_ref;
                });
            if (!earlier_floor_exists) {
                return Invalid("string source_floor must name an earlier floor in this dungeon registration");
            }
        } else {
            return Invalid("source_floor must be an existing native floor number or an earlier local floor id");
        }
        const auto floor_assets = floor_definition.find("assets");
        if (floor_assets != floor_definition.end() &&
            ((!floor_assets->is_object() && !floor_assets->is_array()) || !floor_assets->empty())) {
            return Invalid("floor resource overrides are not accepted by this adapter");
        }
        std::uint32_t room_flags = 0;
        const auto flags_it = floor_definition.find("flags");
        if (flags_it != floor_definition.end()) {
            if (!flags_it->is_array() || flags_it->size() > 3) {
                return Invalid("floor flags must be an array containing exit, boss, or sub");
            }
            for (const Json &flag : *flags_it) {
                if (!flag.is_string()) return Invalid("floor flags must contain only exit, boss, or sub");
                const std::string &name = flag.get_ref<const std::string &>();
                const std::uint32_t bit = name == "exit" ? DNGMAP_ROOM_FLAG_EXIT
                                          : name == "boss" ? DNGMAP_ROOM_FLAG_BOSS
                                          : name == "sub" ? DNGMAP_ROOM_FLAG_SUB
                                                           : 0;
                if (bit == 0 || (room_flags & bit) != 0) {
                    return Invalid("floor flags must be unique values from exit, boss, or sub");
                }
                room_flags |= bit;
            }
        }
        int seal = -1;
        const auto seal_it = floor_definition.find("seal");
        if (seal_it != floor_definition.end()) {
            if (!seal_it->is_string()) return Invalid("floor seal must be none, monica, or max");
            const std::string &name = seal_it->get_ref<const std::string &>();
            if (name == "none") seal = DNGMAP_SEAL_NONE;
            else if (name == "monica") seal = DNGMAP_SEAL_MONICA;
            else if (name == "max") seal = DNGMAP_SEAL_MAX;
            else return Invalid("floor seal must be none, monica, or max");
        }
        const std::string stable_floor_id = qualified_id + "." + local_floor_id;
        if (std::ranges::any_of(dungeon.floors, [&stable_floor_id](const ModDungeonFloor &floor) {
                return floor.id == stable_floor_id;
            })) {
            return Invalid("floor ids must be unique within the dungeon");
        }
        dungeon.floors.push_back({stable_floor_id, local_floor_id, floor_name, source_floor, source_floor_ref,
                                  room_flags, seal, -1});
    }
    std::size_t total_floors = dungeon.floors.size();
    for (const auto &[id, registered] : g_dungeons) {
        (void)id;
        total_floors += registered.floors.size();
    }
    if (total_floors > kMaximumRegisteredDungeonFloors) {
        return Failed("Chronicle Two's current dungeon adapter supports up to 8 added floors per session");
    }

    const std::string id = dungeon.id;
    g_dungeons.emplace(id, std::move(dungeon));
    return {ApiCallStatus::Ok, DungeonJson(g_dungeons.at(id)), {}};
}

ApiCallResult ListDungeons(const Json &args) {
    if (!args.is_array() || !args.empty()) return Invalid("dungeons.list takes no arguments");
    Json result = Json::array();
    for (const auto &[id, dungeon] : g_dungeons) {
        (void)id;
        result.push_back(DungeonJson(dungeon));
    }
    return {ApiCallStatus::Ok, std::move(result), {}};
}

ApiCallResult GetDungeon(std::string_view mod_id, const Json &args) {
    if (!args.is_array() || args.size() != 1) return Invalid("expected one dungeon id or {id = dungeon_id}");
    std::string id;
    if (!ReadId(args[0], id)) return Invalid("expected a stable registered dungeon id");
    if (id.find(':') == std::string::npos) {
        if (!IsIdentifier(id)) return Invalid("local dungeon id must use letters, digits, '.', '_' or '-'");
        id = std::string(mod_id) + ":" + id;
    }
    const auto found = g_dungeons.find(id);
    if (found == g_dungeons.end()) return Failed("no mod-registered dungeon addition has this id");
    return {ApiCallStatus::Ok, DungeonJson(found->second), {}};
}

ApiCallResult BindInputAction(std::string_view mod_id, const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) {
        return Invalid("expected {id = local_action_id, keys = {physical_input, ...}, context = optional local_context_id}");
    }
    const Json &definition = args[0];
    for (auto field = definition.begin(); field != definition.end(); ++field) {
        if (field.key() != "id" && field.key() != "keys" && field.key() != "context") {
            return Invalid("input.bind received an unsupported field");
        }
    }
    const auto id = definition.find("id");
    const auto keys = definition.find("keys");
    if (id == definition.end() || !id->is_string() || !IsIdentifier(id->get_ref<const std::string &>()) ||
        keys == definition.end() || !keys->is_array() || keys->empty() || keys->size() > 8) {
        return Invalid("id must be a local identifier and keys must contain 1-8 physical inputs");
    }
    std::string context = "default";
    if (definition.contains("context")) {
        const Json &context_value = definition["context"];
        if (!context_value.is_string() || !IsIdentifier(context_value.get_ref<const std::string &>())) {
            return Invalid("context must be a local identifier");
        }
        if (context_value != "default") context = std::string(mod_id) + ":" + context_value.get<std::string>();
    }
    std::vector<std::string_view> names;
    Json result_names = Json::array();
    for (const Json &key : *keys) {
        if (!key.is_string() || key.get_ref<const std::string &>().empty() || key.get_ref<const std::string &>().size() > 64) {
            return Invalid("input names must be non-empty strings of at most 64 bytes");
        }
        names.push_back(key.get_ref<const std::string &>());
        result_names.push_back(key);
    }
    const std::string qualified = std::string(mod_id) + ":" + id->get<std::string>();
    if (!InputBindModAction(qualified, names)) {
        return Invalid("keys must name supported keyboard, mouse-button, or gamepad-button inputs; Escape is reserved");
    }
    g_input_contexts.BindAction(qualified, context);
    return {ApiCallStatus::Ok, {{"id", qualified}, {"keys", std::move(result_names)}, {"context", context}}, {}};
}

ApiCallResult ReadInputAction(std::string_view mod_id, const Json &args, bool consume) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) {
        return Invalid("expected {id = local_action_id}");
    }
    const auto id = args[0].find("id");
    if (id == args[0].end() || !id->is_string() || !IsIdentifier(id->get_ref<const std::string &>())) {
        return Invalid("expected a local action id");
    }
    const std::string qualified = std::string(mod_id) + ":" + id->get<std::string>();
    if (!InputModActionExists(qualified)) return Invalid("input action is not registered by this mod");
    const std::string context = g_input_contexts.ContextFor(qualified);
    const bool active = g_input_contexts.IsActive(qualified);
    if (consume) {
        bool pressed = false;
        if (active) {
            pressed = InputModActionConsumePress(qualified);
        } else {
            while (InputModActionConsumePress(qualified)) {}
        }
        return {ApiCallStatus::Ok, {{"id", qualified}, {"context", context}, {"active", active}, {"pressed", pressed}}, {}};
    }
    return {ApiCallStatus::Ok,
            {{"id", qualified}, {"context", context}, {"active", active},
             {"down", active && InputModActionHeld(qualified)}}, {}};
}

ApiCallResult PushInputContext(std::string_view mod_id, const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || args[0].size() != 1 ||
        !args[0].contains("id") || !args[0]["id"].is_string()) {
        return Invalid("expected {id = local_context_id}");
    }
    const std::string &local_id = args[0]["id"].get_ref<const std::string &>();
    if (!IsIdentifier(local_id) || local_id == "default") return Invalid("id must be a local context identifier other than 'default'");
    std::string token;
    std::string qualified_id;
    if (!g_input_contexts.Push(mod_id, local_id, token, qualified_id)) return Failed("the input context stack is full");
    return {ApiCallStatus::Ok,
            {{"id", qualified_id}, {"token", token}, {"active", g_input_contexts.Active()},
             {"depth", g_input_contexts.stack.size()}}, {}};
}

ApiCallResult PopInputContext(std::string_view mod_id, const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || args[0].size() != 1 ||
        !args[0].contains("token") || !args[0]["token"].is_string()) {
        return Invalid("expected {token = token_returned_by_context_push}");
    }
    const std::string &token = args[0]["token"].get_ref<const std::string &>();
    if (token.empty() || token.size() > 64) return Invalid("token must be a non-empty context token");
    std::string removed_id;
    if (!g_input_contexts.Pop(mod_id, token, removed_id)) return Invalid("context token is missing or owned by another mod");
    return {ApiCallStatus::Ok,
            {{"id", removed_id}, {"active", g_input_contexts.Active()}, {"depth", g_input_contexts.stack.size()}}, {}};
}

ApiCallResult GetInputContext(const Json &args) {
    if (!args.is_array() || !args.empty()) return Invalid("input.context_get takes no arguments");
    Json stack = Json::array();
    for (const InputContextFrame &frame : g_input_contexts.stack) {
        stack.push_back({{"id", frame.id}, {"token", frame.token}, {"owner", frame.owner}});
    }
    return {ApiCallStatus::Ok,
            {{"active", g_input_contexts.Active()}, {"depth", stack.size()}, {"stack", std::move(stack)}}, {}};
}

ApiCallResult ReadInputAxis(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) {
        return Invalid("expected {axis = left_x|left_y|right_x|right_y|left_trigger|right_trigger, pad = optional 0 or 1}");
    }
    for (auto field = args[0].begin(); field != args[0].end(); ++field) {
        if (field.key() != "axis" && field.key() != "pad") return Invalid("input.axis received an unsupported field");
    }
    const auto axis = args[0].find("axis");
    if (axis == args[0].end() || !axis->is_string()) {
        return Invalid("axis must name one of the four stick axes or two trigger axes");
    }
    int pad = 0;
    if (!ReadInt(args[0], "pad", 0, 0, kInputPadCount - 1, pad)) {
        return Invalid("pad must be an integer from 0 through 1");
    }
    const std::string &name = axis->get_ref<const std::string &>();
    const InputPadState &state = InputGetPad(pad);
    std::uint8_t value = kInputAxisCentre;
    bool trigger = false;
    if (name == "left_x") value = state.left_x;
    else if (name == "left_y") value = state.left_y;
    else if (name == "right_x") value = state.right_x;
    else if (name == "right_y") value = state.right_y;
    else if (name == "left_trigger") {
        value = state.left_trigger;
        trigger = true;
    } else if (name == "right_trigger") {
        value = state.right_trigger;
        trigger = true;
    } else return Invalid("axis must name one of the four stick axes or two trigger axes");
    const float normalized = trigger ? static_cast<float>(value) / 255.0f
                             : value == kInputAxisCentre ? 0.0f
                             : value < kInputAxisCentre
                                 ? static_cast<float>(static_cast<int>(value) - kInputAxisCentre) / kInputAxisCentre
                                 : static_cast<float>(static_cast<int>(value) - kInputAxisCentre) /
                                       (255 - kInputAxisCentre);
    return {ApiCallStatus::Ok,
            {{"axis", name}, {"pad", pad}, {"connected", state.connected}, {"value", normalized}}, {}};
}

ApiCallResult ReadDungeonCamera(const Json &args) {
    if (!args.is_array() || !args.empty()) return Invalid("render.camera_get takes no arguments");
    if (GetNowLoopNo() != LOOP_DUNGEON || DngMainScene == nullptr || DngMainScene->active_camera < 0) {
        return Failed("there is no active Chronicle Two dungeon camera");
    }
    const int camera_slot = DngMainScene->active_camera;
    mgCCamera *camera = DngMainScene->GetCamera(camera_slot);
    if (camera == nullptr) return Failed("the active dungeon camera slot is empty");
    float eye[4]{};
    float target[4]{};
    camera->GetPos(eye);
    camera->GetRef(target);
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(eye[axis]) || !std::isfinite(target[axis])) {
            return Failed("the native camera pose contains a non-finite coordinate");
        }
    }
    const float heading = camera->GetAngleH();
    const float pitch = camera->GetAngleV();
    const float roll = camera->roll;
    if (!std::isfinite(heading) || !std::isfinite(pitch) || !std::isfinite(roll)) {
        return Failed("the native camera pose contains a non-finite angle");
    }
    return {ApiCallStatus::Ok,
            {{"scene", "dungeon"},
             {"eye", {{"x", eye[0]}, {"y", eye[1]}, {"z", eye[2]}}},
             {"target", {{"x", target[0]}, {"y", target[1]}, {"z", target[2]}}},
             {"heading", heading}, {"pitch", pitch}, {"roll", roll}},
            {}};
}

ApiCallResult SetDungeonCamera(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || args[0].empty()) {
        return Invalid("expected {distance?, height?, angle?} with at least one camera field");
    }
    const Json &definition = args[0];
    for (auto field = definition.begin(); field != definition.end(); ++field) {
        if (field.key() != "distance" && field.key() != "height" && field.key() != "angle") {
            return Invalid("render.camera_set only accepts distance, height, and angle");
        }
    }
    if (GetNowLoopNo() != LOOP_DUNGEON || DngMainScene == nullptr || DngMainScene->active_camera < 0) {
        return Failed("there is no active Chronicle Two dungeon camera");
    }
    mgCCamera *const camera = DngMainScene->GetCamera(DngMainScene->active_camera);
    if (camera == nullptr) return Failed("the active dungeon camera slot is empty");
    mgCCameraFollow *follow = nullptr;
    CCameraControl *controlled = nullptr;
    if (camera->Iam() == MG_CAMERA_KIND_FOLLOW) {
        follow = static_cast<mgCCameraFollow *>(camera);
    } else if (camera->Iam() == CAMERA_KIND_CONTROL) {
        controlled = static_cast<CCameraControl *>(camera);
        follow = controlled;
    } else {
        return Failed("the active Chronicle Two camera is not an orbit follow camera");
    }
    if (follow->follow_on == 0 && (controlled == nullptr || controlled->control_on == 0)) {
        return Failed("the active Chronicle Two camera is not in follow or player-controlled mode");
    }

    std::optional<float> distance;
    std::optional<float> height;
    std::optional<float> angle;
    const auto read_bounded = [&](std::string_view key, float minimum, float maximum,
                                  std::optional<float> &value) -> ApiCallResult {
        const auto found = definition.find(std::string(key));
        if (found == definition.end()) return {ApiCallStatus::Ok, nullptr, {}};
        if (!found->is_number()) return Invalid("camera fields must be finite numbers");
        const float parsed = found->get<float>();
        if (!std::isfinite(parsed) || parsed < minimum || parsed > maximum) {
            return Invalid("camera field '" + std::string(key) + "' is outside its safe range");
        }
        value = parsed;
        return {ApiCallStatus::Ok, nullptr, {}};
    };
    ApiCallResult parsed = read_bounded("distance", 8.0f, 300.0f, distance);
    if (parsed.status != ApiCallStatus::Ok) return parsed;
    parsed = read_bounded("height", -80.0f, 150.0f, height);
    if (parsed.status != ApiCallStatus::Ok) return parsed;
    parsed = read_bounded("angle", -1000.0f, 1000.0f, angle);
    if (parsed.status != ApiCallStatus::Ok) return parsed;
    const float current_distance = controlled != nullptr && controlled->control_on != 0
        ? std::hypot(controlled->next_pos[0] - controlled->next_ref[0],
                     controlled->next_pos[2] - controlled->next_ref[2])
        : follow->distance;
    const float current_height = controlled != nullptr && controlled->control_on != 0
        ? controlled->next_pos[1] - controlled->next_ref[1]
        : follow->height;
    const float current_angle = follow->next_angle;
    if (!std::isfinite(current_distance) || !std::isfinite(current_height) || !std::isfinite(current_angle)) {
        return Failed("the native camera orbit contains a non-finite value");
    }
    const float requested_angle = angle
        ? static_cast<float>(std::remainder(*angle, 6.2831853071795864769))
        : current_angle;
    if (controlled != nullptr && controlled->control_on != 0) {
        if (controlled->active_param < 0 || controlled->active_param >= 4) {
            return Failed("the active native camera has an invalid parameter slot");
        }
        CameraCtrlParam *const limits = controlled->GetActiveParam();
        if (!std::isfinite(limits->min_dist) || !std::isfinite(limits->max_dist) ||
            !std::isfinite(limits->min_height) || !std::isfinite(limits->max_height) ||
            limits->min_dist > limits->max_dist || limits->min_height > limits->max_height) {
            return Failed("the active native camera has invalid distance or height limits");
        }
        if (distance && (*distance < limits->min_dist || *distance > limits->max_dist)) {
            return Invalid("distance is outside the active native camera's limits");
        }
        if (height && (*height < limits->min_height || *height > limits->max_height)) {
            return Invalid("height is outside the active native camera's limits");
        }
        for (int axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(controlled->next_pos[axis]) || !std::isfinite(controlled->next_ref[axis])) {
                return Failed("the active native camera target contains a non-finite coordinate");
            }
        }
        if (distance) {
            if (current_distance <= 0.0001f) {
                const float base_angle = angle ? requested_angle : current_angle;
                controlled->next_pos[0] = controlled->next_ref[0] + *distance * std::sin(base_angle);
                controlled->next_pos[2] = controlled->next_ref[2] + *distance * std::cos(base_angle);
            } else {
                const float scale = *distance / current_distance;
                controlled->next_pos[0] = controlled->next_ref[0] +
                    (controlled->next_pos[0] - controlled->next_ref[0]) * scale;
                controlled->next_pos[2] = controlled->next_ref[2] +
                    (controlled->next_pos[2] - controlled->next_ref[2]) * scale;
            }
            controlled->distance = *distance;
        }
        if (angle) controlled->SetRotate(requested_angle);
        if (height) controlled->SetHeight(*height);
    } else {
        if (distance) follow->SetDistance(*distance);
        if (height) follow->SetHeight(*height);
        if (angle) follow->SetAngle(requested_angle);
    }
    const float result_distance = distance ? *distance :
        controlled != nullptr && controlled->control_on != 0
            ? std::hypot(controlled->next_pos[0] - controlled->next_ref[0],
                         controlled->next_pos[2] - controlled->next_ref[2])
            : follow->distance;
    const float result_height = height ? *height :
        controlled != nullptr && controlled->control_on != 0
            ? controlled->next_pos[1] - controlled->next_ref[1]
            : follow->height;
    return {ApiCallStatus::Ok,
            {{"scene", "dungeon"}, {"distance", result_distance}, {"height", result_height},
             {"angle", requested_angle}},
            {}};
}

ApiCallResult UnbindInputAction(std::string_view mod_id, const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) {
        return Invalid("expected {id = local_action_id}");
    }
    const auto id = args[0].find("id");
    if (id == args[0].end() || !id->is_string() || !IsIdentifier(id->get_ref<const std::string &>())) {
        return Invalid("expected a local action id");
    }
    const std::string qualified = std::string(mod_id) + ":" + id->get<std::string>();
    if (!InputUnbindModAction(qualified)) return Invalid("input action is not registered by this mod");
    g_input_contexts.UnbindAction(qualified);
    return {ApiCallStatus::Ok, {{"id", qualified}, {"removed", true}}, {}};
}

ApiCallResult GetCombat(const Json &args) {
    if (!args.is_array() || !args.empty()) return Invalid("combat.get takes no arguments");
    if (GetNowLoopNo() != LOOP_DUNGEON || DngSaveData == nullptr || ActiveMonster == nullptr || BattleAreaScene == nullptr ||
        DngStatus.mode == DNG_STATUS_EXIT) {
        return Failed("no active dungeon combat state");
    }

    Json enemies = Json::array();
    for (int slot = 0; slot < MONSTER_ACTIVE_MAX; ++slot) {
        CActiveMonster *monster = ActiveMonster->GetActiveMonster(slot);
        if (monster == nullptr || monster->state != ACTIVE_MONSTER_LIVE || monster->life <= 0) continue;
        const unsigned int generation = EnsureMoonModMonsterSlotGeneration(slot);
        enemies.push_back({{"id", "chronicle-two:monster-slot:" + std::to_string(slot)},
                           {"entity_id", "chronicle-two:monster:" + std::to_string(slot) + ":" + std::to_string(generation)},
                           {"slot", slot},
                           {"monster_id", monster->monster_id},
                           {"hp", monster->life},
                           {"max_hp", monster->max_life},
                           {"status_flags", monster->status.attr},
                           {"target_slot", monster->target_no}});
    }
    const std::size_t enemy_count = enemies.size();
    return {ApiCallStatus::Ok,
            {{"in_combat", enemy_count > 0}, {"enemy_count", enemy_count}, {"enemies", std::move(enemies)}},
            {}};
}

ApiCallResult HealCombatEnemy(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() ||
        !args[0].contains("amount") || (!args[0].contains("entity") && !args[0].contains("slot")) ||
        (args[0].contains("entity") && args[0].contains("slot"))) {
        return Invalid("expected {entity = generation_checked_handle, amount = positive_hit_points}");
    }
    for (auto field = args[0].begin(); field != args[0].end(); ++field) {
        if (field.key() != "entity" && field.key() != "slot" && field.key() != "amount") {
            return Invalid("unsupported combat.heal field");
        }
    }
    int slot = -1;
    int amount = 0;
    unsigned int generation = 0;
    const bool has_handle = args[0].contains("entity");
    if ((has_handle && (!args[0]["entity"].is_string() ||
                        !ReadNativeMonsterHandle(args[0]["entity"].get_ref<const std::string &>(), slot,
                                                 generation))) ||
        (!has_handle && !ReadInt(args[0], "slot", -1, 0, MONSTER_ACTIVE_MAX - 1, slot)) ||
        !ReadInt(args[0], "amount", 0, 1, 1000000, amount)) {
        return Invalid("entity must be a Chronicle Two monster handle and amount must be 1-1000000");
    }
    if (GetNowLoopNo() != LOOP_DUNGEON || DngSaveData == nullptr || ActiveMonster == nullptr || BattleAreaScene == nullptr ||
        DngStatus.mode == DNG_STATUS_EXIT) {
        return Failed("no active dungeon combat state");
    }
    if (has_handle && GetMoonModMonsterSlotGeneration(slot) != generation) {
        return Failed("the entity handle is stale because its monster slot was reused");
    }
    CActiveMonster *monster = ActiveMonster->GetActiveMonster(slot);
    if (monster == nullptr || monster->state != ACTIVE_MONSTER_LIVE || monster->life <= 0 || monster->max_life <= 0 ||
        monster->life > monster->max_life) {
        return Failed("the selected entity is not a live, valid target");
    }
    const int hp_before = monster->life;
    const int hp_after = static_cast<int>(std::min<std::int64_t>(monster->max_life,
                                                                 static_cast<std::int64_t>(hp_before) + amount));
    monster->life = hp_after;
    return {ApiCallStatus::Ok,
            {{"entity", "chronicle-two:monster:" + std::to_string(slot) + ":" +
                            std::to_string(EnsureMoonModMonsterSlotGeneration(slot))},
             {"slot", slot}, {"monster_id", monster->monster_id}, {"hp_before", hp_before}, {"hp_after", hp_after},
             {"restored", hp_after - hp_before}, {"changed", hp_after != hp_before}},
            {}};
}

ApiCallResult DamageCombatEnemy(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() ||
        !args[0].contains("entity") || !args[0].contains("amount")) {
        return Invalid("expected {entity = generation_checked_handle, amount = hit_points}");
    }
    for (auto field = args[0].begin(); field != args[0].end(); ++field) {
        if (field.key() != "entity" && field.key() != "amount") return Invalid("unsupported combat.damage field");
    }
    std::string handle;
    int slot = -1;
    unsigned int generation = 0;
    int amount = 0;
    if (!ReadId(args[0]["entity"], handle) || !ReadNativeMonsterHandle(handle, slot, generation) ||
        !ReadInt(args[0], "amount", 0, 1, 1000000, amount)) {
        return Invalid("entity must be a Chronicle Two monster handle and amount must be 1-1000000");
    }
    if (GetNowLoopNo() != LOOP_DUNGEON || DngSaveData == nullptr || ActiveMonster == nullptr || BattleAreaScene == nullptr ||
        DngStatus.mode == DNG_STATUS_EXIT) {
        return Failed("no active dungeon combat state");
    }
    if (GetMoonModMonsterSlotGeneration(slot) != generation) {
        return Failed("the entity handle is stale because its monster slot was reused");
    }
    CActiveMonster *monster = ActiveMonster->GetActiveMonster(slot);
    if (monster == nullptr || monster->state != ACTIVE_MONSTER_LIVE || monster->life <= 0 || monster->max_life <= 0 ||
        monster->life > monster->max_life) {
        return Failed("the selected entity is not a live, valid enemy");
    }
    const int hp_before = monster->life;
    if ((monster->attrib & MONSTER_ATTRIB_NO_DAMAGE) != 0 || monster->muteki_time > 0 ||
        monster->catch_state != ACTION_CATCH_NONE) {
        return {ApiCallStatus::Ok,
                {{"entity", handle}, {"hp_before", hp_before}, {"hp_after", hp_before},
                 {"damage", 0}, {"changed", false}, {"blocked", true}},
                {}};
    }

    const int modified_amount = static_cast<int>(std::ceil(
        ApplyChronicleTwoCombatDamageFormula(static_cast<float>(amount))));
    if (modified_amount <= 0) {
        return {ApiCallStatus::Ok,
                {{"entity", handle}, {"hp_before", hp_before}, {"hp_after", hp_before},
                 {"damage", 0}, {"changed", false}, {"blocked", false}, {"death_queued", false}},
                {}};
    }
    const int hp_after = std::max(0, hp_before - modified_amount);
    monster->life = hp_after;
    if (hp_after == 0) {
        monster->last_hit_kind = DAMAGE_KIND_MONSTER_OTHER;
        monster->last_hit_chara = -1;
        monster->last_hit_source = DAMAGE_SOURCE_NONE;
        monster->last_hit_attr = 0;
        monster->damage_req = ACTION_DAMAGE_REQ_DEAD;
        ChronicleTwoEnemyDefeated(monster, "combat");
    } else {
        monster->req_prog = MONSTER_PROG_DAMAGE;
    }
    return {ApiCallStatus::Ok,
            {{"entity", handle}, {"hp_before", hp_before}, {"hp_after", hp_after},
             {"damage", hp_before - hp_after}, {"changed", hp_before != hp_after},
             {"blocked", false}, {"death_queued", hp_after == 0}},
            {}};
}

bool ReadNativeMonsterHandle(std::string_view handle, int &slot, unsigned int &generation) {
    constexpr std::string_view prefix = "chronicle-two:monster:";
    if (!handle.starts_with(prefix)) return false;
    const std::string_view suffix = handle.substr(prefix.size());
    const std::size_t separator = suffix.find(':');
    if (separator == std::string_view::npos || suffix.find(':', separator + 1) != std::string_view::npos) return false;
    int parsed_slot = -1;
    unsigned int parsed_generation = 0;
    const auto slot_parse = std::from_chars(suffix.data(), suffix.data() + separator, parsed_slot);
    const auto generation_parse = std::from_chars(suffix.data() + separator + 1, suffix.data() + suffix.size(),
                                                  parsed_generation);
    if (slot_parse.ec != std::errc{} || slot_parse.ptr != suffix.data() + separator || parsed_slot < 0 ||
        parsed_slot >= MONSTER_ACTIVE_MAX || generation_parse.ec != std::errc{} ||
        generation_parse.ptr != suffix.data() + suffix.size() || parsed_generation == 0) {
        return false;
    }
    slot = parsed_slot;
    generation = parsed_generation;
    return true;
}

ApiCallResult GetEntity(const Json &args) {
    if (!args.is_array() || args.size() != 1) return Invalid("expected one entity handle or {id = entity_handle}");
    std::string handle;
    if (!ReadId(args[0], handle)) return Invalid("entity handle must be a string or {id = handle}");
    const auto mod_spawn = g_character_spawns.find(handle);

    int slot = -1;
    unsigned int generation = 0;
    if (!ReadNativeMonsterHandle(handle, slot, generation)) return Invalid("entity handle is not a Chronicle Two monster handle");
    Json result = {{"id", handle}, {"type", "monster"}, {"slot", slot}, {"generation", generation}};
    if (mod_spawn != g_character_spawns.end()) {
        result["character_id"] = mod_spawn->second.character_id;
        result["owner"] = mod_spawn->second.owner;
        result["mod_owned"] = true;
    }
    if (GetNowLoopNo() != LOOP_DUNGEON || DngSaveData == nullptr || ActiveMonster == nullptr || DngMainScene == nullptr ||
        ActiveMonster->GetScene() != DngMainScene || DngStatus.mode == DNG_STATUS_EXIT) {
        result["status"] = "expired";
        return {ApiCallStatus::Ok, std::move(result), {}};
    }
    if (GetMoonModMonsterSlotGeneration(slot) != generation) {
        result["status"] = "expired";
        return {ApiCallStatus::Ok, std::move(result), {}};
    }
    CActiveMonster *monster = ActiveMonster->GetActiveMonster(slot);
    if (monster == nullptr || monster->monster_id < 0) {
        result["status"] = "expired";
        return {ApiCallStatus::Ok, std::move(result), {}};
    }
    if (monster->state != ACTIVE_MONSTER_LIVE || monster->life <= 0) {
        result["status"] = "dead";
        return {ApiCallStatus::Ok, std::move(result), {}};
    }
    float position[4] = {};
    monster->GetPosition(position);
    BASE_MONSTER_TBL *definition = GetMonsterTable(monster->monster_id);
    result["status"] = "alive";
    result["definition"] = {{"monster_id", monster->monster_id},
                             {"name", definition == nullptr ? "" : definition->name}};
    result["position"] = {{"x", position[0]}, {"y", position[1]}, {"z", position[2]}};
    result["health"] = {{"current", monster->life}, {"maximum", monster->max_life}};
    result["combat"] = {{"attack", monster->attack}, {"defense", monster->defense}};
    result["status_flags"] = monster->status.attr;
    return {ApiCallStatus::Ok, std::move(result), {}};
}

ApiCallResult GetEntityVelocity(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || args[0].size() != 1 ||
        !args[0].contains("entity") || !args[0]["entity"].is_string()) {
        return Invalid("expected {entity = generation_checked_entity_handle}");
    }
    const std::string &handle = args[0]["entity"].get_ref<const std::string &>();
    const ApiCallResult entity = GetEntity(Json::array({Json{{"id", handle}}}));
    if (entity.status != ApiCallStatus::Ok) return entity;
    if (entity.value.value("status", "") != "alive") return Failed("velocity is only available for a live dungeon entity");

    CActiveMonster *monster = nullptr;
    const auto mod_spawn = g_character_spawns.find(handle);
    if (mod_spawn != g_character_spawns.end()) {
        if (!IsSpawnHandleCurrent(mod_spawn->second)) return Failed("the entity handle is stale because its monster slot was reused");
        monster = ActiveMonster->GetActiveMonster(mod_spawn->second.slot);
    } else {
        int slot = -1;
        unsigned int generation = 0;
        if (!ReadNativeMonsterHandle(handle, slot, generation) || GetMoonModMonsterSlotGeneration(slot) != generation) {
            return Failed("the entity handle is stale because its monster slot was reused");
        }
        monster = ActiveMonster->GetActiveMonster(slot);
    }
    if (monster == nullptr) return Failed("the entity no longer has a live native actor");
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(monster->velocity[axis])) return Failed("the native entity velocity contains a non-finite component");
    }
    return {ApiCallStatus::Ok,
            {{"entity", handle}, {"units", "game_units_per_frame"},
             {"velocity", {{"x", monster->velocity[0]}, {"y", monster->velocity[1]}, {"z", monster->velocity[2]}}}},
            {}};
}

ApiCallResult SetEntityVelocity(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || args[0].size() != 2 ||
        !args[0].contains("entity") || !args[0]["entity"].is_string() || !args[0].contains("velocity")) {
        return Invalid("expected {entity = generation_checked_entity_handle, velocity = {x, y, z}}");
    }
    const std::string &handle = args[0]["entity"].get_ref<const std::string &>();
    std::array<float, 4> velocity{};
    if (!ReadVector3(args[0]["velocity"], velocity, 100000.0f)) {
        return Invalid("velocity must contain finite x, y, and z components within 100000 game units per frame");
    }

    const ApiCallResult entity = GetEntity(Json::array({Json{{"id", handle}}}));
    if (entity.status != ApiCallStatus::Ok) return entity;
    if (entity.value.value("status", "") != "alive") {
        return Failed("velocity can only be set for a live dungeon entity");
    }

    CActiveMonster *monster = nullptr;
    const auto mod_spawn = g_character_spawns.find(handle);
    if (mod_spawn != g_character_spawns.end()) {
        if (!IsSpawnHandleCurrent(mod_spawn->second)) {
            return Failed("the entity handle is stale because its monster slot was reused");
        }
        monster = ActiveMonster->GetActiveMonster(mod_spawn->second.slot);
    } else {
        int slot = -1;
        unsigned int generation = 0;
        if (!ReadNativeMonsterHandle(handle, slot, generation) ||
            GetMoonModMonsterSlotGeneration(slot) != generation) {
            return Failed("the entity handle is stale because its monster slot was reused");
        }
        monster = ActiveMonster->GetActiveMonster(slot);
    }
    if (monster == nullptr || monster->state != ACTIVE_MONSTER_LIVE || monster->life <= 0) {
        return Failed("the entity no longer has a live native actor");
    }
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(monster->velocity[axis])) {
            return Failed("the native entity velocity contains a non-finite component");
        }
    }

    const Json previous = {{"x", monster->velocity[0]}, {"y", monster->velocity[1]}, {"z", monster->velocity[2]}};
    const bool changed = monster->velocity[0] != velocity[0] || monster->velocity[1] != velocity[1] ||
                         monster->velocity[2] != velocity[2];
    monster->velocity[0] = velocity[0];
    monster->velocity[1] = velocity[1];
    monster->velocity[2] = velocity[2];
    monster->velocity[3] = 0.0f;
    return {ApiCallStatus::Ok,
            {{"entity", handle}, {"units", "game_units_per_frame"}, {"changed", changed},
             {"previous_velocity", previous},
             {"velocity", {{"x", velocity[0]}, {"y", velocity[1]}, {"z", velocity[2]}}}},
            {}};
}

ApiCallResult QueryPhysics(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) {
        return Invalid("expected {shape = 'sphere', center = {x, y, z}, radius = game_units}");
    }
    const Json &query = args[0];
    for (auto field = query.begin(); field != query.end(); ++field) {
        if (field.key() != "shape" && field.key() != "center" && field.key() != "radius") {
            return Invalid("unsupported physics.query field");
        }
    }
    if (!query.contains("shape") || !query["shape"].is_string() || query["shape"].get<std::string>() != "sphere" ||
        !query.contains("center") || !query.contains("radius")) {
        return Invalid("physics.query currently requires shape 'sphere', center, and radius");
    }
    std::array<float, 4> center{};
    if (!ReadVector3(query["center"], center, 1000000.0f) || !query["radius"].is_number()) {
        return Invalid("center must contain finite game coordinates within +/-1000000 and radius must be 0-10000");
    }
    const double radius = query["radius"].get<double>();
    if (!std::isfinite(radius) || radius < 0.0 || radius > 10000.0) {
        return Invalid("radius must be finite and within 0-10000 game units");
    }
    if (GetNowLoopNo() != LOOP_DUNGEON || DngSaveData == nullptr || ActiveMonster == nullptr || BattleAreaScene == nullptr ||
        DngStatus.mode == DNG_STATUS_EXIT) {
        return Failed("physics.query requires an active Chronicle Two dungeon");
    }

    const double radius_squared = radius * radius;
    Json matches = Json::array();
    for (int slot = 0; slot < MONSTER_ACTIVE_MAX; ++slot) {
        CActiveMonster *monster = ActiveMonster->GetActiveMonster(slot);
        if (monster == nullptr || monster->state != ACTIVE_MONSTER_LIVE || monster->life <= 0) continue;
        const unsigned int generation = EnsureMoonModMonsterSlotGeneration(slot);
        if (generation == 0) continue;
        float position[4]{};
        monster->GetPosition(position);
        for (int axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(position[axis])) return Failed("a live monster has a non-finite native position");
        }
        const double dx = static_cast<double>(position[0]) - center[0];
        const double dy = static_cast<double>(position[1]) - center[1];
        const double dz = static_cast<double>(position[2]) - center[2];
        const double distance_squared = dx * dx + dy * dy + dz * dz;
        if (distance_squared > radius_squared) continue;
        const std::string handle = "chronicle-two:monster:" + std::to_string(slot) + ":" + std::to_string(generation);
        matches.push_back({{"entity", handle}, {"type", "monster"}, {"slot", slot},
                           {"position", {{"x", position[0]}, {"y", position[1]}, {"z", position[2]}}},
                           {"distance", std::sqrt(distance_squared)}});
    }
    return {ApiCallStatus::Ok,
            {{"shape", "sphere"},
             {"semantics", "live monster actor origins inside or on the sphere; no collider or world geometry test"},
             {"units", "game_units"}, {"center", {{"x", center[0]}, {"y", center[1]}, {"z", center[2]}}},
             {"radius", radius}, {"count", matches.size()}, {"matches", std::move(matches)}},
            {}};
}

ApiCallResult DespawnEntity(std::string_view mod_id, const Json &args) {
    if (!args.is_array() || args.size() != 1) {
        return Invalid("expected one mod-spawned entity handle or {id = entity_handle}");
    }
    std::string handle;
    if (!ReadId(args[0], handle)) return Invalid("entity handle must be a string or {id = handle}");
    const auto found = g_character_spawns.find(handle);
    if (found == g_character_spawns.end()) {
        return Failed("Chronicle Two can only despawn entities created by characters.spawn");
    }
    const ModCharacterSpawn &record = found->second;
    if (record.owner != mod_id) return Failed("a mod can only despawn entities that it spawned");
    if (!IsSpawnHandleCurrent(record)) {
        return {ApiCallStatus::Ok, {{"id", handle}, {"status", "expired"}, {"changed", false}}, {}};
    }
    CActiveMonster *monster = ActiveMonster->GetActiveMonster(record.slot);
    if (monster->state != ACTIVE_MONSTER_LIVE || monster->life <= 0) {
        return {ApiCallStatus::Ok, {{"id", handle}, {"status", "dead"}, {"changed", false}}, {}};
    }
    if (monster->catch_state == ACTION_CATCH_HELD) {
        return Failed("a monster cannot be despawned while it is being held by a native interaction");
    }
    if (!ActiveMonster->DespawnMoonModMonster(record.slot, record.generation)) {
        return Failed("the native dungeon manager could not safely despawn this enemy in its current state");
    }
    const bool animation_released = g_animation_plays.erase(handle) != 0;
    return {ApiCallStatus::Ok,
            {{"id", handle}, {"status", "despawned"}, {"changed", true},
             {"animation_released", animation_released}},
            {}};
}

void DespawnOwnedChronicleTwoActors(std::string_view owner) {
    if (owner.empty() || ActiveMonster == nullptr || DngMainScene == nullptr || ActiveMonster->GetScene() != DngMainScene) return;
    std::vector<std::string> handles;
    for (const auto &[handle, record] : g_character_spawns) {
        if (record.owner == owner && IsSpawnHandleCurrent(record) && IsSpawnHandleLive(record)) {
            handles.push_back(handle);
        }
    }
    for (const std::string &handle : handles) {
        const ApiCallResult removed = DespawnEntity(owner, Json::array({Json{{"id", handle}}}));
        if (removed.status == ApiCallStatus::Ok) g_character_spawns.erase(handle);
    }
}

ApiCallResult QueryEntities(const Json &args) {
    if (!args.is_array() || args.size() > 1 || (args.size() == 1 && !args[0].is_object())) {
        return Invalid("expected no arguments or {type = 'monster'}");
    }
    if (!args.empty()) {
        for (auto field = args[0].begin(); field != args[0].end(); ++field) {
            if (field.key() != "type") return Invalid("unsupported entity query filter");
        }
        const auto type = args[0].find("type");
        if (type != args[0].end() && (!type->is_string() || type->get<std::string>() != "monster")) {
            return Invalid("Chronicle Two currently supports entities.query only for type 'monster'");
        }
    }
    if (GetNowLoopNo() != LOOP_DUNGEON || DngSaveData == nullptr || ActiveMonster == nullptr || BattleAreaScene == nullptr ||
        DngStatus.mode == DNG_STATUS_EXIT) {
        return Failed("no active dungeon entity state");
    }

    Json entities = Json::array();
    for (int slot = 0; slot < MONSTER_ACTIVE_MAX; ++slot) {
        const CActiveMonster *monster = ActiveMonster->GetActiveMonster(slot);
        if (monster == nullptr || monster->state != ACTIVE_MONSTER_LIVE || monster->life <= 0) continue;
        const unsigned int generation = EnsureMoonModMonsterSlotGeneration(slot);
        if (generation == 0) continue;
        const std::string handle = "chronicle-two:monster:" + std::to_string(slot) + ":" + std::to_string(generation);
        Json entity = {{"id", handle},
                       {"type", "monster"},
                       {"slot", slot},
                       {"generation", generation},
                       {"definition", {{"monster_id", monster->monster_id}}},
                       {"health", {{"current", monster->life}, {"maximum", monster->max_life}}},
                       {"status", {{"flags", monster->status.attr}, {"target_slot", monster->target_no}}}};
        const auto mod_spawn = g_character_spawns.find(handle);
        if (mod_spawn != g_character_spawns.end()) {
            entity["character_id"] = mod_spawn->second.character_id;
            entity["owner"] = mod_spawn->second.owner;
            entity["mod_owned"] = true;
        }
        entities.push_back(std::move(entity));
    }
    return {ApiCallStatus::Ok,
            {{"type", "monster"}, {"supported_types", Json::array({"monster"})},
             {"count", entities.size()}, {"entities", std::move(entities)}},
            {}};
}

ApiCallResult ResolveModel(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || args[0].size() != 1) {
        return Invalid("expected {monster_id = native monster model id} or {id = registered model id}");
    }
    if (args[0].contains("id")) {
        if (!args[0]["id"].is_string()) return Invalid("id must be a registered model stable id");
        const auto model = g_models.find(args[0]["id"].get<std::string>());
        if (model == g_models.end()) return Invalid("id does not identify a registered custom model");
        return {ApiCallStatus::Ok, ModModelJson(model->second), {}};
    }
    int model_id = -1;
    if (!ReadInt(args[0], "monster_id", -1, 0, 32767, model_id)) {
        return Invalid("monster_id must be an integer id from the native monster model table");
    }
    BASE_MONSTER_TBL *definition = GetMonsterTable(model_id);
    if (definition == nullptr) return Invalid("monster_id does not identify a loaded native model definition");
    const std::string model_name = BoundedString(definition->model, sizeof(definition->model));
    if (model_name.empty() || !IsIdentifier(model_name)) {
        return Failed("native model definition has no safe model filename");
    }
    const std::string game_path = "dungeon/monster/" + model_name + ".chr";
    Json result = {{"monster_id", model_id}, {"model", model_name},
                   {"script", BoundedString(definition->script, sizeof(definition->script))},
                   {"source", FindNativeResourceOverride(game_path).has_value() ? "package_override" : "native_table"},
                   {"assets", Json::array({{{"slot", 0}, {"model", model_name}, {"game_path", game_path},
                                             {"overridden", FindNativeResourceOverride(game_path).has_value()}}})}};
    bool loaded_on_current_floor = false;
    if (GetNowLoopNo() == LOOP_DUNGEON && DngSaveData != nullptr && ActiveMonster != nullptr &&
        DngMainScene != nullptr && ActiveMonster->GetScene() == DngMainScene && DngStatus.mode != DNG_STATUS_EXIT) {
        const int refer_index = ActiveMonster->SearchBaseIndex(model_id);
        loaded_on_current_floor = refer_index >= 0 && ActiveMonster->refer[refer_index].chara.GetFrame() != nullptr;
    }
    result["loaded_on_current_floor"] = loaded_on_current_floor;
    const auto mod_character = std::ranges::find_if(g_characters, [model_id](const auto &entry) {
        return entry.second.native_monster_id == model_id;
    });
    if (mod_character != g_characters.end()) {
        result["character_id"] = mod_character->first;
        result["owner"] = mod_character->second.owner;
        result["template_monster_id"] = mod_character->second.template_monster_id;
        if (!mod_character->second.model_id.empty()) result["model_id"] = mod_character->second.model_id;
    }
    return {ApiCallStatus::Ok, std::move(result), {}};
}

bool IsDebugOverlayText(std::string_view text) {
    if (text.empty() || text.size() > kMaximumDebugOverlayText) return false;
    constexpr std::string_view allowed_punctuation = " .,:;/()+-_%'[]?~";
    return std::ranges::all_of(text, [allowed_punctuation](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               allowed_punctuation.find(static_cast<char>(c)) != std::string_view::npos;
    });
}

ApiCallResult RegisterDebugOverlay(std::string_view mod_id, const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || args[0].size() != 2 ||
        !args[0].contains("id") || !args[0]["id"].is_string() ||
        !args[0].contains("text") || !args[0]["text"].is_string()) {
        return Invalid("expected {id = local_id, text = one_line_text}");
    }
    std::string id;
    if (!QualifyOwnerScopedId(mod_id, args[0]["id"].get_ref<const std::string &>(), id)) {
        return Invalid("id must be a local identifier owned by this mod");
    }
    const std::string text = args[0]["text"].get<std::string>();
    if (!IsDebugOverlayText(text)) {
        return Invalid("text must use supported overlay characters and contain 1 to 48 characters");
    }
    if (!g_debug_overlays.contains(id) && g_debug_overlays.size() >= kMaximumDebugOverlays) {
        return Failed("the game supports at most four mod diagnostic overlays at once");
    }
    const auto existing = g_debug_overlays.find(id);
    if (existing != g_debug_overlays.end() && (existing->second.render_layer || existing->second.render_bar)) {
        return Invalid("id is already used by a render layer or HUD bar");
    }
    g_debug_overlays[id] = {id, std::string(mod_id), text, false, false, {}, 0.0, 0.0};
    return {ApiCallStatus::Ok, {{"id", id}, {"text", text}, {"status", "registered"}}, {}};
}

ApiCallResult RegisterRenderLayer(std::string_view mod_id, const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || args[0].size() != 2 ||
        !args[0].contains("id") || !args[0]["id"].is_string() ||
        !args[0].contains("text") || !args[0]["text"].is_string()) {
        return Invalid("expected {id = local_id, text = one_line_text}");
    }
    std::string id;
    if (!QualifyOwnerScopedId(mod_id, args[0]["id"].get_ref<const std::string &>(), id)) {
        return Invalid("id must be a local identifier owned by this mod");
    }
    const std::string text = args[0]["text"].get<std::string>();
    if (!IsDebugOverlayText(text)) {
        return Invalid("text must use supported overlay characters and contain 1 to 48 characters");
    }
    const auto existing = g_debug_overlays.find(id);
    if (existing != g_debug_overlays.end() && (!existing->second.render_layer || existing->second.render_bar)) {
        return Invalid("id is already used by a diagnostic overlay or HUD bar");
    }
    if (existing == g_debug_overlays.end() && g_debug_overlays.size() >= kMaximumDebugOverlays) {
        return Failed("the game supports at most four combined diagnostic overlays and HUD text layers");
    }
    g_debug_overlays[id] = {id, std::string(mod_id), text, true, false, {}, 0.0, 0.0};
    return {ApiCallStatus::Ok, {{"id", id}, {"text", text}, {"status", "registered"}}, {}};
}

ApiCallResult RemoveRenderLayer(std::string_view mod_id, const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || args[0].size() != 1 ||
        !args[0].contains("id") || !args[0]["id"].is_string()) {
        return Invalid("expected {id = local_layer_id}");
    }
    std::string id;
    if (!QualifyOwnerScopedId(mod_id, args[0]["id"].get_ref<const std::string &>(), id)) {
        return Invalid("id must be a local identifier owned by this mod");
    }
    const auto existing = g_debug_overlays.find(id);
    if (existing == g_debug_overlays.end() || !existing->second.render_layer || existing->second.owner != mod_id) {
        return Invalid("render layer is missing or owned by another mod");
    }
    g_debug_overlays.erase(existing);
    return {ApiCallStatus::Ok, {{"id", id}, {"removed", true}}, {}};
}

ApiCallResult RegisterRenderBar(std::string_view mod_id, const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() ||
        !args[0].contains("id") || !args[0]["id"].is_string() ||
        !args[0].contains("label") || !args[0]["label"].is_string() ||
        !args[0].contains("current") || !args[0]["current"].is_number() ||
        !args[0].contains("max") || !args[0]["max"].is_number()) {
        return Invalid("expected {id = local_id, label = bar_label, current = number, max = positive_number}");
    }
    for (auto field = args[0].begin(); field != args[0].end(); ++field) {
        if (field.key() != "id" && field.key() != "label" && field.key() != "current" && field.key() != "max") {
            return Invalid("render.bar_register accepts only id, label, current, and max");
        }
    }
    std::string id;
    if (!QualifyOwnerScopedId(mod_id, args[0]["id"].get_ref<const std::string &>(), id)) {
        return Invalid("id must be a local identifier owned by this mod");
    }
    const std::string label = args[0]["label"].get<std::string>();
    if (label.empty() || label.size() > 24 || !IsDebugOverlayText(label)) {
        return Invalid("label must use supported overlay characters and contain 1 to 24 characters");
    }
    const double current = args[0]["current"].get<double>();
    const double maximum = args[0]["max"].get<double>();
    if (!std::isfinite(current) || !std::isfinite(maximum) || maximum <= 0.0 || current < 0.0 || current > maximum) {
        return Invalid("current must be in [0, max] and max must be a finite positive number");
    }
    const auto existing = g_debug_overlays.find(id);
    if (existing != g_debug_overlays.end() && !existing->second.render_bar) {
        return Invalid("id is already used by a diagnostic overlay or text layer");
    }
    if (existing == g_debug_overlays.end() && g_debug_overlays.size() >= kMaximumDebugOverlays) {
        return Failed("the game supports at most four combined diagnostic overlays, HUD text layers, and HUD bars");
    }
    const int percent = static_cast<int>(std::clamp(std::lround((current / maximum) * 100.0), 0L, 100L));
    const std::string text = label + " [" + std::to_string(percent) + "%]";
    g_debug_overlays[id] = {id, std::string(mod_id), text, false, true, label, current, maximum};
    return {ApiCallStatus::Ok,
            {{"id", id}, {"label", label}, {"current", current}, {"max", maximum},
             {"percent", percent}, {"text", text}, {"status", "registered"}},
            {}};
}

ApiCallResult RemoveRenderBar(std::string_view mod_id, const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || args[0].size() != 1 ||
        !args[0].contains("id") || !args[0]["id"].is_string()) {
        return Invalid("expected {id = local_bar_id}");
    }
    std::string id;
    if (!QualifyOwnerScopedId(mod_id, args[0]["id"].get_ref<const std::string &>(), id)) {
        return Invalid("id must be a local identifier owned by this mod");
    }
    const auto existing = g_debug_overlays.find(id);
    if (existing == g_debug_overlays.end() || !existing->second.render_bar || existing->second.owner != mod_id) {
        return Invalid("render bar is missing or owned by another mod");
    }
    g_debug_overlays.erase(existing);
    return {ApiCallStatus::Ok, {{"id", id}, {"removed", true}}, {}};
}

std::string ChronicleTwoWeaponStableId(int item_no) {
    for (const auto &[id, weapon] : g_weapons) {
        if (weapon.native_item_no == item_no) return id;
    }
    return NativeWeaponId(item_no);
}

std::string ChronicleTwoItemOrWeaponStableId(int item_no) {
    for (const auto &[id, weapon] : g_weapons) {
        if (weapon.native_item_no == item_no) return id;
    }
    for (const auto &[id, item] : g_items) {
        if (item.native_item_no == item_no) return id;
    }
    if (FindWeaponCommon(item_no) != nullptr) return NativeWeaponId(item_no);
    return NativeItemId(item_no);
}

bool FindOwnedChronicleTwoWeapon(const Json &selector, CUserDataManager *&user_data, CGameDataUsed *&owned,
                                 int &item_no, std::string &stable_id, ApiCallResult &error) {
    const ApiCallResult definition = GetWeapon(Json::array({selector}));
    if (definition.status != ApiCallStatus::Ok) {
        error = definition;
        return false;
    }
    item_no = definition.value.value("native_item_no", 0);
    if (item_no <= 0) {
        error = Failed("weapon definition is unavailable in the native weapon tables");
        return false;
    }
    user_data = GetUserDataMan();
    if (user_data == nullptr) {
        error = Failed("the game has no active save data");
        return false;
    }
    owned = FindOwnedWeapon(*user_data, item_no);
    if (owned == nullptr || owned->used_type != USED_ITEM_TYPE_WEAPON || owned->item_no != item_no) {
        error = Failed("weapon is not owned by the active save");
        return false;
    }
    stable_id = definition.value.value("id", ChronicleTwoWeaponStableId(item_no));
    return true;
}

ApiCallResult GetWeaponAbs(const Json &args) {
    if (!args.is_array() || args.size() != 1) {
        return Invalid("expected {id = weapon_id}");
    }
    CUserDataManager *user_data = nullptr;
    CGameDataUsed *owned = nullptr;
    int item_no = 0;
    std::string id;
    ApiCallResult error;
    if (!FindOwnedChronicleTwoWeapon(args[0], user_data, owned, item_no, id, error)) return error;
    const WEAPON_USED &state = owned->data.weapon;
    return {ApiCallStatus::Ok,
            {{"id", id}, {"native_item_no", item_no},
             {"level", state.level}, {"abs", state.abs.now}, {"abs_max", state.abs.max},
             {"ready_to_upgrade", owned->IsLevelUp() != 0},
             {"fusion_points", state.fusion_point}},
            {}};
}

ApiCallResult AddWeaponAbs(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() ||
        !args[0].contains("amount") || !args[0]["amount"].is_number_integer()) {
        return Invalid("expected {id = weapon_id, amount = positive_integer}");
    }
    int amount = 0;
    if (!ReadInt(args[0], "amount", 0, 1, 32767, amount)) {
        return Invalid("amount must be an integer from 1 to 32767");
    }
    CUserDataManager *user_data = nullptr;
    CGameDataUsed *owned = nullptr;
    int item_no = 0;
    std::string id;
    ApiCallResult error;
    if (!FindOwnedChronicleTwoWeapon(args[0], user_data, owned, item_no, id, error)) return error;
    WEAPON_USED &state = owned->data.weapon;
    const float previous_abs = state.abs.now;
    state.abs.AddPoint(static_cast<float>(amount));
    const float added = state.abs.now - previous_abs;
    return {ApiCallStatus::Ok,
            {{"id", id}, {"native_item_no", item_no},
             {"level", state.level}, {"previous_abs", previous_abs},
             {"abs", state.abs.now}, {"abs_max", state.abs.max},
             {"added", added}, {"changed", added > 0.0f}},
            {}};
}

bool ReadChronicleTwoAnyItemOrWeaponNo(const Json &selector, int &item_no) {
    if (selector.is_number_integer()) {
        const auto val = selector.get<std::int64_t>();
        if (val <= 0 || val >= ITEM_NO_MAX) return false;
        item_no = static_cast<int>(val);
        return true;
    }
    if (selector.is_string()) {
        if (ReadNativeItemId(selector, item_no) || ReadNativeWeaponId(selector, item_no)) return true;
        std::string err;
        return ResolveItemId(selector.get_ref<const std::string &>(), item_no, err) == ApiCallStatus::Ok;
    }
    if (selector.is_object()) {
        if (selector.contains("item_no")) return ReadChronicleTwoAnyItemOrWeaponNo(selector["item_no"], item_no);
        if (selector.contains("id")) return ReadChronicleTwoAnyItemOrWeaponNo(selector["id"], item_no);
    }
    return false;
}

ApiCallResult GetWeaponAttachments(const Json &args) {
    if (!args.is_array() || args.size() != 1) {
        return Invalid("expected {id = weapon_id}");
    }
    CUserDataManager *user_data = nullptr;
    CGameDataUsed *owned = nullptr;
    int item_no = 0;
    std::string id;
    ApiCallResult error;
    if (!FindOwnedChronicleTwoWeapon(args[0], user_data, owned, item_no, id, error)) return error;
    const WEAPON_USED &state = owned->data.weapon;
    Json attributes = Json::array();
    for (s16 attr : state.attribute) attributes.push_back(attr);
    Json history = Json::array();
    if (const auto found = g_synthesized_weapon_attachments.find(item_no);
        found != g_synthesized_weapon_attachments.end()) {
        for (int index = 0; index < static_cast<int>(found->second.size()); ++index) {
            const int att_no = found->second[static_cast<std::size_t>(index)];
            history.push_back({{"slot", index}, {"occupied", true},
                               {"item_no", att_no}, {"id", ChronicleTwoItemOrWeaponStableId(att_no)}});
        }
    }
    return {ApiCallStatus::Ok,
            {{"id", id}, {"native_item_no", item_no},
             {"level", state.level}, {"fusion_points", state.fusion_point},
             {"socket_count", state.fusion_point + static_cast<int>(history.size())},
             {"occupied_count", history.size()},
             {"stats", {{"attack", state.status[WEAPON_STAT_ATTACK]},
                         {"durability", state.status[WEAPON_STAT_DURABILITY]}}},
             {"attributes", std::move(attributes)},
             {"attachments", std::move(history)}},
            {}};
}

ApiCallResult InsertWeaponAttachment(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() ||
        !args[0].contains("attachment_id")) {
        return Invalid("expected {id = weapon_id, attachment_id = item_id_or_no, cost = optional_int}");
    }
    int attachment_no = -1;
    if (!ReadChronicleTwoAnyItemOrWeaponNo(args[0]["attachment_id"], attachment_no) ||
        attachment_no <= 0 || attachment_no >= ITEM_NO_MAX) {
        return Invalid("attachment_id must identify a valid Chronicle Two item or attachment");
    }
    CUserDataManager *user_data = nullptr;
    CGameDataUsed *owned = nullptr;
    int item_no = 0;
    std::string id;
    ApiCallResult error;
    if (!FindOwnedChronicleTwoWeapon(args[0], user_data, owned, item_no, id, error)) return error;
    WEAPON_USED &state = owned->data.weapon;
    int cost = state.fusion_point > 0 ? 1 : 0;
    if (args[0].contains("cost") && !ReadInt(args[0], "cost", cost, 0, 99, cost)) {
        return Invalid("cost must be an integer from 0 to 99");
    }
    if (state.fusion_point < cost) {
        return Failed("weapon does not have enough synthesis points (fusion_points)");
    }
    bool applied_stat = false;
    if (const CDataAttach *attach_data = GameItemDataManage.GetAttachData(attachment_no); attach_data != nullptr) {
        for (int index = 0; index < 2; ++index) state.status[index] = static_cast<s16>(state.status[index] + attach_data->status[index]);
        for (int index = 0; index < 8; ++index) state.attribute[index] = static_cast<s16>(state.attribute[index] + attach_data->attribute[index]);
        state.special |= attach_data->special;
        applied_stat = true;
    } else if (attachment_no >= 1 && attachment_no <= 0x1A9) {
        const int stat_idx = etcitem_spectol_table[attachment_no - 1][0];
        const int amount = etcitem_spectol_table[attachment_no - 1][1];
        if (stat_idx >= 0 && stat_idx < 2 && amount != 0) {
            state.status[stat_idx] = static_cast<s16>(state.status[stat_idx] + amount);
            applied_stat = true;
        } else if (stat_idx >= 2 && stat_idx < 10 && amount != 0) {
            state.attribute[stat_idx - 2] = static_cast<s16>(state.attribute[stat_idx - 2] + amount);
            applied_stat = true;
        }
    }
    if (!applied_stat) {
        state.status[WEAPON_STAT_ATTACK] = static_cast<s16>(state.status[WEAPON_STAT_ATTACK] + 2);
    }
    owned->CheckParamLimmit();
    state.fusion_point = static_cast<s16>(std::max(0, state.fusion_point - cost));
    auto &history = g_synthesized_weapon_attachments[item_no];
    const int slot = static_cast<int>(history.size());
    history.push_back(attachment_no);
    return {ApiCallStatus::Ok,
            {{"id", id}, {"native_item_no", item_no},
             {"attachment_slot", slot}, {"attachment_item_no", attachment_no},
             {"fusion_points", state.fusion_point},
             {"inserted", true}, {"changed", true}},
            {}};
}

ApiCallResult ListWeaponBuildUps(const Json &args) {
    if (!args.is_array() || args.size() != 1) return Invalid("expected one weapon id or selector");
    const ApiCallResult definition = GetWeapon(args);
    if (definition.status != ApiCallStatus::Ok) return definition;
    const int item_no = definition.value.value("native_item_no", 0);
    const CDataWeapon *weapon = GameItemDataManage.GetWeaponData(item_no);
    if (weapon == nullptr) return Failed("weapon definition is unavailable");
    Json targets = Json::array();
    for (int index = 0; index < 3; ++index) {
        const int target_no = weapon->buildup_weapon[index];
        if (target_no <= 0) continue;
        const bool is_mod = g_mod_weapon_buildups.contains(item_no) &&
                            std::ranges::find(g_mod_weapon_buildups.at(item_no), target_no) !=
                                g_mod_weapon_buildups.at(item_no).end();
        targets.push_back({{"id", ChronicleTwoWeaponStableId(target_no)},
                           {"native_item_no", target_no},
                           {"slot", index},
                           {"required_monster", weapon->buildup_monster[index]},
                           {"source", is_mod ? "mod" : "native"}});
    }
    if (const auto custom = g_mod_weapon_buildups.find(item_no); custom != g_mod_weapon_buildups.end()) {
        for (int target_no : custom->second) {
            const bool already = std::ranges::any_of(targets, [target_no](const Json &entry) {
                return entry.value("native_item_no", -1) == target_no;
            });
            if (!already) {
                targets.push_back({{"id", ChronicleTwoWeaponStableId(target_no)},
                                   {"native_item_no", target_no},
                                   {"source", "mod"}});
            }
        }
    }
    return {ApiCallStatus::Ok,
            {{"id", definition.value.value("id", ChronicleTwoWeaponStableId(item_no))},
             {"native_item_no", item_no},
             {"targets", std::move(targets)}},
            {}};
}

ApiCallResult RegisterWeaponBuildUp(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() ||
        !args[0].contains("from") || !args[0].contains("to")) {
        return Invalid("expected {from = source_weapon_id, to = target_weapon_id}");
    }
    const ApiCallResult from_def = GetWeapon(Json::array({args[0]["from"]}));
    if (from_def.status != ApiCallStatus::Ok) return from_def;
    const ApiCallResult to_def = GetWeapon(Json::array({args[0]["to"]}));
    if (to_def.status != ApiCallStatus::Ok) return to_def;
    const int from_no = from_def.value.value("native_item_no", 0);
    const int to_no = to_def.value.value("native_item_no", 0);
    if (from_no <= 0 || to_no <= 0 || from_no == to_no) {
        return Invalid("from and to must identify two distinct Chronicle Two weapons");
    }
    CDataWeapon *from_weapon = GameItemDataManage.GetWeaponData(from_no);
    const CDataWeapon *to_weapon = GameItemDataManage.GetWeaponData(to_no);
    if (from_weapon == nullptr || to_weapon == nullptr) return Failed("weapon definition is unavailable");
    if (!g_original_weapon_buildups.contains(from_no)) {
        ChronicleTwoWeaponBuildupBackup backup;
        for (int index = 0; index < 3; ++index) backup.buildup_weapon[index] = from_weapon->buildup_weapon[index];
        g_original_weapon_buildups.emplace(from_no, backup);
    }
    bool placed = false;
    for (int index = 0; index < 3; ++index) {
        if (from_weapon->buildup_weapon[index] == to_no) {
            placed = true;
            break;
        }
        if (from_weapon->buildup_weapon[index] <= 0) {
            from_weapon->buildup_weapon[index] = static_cast<s16>(to_no);
            placed = true;
            break;
        }
    }
    if (!placed) {
        from_weapon->buildup_weapon[2] = static_cast<s16>(to_no);
    }
    auto &list = g_mod_weapon_buildups[from_no];
    if (std::ranges::find(list, to_no) == list.end()) list.push_back(to_no);
    return {ApiCallStatus::Ok,
            {{"from", ChronicleTwoWeaponStableId(from_no)},
             {"to", ChronicleTwoWeaponStableId(to_no)},
             {"from_item_no", from_no}, {"to_item_no", to_no},
             {"registered", true}},
            {}};
}

ApiCallResult SpectrumizeWeapon(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) {
        return Invalid("expected {id = owned_weapon_id}");
    }
    const ApiCallResult definition = GetWeapon(args);
    if (definition.status != ApiCallStatus::Ok) return definition;
    const int item_no = definition.value.value("native_item_no", 0);
    if (item_no <= 0) return Failed("weapon definition is unavailable in the native weapon tables");
    CUserDataManager *user_data = GetUserDataMan();
    if (user_data == nullptr) return Failed("the game has no active save data");
    if (user_data->SearchEquip(USER_CHARA_MAX, item_no) != nullptr ||
        user_data->SearchEquip(USER_CHARA_MONICA, item_no) != nullptr) {
        return Failed("cannot spectrumize a currently equipped weapon");
    }
    CGameDataUsed *target = user_data->SearchAllHaveItem(item_no);
    if (target == nullptr || target->used_type != USED_ITEM_TYPE_WEAPON || target->item_no != item_no) {
        return Failed("weapon is not in the unequipped inventory bag");
    }
    CGameDataUsed spectol{};
    target->ToSpectolTrans(&spectol, 1);
    const int spectol_item_no = spectol.item_no > 0 ? spectol.item_no : 0xB9;
    *target = spectol;
    return {ApiCallStatus::Ok,
            {{"id", definition.value.value("id", ChronicleTwoWeaponStableId(item_no))},
             {"native_item_no", item_no},
             {"spectrumized_item_no", spectol_item_no},
             {"spectrumized", true},
             {"changed", true}},
            {}};
}

void EnsureChronicleTwoShopsInitialized() {
    if (g_shops_initialized) return;
    g_shops_initialized = true;
    for (int shop_no = 0; shop_no < 34; ++shop_no) {
        ChronicleTwoShop &shop = g_shops[static_cast<std::size_t>(shop_no)];
        shop.shop_no = shop_no;
        if (shop_no == 23 || shop_no == 28) {
            shop.currency = "robo_abs";
        } else if (shop_no == 32) {
            shop.currency = "medal";
        } else if (shop_no == 33) {
            shop.currency = "dony";
        } else {
            shop.currency = "gilda";
        }
        shop.goods = {221, 224, 240};
    }
    for (int item_no = 1; item_no < ITEM_NO_MAX; ++item_no) {
        g_shop_prices[item_no] = {100, 50};
    }
}

bool ReadChronicleTwoShopNo(const Json &selector, int &shop_no) {
    if (selector.is_number_integer()) {
        const auto val = selector.get<std::int64_t>();
        if (val < 0 || val >= 34) return false;
        shop_no = static_cast<int>(val);
        return true;
    }
    if (selector.is_string()) {
        const std::string &id = selector.get_ref<const std::string &>();
        constexpr std::string_view prefix = "chronicle-two:shop:";
        if (!std::string_view(id).starts_with(prefix)) return false;
        const std::string_view suffix = std::string_view(id).substr(prefix.size());
        int parsed = -1;
        const auto [end, ec] = std::from_chars(suffix.data(), suffix.data() + suffix.size(), parsed);
        if (ec != std::errc{} || end != suffix.data() + suffix.size() || parsed < 0 || parsed >= 34) return false;
        shop_no = parsed;
        return true;
    }
    if (selector.is_object()) {
        if (selector.contains("shop_no")) return ReadInt(selector, "shop_no", -1, 0, 33, shop_no);
        if (selector.contains("id")) return ReadChronicleTwoShopNo(selector["id"], shop_no);
    }
    return false;
}

Json ChronicleTwoShopJson(int shop_no) {
    EnsureChronicleTwoShopsInitialized();
    const ChronicleTwoShop &shop = g_shops[static_cast<std::size_t>(shop_no)];
    Json items = Json::array();
    for (int index = 0; index < static_cast<int>(shop.goods.size()); ++index) {
        const int item_no = shop.goods[static_cast<std::size_t>(index)];
        const auto price_it = g_shop_prices.find(item_no);
        const int buy_price = price_it != g_shop_prices.end() ? price_it->second.buy_price : 100;
        const int sell_price = price_it != g_shop_prices.end() ? price_it->second.sell_price : 50;
        items.push_back({{"slot", index},
                         {"item_no", item_no},
                         {"id", ChronicleTwoItemOrWeaponStableId(item_no)},
                         {"buy_price", buy_price},
                         {"sell_price", sell_price}});
    }
    return {{"id", "chronicle-two:shop:" + std::to_string(shop_no)},
            {"shop_no", shop_no},
            {"currency", shop.currency},
            {"item_count", items.size()},
            {"items", std::move(items)}};
}

ApiCallResult ListShops(const Json &args) {
    if (!args.is_array() || !args.empty()) return Invalid("shops.list takes no arguments");
    EnsureChronicleTwoShopsInitialized();
    Json shops = Json::array();
    for (int shop_no = 0; shop_no < 34; ++shop_no) shops.push_back(ChronicleTwoShopJson(shop_no));
    return {ApiCallStatus::Ok, {{"shops", std::move(shops)}}, {}};
}

ApiCallResult GetShop(const Json &args) {
    if (!args.is_array() || args.size() != 1) return Invalid("expected one shop id or shop_no (0..33)");
    int shop_no = -1;
    if (!ReadChronicleTwoShopNo(args[0], shop_no)) return Invalid("expected chronicle-two:shop:<0..33> or shop_no 0..33");
    return {ApiCallStatus::Ok, ChronicleTwoShopJson(shop_no), {}};
}

ApiCallResult RegisterShopItem(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() ||
        !args[0].contains("item_id")) {
        return Invalid("expected {shop_no|id, item_id = item_or_weapon_id}");
    }
    int shop_no = -1;
    if (!ReadChronicleTwoShopNo(args[0], shop_no)) return Invalid("expected valid shop_no (0..33) or id");
    int item_no = -1;
    if (!ReadChronicleTwoAnyItemOrWeaponNo(args[0]["item_id"], item_no)) {
        return Invalid("item_id must identify a valid Chronicle Two item or weapon");
    }
    EnsureChronicleTwoShopsInitialized();
    ChronicleTwoShop &shop = g_shops[static_cast<std::size_t>(shop_no)];
    for (int index = 0; index < static_cast<int>(shop.goods.size()); ++index) {
        if (shop.goods[static_cast<std::size_t>(index)] == item_no) {
            return {ApiCallStatus::Ok,
                    {{"id", "chronicle-two:shop:" + std::to_string(shop_no)}, {"shop_no", shop_no},
                     {"item_no", item_no}, {"slot", index}, {"added", false}, {"idempotent", true}},
                    {}};
        }
    }
    if (shop.goods.size() >= 32) return Failed("shop already holds the maximum number of goods");
    const int slot = static_cast<int>(shop.goods.size());
    shop.goods.push_back(item_no);
    return {ApiCallStatus::Ok,
            {{"id", "chronicle-two:shop:" + std::to_string(shop_no)}, {"shop_no", shop_no},
             {"item_no", item_no}, {"slot", slot}, {"added", true}, {"changed", true}},
            {}};
}

ApiCallResult RemoveShopItem(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() ||
        !args[0].contains("item_id")) {
        return Invalid("expected {shop_no|id, item_id = item_or_weapon_id}");
    }
    int shop_no = -1;
    if (!ReadChronicleTwoShopNo(args[0], shop_no)) return Invalid("expected valid shop_no (0..33) or id");
    int item_no = -1;
    if (!ReadChronicleTwoAnyItemOrWeaponNo(args[0]["item_id"], item_no)) {
        return Invalid("item_id must identify a valid Chronicle Two item or weapon");
    }
    EnsureChronicleTwoShopsInitialized();
    ChronicleTwoShop &shop = g_shops[static_cast<std::size_t>(shop_no)];
    const auto found = std::ranges::find(shop.goods, item_no);
    if (found == shop.goods.end()) return Failed("item is not listed in the specified shop");
    shop.goods.erase(found);
    return {ApiCallStatus::Ok,
            {{"id", "chronicle-two:shop:" + std::to_string(shop_no)}, {"shop_no", shop_no},
             {"item_no", item_no}, {"removed", true}, {"changed", true}},
            {}};
}

ApiCallResult SetShopPrice(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() ||
        !args[0].contains("item_id") ||
        (!args[0].contains("buy_price") && !args[0].contains("sell_price"))) {
        return Invalid("expected {item_id, buy_price = optional 0..999999, sell_price = optional 0..999999}");
    }
    int item_no = -1;
    if (!ReadChronicleTwoAnyItemOrWeaponNo(args[0]["item_id"], item_no)) {
        return Invalid("item_id must identify a valid Chronicle Two item or weapon");
    }
    EnsureChronicleTwoShopsInitialized();
    ChronicleTwoShopPrice &price = g_shop_prices[item_no];
    int buy_price = price.buy_price;
    int sell_price = price.sell_price;
    if (args[0].contains("buy_price") && !ReadInt(args[0], "buy_price", buy_price, 0, 999999, buy_price)) {
        return Invalid("buy_price must be an integer from 0 to 999999");
    }
    if (args[0].contains("sell_price") && !ReadInt(args[0], "sell_price", sell_price, 0, 999999, sell_price)) {
        return Invalid("sell_price must be an integer from 0 to 999999");
    }
    price.buy_price = buy_price;
    price.sell_price = sell_price;
    return {ApiCallStatus::Ok,
            {{"item_no", item_no}, {"buy_price", buy_price}, {"sell_price", sell_price}, {"changed", true}},
            {}};
}

std::string_view SealName(int seal) {
    switch (seal) {
        case DNGMAP_SEAL_MONICA: return "monica";
        case DNGMAP_SEAL_MAX: return "max";
        default: return "none";
    }
}

ApiCallResult GetDungeonFloorRules(const Json &args) {
    if (!args.is_array() || args.size() > 1 || (args.size() == 1 && !args[0].is_object())) {
        return Invalid("expected optional {dungeon_id = 0..6, floor_id = 0..39}");
    }
    int dungeon_id = (GetNowLoopNo() == LOOP_DUNGEON && DngStatus.dungeon_no >= 0 && DngStatus.dungeon_no <= DNGMAP_DUNGEON_MAX)
                         ? DngStatus.dungeon_no
                         : 0;
    int floor_id = 0;
    if (CSaveData *save = GetSaveData(); save != nullptr && dungeon_id >= 0 && dungeon_id < SAVE_DUNGEON_NUM) {
        if (save->save_dungeon.floor_id[dungeon_id] >= 0 && save->save_dungeon.floor_id[dungeon_id] < DNGMAP_FLOOR_NUM) {
            floor_id = save->save_dungeon.floor_id[dungeon_id];
        }
    }
    if (args.size() == 1) {
        if (args[0].contains("dungeon_id") && !ReadInt(args[0], "dungeon_id", dungeon_id, 0, DNGMAP_DUNGEON_MAX, dungeon_id)) {
            return Invalid("dungeon_id must be an integer from 0 to 6");
        }
        if (args[0].contains("floor_id") && !ReadInt(args[0], "floor_id", floor_id, 0, DNGMAP_FLOOR_NUM - 1, floor_id)) {
            return Invalid("floor_id must be an integer from 0 to 39");
        }
    }
    ChronicleTwoFloorRuleOverride rules{};
    if (const auto found = g_floor_rule_overrides.find({dungeon_id, floor_id}); found != g_floor_rule_overrides.end()) {
        rules = found->second;
    }
    return {ApiCallStatus::Ok,
            {{"dungeon_id", dungeon_id}, {"floor_id", floor_id},
             {"seal", rules.seal}, {"seal_name", SealName(rules.seal)},
             {"limit_zone", rules.seal}, {"limit_zone_name", SealName(rules.seal)},
             {"spheda", rules.spheda}, {"fast_destroy_time", rules.fast_destroy_time}},
            {}};
}

ApiCallResult SetDungeonFloorRules(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() ||
        (!args[0].contains("seal") && !args[0].contains("limit_zone") &&
         !args[0].contains("spheda") && !args[0].contains("fast_destroy_time"))) {
        return Invalid("expected {seal|limit_zone, spheda = optional_bool, fast_destroy_time = optional_int, dungeon_id = optional, floor_id = optional}");
    }
    int dungeon_id = (GetNowLoopNo() == LOOP_DUNGEON && DngStatus.dungeon_no >= 0 && DngStatus.dungeon_no <= DNGMAP_DUNGEON_MAX)
                         ? DngStatus.dungeon_no
                         : 0;
    int floor_id = 0;
    if (CSaveData *save = GetSaveData(); save != nullptr && dungeon_id >= 0 && dungeon_id < SAVE_DUNGEON_NUM) {
        if (save->save_dungeon.floor_id[dungeon_id] >= 0 && save->save_dungeon.floor_id[dungeon_id] < DNGMAP_FLOOR_NUM) {
            floor_id = save->save_dungeon.floor_id[dungeon_id];
        }
    }
    if (args[0].contains("dungeon_id") && !ReadInt(args[0], "dungeon_id", dungeon_id, 0, DNGMAP_DUNGEON_MAX, dungeon_id)) {
        return Invalid("dungeon_id must be an integer from 0 to 6");
    }
    if (args[0].contains("floor_id") && !ReadInt(args[0], "floor_id", floor_id, 0, DNGMAP_FLOOR_NUM - 1, floor_id)) {
        return Invalid("floor_id must be an integer from 0 to 39");
    }
    ChronicleTwoFloorRuleOverride &rules = g_floor_rule_overrides[{dungeon_id, floor_id}];
    const int previous_seal = rules.seal;
    int next_seal = rules.seal;
    const Json *seal_val = args[0].contains("seal") ? &args[0]["seal"]
                           : args[0].contains("limit_zone") ? &args[0]["limit_zone"]
                                                            : nullptr;
    if (seal_val != nullptr) {
        if (seal_val->is_string()) {
            const std::string &s = seal_val->get_ref<const std::string &>();
            if (s == "none") next_seal = DNGMAP_SEAL_NONE;
            else if (s == "monica") next_seal = DNGMAP_SEAL_MONICA;
            else if (s == "max") next_seal = DNGMAP_SEAL_MAX;
            else return Invalid("seal must be 'none', 'monica', 'max', or 0..2");
        } else if (seal_val->is_number_integer()) {
            const auto v = seal_val->get<std::int64_t>();
            if (v < 0 || v > 2) return Invalid("seal/limit_zone must be an integer from 0 to 2");
            next_seal = static_cast<int>(v);
        } else {
            return Invalid("seal must be 'none', 'monica', 'max', or 0..2");
        }
    }
    bool next_spheda = rules.spheda;
    if (args[0].contains("spheda")) {
        if (!args[0]["spheda"].is_boolean()) return Invalid("spheda must be a boolean");
        next_spheda = args[0]["spheda"].get<bool>();
    }
    int next_fast_time = rules.fast_destroy_time;
    if (args[0].contains("fast_destroy_time") &&
        !ReadInt(args[0], "fast_destroy_time", next_fast_time, 0, 216000, next_fast_time)) {
        return Invalid("fast_destroy_time must be an integer from 0 to 216000");
    }
    const bool changed = rules.seal != next_seal || rules.spheda != next_spheda || rules.fast_destroy_time != next_fast_time;
    rules.seal = next_seal;
    rules.spheda = next_spheda;
    rules.fast_destroy_time = next_fast_time;
    return {ApiCallStatus::Ok,
            {{"dungeon_id", dungeon_id}, {"floor_id", floor_id},
             {"previous_seal", previous_seal}, {"seal", rules.seal},
             {"seal_name", SealName(rules.seal)},
             {"previous_limit_zone", previous_seal}, {"limit_zone", rules.seal},
             {"limit_zone_name", SealName(rules.seal)},
             {"spheda", rules.spheda}, {"fast_destroy_time", rules.fast_destroy_time},
             {"changed", changed}},
            {}};
}

ApiCallResult GetDungeonLoot(const Json &args) {
    if (!args.is_array() || args.size() > 1 || (args.size() == 1 && !args[0].is_object())) {
        return Invalid("expected optional {monster_id = 0..343}");
    }
    int monster_id = 0;
    if (args.size() == 1 && args[0].contains("monster_id")) {
        if (!ReadInt(args[0], "monster_id", 0, 0, 32767, monster_id)) {
            return Invalid("monster_id must be a valid native monster id");
        }
    }
    BASE_MONSTER_TBL *monster = GetMonsterTable(monster_id);
    if (monster == nullptr) return Invalid("monster_id does not identify a loaded native monster definition");
    return {ApiCallStatus::Ok,
            {{"monster_id", monster_id},
             {"drop_items", {monster->drop_item[0], monster->drop_item[1], monster->drop_item[2]}},
             {"rare_item", monster->drop_item[2]},
             {"steal_item", monster->drop_item[0]},
             {"reward_exp", monster->reward_exp},
             {"reward_money", monster->reward_money}},
            {}};
}

ApiCallResult OverrideDungeonLoot(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() ||
        !args[0].contains("monster_id")) {
        return Invalid("expected {monster_id = 0..343, drop_items = optional, rare_item = optional, steal_item = optional}");
    }
    int monster_id = -1;
    if (!ReadInt(args[0], "monster_id", -1, 0, 32767, monster_id)) {
        return Invalid("monster_id must be a valid native monster id");
    }
    BASE_MONSTER_TBL *monster = GetMonsterTable(monster_id);
    if (monster == nullptr) return Invalid("monster_id does not identify a loaded native monster definition");
    std::array<int, 3> next_drops = {monster->drop_item[0], monster->drop_item[1], monster->drop_item[2]};
    if (args[0].contains("drop_items")) {
        const Json &arr = args[0]["drop_items"];
        if (!arr.is_array() || arr.size() != 3) return Invalid("drop_items must be an array of 3 item numbers");
        for (std::size_t i = 0; i < 3; ++i) {
            if (!arr[i].is_number_integer()) return Invalid("drop_items entries must be integers");
            const auto val = arr[i].get<std::int64_t>();
            if (val < -1 || val >= ITEM_NO_MAX) return Invalid("drop_items entries must be -1 or a valid item number");
            next_drops[i] = static_cast<int>(val);
        }
    }
    if (args[0].contains("rare_item") && !ReadInt(args[0], "rare_item", next_drops[2], -1, ITEM_NO_MAX - 1, next_drops[2])) {
        return Invalid("rare_item must be -1 or a valid Chronicle Two item number");
    }
    if (args[0].contains("steal_item") && !ReadInt(args[0], "steal_item", next_drops[0], -1, ITEM_NO_MAX - 1, next_drops[0])) {
        return Invalid("steal_item must be -1 or a valid Chronicle Two item number");
    }
    if (!g_original_monster_drops.contains(monster_id)) {
        g_original_monster_drops.emplace(
            monster_id, ChronicleTwoMonsterDropBackup{{monster->drop_item[0], monster->drop_item[1], monster->drop_item[2]}});
    }
    bool changed = false;
    for (int i = 0; i < 3; ++i) {
        if (monster->drop_item[i] != next_drops[static_cast<std::size_t>(i)]) {
            monster->drop_item[i] = static_cast<s16>(next_drops[static_cast<std::size_t>(i)]);
            changed = true;
        }
    }
    return {ApiCallStatus::Ok,
            {{"monster_id", monster_id},
             {"drop_items", {monster->drop_item[0], monster->drop_item[1], monster->drop_item[2]}},
             {"rare_item", monster->drop_item[2]},
             {"steal_item", monster->drop_item[0]},
             {"changed", changed}},
            {}};
}

Json DungeonLayoutJson() {
    return {{"rooms", DngLayoutRooms > 0 ? Json(DngLayoutRooms) : Json("native")},
            {"area", DNG_BUILD_AREA}, {"room_min", DNG_BUILD_ROOM_MIN},
            {"room_max", DNG_BUILD_ROOM_MIN + DNG_BUILD_ROOM_SPAN - 1},
            {"grid", DNG_GRID}, {"room_limit", DNG_ROOM_MAX}};
}

ApiCallResult SetDungeonLayout(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) {
        return Invalid("expected {rooms = 2-" + std::to_string(DNG_ROOM_MAX) + ", area = 8-" + std::to_string(DNG_GRID) +
                       ", room_min = 2-6, room_max = room_min-6}, or {} for the native layout");
    }
    const Json &request = args[0];
    if (request.empty()) {
        DngLayoutRooms = DngLayoutArea = DngLayoutRoomMin = DngLayoutRoomMax = 0;
        return {ApiCallStatus::Ok, DungeonLayoutJson(), {}};
    }
    for (auto field = request.begin(); field != request.end(); ++field) {
        if (field.key() != "rooms" && field.key() != "area" && field.key() != "room_min" && field.key() != "room_max") {
            return Invalid("unsupported dungeon layout field '" + field.key() + "'");
        }
    }
    int rooms = 0, area = DNG_GRID, room_min = 3, room_max = 4;
    if (!request.contains("rooms") || !ReadInt(request, "rooms", 0, 2, DNG_ROOM_MAX, rooms)) {
        return Invalid("rooms must be an integer from 2 to " + std::to_string(DNG_ROOM_MAX));
    }
    if (request.contains("area") && !ReadInt(request, "area", DNG_GRID, 8, DNG_GRID, area)) {
        return Invalid("area must be an integer from 8 to " + std::to_string(DNG_GRID));
    }
    if (request.contains("room_min") && !ReadInt(request, "room_min", 3, 2, 6, room_min)) {
        return Invalid("room_min must be an integer from 2 to 6");
    }
    if (request.contains("room_max") && !ReadInt(request, "room_max", 4, room_min, 6, room_max)) {
        return Invalid("room_max must be an integer from room_min to 6");
    }
    if (room_max < room_min) room_max = room_min;
    if (room_max + 1 > area) return Invalid("area must be larger than room_max");
    DngLayoutRooms = rooms;
    DngLayoutArea = area;
    DngLayoutRoomMin = room_min;
    DngLayoutRoomMax = room_max;
    return {ApiCallStatus::Ok, DungeonLayoutJson(), {}};
}

ApiCallResult SetActiveMonsterLimit(const Json &args) {
    int active = 0;
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || args[0].size() != 1 ||
        !args[0].contains("active") || !ReadInt(args[0], "active", 0, 1, MONSTER_ACTIVE_MAX, active)) {
        return Invalid("expected {active = 1-" + std::to_string(MONSTER_ACTIVE_MAX) + "}");
    }
    const int previous = MonsterActiveLimit > 0     ? MonsterActiveLimit
                         : ActiveMonster != nullptr ? ActiveMonster->priority_limit
                                                    : 5;
    MonsterActiveLimit = active;
    if (ActiveMonster != nullptr) {
        ActiveMonster->priority_limit = active;
    }
    return {ApiCallStatus::Ok, {{"active", active}, {"previous", previous}, {"slots", MONSTER_ACTIVE_MAX}}, {}};
}

std::string SignatureItemStableId(int item_no) {
    if (item_no <= 0) return {};
    for (const auto &[id, item] : g_items) {
        if (item.native_item_no == item_no) return id;
    }
    for (const auto &[id, weapon] : g_weapons) {
        if (weapon.native_item_no == item_no) return id;
    }
    if (FindWeaponCommon(item_no) != nullptr) return NativeWeaponId(item_no);
    if (NativeItemTablesValid()) return NativeItemId(item_no);
    return "chronicle-two:item:" + std::to_string(item_no);
}

CUserDataManager *SignatureUserData() { return GetUserDataMan(); }

bool SignatureResolveItemSelector(const Json &selector, int &item_no, std::string &stable_id,
                                  std::string &error) {
    stable_id.clear();
    if (selector.is_number_integer()) {
        const auto value = selector.get<std::int64_t>();
        if (value < 0 || value > 32767) {
            error = "native item number must be an integer from 0 to 32767";
            return false;
        }
        item_no = static_cast<int>(value);
    } else if (selector.is_string()) {
        const std::string id = selector.get<std::string>();
        if (ResolveItemId(id, item_no, error) != ApiCallStatus::Ok) return false;
        stable_id = id;
    } else {
        error = "item must be a stable id or native item number";
        return false;
    }
    if (stable_id.empty()) stable_id = SignatureItemStableId(item_no);
    return true;
}

Json RoboPartJson(CGameDataUsed &part, const char *slot) {
    Json result = {{"slot", slot}, {"item_no", part.item_no}, {"used_type", part.used_type},
                   {"item_type", part.item_type}};
    if (part.item_no > 0) result["id"] = SignatureItemStableId(part.item_no);
    if (part.used_type == USED_ITEM_TYPE_ROBO_PART) {
        result["defence"] = part.data.robopart.defence;
        result["energy"] = {{"now", part.data.robopart.energy.now}, {"max", part.data.robopart.energy.max}};
        result["durability"] = {{"now", part.data.robopart.whp.now}, {"max", part.data.robopart.whp.max}};
        result["use_capacity"] = part.GetUseCapacity();
    }
    return result;
}

Json RidepodJson(CUserDataManager &user_data) {
    ROBO_DATA &robo = user_data.robo_data;
    int capacity = 0;
    const int used_capacity = CheckNowRoboUseCapacity(&robo, &capacity);
    const std::size_t name_length = std::find(robo.name, robo.name + sizeof(robo.name), '\0') - robo.name;
    return {{"fuel", robo.hp.now},
            {"max_fuel", robo.hp.max},
            {"core_capacity", capacity},
            {"used_capacity", used_capacity},
            {"defense", robo.GetDefenceVol()},
            {"shield_kits", robo.shield_kit_num},
            {"name", std::string(robo.name, name_length)},
            {"voice_unit", robo.voice_unit != 0},
            {"voice_enabled", robo.voice_flag != 0},
            {"parts", {{"arm", RoboPartJson(robo.parts[ROBO_PART_ARM], "arm")},
                       {"body", RoboPartJson(robo.parts[ROBO_PART_BODY], "body")},
                       {"leg", RoboPartJson(robo.parts[ROBO_PART_LEG], "leg")},
                       {"battery", RoboPartJson(robo.parts[ROBO_PART_PACK], "battery")}}}};
}

ApiCallResult GetRidepod(const Json &args) {
    if (!args.is_array() || !args.empty()) return Invalid("player.ridepod_get takes no arguments");
    CUserDataManager *user_data = SignatureUserData();
    if (user_data == nullptr) return Failed("the game has no active save inventory");
    return {ApiCallStatus::Ok, RidepodJson(*user_data), {}};
}

bool RoboPartSlotFromName(std::string_view name, int &slot, int &item_type) {
    if (name == "arm") {
        slot = ROBO_PART_ARM;
        item_type = ITEM_DATA_ROBO_ARM;
        return true;
    }
    if (name == "body") {
        slot = ROBO_PART_BODY;
        item_type = ITEM_DATA_ROBO_BODY;
        return true;
    }
    if (name == "leg") {
        slot = ROBO_PART_LEG;
        item_type = ITEM_DATA_ROBO_LEG;
        return true;
    }
    if (name == "battery" || name == "pack") {
        slot = ROBO_PART_PACK;
        item_type = ITEM_DATA_ROBO_ENERGY_PACK;
        return true;
    }
    return false;
}

ApiCallResult SetRidepod(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) {
        return Invalid("expected {fuel|restore|max_fuel|shield_kits|voice_enabled|voice_unit|parts}");
    }
    const Json &request = args[0];
    if (request.empty()) return Invalid("player.ridepod_set requires at least one field");
    for (auto field = request.begin(); field != request.end(); ++field) {
        if (field.key() != "fuel" && field.key() != "restore" && field.key() != "max_fuel" &&
            field.key() != "shield_kits" && field.key() != "voice_enabled" && field.key() != "voice_unit" &&
            field.key() != "parts") {
            return Invalid("unsupported ridepod field '" + field.key() + "'");
        }
    }
    CUserDataManager *user_data = SignatureUserData();
    if (user_data == nullptr) return Failed("the game has no active save inventory");
    ROBO_DATA &robo = user_data->robo_data;
    const Json previous = RidepodJson(*user_data);
    if (request.contains("max_fuel")) {
        if (!request["max_fuel"].is_number()) return Invalid("max_fuel must be a finite number");
        const double value = request["max_fuel"].get<double>();
        if (!std::isfinite(value) || value <= 0.0 || value > 99999.0) {
            return Invalid("max_fuel must be greater than 0 and at most 99999");
        }
        robo.hp.max = static_cast<float>(value);
        robo.hp.now = std::clamp(robo.hp.now, 0.0f, robo.hp.max);
    }
    if (request.contains("fuel")) {
        if (!request["fuel"].is_number()) return Invalid("fuel must be a finite number");
        const double value = request["fuel"].get<double>();
        if (!std::isfinite(value) || value < 0.0) return Invalid("fuel must be a non-negative finite number");
        robo.hp.now = std::clamp(static_cast<float>(value), 0.0f, robo.hp.max);
    }
    if (request.contains("restore")) {
        if (!request["restore"].is_number()) return Invalid("restore must be a finite number");
        const double value = request["restore"].get<double>();
        if (!std::isfinite(value)) return Invalid("restore must be a finite number");
        robo.hp.now = std::clamp(robo.hp.now + static_cast<float>(value), 0.0f, robo.hp.max);
    }
    if (request.contains("shield_kits")) {
        int kits = 0;
        if (!ReadInt(request, "shield_kits", 0, 0, 999, kits)) {
            return Invalid("shield_kits must be an integer from 0 to 999");
        }
        robo.shield_kit_num = static_cast<u16>(kits);
    }
    if (request.contains("voice_enabled") || request.contains("voice_unit")) {
        const Json &value = request.contains("voice_enabled") ? request["voice_enabled"] : request["voice_unit"];
        if (!value.is_boolean()) return Invalid("voice_enabled and voice_unit must be booleans");
        const s8 flag = value.get<bool>() ? 1 : 0;
        if (request.contains("voice_enabled")) robo.voice_flag = flag;
        if (request.contains("voice_unit")) robo.voice_unit = flag;
    }
    if (request.contains("parts")) {
        const Json &parts = request["parts"];
        if (!parts.is_object() || parts.empty()) return Invalid("parts must be a non-empty table");
        for (auto part = parts.begin(); part != parts.end(); ++part) {
            int slot = -1;
            int item_type = 0;
            if (!RoboPartSlotFromName(part.key(), slot, item_type)) {
                return Invalid("parts supports arm, body, leg, and battery");
            }
            int item_no = 0;
            std::string stable_id;
            std::string error;
            if (!SignatureResolveItemSelector(part.value(), item_no, stable_id, error)) {
                return Invalid("invalid " + part.key() + " part: " + error);
            }
            if (robo.parts[slot].item_no == item_no) continue;
            int inventory_slot = -1;
            for (int index = 0; index < 150; ++index) {
                const CGameDataUsed &candidate = user_data->used_data[index];
                if (candidate.used_type == USED_ITEM_TYPE_ROBO_PART && candidate.item_no == item_no &&
                    candidate.item_type == item_type) {
                    inventory_slot = index;
                    break;
                }
            }
            if (inventory_slot < 0) {
                return Failed("the requested ridepod part is not owned in the inventory");
            }
            GameDataSwap(&user_data->used_data[inventory_slot], &robo.parts[slot], 0);
            if (robo.parts[slot].item_no != item_no) {
                return Failed("the native equipment swap did not install the requested ridepod part");
            }
        }
    }
    Json current = RidepodJson(*user_data);
    return {ApiCallStatus::Ok,
            {{"previous", std::move(previous)}, {"ridepod", std::move(current)}, {"changed", true}},
            {}};
}

Json MonsterBadgeJson(MOS_CHANGE_PARAM &badge, int badge_no, bool active) {
    return {{"badge", badge_no},
            {"monster_id", badge.monster_id},
            {"enabled", badge.enable != 0},
            {"active_form", active},
            {"level", badge.level},
            {"class_level", badge.class_level},
            {"progress", badge.progress},
            {"degree", badge.GetDegreeLevel()},
            {"experience", badge.abs.now},
            {"experience_max", badge.abs.max},
            {"hp", badge.hp.now},
            {"max_hp", badge.hp.max},
            {"attack", badge.GetAttackVol(-1)},
            {"defense", badge.GetDefenceVol(-1)},
            {"can_class_change", badge.CheckClassChange() != 0}};
}

ApiCallResult GetTransform(const Json &args) {
    if (!args.is_array() || !args.empty()) return Invalid("player.transform_get takes no arguments");
    CUserDataManager *user_data = SignatureUserData();
    if (user_data == nullptr) return Failed("the game has no active save inventory");
    Json badges = Json::array();
    int enabled_count = 0;
    for (int index = 0; index < 64; ++index) {
        MOS_CHANGE_PARAM &badge = user_data->monster_box.monster[index];
        const bool enabled = badge.enable != 0;
        if (enabled) ++enabled_count;
        badges.push_back(MonsterBadgeJson(badge, index + 1, badge.monster_id == user_data->monster_id && enabled));
    }
    return {ApiCallStatus::Ok,
            {{"active_monster_id", user_data->monster_id},
             {"unlocked_count", enabled_count},
             {"badges", std::move(badges)}},
            {}};
}

ApiCallResult SetTransform(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) {
        return Invalid("expected {badge = 1..64 or monster_id = native id, and one of enabled|level|experience}");
    }
    const Json &request = args[0];
    for (auto field = request.begin(); field != request.end(); ++field) {
        if (field.key() != "badge" && field.key() != "monster_id" && field.key() != "enabled" &&
            field.key() != "level" && field.key() != "experience") {
            return Invalid("unsupported monster badge field '" + field.key() + "'");
        }
    }
    const bool has_badge = request.contains("badge");
    const bool has_monster = request.contains("monster_id");
    if (has_badge == has_monster) return Invalid("player.transform_set requires exactly one of badge or monster_id");
    if (!request.contains("enabled") && !request.contains("level") && !request.contains("experience")) {
        return Invalid("player.transform_set requires enabled, level, or experience");
    }
    CUserDataManager *user_data = SignatureUserData();
    if (user_data == nullptr) return Failed("the game has no active save inventory");
    int badge_index = -1;
    if (has_badge) {
        int badge_no = 0;
        if (!ReadInt(request, "badge", 0, 1, 64, badge_no)) return Invalid("badge must be an integer from 1 to 64");
        badge_index = badge_no - 1;
    } else {
        int monster_id = -1;
        if (!ReadInt(request, "monster_id", -1, 0, 32767, monster_id)) {
            return Invalid("monster_id must be a native monster id");
        }
        for (int index = 0; index < 64; ++index) {
            if (user_data->monster_box.monster[index].monster_id == monster_id) {
                badge_index = index;
                break;
            }
        }
        if (badge_index < 0) return Failed("no monster badge has that monster id");
    }
    MOS_CHANGE_PARAM &badge = user_data->monster_box.monster[badge_index];
    const Json previous = MonsterBadgeJson(badge, badge_index + 1,
                                           badge.monster_id == user_data->monster_id && badge.enable != 0);
    if (request.contains("enabled")) {
        if (!request["enabled"].is_boolean()) return Invalid("enabled must be a boolean");
        badge.enable = request["enabled"].get<bool>() ? 1 : 0;
    }
    if (request.contains("level")) {
        int level = 0;
        if (!ReadInt(request, "level", 0, 0, 98, level)) return Invalid("level must be an integer from 0 to 98");
        badge.level = static_cast<s16>(level);
        int bonus = 0;
        if (level > 49) bonus = (level - 49) * 25;
        badge.abs.max = static_cast<float>(level * 100 + 100 + bonus);
        badge.abs.now = std::clamp(badge.abs.now, 0.0f, badge.abs.max);
    }
    if (request.contains("experience")) {
        if (!request["experience"].is_number()) return Invalid("experience must be a finite number");
        const double value = request["experience"].get<double>();
        if (!std::isfinite(value) || value < 0.0 || value > 999999.0) {
            return Invalid("experience must be between 0 and 999999");
        }
        if (badge.abs.max < 1.0f) badge.abs.max = 1.0f;
        badge.abs.now = std::clamp(static_cast<float>(value), 0.0f, badge.abs.max);
    }
    return {ApiCallStatus::Ok,
            {{"previous", std::move(previous)},
             {"badge", MonsterBadgeJson(badge, badge_index + 1,
                                        badge.monster_id == user_data->monster_id && badge.enable != 0)},
             {"changed", true}},
            {}};
}

CInventUserData *SignatureInventData() {
    CUserDataManager *user_data = SignatureUserData();
    return user_data == nullptr ? nullptr : &user_data->invent_data;
}

ApiCallResult ListPhotoIdeas(const Json &args) {
    if (!args.is_array() || !args.empty()) return Invalid("photos.ideas_list takes no arguments");
    CInventUserData *invent = SignatureInventData();
    if (invent == nullptr) return Failed("the game has no active save inventory");
    Json ideas = Json::array();
    for (int slot = 0; slot < 0x200; ++slot) {
        if (invent->neta_id[slot] <= 0) continue;
        ideas.push_back({{"slot", slot}, {"idea_id", invent->neta_id[slot]},
                         {"scoop", invent->neta_id[slot] >= INVENT_NETA_ID_SCOOP &&
                                       invent->neta_id[slot] < INVENT_NETA_ID_SCOOP_END}});
    }
    Json photos = Json::array();
    int photo_count = 0;
    for (int slot = 0; slot < INVENT_PHOTO_NUM; ++slot) {
        const USER_PICTURE_INFO &photo = invent->photo[slot];
        if (photo.used == 0) continue;
        ++photo_count;
        photos.push_back({{"slot", slot}, {"neta_id", photo.neta_id}, {"map_no", photo.map_no},
                          {"npc_no", photo.npc_no}, {"monster_no", photo.monster_no},
                          {"new", photo.is_new != 0}});
    }
    return {ApiCallStatus::Ok,
            {{"idea_count", ideas.size()},
             {"photo_count", photo_count},
             {"photo_slots", INVENT_PHOTO_NUM},
             {"ideas", std::move(ideas)},
             {"photos", std::move(photos)}},
            {}};
}

ApiCallResult UnlockPhotoIdea(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || args[0].size() != 1) {
        return Invalid("expected {idea_id = 1..999} or {id = 1..999}");
    }
    const Json *value = args[0].contains("idea_id") ? &args[0]["idea_id"]
                        : args[0].contains("id")     ? &args[0]["id"]
                                                     : nullptr;
    if (value == nullptr || !value->is_number_integer()) {
        return Invalid("idea_id must be an integer from 1 to 999");
    }
    const auto idea = value->get<std::int64_t>();
    if (idea < 1 || idea > 999) return Invalid("idea_id must be an integer from 1 to 999");
    CInventUserData *invent = SignatureInventData();
    if (invent == nullptr) return Failed("the game has no active save inventory");
    const int previous_slot = invent->CheckNetaFlag(static_cast<int>(idea));
    if (previous_slot >= 0) {
        return {ApiCallStatus::Ok,
                {{"idea_id", idea}, {"previous_slot", previous_slot}, {"slot", previous_slot},
                 {"learned", true}, {"changed", false}},
                {}};
    }
    invent->SetNetaFlag(static_cast<int>(idea));
    const int slot = invent->CheckNetaFlag(static_cast<int>(idea));
    if (slot < 0) return Failed("the native idea binder has no free slot");
    return {ApiCallStatus::Ok,
            {{"idea_id", idea}, {"previous_slot", -1}, {"slot", slot}, {"learned", true}, {"changed", true}},
            {}};
}

ApiCallResult ListScoops(const Json &args) {
    if (!args.is_array() || !args.empty()) return Invalid("photos.scoops_list takes no arguments");
    CInventUserData *invent = SignatureInventData();
    if (invent == nullptr) return Failed("the game has no active save inventory");
    Json scoops = Json::array();
    int known_count = 0;
    int obtained_count = 0;
    for (int index = 0; index < INVENT_SCOOP_TABLE_NUM; ++index) {
        SCOOP_DATA *row = GetScoopDataTableIndex(index);
        if (row == nullptr) continue;
        SCOOP_INFO *info = invent->scoop.GetScoopInfo(row->scoop_id);
        const bool known = info != nullptr && info->known != 0;
        const bool obtained = info != nullptr && info->obtained != 0;
        if (known) ++known_count;
        if (obtained) ++obtained_count;
        scoops.push_back({{"index", index},
                          {"scoop_id", row->scoop_id},
                          {"info_index", row->info_no},
                          {"known", known},
                          {"obtained", obtained},
                          {"has_photo", CheckInventPhoto(row->scoop_id, INVENT_PHOTO_CHECK_NETA) > 0},
                          {"learned", invent->CheckNetaFlag(row->scoop_id) >= 0}});
    }
    return {ApiCallStatus::Ok,
            {{"count", scoops.size()},
             {"known_count", known_count},
             {"obtained_count", obtained_count},
             {"scoops", std::move(scoops)}},
            {}};
}

struct ModInventionRecipe {
    std::string id;
    std::string owner;
    std::string target_id;
    int target_item_no = 0;
    std::array<int, 3> ideas{};
    std::vector<std::pair<int, int>> materials;
    std::vector<std::string> material_ids;
};

std::map<std::string, ModInventionRecipe, std::less<>> g_mod_inventions;

Json ModInventionJson(const ModInventionRecipe &recipe) {
    Json materials = Json::array();
    for (std::size_t index = 0; index < recipe.materials.size(); ++index) {
        materials.push_back({{"item_no", recipe.materials[index].first},
                             {"id", recipe.material_ids[index]},
                             {"count", recipe.materials[index].second}});
    }
    return {{"id", recipe.id},
            {"target", recipe.target_id},
            {"item_no", recipe.target_item_no},
            {"ideas", recipe.ideas},
            {"materials", std::move(materials)},
            {"source", "mod"}};
}

Json NativeInventionJson(CInventDataManage &manager, int index) {
    const INVENT_DATA_INFO &recipe = manager.table[index];
    Json materials = Json::array();
    if (recipe.materials.material != nullptr && recipe.materials.num >= 0 && recipe.materials.num <= 4) {
        for (int material = 0; material < recipe.materials.num; ++material) {
            const INVENT_MATERIAL &entry = recipe.materials.material[material];
            materials.push_back({{"item_no", entry.item_id}, {"id", SignatureItemStableId(entry.item_id)},
                                 {"count", entry.num}});
        }
    }
    return {{"id", "chronicle-two:invention:" + std::to_string(recipe.item_id)},
            {"target", SignatureItemStableId(recipe.item_id)},
            {"item_no", recipe.item_id},
            {"ideas", {recipe.neta_id[0], recipe.neta_id[1], recipe.neta_id[2]}},
            {"materials", std::move(materials)},
            {"source", "native"}};
}

ApiCallResult ListInventions(const Json &args) {
    if (!args.is_array() || !args.empty()) return Invalid("inventions.list takes no arguments");
    Json recipes = Json::array();
    CInventDataManage *manager = MoonModGetNativeInventDataManage();
    if (manager != nullptr && manager->table != nullptr && manager->num > 0) {
        const int count = std::min(static_cast<int>(manager->num), 4096);
        for (int index = 0; index < count; ++index) {
            if (manager->table[index].item_id <= 0) continue;
            recipes.push_back(NativeInventionJson(*manager, index));
        }
    }
    for (const auto &[id, recipe] : g_mod_inventions) {
        (void)id;
        recipes.push_back(ModInventionJson(recipe));
    }
    return {ApiCallStatus::Ok, {{"count", recipes.size()}, {"recipes", std::move(recipes)}}, {}};
}

ApiCallResult RegisterInvention(std::string_view mod_id, const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) {
        return Invalid("expected {id = local_id, item = stable_id, ideas = {a, b, c}, materials = optional}");
    }
    const Json &request = args[0];
    for (auto field = request.begin(); field != request.end(); ++field) {
        if (field.key() != "id" && field.key() != "item" && field.key() != "ideas" &&
            field.key() != "materials") {
            return Invalid("unsupported invention field '" + field.key() + "'");
        }
    }
    if (!request.contains("id") || !request["id"].is_string() || !request.contains("item") ||
        !request.contains("ideas")) {
        return Invalid("inventions.register requires id, item, and ideas");
    }
    std::string local_id;
    if (!QualifyOwnerScopedId(mod_id, request["id"].get_ref<const std::string &>(), local_id)) {
        return Invalid("invention id must be a local identifier owned by this mod");
    }
    if (g_mod_inventions.contains(local_id)) return Invalid("an invention with this stable id is already registered");
    if (g_mod_inventions.size() >= 32) return Failed("Chronicle Two supports up to 32 mod invention recipes per session");
    ModInventionRecipe recipe;
    recipe.id = local_id;
    recipe.owner = mod_id;
    std::string error;
    if (!SignatureResolveItemSelector(request["item"], recipe.target_item_no, recipe.target_id, error)) {
        return Invalid("invention item: " + error);
    }
    if (recipe.target_item_no <= 0) return Invalid("invention item must resolve to a native item number");
    if (GetCommonItemData(recipe.target_item_no) == nullptr && FindWeaponCommon(recipe.target_item_no) == nullptr) {
        return Invalid("invention item does not identify a loaded native item or weapon");
    }
    const Json &ideas = request["ideas"];
    if (!ideas.is_array() || ideas.size() != 3) return Invalid("ideas must be an array of exactly 3 idea ids");
    for (int index = 0; index < 3; ++index) {
        if (!ideas[index].is_number_integer()) return Invalid("ideas entries must be integers from 1 to 999");
        const auto idea = ideas[index].get<std::int64_t>();
        if (idea < 1 || idea > 999) return Invalid("ideas entries must be integers from 1 to 999");
        recipe.ideas[index] = static_cast<int>(idea);
    }
    if (recipe.ideas[0] == recipe.ideas[1] || recipe.ideas[0] == recipe.ideas[2] ||
        recipe.ideas[1] == recipe.ideas[2]) {
        return Invalid("ideas must contain three distinct idea ids");
    }
    if (request.contains("materials")) {
        const Json &materials = request["materials"];
        if (!materials.is_array() || materials.size() > 4) {
            return Invalid("materials must be an array of at most 4 entries");
        }
        for (const Json &material : materials) {
            if (!material.is_object() || material.size() != 2 || !material.contains("id") ||
                !material.contains("count")) {
                return Invalid("each material must be {id = stable_id, count = 1..99}");
            }
            int item_no = 0;
            std::string stable_id;
            if (!SignatureResolveItemSelector(material["id"], item_no, stable_id, error)) {
                return Invalid("material: " + error);
            }
            int count = 0;
            if (!ReadInt(material, "count", 1, 1, 99, count)) {
                return Invalid("material count must be an integer from 1 to 99");
            }
            if (GetCommonItemData(item_no) == nullptr) {
                return Invalid("invention material does not identify a loaded native item");
            }
            recipe.materials.emplace_back(item_no, count);
            recipe.material_ids.push_back(std::move(stable_id));
        }
    }
    const Json result = ModInventionJson(recipe);
    g_mod_inventions.emplace(local_id, std::move(recipe));
    return {ApiCallStatus::Ok, result, {}};
}

ApiCallResult CraftInvention(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || !args[0].contains("id")) {
        return Invalid("expected {id = invention_id, count = optional 1..99}");
    }
    int count = 1;
    if (!ReadInt(args[0], "count", 1, 1, 99, count)) return Invalid("count must be an integer from 1 to 99");
    CUserDataManager *user_data = SignatureUserData();
    if (user_data == nullptr) return Failed("the game has no active save inventory");
    CInventUserData *invent = &user_data->invent_data;
    std::string id;
    if (!ReadId(args[0]["id"], id)) return Invalid("id must be a stable invention id");

    const ModInventionRecipe *mod_recipe = nullptr;
    const auto found_mod = g_mod_inventions.find(id);
    if (found_mod != g_mod_inventions.end()) {
        mod_recipe = &found_mod->second;
    } else {
        for (const auto &[qualified, candidate] : g_mod_inventions) {
            const std::size_t separator = qualified.rfind(':');
            if (separator != std::string::npos && qualified.substr(separator + 1) == id) {
                mod_recipe = &candidate;
                break;
            }
        }
    }

    int target_item_no = 0;
    std::string target_id;
    std::string error;
    if (mod_recipe != nullptr) {
        target_item_no = mod_recipe->target_item_no;
        target_id = mod_recipe->target_id;
    } else if (id.starts_with("chronicle-two:invention:")) {
        const std::string numeric = id.substr(std::string("chronicle-two:invention:").size());
        int parsed = 0;
        const auto result = std::from_chars(numeric.data(), numeric.data() + numeric.size(), parsed);
        if (result.ec != std::errc() || result.ptr != numeric.data() + numeric.size()) {
            return Invalid("invention id does not contain a native item number");
        }
        target_item_no = parsed;
        target_id = SignatureItemStableId(parsed);
    } else if (!SignatureResolveItemSelector(args[0]["id"], target_item_no, target_id, error)) {
        return Invalid("invention id: " + error);
    }

    int native_item_no = target_item_no;
    std::array<int, 3> ideas{};
    std::vector<std::pair<int, int>> materials;
    bool native_recipe = false;
    CInventDataManage *manager = nullptr;
    if (mod_recipe != nullptr) {
        ideas = mod_recipe->ideas;
        materials = mod_recipe->materials;
        for (int index = 0; index < 3; ++index) {
            if (invent->CheckNetaFlag(ideas[index]) < 0) {
                return Failed("the invention requires three learnt ideas; idea " + std::to_string(ideas[index]) +
                              " is not in the binder");
            }
        }
    } else {
        manager = MoonModGetNativeInventDataManage();
        if (manager == nullptr || manager->table == nullptr) {
            return Failed("the native invention recipe table is unavailable");
        }
        INVENT_DATA_INFO *recipe = manager->GetInventDataInfoByItemID(native_item_no);
        if (recipe == nullptr) return Failed("no native invention recipe produces this item");
        if (invent->IsAlreadyCreatedItem(native_item_no) < 0) {
            return Failed("the native invention card has not been discovered yet");
        }
        ideas = {recipe->neta_id[0], recipe->neta_id[1], recipe->neta_id[2]};
        if (recipe->materials.material != nullptr && recipe->materials.num >= 0 && recipe->materials.num <= 4) {
            for (int index = 0; index < recipe->materials.num; ++index) {
                const INVENT_MATERIAL &material = recipe->materials.material[index];
                materials.emplace_back(material.item_id, static_cast<int>(material.num) * count);
            }
        }
        native_recipe = true;
    }

    if (user_data->SearchSpaceUsedData() < 0) {
        return Failed("the inventory has no free space for the crafted invention");
    }
    for (const auto &[item_no, amount] : materials) {
        if (user_data->GetNumSameItem(item_no) < amount) {
            return Failed("the inventory does not hold enough of the invention's materials");
        }
    }
    if (native_recipe) {
        if (manager->CheckMakeItem(native_item_no, count, nullptr) == 0) {
            return Failed("the native invention material check failed");
        }
        manager->DeleteUserUsedItem(native_item_no, count);
    } else {
        for (const auto &[item_no, amount] : materials) {
            user_data->DeleteItem(item_no, amount);
        }
    }
    const int added = user_data->GetItem(native_item_no, count);
    Json payload = {{"item_id", target_id},
                    {"native_item_no", native_item_no},
                    {"ideas", ideas},
                    {"requested", count},
                    {"added", added},
                    {"source", "craft"},
                    {"native_recipe", native_recipe},
                    {"frame", SharedRuntime().Frame()}};
    SharedRuntime().QueueEvent("game.invention.created", payload);
    if (added <= 0) return Failed("the native inventory rejected the crafted invention after consuming materials");
    return {ApiCallStatus::Ok, std::move(payload), {}};
}

Json PositionJson(const sceVu0FVECTOR &position) {
    return {{"x", position[0]}, {"y", position[1]}, {"z", position[2]}};
}

ApiCallResult GetSpheda(const Json &args) {
    if (!args.is_array() || !args.empty()) return Invalid("dungeons.spheda_get takes no arguments");
    CSaveData *save = GetSaveData();
    if (save == nullptr) return Failed("the game has no active save data");
    const int dungeon_id = DngStatus.dungeon_no;
    const int floor_id = dungeon_id >= 0 && dungeon_id < SAVE_DUNGEON_NUM
                             ? save->save_dungeon.floor_id[dungeon_id]
                             : -1;
    DNG_FLOOR_SAVE *floor = save->save_dungeon.GetFloorInfoPtr(dungeon_id, floor_id);
    DNGMAP_ROOM_INFO *room = BattleAreaScene == nullptr ? nullptr
                                                        : BattleAreaScene->floor_manager.GetActiveFloorInfo();
    CSphida *sphida = GetSphidaPtr();
    Json result = {{"dungeon_id", dungeon_id},
                   {"floor_id", floor_id},
                   {"challenge", room != nullptr && room->spheda != 0},
                   {"cleared", floor != nullptr && floor->spheda_clear > 0},
                   {"active", sphida != nullptr && sphida->play_flag != 0},
                   {"session", sphida != nullptr}};
    if (floor != nullptr && floor->spheda_clear > 0) result["clear_value"] = floor->spheda_clear;
    if (sphida != nullptr) {
        result["play_flag"] = sphida->play_flag;
        result["status_flag"] = sphida->status_flag;
        result["minimap_flag"] = sphida->minimap_flag;
        result["par"] = sphida->par_count;
        result["club_no"] = sphida->club_no;
        result["omake_mode"] = sphida->omake_mode;
        result["last_challenge"] = sphida->last_challenge != 0;
        result["pin_color"] = sphida->pin_col;
        result["ball_color"] = sphida->ball_col;
        result["pin"] = PositionJson(sphida->pin_pos);
        result["ball"] = PositionJson(sphida->ball_pos);
        result["gauge"] = {{"power", sphida->pow_gage.power},
                           {"state", sphida->pow_gage.state},
                           {"code", sphida->pow_gage.code},
                           {"safe_level", sphida->pow_gage.safe_level}};
    }
    return {ApiCallStatus::Ok, std::move(result), {}};
}

void (*g_spheda_setup_routine)(CSphida *, int) = nullptr;

ApiCallResult SpawnSpheda(const Json &args) {
    if (!args.is_array() || args.size() > 1 || (args.size() == 1 && !args[0].is_object())) {
        return Invalid("expected no arguments or {par = 1..99}");
    }
    int par = 0;
    if (!args.empty() && args[0].contains("par")) {
        if (!ReadInt(args[0], "par", 0, 1, 99, par)) return Invalid("par must be an integer from 1 to 99");
    }
    CSphida *sphida = GetSphidaPtr();
    if (sphida == nullptr) {
        return Failed("no Spheda session is allocated on this floor; start the floor's Spheda event first");
    }
    if (GetNowLoopNo() != LOOP_DUNGEON) return Failed("Spheda setup requires an active dungeon scene");
    if (g_spheda_setup_routine == nullptr && DngMainScene == nullptr) {
        return Failed("Spheda setup requires the live dungeon scene");
    }
    if (g_spheda_setup_routine != nullptr) {
        g_spheda_setup_routine(sphida, par);
    } else {
        sphida->SetUp(0);
    }
    if (par > 0) sphida->par_count = par;
    sphida->play_flag = 1;
    sphida->status_flag = 1;
    sphida->minimap_flag = 1;
    return GetSpheda(Json::array());
}

ApiCallResult Call(std::string_view mod_id, const ApiOperationSpec &operation, const Json &args) {
    if (operation.path == "content.list") return ListContent(args);
    if (operation.path == "content.register") return RegisterContent(mod_id, args);
    if (operation.path == "content.unregister") return UnregisterContent(mod_id, args);
    if (operation.path == "debug.inspect") return InspectNativeRuntime(args);
    if (operation.path == "debug.overlay_register") return RegisterDebugOverlay(mod_id, args);
    if (operation.path == "photos.ideas_list") return ListPhotoIdeas(args);
    if (operation.path == "photos.idea_unlock") return UnlockPhotoIdea(args);
    if (operation.path == "photos.scoops_list") return ListScoops(args);
    if (operation.path == "inventions.list") return ListInventions(args);
    if (operation.path == "inventions.register") return RegisterInvention(mod_id, args);
    if (operation.path == "inventions.craft") return CraftInvention(args);
    if (operation.path == "render.layer_register") return RegisterRenderLayer(mod_id, args);
    if (operation.path == "render.layer_remove") return RemoveRenderLayer(mod_id, args);
    if (operation.path == "render.bar_register") return RegisterRenderBar(mod_id, args);
    if (operation.path == "render.bar_remove") return RemoveRenderBar(mod_id, args);
    if (operation.path == "combat.get") return GetCombat(args);
    if (operation.path == "combat.register_formula") return g_combat_damage_formulas.Register(mod_id, args);
    if (operation.path == "combat.damage") return DamageCombatEnemy(args);
    if (operation.path == "combat.heal") return HealCombatEnemy(args);
    if (operation.path == "dungeons.list") return ListDungeons(args);
    if (operation.path == "dungeons.get") return GetDungeon(mod_id, args);
    if (operation.path == "dungeons.set_layout") return SetDungeonLayout(args);
    if (operation.path == "dungeons.floor_rules_get") return GetDungeonFloorRules(args);
    if (operation.path == "dungeons.floor_rules_set") return SetDungeonFloorRules(args);
    if (operation.path == "dungeons.loot_get") return GetDungeonLoot(args);
    if (operation.path == "dungeons.loot_override") return OverrideDungeonLoot(args);
    if (operation.path == "dungeons.spheda_get") return GetSpheda(args);
    if (operation.path == "dungeons.spheda_spawn") return SpawnSpheda(args);
    if (operation.path == "shops.list") return ListShops(args);
    if (operation.path == "shops.get") return GetShop(args);
    if (operation.path == "shops.register_item") return RegisterShopItem(args);
    if (operation.path == "shops.remove_item") return RemoveShopItem(args);
    if (operation.path == "shops.set_price") return SetShopPrice(args);
    if (operation.path == "weapons.abs_get") return GetWeaponAbs(args);
    if (operation.path == "weapons.abs_add") return AddWeaponAbs(args);
    if (operation.path == "weapons.attachments_get") return GetWeaponAttachments(args);
    if (operation.path == "weapons.attachment_insert") return InsertWeaponAttachment(args);
    if (operation.path == "weapons.build_up_list") return ListWeaponBuildUps(args);
    if (operation.path == "weapons.build_up_register") return RegisterWeaponBuildUp(args);
    if (operation.path == "weapons.spectrumize") return SpectrumizeWeapon(args);
    if (operation.path == "entities.query") return QueryEntities(args);
    if (operation.path == "entities.get") return GetEntity(args);
    if (operation.path == "entities.set_active_limit") return SetActiveMonsterLimit(args);
    if (operation.path == "physics.query") return QueryPhysics(args);
    if (operation.path == "physics.velocity_get") return GetEntityVelocity(args);
    if (operation.path == "physics.velocity_set") return SetEntityVelocity(args);
    if (operation.path == "entities.spawn") return SpawnCharacter(mod_id, args);
    if (operation.path == "entities.despawn") return DespawnEntity(mod_id, args);
    if (operation.path == "input.bind") return BindInputAction(mod_id, args);
    if (operation.path == "input.unbind") return UnbindInputAction(mod_id, args);
    if (operation.path == "input.state") return ReadInputAction(mod_id, args, false);
    if (operation.path == "input.consume") return ReadInputAction(mod_id, args, true);
    if (operation.path == "input.axis") return ReadInputAxis(args);
    if (operation.path == "input.context_push") return PushInputContext(mod_id, args);
    if (operation.path == "input.context_pop") return PopInputContext(mod_id, args);
    if (operation.path == "input.context_get") return GetInputContext(args);
    if (operation.path == "render.camera_get") return ReadDungeonCamera(args);
    if (operation.path == "render.camera_set") return SetDungeonCamera(args);
    if (operation.path == "items.register") {
        return RegisterItem(mod_id, args);
    }
    if (operation.path == "weapons.register") {
        return RegisterWeapon(mod_id, args);
    }
    if (operation.path == "models.register") return RegisterModel(mod_id, args);
    if (operation.path == "models.unregister") return UnregisterModel(mod_id, args);
    if (operation.path == "models.replace") return ReplaceNativeModel(mod_id, args);
    if (operation.path == "models.remove") return RemoveNativeModelReplacement(mod_id, args);
    if (operation.path == "animations.register") return RegisterAnimation(mod_id, args);
    if (operation.path == "animations.play") return PlayAnimation(mod_id, args);
    if (operation.path == "animations.stop") return StopAnimation(mod_id, args);
    if (operation.path == "characters.register") return RegisterCharacter(mod_id, args);
    if (operation.path == "models.resolve") return ResolveModel(args);
    if (operation.path == "characters.spawn") return SpawnCharacter(mod_id, args);
    if (operation.path == "world.register_dungeon") {
        return RegisterDungeon(mod_id, args);
    }
    if (operation.path == "dungeons.register") return RegisterDungeon(mod_id, args);
    if (!args.is_array()) {
        return Invalid("expected an argument list");
    }
    if (operation.path == "player.get") {
        return GetPlayer(args);
    }
    if (operation.path == "player.heal") {
        return HealPlayer(args);
    }
    if (operation.path == "player.damage") {
        return DamagePlayer(args);
    }
    if (operation.path == "player.set") return SetPlayer(args);
    if (operation.path == "player.ridepod_get") return GetRidepod(args);
    if (operation.path == "player.ridepod_set") return SetRidepod(args);
    if (operation.path == "player.transform_get") return GetTransform(args);
    if (operation.path == "player.transform_set") return SetTransform(args);
    if (operation.path == "player.status_add") return AddPlayerStatus(args);
    if (operation.path == "player.status_remove") return RemovePlayerStatus(args);
    if (operation.path == "weapons.list") {
        return ListWeapons(args);
    }
    if (operation.path == "weapons.get") {
        return GetWeapon(args);
    }
    if (operation.path == "weapons.give") {
        if (args.size() != 1 || !args[0].is_object() || args[0].size() != 1 ||
            !args[0].contains("id") || !args[0]["id"].is_string()) {
            return Invalid("expected {id = stable_weapon_id}");
        }
        const std::string id = args[0]["id"].get<std::string>();
        int item_no = 0;
        std::string error;
        const ApiCallStatus resolve_status = ResolveItemId(id, item_no, error);
        if (resolve_status != ApiCallStatus::Ok) return {resolve_status, nullptr, std::move(error)};
        if (!NativeWeaponTablesValid() || FindWeaponCommon(item_no) == nullptr) {
            return Failed("the stable id does not identify a registered weapon");
        }
        CUserDataManager *user_data = GetUserDataMan();
        if (user_data == nullptr) return Failed("the game has no active save inventory");
        const int added = user_data->GetItem(item_no, 1);
        return {ApiCallStatus::Ok, {{"id", id}, {"native_item_no", item_no}, {"requested", 1}, {"added", added}}, {}};
    }
    if (operation.path == "weapons.equip") {
        return EquipWeapon(args);
    }
    if (operation.path == "weapons.upgrade") return UpgradeWeapon(args);
    if (operation.path == "party.list") {
        return ListParty(args);
    }
    if (operation.path == "party.active") {
        return GetActivePartyMember(args);
    }
    if (operation.path == "party.set_active") return SetActivePartyMember(args);
    if (operation.path == "party.recruit") return RecruitPartyNpc(args);
    if (operation.path == "characters.list") return ListCharacters(args);
    if (operation.path == "characters.get") return GetCharacter(args);
    if (operation.path == "economy.get") return GetEconomy(args);
    if (operation.path == "economy.add") return AddEconomy(args);
    if (operation.path == "economy.spend") return SpendEconomy(args);
    if (operation.path == "locations.list") return ListLocations(args);
    if (operation.path == "locations.get") return GetLocation(args);
    if (operation.path == "locations.register") return RegisterLocation(mod_id, args);
    if (operation.path == "world.transition") return TransitionWorld(args);
    if (operation.path == "world.get") {
        return GetWorld(args);
    }
    if (operation.path == "georama.get") return GetGeorama(args);
    if (operation.path == "georama.register") return RegisterGeoramaPiece(mod_id, args);
    if (operation.path == "georama.place") return PlaceGeoramaPiece(args);
    if (operation.path == "georama.remove") return RemoveGeoramaPiece(args);
    if (operation.path == "georama.river_set") return SetGeoramaRiverCell(args);
    if (operation.path == "georama.request_set") return SetGeoramaRequest(args);
    if (operation.path == "georama.request_known_set") return SetGeoramaRequestKnown(args);
    if (operation.path == "georama.condition_set") return SetGeoramaCondition(args);
    if (operation.path == "world.set_time") return SetWorldTime(args);
    if (operation.path == "world.story_flag_get") return GetStoryFlag(args);
    if (operation.path == "world.story_flag_set") return SetStoryFlag(args);
    if (operation.path == "world.story_value_get") return GetStoryValue(args);
    if (operation.path == "world.story_value_set") return SetStoryValue(args);
    if (operation.path == "world.map_flag_get") return GetMapFlag(args);
    if (operation.path == "world.map_flag_set") return SetMapFlag(args);
    if (operation.path == "items.list") {
        if (!NativeItemTablesValid()) return Failed("the native item tables are unavailable or out of range");
        Json result = Json::array();
        const CGameData &data = GameItemDataManage;
        for (int index = 0; index < data.common_num; ++index) {
            const CDataCommon &common = data.common_data[index];
            if (IsWeaponCommon(common)) continue;
            result.push_back(LiveItemJson(common, true));
        }
        for (const auto &[id, item] : g_items) {
            if (item.native_item_no < 0) result.push_back(ItemJson(item));
        }
        return {ApiCallStatus::Ok, std::move(result), {}};
    }
    if (operation.path == "items.get") {
        if (args.size() != 1) {
            return Invalid("expected one stable item id");
        }
        std::string id;
        if (!ReadId(args[0], id)) {
            return Invalid("expected a stable item id string or {id = ...}");
        }
        const auto found = g_items.find(id);
        if (found != g_items.end()) {
            return {ApiCallStatus::Ok, ItemJson(found->second), {}};
        }
        int item_no = 0;
        if (!ReadNativeItemId(Json(id), item_no)) {
            return {ApiCallStatus::Failed, nullptr, "no built-in or mod item is registered with that stable id"};
        }
        if (!NativeItemTablesValid()) return Failed("the native item tables are unavailable or out of range");
        const CDataCommon *common = GameItemDataManage.GetCommonData(item_no);
        if (common == nullptr || IsWeaponCommon(*common)) {
            return {ApiCallStatus::Failed, nullptr, "no non-weapon item has that stable id"};
        }
        return {ApiCallStatus::Ok, LiveItemJson(*common, true), {}};
    }
    if (operation.path == "items.give") {
        if (args.size() != 1 || !args[0].is_object()) {
            return Invalid("expected {id = stable_id, count = optional_integer}");
        }
        std::string id;
        int count = 1;
        if (!ReadId(args[0], id) || !ReadInt(args[0], "count", 1, 1, 999, count)) {
            return Invalid("expected a stable item id and count from 1 to 999");
        }
        int item_no = 0;
        std::string error;
        const ApiCallStatus resolve_status = ResolveItemId(id, item_no, error);
        if (resolve_status != ApiCallStatus::Ok) return {resolve_status, nullptr, std::move(error)};
        CUserDataManager *user_data = GetUserDataMan();
        if (user_data == NULL) {
            return Failed("the game has no active save inventory");
        }
        const int added = user_data->GetItem(item_no, count);
        return {ApiCallStatus::Ok, {{"id", id}, {"requested", count}, {"added", added}}, {}};
    }
    if (operation.path == "items.remove") {
        if (args.size() != 1 || !args[0].is_object()) {
            return Invalid("expected {id = stable_id, count = optional_integer}");
        }
        std::string id;
        int count = 1;
        if (!ReadId(args[0], id) || !ReadInt(args[0], "count", 1, 1, 999, count)) {
            return Invalid("expected a stable item id and count from 1 to 999");
        }
        int item_no = 0;
        std::string error;
        const ApiCallStatus resolve_status = ResolveItemId(id, item_no, error);
        if (resolve_status != ApiCallStatus::Ok) return {resolve_status, nullptr, std::move(error)};
        CUserDataManager *user_data = GetUserDataMan();
        if (user_data == nullptr) {
            return Failed("the game has no active save inventory");
        }
        const int before = user_data->GetNumSameItem(item_no);
        user_data->DeleteItem(item_no, count);
        const int remaining = user_data->GetNumSameItem(item_no);
        return {ApiCallStatus::Ok,
                {{"id", id}, {"requested", count}, {"removed", std::max(0, before - remaining)}, {"remaining", remaining}},
                {}};
    }
    if (operation.path == "items.use") {
        return UseNativeItem(args);
    }
    return {ApiCallStatus::Unsupported, nullptr, "this Chronicle Two adapter does not implement the operation"};
}

} // namespace

void ChronicleTwoPhotoTaken(int neta_id, int map_no, int npc_no, int monster_no, bool used) {
    Json payload = {{"neta_id", neta_id},
                    {"subject_id", neta_id},
                    {"map_no", map_no},
                    {"npc_no", npc_no},
                    {"monster_no", monster_no},
                    {"used", used},
                    {"kind", neta_id >= INVENT_NETA_ID_SCOOP && neta_id < INVENT_NETA_ID_SCOOP_END
                                 ? "scoop"
                                 : neta_id > 0 ? "idea" : "none"},
                    {"ideas", neta_id > 0 && neta_id < INVENT_NETA_ID_SCOOP ? Json::array({neta_id}) : Json::array()},
                    {"frame", SharedRuntime().Frame()}};
    if (CInventUserData *invent = SignatureInventData(); invent != nullptr) {
        int photo_count = 0;
        for (int slot = 0; slot < INVENT_PHOTO_NUM; ++slot) {
            if (invent->photo[slot].used != 0) ++photo_count;
        }
        payload["photo_count"] = photo_count;
        if (payload["kind"] == "scoop") {
            SCOOP_INFO *info = invent->scoop.GetScoopInfo(neta_id);
            payload["scoop"] = {{"scoop_id", neta_id},
                                {"known", info != nullptr && info->known != 0},
                                {"obtained", info != nullptr && info->obtained != 0}};
        }
    }
    SharedRuntime().QueueEvent("game.photo.taken", payload);
}

void ChronicleTwoInventionCreated(int item_id, int neta0, int neta1, int neta2) {
    const std::string stable_id = SignatureItemStableId(item_id);
    SharedRuntime().QueueEvent("game.invention.created",
                               {{"item_id", stable_id},
                                {"native_item_no", item_id},
                                {"ideas", {neta0, neta1, neta2}},
                                {"source", "native"},
                                {"native_recipe", true},
                                {"frame", SharedRuntime().Frame()}});
}

void ChronicleTwoSphedaCompleted(int dungeon_id, int floor_id, int clear_value) {
    SharedRuntime().QueueEvent("game.spheda.completed",
                               {{"dungeon_id", dungeon_id},
                                {"floor_id", floor_id},
                                {"cleared", true},
                                {"clear_value", clear_value},
                                {"frame", SharedRuntime().Frame()},
                                {"detail", "the native save records the Spheda clear flag; stroke count and medal award are not stored"}});
}

void ResetChronicleTwoSignatureForTests() {
    g_mod_inventions.clear();
    g_spheda_setup_routine = nullptr;
}

void SetChronicleTwoSphedaSetupRoutineForTests(void (*routine)(::CSphida *, int)) {
    g_spheda_setup_routine = routine;
}

std::string ChronicleTwoDebugOverlayText() {
    std::string text;
    for (const auto &[id, overlay] : g_debug_overlays) {
        (void)id;
        const std::size_t separator_size = text.empty() ? 0 : 3;
        if (text.size() + separator_size + overlay.text.size() > kMaximumDebugOverlayLine) break;
        if (separator_size != 0) text += " / ";
        text += overlay.text;
    }
    return text;
}

void ChronicleTwoNativeItemPickedUp(int item_no, int count) {

    if (count <= 0 || !NativeItemTablesValid()) return;
    CDataCommon *common = GameItemDataManage.GetCommonData(item_no);
    if (common == nullptr) return;

    const bool is_weapon = IsWeaponCommon(*common);
    std::string stable_id = is_weapon ? NativeWeaponId(item_no) : NativeItemId(item_no);
    if (is_weapon) {
        for (const auto &[id, weapon] : g_weapons) {
            if (weapon.native_item_no == item_no) {
                stable_id = id;
                break;
            }
        }
    } else {
        for (const auto &[id, item] : g_items) {
            if (item.native_item_no == item_no) {
                stable_id = id;
                break;
            }
        }
    }

    SharedRuntime().QueueEvent("game.item_picked_up",
                               {{"id", stable_id}, {"kind", is_weapon ? "weapon" : "item"},
                                {"native_item_no", item_no}, {"count", count}, {"source", "native"},
                                {"frame", SharedRuntime().Frame()}});
}

void ChronicleTwoTrackModSpawnedEnemy(std::string_view owner, std::string_view character_id, int slot,
                                      unsigned int generation, int native_monster_id) {
    if (owner.empty() || character_id.empty() || slot < 0 || slot >= MONSTER_ACTIVE_MAX || generation == 0 ||
        native_monster_id < 0) {
        return;
    }
    ModCharacterSpawn record;
    record.slot = slot;
    record.generation = generation;
    record.native_monster_id = native_monster_id;
    record.character_id = std::string(character_id);
    record.owner = std::string(owner);
    record.handle = "chronicle-two:monster:" + std::to_string(slot) + ":" + std::to_string(generation);
    g_character_spawns[record.handle] = std::move(record);
}

void ChronicleTwoEnemyDefeated(::CActiveMonster *monster, const char *source) {
    if (monster == nullptr || ActiveMonster == nullptr) return;
    int slot = -1;
    for (int index = 0; index < MONSTER_ACTIVE_MAX; ++index) {
        if (ActiveMonster->GetActiveMonster(index) == monster) {
            slot = index;
            break;
        }
    }
    if (slot < 0) return;
    unsigned int generation = GetMoonModMonsterSlotGeneration(slot);
    if (generation == 0) generation = EnsureMoonModMonsterSlotGeneration(slot);
    if (g_enemy_defeat_event_generations[slot] == generation) return;
    g_enemy_defeat_event_generations[slot] = generation;
    const std::string handle = "chronicle-two:monster:" + std::to_string(slot) + ":" + std::to_string(generation);
    const char *event_source = source != nullptr ? source : (monster->last_hit_chara < 0 ? "scripted" : "combat");
    Json payload = {{"entity", handle}, {"slot", slot}, {"native_monster_id", monster->monster_id},
                    {"source", event_source}};
    const auto spawned = g_character_spawns.find(handle);
    if (spawned != g_character_spawns.end() && spawned->second.generation == generation) {
        payload["character_id"] = spawned->second.character_id;
    }
    SharedRuntime().QueueEvent("game.enemy_defeated", payload);
}

float ApplyChronicleTwoCombatDamageFormula(float damage) {
    const float modified = g_combat_damage_formulas.Apply(damage);
    return modified <= 0.0f ? modified : ApplyCombatDamageBonuses(modified, SharedRuntime().CombatDamageBonuses());
}

void ResetChronicleTwoCombatFormulasForTests() {
    g_combat_damage_formulas.Clear();
}

void ConfigureChronicleTwoApi(RuntimeConfig &config) {
    config.current_language = CurrentLanguageTag;
    config.world_location_reader = [] { return GetWorld(Json::array()); };
    for (const char *path : {"player.get", "player.set", "player.heal", "player.damage", "player.status_add", "player.status_remove",
                             "player.ridepod_get", "player.ridepod_set", "player.transform_get", "player.transform_set",
                             "photos.ideas_list", "photos.idea_unlock", "photos.scoops_list",
                             "inventions.list", "inventions.register", "inventions.craft",
                             "dungeons.spheda_get", "dungeons.spheda_spawn",
                             "party.list", "party.active", "party.set_active", "party.recruit",
                             "characters.list", "characters.get", "world.get", "world.transition", "world.set_time",
                             "locations.list", "locations.get", "locations.register",
                             "georama.get", "georama.register", "georama.place", "georama.remove", "georama.river_set", "georama.request_set", "georama.condition_set", "georama.request_known_set",
                             "world.story_flag_get", "world.story_flag_set", "world.map_flag_get", "world.map_flag_set",
                             "world.story_value_get", "world.story_value_set",
                             "combat.get", "combat.damage", "combat.heal", "combat.register_formula", "entities.query",
                             "economy.get", "economy.add", "economy.spend",
                             "items.list", "items.get",
                             "items.register", "items.give", "items.remove", "items.use", "weapons.register", "weapons.give",
                             "world.register_dungeon", "dungeons.list", "dungeons.get", "dungeons.register", "models.resolve",
                              "debug.inspect", "debug.overlay_register", "content.list", "content.register"}) {
        config.api_capabilities.push_back({path, true, {}});
    }
    config.api_capabilities.push_back({"characters.register", true, {}});
    config.api_capabilities.push_back({"render.camera_get", true, {}});
    config.api_capabilities.push_back({"render.camera_set", true, {}});
    config.api_capabilities.push_back({"render.layer_register", true, {}});
    config.api_capabilities.push_back({"render.layer_remove", true, {}});
    config.api_capabilities.push_back({"physics.velocity_get", true, {}});
    config.api_capabilities.push_back({"physics.velocity_set", true, {}});
    config.api_capabilities.push_back({"physics.query", true, {}});
    config.api_capabilities.push_back({"characters.spawn", true, {}});
    config.api_capabilities.push_back({"entities.get", true, {}});
    config.api_capabilities.push_back({"entities.spawn", true, {}});
    config.api_capabilities.push_back({"entities.despawn", true, {}});
    config.api_capabilities.push_back({"dungeons.set_layout", true, {}});
    config.api_capabilities.push_back({"dungeons.set_exit_room", false,
                                       "Chronicle Two has no walk-through exit room yet."});
    config.api_capabilities.push_back({"entities.set_active_limit", true, {}});
    for (const char *path : {"input.bind", "input.unbind", "input.state", "input.consume", "input.axis",
                             "input.context_push", "input.context_pop", "input.context_get"}) {
        config.api_capabilities.push_back({path, true, {}});
    }
    config.api_capabilities.push_back({"models.register", true, {}});
    config.api_capabilities.push_back({"models.unregister", true, {}});
    config.api_capabilities.push_back({"models.replace", true, {}});
    config.api_capabilities.push_back({"models.remove", true, {}});
    for (const char *path : {"animations.register", "animations.play", "animations.stop"}) {
        config.api_capabilities.push_back({path, true, {}});
    }
    for (const char *path : {"weapons.list", "weapons.get", "weapons.equip", "weapons.upgrade"}) {
        config.api_capabilities.push_back({path, true, {}});
    }
    config.api_capabilities.push_back({"content.unregister", true, {}});
    for (const char *path : {"render.bar_register", "render.bar_remove", "weapons.abs_get", "weapons.abs_add",
                             "weapons.attachments_get", "weapons.attachment_insert", "weapons.build_up_list",
                             "weapons.build_up_register", "weapons.spectrumize", "shops.list", "shops.get",
                             "shops.register_item", "shops.remove_item", "shops.set_price",
                             "dungeons.floor_rules_get", "dungeons.floor_rules_set", "dungeons.loot_get",
                             "dungeons.loot_override"}) {
        config.api_capabilities.push_back({path, true, {}});
    }
    config.api_handler = Call;
    config.api_resource_override_removal_guard = ChronicleTwoModelOverrideRemovalGuard;
    config.api_owner_unloaded = [](std::string_view mod_id) {
        const std::string prefix = std::string(mod_id) + ":";
        InputUnbindModActionsByPrefix(prefix);
        g_input_contexts.UnloadOwner(mod_id);
        UnloadChronicleTwoAnimations(mod_id);
        g_combat_damage_formulas.RemoveOwner(mod_id);
        RestoreChronicleTwoModelReplacements(mod_id);
        DespawnOwnedChronicleTwoActors(mod_id);
        std::erase_if(g_debug_overlays, [mod_id](const auto &entry) { return entry.second.owner == mod_id; });
    };
    ConfigureAudioApi(config);
    CompleteApiCapabilityLedger(config.game_id, config.api_capabilities);
    config.require_complete_api_capabilities = true;
}

void ApplyChronicleTwoGeorama(::CEditMap *map, ::mgCMemory *memory) {
    if (map == nullptr || memory == nullptr || map->area_no < 0 || map->area_no >= 5) return;
    CEditInfoMngr &manager = map->info_mngr;
    if (manager.parts_info_num < 0 || manager.parts_info_num > 0x180 ||
        (manager.parts_info_num > 0 && manager.parts_info == nullptr)) {
        return;
    }
    const std::size_t town_id = static_cast<std::size_t>(map->area_no);
    Json native_templates = Json::array();
    for (int index = 0; index < manager.parts_info_num; ++index) {
        const CEditPartsInfo &info = manager.parts_info[index];
        if (info.id <= 0 || info.id > 255 || info.edit_name == nullptr || info.parts_name == nullptr || info.parts == nullptr) {
            continue;
        }
        const bool is_mod_piece = std::ranges::any_of(g_georama_pieces, [&info, town_id](const auto &entry) {
            return entry.second.native_part_ids[town_id] == info.id && entry.second.name == info.edit_name;
        });
        if (!is_mod_piece) native_templates.push_back({{"template_part_id", info.id}, {"name", info.edit_name}});
    }
    g_georama_native_templates[town_id] = std::move(native_templates);
    struct PendingPiece {
        ModGeoramaPiece *piece;
        const CEditPartsInfo *source;
        int native_id;
    };
    std::vector<PendingPiece> pending;
    for (auto &[id, piece] : g_georama_pieces) {
        (void)id;
        int &native_id = piece.native_part_ids[town_id];
        if (native_id >= 0) {
            CEditPartsInfo *already_applied = manager.GetePartsInfoAtID(native_id);
            if (already_applied != nullptr && already_applied->edit_name != nullptr &&
                piece.name == already_applied->edit_name) {
                continue;
            }
            native_id = -1;
        }
        piece.apply_errors[town_id].clear();
        CEditPartsInfo *source = manager.GetePartsInfoAtID(piece.template_part_id);
        if (source == nullptr || source->parts == nullptr || source->parts_name == nullptr || source->edit_name == nullptr) {
            piece.apply_errors[town_id] = "the selected native template piece is unavailable in this Georama map";
            continue;
        }
        if (manager.GetePartsInfo(const_cast<char *>(piece.name.c_str())) != nullptr) {
            piece.apply_errors[town_id] = "the piece name conflicts with a native Georama definition";
            continue;
        }
        int selected_native_id = -1;
        for (int candidate = 1; candidate < 0x80; ++candidate) {
            if (manager.GetePartsInfoAtID(candidate) == nullptr) {
                selected_native_id = candidate;
                break;
            }
        }
        if (selected_native_id < 0) {
            piece.apply_errors[town_id] = "the native Georama menu has no free piece id below 128";
            continue;
        }
        pending.push_back({&piece, source, selected_native_id});
    }
    if (pending.empty()) return;

    const int original_count = manager.parts_info_num;
    const int new_count = original_count + static_cast<int>(pending.size());
    if (original_count < 0 || new_count > 0x180 || (original_count > 0 && manager.parts_info == nullptr)) {
        for (PendingPiece &entry : pending) {
            entry.piece->apply_errors[town_id] = "the native Georama piece table has no safe extension capacity";
        }
        return;
    }
    const std::size_t bytes = static_cast<std::size_t>(new_count) * sizeof(CEditPartsInfo);
    const int allocation_quads = static_cast<int>((bytes + 15u) / 16u + 2u);
    u_long128 *table_memory = memory->Alloc(allocation_quads);
    if (table_memory == nullptr) {
        for (PendingPiece &entry : pending) {
            entry.piece->apply_errors[town_id] = "the native Georama scene ran out of memory while adding piece definitions";
        }
        return;
    }
    CEditPartsInfo *table = new (table_memory) CEditPartsInfo[new_count];
    if (original_count > 0) std::memcpy(table, manager.parts_info, static_cast<std::size_t>(original_count) * sizeof(CEditPartsInfo));
    std::vector<char *> names;
    names.reserve(pending.size());
    for (const PendingPiece &entry : pending) {
        names.push_back(mgCopyString(const_cast<char *>(entry.piece->name.c_str()), memory));
        if (names.back() == nullptr) {
            for (PendingPiece &failed : pending) {
                failed.piece->apply_errors[town_id] = "the native Georama scene ran out of memory while adding piece names";
            }
            return;
        }
    }
    for (std::size_t index = 0; index < pending.size(); ++index) {
        PendingPiece &entry = pending[index];
        CEditPartsInfo &copy = table[static_cast<std::size_t>(original_count) + index];
        std::memcpy(&copy, entry.source, sizeof(CEditPartsInfo));
        copy.id = entry.native_id;
        copy.edit_name = names[index];
        entry.piece->native_part_ids[town_id] = entry.native_id;
        entry.piece->apply_errors[town_id].clear();
    }
    manager.SetePartsInfoTable(table, new_count);
}

int ChronicleTwoGeoramaNativePartId(std::string_view piece_id, int town_id) {
    if (town_id < 0 || town_id >= SAVE_EDIT_DATA_MAX) return -1;
    const auto found = g_georama_pieces.find(std::string(piece_id));
    if (found == g_georama_pieces.end()) return -1;
    return found->second.native_part_ids[static_cast<std::size_t>(town_id)];
}

void ApplyChronicleTwoContent() {
    ApplyChronicleTwoLocations();
    for (auto &[id, item] : g_items) {
        if (item.native_item_no >= 0 && GameItemDataManage.GetCommonData(item.native_item_no) != NULL) {
            continue;
        }
        int item_no = -1;
        char error[160] = {};
        if (!GameItemDataManage.RegisterMoonModItem(item.template_item, item.name.c_str(), item.max_num,
                                                    item.stack_num, item.effect_flags, item.effect_amount,
                                                    &item_no, error, sizeof(error))) {
            std::fprintf(stderr, "moonmod: could not register item %s: %s\n", id.c_str(), error);
            continue;
        }
        item.native_item_no = item_no;
        std::fprintf(stderr, "moonmod: registered item %s as game item %d (vanilla assets from %d)\n", id.c_str(),
                     item_no, item.template_item);
    }
    for (auto &[id, weapon] : g_weapons) {
        if (weapon.native_item_no >= 0 && GameItemDataManage.GetWeaponData(weapon.native_item_no) != NULL) {
            continue;
        }
        int item_no = -1;
        char error[160] = {};
        const int *attributes = weapon.has_attributes ? weapon.attributes.data() : NULL;
        const int *attribute_caps = weapon.has_attributes ? weapon.attribute_caps.data() : NULL;
        if (!GameItemDataManage.RegisterMoonModWeapon(weapon.template_item, weapon.name.c_str(), weapon.max_num,
                                                       weapon.stack_num, weapon.attack, weapon.durability_stat,
                                                       weapon.attack_cap, weapon.durability_cap,
                                                       weapon.durability_capacity, weapon.levelup_exp, attributes,
                                                       attribute_caps, &item_no, error, sizeof(error))) {
            std::fprintf(stderr, "moonmod: could not register weapon %s: %s\n", id.c_str(), error);
            continue;
        }
        weapon.native_item_no = item_no;
        std::fprintf(stderr, "moonmod: registered weapon %s as game item %d (vanilla assets from %d)\n", id.c_str(),
                     item_no, weapon.template_item);
    }
}

void ApplyChronicleTwoDungeons(CDngFloorManager *manager) {
    if (manager == nullptr) {
        return;
    }
    for (const auto &[key, rules] : g_floor_rule_overrides) {
        if (key.first != manager->dng_no) continue;
        if (DNGMAP_ROOM_INFO *room = manager->GetDngMapFloorInfo(key.second); room != nullptr) {
            room->seal = static_cast<s8>(rules.seal);
            room->spheda = rules.spheda ? 1 : 0;
            if (rules.fast_destroy_time > 0) room->fast_destroy_time = rules.fast_destroy_time;
        }
    }
    for (auto &[id, dungeon] : g_dungeons) {
        if (dungeon.dungeon != manager->dng_no) {
            continue;
        }
        for (ModDungeonFloor &floor : dungeon.floors) {
            if (floor.native_floor_id >= 0 && manager->IsMoonModFloor(floor.native_floor_id, floor.name.c_str())) {
                DNGMAP_ROOM_INFO *room = manager->GetDngMapFloorInfo(floor.native_floor_id);
                if (room != nullptr) {
                    room->flag |= floor.room_flags;
                    if (floor.seal >= 0) room->seal = static_cast<s8>(floor.seal);
                }
                continue;
            }
            floor.native_floor_id = -1;
            int source_floor = floor.source_floor;
            if (!floor.source_floor_ref.empty()) {
                const auto source = std::find_if(dungeon.floors.begin(), dungeon.floors.end(), [&floor](const ModDungeonFloor &candidate) {
                    return candidate.local_id == floor.source_floor_ref;
                });
                if (source == dungeon.floors.end() || source->native_floor_id < 0) {
                    continue;
                }
                source_floor = source->native_floor_id;
            }
            int native_floor_id = -1;
            char error[160] = {};
            if (!manager->RegisterMoonModFloor(source_floor, floor.name.c_str(), floor.id.c_str(), &native_floor_id, error,
                                                sizeof(error))) {
                std::fprintf(stderr, "moonmod: could not register floor %s: %s\n", floor.id.c_str(), error);
                continue;
            }
            floor.native_floor_id = native_floor_id;
            DNGMAP_ROOM_INFO *room = manager->GetDngMapFloorInfo(native_floor_id);
            if (room == nullptr || room->title == nullptr || std::strcmp(room->title, floor.name.c_str()) != 0) {
                std::fprintf(stderr, "moonmod: registered floor %s has no matching native room metadata\n", floor.id.c_str());
                continue;
            }
            room->flag |= floor.room_flags;
            if (floor.seal >= 0) room->seal = static_cast<s8>(floor.seal);
            std::fprintf(stderr, "moonmod: registered floor %s as dungeon %d floor %d from floor %d\n", floor.id.c_str(),
                         dungeon.dungeon, native_floor_id, source_floor);
        }
        (void)id;
    }
}

int ChronicleTwoRequestedDungeonEntryFloor(int requested_floor) {
    if (g_pending_dungeon_entry < 0 || g_pending_dungeon_entry_floor < 0 ||
        GetNowLoopNo() != LOOP_DUNGEON || DngStatus.dungeon_no != g_pending_dungeon_entry) {
        return requested_floor;
    }
    CSaveData *save = GetSaveData();
    if (save == nullptr || save->save_dungeon.stage_id != g_pending_dungeon_entry ||
        save->save_dungeon.floor_id[g_pending_dungeon_entry] != g_pending_dungeon_entry_floor) {
        return requested_floor;
    }
    const int destination_floor = g_pending_dungeon_entry_floor;
    g_pending_dungeon_entry = -1;
    g_pending_dungeon_entry_floor = -1;
    return destination_floor;
}

int ChronicleTwoDungeonFloorSource(int dungeon_id, int floor_id) {
    if (dungeon_id < 0 || dungeon_id >= SAVE_DUNGEON_NUM || floor_id < 0 || floor_id >= 128) return -1;
    int source_floor_id = floor_id;
    for (int depth = 0; depth < 128; ++depth) {
        const ModDungeon *owner = nullptr;
        const ModDungeonFloor *mod_floor = nullptr;
        for (const auto &[id, dungeon] : g_dungeons) {
            (void)id;
            if (dungeon.dungeon != dungeon_id) continue;
            const auto found = std::ranges::find(dungeon.floors, source_floor_id, &ModDungeonFloor::native_floor_id);
            if (found != dungeon.floors.end()) {
                owner = &dungeon;
                mod_floor = &*found;
                break;
            }
        }
        if (mod_floor == nullptr) return source_floor_id;

        int parent_floor_id = mod_floor->source_floor;
        if (!mod_floor->source_floor_ref.empty()) {
            const auto parent = std::ranges::find(owner->floors, mod_floor->source_floor_ref,
                                                   &ModDungeonFloor::local_id);
            if (parent == owner->floors.end() || parent->native_floor_id < 0) return -1;
            parent_floor_id = parent->native_floor_id;
        }
        if (parent_floor_id < 0 || parent_floor_id >= 128 || parent_floor_id == source_floor_id) return -1;
        source_floor_id = parent_floor_id;
    }
    return -1;
}

bool InheritChronicleTwoTreasureFloor(TRESURE_BOX_FLOOR_INFO *table, int dungeon_id, int floor_id) {
    if (table == nullptr || floor_id < 0 || floor_id >= 128) return false;
    const int source_floor_id = ChronicleTwoDungeonFloorSource(dungeon_id, floor_id);
    if (source_floor_id < 0 || source_floor_id >= 128 || source_floor_id == floor_id) {
        return false;
    }
    table->floor[floor_id] = table->floor[source_floor_id];
    return true;
}

void PreloadChronicleTwoCharacterTemplates(CMonsterMan *manager, mgCMemory *memory) {
    if (manager == nullptr || memory == nullptr) return;
    std::vector<int> attempted_definitions;
    attempted_definitions.reserve(g_characters.size());
    for (const auto &[id, character] : g_characters) {
        (void)id;
        const int definition_id = character.model_id.empty() ? character.template_monster_id : character.native_monster_id;
        if (definition_id < 0 || manager->SearchBaseIndex(definition_id) >= 0 ||
            std::ranges::find(attempted_definitions, definition_id) != attempted_definitions.end()) {
            continue;
        }
        attempted_definitions.push_back(definition_id);
        if (!manager->EntryRefer(definition_id, memory)) {
            std::fprintf(stderr, "moonmod: could not preload character model definition %d during dungeon setup\n",
                         definition_id);
        }
    }
}

void ResetChronicleTwoContentForTests() {
    while (!g_animation_plays.empty()) {
        const std::string owner = g_animation_plays.begin()->second.owner;
        UnloadChronicleTwoAnimations(owner);
    }
    g_animations.clear();
    g_animation_plays.clear();
    RestoreChronicleTwoModelReplacements();
    g_items.clear();
    g_weapons.clear();
    g_characters.clear();
    g_models.clear();
    g_character_spawns.clear();
    g_enemy_defeat_event_generations.fill(0);
    ResetMoonModMonsterVariants();
    g_dungeons.clear();
    for (const auto &[id, location] : g_locations) {
        (void)id;
        for (const std::string &resource : location.resources) {
            SetNativeResourceOverridePersistent(location.owner, resource, false);
        }
    }
    g_locations.clear();
    TextureSourceAliasesClear();
    ResetMoonModMapNamesForTests();
    g_pending_dungeon_entry = -1;
    g_pending_dungeon_entry_floor = -1;
    g_georama_pieces.clear();
    for (Json &templates : g_georama_native_templates) templates = Json::array();
    DngLayoutRooms = 0;
    DngLayoutArea = 0;
    DngLayoutRoomMin = 0;
    DngLayoutRoomMax = 0;
    MonsterActiveLimit = 0;
    for (const auto &[item_no, backup] : g_original_weapon_buildups) {
        if (CDataWeapon *weapon = GameItemDataManage.GetWeaponData(item_no); weapon != nullptr) {
            for (int index = 0; index < 3; ++index) weapon->buildup_weapon[index] = backup.buildup_weapon[index];
        }
    }
    g_original_weapon_buildups.clear();
    g_mod_weapon_buildups.clear();
    g_synthesized_weapon_attachments.clear();
    for (const auto &[monster_id, backup] : g_original_monster_drops) {
        if (BASE_MONSTER_TBL *monster = GetMonsterTable(monster_id); monster != nullptr) {
            for (int index = 0; index < 3; ++index) monster->drop_item[index] = backup.drop_item[index];
        }
    }
    g_original_monster_drops.clear();
    g_floor_rule_overrides.clear();
    g_shops_initialized = false;
    g_shops = {};
    g_shop_prices.clear();
}

} // namespace moonmod


