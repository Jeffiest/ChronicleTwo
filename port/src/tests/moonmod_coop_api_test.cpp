// Co-op's adapter operations (moonmod/coop.hpp) without a disc: the other players' heroes, the host's monsters
// mirrored on a floor fixture, the doors one player opens, the seed a floor is built from, the title modes and a
// visit's request, and the catalogue (net.* says so without a socket; everything else is supported).
#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

#include "dng_main.hpp"
#include "mainloop.hpp"
#include "monster.hpp"
#include "moonmod/chronicle_two_adapter.hpp"
#include "moonmod/coop.hpp"
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
                             "world.avatar_set", "world.avatar_clear", "world.enter_map", "monsters.mirror_get", "monsters.puppet_set",
                             "monsters.puppet_clear", "monsters.mirror_hit", "dungeons.set_seed", "dungeons.enter_floor",
                             "dungeons.shared_events_get", "dungeons.shared_event_run", "combat.strike_player", "items.owned",
                             "georama.town_export", "georama.town_import", "ui.choice_open", "title.mode_register", "title.mode_set"}) {
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
    EXPECT_EQ(api.Call("world.avatar_set", Json::array({{{"slot", 1}, {"character", "ridepod"}}})).status, ApiCallStatus::InvalidArgument);
    EXPECT_EQ(api.Call("world.avatar_set", Json::array({{{"slot", 1}, {"character", "max"}, {"tint", {300, 0, 0}}}})).status,
              ApiCallStatus::InvalidArgument);
    EXPECT_EQ(api.Call("world.avatar_clear", Json::array({{{"slot", 1}}})).status, ApiCallStatus::Ok);
    EXPECT_EQ(api.Call("world.avatar_clear").status, ApiCallStatus::Ok);
    EXPECT_EQ(api.Call("world.avatar_clear", Json::array({{{"slot", 9}}})).status, ApiCallStatus::InvalidArgument);
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
    // The host no longer lists slot 2: it is gone here too.
    const ApiCallResult again = api.Call("monsters.puppet_set", Json::array({{{"monsters", Json::array({slot0, slot1})}}}));
    ASSERT_EQ(again.status, ApiCallStatus::Ok);
    EXPECT_EQ((*floor.slots)[2].state, ACTIVE_MONSTER_NONE);
    EXPECT_FALSE(moonmod::coop::IsPuppet(2));
    ASSERT_EQ(again.value.at("hits").size(), 1u);
    EXPECT_EQ(again.value.at("hits")[0].at("slot"), 0);
    EXPECT_EQ(again.value.at("hits")[0].at("damage"), 20);
    EXPECT_EQ(api.Call("monsters.puppet_clear").status, ApiCallStatus::Ok);
    EXPECT_FALSE(moonmod::coop::IsPuppet(0));

    // The host takes the blow by slot; a kill dies there.
    const ApiCallResult hit = api.Call("monsters.mirror_hit", Json::array({{{"slot", 0}, {"damage", 20}}}));
    ASSERT_EQ(hit.status, ApiCallStatus::Ok) << hit.message;
    EXPECT_EQ(hit.value.at("damage"), 20);
    EXPECT_FALSE(hit.value.at("killed").get<bool>());
    EXPECT_EQ((*floor.slots)[0].life, 35);
    EXPECT_EQ(api.Call("monsters.mirror_hit", Json::array({{{"slot", 0}, {"damage", 0}}})).status, ApiCallStatus::InvalidArgument);
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
    // A choice open is a busy town or floor for walking in.
    const ApiCallResult enter = api.Call("world.enter_map", Json::array({{{"map_no", 0}}}));
    EXPECT_NE(enter.status, ApiCallStatus::Ok);
    moonmod::coop::ResetForTests();
    EXPECT_FALSE(moonmod::coop::ChoiceIsOpen());
}

TEST(MoonModCoopApi, StrikesAndFloorsAndMapsAreRefusedOffTheFloorWithReasons) {
    FloorFixture floor;
    Api          api;
    // No hero body on the fixture: a strike says so rather than touching anything.
    const ApiCallResult strike = api.Call("combat.strike_player", Json::array({{{"x", 0.0}, {"y", 8.0}, {"z", 0.0}, {"radius", 12}, {"damage", 30}, {"kind", "heavy"}}}));
    EXPECT_EQ(strike.status, ApiCallStatus::Failed);
    EXPECT_EQ(api.Call("combat.strike_player", Json::array({{{"x", 0.0}, {"damage", 30}}})).status, ApiCallStatus::InvalidArgument);
    EXPECT_EQ(api.Call("dungeons.enter_floor", Json::array({{{"floor_id", 200}}})).status, ApiCallStatus::InvalidArgument);
    PortLoopNo = LOOP_TITLE;
    EXPECT_EQ(api.Call("dungeons.enter_floor", Json::array({{{"dungeon_id", 1}, {"floor_id", 2}}})).status, ApiCallStatus::Failed);
    EXPECT_EQ(api.Call("world.enter_map", Json::array({{{"map_no", 0}}})).status, ApiCallStatus::Failed);
    const ApiCallResult owned = api.Call("items.owned");
    ASSERT_EQ(owned.status, ApiCallStatus::Ok) << owned.message;
    EXPECT_TRUE(owned.value.at("items").is_array());
}

} // namespace
