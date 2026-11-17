// Co-op's split of a Chronicle Two save (moonmod/coop_save.hpp): the world and a player's own things go across whole,
// each without the other; a town carries its record and its layout; a damaged or foreign one changes nothing; and the
// host's guests are kept beside the saves.
#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
using moonmod::coop::PlainText;

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
    // Who the story has in the party is the story's: it crosses with the world, not with a player's things.
    const u16 story_party = host.save().user_data.party_member, story_change = host.save().user_data.chara_change;
    host.save().user_data.party_member = 0x7;
    host.save().user_data.chara_change = 0x3;
    guest.save().user_data.party_member = 0x1;
    ASSERT_TRUE(ImportWorld(ExportWorld(host.save()), guest.save(), error)) << error;
    EXPECT_EQ(guest.save().user_data.party_member, 0x7);
    EXPECT_EQ(guest.save().user_data.chara_change, 0x3);
    ASSERT_TRUE(ImportPlayer(ExportPlayer(host.save()), guest.save(), error)) << error;
    guest.save().user_data.party_member = 0x1;
    ASSERT_TRUE(ImportPlayer(ExportPlayer(host.save()), guest.save(), error)) << error;
    EXPECT_EQ(guest.save().user_data.party_member, 0x1) << "a player's things carry no party";
    host.save().user_data.party_member = guest.save().user_data.party_member = story_party;
    host.save().user_data.chara_change = guest.save().user_data.chara_change = story_change;
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
    // A field another build sized differently: the first field's size, one byte more (in the unpacked text, where
    // the field's size is at a known place).
    std::string other = PlainText(world);
    other[4 + 3] = other[4 + 3] == 'A' ? 'B' : 'A';
    EXPECT_FALSE(ImportWorld(other, guest.save(), error));
    EXPECT_NE(error.find("another build"), std::string::npos);
    EXPECT_EQ(ExportWorld(guest.save()) + ExportPlayer(guest.save()), before);
}

TEST(MoonModCoopSave, ATownsFingerprintFollowsItsGeorama) {
    Save a(11), b(12);
    using moonmod::coop::TownFingerprint;
    EXPECT_EQ(TownFingerprint(a.save(), 2), TownFingerprint(b.save(), 2)) << "two new games' towns are the same town";
    const std::uint64_t empty = TownFingerprint(a.save(), 2);
    PortTownLayout(a.save().edit_data[2]) = SampleLayout(1);
    const std::uint64_t built = TownFingerprint(a.save(), 2);
    EXPECT_NE(built, empty) << "a part placed is another town";
    EXPECT_EQ(TownFingerprint(a.save(), 1), TownFingerprint(b.save(), 1)) << "the town next door is as it was";
    // The same town in another game, taken across, has the same number.
    std::string error;
    ASSERT_TRUE(ImportTown(ExportTown(a.save(), 2), 2, b.save(), error)) << error;
    EXPECT_EQ(TownFingerprint(b.save(), 2), built);
    a.save().edit_data[2].culture_point += 5;
    EXPECT_NE(TownFingerprint(a.save(), 2), built) << "a record field changed is another town";
}

TEST(MoonModCoopSave, TheTextIsPackedSmallAndEitherFormReadsBack) {
    Save host(7), guest(8);
    host.save().SetBitFlag(100, 1);
    host.save().user_data.used_data[5].item_no = 77;
    const std::string world = ExportWorld(host.save());
    const std::string player = ExportPlayer(host.save());
    // An operation answers 64 KiB at most, and so does one argument of a visit: a new game's world and things fit.
    EXPECT_EQ(world[4], '!');
    // A new game's world is 176316 characters plain (measured 2026-11-14); packed it has to leave most of the
    // 64 KiB for what a played save adds.
    EXPECT_LT(world.size(), 16u * 1024u) << "a new game's world is " << world.size() << " characters";
    EXPECT_LT(player.size(), 16u * 1024u) << "a new game's things are " << player.size() << " characters";
    // Unpacked it is the same record at its full size, and it reads back the same.
    const std::string plain = PlainText(world);
    EXPECT_GT(plain.size(), world.size() * 8);
    EXPECT_EQ(plain.find('!'), std::string::npos);
    std::string error;
    ASSERT_TRUE(ImportWorld(plain, guest.save(), error)) << error;
    EXPECT_NE(guest.save().GetBitFlag(100), 0);
    EXPECT_EQ(ExportWorld(guest.save()), world);
    ASSERT_TRUE(ImportPlayer(PlainText(player), guest.save(), error)) << error;
    EXPECT_EQ(guest.save().user_data.used_data[5].item_no, 77);
    // A save with things in it that do not repeat packs less and still reads back byte for byte.
    Save played(9), other(10);
    for (int i = 0; i < 300; ++i) played.save().user_data.used_data[i].item_no = static_cast<s16>((i * 37 + 11) % 400);
    for (int i = 0; i < 2048; i += 3) played.save().SetBitFlag(i, 1);
    for (int i = 0; i < 128; ++i) played.save().short_flag[i] = static_cast<s16>(i * 259);
    const std::string busy_world = ExportWorld(played.save()), busy_things = ExportPlayer(played.save());
    ASSERT_TRUE(ImportWorld(busy_world, other.save(), error)) << error;
    ASSERT_TRUE(ImportPlayer(busy_things, other.save(), error)) << error;
    EXPECT_EQ(PlainText(ExportWorld(other.save())), PlainText(busy_world));
    EXPECT_EQ(PlainText(ExportPlayer(other.save())), PlainText(busy_things));
    EXPECT_EQ(other.save().user_data.used_data[299].item_no, played.save().user_data.used_data[299].item_no);
    std::printf("packed sizes: new world %zu, new things %zu, busy world %zu, busy things %zu\n", world.size(), player.size(),
                busy_world.size(), busy_things.size());
    // A packed text cut short is refused whole.
    EXPECT_FALSE(ImportWorld(world.substr(0, world.size() - 40), guest.save(), error));
    EXPECT_EQ(PlainText("not an export"), "not an export");
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

// A part both games put up since the town they agreed on (the same part in the same spot: the town's own parts first
// written into both records as they stood there, or two players' identical work) stands once, and the rest of each
// game's work is kept.
TEST(MoonModCoopSave, APartBothGamesPutUpInOneSpotStandsOnce) {
    Save host(15), scratch(16);
    std::string error;
    const TownLayout start = SampleLayout(0); // parts 10, 11, 12
    PortTownLayout(host.save().edit_data[2]) = start;
    const std::string base = ExportTown(host.save(), 2);
    TownPart gate;
    gate.id = 21;
    gate.state = 1;
    gate.pos = {40, 0, 8};
    TownPart shop = gate, well = gate;
    shop.id = 20;
    well.id = 30;
    TownLayout hosts = start; // the host: the gate and a shop
    hosts.parts.push_back(gate);
    hosts.parts.push_back(shop);
    PortTownLayout(host.save().edit_data[2]) = hosts;
    TownLayout guests = start; // the guest: the same gate in the same spot, and a well
    guests.parts.push_back(gate);
    guests.parts.push_back(well);
    PortTownLayout(scratch.save().edit_data[2]) = guests;
    ASSERT_TRUE(moonmod::coop::MergeTown(base, ExportTown(scratch.save(), 2), 2, host.save(), error)) << error;
    const TownLayout *merged = PortFindTownLayout(host.save().edit_data[2]);
    ASSERT_NE(merged, nullptr);
    std::vector<int> ids;
    for (const TownPart &part : merged->parts) ids.push_back(part.id);
    EXPECT_EQ(ids, (std::vector<int>{10, 11, 12, 21, 20, 30}));
    // Two gates in one spot from each side stay two (a part may stand twice): only what both put up pairs off.
    TownLayout twice = guests;
    twice.parts.push_back(gate);
    PortTownLayout(host.save().edit_data[2]) = hosts;
    PortTownLayout(scratch.save().edit_data[2]) = twice;
    ASSERT_TRUE(moonmod::coop::MergeTown(base, ExportTown(scratch.save(), 2), 2, host.save(), error)) << error;
    ids.clear();
    for (const TownPart &part : PortFindTownLayout(host.save().edit_data[2])->parts) ids.push_back(part.id);
    EXPECT_EQ(ids, (std::vector<int>{10, 11, 12, 21, 20, 30, 21}));
}

TEST(MoonModCoopSave, ATownBuiltPartByPartByOneIsTakenWholeEachTime) {
    Save host(13), builder(14);
    std::string error;
    PortTownLayout(host.save().edit_data[1]) = SampleLayout(0);
    ASSERT_TRUE(ImportTown(ExportTown(host.save(), 1), 1, builder.save(), error)) << error; // the visit begins
    std::string agreed = ExportTown(builder.save(), 1);
    for (int step = 1; step <= 3; ++step) {
        // The builder places a part in a slot of its own choosing (the front), and its log names parts by index.
        TownLayout &theirs = PortTownLayout(builder.save().edit_data[1]);
        TownPart    part;
        part.id = static_cast<s16>(40 + step);
        part.state = 2;
        theirs.parts.insert(theirs.parts.begin(), part);
        EditPlaceLog log{};
        log.parts_no = 0;
        log.base_no = static_cast<s16>(step);
        theirs.place_log.push_back(log);
        const std::string edited = ExportTown(builder.save(), 1);
        ASSERT_TRUE(moonmod::coop::MergeTown(agreed, edited, 1, host.save(), error)) << error;
        EXPECT_EQ(ExportTown(host.save(), 1), edited) << "part " << step << ": the host's town is the builder's, to the byte";
        agreed = edited;
    }
    // The host's own record moved on (its culture points): the layout is still theirs whole, the record merged.
    host.save().edit_data[1].culture_point += 3;
    TownLayout &theirs = PortTownLayout(builder.save().edit_data[1]);
    theirs.parts.erase(theirs.parts.begin() + 1);
    const std::string edited = ExportTown(builder.save(), 1);
    ASSERT_TRUE(moonmod::coop::MergeTown(agreed, edited, 1, host.save(), error)) << error;
    const TownLayout *merged = PortFindTownLayout(host.save().edit_data[1]);
    ASSERT_NE(merged, nullptr);
    EXPECT_EQ(merged->parts.size(), theirs.parts.size());
    EXPECT_EQ(merged->parts.front().id, 43);
    EXPECT_EQ(merged->place_log.size(), theirs.place_log.size());
    EXPECT_NE(ExportTown(host.save(), 1), edited) << "the host's own culture points stand";
}

// Standard base64, as the text uses (unpacked form, from PlainText).
std::string FromBase64(std::string_view in) {
    static constexpr std::string_view kTable = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    unsigned    value = 0;
    int         bits = 0;
    for (const char c : in) {
        if (c == '=') break;
        value = (value << 6) | static_cast<unsigned>(kTable.find(c));
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((value >> bits) & 0xFF));
        }
    }
    return out;
}
std::string ToBase64(std::string_view bytes) {
    static constexpr char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const auto     at = [&bytes](std::size_t k) { return static_cast<unsigned>(static_cast<unsigned char>(bytes[k])); };
        const unsigned v = (at(i) << 16) | ((i + 1 < bytes.size() ? at(i + 1) : 0) << 8) | (i + 2 < bytes.size() ? at(i + 2) : 0);
        out.push_back(kTable[(v >> 18) & 63]);
        out.push_back(kTable[(v >> 12) & 63]);
        out.push_back(i + 1 < bytes.size() ? kTable[(v >> 6) & 63] : '=');
        out.push_back(i + 2 < bytes.size() ? kTable[v & 63] : '=');
    }
    return out;
}

// Review R3: a town's save_count is bumped by the game at every house entered and every map change; it says nothing
// of the town, so neither the fingerprint nor the merge's "nobody here changed it" looks at it.
TEST(MoonModCoopSave, ATownsSaveCountIsNotPartOfTheTown) {
    using moonmod::coop::TownFingerprint;
    Save host(21), builder(22);
    PortTownLayout(host.save().edit_data[1]) = SampleLayout(1);
    const std::uint64_t built = TownFingerprint(host.save(), 1);
    host.save().edit_data[1].save_count += 7;
    EXPECT_EQ(TownFingerprint(host.save(), 1), built) << "a walk through a house is the same town";
    host.save().edit_data[1].save_count -= 7;
    const std::string agreed = ExportTown(host.save(), 1);
    std::string       error;
    ASSERT_TRUE(ImportTown(agreed, 1, builder.save(), error)) << error;
    // The host walked through a house; the builder placed a part. The host's town is the builder's, taken whole.
    host.save().edit_data[1].save_count += 2;
    TownLayout &theirs = PortTownLayout(builder.save().edit_data[1]);
    TownPart    part = theirs.parts.back();
    part.pos = {90, 0, 9};
    theirs.parts.push_back(part);
    theirs.place_log.push_back({.parts_no = static_cast<s16>(theirs.parts.size() - 1), .base_no = 0});
    const std::string edited = ExportTown(builder.save(), 1);
    ASSERT_TRUE(moonmod::coop::MergeTown(agreed, edited, 1, host.save(), error)) << error;
    EXPECT_EQ(ExportTown(host.save(), 1), edited) << "taken whole, log and all";
}

// Review R3: when both built, the placement log is kept, its parts renumbered to the merged order; an entry whose
// part the other game took away goes with it.
TEST(MoonModCoopSave, AMergedTownKeepsItsPlacementLogRenumbered) {
    Save host(23), builder(24);
    TownLayout base;
    for (int i = 0; i < 4; ++i) {
        TownPart part;
        part.id = i == 0 ? 1 : 2;
        part.state = 2;
        part.pos = {static_cast<s16>(i * 10), 0, 0};
        base.parts.push_back(part);
    }
    base.place_log = {{.parts_no = 1, .base_no = 0}, {.parts_no = 2, .base_no = 0}, {.parts_no = 3, .base_no = 2}};
    PortTownLayout(host.save().edit_data[1]) = base;
    const std::string agreed = ExportTown(host.save(), 1);
    std::string       error;
    ASSERT_TRUE(ImportTown(agreed, 1, builder.save(), error)) << error;
    // The host puts a part (at x 40) on part 3; the builder takes part 1 away and puts one (at x 50) on part 2.
    TownLayout &mine = PortTownLayout(host.save().edit_data[1]);
    TownPart    put = mine.parts[1];
    put.pos = {40, 0, 0};
    mine.parts.push_back(put);
    mine.place_log.push_back({.parts_no = 4, .base_no = 3});
    TownLayout &theirs = PortTownLayout(builder.save().edit_data[1]);
    theirs.parts.erase(theirs.parts.begin() + 1);
    theirs.place_log = {{.parts_no = 1, .base_no = 0}, {.parts_no = 2, .base_no = 1}};
    put.pos = {50, 0, 0};
    theirs.parts.push_back(put);
    theirs.place_log.push_back({.parts_no = 3, .base_no = 1});
    ASSERT_TRUE(moonmod::coop::MergeTown(agreed, ExportTown(builder.save(), 1), 1, host.save(), error)) << error;
    const TownLayout *merged = PortFindTownLayout(host.save().edit_data[1]);
    ASSERT_NE(merged, nullptr);
    // Parts: 0, 20, 30, the host's 40, the builder's 50.
    ASSERT_EQ(merged->parts.size(), 5u);
    const std::vector<int> xs = {merged->parts[0].pos[0], merged->parts[1].pos[0], merged->parts[2].pos[0], merged->parts[3].pos[0],
                                 merged->parts[4].pos[0]};
    EXPECT_EQ(xs, (std::vector<int>{0, 20, 30, 40, 50}));
    // Log: 20 on 0, 30 on 20, 40 on 30 (the host's own, renumbered), 50 on 20 (theirs, renumbered); 10's entry gone.
    ASSERT_EQ(merged->place_log.size(), 4u);
    const auto entry = [&merged](std::size_t i) { return std::pair<int, int>(merged->place_log[i].parts_no, merged->place_log[i].base_no); };
    EXPECT_EQ(entry(0), (std::pair<int, int>(1, 0)));
    EXPECT_EQ(entry(1), (std::pair<int, int>(2, 1)));
    EXPECT_EQ(entry(2), (std::pair<int, int>(3, 2)));
    EXPECT_EQ(entry(3), (std::pair<int, int>(4, 1)));
}

// Review R8: a river grid whose cells are not num_x by num_z, or with a count below zero, is damaged; so is a text
// that gives one field twice (and so leaves another out).
TEST(MoonModCoopSave, AGridOfTheWrongSizeOrAFieldGivenTwiceIsRefused) {
    TownLayout  layout = SampleLayout(1);
    TownLayout  back;
    std::string error;
    ASSERT_TRUE(moonmod::coop::LayoutFromBytes(moonmod::coop::LayoutBytes(layout), back, error)) << error;
    layout.grids[0].cells.pop_back(); // three cells for a 2 by 2 grid
    EXPECT_FALSE(moonmod::coop::LayoutFromBytes(moonmod::coop::LayoutBytes(layout), back, error));
    layout.grids[0].cells.clear();
    layout.grids[0].num_x = -1;
    layout.grids[0].num_z = 0;
    EXPECT_FALSE(moonmod::coop::LayoutFromBytes(moonmod::coop::LayoutBytes(layout), back, error));
    layout.grids[0].num_x = -2;
    layout.grids[0].num_z = -2;
    layout.grids[0].cells = {0, 0, 0, 0};
    EXPECT_FALSE(moonmod::coop::LayoutFromBytes(moonmod::coop::LayoutBytes(layout), back, error));

    // A town's text: its record (field 1) twice and no layout (field 2).
    Save        town(25);
    Save        other(26);
    PortTownLayout(town.save().edit_data[1]) = SampleLayout(1);
    const std::string plain = PlainText(ExportTown(town.save(), 1));
    const std::size_t colon = plain.find(':');
    ASSERT_NE(colon, std::string::npos);
    const std::string tag = plain.substr(0, colon + 1);
    const std::string bytes = FromBase64(plain.substr(colon + 1));
    ASSERT_GE(bytes.size(), 6u);
    std::uint32_t first = 0;
    for (int k = 0; k < 4; ++k) first |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[2 + k])) << (8 * k);
    const std::string record = bytes.substr(0, 6 + first);
    ASSERT_TRUE(ImportTown(tag + ToBase64(bytes), 1, other.save(), error)) << error << " (the codec here reads the text back)";
    const std::string before = ExportTown(other.save(), 1);
    EXPECT_FALSE(ImportTown(tag + ToBase64(record + record), 1, other.save(), error));
    EXPECT_NE(error.find("twice"), std::string::npos) << error;
    EXPECT_EQ(ExportTown(other.save(), 1), before);
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
