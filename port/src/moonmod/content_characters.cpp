#include "moonmod/content_characters.hpp"

#include <array>
#include <cstring>
#include <fstream>
#include <map>
#include <span>
#include <system_error>

#include "moonmod/content_slots.hpp"
#include "moonmod/native_resource_overrides.hpp"
#include "moonmod/runtime.hpp"
#include "moonmod/symlinks.hpp"
#include "userdata.hpp"

namespace moonmod {
namespace {

struct RegisteredHero {
    std::string id;
    std::string owner;
    std::string name;
    std::string title;
    std::string pack_stem;   /**< mainchr/<stem>.gltf; the native base stems start c01_base or c02_base. */
    bool native_pack = false;
    std::string portrait;    /**< icons/<stem>.png this mod owns, or empty. */
    int fights_as = USER_CHARA_MAX;
    int start_hp = 32;
    int start_defence = 4;
};

struct HeroState {
    CHARA_DATA data{};
    bool joined = false;
};

std::array<std::optional<RegisteredHero>, kRegisteredCharacterSlots> g_heroes;
std::array<HeroState, kRegisteredCharacterSlots> g_state;

constexpr int kFileVersion = 1;

ApiCallResult Invalid(std::string message) { return {ApiCallStatus::InvalidArgument, nullptr, std::move(message)}; }

bool IsLocalIdentifier(std::string_view value) {
    if (value.empty() || value.size() > 64) return false;
    for (unsigned char c : value) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) {
            return false;
        }
    }
    return true;
}

bool IsStem(std::string_view value, std::size_t maximum) {
    if (value.empty() || value.size() > maximum) return false;
    for (unsigned char c : value) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return true;
}

int SlotOf(int character_index) {
    const int slot = character_index - kRegisteredCharacterBase;
    return slot >= 0 && slot < kRegisteredCharacterSlots && g_heroes[static_cast<std::size_t>(slot)] ? slot : -1;
}

void ResetSlot(int slot) {
    HeroState &state = g_state[static_cast<std::size_t>(slot)];
    std::memset(static_cast<void *>(&state.data), 0, sizeof(state.data));
    const auto &hero = g_heroes[static_cast<std::size_t>(slot)];
    const float life = static_cast<float>(hero ? hero->start_hp : 32);
    state.data.hp.max = life;
    state.data.hp.now = life;
    state.data.defence = static_cast<u16>(hero ? hero->start_defence : 4);
    for (CGameDataUsed &item : state.data.active_item) item.Init();
    for (CGameDataUsed &item : state.data.equip) item.Init();
    state.joined = false;
}

std::string Hex(std::span<const unsigned char> bytes) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string text;
    text.reserve(bytes.size() * 2);
    for (unsigned char byte : bytes) {
        text += digits[byte >> 4];
        text += digits[byte & 15];
    }
    return text;
}

bool Unhex(const std::string &text, std::span<unsigned char> bytes) {
    if (text.size() != bytes.size() * 2) return false;
    auto nibble = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
    for (std::size_t i = 0; i < bytes.size(); i++) {
        const int high = nibble(text[2 * i]);
        const int low = nibble(text[2 * i + 1]);
        if (high < 0 || low < 0) return false;
        bytes[i] = static_cast<unsigned char>(high << 4 | low);
    }
    return true;
}

Json UsedHex(const CGameDataUsed &item) {
    return Hex({reinterpret_cast<const unsigned char *>(&item), sizeof(item)});
}

bool UsedFromHex(const Json &value, CGameDataUsed &item) {
    if (!value.is_string()) return false;
    CGameDataUsed read;
    if (!Unhex(value.get<std::string>(), {reinterpret_cast<unsigned char *>(&read), sizeof(read)})) return false;
    std::memcpy(static_cast<void *>(&item), &read, sizeof(item));
    return true;
}

Json Entry(const HeroState &state, bool leader) {
    const CHARA_DATA &data = state.data;
    Json active = Json::array();
    for (const CGameDataUsed &item : data.active_item) active.push_back(UsedHex(item));
    Json equip = Json::array();
    for (const CGameDataUsed &item : data.equip) equip.push_back(UsedHex(item));
    return {{"joined", state.joined},
            {"leader", leader},
            {"hp", data.hp.now},
            {"max_hp", data.hp.max},
            {"defence", data.defence},
            {"status_attr", data.status_attr},
            {"status_time", {data.status_time[0], data.status_time[1], data.status_time[2], data.status_time[3]}},
            {"keep_costume_on_equip_change", data.keep_costume_on_equip_change},
            {"active_items", std::move(active)},
            {"equip", std::move(equip)}};
}

bool Apply(const Json &entry, HeroState &state) {
    if (!entry.is_object()) return false;
    CHARA_DATA &data = state.data;
    const auto active = entry.find("active_items");
    const auto equip = entry.find("equip");
    if (active == entry.end() || !active->is_array() || active->size() != 3 || equip == entry.end() ||
        !equip->is_array() || equip->size() != 5) {
        return false;
    }
    for (std::size_t index = 0; index < 3; ++index) {
        if (!UsedFromHex((*active)[index], data.active_item[index])) return false;
    }
    for (std::size_t index = 0; index < 5; ++index) {
        if (!UsedFromHex((*equip)[index], data.equip[index])) return false;
    }
    data.hp.now = entry.value("hp", data.hp.now);
    data.hp.max = entry.value("max_hp", data.hp.max);
    data.defence = entry.value("defence", data.defence);
    data.status_attr = entry.value("status_attr", static_cast<u16>(0));
    if (const auto times = entry.find("status_time"); times != entry.end() && times->is_array() && times->size() == 4) {
        for (std::size_t index = 0; index < 4; ++index) data.status_time[index] = (*times)[index].get<s16>();
    }
    data.keep_costume_on_equip_change = entry.value("keep_costume_on_equip_change", static_cast<u8>(0));
    state.joined = entry.value("joined", false);
    return true;
}

Json ReadFile(const std::filesystem::path &path, bool &present, bool &valid) {
    present = false;
    valid = true;
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) return Json::object();
    present = true;
    if (std::filesystem::file_size(path, ec) > 4 * 1024 * 1024 || ec) {
        valid = false;
        return Json::object();
    }
    std::ifstream input(path, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    const Json file = Json::parse(text, nullptr, false);
    if (!file.is_object() || file.value("version", 0) != kFileVersion || !file.contains("saves") || !file["saves"].is_object()) {
        valid = false;
        return Json::object();
    }
    return file;
}

} // namespace

ApiCallResult RegisterChronicleTwoPlayable(std::string_view mod_id, const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) return Invalid("expected one playable character definition");
    const Json &definition = args[0];
    for (auto it = definition.begin(); it != definition.end(); ++it) {
        static constexpr std::string_view allowed[] = {"id", "name", "pack", "portrait", "title", "fights_as", "hp", "defense", "defence"};
        bool known = false;
        for (std::string_view key : allowed) known = known || key == it.key();
        if (!known) return Invalid("register_playable accepts id, name, pack, portrait, title, fights_as, hp, and defense only");
    }
    const auto id_it = definition.find("id");
    const auto name_it = definition.find("name");
    const auto pack_it = definition.find("pack");
    if (id_it == definition.end() || !id_it->is_string() || name_it == definition.end() || !name_it->is_string() ||
        pack_it == definition.end() || !pack_it->is_string()) {
        return Invalid("register_playable requires string id, name, and pack fields");
    }
    std::string local_id = id_it->get<std::string>();
    std::string qualified_id;
    if (const std::size_t separator = local_id.find(':'); separator == std::string::npos) {
        if (!IsLocalIdentifier(local_id)) return Invalid("character id must be a short identifier using letters, digits, '.', '_' or '-'");
        qualified_id = std::string(mod_id) + ":" + local_id;
    } else {
        if (local_id.find(':', separator + 1) != std::string::npos || local_id.substr(0, separator) != mod_id ||
            !IsLocalIdentifier(std::string_view(local_id).substr(separator + 1))) {
            return Invalid("fully qualified character ids must use this mod's namespace");
        }
        qualified_id = local_id;
    }
    RegisteredHero hero;
    hero.id = qualified_id;
    hero.owner = std::string(mod_id);
    hero.name = name_it->get<std::string>();
    if (hero.name.empty() || hero.name.size() > 31 || hero.name.find('\0') != std::string::npos) {
        return Invalid("character name must contain 1-31 bytes");
    }
    if (const auto title = definition.find("title"); title != definition.end()) {
        if (!title->is_string() || title->get_ref<const std::string &>().size() > 40 || title->get_ref<const std::string &>().empty()) {
            return Invalid("title must contain 1-40 bytes");
        }
        hero.title = title->get<std::string>();
    } else {
        hero.title = hero.name;
    }
    if (const auto fights = definition.find("fights_as"); fights != definition.end()) {
        if (!fights->is_string()) return Invalid("fights_as must be max or monica");
        const std::string &who = fights->get_ref<const std::string &>();
        if (who == "max") hero.fights_as = USER_CHARA_MAX;
        else if (who == "monica") hero.fights_as = USER_CHARA_MONICA;
        else return Invalid("fights_as must be max or monica");
    }
    hero.start_hp = hero.fights_as == USER_CHARA_MAX ? 32 : 48;
    hero.start_defence = hero.fights_as == USER_CHARA_MAX ? 4 : 8;
    for (const char *key : {"hp", "defense", "defence"}) {
        const auto found = definition.find(key);
        if (found == definition.end()) continue;
        const bool is_hp = std::string_view(key) == "hp";
        if (!found->is_number_integer() || found->get<long long>() < (is_hp ? 1 : 0) || found->get<long long>() > 999) {
            return Invalid(is_hp ? "hp must be from 1 through 999" : "defense must be from 0 through 999");
        }
        (is_hp ? hero.start_hp : hero.start_defence) = found->get<int>();
    }
    const std::string &pack = pack_it->get_ref<const std::string &>();
    std::string stem = pack;
    if (stem.starts_with("mainchr/")) stem = stem.substr(8);
    if (stem.ends_with(".gltf")) stem = stem.substr(0, stem.size() - 5);
    if (!IsStem(stem, 15)) return Invalid("pack must name mainchr/<stem>.gltf (stem 1-15 of a-z, 0-9, _)");
    hero.pack_stem = stem;
    hero.native_pack = stem.starts_with("c01_base") || stem.starts_with("c02_base");
    if (!hero.native_pack && !NativeResourceOverrideOwnedBy(mod_id, "mainchr/" + stem + ".gltf")) {
        return Invalid("pack names mainchr/" + stem + ".gltf, which is neither a native base pack nor this mod's resource override");
    }
    if (const auto portrait = definition.find("portrait"); portrait != definition.end()) {
        if (!portrait->is_string() || !IsStem(portrait->get_ref<const std::string &>(), 31)) {
            return Invalid("portrait must be the stem of an icons/<stem>.png this mod owns");
        }
        hero.portrait = "icons/" + portrait->get<std::string>() + ".png";
        if (!NativeResourceOverrideOwnedBy(mod_id, hero.portrait)) {
            return Invalid("portrait names " + hero.portrait + ", which this mod has not registered as a resource override");
        }
    }

    int slot = -1;
    for (int index = 0; index < kRegisteredCharacterSlots; ++index) {
        const auto &existing = g_heroes[static_cast<std::size_t>(index)];
        if (existing && existing->id == qualified_id) {
            if (existing->owner != mod_id) return Invalid("a playable character with this id belongs to another mod");
            slot = index;
            break;
        }
    }
    const bool updating = slot >= 0;
    if (!updating) {
        for (int index = 0; index < kRegisteredCharacterSlots && slot < 0; ++index) {
            if (!g_heroes[static_cast<std::size_t>(index)]) slot = index;
        }
        if (slot < 0) {
            return {ApiCallStatus::Failed, nullptr,
                    "Chronicle Two holds up to " + std::to_string(kRegisteredCharacterSlots) + " registered playable characters"};
        }
    }
    g_heroes[static_cast<std::size_t>(slot)] = std::move(hero);
    if (!updating) ResetSlot(slot);
    int count = 0;
    for (const auto &entry : g_heroes) count += entry ? 1 : 0;
    return {ApiCallStatus::Ok,
            {{"id", qualified_id}, {"character_id", kRegisteredCharacterBase + slot}, {"count", kRegisteredCharacterBase + count},
             {"fights_as", g_heroes[static_cast<std::size_t>(slot)]->fights_as == USER_CHARA_MAX ? "max" : "monica"},
             {"updated", updating}},
            {}};
}

bool ChronicleTwoRegisteredCharacterIndex(std::string_view id, int &character_index) {
    for (int slot = 0; slot < kRegisteredCharacterSlots; ++slot) {
        const auto &hero = g_heroes[static_cast<std::size_t>(slot)];
        if (hero && hero->id == id) {
            character_index = kRegisteredCharacterBase + slot;
            return true;
        }
    }
    return false;
}

std::string ChronicleTwoRegisteredCharacterId(int character_index) {
    const int slot = SlotOf(character_index);
    return slot < 0 ? std::string() : g_heroes[static_cast<std::size_t>(slot)]->id;
}

const char *ChronicleTwoRegisteredCharacterIdText(int character_index) {
    const int slot = SlotOf(character_index);
    return slot < 0 ? nullptr : g_heroes[static_cast<std::size_t>(slot)]->id.c_str();
}

bool ChronicleTwoRegisteredCharacterBody(int character_index, int &body) {
    const int slot = SlotOf(character_index);
    if (slot < 0) return false;
    body = g_heroes[static_cast<std::size_t>(slot)]->fights_as;
    return true;
}

std::optional<std::string> ChronicleTwoRegisteredCharacterPack(int character_index, int alternate) {
    const int slot = SlotOf(character_index);
    if (slot < 0) return std::nullopt;
    const RegisteredHero &hero = *g_heroes[static_cast<std::size_t>(slot)];
    if (hero.native_pack) {
        // A native base stem follows the game's own costume/weapon numbering for the character it fights as.
        CHARA_DATA &data = g_state[static_cast<std::size_t>(slot)].data;
        int model_number = data.equip[CHARA_EQUIP_MELEE].GetModelNo();
        model_number = model_number < 0 ? 0 : model_number > 3 ? 3 : model_number;
        if (hero.fights_as == USER_CHARA_MAX) model_number += 2;
        const char *prefix = hero.fights_as == USER_CHARA_MAX ? "c01_base" : "c02_base";
        return alternate != 0 ? std::string(prefix) + ".gltf" : std::string(prefix) + std::to_string(model_number) + ".gltf";
    }
    return hero.pack_stem + ".gltf";
}

CHARA_DATA *ChronicleTwoRegisteredCharaData(int character_index) {
    const int slot = SlotOf(character_index);
    return slot < 0 ? nullptr : &g_state[static_cast<std::size_t>(slot)].data;
}

ApiCallResult RecruitChronicleTwoPlayable(const Json &definition) {
    for (auto it = definition.begin(); it != definition.end(); ++it) {
        if (it.key() != "id" && it.key() != "joined") return Invalid("expected {id = registered playable id, joined = optional boolean}");
    }
    const auto id = definition.find("id");
    const auto joined = definition.find("joined");
    if (!id->is_string() || (joined != definition.end() && !joined->is_boolean())) {
        return Invalid("expected {id = registered playable id, joined = optional boolean}");
    }
    int index = -1;
    if (!ChronicleTwoRegisteredCharacterIndex(id->get_ref<const std::string &>(), index)) {
        return Invalid("id must name a registered playable character");
    }
    const bool join = joined == definition.end() || joined->get<bool>();
    const bool changed = ChronicleTwoRegisteredCharacterJoined(index) != join;
    SetChronicleTwoRegisteredCharacterJoined(index, join);
    Json result = {{"id", id->get<std::string>()}, {"character_id", index}, {"status", join ? "in_party" : "left"},
                   {"recruited", join}, {"changed", changed}};
    if (changed) SharedRuntime().QueueEvent("game.party.recruited", result);
    return {ApiCallStatus::Ok, std::move(result), {}};
}

bool ChronicleTwoRegisteredCharacterJoined(int character_index) {
    const int slot = SlotOf(character_index);
    return slot >= 0 && g_state[static_cast<std::size_t>(slot)].joined;
}

void SetChronicleTwoRegisteredCharacterJoined(int character_index, bool joined) {
    const int slot = SlotOf(character_index);
    if (slot >= 0) g_state[static_cast<std::size_t>(slot)].joined = joined;
}

Json ChronicleTwoRegisteredCharacterJson(int character_index, int active_character_id) {
    const int slot = SlotOf(character_index);
    if (slot < 0) return nullptr;
    const RegisteredHero &hero = *g_heroes[static_cast<std::size_t>(slot)];
    const HeroState &state = g_state[static_cast<std::size_t>(slot)];
    Json result = {{"id", hero.id},
                   {"character_id", character_index},
                   {"kind", "playable"},
                   {"name", hero.name},
                   {"title", hero.title},
                   {"fights_as", hero.fights_as == USER_CHARA_MAX ? "max" : "monica"},
                   {"pack", "mainchr/" + hero.pack_stem + ".gltf"},
                   {"in_party", state.joined},
                   {"active", character_index == active_character_id},
                   {"hp", state.data.hp.now},
                   {"max_hp", state.data.hp.max},
                   {"defence", state.data.defence},
                   {"mod_owned", true},
                   {"owner", hero.owner}};
    if (!hero.portrait.empty()) result["portrait"] = hero.portrait;
    return result;
}

Json ChronicleTwoRegisteredCharactersJson(int active_character_id) {
    Json result = Json::array();
    for (int slot = 0; slot < kRegisteredCharacterSlots; ++slot) {
        if (g_heroes[static_cast<std::size_t>(slot)]) result.push_back(ChronicleTwoRegisteredCharacterJson(kRegisteredCharacterBase + slot, active_character_id));
    }
    return result;
}

void ChronicleTwoResetRegisteredCharacterState() {
    for (int slot = 0; slot < kRegisteredCharacterSlots; ++slot) ResetSlot(slot);
}

std::filesystem::path ChronicleTwoRegisteredCharactersFile() {
    return ChronicleTwoContentSaveRoot() / "moonmod" / "characters.json";
}

bool ChronicleTwoRegisteredCharactersSave(std::string_view identity) {
    const std::filesystem::path root = ChronicleTwoContentSaveRoot();
    if (root.empty() || identity.empty()) return false;
    const std::filesystem::path path = ChronicleTwoRegisteredCharactersFile();
    if (ContainsSymlinkBelow(path, root)) return false;
    bool present = false;
    bool valid = true;
    Json file = ReadFile(path, present, valid);
    if (!valid) file = Json::object();
    if (!file.contains("saves") || !file["saves"].is_object()) file["saves"] = Json::object();
    file["version"] = kFileVersion;
    Json &saved = file["saves"][std::string(identity)];
    if (!saved.is_object()) saved = Json::object();
    for (int slot = 0; slot < kRegisteredCharacterSlots; ++slot) {
        const auto &hero = g_heroes[static_cast<std::size_t>(slot)];
        if (!hero) continue;
        saved[hero->id] = Entry(g_state[static_cast<std::size_t>(slot)], false);
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return false;
    const std::filesystem::path temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return false;
        const std::string text = file.dump(2);
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        output.flush();
        if (!output.good()) return false;
    }
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    return true;
}

bool ChronicleTwoRegisteredCharactersLoad(std::string_view identity) {
    ChronicleTwoResetRegisteredCharacterState();
    const std::filesystem::path root = ChronicleTwoContentSaveRoot();
    if (root.empty() || identity.empty()) return true;
    const std::filesystem::path path = ChronicleTwoRegisteredCharactersFile();
    if (ContainsSymlinkBelow(path, root)) return false;
    bool present = false;
    bool valid = true;
    const Json file = ReadFile(path, present, valid);
    if (!present) return true;
    if (!valid) return false;
    const auto saved = file["saves"].find(std::string(identity));
    if (saved == file["saves"].end() || !saved->is_object()) return true;
    bool ok = true;
    for (int slot = 0; slot < kRegisteredCharacterSlots; ++slot) {
        const auto &hero = g_heroes[static_cast<std::size_t>(slot)];
        if (!hero) continue;
        const auto entry = saved->find(hero->id);
        if (entry == saved->end()) continue;
        if (!Apply(*entry, g_state[static_cast<std::size_t>(slot)])) {
            ResetSlot(slot);
            ok = false;
        }
    }
    return ok;
}

void UnloadChronicleTwoRegisteredCharacters(std::string_view owner) {
    for (int slot = 0; slot < kRegisteredCharacterSlots; ++slot) {
        auto &hero = g_heroes[static_cast<std::size_t>(slot)];
        if (hero && hero->owner == owner) {
            hero.reset();
            ResetSlot(slot);
        }
    }
}

void ResetChronicleTwoRegisteredCharactersForTests() {
    for (auto &hero : g_heroes) hero.reset();
    for (int slot = 0; slot < kRegisteredCharacterSlots; ++slot) ResetSlot(slot);
}

} // namespace moonmod
