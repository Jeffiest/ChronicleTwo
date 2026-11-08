#include "moonmod/content_items.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <vector>

#include "gamedata.hpp"
#include "mg_memory.hpp"
#include "mg_texture.hpp"
#include "moonmod/content_characters.hpp"
#include "moonmod/content_slots.hpp"
#include "moonmod/gamedata_moonmod.hpp"
#include "moonmod/native_resource_overrides.hpp"
#include "platform/arena.hpp"
#include "platform/datapacks.hpp"
#include "userdata.hpp"

extern short local_itemdatano_converttable[ITEM_NO_MAX] asm("GameData_ItemIndex");

namespace moonmod {
namespace {

struct ContentExtras {
    std::string owner_mod;
    std::string description;
    std::string icon_stem;
    std::string model_stem;
    std::string owner;      /**< "max", "monica" or a registered hero's id; empty keeps the template's type. */
    std::string holder;     /**< The registered hero that holds the weapon, when owner names one. */
    int combat_character = -1;
    bool weapon = false;
    int item_no = -1;
    int message_slot = -1;
};

std::map<std::string, ContentExtras, std::less<>> g_extras;
std::array<std::vector<u16>, kModItemMessageSlots> g_message_lines;
int g_message_slots_used = 0;

struct IconOverride {
    std::string game_path;
    std::vector<std::uint8_t> rgba; /**< 32 x 32 x 4, alpha already halved to the GS range. */
    mgCTexture *texture = nullptr;
    bool failed = false;
};
std::map<int, IconOverride> g_icons;

constexpr int kIconSide = 32;
constexpr int kIconBlock = 0;
constexpr int kIconSlots = 64;

/** Texture ownership for icon overrides, apart from each mode's own texture table (as item_icon3d.cpp keeps its own). */
struct IconTextures {
    mgCMemory         memory;
    mgCTextureManager manager;
    bool              ready = false;
    std::vector<std::unique_ptr<u_long128[]>> pixels;

    void Prepare() {
        if (ready) return;
        constexpr auto bytes = 2 * sizeof(mgCTextureBlock) +
                               kIconSlots * (sizeof(mgCTexture) + sizeof(mgTEXTURE_HASH) + sizeof(mgCTexture *) + sizeof(mgTEXTURE_HASH *)) + 1024;
        constexpr auto quadwords = (bytes + sizeof(u_long128) - 1) / sizeof(u_long128);
        memory.stSetBuffer(reinterpret_cast<u_long128 *>(ArenaBlock(&memory, quadwords * sizeof(u_long128))),
                           static_cast<int>(quadwords));
        manager.SetTableBuffer(kIconSlots, kIconBlock + 1, &memory);
        manager.Initialize(0, -1);
        ready = true;
    }
};

IconTextures &Textures() {
    static IconTextures textures;
    return textures;
}

bool IsStem(std::string_view value, std::size_t maximum) {
    if (value.empty() || value.size() > maximum) return false;
    for (unsigned char c : value) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return true;
}

bool ReadDescription(const Json &value, std::string &out, std::string &error) {
    std::vector<std::string> lines;
    if (value.is_string()) {
        lines.push_back(value.get<std::string>());
    } else if (value.is_array() && !value.empty() && value.size() <= 4) {
        for (const Json &line : value) {
            if (!line.is_string()) {
                error = "description must be a string or up to 4 lines of 1-64 bytes";
                return false;
            }
            lines.push_back(line.get<std::string>());
        }
    } else {
        error = "description must be a string or up to 4 lines of 1-64 bytes";
        return false;
    }
    std::string joined;
    for (const std::string &line : lines) {
        if (line.empty() || line.size() > 64 || line.find('\0') != std::string::npos) {
            error = "description lines must contain 1-64 bytes";
            return false;
        }
        if (!joined.empty()) joined += '\n';
        joined += line;
    }
    if (joined.size() > 255) {
        error = "description must fit 255 bytes";
        return false;
    }
    out = joined;
    return true;
}

/** Nearest-neighbour fit of a decoded PNG into the 32 x 32 menu cell with GS alpha (0x80 opaque). */
void FitIcon(const std::vector<std::uint8_t> &source, unsigned width, unsigned height, std::vector<std::uint8_t> &out) {
    out.assign(static_cast<std::size_t>(kIconSide) * kIconSide * 4, 0);
    for (int y = 0; y < kIconSide; ++y) {
        const unsigned sy = std::min(height - 1, static_cast<unsigned>(y) * height / kIconSide);
        for (int x = 0; x < kIconSide; ++x) {
            const unsigned sx = std::min(width - 1, static_cast<unsigned>(x) * width / kIconSide);
            const std::size_t from = (static_cast<std::size_t>(sy) * width + sx) * 4;
            const std::size_t to = (static_cast<std::size_t>(y) * kIconSide + x) * 4;
            out[to] = source[from];
            out[to + 1] = source[from + 1];
            out[to + 2] = source[from + 2];
            out[to + 3] = static_cast<std::uint8_t>((source[from + 3] + 1) / 2);
        }
    }
}

bool LoadIconPixels(std::string_view game_path, std::vector<std::uint8_t> &rgba, std::string &error) {
    const auto path = FindNativeResourceOverride(game_path);
    if (!path) {
        error = "the icon override is no longer registered";
        return false;
    }
    std::ifstream input(*path, std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (bytes.empty() || bytes.size() > 2 * 1024 * 1024) {
        error = "the icon file is empty or larger than 2 MiB";
        return false;
    }
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> decoded;
    if (!datapacks::DecodePng(bytes, width, height, decoded, error) || width == 0 || height == 0 ||
        decoded.size() < static_cast<std::size_t>(width) * height * 4) {
        if (error.empty()) error = "the icon is not a PNG";
        return false;
    }
    FitIcon(decoded, width, height, rgba);
    return true;
}

} // namespace

bool ChronicleTwoContentParseExtras(std::string_view mod_id, std::string_view qualified_id, const Json &definition,
                                    bool weapon, std::string &error) {
    ContentExtras extras;
    extras.owner_mod = std::string(mod_id);
    extras.weapon = weapon;
    if (definition.contains("menu_scale")) {
        error = "Chronicle Two's weapon menu has no per-weapon model scale; menu_scale is unsupported";
        return false;
    }
    if (const auto found = definition.find("description"); found != definition.end()) {
        if (!ReadDescription(*found, extras.description, error)) return false;
    }
    if (const auto found = definition.find("icon"); found != definition.end()) {
        if (!found->is_string() || !IsStem(found->get_ref<const std::string &>(), 31)) {
            error = "icon must be the stem of an icons/<stem>.png this mod owns (1-31 of a-z, 0-9, _)";
            return false;
        }
        extras.icon_stem = found->get<std::string>();
        const std::string game_path = "icons/" + extras.icon_stem + ".png";
        if (!NativeResourceOverrideOwnedBy(mod_id, game_path)) {
            error = "icon names " + game_path + ", which this mod has not registered as a resource override";
            return false;
        }
        std::vector<std::uint8_t> rgba;
        if (!LoadIconPixels(game_path, rgba, error)) {
            error = "icon " + game_path + ": " + error;
            return false;
        }
    }
    if (const auto found = definition.find("model"); found != definition.end()) {
        if (!weapon) {
            error = "only weapons take a model; items draw their template's icon and model";
            return false;
        }
        if (!found->is_string() || !IsStem(found->get_ref<const std::string &>(), 15)) {
            error = "model must be the stem of a mainchr/<stem>.gltf this mod owns (1-15 of a-z, 0-9, _)";
            return false;
        }
        extras.model_stem = found->get<std::string>();
        const std::string game_path = "mainchr/" + extras.model_stem + ".gltf";
        if (!NativeResourceOverrideOwnedBy(mod_id, game_path)) {
            error = "model names " + game_path + ", which this mod has not registered as a resource override";
            return false;
        }
    }
    if (const auto found = definition.find("owner"); found != definition.end()) {
        if (!weapon) {
            error = "only weapons take an owner";
            return false;
        }
        if (!found->is_string()) {
            error = "owner must be max, monica, or a registered playable character id";
            return false;
        }
        extras.owner = found->get<std::string>();
        if (!ChronicleTwoResolveWeaponOwner(extras.owner, extras.combat_character, extras.holder, error)) return false;
    }
    g_extras[std::string(qualified_id)] = std::move(extras);
    return true;
}

std::size_t ChronicleTwoContentExtraKeyCount(const Json &definition) {
    std::size_t count = 0;
    for (const char *key : {"description", "icon", "model", "owner"}) {
        if (definition.contains(key)) ++count;
    }
    return count;
}

void ChronicleTwoContentForgetExtras(std::string_view qualified_id) {
    const auto found = g_extras.find(qualified_id);
    if (found != g_extras.end() && found->second.item_no < 0) g_extras.erase(found);
}

bool ChronicleTwoContentReserveItemNumber(std::string_view qualified_id, bool weapon, int &item_no, std::string &error) {
    const bool reserved = AllocateChronicleTwoContentSlot(
        weapon ? ContentSlotKind::Weapon : ContentSlotKind::Item, qualified_id, ITEM_NO_MAX - 1, 1,
        [](int candidate) { return local_itemdatano_converttable[candidate] < 0; }, item_no, error);
    if (!reserved) return false;
    if (item_no <= 0 || item_no >= ITEM_NO_MAX || local_itemdatano_converttable[item_no] >= 0) {
        error = "the persisted item number " + std::to_string(item_no) + " is taken by another item in this build";
        return false;
    }
    SetMoonModNextItemNo(item_no);
    return true;
}

void ChronicleTwoContentApplyExtras(std::string_view qualified_id, int item_no) {
    const auto found = g_extras.find(qualified_id);
    if (found == g_extras.end()) return;
    ContentExtras &extras = found->second;
    extras.item_no = item_no;
    CDataCommon *common = GameItemDataManage.GetCommonData(item_no);
    if (common == nullptr) return;
    if (extras.combat_character >= 0 && extras.weapon) {
        const bool melee = common->type == ITEM_DATA_MAX_MELEE || common->type == ITEM_DATA_MONICA_MELEE;
        const bool ranged = common->type == ITEM_DATA_MAX_GUN || common->type == ITEM_DATA_MONICA_MAGIC;
        if (melee || ranged) {
            common->type = static_cast<s16>(extras.combat_character == USER_CHARA_MAX
                                                ? (melee ? ITEM_DATA_MAX_MELEE : ITEM_DATA_MAX_GUN)
                                                : (melee ? ITEM_DATA_MONICA_MELEE : ITEM_DATA_MONICA_MAGIC));
        }
    }
    if (!extras.model_stem.empty()) {
        std::memset(common->file_name, 0, sizeof(common->file_name));
        std::memcpy(common->file_name, extras.model_stem.data(),
                    std::min(extras.model_stem.size(), sizeof(common->file_name) - 1));
    }
    if (!extras.description.empty()) {
        if (extras.message_slot < 0 && g_message_slots_used < kModItemMessageSlots) {
            extras.message_slot = g_message_slots_used++;
        }
        if (extras.message_slot >= 0) {
            g_message_lines[static_cast<std::size_t>(extras.message_slot)] = dc2::text::EncodeMessageText(extras.description);
            common->message_no = static_cast<s16>(kModItemMessageBase + extras.message_slot);
        }
    }
    if (!extras.icon_stem.empty()) {
        IconOverride &icon = g_icons[item_no];
        icon.game_path = "icons/" + extras.icon_stem + ".png";
        icon.texture = nullptr;
        icon.failed = false;
        std::string error;
        if (!LoadIconPixels(icon.game_path, icon.rgba, error)) icon.failed = true;
    }
}

Json ChronicleTwoContentAssetsJson(std::string_view qualified_id) {
    Json result = Json::object();
    const auto found = g_extras.find(qualified_id);
    if (found == g_extras.end()) return result;
    const ContentExtras &extras = found->second;
    if (!extras.model_stem.empty()) result["model"] = "mainchr/" + extras.model_stem + ".gltf";
    if (!extras.icon_stem.empty()) result["icon"] = "icons/" + extras.icon_stem + ".png";
    if (!extras.description.empty()) result["description"] = extras.description;
    if (!extras.owner.empty()) {
        result["owner"] = extras.owner;
        result["owner_character"] = extras.combat_character == USER_CHARA_MAX ? "max" : "monica";
        if (!extras.holder.empty()) result["holder"] = extras.holder;
    }
    if (extras.message_slot >= 0) result["message_no"] = kModItemMessageBase + extras.message_slot;
    return result;
}

std::span<const u16> ChronicleTwoModMessageLine(int line_id) {
    // Names come from CDataCommon::name; the description is the item's message + 10000 (GetItemMessageNo kind 1).
    const int slot = line_id - kModItemMessageBase - 10000;
    if (slot < 0 || slot >= g_message_slots_used) return {};
    return g_message_lines[static_cast<std::size_t>(slot)];
}

std::string ChronicleTwoItemIconOverridePath(int item_no) {
    const auto found = g_icons.find(item_no);
    return found == g_icons.end() ? std::string() : found->second.game_path;
}

mgCTexture *ChronicleTwoItemIconOverride(int item_no) {
    const auto found = g_icons.find(item_no);
    if (found == g_icons.end() || found->second.failed) return nullptr;
    IconOverride &icon = found->second;
    if (icon.texture != nullptr) return icon.texture;
    IconTextures &textures = Textures();
    textures.Prepare();
    constexpr std::size_t quads = static_cast<std::size_t>(kIconSide) * kIconSide * 4 / sizeof(u_long128);
    auto pixels = std::make_unique<u_long128[]>(quads);
    std::memcpy(pixels.get(), icon.rgba.data(), icon.rgba.size());
    u_long128 *levels[2] = {pixels.get(), nullptr};
    char name[32];
    std::snprintf(name, sizeof name, "moonicon%d", item_no);
    mgCTexture *texture = textures.manager.EnterTexture(kIconBlock, name, levels, kIconSide, kIconSide, 32, nullptr, 0, 0);
    if (texture == nullptr) {
        icon.failed = true;
        return nullptr;
    }
    texture->tex0.bits.tcc = 1;
    textures.pixels.push_back(std::move(pixels));
    icon.texture = texture;
    return texture;
}

bool ChronicleTwoResolveWeaponOwner(std::string_view owner, int &combat_character, std::string &holder_id,
                                    std::string &error) {
    holder_id.clear();
    if (owner == "max") {
        combat_character = USER_CHARA_MAX;
        return true;
    }
    if (owner == "monica") {
        combat_character = USER_CHARA_MONICA;
        return true;
    }
    int index = -1;
    if (ChronicleTwoRegisteredCharacterIndex(owner, index) && ChronicleTwoRegisteredCharacterBody(index, combat_character)) {
        holder_id = std::string(owner);
        return true;
    }
    error = "owner must be max, monica, or a registered playable character id";
    return false;
}

void ResetChronicleTwoContentItemsForTests() {
    g_extras.clear();
    for (auto &line : g_message_lines) line.clear();
    g_message_slots_used = 0;
    g_icons.clear();
    IconTextures &textures = Textures();
    if (textures.ready) textures.manager.Initialize(0, -1);
    textures.pixels.clear();
}

} // namespace moonmod
