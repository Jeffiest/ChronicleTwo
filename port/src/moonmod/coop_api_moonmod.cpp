// Co-op's adapter operations (moonmod/coop.hpp): net.*, session.*, the save split and the host's guests, the other
// players' heroes, the host's monsters, shared doors, floors, the choice panel and the title modes. Dispatched before
// the Chronicle Two adapter's own operations (chronicle_two_adapter.cpp calls Call first).

#include "moonmod/coop.hpp"
#include "moonmod/coop_down.hpp"
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

#include "dng_event.hpp"
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
#include "mapjump.hpp"
#include "menucommon.hpp"
#include "platform/legacy_encoding.hpp"
#include "actionchara.hpp"
#include "colprim.hpp"
#include "dng_effect.hpp"
#include "dng_hud.hpp"
#include "snd_mngr.hpp"
#include "userdata.hpp"

// actionchara.cpp's: the hero's own guard flash, and its amulet and held-item checks (CActionChara::CheckDamage).
void GuardEffectSet(CScene *scene, float *point);
int  CheckAmuletAvoid(int item_no);
int  CheckEquipSetItem(int item_no);

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
std::string g_room_hero_owner; // the mod that set the room's hero (party.room_hero_set)
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
    if (!OnlyFields(visit, {"world", "player", "map", "map_no"}, unknown)) return Invalid("visit accepts only world, player, map and map_no");
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
    // Where it starts: map_no, a map's own number (the host's world.get().map_no), or map, a Georama town's area.
    int map = 0, map_no = -1;
    if (visit.contains("map_no")) {
        if (!ReadBoundedInt(visit, "map_no", 0, 9999, map_no)) return Invalid("map_no must be a map's number (world.get().map_no)");
        if (visit.contains("map") && !ReadBoundedInt(visit, "map", 0, SAVE_EDIT_DATA_MAX - 1, map)) {
            return Invalid("map must be the town (area) to start in, 0-" + std::to_string(SAVE_EDIT_DATA_MAX - 1));
        }
    } else if (!visit.contains("map") || !ReadBoundedInt(visit, "map", 0, SAVE_EDIT_DATA_MAX - 1, map)) {
        return Invalid("map must be the town (area) to start in, 0-" + std::to_string(SAVE_EDIT_DATA_MAX - 1) + ", or give map_no");
    }
    VisitRequest queued;
    queued.owner = std::string(mod_id);
    queued.mode_id = open->id;
    queued.map = map;
    queued.map_no = map_no;
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
    // x, y and teleport are Chronicle's way to arrive beside another player; here the way in is the floor's own.
    if (!OnlyFields(request, {"dungeon_id", "floor_id", "x", "y", "teleport"}, unknown)) return Invalid("unknown field: " + unknown);
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
    // The floor load event: onto the floor itself. Event 0, the dungeon's start, stops at its floor list, where the
    // player would have to pick the floor by hand.
    destination.event_no = 1010;
    NextLoop(LOOP_DUNGEON, destination);
    scene->exit_flag = true;
    return Ok({{"dungeon_id", dungeon}, {"floor_id", floor}, {"queued", true}, {"teleport", false}});
}

// A direction along the floor, made one long (none stays none).
void FlatUnit(float *v) {
    v[1] = 0.0f;
    const float length = std::sqrt(v[0] * v[0] + v[2] * v[2]);
    if (length > 1.0e-6f) {
        v[0] /= length;
        v[2] /= length;
    }
    v[3] = 1.0f;
}

ApiCallResult StrikePlayer(const Json &args) {
    static constexpr const char *kUsage =
        "expected {x, y, z, radius = 1-200, damage = 1-9999, kind = \"light\" or \"heavy\", flags = 0-255, stagger = 0-16, stun = 0-255, "
        "guard_rate = 0-100, status = 0-2097151, toward = {x, y, z}}";
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) return Invalid(kUsage);
    const Json &request = args[0];
    std::string unknown;
    if (!OnlyFields(request, {"x", "y", "z", "radius", "damage", "kind", "flags", "stagger", "stun", "guard_rate", "status", "toward"}, unknown)) {
        return Invalid("unknown field: " + unknown);
    }
    float at[4] = {0.0f, 0.0f, 0.0f, 1.0f}, radius = 12.0f;
    int   damage = 0, flags = 0, stagger = 2, stun = 15, guard_rate = 25, status = 0;
    if (!ReadNumber(request, "x", at[0], true) || !ReadNumber(request, "y", at[1], true) || !ReadNumber(request, "z", at[2], true) ||
        !ReadNumber(request, "radius", radius, false) || radius < 1.0f || radius > 200.0f || !ReadBoundedInt(request, "damage", 1, 9999, damage)) {
        return Invalid(kUsage);
    }
    // How the blow was dealt, as the monster's damage row has it (DAMAGE_PARAM); without them, a blow that always
    // makes the hero flinch, as it did before they were sent.
    if ((request.contains("flags") && !ReadBoundedInt(request, "flags", 0, 255, flags)) ||
        (request.contains("stagger") && !ReadBoundedInt(request, "stagger", 0, 16, stagger)) ||
        (request.contains("stun") && !ReadBoundedInt(request, "stun", 0, 255, stun)) ||
        (request.contains("guard_rate") && !ReadBoundedInt(request, "guard_rate", 0, 100, guard_rate)) ||
        (request.contains("status") && !ReadBoundedInt(request, "status", 0, 0x1FFFFF, status))) {
        return Invalid(kUsage);
    }
    float toward[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    if (request.contains("toward")) {
        const Json &t = request["toward"];
        if (!t.is_object() || !ReadNumber(t, "x", toward[0], true) || !ReadNumber(t, "z", toward[2], true) || std::fabs(toward[0]) > 10.0f ||
            std::fabs(toward[2]) > 10.0f) {
            return Invalid(kUsage);
        }
    }
    std::string kind = "light";
    if (request.contains("kind")) {
        if (!request["kind"].is_string()) return Invalid(kUsage);
        kind = request["kind"].get<std::string>();
        if (kind != "light" && kind != "heavy") return Invalid(kUsage);
    }
    if (kind == "heavy") flags |= DAMAGE_HIT_KNOCKDOWN;
    if (GetNowLoopNo() != LOOP_DUNGEON || MainChara__2 == nullptr || GetBattleCharaInfo() == nullptr) return Failed("the player isn't on a dungeon floor");
    if (DngStatus.mode != DNG_STATUS_FIELD) return Ok({{"hit", false}, {"damage", 0}, {"reason", "busy"}});
    CActionChara     *hero = MainChara__2;
    CBattleCharaInfo *info = GetBattleCharaInfo();
    float             player[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    hero->GetPosition(player);
    const float dx = player[0] - at[0], dz = player[2] - at[2];
    if (std::sqrt(dx * dx + dz * dz) > radius + 10.0f || std::fabs(player[1] - at[1]) > 40.0f) return Ok({{"hit", false}, {"damage", 0}, {"reason", "away"}});
    if (hero->muteki_time > 0 || hero->damage_time > 0 || info->GetNowHp_i() <= 0) return Ok({{"hit", false}, {"damage", 0}, {"reason", "grace"}});

    // The blow as CActionChara::CheckDamage takes one: damage is its strength as the monster dealt it in the other
    // game; this hero's defence comes off it, a guard lets the row's share through, and its specials (poison, goo,
    // stone, stop, curse, thirst) take as they do, amulets and all.
    CScene   *scene = DngMainScene;
    const u32 battle_sound = scene != nullptr ? static_cast<u32>(scene->se_battle_id) : static_cast<u32>(-1);
    const u32 chara_sound = hero->sound_info.se_bank;
    const u32 attributes = static_cast<u32>(info->GetAttr());
    bool      immobilized = (attributes & (CHARA_STATUS_STOP | CHARA_STATUS_STONE)) != 0;
    const int max_hp = info->GetMaxHp_i(), now_hp = info->GetNowHp_i();
    float     strength = static_cast<float>(damage) * (1.0f + fRand(0.15f)) - static_cast<float>(info->GetDefenceVol());
    if (strength <= 0.0f) strength = 0.0f;
    strength += 1.0f + fRand(2.0f);
    if ((status & WEAPON_SPECIAL_RATIO_DAMAGE) != 0) {
        strength = static_cast<float>(static_cast<int>(static_cast<float>(max_hp) * (0.01f * static_cast<float>(damage))));
        if (static_cast<float>(now_hp) - strength < 0.0f) strength = static_cast<float>(now_hp) - 1.0f;
    }
    if ((status & WEAPON_SPECIAL_HALF_HP) != 0) strength = std::max(1.0f, static_cast<float>(now_hp / 2));
    if (static_cast<int>(strength) <= 0) strength = 0.0f;
    bool guarded = hero->guard_flag != 0;
    if (guarded) strength *= 0.01f * static_cast<float>(guard_rate);
    if ((flags & DAMAGE_HIT_IGNORE_GUARD) != 0) {
        hero->guard_flag = false;
        guarded = false;
    }
    if (strength >= 0.0f) strength = static_cast<float>(GetDispVolumeForFloat(strength));
    info->AddHp_Point(-strength, 0.0f);
    const int points = static_cast<int>(strength);
    if (points > 0 && scene != nullptr) {
        const bool rain = scene->battle_area.weather == DNG_WEATHER_RAIN;
        if ((status & WEAPON_SPECIAL_POISON) != 0 && iRand(100) < 30 && (attributes & CHARA_STATUS_POISON) == 0 && CheckAmuletAvoid(ITEM_ID_ANTIDOTE_AMULET) == 0) {
            info->SetAttr(CHARA_STATUS_POISON, 0);
            sndSePlay(battle_sound, 0x18, 0);
        }
        if ((status & WEAPON_SPECIAL_SLOW) != 0 && !rain && iRand(100) < 30 && (attributes & CHARA_STATUS_SLOW) == 0 && CheckAmuletAvoid(ITEM_ID_ANTI_GOO_AMULET) == 0) {
            info->SetAttrVol(CHARA_STATUS_SLOW, 3600);
            sndSePlay(battle_sound, 0x52, 0);
        }
        if ((status & WEAPON_SPECIAL_STONE) != 0 && iRand(100) < 50 && (attributes & CHARA_STATUS_STONE) == 0 && CheckAmuletAvoid(ITEM_ID_ANTI_PETRIFY_AMULET) == 0) {
            info->SetAttrVol(CHARA_STATUS_STONE, 900);
            immobilized = true;
            sndSePlay(battle_sound, 0x53, 0);
        }
        if ((status & WEAPON_SPECIAL_STOP) != 0 && iRand(100) < 50 && (attributes & CHARA_STATUS_STOP) == 0 && CheckAmuletAvoid(ITEM_ID_NON_STOP_AMULET) == 0) {
            info->SetAttrVol(CHARA_STATUS_STOP, 300);
            immobilized = true;
            sndSePlay(battle_sound, 0x54, 0);
        }
        if ((status & WEAPON_SPECIAL_CURSE) != 0 && iRand(100) < 50 && (attributes & CHARA_STATUS_CURSE) == 0 && CheckAmuletAvoid(ITEM_ID_ANTI_CURSE_AMULET) == 0) {
            info->SetAttr(CHARA_STATUS_CURSE, 0);
            sndSePlay(battle_sound, 0x55, 0);
        }
        if ((status & WEAPON_SPECIAL_DRY) != 0 && iRand(100) < 50) {
            info->SetAttr(CHARA_STATUS_DRY, 0);
            sndSePlay(battle_sound, 0x52, 0);
        }
    }
    int reaction = ACTION_REACTION_STAGGER;
    if ((flags & DAMAGE_HIT_KNOCKDOWN) != 0) reaction = ACTION_REACTION_KNOCKDOWN;
    if ((flags & DAMAGE_HIT_NO_STAGGER) != 0) reaction = ACTION_REACTION_NONE;
    if (guarded) reaction = ACTION_REACTION_GUARD;
    if (immobilized) reaction = ACTION_REACTION_HOLD;
    if (info->GetNowHp_i() <= 0) {
        // (a Resurrection Powder held brings the hero back, as from any blow; without one, the room's rule)
        if (CheckEquipSetItem(ITEM_ID_RESURRECTION_POWDER) == 0) {
            reaction = ACTION_REACTION_DEAD;
        } else {
            info->SetHpRate(0.5f);
            hero->pallet[0].SetAnim(96, 180, 255, 1, 45, 0);
            if (hero->effect_man != nullptr) {
                hero->effect_man->CreateEffSpt(const_cast<char *>("\x92\xCA\x8F\xED\x89\xF1\x95\x9C"), 0, 0);
                hero->effect_man->SetScriptTargetId(0, -1, -1);
                hero->effect_man->SetValue(0, 0, 0, -1);
            }
            sndSePlay(SystemSND_ID, SYSTEM_SE_RECOVER, 0);
        }
    }
    // Knocked the way the blow drove, as far as a monster's own blow knocks the hero.
    if (toward[0] == 0.0f && toward[2] == 0.0f) {
        toward[0] = player[0] - at[0];
        toward[2] = player[2] - at[2];
    }
    FlatUnit(toward);
    for (int k = 0; k < 4; ++k) hero->blow_vec[k] = toward[k];
    hero->blow_speed = 4.0f;
    hero->blow_rate = 1.0f;
    hero->blow_decel = 0.0f;
    hero->blow_time = 5;
    // And it looks, sounds and feels as one does: a red flash, a shake, the pad's rumble, the hero's cry, a spark where
    // it struck and the damage over the hero's head; a guard rings and flashes instead.
    if (guarded) {
        sndSePlay(battle_sound, 0x21, 0);
        if (!immobilized) sndSePlay(chara_sound, 0x1D, 0);
        if (scene != nullptr) GuardEffectSet(scene, at);
        if (strength > 0.0f) {
            if (scene != nullptr) HitEffectSet(scene, at, flags);
            hero->pallet[0].SetAnim(255, 128, 128, 1, 45, 0);
            hero->shake.time = 6;
            DamageScore2.SetValue(0, points, 0.0f);
            hero->damage_time = stun * 2;
            GamePad__2.SetVibration(0, 96, 6);
        } else {
            GamePad__2.SetVibration(0, 72, 6);
        }
    } else {
        if (reaction == ACTION_REACTION_KNOCKDOWN) {
            hero->damage_time = stun * 7;
            hero->pallet[0].SetAnim(255, 128, 128, 1, 90, 0);
            hero->shake.time = 8;
            GamePad__2.SetVibration(1, 180, 30);
        } else {
            hero->damage_time = stun * 3;
            hero->pallet[0].SetAnim(255, 128, 128, 1, 30, 0);
            hero->shake.time = 8;
            GamePad__2.SetVibration(1, 128, 20);
        }
        if (!immobilized) sndSePlay(chara_sound, 0x24, 0);
        sndSePlay(battle_sound, 0x16, 0);
        if (scene != nullptr) HitEffectSet(scene, at, flags);
        DamageScore2.SetValue(0, points, hero->body_height);
    }
    // The hero reacts as to a monster's own blow: a flinch once the stagger builds (or at once while standing idle),
    // a fall from a knock-down, held still while stopped or stone.
    bool handled = false;
    switch (reaction) {
        case ACTION_REACTION_HOLD:
            handled = true;
            hero->damage_req = ACTION_DAMAGE_REQ_HOLD;
            break;
        case ACTION_REACTION_STAGGER:
            hero->stagger = static_cast<s8>(std::min(127, hero->stagger + stagger));
            hero->stagger_time = 60;
            if (hero->stagger >= 2 || (hero->menu_flag != 0 && hero->stand_flag != 0)) hero->damage_req = ACTION_DAMAGE_REQ_SMALL;
            handled = true;
            break;
        case ACTION_REACTION_KNOCKDOWN:
            handled = true;
            hero->damage_req = ACTION_DAMAGE_REQ_LARGE;
            break;
        case ACTION_REACTION_DEAD:
            handled = true;
            break;
        default:
            break;
    }
    if (handled) hero->menu_flag = 0;
    return Ok({{"hit", true}, {"damage", points}, {"hp", info->GetNowHp_i()}, {"kind", kind}, {"guarded", guarded}});
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
    if (!importing) {
        TownRecordFromLiveMap(); // the town on screen goes as it stands
        return Ok({{"town_id", town}, {"data", ExportTown(*save, town)}});
    }
    if (!args[0].contains("data") || !args[0]["data"].is_string()) return Invalid("data must be georama.town_export text");
    // A Georama town is its map: maps 0-4 are the five towns, each with the edit data of its own number
    // (CSaveData::GetEditData). The town is on screen when the town loop runs that map.
    const bool here = TownOnScreen(town);
    // In its editor the live map is ahead of the record, and is written over it as the player builds. Between the
    // player's actions it is written into the record now, the other game's town is merged into that, and the live map
    // is built again at once: the player sees the other's parts without leaving the editor.
    const bool editing = here && IsEditMode() != 0;
    if (editing && !TownEditorReady()) return Failed("busy: the player is in the middle of building in that town; try again in a moment");
    if (editing) {
        TownRebuildStep(); // a change taken earlier, in a menu, is built first, so the live map is not behind
        TownLiveSync();
    } else if (here) {
        TownRecordFromLiveMap(); // what stands is what the other game's change is merged into (not if behind it)
    }
    const std::string &data = args[0]["data"].get_ref<const std::string &>();
    const bool         merging = args[0].contains("base") && !args[0]["base"].is_null();
    if (merging && !args[0]["base"].is_string()) return Invalid("base must be georama.town_export text");
    std::string error;
    if (merging ? !MergeTown(args[0]["base"].get_ref<const std::string &>(), data, town, *save, error) : !ImportTown(data, town, *save, error)) {
        return Invalid(error);
    }
    std::fprintf(stderr, "co-op: town %d taken (%s%s): its record %d parts, %d standing\n", town, merging ? "merged" : "whole",
                 editing ? ", in its editor" : here ? ", on screen" : ", elsewhere",
                 TownRecordParts(town), TownLiveParts());
    RegisterTownAgreed(town); // what came from another game is not this game's change
    // The town on screen is the old one: its live map is built from the record again where the player stands, as
    // soon as they walk free (TownRebuildStep). Nobody is walked back to the entrance.
    if (here) TownRebuildWanted(town);
    if (editing) {
        TownRebuildStep();
        std::fprintf(stderr, "co-op: town %d %s\n", town, TownRebuildPending() ? "waits for its editor" : "built again in its editor");
    }
    return Ok({{"town_id", town}, {"imported", true}, {"merged", merging}, {"reloaded", false}, {"rebuilding", here && !editing},
               {"rebuilt", editing && !TownRebuildPending()}});
}

// ---- Avatars and monsters ---------------------------------------------------------------------------------------------------

// player.effects_take: the shots and spells this game's hero fired since the last call, oldest first.
ApiCallResult EffectsTakeCall(const Json &args) {
    if (!args.is_array() || !args.empty()) return Invalid("player.effects_take takes no arguments");
    Json effects = Json::array();
    for (const HeroEffect &e : TakeHeroEffects()) {
        Json entry = {{"kind", e.kind}, {"x", e.pos[0]}, {"y", e.pos[1]}, {"z", e.pos[2]}, {"dx", e.dir[0]}, {"dy", e.dir[1]},
                      {"dz", e.dir[2]}, {"value", e.value}, {"amount", e.amount}, {"target", e.target}};
        if (!e.frame0.empty()) {
            entry["frame0"] = e.frame0;
            entry["frame1"] = e.frame1;
        }
        effects.push_back(std::move(entry));
    }
    return Ok({{"effects", std::move(effects)}});
}

// world.avatar_effect: another player's shot, drawn from their avatar.
ApiCallResult AvatarEffectCall(const Json &args) {
    static constexpr const char *kUsage =
        "expected {slot = 1-3, kind = 1-10, x, y, z, dx, dy, dz, value = a whole number or nil, amount = a number or nil, target = a scene slot or nil, frame0, frame1 = a trail's frames or nil, age = frames since it was fired (0-3600) or nil} (player.effects_take's entries)";
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) return Invalid(kUsage);
    const Json &request = args[0];
    std::string unknown;
    if (!OnlyFields(request, {"slot", "kind", "x", "y", "z", "dx", "dy", "dz", "value", "amount", "target", "frame0", "frame1", "age"}, unknown)) {
        return Invalid("unknown field: " + unknown);
    }
    int        slot = 0;
    HeroEffect e;
    if (!ReadBoundedInt(request, "slot", 1, kAvatars, slot) || !ReadBoundedInt(request, "kind", 1, 10, e.kind)) return Invalid(kUsage);
    for (const char *key : {"frame0", "frame1"}) {
        if (!request.contains(key) || request[key].is_null()) continue;
        if (!request[key].is_string() || request[key].get<std::string>().size() > 31) return Invalid(kUsage);
        (key[5] == '0' ? e.frame0 : e.frame1) = request[key].get<std::string>();
    }
    if (!ReadNumber(request, "x", e.pos[0], true) || !ReadNumber(request, "y", e.pos[1], true) || !ReadNumber(request, "z", e.pos[2], true) ||
        !ReadNumber(request, "dx", e.dir[0], true) || !ReadNumber(request, "dy", e.dir[1], true) || !ReadNumber(request, "dz", e.dir[2], true) ||
        !ReadNumber(request, "amount", e.amount, false)) {
        return Invalid(kUsage);
    }
    if (request.contains("value") && !request["value"].is_null() && !ReadBoundedInt(request, "value", -1, 65535, e.value)) return Invalid(kUsage);
    if (request.contains("target") && !request["target"].is_null() && !ReadBoundedInt(request, "target", -1, 255, e.target)) return Invalid(kUsage);
    int age = -1;
    if (request.contains("age") && !request["age"].is_null() && !ReadBoundedInt(request, "age", 0, 3600, age)) return Invalid(kUsage);
    return Ok({{"shown", AvatarEffect(slot - 1, e, age)}});
}

ApiCallResult AvatarSetCall(const Json &args) {
    static constexpr const char *kUsage =
        "expected {slot = 1-3, character = \"max\", \"monica\", \"ridepod\" or \"monster\", weapon = item_no or nil, outfit = {head, clothes, feet, sub} item_nos or nil, ridepod = {leg, arm, body, pack} item_nos or nil, form = monster_id or nil, x, y, z, heading, motion, motion_flags = 0-7 or nil, motion_step = 0-16 or nil, age = 0-3600 frames or nil, tint = {r, g, b} or nil, name = text or nil, down = true or nil, away = true or nil}";
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) return Invalid(kUsage);
    const Json &request = args[0];
    std::string unknown;
    if (!OnlyFields(request, {"slot", "character", "weapon", "outfit", "ridepod", "form", "x", "y", "z", "heading", "motion", "motion_flags",
                              "motion_step", "age", "tint", "name", "down", "away"}, unknown)) {
        return Invalid("unknown field: " + unknown);
    }
    int slot = 0;
    if (!ReadBoundedInt(request, "slot", 1, kAvatars, slot)) return Invalid(kUsage);
    AvatarState state;
    if (!request.contains("character") || !request["character"].is_string()) return Invalid(kUsage);
    const std::string character = request["character"].get<std::string>();
    if (character == "max") state.character = USER_CHARA_MAX;
    else if (character == "monica") state.character = USER_CHARA_MONICA;
    else if (character == "ridepod" || character == "monster") {
        // Max's ridepod and Monica's monster forms are drawn as themselves when ridepod (its parts) or form (the
        // monster) says which; else, or when they fail to load, the hero stands in for them (AvatarState::stand_in), so
        // the other player is still seen where they are, and still someone a monster goes for.
        state.character = character == "ridepod" ? USER_CHARA_MAX : USER_CHARA_MONICA;
        state.stand_in = true;
    } else return Invalid("character must be \"max\", \"monica\", \"ridepod\" or \"monster\"");
    if (request.contains("weapon") && !request["weapon"].is_null() && !ReadBoundedInt(request, "weapon", 1, ITEM_NO_MAX - 1, state.weapon)) {
        return Invalid("weapon must be an item_no");
    }
    if (request.contains("outfit") && !request["outfit"].is_null()) {
        // What the other player's hero wears, by item_no; a piece left out is as this game's own hero has it.
        const Json &outfit = request["outfit"];
        std::string strange;
        if (!outfit.is_object() || !OnlyFields(outfit, {"head", "clothes", "feet", "sub"}, strange)) return Invalid("outfit must be {head, clothes, feet, sub}, each an item_no or left out");
        const std::pair<const char *, int *> pieces[] = {{"head", &state.head}, {"clothes", &state.clothes}, {"feet", &state.feet}, {"sub", &state.sub}};
        for (const auto &[key, into] : pieces) {
            if (outfit.contains(key) && !outfit[key].is_null() && !ReadBoundedInt(outfit, key, 1, ITEM_NO_MAX - 1, *into)) {
                return Invalid("outfit must be {head, clothes, feet, sub}, each an item_no or left out");
            }
        }
    }
    if (request.contains("ridepod") && !request["ridepod"].is_null()) {
        // The ridepod's four parts, as player.get's ridepod gives them: the leg, the arm, the body and the energy pack.
        static constexpr const char *kRidepodUsage = "ridepod must be {leg, arm, body, pack}, each an item_no";
        const Json &parts = request["ridepod"];
        std::string strange;
        if (!parts.is_object() || !OnlyFields(parts, {"leg", "arm", "body", "pack"}, strange)) return Invalid(kRidepodUsage);
        const char *keys[4] = {"leg", "arm", "body", "pack"};
        for (int part = 0; part < 4; ++part) {
            if (!ReadBoundedInt(parts, keys[part], 1, ITEM_NO_MAX - 1, state.ridepod[part])) return Invalid(kRidepodUsage);
        }
    }
    if (request.contains("form") && !request["form"].is_null() && !ReadBoundedInt(request, "form", 0, 4095, state.form)) {
        return Invalid("form must be a monster_id");
    }
    if (!ReadNumber(request, "x", state.x, false) || !ReadNumber(request, "y", state.y, false) || !ReadNumber(request, "z", state.z, false) ||
        !ReadNumber(request, "heading", state.heading, false)) {
        return Invalid(kUsage);
    }
    if (request.contains("motion") && !ReadBoundedInt(request, "motion", 0, 1023, state.motion)) return Invalid("motion must be 0-1023");
    if (request.contains("motion_flags") && !request["motion_flags"].is_null() && !ReadBoundedInt(request, "motion_flags", 0, 7, state.motion_flags)) {
        return Invalid("motion_flags must be 0-7");
    }
    if (request.contains("motion_step") && !request["motion_step"].is_null() &&
        (!ReadNumber(request, "motion_step", state.motion_step, true) || state.motion_step < 0.0f || state.motion_step > 16.0f)) {
        return Invalid("motion_step must be 0-16");
    }
    if (request.contains("age") && !request["age"].is_null() && !ReadBoundedInt(request, "age", 0, 3600, state.age)) return Invalid("age must be 0-3600 frames");
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
    if (request.contains("down") && !request["down"].is_null()) {
        if (!request["down"].is_boolean()) return Invalid("down must be true or left out");
        state.down = request["down"].get<bool>();
    }
    if (request.contains("away") && !request["away"].is_null()) {
        if (!request["away"].is_boolean()) return Invalid("away must be true or left out");
        state.away = request["away"].get<bool>();
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
                                {"heading", std::round(rotation[1] * 1000.0f) / 1000.0f}, {"motion", MotionNumberOf(*m)},
                                {"motion_flags", m->now_flags & 7}, {"motion_step", std::round(m->step * 100.0f) / 100.0f},
                                // (drawn in this game: in sight of its hero, or fading in or out)
                                {"seen", m->view_state != MONSTER_VIEW_OUT && m->view_state != MONSTER_VIEW_INIT && m->view_alpha > 0.0f}});
        }
    }
    Json hits = Json::array();
    for (const AvatarHit &hit : TakeAvatarHits()) {
        const auto round = [](float v) { return std::round(v * 100.0f) / 100.0f; };
        hits.push_back({{"avatar", hit.avatar}, {"slot", hit.monster}, {"damage", hit.damage}, {"kind", hit.kind}, {"flags", hit.flags},
                        {"stun", hit.stun}, {"stagger", hit.stagger}, {"guard_rate", hit.guard_rate}, {"status", hit.status},
                        {"at", {{"x", round(hit.at[0])}, {"y", round(hit.at[1])}, {"z", round(hit.at[2])}}},
                        {"toward", {{"x", round(hit.toward[0])}, {"z", round(hit.toward[2])}}}});
    }
    return Ok({{"monsters", std::move(monsters)}, {"hits", std::move(hits)}});
}

ApiCallResult PuppetSetCall(const Json &args) {
    static constexpr const char *kUsage = "expected {monsters = {{slot, kind, state, hp, max_hp, x, y, z, heading, motion, motion_flags, motion_step}, ...}, age = frames or nil}";
    if (!args.is_array() || args.size() != 1 || !args[0].is_object() || !args[0].contains("monsters") ||
        !(args[0]["monsters"].is_array() || (args[0]["monsters"].is_object() && args[0]["monsters"].empty()))) {
        return Invalid(kUsage);
    }
    if (GetNowLoopNo() != LOOP_DUNGEON || ActiveMonster == nullptr) return Failed("not on a dungeon floor");
    int age = -1;
    if (args[0].contains("age") && !args[0]["age"].is_null() && !ReadBoundedInt(args[0], "age", 0, 3600, age)) return Invalid(kUsage);
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
        if (entry.contains("motion_flags") && !ReadBoundedInt(entry, "motion_flags", 0, 7, puppet.motion_flags)) return Invalid(kUsage);
        if (entry.contains("motion_step") && (!ReadNumber(entry, "motion_step", puppet.motion_step, true) || puppet.motion_step > 16.0f)) {
            return Invalid(kUsage);
        }
        puppet.age = age;
        if (!ReadNumber(entry, "x", puppet.x, false) || !ReadNumber(entry, "y", puppet.y, false) || !ReadNumber(entry, "z", puppet.z, false) ||
            !ReadNumber(entry, "heading", puppet.heading, false)) {
            return Invalid(kUsage);
        }
        named[static_cast<std::size_t>(slot)] = true;
        (PuppetSet(slot, puppet) ? followed : refused).push_back(slot);
    }
    // A slot the leader no longer lists is gone there, and so is a monster of this floor's the leader never listed
    // (one the leader struck down before this game came onto the floor): the list is the leader's whole floor, and
    // a monster left standing here would keep this game's floor from its clear.
    for (int slot = 0; slot < MONSTER_ACTIVE_MAX; ++slot) {
        if (named[static_cast<std::size_t>(slot)]) continue;
        const CActiveMonster *monster = ActiveMonster->GetActiveMonster(slot);
        const bool            here = monster != nullptr && (monster->state != ACTIVE_MONSTER_NONE || monster->chara_kind == ACTION_KIND_SCRIPT);
        if (IsPuppet(slot) || here) PuppetSet(slot, Puppet{});
    }
    Json hits = Json::array();
    for (const PuppetHit &hit : TakePuppetHits()) {
        Json one = {{"slot", hit.slot}, {"damage", hit.damage}};
        if (hit.kind >= 0) {
            const auto round = [](float v) { return std::round(v * 10.0f) / 10.0f; };
            one["kind"] = hit.kind;
            one["element"] = hit.element;
            one["flags"] = hit.flags;
            one["stagger"] = hit.stagger;
            one["stun"] = hit.stun;
            one["guard"] = hit.guard;
            one["at"] = {{"x", round(hit.at[0])}, {"y", round(hit.at[1])}, {"z", round(hit.at[2])}};
        }
        hits.push_back(std::move(one));
    }
    return Ok({{"followed", std::move(followed)}, {"refused", std::move(refused)}, {"hits", std::move(hits)}});
}

ApiCallResult MirrorHit(const Json &args) {
    static constexpr const char *kUsage =
        "expected {slot, damage = 0-1000000, kind, element = -1-3, flags = 0-255, stagger = 0-16, stun = 0-255, guard, at = {x, y, z}}";
    int slot = 0, damage = 0, kind = -1, element = -1, flags = 0, stagger = -1, stun = 0;
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) return Invalid(kUsage);
    const Json &request = args[0];
    std::string unknown;
    if (!OnlyFields(request, {"slot", "damage", "kind", "element", "flags", "stagger", "stun", "guard", "at"}, unknown)) return Invalid("unknown field: " + unknown);
    if (!ReadBoundedInt(request, "slot", 0, MONSTER_ACTIVE_MAX - 1, slot) || !ReadBoundedInt(request, "damage", 0, 1000000, damage) ||
        (request.contains("kind") && !ReadBoundedInt(request, "kind", -1, 31, kind)) ||
        (request.contains("element") && !ReadBoundedInt(request, "element", -1, 3, element)) ||
        (request.contains("flags") && !ReadBoundedInt(request, "flags", 0, 255, flags)) ||
        (request.contains("stagger") && !ReadBoundedInt(request, "stagger", 0, 16, stagger)) ||
        (request.contains("stun") && !ReadBoundedInt(request, "stun", 0, 255, stun)) ||
        (request.contains("guard") && !request["guard"].is_boolean())) {
        return Invalid(kUsage);
    }
    const bool guard = request.contains("guard") && request["guard"].get<bool>();
    float      offset[3] = {0.0f, 0.0f, 0.0f};
    bool       placed = false;
    if (request.contains("at")) {
        const Json &a = request["at"];
        if (!a.is_object() || !ReadNumber(a, "x", offset[0], true) || !ReadNumber(a, "y", offset[1], true) || !ReadNumber(a, "z", offset[2], true) ||
            std::fabs(offset[0]) > 200.0f || std::fabs(offset[1]) > 200.0f || std::fabs(offset[2]) > 200.0f) {
            return Invalid(kUsage);
        }
        placed = true;
    }
    if (damage == 0 && !request.contains("kind")) return Invalid(kUsage); // (a blow that took nothing says how it landed)
    if (GetNowLoopNo() != LOOP_DUNGEON || ActiveMonster == nullptr) return Failed("not on a dungeon floor");
    CActiveMonster *monster = ActiveMonster->GetActiveMonster(slot);
    if (monster == nullptr || monster->state != ACTIVE_MONSTER_LIVE || monster->life <= 0) return Ok({{"slot", slot}, {"damage", 0}, {"killed", false}});
    // Where it struck: on the monster where the other game saw it land, else at its middle.
    float hit_pos[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    monster->GetPosition(hit_pos);
    if (placed) {
        for (int k = 0; k < 3; ++k) hit_pos[k] += offset[k];
    } else {
        for (int k = 0; k < 3; ++k) hit_pos[k] = monster->center_pos[k];
    }
    CScene   *scene = DngMainScene;
    const u32 se_id = scene != nullptr ? static_cast<u32>(scene->se_battle_id) : static_cast<u32>(-1);
    const bool shown = scene != nullptr && kind >= 0; // (a blow told only by its damage, as before: no look)
    // A blow that took nothing rings off it, as the game shows one (CMonsterMan::CheckDamage).
    if (damage == 0) {
        if (shown) {
            GuardEffectSet(scene, hit_pos, 0);
            sndSePlay(se_id, 0x26, 0);
            HitScoreSet(hit_pos, guard ? HIT_SCORE_GUARD : HIT_SCORE_NO_DAMAGE, 0);
        }
        if (guard) monster->req_prog = MONSTER_PROG_GUARD;
        return Ok({{"slot", slot}, {"damage", 0}, {"killed", false}});
    }
    // Another player's blow, as their game measured it: the life goes, and a kill dies here.
    const int dealt = std::min(damage, monster->life);
    monster->life -= dealt;
    if (shown) {
        // It looks and sounds as this game's own hero's blow would: knocked back a little by a blade or a fist, a flash in
        // the element's colour, a shake, the weapon's hit sound, a spark and the damage over it.
        const BASE_MONSTER_TBL *tbl = monster->tbl;
        const bool contact = kind == DAMAGE_KIND_MAX_MELEE || kind == DAMAGE_KIND_MONICA_MELEE || kind == DAMAGE_KIND_ITEM ||
                             kind == DAMAGE_KIND_RIDEPOD_PUNCH || kind == DAMAGE_KIND_RIDEPOD_SWORD || kind == DAMAGE_KIND_RIDEPOD_OTHER ||
                             kind == DAMAGE_KIND_RIDEPOD_GUN || kind == DAMAGE_KIND_MONSTER || kind == DAMAGE_KIND_MONSTER_OTHER;
        if ((monster->attrib & MONSTER_ATTRIB_NO_BLOW) == 0 && contact) {
            float feet[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            monster->GetPosition(feet);
            for (int k = 0; k < 3; ++k) monster->blow_vec[k] = feet[k] - hit_pos[k];
            FlatUnit(monster->blow_vec);
            monster->blow_speed = 2.0f;
            monster->blow_rate = 1.0f;
            monster->blow_decel = 0.0f;
            monster->blow_time = 3;
        }
        monster->damage_time = stun;
        switch (element) {
            case WEAPON_ELEMENT_THUNDER: monster->pallet[0].SetAnim(0xB4, 0xB4, 0, 4, 0x18, 0); break;
            case WEAPON_ELEMENT_FIRE: monster->pallet[0].SetAnim(0xFF, 0x60, 0, 1, 0x18, 0); break;
            case WEAPON_ELEMENT_WIND: monster->pallet[0].SetAnim(0x60, 0xA0, 0x60, 1, 0x18, 0); break;
            case WEAPON_ELEMENT_COLD: monster->pallet[0].SetAnim(0, 0xB4, 0xFF, 1, 0x18, 0); break;
            default: monster->pallet[0].SetAnim(0xFF, 0xB4, 0x80, 2, 0xA, 0); break;
        }
        monster->shake.time = 4;
        int se_no = -1;
        switch (kind) {
            case DAMAGE_KIND_MONSTER:
            case DAMAGE_KIND_MONSTER_OTHER:
            case DAMAGE_KIND_ITEM:
            case DAMAGE_KIND_MAX_MELEE: se_no = 0x1C; break;
            case DAMAGE_KIND_MAX_GUN: se_no = 0x1A; break;
            case DAMAGE_KIND_MONICA_MELEE:
            case DAMAGE_KIND_RIDEPOD_SWORD: se_no = 0x1B; break;
            case DAMAGE_KIND_RIDEPOD_PUNCH: se_no = 0x19; break;
            default: break;
        }
        if (se_no >= 0) sndSePlay(se_id, se_no, 0);
        HitEffectSet(scene, hit_pos, flags);
        HitScoreSet(hit_pos, HIT_SCORE_DAMAGE, dealt);
        // And it reacts as to this game's own: a flinch once its stagger builds, a fall from a knock-down, held while
        // stopped or stone, a guard if it guarded, stars from a stunning blow.
        if (monster->life > 0) {
            if ((monster->status.attr & (MONSTER_STATUS_STOP | MONSTER_STATUS_STONE)) != 0) {
                monster->req_prog = MONSTER_PROG_STATUS;
                sndSePlay(monster->sound_info.se_bank, 0xB, 0);
            } else if ((flags & DAMAGE_HIT_KNOCKDOWN) != 0) {
                if (tbl == nullptr || (tbl->flags & MONSTER_TBL_FLAG_NO_KNOCK) == 0) {
                    monster->req_prog = MONSTER_PROG_KNOCK;
                    monster->piyori.Reset();
                    monster->stagger = 0;
                    monster->stagger_time = 0;
                }
            } else if ((flags & DAMAGE_HIT_NO_STAGGER) == 0) {
                if (guard) {
                    monster->req_prog = MONSTER_PROG_GUARD;
                } else if (tbl != nullptr && ((tbl->flags & MONSTER_TBL_FLAG_NO_STAGGER) != 0 || monster->piyori_time > 0)) {
                    // (it shrugs it off)
                } else if (tbl != nullptr && tbl->stagger > 0 && stagger >= 0) {
                    monster->stagger = static_cast<decltype(monster->stagger)>(monster->stagger + stagger);
                    monster->stagger_time = 60;
                    if (monster->stagger >= tbl->stagger) {
                        monster->req_prog = MONSTER_PROG_DAMAGE;
                        sndSePlay(monster->sound_info.se_bank, 0xB, 0);
                    }
                } else {
                    monster->req_prog = MONSTER_PROG_DAMAGE;
                    sndSePlay(monster->sound_info.se_bank, 0xB, 0);
                }
            }
            if ((monster->req_prog == MONSTER_PROG_DAMAGE || monster->req_prog == MONSTER_PROG_KNOCK) && (flags & DAMAGE_HIT_STUN) != 0) {
                monster->req_prog = MONSTER_PROG_PIYORI;
                monster->piyori_time = 120;
            }
        }
    } else if (monster->life > 0) {
        monster->req_prog = MONSTER_PROG_DAMAGE;
    }
    if (monster->life == 0) {
        if (kind >= 0) monster->last_hit_kind = kind;
        // (dealt by another player: the game shares a kill another dealt between this hero's weapons, as for an
        // attacker of -1 in AddExpWeaponParam; left as it was, Max took nothing from it)
        monster->last_hit_chara = -1;
        monster->last_hit_source = DAMAGE_SOURCE_NONE;
        monster->last_hit_attr = 0;
        monster->damage_req = ACTION_DAMAGE_REQ_DEAD;
        ChronicleTwoEnemyDefeated(monster, "coop");
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
    static constexpr const char *kUsage =
        "expected {id, title, subtitle?, cancel?, town?, style = \"panel\" or \"yes_no\", options = {{title, subtitle?}, ...} (1-6; yes_no: 2)}";
    if (!args.is_array() || args.size() != 1 || !args[0].is_object()) return Invalid(kUsage);
    const Json &request = args[0];
    std::string unknown;
    if (!OnlyFields(request, {"id", "title", "subtitle", "cancel", "town", "style", "options"}, unknown)) return Invalid("ui.choice_open received an unsupported field: " + unknown);
    bool yes_no = false;
    if (request.contains("style")) {
        if (!request["style"].is_string()) return Invalid(kUsage);
        const std::string &style = request["style"].get_ref<const std::string &>();
        if (style != "panel" && style != "yes_no") return Invalid(kUsage);
        yes_no = style == "yes_no";
        if (yes_no && (!request.contains("options") || !request["options"].is_array() || request["options"].size() != 2)) {
            return Invalid("a yes_no choice has two options: yes, then no");
        }
    }
    if (!request.contains("id") || !request["id"].is_string() || request["id"].get_ref<const std::string &>().empty() ||
        request["id"].get_ref<const std::string &>().size() > 64) {
        return Invalid("id must be 1-64 bytes of text");
    }
    if (!request.contains("title") || !request["title"].is_string() || request["title"].get_ref<const std::string &>().size() > 64) return Invalid("title must be at most 64 bytes of text");
    if (!request.contains("options") || !request["options"].is_array() || request["options"].empty() || request["options"].size() > 6) return Invalid(kUsage);
    Choice choice;
    choice.yes_no = yes_no;
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

// ui.choice_close {}: the calling mod's own open panel closes, with no ui.choice_made; a panel another mod opened
// stays. {closed = true} when a panel closed, {closed = false} when this mod had none open.
ApiCallResult ChoiceCloseCall(std::string_view mod_id, const Json &args) {
    static constexpr const char *kUsage = "expected {} or no argument";
    if (!args.is_array() || args.size() > 1) return Invalid(kUsage);
    if (args.size() == 1 && !(args[0].is_object() && args[0].empty()) && !args[0].is_null()) return Invalid(kUsage);
    return Ok({{"closed", ChoiceCloseQuietly(mod_id)}});
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
    if (loop == LOOP_DUNGEON) player["down"] = HeroIsDown();          // lying under the room's rule, waiting to be revived
    player["motion"] = std::max(0, MotionNumberOf(*hero));            // the motion the hero plays (the avatars play it on other screens)
    player["motion_flags"] = hero->now_flags & 7;                      // how: 0 loops, 2 stops on its last frame (CharaMotionFlag)
    player["motion_step"] = std::round(hero->step * 100.0f) / 100.0f;  // and how fast: frames a step (a walk's follows the stick)
    if (CUserDataManager *user = GetUserDataMan(); user != nullptr) {
        const int character = user->GetActiveChrNo();
        if (character == USER_CHARA_ROBO) {
            // The ridepod's parts, for the other games to build it as it is (world.avatar_set's ridepod).
            const ROBO_DATA &robo = user->robo_data;
            player["ridepod"] = {{"leg", robo.parts[ROBO_PART_LEG].item_no}, {"arm", robo.parts[ROBO_PART_ARM].item_no},
                                 {"body", robo.parts[ROBO_PART_BODY].item_no}, {"pack", robo.parts[ROBO_PART_PACK].item_no}};
        } else if (character == USER_CHARA_MONSTER) {
            player["form"] = user->monster_id; // the monster whose form Monica wears (world.avatar_set's form)
        }
        // A ridepod or a form is the hero's second shape: what the hero wears goes with it (Max sits in the ridepod).
        const int hero = character == USER_CHARA_ROBO ? USER_CHARA_MAX : character == USER_CHARA_MONSTER ? USER_CHARA_MONICA : character;
        if (hero == USER_CHARA_MAX || hero == USER_CHARA_MONICA) {
            if (loop == LOOP_EDIT && hero == character) player["town_character"] = CharacterName(character);
            if (CHARA_DATA *data = user->GetCharaDataPtr(hero); data != nullptr && !player.contains("weapon")) {
                const int item_no = data->equip[CHARA_EQUIP_MELEE].item_no;
                if (item_no > 0) player["weapon"] = {{"id", "chronicle-two:item:" + std::to_string(item_no)}, {"item_no", item_no}, {"slot", CHARA_EQUIP_MELEE}};
                // What the hero wears and carries besides, for the other games to draw (world.avatar_set's outfit).
                player["outfit"] = {{"head", data->equip[CHARA_EQUIP_HEAD].item_no}, {"clothes", data->equip[CHARA_EQUIP_CLOTHES].item_no},
                                    {"feet", data->equip[CHARA_EQUIP_FEET].item_no}, {"sub", data->equip[CHARA_EQUIP_SUB].item_no}};
            }
        }
    }
}

void ExtendWorld(Json &world) {
    const int loop = GetNowLoopNo();
    world["story_scene"] = StoryScenePlaying(); // (one of the story's scenes plays here: the others hide this hero)
    CSaveData *save = GetSaveData();
    if (save == nullptr) return;
    world["map_no"] = save->map_no;
    if (const std::optional<int> hero = RoomHero()) world["room_hero"] = *hero == USER_CHARA_MAX ? "max" : "monica";
    world["progress"] = save->game_progress; // how far the story is; the chapter follows from it
    world["chapter"] = GetNowChapter(save);
    if (loop == LOOP_EDIT) {
        world["town_map"] = save->area_no;
        world["editing"] = IsEditMode() != 0;
        world["walking"] = IsEditMode() == 0 && !ChoiceIsOpen();
        // The place itself: the map the town loop runs (a district of Palm Brinks is a map of its own, while the
        // save's map stays the town's), and the house the hero is inside, if any. Its name is the game's own.
        const INIT_LOOP_ARG *now = GetNowInitArg();
        const int            scene_map = now != nullptr && now->map_no >= 0 ? now->map_no : save->map_no;
        int                  named = scene_map;
        world["scene_map"] = scene_map;
        // The Georama town this map is, 0-4 (the number georama.town_export and its edit data go by), if it is one.
        if (scene_map >= 0 && scene_map < SAVE_EDIT_DATA_MAX && GetMapType(scene_map) == MAP_TYPE_GEORAMA) world["georama_town"] = scene_map;
        // The parts standing in it as it is on screen (the town's record may be ahead of the screen for a moment).
        if (const int parts = TownLiveParts(); parts >= 0) world["georama_live_parts"] = parts;
        if (InInterior() != 0 && save->sub_map_no >= 0) {
            world["house"] = save->sub_map_no;
            named = save->sub_map_no;
        }
        if (const char *title = GetMapTitle(named); title != nullptr && title[0] != '\0') {
            if (const std::optional<std::string> text = legacy_encoding::ReadableText(title)) world["map_title"] = *text;
        }
    }
    if (loop == LOOP_DUNGEON) {
        if (const std::optional<int> seed = LastFloorSeed()) world["floor_seed"] = *seed;
        if (const std::optional<std::string> layout = FloorLayoutHash()) world["floor_layout"] = *layout;
        // Playing on the floor: the hero is there and no scene, menu or floor change holds the game.
        world["walking"] = MainChara__2 != nullptr && DngStatus.mode == DNG_STATUS_FIELD && !ChoiceIsOpen();
        // The floor's clear as the game itself decides it (IsEventRun): the monsters it still counts (a monster
        // struck down counts until its death has played out and its slot is free), the mimic chests not yet opened
        // (a mimic is a monster to the rule), and whether the floor's clear has been given.
        if (ActiveMonster != nullptr) world["monsters_counted"] = ActiveMonster->GetMonsterNum(-1.0f);
        if (TreasureBoxMan != nullptr) world["mimic_chests"] = TreasureBoxMan->MimicCount();
        if (BattleAreaScene != nullptr) world["floor_cleared"] = BattleAreaScene->battle_clear != 0;
        // The map this dungeon's own way out leads to (the game's table, as its menu's way out of a dungeon uses it).
        static constexpr const char *kWayOut[7] = {"d01e01", "s02", "g02", "g03", "g04", "d06e01", "m05"};
        if (DngStatus.dungeon_no >= 0 && DngStatus.dungeon_no < 7 && GetMapNameCount() > 0) {
            char name[16] = {};
            std::snprintf(name, sizeof(name), "%s", kWayOut[DngStatus.dungeon_no]);
            if (const int map = SearchMapNo(name); map >= 0) world["exit_map"] = map;
        }
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
        if (path == "save.world_export") TownRecordFromLiveMap(); // the town on screen goes as it stands
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
        if (path == "save.world_import") {
            RegisterRebase(); // a whole world is where the games begin again, not a change
            for (int town = 0; town < SAVE_EDIT_DATA_MAX; ++town) {
                if (TownOnScreen(town)) TownRebuildWanted(town); // the town on screen is the old one: built again
            }
        }
        return Ok({{"imported", true}});
    }
    if (path == "world.watch_set") {
        if (!args.is_array() || args.size() != 1 || !args[0].is_object() || args[0].size() != 1 || !args[0].contains("enabled") || !args[0]["enabled"].is_boolean()) {
            return Invalid("expected {enabled = true or false}");
        }
        RegisterWatch(mod_id, args[0]["enabled"].get<bool>());
        return Ok({{"enabled", args[0]["enabled"].get<bool>()}});
    }
    if (path == "world.change_apply") {
        int id = 0;
        if (!args.is_array() || args.size() != 1 || !args[0].is_object() || args[0].size() != 3 || !args[0].contains("kind") || !args[0]["kind"].is_string() ||
            !ReadBoundedInt(args[0], "id", 0, 100000, id) || !args[0].contains("value")) {
            return Invalid("expected {kind, id, value}, as game.world.changed gives them");
        }
        std::string error;
        if (!RegisterApply(args[0]["kind"].get_ref<const std::string &>(), id, args[0]["value"], error)) return Invalid(error);
        return Ok({{"applied", true}});
    }
    if (path == "player.down_rule_set") {
        if (!args.is_array() || args.size() != 1 || !args[0].is_object() || args[0].size() != 1 || !args[0].contains("enabled") || !args[0]["enabled"].is_boolean()) {
            return Invalid("expected {enabled = true or false}");
        }
        SetDownRule(mod_id, args[0]["enabled"].get<bool>());
        return Ok({{"enabled", DownRule()}});
    }
    if (path == "player.revive") {
        static constexpr const char *kUsage = "expected {hp = 1 or more, x, y, z = a place to stand up at, or left out}";
        if (!args.is_array() || args.size() != 1 || !args[0].is_object()) return Invalid(kUsage);
        std::string unknown;
        if (!OnlyFields(args[0], {"hp", "x", "y", "z"}, unknown)) return Invalid("unknown field: " + unknown);
        int hp = 0;
        if (!ReadBoundedInt(args[0], "hp", 1, 99999, hp)) return Invalid(kUsage);
        float at[3] = {0.0f, 0.0f, 0.0f};
        const bool placed = args[0].contains("x") || args[0].contains("y") || args[0].contains("z");
        if (placed && (!ReadNumber(args[0], "x", at[0], true) || !ReadNumber(args[0], "y", at[1], true) || !ReadNumber(args[0], "z", at[2], true))) {
            return Invalid(kUsage);
        }
        std::string error;
        if (!HeroRevive(hp, placed ? at : nullptr, error)) return Failed(error);
        return Ok({{"revived", true}});
    }
    if (path == "player.give_up") {
        if (!args.is_array() || !args.empty()) return Invalid("player.give_up takes no arguments");
        std::string error;
        if (!HeroGiveUp(error)) return Failed(error);
        return Ok({{"queued", true}});
    }
    if (path == "party.room_hero_set") {
        if (!args.is_array() || args.size() != 1 || !args[0].is_object() || args[0].size() != 1 || !args[0].contains("hero") || !args[0]["hero"].is_string()) {
            return Invalid("expected {hero = 'max' or 'monica'}");
        }
        const std::string &name = args[0]["hero"].get_ref<const std::string &>();
        if (name != "max" && name != "monica") return Invalid("hero must be 'max' or 'monica'");
        const int hero = name == "max" ? USER_CHARA_MAX : USER_CHARA_MONICA;
        SetRoomHero(hero);
        g_room_hero_owner = std::string(mod_id);
        return Ok({{"hero", name}});
    }
    if (path == "party.room_hero_clear") {
        if (!args.is_array() || args.size() > 1 || (args.size() == 1 && (!args[0].is_object() || !args[0].empty()))) return Invalid("party.room_hero_clear takes no arguments");
        const bool had = RoomHero().has_value();
        SetRoomHero(std::nullopt);
        g_room_hero_owner.clear();
        return Ok({{"cleared", had}});
    }
    if (path == "save.slots_list") {
        if (!args.is_array() || !args.empty()) return Invalid("save.slots_list takes no arguments");
        Json slots = Json::array();
        for (const SlotInfo &slot : SlotsList()) {
            slots.push_back({{"slot", slot.slot}, {"ready", slot.ready}, {"progress", slot.progress}, {"chapter", slot.chapter}, {"play_time", slot.play_time}});
        }
        return Ok({{"slots", std::move(slots)}});
    }
    if (path == "save.slot_player_export" || path == "save.slot_player_import") {
        const bool import = path == "save.slot_player_import";
        int        slot = -1;
        if (!args.is_array() || args.size() != 1 || !args[0].is_object() || !ReadBoundedInt(args[0], "slot", 0, 9999, slot) ||
            args[0].size() != (import ? 2u : 1u) || (import && (!args[0].contains("data") || !args[0]["data"].is_string()))) {
            return Invalid(import ? "expected {slot = a save's number (save.slots_list), data = save.player_export text}" : "expected {slot = a save's number (save.slots_list)}");
        }
        std::string error;
        if (!import) {
            std::string text;
            int         progress = 0;
            if (!SlotPlayerExport(slot, text, progress, error)) return Failed(error);
            return Ok({{"slot", slot}, {"data", std::move(text)}, {"progress", progress}, {"chapter", ChapterOfProgress(progress)}});
        }
        // Only a visitor's game writes its player's things back: any other time the game's own saving does it.
        const std::string *owner = VisitOwner();
        if (owner == nullptr || *owner != mod_id) return Failed("only this mod's co-op visit writes a player's things into a save");
        if (!SlotPlayerImport(slot, args[0]["data"].get_ref<const std::string &>(), error)) return Failed(error);
        return Ok({{"slot", slot}, {"written", true}});
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
    if (path == "world.avatar_effect") return AvatarEffectCall(args);
    if (path == "player.effects_take") return EffectsTakeCall(args);
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
    if (path == "ui.choice_close") return ChoiceCloseCall(mod_id, args);
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
                             "save.account_list", "save.account_remove", "save.slots_list", "save.slot_player_export", "save.slot_player_import",
                             "party.room_hero_set", "party.room_hero_clear", "world.watch_set", "world.change_apply",
                             "player.down_rule_set", "player.revive", "player.give_up",
                             "world.avatar_set", "world.avatar_clear", "world.enter_map", "world.avatar_effect", "player.effects_take",
                             "monsters.mirror_get", "monsters.puppet_set", "monsters.puppet_clear", "monsters.mirror_hit",
                             "dungeons.set_seed", "dungeons.enter_floor", "dungeons.shared_events_get", "dungeons.shared_event_run",
                             "combat.strike_player", "items.owned", "georama.town_export", "georama.town_import", "ui.choice_open", "ui.choice_close",
                             "title.mode_register", "title.mode_set", "title.mode_remove"}) {
        config.api_capabilities.push_back({path, true, {}});
    }
}

void OwnerUnloaded(std::string_view mod_id) {
    UiOwnerUnloaded(mod_id);
    RegisterOwnerUnloaded(mod_id);
    DownOwnerUnloaded(mod_id);
    if (g_room_hero_owner == mod_id) { // the rule goes with the mod that set it
        SetRoomHero(std::nullopt);
        g_room_hero_owner.clear();
    }
    if (net::Shared().owner() == mod_id) net::Shared().Leave("the mod unloaded");
    g_player_extras.erase(std::string(mod_id));
}

void FrameStep() {
    net::Shared().Pump();
    TownRebuildStep();
    DownStep();
    RegisterStep();
    WorldStep();
    UiStep();
}

void ResetForTests() {
    RegisterReset();
    TownReset();
    DownReset();
    SetRoomHero(std::nullopt);
    g_room_hero_owner.clear();
    WorldReset();
    UiReset();
    g_player_extras.clear();
    if (net::Shared().state() != net::State::Idle) net::Shared().Leave();
}

} // namespace moonmod::coop
