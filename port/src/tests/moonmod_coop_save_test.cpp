// Co-op's split of a Chronicle Two save (moonmod/coop_save.hpp): the world and a player's own things go across whole,
// each without the other; a town carries its record and its layout; a damaged or foreign one changes nothing; and the
// host's guests are kept beside the saves.
#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>

#include "moonmod/coop_save.hpp"
#include "save_json.hpp"
#include "savedata.hpp"

namespace {

using moonmod::coop::ExportPlayer;
using moonmod::coop::ExportTown;
using moonmod::coop::ExportWorld;
using moonmod::coop::ImportPlayer;
using moonmod::coop::ImportTown;
using moonmod::coop::ImportWorld;

struct Save {
    std::unique_ptr<CSaveData> data = std::make_unique<CSaveData>();
    explicit Save(int fill = 0) {
        data->Initialize();
        data->day = fill;
        data->user_data.money = 100 + fill;
    }
    ~Save() {
        for (CEditData &town : data->edit_data) PortClearTownLayout(town);
    }
    CSaveData &save() { return *data; }
};

TownLayout SampleLayout(int seed) {
    TownLayout layout;
    for (int i = 0; i < 3 + seed; ++i) {
        TownPart part;
        part.id = 10 + i;
        part.state = 2;
        part.angle = static_cast<s8>(i);
        part.pos = {static_cast<s16>(i * 7), 0, static_cast<s16>(seed)};
        part.color[0] = {0x80, 0x40, 0x20};
        part.house_no = i == 0 ? 1 : 0;
        layout.parts.push_back(part);
    }
    layout.houses.push_back({.npc_no = static_cast<s16>(5 + seed)});
    layout.place_log.push_back(EditPlaceLog{});
    TownGrid grid;
    grid.num_x = 2;
    grid.num_z = 2;
    grid.pos = {1, 2, 3};
    grid.cells = {0, 1, 1, 0};
    layout.grids.push_back(grid);
    return layout;
}

bool SameLayout(const TownLayout &a, const TownLayout &b) {
    if (a.parts.size() != b.parts.size() || a.houses.size() != b.houses.size() || a.grids.size() != b.grids.size()) return false;
    for (std::size_t i = 0; i < a.parts.size(); ++i) {
        if (a.parts[i].id != b.parts[i].id || a.parts[i].pos != b.parts[i].pos || a.parts[i].color != b.parts[i].color) return false;
    }
    for (std::size_t i = 0; i < a.grids.size(); ++i) {
        if (a.grids[i].cells != b.grids[i].cells || a.grids[i].pos != b.grids[i].pos) return false;
    }
    return true;
}

TEST(MoonModCoopSave, TheWorldCrossesWithoutThePlayersThings) {
    Save host(1), guest(2);
    host.save().SetBitFlag(40, 1);
    host.save().short_flag[3] = 77;
    host.save().save_dungeon.floor_id[2] = 9;
    host.save().now_time = 13.5f;
    host.save().area_no = 3;
    PortTownLayout(host.save().edit_data[2]) = SampleLayout(1);
    guest.save().user_data.money = 55;
    const std::string world = ExportWorld(host.save());
    ASSERT_TRUE(world.starts_with("DCW2"));
    std::string error;
    ASSERT_TRUE(ImportWorld(world, guest.save(), error)) << error;
    EXPECT_NE(guest.save().GetBitFlag(40), 0);
    EXPECT_EQ(guest.save().short_flag[3], 77);
    EXPECT_EQ(guest.save().save_dungeon.floor_id[2], 9);
    EXPECT_FLOAT_EQ(guest.save().now_time, 13.5f);
    EXPECT_EQ(guest.save().area_no, 3);
    EXPECT_EQ(guest.save().user_data.money, 55); // the guest keeps their own Gilda
    ASSERT_NE(PortFindTownLayout(guest.save().edit_data[2]), nullptr);
    EXPECT_TRUE(SameLayout(*PortFindTownLayout(guest.save().edit_data[2]), SampleLayout(1)));
    EXPECT_EQ(ExportWorld(guest.save()), world);
}

TEST(MoonModCoopSave, APlayersThingsCrossWithoutTheWorld) {
    Save first(3), second(4);
    first.save().user_data.money = 500;
    first.save().user_data.chara_data[1].defence = 180;
    first.save().user_data.used_data[0].item_no = 42;
    first.save().user_data.yarikomi_medal = 9;
    second.save().save_dungeon.floor_id[3] = 5;
    const std::string player = ExportPlayer(first.save());
    ASSERT_TRUE(player.starts_with("DCP2"));
    std::string error;
    ASSERT_TRUE(ImportPlayer(player, second.save(), error)) << error;
    EXPECT_EQ(second.save().user_data.money, 500);
    EXPECT_EQ(second.save().user_data.chara_data[1].defence, 180);
    EXPECT_EQ(second.save().user_data.used_data[0].item_no, 42);
    EXPECT_EQ(second.save().user_data.yarikomi_medal, 9);
    EXPECT_EQ(second.save().save_dungeon.floor_id[3], 5); // the world stays the receiver's
    EXPECT_EQ(second.save().day, 4);
    EXPECT_EQ(ExportPlayer(second.save()), player);
}

TEST(MoonModCoopSave, DamagedOrMistakenDataChangesNothing) {
    Save host(5), guest(6);
    const std::string world = ExportWorld(host.save());
    const std::string player = ExportPlayer(host.save());
    const std::string before = ExportWorld(guest.save()) + ExportPlayer(guest.save());
    std::string error;
    EXPECT_FALSE(ImportWorld(player, guest.save(), error)); // a player is not a world
    EXPECT_FALSE(ImportPlayer(world, guest.save(), error));
    EXPECT_FALSE(ImportWorld(world.substr(0, world.size() / 2), guest.save(), error)); // cut short
    EXPECT_FALSE(ImportWorld("DCW2***", guest.save(), error));
    // A field another build sized differently: the first field's size, one byte more.
    std::string other = world;
    other[4 + 3] = other[4 + 3] == 'A' ? 'B' : 'A';
    EXPECT_FALSE(ImportWorld(other, guest.save(), error));
    EXPECT_NE(error.find("another build"), std::string::npos);
    EXPECT_EQ(ExportWorld(guest.save()) + ExportPlayer(guest.save()), before);
}

TEST(MoonModCoopSave, ATownsLayoutIsCheckedWholeBeforeAnythingIsWritten) {
    const TownLayout layout = SampleLayout(2);
    const std::string bytes = moonmod::coop::LayoutBytes(layout);
    TownLayout        back;
    std::string       error;
    ASSERT_TRUE(moonmod::coop::LayoutFromBytes(bytes, back, error)) << error;
    EXPECT_TRUE(SameLayout(layout, back));
    EXPECT_EQ(back.houses.size(), 1u);
    EXPECT_EQ(back.houses[0].npc_no, 7);
    EXPECT_EQ(back.place_log.size(), 1u);
    EXPECT_FALSE(moonmod::coop::LayoutFromBytes(bytes.substr(0, bytes.size() - 3), back, error)); // cut short
    EXPECT_FALSE(moonmod::coop::LayoutFromBytes(bytes + "x", back, error));                      // trailing bytes
    std::string huge = bytes;
    huge[4] = static_cast<char>(0xFF); // a parts count past any town
    huge[5] = static_cast<char>(0xFF);
    huge[6] = static_cast<char>(0xFF);
    EXPECT_FALSE(moonmod::coop::LayoutFromBytes(huge, back, error));
}

TEST(MoonModCoopSave, OneTownsGeoramaCrossesAlone) {
    Save editor(7), other(8);
    PortTownLayout(editor.save().edit_data[2]) = SampleLayout(3);
    editor.save().edit_data[2].culture_point = 1234;
    PortTownLayout(editor.save().edit_data[1]) = SampleLayout(4);
    const std::string before_world = ExportWorld(other.save());
    const std::string town = ExportTown(editor.save(), 2);
    ASSERT_TRUE(town.starts_with("DCT22:"));
    std::string error;
    EXPECT_FALSE(ImportTown(town, 3, other.save(), error)); // another town's
    EXPECT_FALSE(ImportTown(town, 9, other.save(), error));
    EXPECT_EQ(ExportWorld(other.save()), before_world);
    ASSERT_TRUE(ImportTown(town, 2, other.save(), error)) << error;
    EXPECT_EQ(ExportTown(other.save(), 2), town);
    EXPECT_EQ(other.save().edit_data[2].culture_point, 1234);
    EXPECT_NE(ExportTown(other.save(), 1), ExportTown(editor.save(), 1)); // only town 2 came
    EXPECT_TRUE(ExportTown(other.save(), 7).empty());
}

TEST(MoonModCoopSave, TwoBuildingInOneTownBothKeepTheirWork) {
    Save host(9);
    std::string error;
    TownLayout  start = SampleLayout(0); // parts 10, 11, 12
    PortTownLayout(host.save().edit_data[2]) = start;
    const std::string base = ExportTown(host.save(), 2); // the guest opens the editor here

    // Meanwhile the host puts up part 20 and changes its river grid.
    TownLayout hosts = start;
    TownPart   shop;
    shop.id = 20;
    hosts.parts.push_back(shop);
    hosts.grids[0].cells = {1, 1, 1, 1};
    PortTownLayout(host.save().edit_data[2]) = hosts;

    // The guest took part 10 down, put up part 30 and moved the house's villager.
    TownLayout guests = start;
    guests.parts.erase(guests.parts.begin());
    TownPart well;
    well.id = 30;
    guests.parts.push_back(well);
    guests.houses[0].npc_no = 99;
    Save scratch(10);
    PortTownLayout(scratch.save().edit_data[2]) = guests;
    const std::string edited = ExportTown(scratch.save(), 2);

    ASSERT_TRUE(moonmod::coop::MergeTown(base, edited, 2, host.save(), error)) << error;
    const TownLayout *merged = PortFindTownLayout(host.save().edit_data[2]);
    ASSERT_NE(merged, nullptr);
    std::vector<int> ids;
    for (const TownPart &part : merged->parts) ids.push_back(part.id);
    EXPECT_EQ(ids, (std::vector<int>{11, 12, 20, 30})); // part 10 gone, the shop and the well both up
    EXPECT_EQ(merged->houses[0].npc_no, 99);
    EXPECT_EQ(merged->grids[0].cells, (std::vector<u8>{1, 1, 1, 1})); // the host's grid stays
    EXPECT_FALSE(moonmod::coop::MergeTown(base, ExportTown(scratch.save(), 1), 2, host.save(), error)); // another town's
}

struct TempDir {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
                                 ("moonmod-coop-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                                  std::to_string(std::random_device{}()));
    TempDir() { std::filesystem::create_directories(path); }
    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

TEST(MoonModCoopSave, TheHostKeepsItsGuestsThingsBesideTheSaves) {
    using namespace moonmod::coop;
    Save save(11);
    save.save().user_data.money = 900;
    const std::string things = ExportPlayer(save.save());
    AccountsClear();
    std::string error;
    ASSERT_TRUE(AccountSet({"guest-0001", "Monica", things}, error)) << error;
    ASSERT_TRUE(AccountSet({"guest-0002", "Max but a much longer name than fits here", things}, error)) << error;
    EXPECT_FALSE(AccountSet({"Bad Id", "x", things}, error));
    EXPECT_FALSE(AccountSet({"guest-0003", "x", "not a player"}, error));
    EXPECT_FALSE(AccountSet({"guest-0004", "x", "DCP1" + things.substr(4)}, error)); // Chronicle's, not Chronicle Two's
    ASSERT_EQ(Accounts().size(), 2u);
    EXPECT_EQ(AccountGet("guest-0002")->name.size(), 24u);

    TempDir dir;
    SetAccountsDirectory(dir.path / "moonmod");
    ASSERT_TRUE(AccountsStore());
    EXPECT_TRUE(std::filesystem::exists(dir.path / "moonmod" / "coop_accounts.json"));
    AccountsClear();
    EXPECT_EQ(AccountGet("guest-0001"), nullptr);
    SetAccountsDirectory(dir.path / "moonmod"); // forgets that they were loaded
    AccountsEnsureLoaded();
    ASSERT_NE(AccountGet("guest-0001"), nullptr);
    EXPECT_EQ(AccountGet("guest-0001")->name, "Monica");
    EXPECT_EQ(AccountGet("guest-0001")->player, things);

    EXPECT_TRUE(AccountRemove("guest-0001"));
    EXPECT_TRUE(AccountRemove("guest-0002"));
    ASSERT_TRUE(AccountsStore()); // none left: the file goes
    EXPECT_FALSE(std::filesystem::exists(dir.path / "moonmod" / "coop_accounts.json"));
    AccountsLoad(dir.path / "moonmod");
    EXPECT_TRUE(Accounts().empty());

    std::filesystem::create_directories(dir.path / "moonmod");
    std::ofstream(dir.path / "moonmod" / "coop_accounts.json") << "{not json";
    AccountsLoad(dir.path / "moonmod"); // a damaged file keeps no guests, and says so
    EXPECT_TRUE(Accounts().empty());
    AccountsClear();
}

TEST(MoonModCoopSave, AGameKeepsSixtyFourGuestsAtMost) {
    using namespace moonmod::coop;
    Save save(12);
    const std::string things = ExportPlayer(save.save());
    AccountsClear();
    std::string error;
    for (std::size_t i = 0; i < kAccountsMax; ++i) ASSERT_TRUE(AccountSet({"guest-" + std::to_string(1000 + i), "", things}, error));
    EXPECT_FALSE(AccountSet({"guest-newcomer", "", things}, error));
    EXPECT_TRUE(AccountSet({"guest-1000", "again", things}, error)); // an old guest is updated
    AccountsClear();
}

} // namespace
