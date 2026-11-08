#include "moonmod/content_georama.hpp"

#include <array>
#include <cmath>
#include <map>

#include "editinfo.hpp"
#include "editmap.hpp"
#include "mainloop.hpp"
#include "mg_memory.hpp"
#include "moonmod/content_slots.hpp"
#include "savedata.hpp"

namespace moonmod {
namespace {

struct GeoramaExtras {
    std::string owner_mod;
    int town_id = -1;
    int stock = 0;
    std::string model_part;
    bool has_territory = false;
    float territory_radius = 0.0f;
    float territory_height = 0.0f;
    int definition_id = -1;
    std::array<bool, 5> stock_granted{};
    std::array<std::string, 5> model_notes{};
};

std::map<std::string, GeoramaExtras, std::less<>> g_extras;

bool IsPartName(std::string_view value) {
    if (value.empty() || value.size() > 31) return false;
    for (unsigned char c : value) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    }
    return true;
}

bool ReadFinite(const Json &object, const char *key, float minimum, float maximum, float &out, bool &present) {
    const auto found = object.find(key);
    present = found != object.end();
    if (!present) return true;
    if (!found->is_number()) return false;
    const double value = found->get<double>();
    if (!std::isfinite(value) || value < minimum || value > maximum) return false;
    out = static_cast<float>(value);
    return true;
}

} // namespace

bool ChronicleTwoGeoramaParseExtras(std::string_view mod_id, std::string_view qualified_id, const Json &definition,
                                    std::string &error) {
    GeoramaExtras extras;
    extras.owner_mod = std::string(mod_id);
    if (const auto found = definition.find("town_id"); found != definition.end()) {
        if (!found->is_number_integer() || found->get<long long>() < 0 || found->get<long long>() > 4) {
            error = "town_id must be a Chronicle Two town from 0 through 4";
            return false;
        }
        extras.town_id = found->get<int>();
    }
    if (const auto found = definition.find("stock"); found != definition.end()) {
        if (!found->is_number_integer() || found->get<long long>() < 1 || found->get<long long>() > 999) {
            error = "stock must be from 1 through 999 built parts";
            return false;
        }
        extras.stock = found->get<int>();
    }
    if (const auto found = definition.find("model"); found != definition.end()) {
        if (!found->is_object() || found->size() != 1 || !found->contains("part") || !(*found)["part"].is_string() ||
            !IsPartName((*found)["part"].get_ref<const std::string &>())) {
            error = found->is_object() && found->contains("scene")
                        ? "Chronicle Two Georama models come from the town's own scene: model = {part = <name of a part the "
                          "town's map loads>}; an owned scene file is unsupported"
                        : "model must be {part = <1-31 character name of a part of the town's scene>}";
            return false;
        }
        extras.model_part = (*found)["part"].get<std::string>();
    }
    if (const auto found = definition.find("territory"); found != definition.end()) {
        bool has_radius = false;
        bool has_height = false;
        if (!found->is_object() || found->empty() || found->size() > 2 ||
            !ReadFinite(*found, "radius", 1.0f, 10000.0f, extras.territory_radius, has_radius) ||
            !ReadFinite(*found, "height", 1.0f, 10000.0f, extras.territory_height, has_height) ||
            static_cast<std::size_t>(has_radius) + static_cast<std::size_t>(has_height) != found->size()) {
            error = "territory must be {radius = 1..10000, height = 1..10000} in world units";
            return false;
        }
        extras.has_territory = true;
    }
    g_extras[std::string(qualified_id)] = std::move(extras);
    return true;
}

std::size_t ChronicleTwoGeoramaExtraKeyCount(const Json &definition) {
    std::size_t count = 0;
    for (const char *key : {"town_id", "stock", "model", "territory"}) {
        if (definition.contains(key)) ++count;
    }
    return count;
}

void ChronicleTwoGeoramaForgetExtras(std::string_view qualified_id) {
    const auto found = g_extras.find(qualified_id);
    if (found != g_extras.end() && found->second.definition_id < 0) g_extras.erase(found);
}

bool ChronicleTwoGeoramaPieceAppliesToTown(std::string_view qualified_id, int town_id) {
    const auto found = g_extras.find(qualified_id);
    return found == g_extras.end() || found->second.town_id < 0 || found->second.town_id == town_id;
}

int ChronicleTwoGeoramaPersistentPartId(std::string_view qualified_id, CEditInfoMngr &manager, std::string &error) {
    int id = -1;
    const bool allocated = AllocateChronicleTwoContentSlot(
        ContentSlotKind::Georama, qualified_id, kModGeoramaPartIdTop, kModGeoramaPartIdBottom,
        [&manager](int candidate) { return manager.GetePartsInfoAtID(candidate) == nullptr; }, id, error);
    if (!allocated) return -1;
    if (manager.GetePartsInfoAtID(id) != nullptr) {
        error = "the piece's persisted definition id " + std::to_string(id) + " belongs to a native part of this town";
        return -1;
    }
    auto found = g_extras.find(qualified_id);
    if (found == g_extras.end()) found = g_extras.emplace(std::string(qualified_id), GeoramaExtras{}).first;
    found->second.definition_id = id;
    return id;
}

void ChronicleTwoGeoramaApplyExtras(std::string_view qualified_id, CEditPartsInfo &copy, CEditMap &map,
                                    mgCMemory &memory, int town_id) {
    const auto found = g_extras.find(qualified_id);
    if (found == g_extras.end()) return;
    GeoramaExtras &extras = found->second;
    extras.definition_id = copy.id;
    const std::size_t town = static_cast<std::size_t>(town_id >= 0 && town_id < 5 ? town_id : 0);
    if (!extras.model_part.empty()) {
        std::string name = extras.model_part;
        CMapParts *parts = map.GetParts(name.data());
        if (parts == nullptr) {
            extras.model_notes[town] = "the town's scene has no part named " + extras.model_part + "; the template's model is kept";
        } else if (char *copied = mgCopyString(name.data(), &memory); copied != nullptr) {
            copy.parts = parts;
            copy.parts_name = copied;
            extras.model_notes[town].clear();
        } else {
            extras.model_notes[town] = "the scene ran out of memory for the part name; the template's model is kept";
        }
    }
    if (extras.has_territory) {
        copy.territory_radius = extras.territory_radius;
        copy.territory_height = extras.territory_height;
    }
    if (extras.stock > 0 && !extras.stock_granted[town]) {
        if (CSaveData *save = GetSaveData(); save != nullptr && copy.id >= 0 && copy.id < SAVE_BUILD_PARTS_MAX &&
                                              save->GetBuildPartsNum(copy.id) <= 0 &&
                                              save->GetPlaceEditPartsNum(copy.id) == 0) {
            // A save that holds none built and none placed has never had the piece: it gets the stock once.
            save->AddBuildPartsNum(copy.id, extras.stock);
            extras.stock_granted[town] = true;
        }
    }
}

Json ChronicleTwoGeoramaExtrasJson(std::string_view qualified_id) {
    Json result = Json::object();
    const auto found = g_extras.find(qualified_id);
    if (found == g_extras.end()) return result;
    const GeoramaExtras &extras = found->second;
    if (extras.town_id >= 0) result["town_id"] = extras.town_id;
    if (extras.stock > 0) result["stock"] = extras.stock;
    if (!extras.model_part.empty()) result["model"] = Json{{"part", extras.model_part}};
    if (extras.has_territory) result["territory"] = Json{{"radius", extras.territory_radius}, {"height", extras.territory_height}};
    if (extras.definition_id >= 0) result["definition_id"] = extras.definition_id;
    Json notes = Json::object();
    for (std::size_t town = 0; town < extras.model_notes.size(); ++town) {
        if (!extras.model_notes[town].empty()) notes[std::to_string(town)] = extras.model_notes[town];
    }
    if (!notes.empty()) result["model_notes"] = std::move(notes);
    return result;
}

void ResetChronicleTwoContentGeoramaForTests() { g_extras.clear(); }

} // namespace moonmod
