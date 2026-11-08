// Co-op's adapter operations (moonmod/coop.hpp): net.*, session.*, the save split and the host's guests, the other
// players' heroes, the host's monsters, shared doors, floors, the choice panel and the title modes. Dispatched before
// the Chronicle Two adapter's own operations (chronicle_two_adapter.cpp calls Call first).

#include "moonmod/coop.hpp"
#include "moonmod/chronicle_two_adapter.hpp"
#include "moonmod/coop_save.hpp"
#include "moonmod/net_session.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <string>

#include "dng_main.hpp"
#include "gamedata.hpp"
#include "mainloop.hpp"
#include "mapselect.hpp"
#include "character.hpp"
#include "monster.hpp"
#include "moonmod/mapselect_moonmod.hpp"
#include "savedata.hpp"
#include "scenesnd.hpp"
#include "userdata.hpp"
#include "editloop.hpp"

namespace moonmod::coop {

namespace {

ApiCallResult Invalid(std::string message) { return {ApiCallStatus::InvalidArgument, nullptr, std::move(message)}; }
ApiCallResult Failed(std::string message) { return {ApiCallStatus::Failed, nullptr, std::move(message)}; }
ApiCallResult Ok(Json value) { return {ApiCallStatus::Ok, std::move(value), {}}; }

bool ReadBoundedInt(const Json &object, const char *key, int minimum, int maximum, int &out) {
    const auto field = object.find(key);
    if (field == object.end() || !field->is_number_integer()) return false;
    const std::int64_t value = field->get<std::int64_t>();
    if (value < minimum || value > maximum) return false;
    out = static_cast<int>(value);
    return true;
}

bool ReadNumber(const Json &object, const char *key, float &out, bool required) {
    const auto field = object.find(key);
    if (field == object.end()) return !required;
    if (!field->is_number()) return false;
    out = field->get<float>();
    return std::isfinite(out) && std::fabs(out) < 1.0e6f;
}

bool OnlyFields(const Json &object, std::initializer_list<std::string_view> allowed, std::string &unknown) {
    for (auto field = object.begin(); field != object.end(); ++field) {
        if (std::ranges::find(allowed, field.key()) == allowed.end()) {
            unknown = field.key();
            return false;
        }
    }
    return true;
}

const char *CharacterName(int character) { return character == USER_CHARA_MONICA ? "monica" : "max"; }

// Mods' own things that travel with a player's (save.player_extra_set), by mod id, after the player's text.
constexpr std::string_view kPlayerExtrasTag = "|DCX2";
constexpr std::size_t      kPlayerExtraMax = 32 * 1024;
std::map<std::string, Json, std::less<>> g_player_extras;

std::string ExportPlayerText(const CSaveData &save) {
    std::string text = ExportPlayer(save);
    if (g_player_extras.empty()) return text;
    Json mods = Json::object();
    for (const auto &[id, data] : g_player_extras) mods[id] = data;
    return text + std::string(kPlayerExtrasTag) + Json{{"mods", std::move(mods)}}.dump();
}

bool ImportPlayerText(std::string_view text, CSaveData &save, std::string &error) {
    const std::size_t split = text.find(kPlayerExtrasTag);
    Json extras;
    if (split != std::string_view::npos) {
        extras = Json::parse(text.substr(split + kPlayerExtrasTag.size()), nullptr, false);
        if (extras.is_discarded() || !extras.is_object()) {
            error = "the player's mod things are damaged";
            return false;
        }
    }
    if (!ImportPlayer(text.substr(0, split), save, error)) return false;
    g_player_extras.clear();
    if (extras.contains("mods") && extras["mods"].is_object()) {
        for (auto mod = extras["mods"].begin(); mod != extras["mods"].end(); ++mod) {
            if (mod.value().dump().size() <= kPlayerExtraMax) g_player_extras[mod.key()] = mod.value();
        }
    }
    return true;
}

// ---- net.* -------------------------------------------------------------------------------------------------------------

Json NetStatusJson() {
    net::Session &session = net::Shared();
    Json          peers = Json::array();
    for (const net::Peer &peer : session.peers()) peers.push_back({{"id", peer.id}, {"name", peer.name}});
    Json out = {{"state", std::string(net::StateName(session.state()))}, {"available", session.available()}, {"peers", std::move(peers)}};
    if (session.id() >= 0) out["id"] = session.id();
    if (!session.code().empty()) out["code"] = session.code();
    if (!session.owner().empty()) out["owner"] = session.owner();
    return out;
}

ApiCallResult NetCall(std::string_view mod_id, const ApiOperationSpec &operation, const Json &args) {
    net::Session &session = net::Shared();
    const bool    no_args = args.is_array() && (args.empty() || (args.size() == 1 && args[0].is_object() && args[0].empty()));
    const Json    empty = Json::object();
    const Json   &request = args.is_array() && args.size() == 1 && args[0].is_object() ? args[0] : empty;
    if (!args.is_array() || args.size() > 1 || (args.size() == 1 && !args[0].is_object())) return Invalid("expected one table, or none");
    const auto text_of = [&request](const char *key) {
        return request.contains(key) && request[key].is_string() ? request[key].get<std::string>() : std::string();
    };
    if (operation.path == "net.status") {
        if (!no_args) return Invalid("net.status takes no arguments");
        session.Pump();
        return Ok(NetStatusJson());
    }
    const bool mine = session.owner() == mod_id;
    if (!session.owner().empty() && !mine) return Failed("another mod's online session is open (" + session.owner() + ")");
    if (operation.path == "net.host" || operation.path == "net.join") {
        std::string unknown;
        if (!OnlyFields(request, {"name", "relay", "version", "code"}, unknown)) return Invalid("unknown field: " + unknown);
        const bool  join = operation.path == "net.join";
        std::string code = text_of("code");
        if (join) {
            std::ranges::transform(code, code.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            if (code.size() != 5 || !std::ranges::all_of(code, [](unsigned char c) { return std::isalnum(c) != 0; })) {
                return Invalid("code must be the room's five letters and digits");
            }
        } else if (request.contains("code")) {
            return Invalid("net.host takes no code: the relay gives the room one");
        }
        if (!request.contains("version") || !request["version"].is_number_integer() || request["version"].get<int>() < 1 ||
            request["version"].get<int>() > 9999) {
            return Invalid("version must be the mod's message protocol number, 1-9999: players on other numbers can't join");
        }
        const std::string name = text_of("name");
        if (name.size() > 24 || !net::ValidUtf8(name)) return Invalid("name must be text of at most 24 bytes");
        const std::string relay = net::RelayAddress(text_of("relay"));
        std::string       error;
        if (!session.Start(mod_id, relay, join ? code : std::string(), name, "chronicle-two", request["version"].get<int>(), error)) {
            return session.available() ? Failed(error) : ApiCallResult{ApiCallStatus::Unsupported, {}, error};
        }
        return Ok(NetStatusJson());
    }
    if (operation.path == "net.leave") {
        if (!no_args) return Invalid("net.leave takes no arguments");
        const bool was_open = session.state() != net::State::Idle;
        if (mine) session.Leave();
        return Ok({{"left", was_open && mine}});
    }
    if (operation.path == "net.send") {
        std::string unknown;
        if (!OnlyFields(request, {"to", "data"}, unknown)) return Invalid("unknown field: " + unknown);
        if (!request.contains("data") || !request["data"].is_string()) return Invalid("data must be text");
        int to = -1;
        if (request.contains("to") && request["to"].is_string() && request["to"].get<std::string>() == "all") to = net::kBroadcast;
        else if (request.contains("to") && request["to"].is_number_integer()) to = request["to"].get<int>();
        else return Invalid("to must be a player's id or \"all\"");
        if (!mine) return Failed("not in an online session");
        std::string error;
        if (!session.Send(to, request["data"].get_ref<const std::string &>(), error)) return Failed(error);
        return Ok({{"sent", true}});
    }
    if (operation.path == "net.receive") {
        std::string unknown;
        if (!OnlyFields(request, {"max"}, unknown)) return Invalid("unknown field: " + unknown);
        std::size_t max = 256;
        if (request.contains("max")) {
            if (!request["max"].is_number_integer() || request["max"].get<int>() < 1 || request["max"].get<int>() > 1024) return Invalid("max must be 1-1024");
            max = static_cast<std::size_t>(request["max"].get<int>());
        }
        Json events = Json::array();
        if (mine) {
            for (net::Event &event : session.Receive(max)) {
                Json entry = {{"kind", event.kind}};
                if (event.peer >= 0) entry["peer"] = event.peer;
                if (event.kind == "message") entry["data"] = std::move(event.data);
                if (event.kind == "connected") entry["code"] = std::move(event.data);
                if (event.kind == "closed") entry["reason"] = std::move(event.data);
                if (!event.name.empty()) entry["name"] = std::move(event.name);
                events.push_back(std::move(entry));
            }
        }
        return Ok({{"events", std::move(events)}});
    }
    return Invalid("unknown net operation");
}

// ---- session.* -----------------------------------------------------------------------------------------------------

ApiCallResult StartSession(std::string_view mod_id, const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) return Invalid("expected {visit = {world, player?, map}}");
    const Json &request = args[0];
    if (!request.contains("visit") || request.size() != 1) {
        return {ApiCallStatus::Unsupported, nullptr, "Chronicle Two starts only co-op visits: session.start {visit = {world, player?, map}}"};
    }
    const Json &visit = request["visit"];
    if (!visit.is_object()) return Invalid("visit must be {world, player?, map}");
    std::string unknown;
    if (!OnlyFields(visit, {"world", "player", "map"}, unknown)) return Invalid("visit accepts only world, player and map");
    if (GetNowLoopNo() != LOOP_TITLE) return Failed("a visit starts from the title screen");
    const TitleMode *open = OpenTitleMode();
    if (open == nullptr || open->owner != mod_id) return Failed("start the visit from this mod's open title mode page");
    if (VisitRequestPending()) return Failed("a visit is already starting");
    if (!visit.contains("world") || !visit["world"].is_string() || !visit["world"].get_ref<const std::string &>().starts_with("DCW2")) {
        return Invalid("world must be the host's save.world_export text");
    }
    if (visit.contains("player") && !visit["player"].is_null() &&
        (!visit["player"].is_string() || !visit["player"].get_ref<const std::string &>().starts_with("DCP2"))) {
        return Invalid("player must be save.player_export text, or left out for a new account");
    }
    int map = 0;
    if (!visit.contains("map") || !ReadBoundedInt(visit, "map", 0, SAVE_EDIT_DATA_MAX - 1, map)) {
        return Invalid("map must be the town (area) to start in, 0-" + std::to_string(SAVE_EDIT_DATA_MAX - 1));
    }
    VisitRequest queued;
    queued.owner = std::string(mod_id);
    queued.mode_id = open->id;
    queued.map = map;
    queued.world = visit["world"].get<std::string>();
    if (visit.contains("player") && visit["player"].is_string()) queued.player = visit["player"].get<std::string>();
    const bool new_account = queued.player.empty();
    QueueVisit(std::move(queued));
    return Ok({{"queued", true}, {"visit", true}, {"map", map}, {"new_account", new_account}});
}

ApiCallResult FinishSession(std::string_view mod_id, const Json &args) {
    if (!args.is_array() || args.size() > 1 || (args.size() == 1 && (!args[0].is_object() || !args[0].empty()))) return Invalid("session.finish takes no arguments");
    const std::string *owner = VisitOwner();
    if (owner == nullptr) return Failed("no visit is running");
    if (*owner != mod_id) return Failed("only the mod that started the visit can end it");
    const int loop = GetNowLoopNo();
    if (loop != LOOP_DUNGEON && loop != LOOP_EDIT) return Failed("a visit ends from a town or a dungeon");
    CScene *scene = GetMainScene();
    if (scene == nullptr) return Failed("the active scene is unavailable");
    EndVisit("ended");
    INIT_LOOP_ARG title{};
    title.map_no = -1;
    title.floor_no = -1;
    title.event_no = -1;
    NextLoop(LOOP_TITLE, title);
    scene->exit_flag = true;
    return Ok({{"queued", true}});
}

// ---- Places ------------------------------------------------------------------------------------------------------------

ApiCallResult EnterMap(const Json &args) {
    int target = -1;
    const int count = GetMapNameCount();
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || !ReadBoundedInt(args[0], "map_no", 0, std::max(0, count - 1), target)) {
        return Invalid("expected {map_no = a town or house's map number (world.get().map_no)}");
    }
    if (count <= 0) return Failed("the native map catalog is not loaded");
    if (GetMapType(target) < 0) return Failed("no map has that number");
    const int loop = GetNowLoopNo();
    if (loop != LOOP_EDIT && loop != LOOP_DUNGEON) return Failed("busy: the player isn't in a town or on a floor");
    if (ChoiceIsOpen()) return Failed("busy: a panel is open");
    CScene *scene = GetMainScene();
    if (scene == nullptr) return Failed("the active scene is unavailable");
    if (loop == LOOP_EDIT) {
        const INIT_LOOP_ARG *current = GetNowInitArg();
        if (current != nullptr && current->map_no == target) return Ok({{"map_no", target}, {"queued", false}});
    }
    INIT_LOOP_ARG destination{};
    destination.map_no = target;
    destination.floor_no = -1;
    destination.event_no = 100;
    NextLoop(LOOP_EDIT, destination);
    scene->exit_flag = true;
    return Ok({{"map_no", target}, {"queued", true}});
}

ApiCallResult EnterFloor(const Json &args) {
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) return Invalid("expected {dungeon_id = optional 0-6, floor_id = 0-127}");
    const Json &request = args[0];
    std::string unknown;
    if (!OnlyFields(request, {"dungeon_id", "floor_id"}, unknown)) return Invalid("unknown field: " + unknown);
    const int loop = GetNowLoopNo();
    int       dungeon = loop == LOOP_DUNGEON ? DngStatus.dungeon_no : -1;
    int       floor = -1;
    if (request.contains("dungeon_id") && !ReadBoundedInt(request, "dungeon_id", 0, SAVE_DUNGEON_NUM - 1, dungeon)) {
        return Invalid("dungeon_id must be 0-" + std::to_string(SAVE_DUNGEON_NUM - 1));
    }
    if (!ReadBoundedInt(request, "floor_id", 0, 127, floor)) return Invalid("floor_id must be 0-127");
    if (dungeon < 0) return Invalid("from a town, dungeon_id says which dungeon");
    if (loop != LOOP_EDIT && loop != LOOP_DUNGEON) return Failed("busy: the player isn't in a town or on a floor");
    CScene *scene = GetMainScene();
    if (scene == nullptr) return Failed("the active scene is unavailable");
    INIT_LOOP_ARG destination{};
    destination.map_no = dungeon;
    destination.floor_no = floor;
    destination.event_no = 0; // the floor start event
    NextLoop(LOOP_DUNGEON, destination);
    scene->exit_flag = true;
    return Ok({{"dungeon_id", dungeon}, {"floor_id", floor}, {"queued", true}});
}

ApiCallResult StrikePlayer(const Json &args) {
    static constexpr const char *kUsage = "expected {x, y, z, radius = 1-200, damage = 1-9999, kind = \"light\" or \"heavy\"}";
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) return Invalid(kUsage);
    const Json &request = args[0];
    std::string unknown;
    if (!OnlyFields(request, {"x", "y", "z", "radius", "damage", "kind"}, unknown)) return Invalid("unknown field: " + unknown);
    float at[3] = {0.0f, 0.0f, 0.0f}, radius = 12.0f;
    int   damage = 0;
    if (!ReadNumber(request, "x", at[0], true) || !ReadNumber(request, "y", at[1], true) || !ReadNumber(request, "z", at[2], true) ||
        !ReadNumber(request, "radius", radius, false) || radius < 1.0f || radius > 200.0f || !ReadBoundedInt(request, "damage", 1, 9999, damage)) {
        return Invalid(kUsage);
    }
    std::string kind = "light";
    if (request.contains("kind")) {
        if (!request["kind"].is_string()) return Invalid(kUsage);
        kind = request["kind"].get<std::string>();
        if (kind != "light" && kind != "heavy") return Invalid(kUsage);
    }
    if (GetNowLoopNo() != LOOP_DUNGEON || MainChara__2 == nullptr || GetBattleCharaInfo() == nullptr) return Failed("the player isn't on a dungeon floor");
    if (DngStatus.mode != DNG_STATUS_FIELD) return Ok({{"hit", false}, {"damage", 0}, {"reason", "busy"}});
    CBattleCharaInfo *info = GetBattleCharaInfo();
    float             player[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    MainChara__2->GetPosition(player);
    const float dx = player[0] - at[0], dz = player[2] - at[2];
    if (std::sqrt(dx * dx + dz * dz) > radius + 10.0f || std::fabs(player[1] - at[1]) > 40.0f) return Ok({{"hit", false}, {"damage", 0}, {"reason", "away"}});
    if (MainChara__2->muteki_time > 0 || info->GetNowHp_i() <= 0) return Ok({{"hit", false}, {"damage", 0}, {"reason", "guarded"}});
    const int dealt = std::min(damage, info->GetNowHp_i());
    info->AddHp_Point(-static_cast<float>(dealt), 0.0f);
    MainChara__2->damage_req = kind == "heavy" ? ACTION_DAMAGE_REQ_LARGE : ACTION_DAMAGE_REQ_SMALL;
    MainChara__2->muteki_time = 45; // about the game's own grace after a blow
    return Ok({{"hit", true}, {"damage", dealt}, {"hp", info->GetNowHp_i()}, {"kind", kind}});
}

// ---- Things ---------------------------------------------------------------------------------------------------------------

ApiCallResult ItemsOwned(const Json &args) {
    if (!args.is_array() || !args.empty()) return Invalid("items.owned takes no arguments");
    CUserDataManager *user = GetUserDataMan();
    if (user == nullptr) return Failed("the game has no active save inventory");
    std::vector<int> seen;
    Json             items = Json::array();
    for (const CGameDataUsed &place : user->used_data) {
        const int item_no = place.item_no;
        if (item_no <= 0 || std::ranges::find(seen, item_no) != seen.end()) continue;
        seen.push_back(item_no);
        const CDataCommon *common = GameItemDataManage.GetCommonData(item_no);
        const char        *name = common != nullptr && common->name != nullptr ? common->name : "";
        items.push_back({{"id", "chronicle-two:item:" + std::to_string(item_no)}, {"item_no", item_no}, {"name", name},
                         {"count", std::max(1, user->GetNumSameItem(item_no))}});
    }
    return Ok({{"items", std::move(items)}});
}

// ---- Towns ---------------------------------------------------------------------------------------------------------------

ApiCallResult TownExportImport(const ApiOperationSpec &operation, const Json &args) {
    const bool importing = operation.path == "georama.town_import";
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) {
        return Invalid(importing ? "expected {town_id = 0-4, data, base = optional}" : "expected {town_id = 0-4}");
    }
    int town = 0;
    if (!ReadBoundedInt(args[0], "town_id", 0, SAVE_EDIT_DATA_MAX - 1, town)) return Invalid("town_id must be 0-" + std::to_string(SAVE_EDIT_DATA_MAX - 1));
    CSaveData *save = GetSaveData();
    if (save == nullptr) return Failed("the game has no save data yet");
    if (!importing) return Ok({{"town_id", town}, {"data", ExportTown(*save, town)}});
    if (!args[0].contains("data") || !args[0]["data"].is_string()) return Invalid("data must be georama.town_export text");
    const bool here = GetNowLoopNo() == LOOP_EDIT && save->area_no == town;
    if (here && IsEditMode() != 0) return Failed("busy: the player is editing that town; try again in a moment");
    if (here && ChoiceIsOpen()) return Failed("busy: a panel is open");
    const std::string &data = args[0]["data"].get_ref<const std::string &>();
    const bool         merging = args[0].contains("base") && !args[0]["base"].is_null();
    if (merging && !args[0]["base"].is_string()) return Invalid("base must be georama.town_export text");
    std::string error;
    if (merging ? !MergeTown(args[0]["base"].get_ref<const std::string &>(), data, town, *save, error) : !ImportTown(data, town, *save, error)) {
        return Invalid(error);
    }
    // The town on screen is the old one: walk into it again, so the player sees what came.
    bool reloaded = false;
    if (here) {
        if (CScene *scene = GetMainScene(); scene != nullptr) {
            INIT_LOOP_ARG again{};
            again.map_no = save->map_no;
            again.floor_no = -1;
            again.event_no = 100;
            NextLoop(LOOP_EDIT, again);
            scene->exit_flag = true;
            reloaded = true;
        }
    }
    return Ok({{"town_id", town}, {"imported", true}, {"merged", merging}, {"reloaded", reloaded}});
}

// ---- Avatars and monsters ---------------------------------------------------------------------------------------------------

ApiCallResult AvatarSetCall(const Json &args) {
    static constexpr const char *kUsage =
        "expected {slot = 1-3, character = \"max\" or \"monica\", weapon = item_no or nil, x, y, z, heading, motion, tint = {r, g, b} or nil, name = text or nil}";
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) return Invalid(kUsage);
    const Json &request = args[0];
    std::string unknown;
    if (!OnlyFields(request, {"slot", "character", "weapon", "x", "y", "z", "heading", "motion", "tint", "name"}, unknown)) {
        return Invalid("unknown field: " + unknown);
    }
    int slot = 0;
    if (!ReadBoundedInt(request, "slot", 1, kAvatars, slot)) return Invalid(kUsage);
    AvatarState state;
    if (!request.contains("character") || !request["character"].is_string()) return Invalid(kUsage);
    const std::string character = request["character"].get<std::string>();
    if (character == "max") state.character = USER_CHARA_MAX;
    else if (character == "monica") state.character = USER_CHARA_MONICA;
    else return Invalid("character must be \"max\" or \"monica\" (the ridepod and monster forms aren't shown)");
    if (request.contains("weapon") && !request["weapon"].is_null() && !ReadBoundedInt(request, "weapon", 1, ITEM_NO_MAX - 1, state.weapon)) {
        return Invalid("weapon must be an item_no");
    }
    if (!ReadNumber(request, "x", state.x, false) || !ReadNumber(request, "y", state.y, false) || !ReadNumber(request, "z", state.z, false) ||
        !ReadNumber(request, "heading", state.heading, false)) {
        return Invalid(kUsage);
    }
    if (request.contains("motion") && !ReadBoundedInt(request, "motion", 0, 1023, state.motion)) return Invalid("motion must be 0-1023");
    if (request.contains("tint") && !request["tint"].is_null()) {
        const Json &tint = request["tint"];
        if (!tint.is_array() || tint.size() != 3) return Invalid("tint must be {r, g, b}, each 0-128");
        for (int c = 0; c < 3; ++c) {
            if (!tint[static_cast<std::size_t>(c)].is_number() || tint[static_cast<std::size_t>(c)].get<float>() < 0.0f ||
                tint[static_cast<std::size_t>(c)].get<float>() > 128.0f) {
                return Invalid("tint must be {r, g, b}, each 0-128");
            }
            state.tint[c] = tint[static_cast<std::size_t>(c)].get<float>();
        }
    }
    if (request.contains("name") && !request["name"].is_null()) {
        if (!request["name"].is_string() || request["name"].get_ref<const std::string &>().size() > 24) return Invalid("name must be text of 24 bytes at most");
        state.name = request["name"].get<std::string>();
    }
    if (!AvatarSet(slot - 1, state)) return Failed("that character can't be shown");
    return Ok({{"slot", slot}, {"shown", AvatarShown(slot - 1)}});
}

ApiCallResult MirrorGet(const Json &args) {
    if (!args.is_array() || !args.empty()) return Invalid("expected no arguments");
    Json monsters = Json::array();
    if (GetNowLoopNo() == LOOP_DUNGEON && ActiveMonster != nullptr) {
        for (int slot = 0; slot < MONSTER_ACTIVE_MAX; ++slot) {
            CActiveMonster *m = ActiveMonster->GetActiveMonster(slot);
            if (m == nullptr || m->state == ACTIVE_MONSTER_NONE) continue;
            float at[4] = {0.0f, 0.0f, 0.0f, 1.0f}, rotation[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            m->GetPosition(at);
            m->GetRotation(rotation);
            const auto round = [](float v) { return std::round(v * 10.0f) / 10.0f; };
            monsters.push_back({{"slot", slot}, {"kind", m->monster_id}, {"state", m->state}, {"hp", m->life}, {"max_hp", m->max_life},
                                {"x", round(at[0])}, {"y", round(at[1])}, {"z", round(at[2])},
                                {"heading", std::round(rotation[1] * 1000.0f) / 1000.0f}, {"motion", MotionNumberOf(*m)}});
        }
    }
    Json hits = Json::array();
    for (const AvatarHit &hit : TakeAvatarHits()) {
        hits.push_back({{"avatar", hit.avatar}, {"slot", hit.monster}, {"damage", hit.damage}, {"kind", hit.kind}, {"flags", hit.flags}});
    }
    return Ok({{"monsters", std::move(monsters)}, {"hits", std::move(hits)}});
}

ApiCallResult PuppetSetCall(const Json &args) {
    static constexpr const char *kUsage = "expected {monsters = {{slot, kind, state, hp, max_hp, x, y, z, heading, motion}, ...}}";
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || !args[0].contains("monsters") ||
        !(args[0]["monsters"].is_array() || (args[0]["monsters"].is_object() && args[0]["monsters"].empty()))) {
        return Invalid(kUsage);
    }
    if (GetNowLoopNo() != LOOP_DUNGEON || ActiveMonster == nullptr) return Failed("not on a dungeon floor");
    std::vector<bool> named(static_cast<std::size_t>(MONSTER_ACTIVE_MAX), false);
    Json              followed = Json::array(), refused = Json::array();
    for (const Json &entry : args[0]["monsters"]) {
        int    slot = 0;
        Puppet puppet;
        if (!entry.is_object() || !ReadBoundedInt(entry, "slot", 0, MONSTER_ACTIVE_MAX - 1, slot) || !ReadBoundedInt(entry, "kind", -1, 32767, puppet.kind) ||
            !ReadBoundedInt(entry, "state", -1, 3, puppet.state) || !ReadBoundedInt(entry, "hp", -1000000, 1000000, puppet.hp)) {
            return Invalid(kUsage);
        }
        if (entry.contains("max_hp") && !ReadBoundedInt(entry, "max_hp", 0, 1000000, puppet.max_hp)) return Invalid(kUsage);
        if (entry.contains("motion") && !ReadBoundedInt(entry, "motion", -1, 1023, puppet.motion)) return Invalid(kUsage);
        if (!ReadNumber(entry, "x", puppet.x, false) || !ReadNumber(entry, "y", puppet.y, false) || !ReadNumber(entry, "z", puppet.z, false) ||
            !ReadNumber(entry, "heading", puppet.heading, false)) {
            return Invalid(kUsage);
        }
        named[static_cast<std::size_t>(slot)] = true;
        (PuppetSet(slot, puppet) ? followed : refused).push_back(slot);
    }
    // A slot the host no longer lists is gone there.
    for (int slot = 0; slot < MONSTER_ACTIVE_MAX; ++slot) {
        if (!named[static_cast<std::size_t>(slot)] && IsPuppet(slot)) PuppetSet(slot, Puppet{});
    }
    Json hits = Json::array();
    for (const PuppetHit &hit : TakePuppetHits()) hits.push_back({{"slot", hit.slot}, {"damage", hit.damage}});
    return Ok({{"followed", std::move(followed)}, {"refused", std::move(refused)}, {"hits", std::move(hits)}});
}

ApiCallResult MirrorHit(const Json &args) {
    int slot = 0, damage = 0;
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || !ReadBoundedInt(args[0], "slot", 0, MONSTER_ACTIVE_MAX - 1, slot) ||
        !ReadBoundedInt(args[0], "damage", 1, 1000000, damage)) {
        return Invalid("expected {slot, damage}");
    }
    if (GetNowLoopNo() != LOOP_DUNGEON || ActiveMonster == nullptr) return Failed("not on a dungeon floor");
    CActiveMonster *monster = ActiveMonster->GetActiveMonster(slot);
    if (monster == nullptr || monster->state != ACTIVE_MONSTER_LIVE || monster->life <= 0) return Ok({{"slot", slot}, {"damage", 0}, {"killed", false}});
    // Another player's blow, as their game measured it: the life goes, and a kill dies here.
    const int dealt = std::min(damage, monster->life);
    monster->life -= dealt;
    if (monster->life == 0) {
        monster->damage_req = ACTION_DAMAGE_REQ_DEAD;
        ChronicleTwoEnemyDefeated(monster, "coop");
    } else {
        monster->req_prog = MONSTER_PROG_DAMAGE;
    }
    return Ok({{"slot", slot}, {"damage", dealt}, {"killed", monster->life == 0}});
}

// ---- Doors ---------------------------------------------------------------------------------------------------------------

ApiCallResult SharedEventsGet(const Json &args) {
    if (!args.is_array() || !args.empty()) return Invalid("expected no arguments");
    Json events = Json::array();
    for (const SharedEvent &e : TakeSharedEvents()) {
        const auto round = [](float v) { return std::round(v * 100.0f) / 100.0f; };
        events.push_back({{"script", e.script}, {"mode", e.mode}, {"ext", e.ext},
                          {"pos", {round(e.pos[0]), round(e.pos[1]), round(e.pos[2])}},
                          {"dir", {round(e.dir[0]), round(e.dir[1]), round(e.dir[2])}}});
    }
    return Ok({{"events", std::move(events)}});
}

ApiCallResult SharedEventRun(const Json &args) {
    static constexpr const char *kUsage = "expected {script, mode, ext, pos = {x, y, z}, dir = {x, y, z}} from dungeons.shared_events_get";
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) return Invalid(kUsage);
    const Json &request = args[0];
    SharedEvent e;
    if (!ReadBoundedInt(request, "script", 0, 9999, e.script) || !ReadBoundedInt(request, "mode", 0, 2, e.mode) || !ReadBoundedInt(request, "ext", 0, 1, e.ext)) {
        return Invalid(kUsage);
    }
    for (const char *key : {"pos", "dir"}) {
        if (!request.contains(key) || !request[key].is_array() || request[key].size() != 3) return Invalid(kUsage);
        float *out = std::string_view(key) == "pos" ? e.pos : e.dir;
        for (std::size_t i = 0; i < 3; ++i) {
            if (!request[key][i].is_number()) return Invalid(kUsage);
            out[i] = request[key][i].get<float>();
            if (!std::isfinite(out[i]) || std::fabs(out[i]) > 1.0e6f) return Invalid(kUsage);
        }
    }
    if (GetNowLoopNo() != LOOP_DUNGEON) return Failed("not on a dungeon floor");
    if (!RunSharedEvent(e)) return Failed("busy: another player's door is already waiting to open here");
    return Ok({{"queued", true}});
}

// ---- Saves and accounts -------------------------------------------------------------------------------------------------

ApiCallResult AccountCall(const ApiOperationSpec &operation, const Json &args) {
    if (VisitActive()) return Failed("a visit keeps no guests; the host's game does");
    const Json  empty = Json::object();
    const Json &request = args.is_array() && args.size() == 1 && args[0].is_object() ? args[0] : empty;
    if (!args.is_array() || args.size() > 1 || (args.size() == 1 && !args[0].is_object())) return Invalid("expected one table, or none");
    AccountsEnsureLoaded();
    const std::string id = request.contains("id") && request["id"].is_string() ? request["id"].get<std::string>() : std::string();
    if (operation.path == "save.account_list") {
        Json list = Json::array();
        for (const Account *account : Accounts()) list.push_back({{"id", account->id}, {"name", account->name}});
        return Ok({{"accounts", std::move(list)}});
    }
    if (!ValidAccountId(id)) return Invalid("id must be 8-64 of a-z, 0-9 and -");
    if (operation.path == "save.account_get") {
        const Account *account = AccountGet(id);
        if (account == nullptr) return Ok(Json::object());
        return Ok({{"id", account->id}, {"name", account->name}, {"data", account->player}});
    }
    if (operation.path == "save.account_remove") {
        const bool removed = AccountRemove(id);
        if (removed) AccountsStore();
        return Ok({{"removed", removed}});
    }
    if (operation.path == "save.account_set") {
        if (!request.contains("data") || !request["data"].is_string()) return Invalid("data must be save.player_export text");
        std::string error;
        if (!AccountSet({id, request.value("name", std::string()), request["data"].get<std::string>()}, error)) return Invalid(error);
        return Ok({{"kept", true}, {"written", AccountsStore()}});
    }
    return Invalid("unknown save.account operation");
}

ApiCallResult ChoiceOpenCall(std::string_view mod_id, const Json &args) {
    static constexpr const char *kUsage = "expected {id, title, subtitle?, cancel?, town?, options = {{title, subtitle?}, ...} (1-6)}";
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) return Invalid(kUsage);
    const Json &request = args[0];
    std::string unknown;
    if (!OnlyFields(request, {"id", "title", "subtitle", "cancel", "town", "options"}, unknown)) return Invalid("ui.choice_open received an unsupported field: " + unknown);
    if (!request.contains("id") || !request["id"].is_string() || request["id"].get_ref<const std::string &>().empty() ||
        request["id"].get_ref<const std::string &>().size() > 64) {
        return Invalid("id must be 1-64 bytes of text");
    }
    if (!request.contains("title") || !request["title"].is_string() || request["title"].get_ref<const std::string &>().size() > 64) return Invalid("title must be at most 64 bytes of text");
    if (!request.contains("options") || !request["options"].is_array() || request["options"].empty() || request["options"].size() > 6) return Invalid(kUsage);
    Choice choice;
    choice.owner = std::string(mod_id);
    choice.id = std::string(mod_id) + ":" + request["id"].get<std::string>();
    choice.title = request["title"].get<std::string>();
    if (request.contains("subtitle") && request["subtitle"].is_string()) choice.subtitle = request["subtitle"].get<std::string>().substr(0, 96);
    if (request.contains("cancel")) {
        if (!request["cancel"].is_boolean()) return Invalid("cancel must be a boolean");
        choice.cancel = request["cancel"].get<bool>();
    }
    for (const Json &option : request["options"]) {
        if (!option.is_object() || !option.contains("title") || !option["title"].is_string() || option["title"].get_ref<const std::string &>().size() > 48) {
            return Invalid("each option needs a title of at most 48 bytes");
        }
        ChoiceOption out;
        out.title = option["title"].get<std::string>();
        if (option.contains("subtitle") && option["subtitle"].is_string()) out.subtitle = option["subtitle"].get<std::string>().substr(0, 64);
        choice.options.push_back(std::move(out));
    }
    const int loop = GetNowLoopNo();
    if (loop != LOOP_DUNGEON && loop != LOOP_EDIT && loop != LOOP_TITLE) return Failed("a panel opens over a dungeon floor, a town or the title");
    if (!ChoiceOpen(std::move(choice))) return Failed("a choice panel is already open");
    return Ok({{"id", std::string(mod_id) + ":" + request["id"].get<std::string>()}, {"open", true}});
}

} // namespace

int MotionNumberOf(CCharacter2 &chara) {
    const CHRINFO_KEY_SET *now = chara.now_key;
    if (now == nullptr) return -1;
    int number = 0;
    for (int set = 0; set < CHARA_MOTION_SET_MAX; ++set) {
        const CHRINFO_KEY_SET *key = chara.key_list[set];
        if (key == nullptr) continue;
        for (; key->name[0] != '\0'; ++key, ++number) {
            if (key == now) return number;
        }
    }
    return -1;
}

void ExtendPlayer(Json &player) {
    const int loop = GetNowLoopNo();
    CCharacter2 *hero = nullptr;
    if (loop == LOOP_DUNGEON && MainChara__2 != nullptr) {
        hero = MainChara__2;
    } else if (loop == LOOP_EDIT) {
        if (CScene *scene = GetMainScene(); scene != nullptr && scene->player_chara >= 0) hero = scene->GetCharacter(scene->player_chara);
    }
    if (hero == nullptr) return;
    float position[4] = {0.0f, 0.0f, 0.0f, 1.0f}, rotation[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    hero->GetPosition(position);
    hero->GetRotation(rotation);
    if (std::isfinite(position[0]) && std::isfinite(position[1]) && std::isfinite(position[2])) {
        player["position"] = {{"x", position[0]}, {"y", position[1]}, {"z", position[2]}};
    }
    if (std::isfinite(rotation[1])) player["heading"] = rotation[1]; // the way the hero faces, as the game turns it about Y
    player["motion"] = std::max(0, MotionNumberOf(*hero));            // the motion the hero plays (the avatars play it on other screens)
    if (CUserDataManager *user = GetUserDataMan(); user != nullptr) {
        const int character = user->GetActiveChrNo();
        if (character == USER_CHARA_MAX || character == USER_CHARA_MONICA) {
            if (loop == LOOP_EDIT) player["town_character"] = CharacterName(character);
            if (CHARA_DATA *data = user->GetCharaDataPtr(character); data != nullptr && !player.contains("weapon")) {
                const int item_no = data->equip[CHARA_EQUIP_MELEE].item_no;
                if (item_no > 0) player["weapon"] = {{"id", "chronicle-two:item:" + std::to_string(item_no)}, {"item_no", item_no}, {"slot", CHARA_EQUIP_MELEE}};
            }
        }
    }
}

void ExtendWorld(Json &world) {
    const int loop = GetNowLoopNo();
    CSaveData *save = GetSaveData();
    if (save == nullptr) return;
    world["map_no"] = save->map_no;
    if (loop == LOOP_EDIT) {
        world["town_map"] = save->area_no;
        world["editing"] = IsEditMode() != 0;
        world["walking"] = IsEditMode() == 0 && !ChoiceIsOpen();
    }
    if (loop == LOOP_DUNGEON) {
        if (const std::optional<int> seed = LastFloorSeed()) world["floor_seed"] = *seed;
    }
}

std::optional<ApiCallResult> Call(std::string_view mod_id, const ApiOperationSpec &operation, const Json &args) {
    const std::string_view path = operation.path;
    if (path.starts_with("net.")) return NetCall(mod_id, operation, args);
    if (path == "session.get") {
        if (!args.is_array() || !args.empty()) return Invalid("session.get takes no arguments");
        return Ok(SessionJson());
    }
    if (path == "session.start") return StartSession(mod_id, args);
    if (path == "session.finish") return FinishSession(mod_id, args);
    if (path == "save.world_export" || path == "save.player_export") {
        if (!args.is_array() || !args.empty()) return Invalid(std::string(path) + " takes no arguments");
        CSaveData *save = GetSaveData();
        if (save == nullptr) return Failed("the game has no save data yet");
        return Ok({{"data", path == "save.world_export" ? ExportWorld(*save) : ExportPlayerText(*save)}});
    }
    if (path == "save.world_import" || path == "save.player_import") {
        if (!args.is_array() || args.size() != 1 || !args[0].is_object() || args[0].size() != 1 || !args[0].contains("data") || !args[0]["data"].is_string()) {
            return Invalid("expected {data = the export's text}");
        }
        const std::string *owner = VisitOwner();
        if (owner == nullptr || *owner != mod_id) return Failed("only this mod's co-op visit imports");
        CSaveData *save = GetSaveData();
        if (save == nullptr) return Failed("the game has no save data yet");
        std::string        error;
        const std::string &data = args[0]["data"].get_ref<const std::string &>();
        const bool ok = path == "save.world_import" ? ImportWorld(data, *save, error) : ImportPlayerText(data, *save, error);
        if (!ok) return Invalid(error);
        return Ok({{"imported", true}});
    }
    if (path.starts_with("save.account")) return AccountCall(operation, args);
    if (path == "save.player_extra_get") {
        if (!args.is_array() || !args.empty()) return Invalid("save.player_extra_get takes no arguments");
        const auto found = g_player_extras.find(mod_id);
        if (found == g_player_extras.end()) return Ok(Json::object());
        return Ok({{"data", found->second}});
    }
    if (path == "save.player_extra_set") {
        if (!args.is_array() || args.size() != 1 || !args[0].is_object() || (args[0].size() == 1 && !args[0].contains("data")) || args[0].size() > 1) {
            return Invalid("expected {data = a value of this mod's (32 KB as JSON at most)}, or {} to drop it");
        }
        if (!args[0].contains("data") || args[0]["data"].is_null()) {
            g_player_extras.erase(std::string(mod_id));
            return Ok({{"kept", false}});
        }
        if (args[0]["data"].dump().size() > kPlayerExtraMax) return Invalid("data is over 32 KB as JSON");
        g_player_extras[std::string(mod_id)] = args[0]["data"];
        return Ok({{"kept", true}});
    }
    if (path == "world.avatar_set") return AvatarSetCall(args);
    if (path == "world.avatar_clear") {
        int slot = 0;
        if (!args.is_array() || args.size() > 1 ||
            (args.size() == 1 && (!args[0].is_object() || (args[0].contains("slot") && !ReadBoundedInt(args[0], "slot", 1, kAvatars, slot))))) {
            return Invalid("expected {slot = 1-3}, or nothing for every one");
        }
        AvatarClear(slot - 1);
        return Ok({{"cleared", true}});
    }
    if (path == "world.enter_map") return EnterMap(args);
    if (path == "monsters.mirror_get") return MirrorGet(args);
    if (path == "monsters.puppet_set") return PuppetSetCall(args);
    if (path == "monsters.puppet_clear") {
        if (!args.is_array() || !args.empty()) return Invalid("expected no arguments");
        PuppetsClear();
        return Ok({{"cleared", true}});
    }
    if (path == "monsters.mirror_hit") return MirrorHit(args);
    if (path == "dungeons.set_seed") {
        if (!args.is_array() || args.size() > 1 || (args.size() == 1 && !args[0].is_object())) return Invalid("expected {seed = 0-2147483647}, or {} for the game's own");
        if (args.empty() || args[0].empty()) {
            SetNextFloorSeed(std::nullopt);
            return Ok({{"seed", nullptr}});
        }
        const auto seed = args[0].find("seed");
        if (args[0].size() != 1 || seed == args[0].end() || !seed->is_number_integer() || seed->get<std::int64_t>() < 0 || seed->get<std::int64_t>() > 2147483647) {
            return Invalid("expected {seed = 0-2147483647}, or {} for the game's own");
        }
        SetNextFloorSeed(static_cast<std::uint32_t>(seed->get<std::int64_t>()));
        return Ok({{"seed", seed->get<std::int64_t>()}});
    }
    if (path == "dungeons.enter_floor") return EnterFloor(args);
    if (path == "dungeons.shared_events_get") return SharedEventsGet(args);
    if (path == "dungeons.shared_event_run") return SharedEventRun(args);
    if (path == "combat.strike_player") return StrikePlayer(args);
    if (path == "items.owned") return ItemsOwned(args);
    if (path == "georama.town_export" || path == "georama.town_import") return TownExportImport(operation, args);
    if (path == "ui.choice_open") return ChoiceOpenCall(mod_id, args);
    if (path == "title.mode_register") return RegisterTitleMode(mod_id, args);
    if (path == "title.mode_set") return SetTitleMode(mod_id, args);
    if (path == "title.mode_remove") return RemoveTitleMode(mod_id, args);
    return std::nullopt;
}

void AddCapabilities(RuntimeConfig &config) {
    const bool socket = net::Shared().available();
    for (const char *path : {"net.host", "net.join", "net.leave", "net.status", "net.send", "net.receive"}) {
        config.api_capabilities.push_back({path, socket, socket ? std::string() : "this build of the game has no online play (no libcurl, WinHTTP or browser socket)"});
    }
    for (const char *path : {"session.get", "session.start", "session.finish", "save.world_export", "save.world_import", "save.player_export",
                             "save.player_import", "save.player_extra_get", "save.player_extra_set", "save.account_get", "save.account_set",
                             "save.account_list", "save.account_remove", "world.avatar_set", "world.avatar_clear", "world.enter_map",
                             "monsters.mirror_get", "monsters.puppet_set", "monsters.puppet_clear", "monsters.mirror_hit",
                             "dungeons.set_seed", "dungeons.enter_floor", "dungeons.shared_events_get", "dungeons.shared_event_run",
                             "combat.strike_player", "items.owned", "georama.town_export", "georama.town_import", "ui.choice_open",
                             "title.mode_register", "title.mode_set", "title.mode_remove"}) {
        config.api_capabilities.push_back({path, true, {}});
    }
}

void OwnerUnloaded(std::string_view mod_id) {
    UiOwnerUnloaded(mod_id);
    if (net::Shared().owner() == mod_id) net::Shared().Leave("the mod unloaded");
    g_player_extras.erase(std::string(mod_id));
}

void FrameStep() {
    net::Shared().Pump();
    WorldStep();
    UiStep();
}

void ResetForTests() {
    WorldReset();
    UiReset();
    g_player_extras.clear();
    if (net::Shared().state() != net::State::Idle) net::Shared().Leave();
}

} // namespace moonmod::coop
