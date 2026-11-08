#include "moonmod/content_npcs.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <numbers>

#include "dataread.hpp"
#include "mg_memory.hpp"
#include "mg_texture.hpp"
#include "mglib.hpp"
#include "moonmod/native_resource_overrides.hpp"
#include "moonmod/runtime.hpp"
#include "scene.hpp"
#include "sceneevent.hpp"
#include "scenesnd.hpp"
#include "scenevillager.hpp"
#include "text/bounded.hpp"
#include "villagermngr.hpp"

namespace moonmod {
namespace {

struct ModNpc {
    std::string id;
    std::string owner;
    std::string name;
    std::string model;
    int map_no = -1;
    std::array<float, 3> position{};
    float facing_degrees = 0.0f;
    int talk_event = 0;
    int chara_no = -1;
    int slot = -1;
    std::string load_note;
};

std::map<std::string, ModNpc, std::less<>> g_npcs;
int g_next_chara_no = kModVillagerCharaBase;

bool IsModelStem(std::string_view value) {
    if (value.empty() || value.size() > 31) return false;
    for (unsigned char c : value) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return true;
}

bool IsLocalIdentifier(std::string_view value) {
    if (value.empty() || value.size() > 64) return false;
    for (unsigned char c : value) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) {
            return false;
        }
    }
    return true;
}

Json NpcJson(const ModNpc &npc) {
    return {{"id", npc.id},
            {"kind", "npc"},
            {"name", npc.name},
            {"owner", npc.owner},
            {"map_no", npc.map_no},
            {"model", "chara/" + npc.model + ".gltf"},
            {"position", {{"x", npc.position[0]}, {"y", npc.position[1]}, {"z", npc.position[2]}}},
            {"facing", npc.facing_degrees},
            {"talk_event", npc.talk_event},
            {"chara_no", npc.chara_no},
            {"slot", npc.slot < 0 ? Json(nullptr) : Json(npc.slot)},
            {"status", npc.slot >= 0 ? "placed" : "registered"},
            {"note", npc.load_note},
            {"mod_owned", true}};
}

ApiCallResult Invalid(std::string message) { return {ApiCallStatus::InvalidArgument, nullptr, std::move(message)}; }

} // namespace

ApiCallResult RegisterChronicleTwoNpc(std::string_view mod_id, const Json &definition) {
    if (!definition.is_object()) return Invalid("expected one villager definition object");
    for (auto it = definition.begin(); it != definition.end(); ++it) {
        static constexpr std::string_view allowed[] = {"id", "kind", "name", "map_no", "model", "position", "facing", "talk_event"};
        bool known = false;
        for (std::string_view key : allowed) known = known || key == it.key();
        if (!known) return Invalid("villager definition accepts id, kind, name, map_no, model, position, facing, and talk_event only");
    }
    const auto id_it = definition.find("id");
    const auto name_it = definition.find("name");
    const auto model_it = definition.find("model");
    const auto map_it = definition.find("map_no");
    const auto position_it = definition.find("position");
    if (id_it == definition.end() || !id_it->is_string() || name_it == definition.end() || !name_it->is_string()) {
        return Invalid("villager definition requires string id and name fields");
    }
    std::string local_id = id_it->get<std::string>();
    std::string qualified_id;
    if (const std::size_t separator = local_id.find(':'); separator == std::string::npos) {
        if (!IsLocalIdentifier(local_id)) return Invalid("villager id must be a short identifier using letters, digits, '.', '_' or '-'");
        qualified_id = std::string(mod_id) + ":" + local_id;
    } else {
        if (local_id.find(':', separator + 1) != std::string::npos || local_id.substr(0, separator) != mod_id ||
            !IsLocalIdentifier(std::string_view(local_id).substr(separator + 1))) {
            return Invalid("fully qualified villager ids must use this mod's namespace");
        }
        qualified_id = local_id;
    }
    const std::string &name = name_it->get_ref<const std::string &>();
    if (name.empty() || name.size() > 63 || name.find('\0') != std::string::npos) return Invalid("villager name must contain 1-63 bytes");
    if (map_it == definition.end() || !map_it->is_number_integer() || map_it->get<long long>() < 0 || map_it->get<long long>() > 255) {
        return Invalid("map_no must be the number of the town map the villager stands in, 0 through 255");
    }
    if (model_it == definition.end() || !model_it->is_string() || !IsModelStem(model_it->get_ref<const std::string &>())) {
        return Invalid("model must be the stem of chara/<model>.gltf (1-31 of a-z, 0-9, _), a native villager model or one this mod owns");
    }
    if (position_it == definition.end() || !position_it->is_object() || position_it->size() != 3 ||
        !position_it->contains("x") || !position_it->contains("y") || !position_it->contains("z")) {
        return Invalid("position must be {x, y, z} in world units");
    }
    ModNpc npc;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        const Json &component = (*position_it)[axis == 0 ? "x" : axis == 1 ? "y" : "z"];
        if (!component.is_number()) return Invalid("position components must be numbers");
        const double value = component.get<double>();
        if (!std::isfinite(value) || std::fabs(value) > 100000.0) return Invalid("position components must be finite and within 100000 units");
        npc.position[axis] = static_cast<float>(value);
    }
    if (const auto facing = definition.find("facing"); facing != definition.end()) {
        if (!facing->is_number() || !std::isfinite(facing->get<double>()) || std::fabs(facing->get<double>()) > 360.0) {
            return Invalid("facing must be an angle in degrees from -360 through 360");
        }
        npc.facing_degrees = static_cast<float>(facing->get<double>());
    }
    if (const auto talk = definition.find("talk_event"); talk != definition.end()) {
        if (!talk->is_number_integer() || talk->get<long long>() < 0 || talk->get<long long>() > 0xFFFF) {
            return Invalid("talk_event must be a native event number from 1 through 65535, or 0 for game.npc.talk");
        }
        npc.talk_event = talk->get<int>();
    }
    if (g_npcs.contains(qualified_id)) return Invalid("a villager with this stable id is already registered");
    if (g_npcs.size() >= kMaximumModVillagers) {
        return {ApiCallStatus::Failed, nullptr, "Chronicle Two holds up to 32 mod villagers in a session"};
    }
    npc.id = qualified_id;
    npc.owner = std::string(mod_id);
    npc.name = name;
    npc.model = model_it->get<std::string>();
    npc.map_no = map_it->get<int>();
    npc.chara_no = g_next_chara_no++;
    g_npcs.emplace(qualified_id, std::move(npc));
    return {ApiCallStatus::Ok, NpcJson(g_npcs.at(qualified_id)), {}};
}

Json ChronicleTwoNpcJson(std::string_view qualified_id) {
    const auto found = g_npcs.find(qualified_id);
    return found == g_npcs.end() ? Json(nullptr) : NpcJson(found->second);
}

Json ChronicleTwoNpcsJson() {
    Json result = Json::array();
    for (const auto &[id, npc] : g_npcs) result.push_back(NpcJson(npc));
    return result;
}

int ChronicleTwoLoadModVillagers(CScene *scene, int map_no, int texb, mgCMemory *stack, int loaded) {
    if (scene == nullptr || stack == nullptr) return 0;
    int placed = 0;
    for (auto &[id, npc] : g_npcs) {
        npc.slot = -1;
        npc.load_note.clear();
        if (npc.map_no != map_no) continue;
        if (loaded + placed >= SCENE_VILLAGER_SLOT_NUM) {
            npc.load_note = "no free villager slot in this map";
            continue;
        }
        const int slot_index = SCENE_VILLAGER_SLOT_TOP + loaded + placed;
        const std::string path = "chara/" + npc.model + ".gltf";
        const auto read_storage = scene->GetReadBufferBytes();
        auto *buffer = reinterpret_cast<unsigned int *>(read_storage.data());
        int file_size = 0;
        std::string mutable_path = path;
        if (LoadFile2(mutable_path.data(), read_storage, &file_size, LOAD_FILE_READ) == 0) {
            npc.load_note = "could not read " + path;
            continue;
        }
        const int chr_size = GetChrFileSize(buffer, file_size);
        if (stack->stGetRest() < chr_size / 16 + 1) {
            npc.load_note = "the villager memory stack is full";
            continue;
        }
        mgCTextureManager *tex_manager = &mgTexManager;
        tex_manager->DeleteBlock(texb + loaded + placed);
        char suffix[8];
        std::snprintf(suffix, sizeof(suffix), "%d", slot_index);
        dc2::text::Copy(tex_manager->name_suffix, dc2::text::View(suffix));
        char info[] = "info";
        const int slot = scene->LoadChara(slot_index, buffer, info, stack, stack, stack, texb + loaded + placed, 0);
        tex_manager->name_suffix[0] = 0;
        if (slot < 0 || scene->GetCharacter(slot) == nullptr) {
            npc.load_note = "the character pack did not load";
            continue;
        }
        scene->SetCharaNo(slot, npc.chara_no);
        auto *place = dc2::memory::NewObject<CVillagerPlaceInfo>(*stack);
        if (place == nullptr) {
            npc.load_note = "the villager memory stack is full";
            continue;
        }
        place->pos[0] = npc.position[0];
        place->pos[1] = npc.position[1];
        place->pos[2] = npc.position[2];
        place->pos[3] = npc.facing_degrees * std::numbers::pi_v<float> / 180.0f;
        place->map_no = map_no;
        if (scene->RegisterVillager(slot, npc.chara_no, place) == 0) {
            npc.load_note = "the villager manager has no free record";
            continue;
        }
        scene->SetActive(1, slot);
        npc.slot = slot;
        ++placed;
    }
    return placed;
}

bool ChronicleTwoModVillagerTalk(CScene *scene, CSceneEventData *event) {
    if (event == nullptr) return false;
    for (auto &[id, npc] : g_npcs) {
        if (npc.chara_no != event->chara_no) continue;
        if (npc.talk_event > 0 && scene != nullptr) {
            scene->RunEvent(npc.talk_event, event);
        } else {
            SharedRuntime().QueueEvent("game.npc.talk", Json{{"id", npc.id}, {"name", npc.name}, {"map_no", npc.map_no},
                                                             {"owner", npc.owner}, {"slot", event->chara_slot}});
        }
        return true;
    }
    return false;
}

void UnloadChronicleTwoNpcs(std::string_view owner) {
    std::erase_if(g_npcs, [owner](const auto &entry) { return entry.second.owner == owner; });
}

void ResetChronicleTwoNpcsForTests() {
    g_npcs.clear();
    g_next_chara_no = kModVillagerCharaBase;
}

} // namespace moonmod
