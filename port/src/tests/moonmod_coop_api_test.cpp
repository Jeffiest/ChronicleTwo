// Co-op's adapter operations (moonmod/coop.hpp) without a disc: the other players' heroes, the host's monsters
// mirrored on a floor fixture, the doors one player opens, the seed a floor is built from, the title modes and a
// visit's request, and the catalogue (net.* says so without a socket; everything else is supported).
#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

#include "colprim.hpp"
#include "dng_main.hpp"
#include "event.hpp"
#include "event_func.hpp"
#include "gamedata.hpp"
#include "mainloop.hpp"
#include "monster.hpp"
#include "moonmod/chronicle_two_adapter.hpp"
#include "moonmod/coop.hpp"
#include "moonmod/coop_down.hpp"
#include "moonmod/coop_story.hpp"
#include "moonmod/coop_pad.hpp"
#include "moonmod/coop_save.hpp"
#include "moonmod/net_session.hpp"
#include "moonmod_monster_slots.hpp"
#include "platform/game_loop_state.hpp"
#include "savedata.hpp"
#include "scene.hpp"
#include "sceneevent.hpp"
#include "scenesnd.hpp"
#include "scoped_global_object.hpp"

namespace {

using moonmod::ApiCallResult;
using moonmod::ApiCallStatus;
using moonmod::Json;

struct Api {
    moonmod::RuntimeConfig config;
    Api() { moonmod::ConfigureChronicleTwoApi(config); }
    ApiCallResult Call(const char *path, const Json &args = Json::array(), const char *mod = "test.coop") const {
        const moonmod::ApiOperationSpec *operation = moonmod::FindApiOperation(path);
        if (operation == nullptr) return {ApiCallStatus::Unsupported, nullptr, std::string("not in the catalogue: ") + path};
        return config.api_handler(mod, *operation, args);
    }
    bool Supported(const char *path, std::string *reason = nullptr) const {
        for (const moonmod::ApiCapability &capability : config.api_capabilities) {
            if (capability.path == path) {
                if (reason != nullptr) *reason = capability.reason;
                return capability.supported;
            }
        }
        return false;
    }
};

// A dungeon floor without a disc: the loop, a scene with a few monsters bound, the manager and the battle area.
struct FloorFixture {
    int                                       previous_loop = PortLoopNo;
    CMonsterMan                              *previous_manager = ActiveMonster;
    CScene                                   *previous_scene = DngMainScene;
    DNG_BATTLE_AREA                          *previous_area = BattleAreaScene;
    CSaveData                                *previous_save = PortActiveSaveData;
    CUserDataManager                         *previous_user = DngUserData;
    CSaveDataDungeon                         *previous_dungeon_save = DngSaveDataDungeon;
    DNG_STATUS                                previous_status = DngStatus;
    std::unique_ptr<CScene>                   scene = std::make_unique<CScene>();
    std::unique_ptr<CMonsterMan>              manager = std::make_unique<CMonsterMan>();
    std::unique_ptr<CSaveData>                save = std::make_unique<CSaveData>();
    std::unique_ptr<moonmod_test::MonsterSlots> slots;

    FloorFixture() {
        save->Initialize();
        PortActiveSaveData = save.get();
        DngUserData = &save->user_data;
        DngSaveDataDungeon = &save->save_dungeon;
        slots = std::make_unique<moonmod_test::MonsterSlots>(*manager, *scene, std::initializer_list<int>{0, 1, 2});
        ActiveMonster = manager.get();
        DngMainScene = scene.get();
        BattleAreaScene = &scene->battle_area;
        DngStatus.mode = DNG_STATUS_FIELD;
        DngStatus.dungeon_no = 1;
        save->save_dungeon.stage_id = 1;
        save->save_dungeon.floor_id[1] = 4;
        PortLoopNo = LOOP_DUNGEON;
        moonmod::coop::ResetForTests();
    }
    ~FloorFixture() {
        moonmod::coop::ResetForTests();
        PortLoopNo = previous_loop;
        ActiveMonster = previous_manager;
        DngMainScene = previous_scene;
        BattleAreaScene = previous_area;
        PortActiveSaveData = previous_save;
        DngUserData = previous_user;
        DngSaveDataDungeon = previous_dungeon_save;
        DngStatus = previous_status;
    }
    CActiveMonster &Live(int slot, int kind, int life) {
        CActiveMonster &m = (*slots)[slot];
        m.state = ACTIVE_MONSTER_LIVE;
        m.monster_id = static_cast<s16>(kind);
        m.life = life;
        m.max_life = life;
        m.SetPosition(10.0f * static_cast<float>(slot), 0.0f, 5.0f);
        return m;
    }
};

TEST(MoonModCoopApi, TheCatalogueNamesEveryCoopOperationAndNetSaysWhetherItHasASocket) {
    Api api;
    for (const char *path : {"session.get", "session.start", "session.finish", "save.world_export", "save.player_export", "save.account_list",
                             "save.slots_list", "save.slot_player_export", "save.slot_player_import", "party.room_hero_set",
                             "party.room_hero_clear", "world.watch_set", "world.change_apply",
                             "world.avatar_set", "world.avatar_clear", "world.enter_map", "world.avatar_effect", "player.effects_take",
                             "monsters.mirror_get", "monsters.puppet_set",
                             "monsters.puppet_clear", "monsters.mirror_hit", "dungeons.set_seed", "dungeons.enter_floor",
                             "dungeons.shared_events_get", "dungeons.shared_event_run", "combat.strike_player", "items.owned",
                             "georama.town_export", "georama.town_import", "ui.choice_open", "ui.choice_close", "title.mode_register", "title.mode_set"}) {
        EXPECT_TRUE(api.Supported(path)) << path;
    }
    // mods.* are the runtime's own (every game has them), not the adapter's.
    EXPECT_TRUE(moonmod::IsCoreApiOperation("mods.list"));
    EXPECT_TRUE(moonmod::IsCoreApiOperation("mods.settings_overlay"));
    std::string reason;
    EXPECT_EQ(api.Supported("net.host", &reason), moonmod::net::Shared().available()) << reason;
    if (!moonmod::net::Shared().available()) EXPECT_NE(reason.find("no online play"), std::string::npos);
    const ApiCallResult status = api.Call("net.status");
    ASSERT_EQ(status.status, ApiCallStatus::Ok) << status.message;
    EXPECT_EQ(status.value.at("state"), "idle");
}

TEST(MoonModCoopApi, AvatarsTakeASlotOnTheFloorAndAreCleared) {
    FloorFixture floor;
    Api          api;
    const Json   set = Json::array({{{"slot", 1}, {"character", "monica"}, {"x", 30.0}, {"y", 0.0}, {"z", 5.0}, {"heading", 1.5},
                                     {"motion", 2}, {"tint", {64, 84, 128}}, {"name", "Monica"}}});
    const ApiCallResult shown = api.Call("world.avatar_set", set);
    ASSERT_EQ(shown.status, ApiCallStatus::Ok) << shown.message;
    EXPECT_EQ(shown.value.at("slot"), 1);
    // Without a disc the hero's model can't load: the wish is kept and nothing is drawn.
    EXPECT_FALSE(shown.value.at("shown").get<bool>());
    EXPECT_FALSE(moonmod::coop::AvatarShown(0));
    EXPECT_EQ(api.Call("world.avatar_set", Json::array({{{"slot", 4}, {"character", "max"}}})).status, ApiCallStatus::InvalidArgument);
    EXPECT_EQ(api.Call("world.avatar_set", Json::array({{{"slot", 1}, {"character", "steve"}}})).status, ApiCallStatus::InvalidArgument);
    // A player in the ridepod or a monster's form is shown as their hero (a stand-in), and one who is down is marked.
    EXPECT_EQ(api.Call("world.avatar_set", Json::array({{{"slot", 3}, {"character", "ridepod"}, {"x", 0.0}, {"y", 0.0}, {"z", 0.0}, {"heading", 0.0},
                                                         {"motion", 77}}})).status, ApiCallStatus::Ok);
    EXPECT_EQ(api.Call("world.avatar_set", Json::array({{{"slot", 3}, {"character", "monster"}, {"x", 0.0}, {"y", 0.0}, {"z", 0.0}, {"heading", 0.0},
                                                         {"down", true}}})).status, ApiCallStatus::Ok);
    EXPECT_EQ(api.Call("world.avatar_set", Json::array({{{"slot", 3}, {"character", "max"}, {"x", 0.0}, {"y", 0.0}, {"z", 0.0}, {"heading", 0.0},
                                                         {"down", 1}}})).status, ApiCallStatus::InvalidArgument);
    EXPECT_EQ(api.Call("world.avatar_set", Json::array({{{"slot", 1}, {"character", "max"}, {"tint", {300, 0, 0}}}})).status,
              ApiCallStatus::InvalidArgument);
    // What the other player's hero wears: item numbers, each left out at will; anything else is refused.
    const Json dressed = Json::array({{{"slot", 2}, {"character", "max"}, {"x", 0.0}, {"y", 0.0}, {"z", 0.0}, {"heading", 0.0}, {"weapon", 300},
                                       {"outfit", {{"head", 301}, {"clothes", 302}, {"sub", 304}}}}});
    EXPECT_EQ(api.Call("world.avatar_set", dressed).status, ApiCallStatus::Ok);
    EXPECT_EQ(api.Call("world.avatar_set", Json::array({{{"slot", 2}, {"character", "max"}, {"x", 0.0}, {"y", 0.0}, {"z", 0.0}, {"heading", 0.0},
                                                         {"outfit", {{"nose", 1}}}}})).status, ApiCallStatus::InvalidArgument);
    EXPECT_EQ(api.Call("world.avatar_set", Json::array({{{"slot", 2}, {"character", "max"}, {"x", 0.0}, {"y", 0.0}, {"z", 0.0}, {"heading", 0.0},
                                                         {"outfit", {{"head", 0}}}}})).status, ApiCallStatus::InvalidArgument);
    EXPECT_EQ(api.Call("world.avatar_clear", Json::array({{{"slot", 1}}})).status, ApiCallStatus::Ok);
    EXPECT_EQ(api.Call("world.avatar_clear").status, ApiCallStatus::Ok);
    EXPECT_EQ(api.Call("world.avatar_clear", Json::array({{{"slot", 9}}})).status, ApiCallStatus::InvalidArgument);
}

TEST(MoonModCoopApi, TheHerosShotsAreTakenOnceAndAnotherPlayersAreDrawnOnlyFromTheirAvatar) {
    FloorFixture  floor;
    Api           api;
    CActionChara *previous_hero = MainChara__2;
    // (only compared with, never touched: the hero is who fired)
    alignas(16) static unsigned char hero_storage[16];
    alignas(16) static unsigned char other_storage[16];
    MainChara__2 = reinterpret_cast<CActionChara *>(hero_storage);
    const float at[3] = {1.0f, 2.0f, 3.0f}, way[3] = {0.0f, 0.0f, 1.0f};
    moonmod::coop::HeroFired(hero_storage, moonmod::coop::HeroShot::Laser, at, way, 2);
    moonmod::coop::HeroFired(other_storage, moonmod::coop::HeroShot::Gun, at, way); // a monster's or a scene's: not the hero's
    moonmod::coop::HeroFired(hero_storage, moonmod::coop::HeroShot::Magic, at, way, 41, 0.75f, 25);
    const ApiCallResult taken = api.Call("player.effects_take");
    ASSERT_EQ(taken.status, ApiCallStatus::Ok) << taken.message;
    const Json &effects = taken.value.at("effects");
    ASSERT_EQ(effects.size(), 2u);
    EXPECT_EQ(effects[0].at("kind"), 4);
    EXPECT_EQ(effects[0].at("value"), 2);
    EXPECT_EQ(effects[0].at("z"), 3.0f);
    EXPECT_EQ(effects[1].at("kind"), 5);
    EXPECT_EQ(effects[1].at("value"), 41);
    EXPECT_EQ(effects[1].at("amount"), 0.75f);
    EXPECT_EQ(effects[1].at("target"), 25);
    EXPECT_TRUE(api.Call("player.effects_take").value.at("effects").empty()); // taken once
    MainChara__2 = previous_hero;

    // Another player's shot needs their avatar on this floor (none here without a disc) and a whole request.
    const Json shot = Json::array({{{"slot", 1}, {"kind", 1}, {"x", 0.0}, {"y", 0.0}, {"z", 0.0}, {"dx", 0.0}, {"dy", 0.0}, {"dz", 1.0}}});
    const ApiCallResult drawn = api.Call("world.avatar_effect", shot);
    ASSERT_EQ(drawn.status, ApiCallStatus::Ok) << drawn.message;
    EXPECT_FALSE(drawn.value.at("shown").get<bool>());
    EXPECT_EQ(api.Call("world.avatar_effect", Json::array({{{"slot", 1}, {"kind", 11}, {"x", 0.0}, {"y", 0.0}, {"z", 0.0}, {"dx", 0.0},
                                                            {"dy", 0.0}, {"dz", 1.0}}})).status, ApiCallStatus::InvalidArgument);
    // a looped sound (kind 10) too: off a floor it is not heard, and nothing is left going
    const ApiCallResult looped = api.Call("world.avatar_effect", Json::array({{{"slot", 1}, {"kind", 10}, {"x", 0.0}, {"y", 0.0}, {"z", 0.0},
                                                                               {"dx", 0.0}, {"dy", 0.0}, {"dz", 0.0}, {"value", 21}, {"amount", 4.0},
                                                                               {"frame0", "CH_020"}, {"frame1", "CH_020"}}}));
    ASSERT_EQ(looped.status, ApiCallStatus::Ok) << looped.message;
    EXPECT_FALSE(looped.value.at("shown").get<bool>());
    EXPECT_EQ(moonmod::coop::AvatarLoopsGoing(0), 0);
    // a sound of the other hero's (kind 9) names its bank; off a floor it is not heard
    const ApiCallResult heard = api.Call("world.avatar_effect", Json::array({{{"slot", 1}, {"kind", 9}, {"x", 0.0}, {"y", 0.0}, {"z", 0.0},
                                                                              {"dx", 0.0}, {"dy", 0.0}, {"dz", 0.0}, {"value", 17},
                                                                              {"frame0", "CH_005"}, {"frame1", "CH_005"}}}));
    ASSERT_EQ(heard.status, ApiCallStatus::Ok);
    EXPECT_FALSE(heard.value.at("shown").get<bool>());
    EXPECT_EQ(api.Call("world.avatar_effect", Json::array({{{"slot", 1}, {"kind", 1}, {"x", 0.0}}})).status, ApiCallStatus::InvalidArgument);
    EXPECT_EQ(api.Call("world.avatar_effect", Json::array({{{"slot", 1}, {"kind", 1}, {"x", 0.0}, {"y", 0.0}, {"z", 0.0}, {"dx", 0.0},
                                                            {"dy", 0.0}, {"dz", 1.0}, {"damage", 5}}})).status, ApiCallStatus::InvalidArgument);
}

TEST(MoonModCoopApi, ARidepodSaysItsFourPartsAndAFormItsMonster) {
    FloorFixture floor;
    Api          api;
    const auto   at = [](Json extra) {
        Json request = {{"slot", 1}, {"x", 0.0}, {"y", 0.0}, {"z", 0.0}, {"heading", 0.0}, {"name", "Host"}};
        for (auto field = extra.begin(); field != extra.end(); ++field) request[field.key()] = field.value();
        return Json::array({request});
    };
    // The ridepod's parts by item_no (leg, arm, body, pack), all four or none; a form by its monster_id.
    const ApiCallResult riding = api.Call("world.avatar_set", at({{"character", "ridepod"}, {"ridepod", {{"leg", 155}, {"arm", 145}, {"body", 135}, {"pack", 165}}}}));
    ASSERT_EQ(riding.status, ApiCallStatus::Ok) << riding.message;
    EXPECT_EQ(api.Call("world.avatar_set", at({{"character", "monster"}, {"form", 12}})).status, ApiCallStatus::Ok);
    EXPECT_EQ(api.Call("world.avatar_set", at({{"character", "ridepod"}, {"ridepod", nullptr}})).status, ApiCallStatus::Ok);
    EXPECT_EQ(api.Call("world.avatar_set", at({{"character", "ridepod"}, {"ridepod", {{"leg", 155}, {"arm", 145}, {"body", 135}}}})).status,
              ApiCallStatus::InvalidArgument);
    EXPECT_EQ(api.Call("world.avatar_set", at({{"character", "ridepod"}, {"ridepod", {{"leg", 155}, {"arm", 145}, {"body", 135}, {"pack", 0}}}})).status,
              ApiCallStatus::InvalidArgument);
    EXPECT_EQ(api.Call("world.avatar_set", at({{"character", "ridepod"}, {"ridepod", {{"leg", 155}, {"arm", 145}, {"body", 135}, {"pack", 165}, {"wheel", 1}}}})).status,
              ApiCallStatus::InvalidArgument);
    EXPECT_EQ(api.Call("world.avatar_set", at({{"character", "ridepod"}, {"ridepod", {155, 145, 135, 165}}})).status, ApiCallStatus::InvalidArgument);
    EXPECT_EQ(api.Call("world.avatar_set", at({{"character", "monster"}, {"form", -1}})).status, ApiCallStatus::InvalidArgument);
    EXPECT_EQ(api.Call("world.avatar_set", at({{"character", "monster"}, {"form", "frog"}})).status, ApiCallStatus::InvalidArgument);
    // A mod's notice in the game's own words: lines of about forty letters, three to a window, other letters coded.
    const std::vector<std::string> one = moonmod::coop::GameNoticeText("Monica is down! Stand by her and hold Revive to help her up.");
    ASSERT_EQ(one.size(), 1u);
    EXPECT_EQ(one[0], "Monica is down! Stand by her and hold\nRevive to help her up.");
    const std::vector<std::string> coded = moonmod::coop::GameNoticeText("Jos\xC3\xA9 [left]");
    ASSERT_EQ(coded.size(), 1u);
    EXPECT_EQ(coded[0], "Jos[UNI00e9] (left)");
    const std::vector<std::string> long_one = moonmod::coop::GameNoticeText(std::string(200, 'a') + " b c");
    ASSERT_FALSE(long_one.empty());
    for (const std::string &w : long_one) EXPECT_LE(w.size(), 127u);
    EXPECT_TRUE(moonmod::coop::GameNoticeText("").empty());
    // a question's window takes a longer line: a place's name is not broken from its floor's number
    const std::string asked = "Host went to Underground Channel, floor 2";
    EXPECT_NE(moonmod::coop::GameNoticeText(asked).at(0).find('\n'), std::string::npos);
    EXPECT_EQ(moonmod::coop::GameNoticeText(asked, 52).at(0), asked);
    // A motion as that player's game plays it (looping or held, at its pace), and the report's age on its clock.
    EXPECT_EQ(api.Call("world.avatar_set", at({{"character", "max"}, {"motion", 3}, {"motion_flags", 2}, {"motion_step", 0.5}, {"age", 6}})).status,
              ApiCallStatus::Ok);
    EXPECT_EQ(api.Call("world.avatar_set", at({{"character", "max"}, {"motion_flags", 8}})).status, ApiCallStatus::InvalidArgument);
    EXPECT_EQ(api.Call("world.avatar_set", at({{"character", "max"}, {"motion_step", 17.0}})).status, ApiCallStatus::InvalidArgument);
    EXPECT_EQ(api.Call("world.avatar_set", at({{"character", "max"}, {"motion_step", -1.0}})).status, ApiCallStatus::InvalidArgument);
    EXPECT_EQ(api.Call("world.avatar_set", at({{"character", "max"}, {"age", -2}})).status, ApiCallStatus::InvalidArgument);
    EXPECT_EQ(api.Call("world.avatar_set", at({{"character", "max"}, {"age", 4000}})).status, ApiCallStatus::InvalidArgument);
    // A shot told with an age is kept for its moment, but only for an avatar on this floor; a bad age is refused.
    const Json shot = {{"slot", 1}, {"kind", 1}, {"x", 0.0}, {"y", 0.0}, {"z", 0.0}, {"dx", 0.0}, {"dy", 0.0}, {"dz", 1.0}};
    Json       aged = shot;
    aged["age"] = 3;
    const ApiCallResult kept = api.Call("world.avatar_effect", Json::array({aged}));
    ASSERT_EQ(kept.status, ApiCallStatus::Ok) << kept.message;
    EXPECT_FALSE(kept.value.at("shown").get<bool>());
    aged["age"] = -1;
    EXPECT_EQ(api.Call("world.avatar_effect", Json::array({aged})).status, ApiCallStatus::InvalidArgument);
    // Without a disc nothing loads: no body, so no name over a head, and the names' draw has nothing to draw.
    float       head[2] = {}, feet[2] = {};
    std::string name;
    EXPECT_FALSE(moonmod::coop::AvatarNameOnScreen(0, head, feet, name));
    EXPECT_FALSE(moonmod::coop::AvatarNameOnScreen(7, head, feet, name));
    moonmod::coop::DrawAvatarNames();
    EXPECT_TRUE(moonmod::coop::AvatarsOnMap().empty()); // (nor on the floor's map)
    EXPECT_EQ(api.Call("world.avatar_clear").status, ApiCallStatus::Ok);
}

TEST(MoonModCoopApi, TheHostMirrorsItsMonstersAndAGuestFollowsThemAsPuppets) {
    FloorFixture floor;
    Api          api;
    floor.Live(0, 12, 80);
    floor.Live(2, 7, 30);
    const ApiCallResult mirror = api.Call("monsters.mirror_get");
    ASSERT_EQ(mirror.status, ApiCallStatus::Ok) << mirror.message;
    ASSERT_EQ(mirror.value.at("monsters").size(), 2u);
    const Json &first = mirror.value.at("monsters")[0];
    EXPECT_EQ(first.at("slot"), 0);
    EXPECT_EQ(first.at("kind"), 12);
    EXPECT_EQ(first.at("state"), ACTIVE_MONSTER_LIVE);
    EXPECT_EQ(first.at("hp"), 80);
    EXPECT_EQ(first.at("max_hp"), 80);
    EXPECT_NEAR(first.at("x").get<double>(), 0.0, 0.01);
    EXPECT_NEAR(first.at("z").get<double>(), 5.0, 0.01);
    EXPECT_TRUE(mirror.value.at("hits").empty());

    // A guest on the same floor: slots 0 and 2 follow the host's, slot 1 was never there.
    const Json slot0 = {{"slot", 0}, {"kind", 12}, {"state", 1}, {"hp", 55}, {"max_hp", 80}, {"x", 40.0}, {"y", 0.0}, {"z", 9.0}, {"heading", 0.5}, {"motion", 1}};
    const Json slot1 = {{"slot", 1}, {"kind", 3}, {"state", 1}, {"hp", 10}};
    const Json slot2 = {{"slot", 2}, {"kind", 7}, {"state", 1}, {"hp", 30}};
    const ApiCallResult followed = api.Call("monsters.puppet_set", Json::array({{{"monsters", Json::array({slot0, slot1, slot2})}}}));
    ASSERT_EQ(followed.status, ApiCallStatus::Ok) << followed.message;
    EXPECT_EQ(followed.value.at("followed"), Json::array({0, 2}));
    EXPECT_EQ(followed.value.at("refused"), Json::array({1}));
    EXPECT_TRUE(moonmod::coop::IsPuppet(0));
    EXPECT_TRUE(moonmod::coop::IsPuppet(2));
    EXPECT_EQ((*floor.slots)[0].life, 55);
    // A guest's blow on the copy goes to the host, and the copy keeps the host's life.
    moonmod::coop::MonsterDamaged(0, 20);
    EXPECT_EQ((*floor.slots)[0].life, 55);
    // One the copy guarded goes too, with how it landed, for the host's game to show it so.
    DAMAGE_PARAM row{};
    row.kind = DAMAGE_KIND_MONICA_MELEE;
    row.hit_flags = DAMAGE_HIT_STUN;
    row.stagger = 1;
    row.stun_time = 12;
    CColPrim prim;
    prim.param = &row;
    moonmod::coop::MonsterDamaged(0, 0, &prim, 1, true);
    moonmod::coop::MonsterDamaged(0, 0); // (a blow that took nothing and says nothing of how: not sent)
    // The host no longer lists slot 2: it is gone here too.
    const ApiCallResult again = api.Call("monsters.puppet_set", Json::array({{{"monsters", Json::array({slot0, slot1})}}}));
    ASSERT_EQ(again.status, ApiCallStatus::Ok);
    EXPECT_EQ((*floor.slots)[2].state, ACTIVE_MONSTER_NONE);
    EXPECT_FALSE(moonmod::coop::IsPuppet(2));
    ASSERT_EQ(again.value.at("hits").size(), 2u);
    EXPECT_EQ(again.value.at("hits")[0].at("slot"), 0);
    EXPECT_EQ(again.value.at("hits")[0].at("damage"), 20);
    EXPECT_FALSE(again.value.at("hits")[0].contains("kind"));
    const Json &told = again.value.at("hits")[1];
    EXPECT_EQ(told.at("damage"), 0);
    EXPECT_EQ(told.at("kind"), DAMAGE_KIND_MONICA_MELEE);
    EXPECT_EQ(told.at("element"), 1);
    EXPECT_EQ(told.at("flags"), DAMAGE_HIT_STUN);
    EXPECT_EQ(told.at("stagger"), 1);
    EXPECT_EQ(told.at("stun"), 12);
    EXPECT_TRUE(told.at("guard").get<bool>());
    EXPECT_TRUE(told.at("at").contains("y"));
    EXPECT_EQ(api.Call("monsters.puppet_clear").status, ApiCallStatus::Ok);
    EXPECT_FALSE(moonmod::coop::IsPuppet(0));

    // The host takes the blow by slot; a kill dies there.
    const ApiCallResult hit = api.Call("monsters.mirror_hit", Json::array({{{"slot", 0}, {"damage", 20}}}));
    ASSERT_EQ(hit.status, ApiCallStatus::Ok) << hit.message;
    EXPECT_EQ(hit.value.at("damage"), 20);
    EXPECT_FALSE(hit.value.at("killed").get<bool>());
    EXPECT_EQ((*floor.slots)[0].life, 35);
    EXPECT_EQ(api.Call("monsters.mirror_hit", Json::array({{{"slot", 0}, {"damage", 0}}})).status, ApiCallStatus::InvalidArgument);
    // A blow told with how it landed is taken as this game's own: the life goes, the stun holds it, and a knock-down
    // knocks it over. One that took nothing and was guarded leaves the life and has it guard.
    const Json landed = {{"slot", 0}, {"damage", 5}, {"kind", DAMAGE_KIND_MAX_MELEE}, {"element", 0}, {"flags", DAMAGE_HIT_KNOCKDOWN},
                         {"stagger", 1}, {"stun", 10}, {"at", {{"x", 1.0}, {"y", 8.0}, {"z", 0.0}}}};
    const ApiCallResult knocked = api.Call("monsters.mirror_hit", Json::array({landed}));
    ASSERT_EQ(knocked.status, ApiCallStatus::Ok) << knocked.message;
    EXPECT_EQ(knocked.value.at("damage"), 5);
    EXPECT_EQ((*floor.slots)[0].life, 30);
    EXPECT_EQ((*floor.slots)[0].damage_time, 10);
    EXPECT_EQ((*floor.slots)[0].req_prog, MONSTER_PROG_KNOCK);
    const ApiCallResult guarded = api.Call("monsters.mirror_hit", Json::array({{{"slot", 0}, {"damage", 0}, {"kind", DAMAGE_KIND_MAX_MELEE}, {"guard", true}}}));
    ASSERT_EQ(guarded.status, ApiCallStatus::Ok) << guarded.message;
    EXPECT_EQ(guarded.value.at("damage"), 0);
    EXPECT_EQ((*floor.slots)[0].life, 30);
    EXPECT_EQ((*floor.slots)[0].req_prog, MONSTER_PROG_GUARD);
    EXPECT_EQ(api.Call("monsters.mirror_hit", Json::array({{{"slot", 0}, {"damage", 5}, {"element", 4}}})).status, ApiCallStatus::InvalidArgument);
    EXPECT_EQ(api.Call("monsters.mirror_hit", Json::array({{{"slot", 0}, {"damage", 5}, {"at", 3}}})).status, ApiCallStatus::InvalidArgument);
    EXPECT_EQ(api.Call("monsters.mirror_hit", Json::array({{{"slot", 0}, {"damage", 5}, {"why", 1}}})).status, ApiCallStatus::InvalidArgument);
}

TEST(MoonModCoopApi, AFollowersMonstersTheLeaderNoLongerHasLeaveTheFloorsCountForItsClear) {
    FloorFixture floor;
    Api          api;
    floor.Live(0, 12, 80).chara_kind = ACTION_KIND_SCRIPT;
    floor.Live(1, 3, 10).chara_kind = ACTION_KIND_SCRIPT;
    floor.Live(2, 7, 30).chara_kind = ACTION_KIND_SCRIPT;
    ASSERT_EQ(ActiveMonster->GetMonsterNum(-1.0f), 3);
    const ApiCallResult before = api.Call("world.get");
    ASSERT_EQ(before.status, ApiCallStatus::Ok) << before.message;
    EXPECT_EQ(before.value.at("monsters_counted"), 3);
    EXPECT_FALSE(before.value.at("floor_cleared").get<bool>());

    // The leader lists slots 0 and 2: slot 1 (struck down there before this game came) goes here, slot and kind.
    const Json slot0 = {{"slot", 0}, {"kind", 12}, {"state", 1}, {"hp", 80}};
    const Json slot2 = {{"slot", 2}, {"kind", 7}, {"state", 1}, {"hp", 30}};
    ASSERT_EQ(api.Call("monsters.puppet_set", Json::array({{{"monsters", Json::array({slot0, slot2})}}})).status, ApiCallStatus::Ok);
    EXPECT_EQ((*floor.slots)[1].state, ACTIVE_MONSTER_NONE);
    EXPECT_EQ((*floor.slots)[1].chara_kind, ACTION_KIND_NONE);
    EXPECT_EQ(ActiveMonster->GetMonsterNum(-1.0f), 2);
    // The leader's copy of slot 2 dies and its slot empties there: the copy here leaves the count too.
    ASSERT_EQ(api.Call("monsters.puppet_set", Json::array({{{"monsters", Json::array({slot0})}}})).status, ApiCallStatus::Ok);
    EXPECT_EQ((*floor.slots)[2].chara_kind, ACTION_KIND_NONE);
    EXPECT_EQ(ActiveMonster->GetMonsterNum(-1.0f), 1);
    // The leader's floor is empty: so is this one, and the game's own rule (IsEventRun) now gives its clear.
    ASSERT_EQ(api.Call("monsters.puppet_set", Json::array({{{"monsters", Json::array()}}})).status, ApiCallStatus::Ok);
    EXPECT_EQ(ActiveMonster->GetMonsterNum(-1.0f), 0);
    EXPECT_EQ(api.Call("world.get").value.at("monsters_counted"), 0);
}

TEST(MoonModCoopApi, ACopyWhoseMonsterDiedThereDiesHereAsTheGamesOwnAndPaysTheHeroWhoKilledIt) {
    FloorFixture floor;
    Api          api;
    // (a hero on the floor: the copies follow the leader only where this game has one)
    struct Hero {
        CActionChara  body;
        CActionChara *previous = MainChara__2;
        Hero() { MainChara__2 = &body; }
        ~Hero() { MainChara__2 = previous; }
    } hero;
    floor.Live(0, 12, 80);
    floor.Live(1, 3, 10);
    const auto report = [&](int hp0, int state0, int hp1, int state1) {
        Json list = Json::array();
        if (state0 >= 0) list.push_back({{"slot", 0}, {"kind", 12}, {"state", state0}, {"hp", hp0}, {"x", 0.0}, {"y", 0.0}, {"z", 5.0}, {"heading", 0.0}});
        if (state1 >= 0) list.push_back({{"slot", 1}, {"kind", 3}, {"state", state1}, {"hp", hp1}, {"x", 10.0}, {"y", 0.0}, {"z", 5.0}, {"heading", 0.0}});
        return api.Call("monsters.puppet_set", Json::array({{{"monsters", list}, {"age", 0}}}));
    };
    ASSERT_EQ(report(80, ACTIVE_MONSTER_LIVE, 10, ACTIVE_MONSTER_LIVE).status, ApiCallStatus::Ok);
    moonmod::coop::WorldStep();
    ASSERT_TRUE(moonmod::coop::MonstersMirrored());
    EXPECT_TRUE(moonmod::coop::CopyRests(0));
    EXPECT_TRUE(moonmod::coop::CopyRests(1));
    EXPECT_FALSE(moonmod::coop::CopiesDying());

    // This game's hero strikes the copy in slot 0 (Monica's sword), and the leader's monster dies of it there.
    DAMAGE_PARAM row{};
    row.kind = DAMAGE_KIND_MONICA_MELEE;
    row.source_type = DAMAGE_SOURCE_MAIN_WEAPON;
    CColPrim prim;
    prim.param = &row;
    prim.attacker = 1;
    prim.status = 0x40;
    moonmod::coop::MonsterDamaged(0, 80, &prim);
    ASSERT_EQ(report(0, ACTIVE_MONSTER_LIVE, 10, ACTIVE_MONSTER_LIVE).status, ApiCallStatus::Ok);
    // It is shown 8 frames behind the leader's clock: it dies when it comes to that moment, not before.
    for (int frame = 0; frame < 7; ++frame) moonmod::coop::WorldStep();
    EXPECT_NE((*floor.slots)[0].damage_req, ACTION_DAMAGE_REQ_DEAD);
    EXPECT_FALSE(moonmod::coop::CopiesDying());
    moonmod::coop::WorldStep();
    moonmod::coop::WorldStep();
    // Now the game's own death plays it (the monsters think this frame, every living copy resting), and its orbs go to
    // the weapon that dealt the blow.
    EXPECT_EQ((*floor.slots)[0].damage_req, ACTION_DAMAGE_REQ_DEAD);
    EXPECT_EQ((*floor.slots)[0].life, 0);
    EXPECT_EQ((*floor.slots)[0].last_hit_chara, 1);
    EXPECT_EQ((*floor.slots)[0].last_hit_source, DAMAGE_SOURCE_MAIN_WEAPON);
    EXPECT_EQ((*floor.slots)[0].last_hit_kind, DAMAGE_KIND_MONICA_MELEE);
    EXPECT_EQ((*floor.slots)[0].last_hit_attr, 0x40u);
    EXPECT_TRUE(moonmod::coop::CopiesDying());
    EXPECT_FALSE(moonmod::coop::CopyRests(0));
    EXPECT_TRUE(moonmod::coop::CopyRests(1));
    // Not while the game holds its monsters itself (an event, a menu).
    DngStatus.mode = DNG_STATUS_EVENT;
    EXPECT_FALSE(moonmod::coop::CopiesDying());
    DngStatus.mode = DNG_STATUS_FIELD;

    // The leader's monster fading out there, then gone, changes nothing here: the death plays out here.
    ASSERT_EQ(report(0, ACTIVE_MONSTER_DEAD, 10, ACTIVE_MONSTER_LIVE).status, ApiCallStatus::Ok);
    ASSERT_EQ(report(0, -1, 10, ACTIVE_MONSTER_LIVE).status, ApiCallStatus::Ok);
    moonmod::coop::WorldStep();
    EXPECT_EQ((*floor.slots)[0].state, ACTIVE_MONSTER_LIVE);
    EXPECT_TRUE(moonmod::coop::IsPuppet(0));
    EXPECT_TRUE(moonmod::coop::CopiesDying());
    // Once the game has emptied its slot (the death played, the fade done) the copy is gone.
    (*floor.slots)[0].state = ACTIVE_MONSTER_NONE;
    moonmod::coop::WorldStep();
    EXPECT_FALSE(moonmod::coop::IsPuppet(0));
    EXPECT_FALSE(moonmod::coop::CopiesDying());

    // A monster the other player struck down (no blow from this game's hero): the game shares that kill between the
    // hero's weapons, as for an attacker of -1.
    ASSERT_EQ(report(0, -1, 0, ACTIVE_MONSTER_LIVE).status, ApiCallStatus::Ok);
    for (int frame = 0; frame < 9; ++frame) moonmod::coop::WorldStep();
    EXPECT_EQ((*floor.slots)[1].damage_req, ACTION_DAMAGE_REQ_DEAD);
    EXPECT_EQ((*floor.slots)[1].last_hit_chara, -1);
    EXPECT_EQ((*floor.slots)[1].last_hit_source, DAMAGE_SOURCE_NONE);
    EXPECT_TRUE(moonmod::coop::CopiesDying());
    EXPECT_EQ(api.Call("monsters.puppet_clear").status, ApiCallStatus::Ok);
    EXPECT_FALSE(moonmod::coop::CopiesDying());
    EXPECT_FALSE(moonmod::coop::CopyRests(1));
}

TEST(MoonModCoopApi, AMonsterTheOtherPlayerKillsSharesItsAbsorptionAsAKillAnotherDealt) {
    FloorFixture floor;
    Api          api;
    CActiveMonster &monster = floor.Live(2, 7, 30);
    monster.last_hit_chara = 0;
    monster.last_hit_source = DAMAGE_SOURCE_MAIN_WEAPON;
    const ApiCallResult hit = api.Call("monsters.mirror_hit", Json::array({{{"slot", 2}, {"damage", 30}, {"kind", DAMAGE_KIND_MAX_MELEE}}}));
    ASSERT_EQ(hit.status, ApiCallStatus::Ok) << hit.message;
    EXPECT_TRUE(hit.value.at("killed").get<bool>());
    EXPECT_EQ(monster.damage_req, ACTION_DAMAGE_REQ_DEAD);
    EXPECT_EQ(monster.last_hit_chara, -1);
    EXPECT_EQ(monster.last_hit_source, DAMAGE_SOURCE_NONE);
    EXPECT_EQ(monster.last_hit_kind, DAMAGE_KIND_MAX_MELEE);
}

TEST(MoonModCoopApi, AMonstersTurnLooksAtTheNearestPlayerAndTheSwapIsUndone) {
    FloorFixture floor;
    floor.Live(0, 1, 10);
    // No player body on this fixture (MainChara__2 is null): the swap is skipped and nothing breaks.
    moonmod::coop::MonsterThinkBegin(0);
    moonmod::coop::MonsterThinkEnd();
    float at[3] = {};
    EXPECT_FALSE(moonmod::coop::AvatarAt(0, at));
}

TEST(MoonModCoopApi, DoorsOnePlayerOpensAreQueuedForTheOthersOneAtATime) {
    FloorFixture floor;
    Api          api;
    CSceneEventData data{};
    data.event.point_no = 7;
    data.position[0] = 100.0f;
    data.position[2] = 200.0f;
    data.rotation[1] = 1.0f;
    moonmod::coop::MapEventStarted(0, data); // touched: not shared
    moonmod::coop::MapEventStarted(1, data); // the action button: shared
    const ApiCallResult events = api.Call("dungeons.shared_events_get");
    ASSERT_EQ(events.status, ApiCallStatus::Ok) << events.message;
    ASSERT_EQ(events.value.at("events").size(), 1u);
    const Json &e = events.value.at("events")[0];
    EXPECT_EQ(e.at("script"), 7);
    EXPECT_EQ(e.at("mode"), 1);
    EXPECT_NEAR(e.at("pos")[0].get<double>(), 100.0, 0.01);
    EXPECT_NEAR(e.at("dir")[1].get<double>(), 1.0, 0.01);
    EXPECT_TRUE(api.Call("dungeons.shared_events_get").value.at("events").empty()); // taken once
    const Json run = Json::array({{{"script", 7}, {"mode", 1}, {"ext", 0}, {"pos", {100.0, 0.0, 200.0}}, {"dir", {0.0, 1.0, 0.0}}}});
    ASSERT_EQ(api.Call("dungeons.shared_event_run", run).status, ApiCallStatus::Ok);
    EXPECT_TRUE(moonmod::coop::SharedEventWaiting());
    const ApiCallResult busy = api.Call("dungeons.shared_event_run", run);
    EXPECT_EQ(busy.status, ApiCallStatus::Failed);
    EXPECT_NE(busy.message.find("busy"), std::string::npos);
    EXPECT_EQ(api.Call("dungeons.shared_event_run", Json::array({{{"script", 7}}})).status, ApiCallStatus::InvalidArgument);
    // The floor fixture has no map: the waiting event finds no point there and is dropped, so the next may come.
    moonmod::coop::WorldStep();
    EXPECT_FALSE(moonmod::coop::SharedEventWaiting());
}

TEST(MoonModCoopApi, ASeedSetOnceBuildsTheNextFloorAndTheWorldReportsIt) {
    FloorFixture floor;
    Api          api;
    EXPECT_EQ(api.Call("dungeons.set_seed", Json::array({{{"seed", 424242}}})).status, ApiCallStatus::Ok);
    EXPECT_EQ(moonmod::coop::FloorSeed(17), 424242);
    EXPECT_EQ(moonmod::coop::FloorSeed(18), 18); // once
    EXPECT_EQ(api.Call("dungeons.set_seed", Json::array({Json::object()})).status, ApiCallStatus::Ok);
    EXPECT_EQ(moonmod::coop::FloorSeed(19), 19);
    EXPECT_EQ(api.Call("dungeons.set_seed", Json::array({{{"seed", -1}}})).status, ApiCallStatus::InvalidArgument);
    const ApiCallResult world = api.Call("world.get");
    ASSERT_EQ(world.status, ApiCallStatus::Ok) << world.message;
    EXPECT_EQ(world.value.at("floor_seed"), 19);
    EXPECT_EQ(world.value.at("map_no"), floor.save->map_no);
}

TEST(MoonModCoopApi, TheSaveSplitAndTheGuestsGoThroughTheApi) {
    FloorFixture floor;
    Api          api;
    moonmod::coop::SetAccountsDirectory(std::filesystem::temp_directory_path() / "moonmod-coop-api-test");
    moonmod::coop::AccountsClear();
    floor.save->user_data.money = 777;
    const ApiCallResult world = api.Call("save.world_export");
    ASSERT_EQ(world.status, ApiCallStatus::Ok) << world.message;
    EXPECT_TRUE(world.value.at("data").get<std::string>().starts_with("DCW2"));
    const ApiCallResult player = api.Call("save.player_export");
    ASSERT_EQ(player.status, ApiCallStatus::Ok);
    const std::string things = player.value.at("data").get<std::string>();
    EXPECT_TRUE(things.starts_with("DCP2"));
    // Imports belong to a visit alone.
    EXPECT_EQ(api.Call("save.player_import", Json::array({{{"data", things}}})).status, ApiCallStatus::Failed);
    // A mod's own things travel after the player's.
    ASSERT_EQ(api.Call("save.player_extra_set", Json::array({{{"data", {{"level", 3}}}}})).status, ApiCallStatus::Ok);
    EXPECT_NE(api.Call("save.player_export").value.at("data").get<std::string>().find("|DCX2"), std::string::npos);
    EXPECT_EQ(api.Call("save.player_extra_get").value.at("data").at("level"), 3);
    // The host's guests.
    ASSERT_EQ(api.Call("save.account_set", Json::array({{{"id", "guest-abcdef"}, {"name", "Monica"}, {"data", things}}})).status, ApiCallStatus::Ok);
    const ApiCallResult got = api.Call("save.account_get", Json::array({{{"id", "guest-abcdef"}}}));
    ASSERT_EQ(got.status, ApiCallStatus::Ok);
    EXPECT_EQ(got.value.at("name"), "Monica");
    EXPECT_EQ(got.value.at("data"), things);
    EXPECT_EQ(api.Call("save.account_list").value.at("accounts").size(), 1u);
    EXPECT_EQ(api.Call("save.account_get", Json::array({{{"id", "guest-nobody"}}})).value, Json::object());
    EXPECT_EQ(api.Call("save.account_set", Json::array({{{"id", "bad"}, {"data", things}}})).status, ApiCallStatus::InvalidArgument);
    EXPECT_TRUE(api.Call("save.account_remove", Json::array({{{"id", "guest-abcdef"}}})).value.at("removed").get<bool>());
    moonmod::coop::AccountsClear();
    std::error_code ignored;
    std::filesystem::remove_all(std::filesystem::temp_directory_path() / "moonmod-coop-api-test", ignored);
}

TEST(MoonModCoopApi, AModsSeedStartsTheLayoutTheChestsAndTheMonstersEachFromItsOwnNumber) {
    FloorFixture floor;
    Api          api;
    const auto seeded = [&api] { ASSERT_EQ(api.Call("dungeons.set_seed", Json::array({{{"seed", 99}}})).status, ApiCallStatus::Ok); };
    seeded();
    moonmod::coop::FloorBuildBegin();
    const int pick = rand(); // what a preset floor picks its layout with
    EXPECT_EQ(moonmod::coop::FloorSeed(5), 99); // and a random floor's own seed
    moonmod::coop::FloorPlacing(1);
    const int chests = rand();
    moonmod::coop::FloorPlacing(2);
    const int monsters = rand();
    EXPECT_NE(chests, monsters);
    // The same seed again, with other draws in between (another game's floor: its effects draw too).
    seeded();
    moonmod::coop::FloorBuildBegin();
    EXPECT_EQ(rand(), pick);
    for (int i = 0; i < 7; ++i) rand();
    moonmod::coop::FloorPlacing(1);
    EXPECT_EQ(rand(), chests);
    rand();
    moonmod::coop::FloorPlacing(2);
    EXPECT_EQ(rand(), monsters);
    EXPECT_EQ(api.Call("world.get").value.at("floor_seed"), 99);
    // With no seed from a mod the game's own stream is left alone.
    moonmod::coop::FloorBuildBegin();
    srand(1234);
    const int own = rand();
    srand(1234);
    moonmod::coop::FloorPlacing(1);
    moonmod::coop::FloorPlacing(2);
    EXPECT_EQ(rand(), own);
    EXPECT_FALSE(api.Call("world.get").value.contains("floor_seed"));
}

TEST(MoonModCoopApi, ARoomGivesThisGameOneHeroAndLeavesTheStorysPartyAlone) {
    FloorFixture      floor;
    Api               api;
    CUserDataManager &user = floor.save->user_data;
    const int         max = 1 << USER_CHARA_MAX, monica = 1 << USER_CHARA_MONICA, ridepod = 1 << USER_CHARA_ROBO;
    // Chapter 1: the story has Max alone.
    user.party_member = static_cast<u16>(max);
    user.chara_change = static_cast<u16>(max);
    user.InitCharaChangeMask();
    user.active_chr_no = USER_CHARA_MAX;
    EXPECT_EQ(user.GetNowPartyMember(), max);

    // The joiner's game plays Monica, before the story has her.
    ASSERT_EQ(api.Call("party.room_hero_set", Json::array({{{"hero", "monica"}}})).status, ApiCallStatus::Ok);
    EXPECT_EQ(user.GetNowPartyMember(), monica);
    EXPECT_EQ(user.GetEnableCharaChangeFlag(), static_cast<u32>(monica));
    EXPECT_EQ(user.party_member, max) << "the story's own party is not written to";
    EXPECT_EQ(user.chara_change, max);
    EXPECT_EQ(api.Call("world.get").value.at("room_hero"), "monica");
    {
        const moonmod::coop::StoryPartyScope story; // a scene asking who is in the party hears the story's answer
        EXPECT_EQ(user.GetNowPartyMember(), max);
    }
    EXPECT_EQ(user.GetNowPartyMember(), monica);
    // The hero played is Max still: the rule asks the floor for the change across, once, then waits.
    EXPECT_EQ(moonmod::coop::RoomHeroFloorChange(), DNG_CHANGE_UNIT_PARTNER);
    EXPECT_EQ(moonmod::coop::RoomHeroFloorChange(), -1);
    user.active_chr_no = USER_CHARA_MONICA;
    EXPECT_EQ(moonmod::coop::RoomHeroFloorChange(), -1);
    EXPECT_FALSE(moonmod::coop::RoomHeroTownChange());

    // The host's game plays Max; the ridepod is his when the story has it, and Monica stays out when it has her.
    ASSERT_EQ(api.Call("party.room_hero_set", Json::array({{{"hero", "max"}}})).status, ApiCallStatus::Ok);
    user.active_chr_no = USER_CHARA_MAX;
    EXPECT_EQ(user.GetNowPartyMember(), max);
    user.party_member = static_cast<u16>(max | monica | ridepod);
    user.chara_change = static_cast<u16>(max | monica | ridepod);
    EXPECT_EQ(user.GetNowPartyMember(), max | ridepod);
    EXPECT_EQ(user.GetEnableCharaChangeFlag(), static_cast<u32>(max | ridepod));
    EXPECT_EQ(user.CheckQuickChange(USER_CHARA_MONICA, nullptr), 0) << "the other hero cannot be changed to";
    EXPECT_EQ(moonmod::coop::RoomHeroFloorChange(), -1);

    EXPECT_EQ(api.Call("party.room_hero_set", Json::array({{{"hero", "steve"}}})).status, ApiCallStatus::InvalidArgument);
    EXPECT_EQ(api.Call("party.room_hero_set", Json::array({Json::object()})).status, ApiCallStatus::InvalidArgument);

    // Without the rule the party is the story's again.
    EXPECT_TRUE(api.Call("party.room_hero_clear").value.at("cleared").get<bool>());
    EXPECT_EQ(user.GetNowPartyMember(), max | monica | ridepod);
    EXPECT_FALSE(api.Call("world.get").value.contains("room_hero"));
    EXPECT_FALSE(api.Call("party.room_hero_clear").value.at("cleared").get<bool>());
}

TEST(MoonModCoopApi, TheSharedWorldIsToldAsItChangesAndTakenWithoutAnEcho) {
    FloorFixture floor;
    Api          api;
    CSaveData   &save = *floor.save;
    ASSERT_EQ(api.Call("world.watch_set", Json::array({{{"enabled", true}}})).status, ApiCallStatus::Ok);
    EXPECT_TRUE(moonmod::coop::RegisterWatched());
    // The floor as it stands when watching begins is where the games start from: the first step tells nothing.
    save.SetBitFlag(7, 1);
    moonmod::coop::RegisterStep();
    // A change the game makes afterwards differs from what is shared until the step that tells it.
    save.SetBitFlag(100, 1);
    save.short_flag[5] = 12;
    save.game_progress = 2;
    moonmod::coop::RegisterStep(); // (told to the watching mod; no mod runs here, so only the bookkeeping is seen)
    // Another game's changes are written and are not differences: a second step has nothing to tell.
    ASSERT_EQ(api.Call("world.change_apply", Json::array({{{"kind", "flag"}, {"id", 207}, {"value", true}}})).status, ApiCallStatus::Ok);
    ASSERT_EQ(api.Call("world.change_apply", Json::array({{{"kind", "flag"}, {"id", 100}, {"value", false}}})).status, ApiCallStatus::Ok);
    ASSERT_EQ(api.Call("world.change_apply", Json::array({{{"kind", "counter"}, {"id", 9}, {"value", -4}}})).status, ApiCallStatus::Ok);
    ASSERT_EQ(api.Call("world.change_apply", Json::array({{{"kind", "progress"}, {"id", 0}, {"value", 4}}})).status, ApiCallStatus::Ok);
    ASSERT_EQ(api.Call("world.change_apply", Json::array({{{"kind", "party"}, {"id", 0}, {"value", 3}}})).status, ApiCallStatus::Ok);
    ASSERT_EQ(api.Call("world.change_apply", Json::array({{{"kind", "recruit"}, {"id", 2}, {"value", {14, 3}}}})).status, ApiCallStatus::Ok);
    EXPECT_NE(save.GetBitFlag(207), 0);
    EXPECT_EQ(save.GetBitFlag(100), 0);
    EXPECT_EQ(save.short_flag[9], -4);
    EXPECT_EQ(save.game_progress, 4);
    EXPECT_EQ(save.user_data.party_member, 3);
    EXPECT_EQ(save.user_data.party_chara[2].chara_no, 14);
    EXPECT_EQ(save.user_data.party_chara[2].status, PARTY_CHARA_STATUS_JOINED) << "joined, and not walking with this player";
    // A floor's record is merged: what either game did on the floor stands.
    DNG_FLOOR_SAVE &record = save.save_dungeon.floor_info[40];
    record.flag = 0x1;
    record.kill_count = 9;
    record.fast_destroy_time = 5000;
    moonmod::coop::RegisterStep();
    const Json theirs = Json::array({0, 3000, 0, 0, 0, 1, 0x2, 4, 2});
    ASSERT_EQ(api.Call("world.change_apply", Json::array({{{"kind", "floor"}, {"id", 40}, {"value", theirs}}})).status, ApiCallStatus::Ok);
    EXPECT_EQ(record.flag, 0x3);
    EXPECT_EQ(record.kill_count, 9);
    EXPECT_EQ(record.visit_count, 2);
    EXPECT_EQ(record.fast_destroy_time, 3000);
    EXPECT_EQ(record.spheda_clear, 1);
    // What a scene gives one player it gives the other: told once, and added to the other's own bag.
    const int bread = 100;
    const int had = save.user_data.GetNumSameItem(bread);
    moonmod::coop::StoryGave(bread, 2); // (kept for the next step, which tells the watching mod)
    moonmod::coop::RegisterStep();
    EXPECT_EQ(save.user_data.GetNumSameItem(bread), had) << "telling gives nothing here";
    ASSERT_EQ(api.Call("world.change_apply", Json::array({{{"kind", "gift"}, {"id", bread}, {"value", 2}}})).status, ApiCallStatus::Ok);
    EXPECT_GE(save.user_data.GetNumSameItem(bread), had) << "the other game's gift lands in this bag";
    EXPECT_EQ(api.Call("world.change_apply", Json::array({{{"kind", "gift"}, {"id", 0}, {"value", 1}}})).status, ApiCallStatus::InvalidArgument);
    EXPECT_EQ(api.Call("world.change_apply", Json::array({{{"kind", "gift"}, {"id", bread}, {"value", 5000}}})).status, ApiCallStatus::InvalidArgument);
    // A dungeon's system script (a chest opening) gives the player's own: nothing is kept to tell.
    moonmod::coop::SystemScriptBegan();
    DngStatus.mode = DNG_STATUS_EVENT;
    moonmod::coop::StoryGave(bread, 1);
    DngStatus.mode = DNG_STATUS_FIELD;
    moonmod::coop::RegisterStep();
    // Georama parts built and waiting to be placed are the world's: either player places them.
    ASSERT_EQ(api.Call("world.change_apply", Json::array({{{"kind", "stock"}, {"id", 12}, {"value", 3}}})).status, ApiCallStatus::Ok);
    EXPECT_EQ(save.build_parts_num[12], 3);
    EXPECT_EQ(api.Call("world.change_apply", Json::array({{{"kind", "stock"}, {"id", 12}, {"value", -1}}})).status, ApiCallStatus::InvalidArgument);
    EXPECT_EQ(api.Call("world.change_apply", Json::array({{{"kind", "stock"}, {"id", SAVE_BUILD_PARTS_MAX}, {"value", 1}}})).status,
              ApiCallStatus::InvalidArgument);
    // What is not a change of the world is refused whole.
    EXPECT_EQ(api.Call("world.change_apply", Json::array({{{"kind", "flag"}, {"id", 5000}, {"value", true}}})).status, ApiCallStatus::InvalidArgument);
    EXPECT_EQ(api.Call("world.change_apply", Json::array({{{"kind", "flag"}, {"id", 5}, {"value", 1}}})).status, ApiCallStatus::InvalidArgument);
    EXPECT_EQ(api.Call("world.change_apply", Json::array({{{"kind", "gilda"}, {"id", 0}, {"value", 1}}})).status, ApiCallStatus::InvalidArgument);
    EXPECT_EQ(api.Call("world.change_apply", Json::array({{{"kind", "floor"}, {"id", 40}, {"value", {1, 2, 3}}}})).status, ApiCallStatus::InvalidArgument);
    EXPECT_EQ(api.Call("world.watch_set", Json::array({{{"enabled", false}}})).status, ApiCallStatus::Ok);
    EXPECT_FALSE(moonmod::coop::RegisterWatched());
}

TEST(MoonModCoopApi, UnderTheRuleAHeroGoesDownInsteadOfDyingAndIsRevivedOrGivesUp) {
    using namespace moonmod::coop;
    FloorFixture floor;
    Api          api;
    struct Hero { // a hero on the floor, with life of their own
        CActionChara *previous = MainChara__2;
        CActionChara  body;
        Hero() { MainChara__2 = &body; }
        ~Hero() {
            MainChara__2 = previous;
            GetBattleCharaInfo()->Initialize();
        }
    } hero;
    CUserDataManager &user = floor.save->user_data;
    user.SetActiveChrNo(USER_CHARA_MAX);
    CHARA_DATA *max = user.GetCharaDataPtr(USER_CHARA_MAX);
    ASSERT_NE(max, nullptr);
    max->hp.max = 100.0f;
    max->hp.now = 40.0f;
    CBattleCharaInfo *info = GetBattleCharaInfo();
    info->Initialize();
    info->SetChrNo(USER_CHARA_MAX);
    ASSERT_EQ(info->GetNowHp_i(), 40);
    // Without the rule a death is the game's own.
    EXPECT_FALSE(HeroFalls(MainChara__2));
    EXPECT_FALSE(HeroIsDown());
    EXPECT_EQ(api.Call("player.revive", Json::array({{{"hp", 10}}})).status, ApiCallStatus::Failed);
    EXPECT_EQ(api.Call("player.give_up", Json::array()).status, ApiCallStatus::Failed);
    ASSERT_EQ(api.Call("player.down_rule_set", Json::array({{{"enabled", true}}})).status, ApiCallStatus::Ok);
    EXPECT_TRUE(DownRule());
    // The hero's life runs out: the death plays, and the hero stays down.
    max->hp.now = 0.0f;
    EXPECT_TRUE(HeroFalls(MainChara__2));
    EXPECT_TRUE(HeroIsDown());
    EXPECT_TRUE(HeroHeldDown(MainChara__2));
    EXPECT_EQ(api.Call("player.get", Json::array()).value["down"], true);
    MainChara__2->muteki_time = 0;
    DownStep();
    EXPECT_GT(MainChara__2->muteki_time, 0) << "nothing strikes a hero who is down";
    // Another player revives them: life back, on their feet.
    EXPECT_EQ(api.Call("player.revive", Json::array({{{"hp", 0}}})).status, ApiCallStatus::InvalidArgument);
    ASSERT_EQ(api.Call("player.revive", Json::array({{{"hp", 7}}})).status, ApiCallStatus::Ok);
    EXPECT_FALSE(HeroIsDown());
    EXPECT_EQ(info->GetNowHp_i(), 7);
    EXPECT_EQ(MainChara__2->prog_no, ACTION_PROG_MAIN);
    EXPECT_EQ(api.Call("player.get", Json::array()).value["down"], false);
    // Down again, and giving up: the game's own death event is raised.
    max->hp.now = 0.0f;
    EXPECT_TRUE(HeroFalls(MainChara__2));
    DngMainScene->battle_area.script.event_no = -1;
    ASSERT_EQ(api.Call("player.give_up", Json::array()).status, ApiCallStatus::Ok);
    EXPECT_FALSE(HeroIsDown());
    EXPECT_EQ(DngMainScene->battle_area.script.event_no, 1200);
    // The rule turned off while the hero lies down: the game's own death goes on, as when its mod goes away.
    max->hp.now = 0.0f;
    EXPECT_TRUE(HeroFalls(MainChara__2));
    DngMainScene->battle_area.script.event_no = -1;
    ASSERT_EQ(api.Call("player.down_rule_set", Json::array({{{"enabled", false}}})).status, ApiCallStatus::Ok);
    EXPECT_FALSE(DownRule());
    EXPECT_FALSE(HeroIsDown());
    EXPECT_EQ(DngMainScene->battle_area.script.event_no, 1200);
    // The rule goes with the mod that set it.
    EXPECT_FALSE(HeroFalls(MainChara__2));
    DngMainScene->battle_area.script.event_no = -1;
}

TEST(MoonModCoopApi, ATownTakenOffScreenIsBuiltWhenNextWalkedInto) {
    using namespace moonmod::coop;
    FloorFixture floor; // a dungeon floor runs: no town is on screen
    Api          api;
    EXPECT_FALSE(TownOnScreen(0));
    EXPECT_FALSE(TownRebuildPending());
    // A town taken while its player is elsewhere changes the record alone.
    const Json town = api.Call("georama.town_export", Json::array({{{"town_id", 2}}})).value;
    const Json taken = api.Call("georama.town_import", Json::array({{{"town_id", 2}, {"data", town["data"]}}})).value;
    EXPECT_EQ(taken["rebuilding"], false);
    EXPECT_EQ(taken["reloaded"], false);
    EXPECT_FALSE(TownRebuildPending());
    // A rebuild wanted for a town that is no longer on screen is dropped: the town loads from its record.
    TownRebuildWanted(2);
    EXPECT_TRUE(TownRebuildPending());
    // While it is pending the town's live map is not written over its record (it lacks the other player's change).
    EXPECT_FALSE(TownRecordWritable(2));
    EXPECT_TRUE(TownRecordWritable(1)) << "another town's map is";
    TownBeforeSave(); // (no live map to bring up to the record)
    EXPECT_TRUE(TownRebuildPending());
    TownRebuildStep();
    EXPECT_FALSE(TownRebuildPending());
    EXPECT_TRUE(TownRecordWritable(2));
    TownLiveSync(); // and no editor runs, so nothing is written
}

TEST(MoonModCoopApi, TitleModesArePagesAModDrivesAndAVisitStartsFromAnOpenOne) {
    FloorFixture floor;
    Api          api;
    PortLoopNo = LOOP_TITLE;
    const Json page = Json::array({{{"id", "coop"}, {"label", "Online Co-op"}, {"title", "ONLINE CO-OP"}, {"lines", {"Join a friend."}},
                                    {"entries", Json::array({{{"id", "join"}, {"label", "Join"}}, {{"id", "clear"}, {"label", "Clear"}}})}}});
    const ApiCallResult registered = api.Call("title.mode_register", page);
    ASSERT_EQ(registered.status, ApiCallStatus::Ok) << registered.message;
    EXPECT_EQ(registered.value.at("id"), "test.coop:coop");
    EXPECT_EQ(moonmod::coop::TitleExtraRows(), 1);
    EXPECT_EQ(api.Call("title.mode_set", Json::array({{{"id", "coop"}, {"cursor", 1}}})).value.at("cursor"), 1);
    EXPECT_EQ(api.Call("title.mode_set", Json::array({{{"id", "coop"}}}), "another.mod").status, ApiCallStatus::Failed);
    // A visit needs the mod's page open.
    const Json visit = Json::array({{{"visit", {{"world", api.Call("save.world_export").value.at("data")}, {"map", 0}}}}});
    EXPECT_EQ(api.Call("session.start", visit).status, ApiCallStatus::Failed);
    ASSERT_TRUE(moonmod::coop::TitleOpenRow(0));
    EXPECT_EQ(api.Call("session.get").value.at("open_mode"), "test.coop:coop");
    EXPECT_EQ(api.Call("session.get").value.at("kind"), "title");
    EXPECT_EQ(moonmod::coop::TitlePageStep(moonmod::coop::TitlePageInput::Down), moonmod::coop::TitlePageOutcome::Stay);
    EXPECT_EQ(moonmod::coop::OpenTitleMode()->cursor, 0u); // from 1, round to 0
    EXPECT_EQ(api.Call("session.start", Json::array({{{"visit", {{"world", "DCW1xx"}, {"map", 0}}}}})).status, ApiCallStatus::InvalidArgument);
    // A place is a town's area (map) or a map's own number (map_no); one of them is needed.
    const Json world_text = api.Call("save.world_export").value.at("data");
    EXPECT_EQ(api.Call("session.start", Json::array({{{"visit", {{"world", world_text}}}}})).status, ApiCallStatus::InvalidArgument);
    EXPECT_EQ(api.Call("session.start", Json::array({{{"visit", {{"world", world_text}, {"map_no", -3}}}}})).status, ApiCallStatus::InvalidArgument);
    EXPECT_EQ(api.Call("session.start", Json::array({{{"visit", {{"world", world_text}, {"map_no", 10}, {"door", 1}}}}})).status, ApiCallStatus::InvalidArgument);
    const ApiCallResult queued = api.Call("session.start", visit);
    ASSERT_EQ(queued.status, ApiCallStatus::Ok) << queued.message;
    EXPECT_TRUE(queued.value.at("new_account").get<bool>());
    EXPECT_TRUE(moonmod::coop::VisitRequestPending());
    EXPECT_EQ(moonmod::coop::TitlePageStep(moonmod::coop::TitlePageInput::None), moonmod::coop::TitlePageOutcome::StartVisit);
    EXPECT_EQ(api.Call("session.start", visit).status, ApiCallStatus::Failed); // already starting
    EXPECT_EQ(api.Call("session.finish").status, ApiCallStatus::Failed);       // nothing running yet
    // Back on a root page closes it, and nothing may start from it any more.
    EXPECT_EQ(moonmod::coop::TitlePageStep(moonmod::coop::TitlePageInput::Back), moonmod::coop::TitlePageOutcome::StartVisit);
    moonmod::coop::ResetForTests();
    ASSERT_TRUE(api.Call("title.mode_register", page).status == ApiCallStatus::Ok);
    ASSERT_TRUE(moonmod::coop::TitleOpenRow(0));
    EXPECT_EQ(moonmod::coop::TitlePageStep(moonmod::coop::TitlePageInput::Back), moonmod::coop::TitlePageOutcome::ReturnToTitle);
    EXPECT_FALSE(moonmod::coop::TitlePageOpen());
    EXPECT_EQ(api.Call("title.mode_remove", Json::array({{{"id", "coop"}}})).status, ApiCallStatus::Ok);
    EXPECT_EQ(moonmod::coop::TitleExtraRows(), 0);
}

// Review R6 and R7: the story's progress only goes forward (each game keeps the higher), and a townsperson's state
// is shared without the in-party bit, which says who walks with this game's own player.
TEST(MoonModCoopApi, TheRegisterKeepsTheHigherProgressAndEachGamesOwnPartyBit) {
    FloorFixture floor;
    Api          api;
    CSaveData   &save = *floor.save;
    ASSERT_EQ(api.Call("world.watch_set", Json::array({{{"enabled", true}}})).status, ApiCallStatus::Ok);
    save.game_progress = 6;
    moonmod::coop::RegisterStep();
    ASSERT_EQ(api.Call("world.change_apply", Json::array({{{"kind", "progress"}, {"id", 0}, {"value", 4}}})).status, ApiCallStatus::Ok);
    EXPECT_EQ(save.game_progress, 6) << "a game behind does not take this one's story back";
    ASSERT_EQ(api.Call("world.change_apply", Json::array({{{"kind", "progress"}, {"id", 0}, {"value", 9}}})).status, ApiCallStatus::Ok);
    EXPECT_EQ(save.game_progress, 9);
    // Townsperson 3 walks with this game's player; the other game's says recruited and housed, and in its party.
    save.user_data.party_chara[3].chara_no = 14;
    save.user_data.party_chara[3].status = PARTY_CHARA_STATUS_JOINED | PARTY_CHARA_STATUS_IN_PARTY;
    const int theirs = PARTY_CHARA_STATUS_IN_HOUSE | PARTY_CHARA_STATUS_JOINED | PARTY_CHARA_STATUS_IN_PARTY;
    ASSERT_EQ(api.Call("world.change_apply", Json::array({{{"kind", "recruit"}, {"id", 3}, {"value", {14, theirs}}}})).status, ApiCallStatus::Ok);
    EXPECT_EQ(save.user_data.party_chara[3].status, PARTY_CHARA_STATUS_IN_HOUSE | PARTY_CHARA_STATUS_JOINED | PARTY_CHARA_STATUS_IN_PARTY)
        << "still walking here";
    // Townsperson 4 walks with nobody here; theirs walks with the other player: here they are recruited, not in the party.
    save.user_data.party_chara[4].chara_no = 15;
    save.user_data.party_chara[4].status = 0;
    ASSERT_EQ(api.Call("world.change_apply", Json::array({{{"kind", "recruit"}, {"id", 4}, {"value", {15, PARTY_CHARA_STATUS_JOINED | PARTY_CHARA_STATUS_IN_PARTY}}}})).status,
              ApiCallStatus::Ok);
    EXPECT_EQ(save.user_data.party_chara[4].status, PARTY_CHARA_STATUS_JOINED);
    // The player here takes townsperson 4 into the party: that is not a change of the world, so nothing is told.
    save.user_data.party_chara[4].status |= PARTY_CHARA_STATUS_IN_PARTY;
    moonmod::coop::RegisterStep();
    ASSERT_EQ(api.Call("world.watch_set", Json::array({{{"enabled", false}}})).status, ApiCallStatus::Ok);
}

// Review R9: an item the game gives is told to the other game only when the story moved (a flag, a counter or the
// progress) within 300 frames of it: a story scene's gift. A chest, a shop or a trade moves no story and stays here.
TEST(MoonModCoopApi, AGiftIsToldOnlyWhenTheStoryMovesBesideIt) {
    FloorFixture floor;
    Api          api;
    CSaveData   &save = *floor.save;
    ASSERT_EQ(api.Call("world.watch_set", Json::array({{{"enabled", true}}})).status, ApiCallStatus::Ok);
    moonmod::coop::RegisterStep();
    moonmod::coop::StoryGave(100, 1); // a chest in a town: no flag goes with it
    for (int i = 0; i < 302; ++i) moonmod::coop::RegisterStep();
    EXPECT_EQ(moonmod::coop::RegisterGiftsTold(), 0);
    save.SetBitFlag(300, 1); // ...and a story flag set long after it does not bring it back
    moonmod::coop::RegisterStep();
    EXPECT_EQ(moonmod::coop::RegisterGiftsTold(), 0);
    for (int i = 0; i < 302; ++i) moonmod::coop::RegisterStep(); // (well clear of that flag)
    // A scene that gives an item and sets its flag a few frames later: told.
    moonmod::coop::StoryGave(101, 1);
    for (int i = 0; i < 20; ++i) moonmod::coop::RegisterStep();
    EXPECT_EQ(moonmod::coop::RegisterGiftsTold(), 0) << "held while the story may still move";
    save.SetBitFlag(301, 1);
    moonmod::coop::RegisterStep();
    EXPECT_EQ(moonmod::coop::RegisterGiftsTold(), 1);
    // A scene that sets its flag first and then gives: told at once.
    save.short_flag[7] = 3;
    moonmod::coop::RegisterStep();
    moonmod::coop::StoryGave(102, 2);
    moonmod::coop::RegisterStep();
    EXPECT_EQ(moonmod::coop::RegisterGiftsTold(), 2);
    ASSERT_EQ(api.Call("world.watch_set", Json::array({{{"enabled", false}}})).status, ApiCallStatus::Ok);
}

// Job 2: a monster the leader has and this game does not (gained after the floor was built) is made here in the
// leader's slot. A kind this game cannot make is refused for that slot only; the others still follow.
TEST(MoonModCoopApi, AMonsterThisGameCannotMakeIsRefusedForItsSlotOnly) {
    FloorFixture floor;
    Api          api;
    floor.Live(0, 12, 80);
    floor.Live(2, 7, 30);
    const Json slot0 = {{"slot", 0}, {"kind", 12}, {"state", 1}, {"hp", 80}};
    const Json slot1 = {{"slot", 1}, {"kind", 32000}, {"state", 1}, {"hp", 10}}; // no such kind in this game
    const Json slot2 = {{"slot", 2}, {"kind", 7}, {"state", 1}, {"hp", 30}};
    const ApiCallResult followed = api.Call("monsters.puppet_set", Json::array({{{"monsters", Json::array({slot0, slot1, slot2})}}}));
    ASSERT_EQ(followed.status, ApiCallStatus::Ok) << followed.message;
    EXPECT_EQ(followed.value.at("followed"), Json::array({0, 2}));
    EXPECT_EQ(followed.value.at("refused"), Json::array({1}));
    EXPECT_EQ((*floor.slots)[1].state, ACTIVE_MONSTER_NONE) << "nothing half made in the refused slot";
    // The leader's monster in slot 1 already struck down there: nothing to make, and nothing refused.
    const Json gone = {{"slot", 1}, {"kind", 32000}, {"state", ACTIVE_MONSTER_DEAD}, {"hp", 0}};
    const ApiCallResult dead = api.Call("monsters.puppet_set", Json::array({{{"monsters", Json::array({slot0, gone, slot2})}}}));
    ASSERT_EQ(dead.status, ApiCallStatus::Ok) << dead.message;
    EXPECT_TRUE(dead.value.at("refused").empty());
    // A slot here holding another kind than the leader's, whose monster is gone there: freed here too.
    floor.Live(1, 5, 20).chara_kind = ACTION_KIND_SCRIPT;
    ASSERT_EQ(api.Call("monsters.puppet_set", Json::array({{{"monsters", Json::array({slot0, gone, slot2})}}})).status, ApiCallStatus::Ok);
    EXPECT_EQ((*floor.slots)[1].state, ACTIVE_MONSTER_NONE);
    EXPECT_EQ((*floor.slots)[1].chara_kind, ACTION_KIND_NONE);
}

// Review R11: a mod closes its own choice panel quietly; another mod's panel stays open.
TEST(MoonModCoopApi, AModClosesItsOwnChoicePanelAndNoOneElses) {
    FloorFixture floor;
    Api          api;
    const Json open = Json::array({{{"id", "ask"}, {"title", "Join?"}, {"options", Json::array({{{"title", "Yes"}}, {{"title", "No"}}})}}});
    ASSERT_EQ(api.Call("ui.choice_open", open).status, ApiCallStatus::Ok);
    const ApiCallResult other = api.Call("ui.choice_close", Json::array({Json::object()}), "another.mod");
    ASSERT_EQ(other.status, ApiCallStatus::Ok) << other.message;
    EXPECT_FALSE(other.value.at("closed").get<bool>());
    EXPECT_TRUE(moonmod::coop::ChoiceIsOpen()) << "another mod's panel stays";
    const ApiCallResult own = api.Call("ui.choice_close", Json::array({Json::object()}));
    ASSERT_EQ(own.status, ApiCallStatus::Ok) << own.message;
    EXPECT_TRUE(own.value.at("closed").get<bool>());
    EXPECT_FALSE(moonmod::coop::ChoiceIsOpen());
    EXPECT_FALSE(moonmod::coop::PanelHoldsPad());
    EXPECT_FALSE(api.Call("ui.choice_close").value.at("closed").get<bool>()) << "nothing open: nothing closed";
    EXPECT_EQ(api.Call("ui.choice_close", Json::array({{{"id", "ask"}}})).status, ApiCallStatus::InvalidArgument);
    // Closed quietly, the mod may open the next one at once.
    EXPECT_EQ(api.Call("ui.choice_open", open).status, ApiCallStatus::Ok);
    moonmod::coop::ResetForTests();
}

TEST(MoonModCoopApi, AChoicePanelOpensOverTheFloorAndAnswersThroughTheOwner) {
    FloorFixture floor;
    Api          api;
    const Json open = Json::array({{{"id", "coop_together"}, {"title", "Join whom?"}, {"cancel", true}, {"town", true},
                                    {"options", Json::array({{{"title", "Max"}, {"subtitle", "Palm Brinks"}}, {{"title", "Stay here"}}})}}});
    const ApiCallResult opened = api.Call("ui.choice_open", open);
    ASSERT_EQ(opened.status, ApiCallStatus::Ok) << opened.message;
    EXPECT_EQ(opened.value.at("id"), "test.coop:coop_together");
    ASSERT_TRUE(moonmod::coop::ChoiceIsOpen());
    EXPECT_EQ(moonmod::coop::OpenChoice()->options.size(), 2u);
    EXPECT_EQ(api.Call("ui.choice_open", open).status, ApiCallStatus::Failed); // one at a time
    EXPECT_EQ(api.Call("ui.choice_open", Json::array({{{"id", "x"}, {"title", "t"}, {"options", Json::array()}}})).status, ApiCallStatus::InvalidArgument);
    // While it is open the pad's buttons are the panel's: the game's own reads see none of them.
    GamePad__2.pad[0].button = PAD_CROSS;
    GamePad__2.previous_pad[0].button = 0;
    EXPECT_EQ(GamePad__2.Down(PAD_CROSS), 0);
    EXPECT_EQ(GamePad__2.On(PAD_CROSS), 0);
    EXPECT_TRUE(moonmod::coop::PanelHoldsPad());
    // A choice open is a busy town or floor for walking in.
    const ApiCallResult enter = api.Call("world.enter_map", Json::array({{{"map_no", 0}}}));
    EXPECT_NE(enter.status, ApiCallStatus::Ok);
    moonmod::coop::ResetForTests();
    EXPECT_FALSE(moonmod::coop::ChoiceIsOpen());
    EXPECT_FALSE(moonmod::coop::PanelHoldsPad());
    EXPECT_NE(GamePad__2.On(PAD_CROSS), 0) << "with the panel gone the buttons are the game's again";
    GamePad__2.pad[0].button = 0;
}

TEST(MoonModCoopApi, AYesOrNoIsAskedInTheGamesOwnWindowOnAFloorAndChosenLeftAndRight) {
    FloorFixture floor;
    Api          api;
    const Json ask = Json::array({{{"id", "follow"}, {"title", "Monica went to Underground Channel, floor 3"}, {"subtitle", "Join them there?"},
                                   {"style", "yes_no"}, {"options", Json::array({{{"title", "Join Monica"}}, {{"title", "Stay here"}}})}}});
    const ApiCallResult opened = api.Call("ui.choice_open", ask);
    ASSERT_EQ(opened.status, ApiCallStatus::Ok) << opened.message;
    ASSERT_TRUE(moonmod::coop::ChoiceIsOpen());
    EXPECT_TRUE(moonmod::coop::OpenChoice()->yes_no);
    EXPECT_EQ(moonmod::coop::OpenChoice()->cursor, 0u) << "Yes first";
    // (past the grace that keeps the press that opened it from answering it) right is No, left Yes again
    GamePad__2.pad[0].button = 0;
    GamePad__2.previous_pad[0].button = 0;
    for (int i = 0; i < 9; ++i) moonmod::coop::ChoiceStep();
    GamePad__2.pad[0].button = PAD_RIGHT;
    moonmod::coop::ChoiceStep();
    EXPECT_EQ(moonmod::coop::OpenChoice()->cursor, 1u);
    GamePad__2.previous_pad[0].button = PAD_RIGHT;
    GamePad__2.pad[0].button = PAD_LEFT;
    moonmod::coop::ChoiceStep();
    EXPECT_EQ(moonmod::coop::OpenChoice()->cursor, 0u);
    GamePad__2.pad[0].button = 0;
    GamePad__2.previous_pad[0].button = 0;
    moonmod::coop::ResetForTests();
    // Three options, or no style that is known: refused; at the title it is the panel.
    Json three = ask;
    three[0]["options"].push_back({{"title", "Later"}});
    EXPECT_EQ(api.Call("ui.choice_open", three).status, ApiCallStatus::InvalidArgument);
    Json odd = ask;
    odd[0]["style"] = "bubble";
    EXPECT_EQ(api.Call("ui.choice_open", odd).status, ApiCallStatus::InvalidArgument);
    PortLoopNo = LOOP_TITLE;
    ASSERT_EQ(api.Call("ui.choice_open", ask).status, ApiCallStatus::Ok);
    EXPECT_FALSE(moonmod::coop::OpenChoice()->yes_no) << "the title has no message window of the game's: the panel";
    moonmod::coop::ResetForTests();
}

// A question waits out a scene, as the game's own never break into one: unseen, the buttons the game's (the scene's
// lines go on), and opening the game's menu no longer answers it. Once the player walks free it is asked, after a
// grace, so the press that ended the scene does not answer it.
TEST(MoonModCoopApi, AQuestionWaitsOutASceneAndTheMenuAndIsAskedAfter) {
    FloorFixture floor;
    Api          api;
    const Json ask = Json::array({{{"id", "follow"}, {"title", "Max went to Palm Brinks"}, {"subtitle", "Join them there?"},
                                   {"style", "yes_no"}, {"options", Json::array({{{"title", "Join Max"}}, {{"title", "Stay here"}}})}}});
    BattleAreaScene->script.running = 1; // a floor's scene runs
    ASSERT_EQ(api.Call("ui.choice_open", ask).status, ApiCallStatus::Ok);
    EXPECT_TRUE(moonmod::coop::OpenChoice()->yes_no);
    EXPECT_FALSE(moonmod::coop::PanelHoldsPad()) << "in a scene the buttons are the game's";
    GamePad__2.pad[0].button = PAD_CIRCLE;
    GamePad__2.previous_pad[0].button = 0;
    EXPECT_NE(GamePad__2.On(PAD_CIRCLE), 0);
    for (int i = 0; i < 30; ++i) moonmod::coop::ChoiceStep();
    ASSERT_TRUE(moonmod::coop::ChoiceIsOpen()) << "a press in the scene answers nothing";
    DngStatus.mode = DNG_STATUS_MENU; // the floor's menu opened: still waiting, not answered
    for (int i = 0; i < 30; ++i) moonmod::coop::ChoiceStep();
    ASSERT_TRUE(moonmod::coop::ChoiceIsOpen()) << "opening the menu does not answer it";
    EXPECT_FALSE(moonmod::coop::PanelHoldsPad());
    DngStatus.mode = DNG_STATUS_FIELD;
    BattleAreaScene->script.running = 0; // the scene is over; the press that ended it is still down
    moonmod::coop::ChoiceStep();
    EXPECT_TRUE(moonmod::coop::PanelHoldsPad()) << "free again: the question is asked and has the buttons";
    for (int i = 0; i < 7; ++i) moonmod::coop::ChoiceStep();
    EXPECT_TRUE(moonmod::coop::ChoiceIsOpen()) << "within the grace no press answers it";
    GamePad__2.pad[0].button = 0;
    GamePad__2.previous_pad[0].button = 0;
    moonmod::coop::ChoiceStep();
    GamePad__2.pad[0].button = PAD_RIGHT;
    moonmod::coop::ChoiceStep();
    EXPECT_EQ(moonmod::coop::OpenChoice()->cursor, 1u) << "then left and right choose";
    GamePad__2.pad[0].button = 0;
    GamePad__2.previous_pad[0].button = 0;
    moonmod::coop::ResetForTests();
    EXPECT_FALSE(moonmod::coop::PanelHoldsPad());
}

TEST(MoonModCoopApi, AStoryScenePlayingIsToldAndAPlayerWatchingOneIsAwayOnTheOtherScreens) {
    FloorFixture floor;
    Api          api;
    const int    saved_skip = EdEventInfo.skip_state, saved_button = EdEventInfo.skip_button;
    auto story = [&] { return api.Call("world.get").value.at("story_scene").get<bool>(); };
    EXPECT_FALSE(moonmod::coop::StoryScenePlaying());
    EXPECT_FALSE(story());
    DngStatus.mode = DNG_STATUS_EVENT; // a talk or a door has the floor: no scene offers its skip
    EXPECT_FALSE(moonmod::coop::StoryScenePlaying()) << "a talk is not one of the story's scenes";
    InitDramaScene(); // one of the story's scenes begins: the game offers its skip
    EXPECT_TRUE(moonmod::coop::StoryScenePlaying());
    EXPECT_TRUE(story());
    moonmod::coop::StoryScenesStep();
    EXPECT_TRUE(moonmod::coop::StoryScenePlaying()) << "the scene goes on";
    DngStatus.mode = DNG_STATUS_FIELD;
    BattleAreaScene->script.running = 1; // (a floor's own scene script)
    EXPECT_TRUE(moonmod::coop::StoryScenePlaying());
    BattleAreaScene->script.running = 0; // over, its skip left offered: the field is no scene
    EXPECT_FALSE(moonmod::coop::StoryScenePlaying());
    EXPECT_FALSE(story());
    moonmod::coop::StoryScenesStep(); // (the player walks free: that scene is over for good)
    DngStatus.mode = DNG_STATUS_EVENT; // a talk after it, the skip still offered
    EXPECT_FALSE(moonmod::coop::StoryScenePlaying()) << "a skip a scene left offered does not make a talk one";
    DngStatus.mode = DNG_STATUS_FIELD;
    EdEventInfo.skip_state = saved_skip;
    EdEventInfo.skip_button = saved_button;
    // Another player watching one in their game: their avatar is away (a flag; nothing is drawn without a disc).
    const Json away = Json::array({{{"slot", 1}, {"character", "max"}, {"x", 0.0}, {"y", 0.0}, {"z", 0.0}, {"heading", 0.0}, {"away", true}}});
    EXPECT_EQ(api.Call("world.avatar_set", away).status, ApiCallStatus::Ok);
    EXPECT_EQ(moonmod::coop::AvatarSeen(0), 0.0f);
    EXPECT_EQ(moonmod::coop::AvatarSeen(9), 0.0f);
    EXPECT_EQ(api.Call("world.avatar_set", Json::array({{{"slot", 1}, {"character", "max"}, {"x", 0.0}, {"y", 0.0}, {"z", 0.0}, {"heading", 0.0},
                                                         {"away", "yes"}}})).status, ApiCallStatus::InvalidArgument);
    EXPECT_EQ(api.Call("world.avatar_clear").status, ApiCallStatus::Ok);
}

TEST(MoonModCoopApi, StrikesAndFloorsAndMapsAreRefusedOffTheFloorWithReasons) {
    FloorFixture floor;
    Api          api;
    // No hero body on the fixture: a strike says so rather than touching anything.
    const ApiCallResult strike = api.Call("combat.strike_player", Json::array({{{"x", 0.0}, {"y", 8.0}, {"z", 0.0}, {"radius", 12}, {"damage", 30}, {"kind", "heavy"}}}));
    EXPECT_EQ(strike.status, ApiCallStatus::Failed);
    EXPECT_EQ(api.Call("combat.strike_player", Json::array({{{"x", 0.0}, {"damage", 30}}})).status, ApiCallStatus::InvalidArgument);
    // How the blow was dealt (the monster's damage row) is read and checked before the hero is looked for.
    const Json dealt = {{"x", 0.0}, {"y", 8.0}, {"z", 0.0}, {"damage", 30}, {"flags", DAMAGE_HIT_KNOCKDOWN}, {"stagger", 1}, {"stun", 8},
                        {"guard_rate", 30}, {"status", WEAPON_SPECIAL_POISON}, {"toward", {{"x", 1.0}, {"z", 0.0}}}};
    EXPECT_EQ(api.Call("combat.strike_player", Json::array({dealt})).status, ApiCallStatus::Failed);
    for (const char *bad : {"flags", "stagger", "stun", "guard_rate", "status"}) {
        Json wrong = dealt;
        wrong[bad] = 5000000;
        EXPECT_EQ(api.Call("combat.strike_player", Json::array({wrong})).status, ApiCallStatus::InvalidArgument) << bad;
    }
    Json no_way = dealt;
    no_way["toward"] = 1;
    EXPECT_EQ(api.Call("combat.strike_player", Json::array({no_way})).status, ApiCallStatus::InvalidArgument);
    EXPECT_EQ(api.Call("dungeons.enter_floor", Json::array({{{"floor_id", 200}}})).status, ApiCallStatus::InvalidArgument);
    PortLoopNo = LOOP_TITLE;
    EXPECT_EQ(api.Call("dungeons.enter_floor", Json::array({{{"dungeon_id", 1}, {"floor_id", 2}}})).status, ApiCallStatus::Failed);
    EXPECT_EQ(api.Call("world.enter_map", Json::array({{{"map_no", 0}}})).status, ApiCallStatus::Failed);
    const ApiCallResult owned = api.Call("items.owned");
    ASSERT_EQ(owned.status, ApiCallStatus::Ok) << owned.message;
    EXPECT_TRUE(owned.value.at("items").is_array());
}

} // namespace
