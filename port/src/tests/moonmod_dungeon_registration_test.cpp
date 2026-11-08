#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>

#include "automap.hpp"
#include "dng_main.hpp"
#include "dngfloor.hpp"
#include "mainloop.hpp"
#include "platform/game_loop_state.hpp"
#include "mg_memory.hpp"
#include "moonmod/chronicle_two_adapter.hpp"
#include "scene.hpp"
#include "savedata.hpp"
#include "savedatadungeon.hpp"

extern CSaveData *PortActiveSaveData asm("MainLoop_ActiveSaveData");

namespace {
struct ActiveDungeonSaveScope {
    CSaveData *previous = PortActiveSaveData;
    std::unique_ptr<CSaveData> save = std::make_unique<CSaveData>();

    ActiveDungeonSaveScope() {
        PortActiveSaveData = save.get();
        save->save_dungeon.Initialize();
    }

    ~ActiveDungeonSaveScope() {
        ResetMoonModDungeonSaveExtension(&save->save_dungeon);
        PortActiveSaveData = previous;
    }
};
} // namespace

TEST(MoonModDungeonRegistration, AddedFloorUsesNativeGraphAndTransitionQueries) {
    ActiveDungeonSaveScope active_save;
    moonmod::ResetChronicleTwoContentForTests();
    CDngFloorManager manager;
    manager.Initialize();
    alignas(64) u_long128 arena_storage[1024]{};
    mgCMemory arena;
    arena.stSetBuffer(arena_storage, 1024);
    char dungeon_map[] =
        "TREE_INFO 6,3,3;"
        "GI 1,1,1,0,0; RI 0,0,0; RI_LINK -1,-1,-1,1; RI_TITLE 0,\"Entrance\";"
        "GI 0,2,1,0,0; RT 0,0,0;"
        "GI 1,3,1,0,0; RI 1,1,0; RI_LINK -1,-1,0,-1; RI_TITLE 1,\"Source Floor\";";
    manager.AnalyzeFile(dungeon_map, static_cast<int>(std::strlen(dungeon_map)), &arena);
    ASSERT_EQ(manager.glid_num, 3);
    ASSERT_NE(manager.GetDngMapFloorInfo(1), nullptr);

    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    const moonmod::ApiOperationSpec *content_register_operation = moonmod::FindApiOperation("content.register");
    const moonmod::ApiOperationSpec *content_unregister_operation = moonmod::FindApiOperation("content.unregister");
    const moonmod::ApiOperationSpec *content_list_operation = moonmod::FindApiOperation("content.list");
    const moonmod::ApiOperationSpec *register_operation = moonmod::FindApiOperation("dungeons.register");
    const moonmod::ApiOperationSpec *legacy_register_operation = moonmod::FindApiOperation("world.register_dungeon");
    const moonmod::ApiOperationSpec *list_operation = moonmod::FindApiOperation("dungeons.list");
    const moonmod::ApiOperationSpec *get_operation = moonmod::FindApiOperation("dungeons.get");
    const moonmod::ApiOperationSpec *transition_operation = moonmod::FindApiOperation("world.transition");
    ASSERT_NE(content_register_operation, nullptr);
    ASSERT_NE(content_unregister_operation, nullptr);
    ASSERT_NE(content_list_operation, nullptr);
    ASSERT_NE(register_operation, nullptr);
    ASSERT_NE(legacy_register_operation, nullptr);
    ASSERT_NE(list_operation, nullptr);
    ASSERT_NE(get_operation, nullptr);
    ASSERT_NE(transition_operation, nullptr);
    const moonmod::Json definition = {
        {"id", "moon-vault"},
        {"name", "Moon Vault"},
        {"dungeon", 0},
        {"floors", moonmod::Json::array({
                        {{"id", "annex"}, {"name", "Moon Vault Annex"}, {"source_floor", 1},
                         {"flags", moonmod::Json::array({"exit", "boss"})}, {"seal", "max"}},
                        {{"id", "gallery"}, {"name", "Moon Vault Gallery"}, {"source_floor", "annex"},
                         {"flags", moonmod::Json::array({"sub"})}, {"seal", "monica"}},
                    })},
    };
    const moonmod::Json forward_reference = {
        {"id", "invalid-vault"},
        {"name", "Invalid Vault"},
        {"dungeon", 0},
        {"floors", moonmod::Json::array({
                        {{"id", "gallery"}, {"source_floor", "annex"}},
                        {{"id", "annex"}, {"source_floor", 1}},
                    })},
    };
    EXPECT_EQ(config.api_handler("test.moonmod.dungeon-probe", *register_operation,
                                 moonmod::Json::array({forward_reference})).status,
              moonmod::ApiCallStatus::InvalidArgument);
    const moonmod::Json invalid_roles = {
        {"id", "invalid-roles"}, {"name", "Invalid Roles"}, {"dungeon", 0},
        {"floors", moonmod::Json::array({{{"id", "annex"}, {"source_floor", 1},
                                             {"flags", moonmod::Json::array({"start"})}}})},
    };
    EXPECT_EQ(config.api_handler("test.moonmod.dungeon-probe", *register_operation,
                                 moonmod::Json::array({invalid_roles})).status,
              moonmod::ApiCallStatus::InvalidArgument);
    const moonmod::Json invalid_seal = {
        {"id", "invalid-seal"}, {"name", "Invalid Seal"}, {"dungeon", 0},
        {"floors", moonmod::Json::array({{{"id", "annex"}, {"source_floor", 1}, {"seal", "party"}}})},
    };
    EXPECT_EQ(config.api_handler("test.moonmod.dungeon-probe", *register_operation,
                                 moonmod::Json::array({invalid_seal})).status,
              moonmod::ApiCallStatus::InvalidArgument);
    EXPECT_EQ(config.api_handler("test.moonmod.dungeon-probe", *legacy_register_operation,
                                 moonmod::Json::array({invalid_seal})).status,
              moonmod::ApiCallStatus::InvalidArgument);
    const moonmod::ApiCallResult registration = config.api_handler(
        "test.moonmod.dungeon-probe", *content_register_operation,
        moonmod::Json::array({{{"type", "dungeon"}, {"definition", definition}}}));
    ASSERT_EQ(registration.status, moonmod::ApiCallStatus::Ok) << registration.message;
    EXPECT_EQ(registration.value.value("id", ""), "test.moonmod.dungeon-probe:moon-vault");
    ASSERT_EQ(registration.value.at("floors").size(), 2u);
    EXPECT_EQ(registration.value.at("floors")[0].value("status", ""), "pending");
    EXPECT_EQ(registration.value.at("floors")[1].value("source_floor", ""), "annex");
    EXPECT_EQ(registration.value.at("floors")[1].value("status", ""), "pending");
    EXPECT_EQ(registration.value.at("floors")[0].at("flags"), moonmod::Json::array({"exit", "boss"}));
    EXPECT_EQ(registration.value.at("floors")[0].value("seal", ""), "max");
    EXPECT_EQ(registration.value.at("floors")[1].at("flags"), moonmod::Json::array({"sub"}));
    EXPECT_EQ(registration.value.at("floors")[1].value("seal", ""), "monica");

    const moonmod::ApiCallResult pending_list = config.api_handler(
        "test.moonmod.dungeon-probe", *list_operation, moonmod::Json::array());
    ASSERT_EQ(pending_list.status, moonmod::ApiCallStatus::Ok) << pending_list.message;
    ASSERT_EQ(pending_list.value.size(), 1u);
    EXPECT_EQ(pending_list.value[0].at("id"), "test.moonmod.dungeon-probe:moon-vault");
    EXPECT_EQ(pending_list.value[0].at("floors")[0].at("status"), "pending");
    const moonmod::ApiCallResult generic_list = config.api_handler(
        "test.moonmod.dungeon-probe", *content_list_operation, moonmod::Json::array());
    ASSERT_EQ(generic_list.status, moonmod::ApiCallStatus::Ok) << generic_list.message;
    ASSERT_EQ(generic_list.value.value("count", 0), 1);
    EXPECT_EQ(generic_list.value.at("entries")[0].value("content_type", ""), "dungeon");
    const moonmod::ApiCallResult pending_get = config.api_handler(
        "test.moonmod.dungeon-probe", *get_operation,
        moonmod::Json::array({"test.moonmod.dungeon-probe:moon-vault"}));
    ASSERT_EQ(pending_get.status, moonmod::ApiCallStatus::Ok) << pending_get.message;
    EXPECT_EQ(pending_get.value.at("floors")[0].at("status"), "pending");
    EXPECT_EQ(config.api_handler("test.moonmod.dungeon-probe", *get_operation,
                                 moonmod::Json::array({"missing-vault"})).status,
              moonmod::ApiCallStatus::Failed);

    moonmod::ApplyChronicleTwoDungeons(&manager);
    const moonmod::ApiCallResult unregister_materialized = config.api_handler(
        "test.moonmod.dungeon-probe", *content_unregister_operation,
        moonmod::Json::array({{{"type", "dungeon"}, {"id", "moon-vault"}}}));
    EXPECT_EQ(unregister_materialized.status, moonmod::ApiCallStatus::Failed);
    EXPECT_NE(unregister_materialized.message.find("already materialized"), std::string::npos);
    ASSERT_EQ(manager.glid_num, 7);
    DNGMAP_ROOM_INFO *added = nullptr;
    for (int i = 0; i < manager.glid_num; ++i) {
        if (manager.glid_info[i].type == GLID_TYPE_ROOM && manager.glid_info[i].room.title != nullptr &&
            std::strcmp(manager.glid_info[i].room.title, "Moon Vault Annex") == 0) {
            added = &manager.glid_info[i].room;
            break;
        }
    }
    ASSERT_NE(added, nullptr);
    EXPECT_EQ(added->floor_id, 2);
    EXPECT_EQ(added->order, 2);
    EXPECT_NE(added->flag & DNGMAP_ROOM_FLAG_EXIT, 0);
    EXPECT_NE(added->flag & DNGMAP_ROOM_FLAG_BOSS, 0);
    EXPECT_EQ(added->seal, DNGMAP_SEAL_MAX);
    DNGMAP_ROOM_INFO *gallery_room = manager.GetDngMapFloorInfo(3);
    ASSERT_NE(gallery_room, nullptr);
    EXPECT_NE(gallery_room->flag & DNGMAP_ROOM_FLAG_SUB, 0);
    EXPECT_EQ(gallery_room->seal, DNGMAP_SEAL_MONICA);
    EXPECT_TRUE(manager.IsMoonModFloor(2, "Moon Vault Annex"));
    EXPECT_TRUE(manager.IsMoonModFloor(3, "Moon Vault Gallery"));
    EXPECT_NE(manager.GetDngMapFloorInfo(2)->flag & DNGMAP_ROOM_FLAG_EXIT, 0);
    EXPECT_EQ(manager.GetDngMapFloorInfo(2)->seal, DNGMAP_SEAL_MAX);
    EXPECT_FALSE(manager.IsMoonModFloor(2, "Unrelated Vanilla Floor"));
    EXPECT_EQ(manager.GetDngMapFloorInfo(2), added);
    EXPECT_EQ(moonmod::ChronicleTwoDungeonFloorSource(0, 2), 1);
    EXPECT_EQ(moonmod::ChronicleTwoDungeonFloorSource(0, 3), 1);
    TRESURE_BOX_FLOOR_INFO treasure_table{};
    treasure_table.floor[1].group_num = 2;
    treasure_table.floor[1].group_id[0] = 17;
    treasure_table.floor[1].group_id[1] = 25;
    ASSERT_TRUE(moonmod::InheritChronicleTwoTreasureFloor(&treasure_table, 0, 2));
    EXPECT_EQ(treasure_table.floor[2].group_num, 2);
    EXPECT_EQ(treasure_table.floor[2].group_id[0], 17);
    EXPECT_EQ(treasure_table.floor[2].group_id[1], 25);
    ASSERT_TRUE(moonmod::InheritChronicleTwoTreasureFloor(&treasure_table, 0, 3));
    EXPECT_EQ(treasure_table.floor[3].group_num, 2);
    EXPECT_EQ(treasure_table.floor[3].group_id[0], 17);
    EXPECT_EQ(treasure_table.floor[3].group_id[1], 25);
    EXPECT_EQ(manager.GetDngMapNextFloorID(1, 0), 2);
    EXPECT_EQ(manager.GetDngMapNextFloorID(2, 0), 3);
    const moonmod::ApiCallResult registered_get = config.api_handler(
        "test.moonmod.dungeon-probe", *get_operation,
        moonmod::Json::array({"test.moonmod.dungeon-probe:moon-vault"}));
    ASSERT_EQ(registered_get.status, moonmod::ApiCallStatus::Ok) << registered_get.message;
    EXPECT_EQ(registered_get.value.at("floors")[0].at("status"), "registered");
    EXPECT_EQ(registered_get.value.at("floors")[0].at("native_floor_id"), 2);
    GLID_INFO *next = manager.GetNextRoom(1, GLID_DIR_RIGHT, nullptr, -1, nullptr);
    ASSERT_NE(next, nullptr);
    EXPECT_EQ(next->type, GLID_TYPE_ROOM);
    EXPECT_EQ(next->room.floor_id, 2);
    GLID_INFO *gallery = manager.GetNextRoom(2, GLID_DIR_RIGHT, nullptr, -1, nullptr);
    ASSERT_NE(gallery, nullptr);
    EXPECT_EQ(gallery->type, GLID_TYPE_ROOM);
    EXPECT_EQ(gallery->room.floor_id, 3);

    struct RestoreTransitionState {
        int loop = PortLoopNo;
        int next_loop = PortNextLoopNo;
        INIT_LOOP_ARG next_arg = NextInitArg;
        DNG_STATUS dungeon_status = DngStatus;
        CScene *scene = GetMainScene();
        int exit_flag = scene == nullptr ? 0 : scene->exit_flag;
        ~RestoreTransitionState() {
            PortLoopNo = loop;
            PortNextLoopNo = next_loop;
            NextInitArg = next_arg;
            DngStatus = dungeon_status;
            if (scene != nullptr) scene->exit_flag = exit_flag;
        }
    } restore_transition_state;
    ASSERT_NE(restore_transition_state.scene, nullptr);
    PortLoopNo = LOOP_DUNGEON;
    PortNextLoopNo = LOOP_MENU;
    NextInitArg = INIT_LOOP_ARG{};
    restore_transition_state.scene->exit_flag = false;
    DngStatus.dungeon_no = 0;
    DngStatus.mode = DNG_STATUS_EVENT;
    active_save.save->save_dungeon.stage_id = 0;
    active_save.save->save_dungeon.floor_id[0] = 1;

    const std::string annex_id = registered_get.value.at("floors")[0].at("id").get<std::string>();
    const moonmod::ApiCallResult event_floor_transition = config.api_handler(
        "test.moonmod.dungeon-probe", *transition_operation, moonmod::Json::array({{{"id", annex_id}}}));
    ASSERT_EQ(event_floor_transition.status, moonmod::ApiCallStatus::Ok) << event_floor_transition.message;
    EXPECT_TRUE(event_floor_transition.value.value("queued", false));
    EXPECT_EQ(PortNextLoopNo, LOOP_DUNGEON);
    EXPECT_EQ(NextInitArg.floor_no, 2);
    EXPECT_TRUE(restore_transition_state.scene->exit_flag);
    EXPECT_EQ(moonmod::ChronicleTwoRequestedDungeonEntryFloor(1), 1);
    active_save.save->save_dungeon.floor_id[0] = 2;
    EXPECT_EQ(moonmod::ChronicleTwoRequestedDungeonEntryFloor(1), 2);
    EXPECT_EQ(moonmod::ChronicleTwoRequestedDungeonEntryFloor(1), 1);

    PortNextLoopNo = LOOP_MENU;
    restore_transition_state.scene->exit_flag = false;
    DngStatus.mode = DNG_STATUS_FIELD;
    active_save.save->save_dungeon.floor_id[0] = 1;
    const moonmod::ApiCallResult floor_transition = config.api_handler(
        "test.moonmod.dungeon-probe", *transition_operation, moonmod::Json::array({{{"id", annex_id}}}));
    ASSERT_EQ(floor_transition.status, moonmod::ApiCallStatus::Ok) << floor_transition.message;
    EXPECT_EQ(floor_transition.value.value("native_floor_id", -1), 2);
    EXPECT_TRUE(floor_transition.value.value("queued", false));
    EXPECT_EQ(PortNextLoopNo, LOOP_DUNGEON);
    EXPECT_EQ(NextInitArg.map_no, 0);
    EXPECT_EQ(NextInitArg.floor_no, 2);
    EXPECT_EQ(NextInitArg.event_no, 0);
    EXPECT_TRUE(restore_transition_state.scene->exit_flag);

    active_save.save->save_dungeon.floor_id[0] = 2;
    EXPECT_EQ(moonmod::ChronicleTwoRequestedDungeonEntryFloor(1), 2);
    active_save.save->save_dungeon.floor_id[0] = 2;
    const moonmod::ApiCallResult already_there = config.api_handler(
        "test.moonmod.dungeon-probe", *transition_operation, moonmod::Json::array({{{"id", annex_id}}}));
    ASSERT_EQ(already_there.status, moonmod::ApiCallStatus::Ok) << already_there.message;
    EXPECT_FALSE(already_there.value.value("queued", true));
    EXPECT_FALSE(already_there.value.value("changed", true));

    // Reapplying to the same loaded map is idempotent. Reloading the manager rebuilds the
    // appended floor rather than mistaking its saved numeric id for a vanilla floor.
    moonmod::ApplyChronicleTwoDungeons(&manager);
    EXPECT_EQ(manager.glid_num, 7);
    EXPECT_TRUE(manager.IsMoonModFloor(2, "Moon Vault Annex"));
    EXPECT_TRUE(manager.IsMoonModFloor(3, "Moon Vault Gallery"));

    CDngFloorManager reloaded_manager;
    reloaded_manager.Initialize();
    alignas(64) u_long128 reloaded_arena_storage[1024]{};
    mgCMemory reloaded_arena;
    reloaded_arena.stSetBuffer(reloaded_arena_storage, 1024);
    char reloaded_dungeon_map[] =
        "TREE_INFO 6,3,3;"
        "GI 1,1,1,0,0; RI 0,0,0; RI_LINK -1,-1,-1,1; RI_TITLE 0,\"Entrance\";"
        "GI 0,2,1,0,0; RT 0,0,0;"
        "GI 1,3,1,0,0; RI 1,1,0; RI_LINK -1,-1,0,-1; RI_TITLE 1,\"Source Floor\";";
    reloaded_manager.AnalyzeFile(reloaded_dungeon_map, static_cast<int>(std::strlen(reloaded_dungeon_map)),
                                 &reloaded_arena);
    moonmod::ApplyChronicleTwoDungeons(&reloaded_manager);
    EXPECT_EQ(reloaded_manager.glid_num, 7);
    EXPECT_TRUE(reloaded_manager.IsMoonModFloor(2, "Moon Vault Annex"));
    EXPECT_TRUE(reloaded_manager.IsMoonModFloor(3, "Moon Vault Gallery"));
    ASSERT_NE(reloaded_manager.GetDngMapFloorInfo(2), nullptr);
    EXPECT_NE(reloaded_manager.GetDngMapFloorInfo(2)->flag & DNGMAP_ROOM_FLAG_EXIT, 0);
    EXPECT_EQ(reloaded_manager.GetDngMapFloorInfo(2)->seal, DNGMAP_SEAL_MAX);

    moonmod::ResetChronicleTwoContentForTests();
    reloaded_manager.Initialize();
}

TEST(MoonModDungeonRegistration, ExtendedFloorProgressSurvivesStableIdSaveReload) {
    ActiveDungeonSaveScope active_save;
    EXPECT_EQ(GetSavedDungeonFloorCount(0), 9);
    EXPECT_EQ(GetSavedDungeonFloorCount(6), 39);
    EXPECT_EQ(GetSavedDungeonFloorCount(-1), 0);
    EXPECT_EQ(GetSavedDungeonFloorCount(SAVE_DUNGEON_NUM), 0);
    EXPECT_TRUE(HasSavedDungeonFloorRecord(0, 8));
    EXPECT_FALSE(HasSavedDungeonFloorRecord(0, 9));
    EXPECT_TRUE(HasSavedDungeonFloorRecord(6, 38));
    EXPECT_FALSE(HasSavedDungeonFloorRecord(6, 39));

    CDngFloorManager manager;
    manager.Initialize();
    alignas(64) u_long128 arena_storage[1024]{};
    mgCMemory arena;
    arena.stSetBuffer(arena_storage, 1024);
    std::string dungeon_map = "TREE_INFO 18,1,9;";
    for (int floor = 0; floor < GetSavedDungeonFloorCount(0); ++floor) {
        dungeon_map += "GI 1," + std::to_string(floor * 2) + ",0,0,0; RI " + std::to_string(floor) + "," +
                       std::to_string(floor) + ",0; RI_LINK -1,-1,-1,-1; RI_TITLE " + std::to_string(floor) +
                       ",\"Native Floor\";";
    }
    manager.AnalyzeFile(dungeon_map.data(), static_cast<int>(dungeon_map.size()), &arena);
    ASSERT_EQ(manager.glid_num, GetSavedDungeonFloorCount(0));
    for (int floor = 0; floor < GetSavedDungeonFloorCount(0); ++floor) {
        ASSERT_NE(manager.GetDngMapFloorInfo(floor), nullptr);
    }

    int added_floor_id = -1;
    char error[160]{};
    ASSERT_TRUE(manager.RegisterMoonModFloor(8, "Overflow Floor", "test.moonmod:moon-vault.overflow", &added_floor_id,
                                              error, sizeof(error)))
        << error;
    EXPECT_EQ(added_floor_id, 9);
    EXPECT_EQ(manager.glid_num, GetSavedDungeonFloorCount(0) + 2);

    DNG_FLOOR_SAVE *floor_save = active_save.save->save_dungeon.GetFloorInfoPtr(0, added_floor_id);
    ASSERT_NE(floor_save, nullptr);
    floor_save->flag |= DNG_FLOOR_FLAG_CLEAR;
    floor_save->visit_count = 3;
    floor_save->kill_count = 17;

    MOONMOD_DUNGEON_SAVE_EXTENSION extension{};
    ASSERT_TRUE(ExportMoonModDungeonSaveExtension(&active_save.save->save_dungeon, &extension));
    ASSERT_EQ(extension.record_count, 1u);
    ResetMoonModDungeonSaveExtension(&active_save.save->save_dungeon);
    ASSERT_TRUE(ImportMoonModDungeonSaveExtension(&active_save.save->save_dungeon, &extension));
    // Native floor IDs can change when installed mods change. Progress follows the stable
    // package ID and binds to the newly materialized numeric slot.
    ASSERT_TRUE(RegisterMoonModDungeonSaveRecord(&active_save.save->save_dungeon, 2, 39,
                                                  "test.moonmod:moon-vault.overflow"));
    floor_save = active_save.save->save_dungeon.GetFloorInfoPtr(2, 39);
    ASSERT_NE(floor_save, nullptr);
    EXPECT_NE(floor_save->flag & DNG_FLOOR_FLAG_CLEAR, 0);
    EXPECT_EQ(floor_save->visit_count, 3);
    EXPECT_EQ(floor_save->kill_count, 17);
}

TEST(MoonModDungeonRegistration, ConfiguresProceduralDungeonLayoutLimits) {
    moonmod::ResetChronicleTwoContentForTests();
    struct RestoreLayout {
        ~RestoreLayout() { moonmod::ResetChronicleTwoContentForTests(); }
    } restore_layout;

    moonmod::RuntimeConfig config;
    moonmod::ConfigureChronicleTwoApi(config);
    const moonmod::ApiOperationSpec *layout_operation = moonmod::FindApiOperation("dungeons.set_layout");
    ASSERT_NE(layout_operation, nullptr);
    EXPECT_TRUE(std::ranges::any_of(config.api_capabilities, [](const moonmod::ApiCapability &capability) {
        return capability.path == "dungeons.set_layout" && capability.supported;
    }));

    const moonmod::ApiCallResult updated = config.api_handler(
        "test.moonmod.dungeon-probe", *layout_operation,
        moonmod::Json::array({moonmod::Json{{"rooms", 24}, {"area", 20}, {"room_min", 2}, {"room_max", 5}}}));
    ASSERT_EQ(updated.status, moonmod::ApiCallStatus::Ok) << updated.message;
    EXPECT_EQ(updated.value.value("rooms", -1), 24);
    EXPECT_EQ(updated.value.value("area", -1), 20);
    EXPECT_EQ(updated.value.value("room_min", -1), 2);
    EXPECT_EQ(updated.value.value("room_max", -1), 5);
    EXPECT_EQ(updated.value.value("grid", -1), DNG_GRID);
    EXPECT_EQ(updated.value.value("room_limit", -1), DNG_ROOM_MAX);
    EXPECT_EQ(DngLayoutRooms, 24);
    EXPECT_EQ(DngLayoutArea, 20);
    EXPECT_EQ(DngLayoutRoomMin, 2);
    EXPECT_EQ(DngLayoutRoomMax, 5);

    const moonmod::ApiCallResult reset = config.api_handler(
        "test.moonmod.dungeon-probe", *layout_operation,
        moonmod::Json::array({moonmod::Json::object()}));
    ASSERT_EQ(reset.status, moonmod::ApiCallStatus::Ok) << reset.message;
    EXPECT_EQ(DngLayoutRooms, 0);
    EXPECT_EQ(DngLayoutArea, 0);
    EXPECT_EQ(DngLayoutRoomMin, 0);
    EXPECT_EQ(DngLayoutRoomMax, 0);

    EXPECT_EQ(config.api_handler("test.moonmod.dungeon-probe", *layout_operation,
                                 moonmod::Json::array({moonmod::Json{{"rooms", 1}}})).status,
              moonmod::ApiCallStatus::InvalidArgument);
    EXPECT_EQ(config.api_handler("test.moonmod.dungeon-probe", *layout_operation,
                                 moonmod::Json::array({moonmod::Json{{"rooms", 12}, {"area", 31}}})).status,
              moonmod::ApiCallStatus::InvalidArgument);
    EXPECT_EQ(config.api_handler("test.moonmod.dungeon-probe", *layout_operation,
                                 moonmod::Json::array({moonmod::Json{{"rooms", 12}, {"room_min", 5}, {"room_max", 3}}})).status,
              moonmod::ApiCallStatus::InvalidArgument);
}
