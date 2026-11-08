// Work package E: content registration through MoonMod on Chronicle Two (port/docs/CONTENT.md).

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>

#include "editmap.hpp"
#include "gamedata.hpp"
#include "mainloop.hpp"
#include "moonmod/chronicle_two_adapter.hpp"
#include "moonmod/content_characters.hpp"
#include "moonmod/content_georama.hpp"
#include "moonmod/content_items.hpp"
#include "moonmod/content_npcs.hpp"
#include "moonmod/content_slots.hpp"
#include "moonmod/native_resource_overrides.hpp"
#include "nd_meswin.hpp"
#include "save_json.hpp"
#include "savedata.hpp"
#include "sceneevent.hpp"
#include "stubs/gamedata.hpp"
#include "userdata.hpp"

extern CSaveData *PortActiveSaveData asm("MainLoop_ActiveSaveData");

namespace {

using moonmod::ApiCallStatus;
using moonmod::Json;

constexpr unsigned char kTinyPng[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x02, 0x08, 0x06, 0x00, 0x00, 0x00, 0x72, 0xb6, 0x0d, 0x24, 0x00, 0x00, 0x00, 0x10, 0x49,
    0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x0a, 0x21, 0x14, 0x00, 0x45, 0xcc, 0x07, 0xf9,
    0x66, 0xcb, 0xff, 0x39, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};

/** A fresh content registry, a temporary save root and package, and an active save with user data. */
struct ContentFixture {
    std::filesystem::path root;
    std::filesystem::path package;
    CSaveData *previous_save = PortActiveSaveData;
    std::unique_ptr<CSaveData> save = std::make_unique<CSaveData>();
    moonmod::RuntimeConfig config;

    explicit ContentFixture(const char *tag) {
        moonmod::ResetChronicleTwoContentForTests();
        moonmod::ResetNativeResourceOverridesForTests();
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        root = std::filesystem::temp_directory_path() / (std::string("moonmod-content-") + tag + "-" + std::to_string(suffix));
        package = root / "package";
        std::filesystem::create_directories(package / "resources");
        moonmod::SetChronicleTwoContentSaveRootForTests(root / "saves");
        save->Initialize();
        PortActiveSaveData = save.get();
        moonmod::ConfigureChronicleTwoApi(config);
    }

    ~ContentFixture() {
        PortActiveSaveData = previous_save;
        moonmod::ResetChronicleTwoContentForTests();
        moonmod::ResetNativeResourceOverridesForTests();
        moonmod::SetChronicleTwoContentSaveRootForTests({});
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    moonmod::ApiCallResult Call(const char *owner, const char *path, const Json &args) {
        const moonmod::ApiOperationSpec *operation = moonmod::FindApiOperation(path);
        EXPECT_NE(operation, nullptr) << path;
        if (operation == nullptr) return {ApiCallStatus::Failed, nullptr, "missing operation"};
        return config.api_handler(owner, *operation, args);
    }

    bool Supported(const char *path) const {
        return std::ranges::any_of(config.api_capabilities, [path](const moonmod::ApiCapability &capability) {
            return capability.path == path && capability.supported && capability.reason.empty();
        });
    }

    /** Registers `resources/<file>` of the package as the owner's override of `game_path`. */
    void Own(const char *owner, const std::string &file, const std::string &game_path, const void *bytes, std::size_t size) {
        std::ofstream(package / "resources" / file, std::ios::binary).write(static_cast<const char *>(bytes), static_cast<std::streamsize>(size));
        const moonmod::ApiCallResult result = moonmod::RegisterNativeResourceOverride(
            owner, package, Json::array({{{"path", "resources/" + file}, {"game_path", game_path}}}));
        ASSERT_EQ(result.status, ApiCallStatus::Ok) << result.message;
    }
};

/** One weapon template (item 0x20, Max's melee) and one usable item template (item 0x112) in the live tables. */
void SeedItemTables() {
    GameItemDataManage.Initialize();
    CDataCommon &weapon_common = GameItemDataManage.common_data[0];
    std::memset(&weapon_common, 0, sizeof(weapon_common));
    weapon_common.type = ITEM_DATA_MAX_MELEE;
    weapon_common.item_no = 0x20;
    weapon_common.list_no = 0;
    weapon_common.icon_no = 11;
    weapon_common.message_no = 50;
    weapon_common.max_num = 1;
    std::strcpy(weapon_common.file_name, "c01w01");
    weapon_common.active_set = 1;
    weapon_common.stack_num = 1;
    weapon_common.icon_texture_no = 1;
    weapon_common.name = const_cast<char *>("Vanilla Wrench");
    CDataWeapon &weapon = GameItemDataManage.weapon_data[0];
    std::memset(&weapon, 0, sizeof(weapon));
    weapon.durability = 16;
    weapon.status[0] = 7;
    weapon.model_no = 1;
    GameItemDataManage.weapon_num = 1;

    CDataCommon &item_common = GameItemDataManage.common_data[1];
    std::memset(&item_common, 0, sizeof(item_common));
    item_common.type = ITEM_DATA_RECOVERY;
    item_common.item_no = 0x112;
    item_common.list_no = 0;
    item_common.icon_no = 17;
    item_common.message_no = 100;
    item_common.max_num = 30;
    std::strcpy(item_common.file_name, "VANILLA_TONIC");
    item_common.active_set = 1;
    item_common.stack_num = 10;
    item_common.icon_texture_no = 0;
    item_common.attribute = ITEM_ATTRIBUTE_USABLE;
    item_common.name = const_cast<char *>("Vanilla Tonic");
    CDataItem &item = GameItemDataManage.item_data[0];
    std::memset(&item, 0, sizeof(item));
    item.action_flags = ITEM_ACTION_USE;
    item.use_flags = ITEM_USE_FLAG_RESTORE_HP;
    item.target_flags = ITEM_TARGET_MAX | ITEM_TARGET_MONICA;
    item.value[0] = 8;
    GameItemDataManage.item_num = 1;
    GameItemDataManage.common_num = 2;
    GameItemDataManage.max_item_no = 0x112;
    local_itemdatano_converttable[0x20] = 0;
    local_itemdatano_converttable[0x112] = 1;
}

int NativeNumber(ContentFixture &fixture, const char *owner, const char *path, const std::string &id) {
    const moonmod::ApiCallResult result = fixture.Call(owner, path, Json::array({Json{{"id", id}}}));
    EXPECT_EQ(result.status, ApiCallStatus::Ok) << result.message;
    if (result.status != ApiCallStatus::Ok || !result.value.contains("native_item_no") || !result.value["native_item_no"].is_number_integer()) {
        return -1;
    }
    return result.value["native_item_no"].get<int>();
}

} // namespace

TEST(MoonModContentSlots, TwoRegistrationOrdersGiveTheSameNumbersOnceASlotsFileExists) {
    ContentFixture fixture("slots");
    constexpr const char *owner = "test.moonmod.content-slots";
    const Json tonic = {{"id", "tonic"}, {"name", "Moon Tonic"}, {"template_item", 0x112}};
    const Json elixir = {{"id", "elixir"}, {"name", "Moon Elixir"}, {"template_item", 0x112}};
    const Json blade = {{"id", "blade"}, {"name", "Moon Blade"}, {"template_weapon", 0x20}};

    SeedItemTables();
    ASSERT_EQ(fixture.Call(owner, "items.register", Json::array({tonic})).status, ApiCallStatus::Ok);
    ASSERT_EQ(fixture.Call(owner, "items.register", Json::array({elixir})).status, ApiCallStatus::Ok);
    ASSERT_EQ(fixture.Call(owner, "weapons.register", Json::array({blade})).status, ApiCallStatus::Ok);
    moonmod::ApplyChronicleTwoContent();
    const int tonic_no = NativeNumber(fixture, owner, "items.get", std::string(owner) + ":tonic");
    const int elixir_no = NativeNumber(fixture, owner, "items.get", std::string(owner) + ":elixir");
    const int blade_no = NativeNumber(fixture, owner, "weapons.get", std::string(owner) + ":blade");
    ASSERT_GT(tonic_no, 0);
    ASSERT_GT(elixir_no, 0);
    ASSERT_GT(blade_no, 0);
    EXPECT_NE(tonic_no, elixir_no);
    EXPECT_NE(tonic_no, blade_no);
    EXPECT_NE(elixir_no, blade_no);
    EXPECT_TRUE(std::filesystem::exists(moonmod::ChronicleTwoContentSlotFile(moonmod::ContentSlotKind::Item, fixture.root / "saves")));
    EXPECT_TRUE(std::filesystem::exists(moonmod::ChronicleTwoContentSlotFile(moonmod::ContentSlotKind::Weapon, fixture.root / "saves")));
    std::map<std::string, int> items;
    std::string error;
    ASSERT_TRUE(moonmod::LoadChronicleTwoContentSlots(moonmod::ContentSlotKind::Item, items, error)) << error;
    EXPECT_EQ(items.at(std::string(owner) + ":tonic"), tonic_no);
    EXPECT_EQ(items.at(std::string(owner) + ":elixir"), elixir_no);

    // A new process registers the same content in another order, and a new item, with the tonic gone (a tombstone).
    moonmod::ResetChronicleTwoContentForTests();
    SeedItemTables();
    const Json tincture = {{"id", "tincture"}, {"name", "Moon Tincture"}, {"template_item", 0x112}};
    ASSERT_EQ(fixture.Call(owner, "weapons.register", Json::array({blade})).status, ApiCallStatus::Ok);
    ASSERT_EQ(fixture.Call(owner, "items.register", Json::array({tincture})).status, ApiCallStatus::Ok);
    ASSERT_EQ(fixture.Call(owner, "items.register", Json::array({elixir})).status, ApiCallStatus::Ok);
    moonmod::ApplyChronicleTwoContent();
    EXPECT_EQ(NativeNumber(fixture, owner, "items.get", std::string(owner) + ":elixir"), elixir_no);
    EXPECT_EQ(NativeNumber(fixture, owner, "weapons.get", std::string(owner) + ":blade"), blade_no);
    const int tincture_no = NativeNumber(fixture, owner, "items.get", std::string(owner) + ":tincture");
    ASSERT_GT(tincture_no, 0);
    EXPECT_NE(tincture_no, tonic_no) << "the removed tonic keeps its number";
    EXPECT_NE(tincture_no, elixir_no);
    EXPECT_NE(tincture_no, blade_no);
    ASSERT_TRUE(moonmod::LoadChronicleTwoContentSlots(moonmod::ContentSlotKind::Item, items, error)) << error;
    EXPECT_EQ(items.at(std::string(owner) + ":tonic"), tonic_no);
    EXPECT_EQ(items.size(), 3u);
}

TEST(MoonModContentSlots, AnUnreadableSlotsFileLeavesTheItemUnmaterialized) {
    ContentFixture fixture("slots-bad");
    constexpr const char *owner = "test.moonmod.content-slots-bad";
    const auto file = moonmod::ChronicleTwoContentSlotFile(moonmod::ContentSlotKind::Item, fixture.root / "saves");
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file) << "{\"format_version\": 1, \"items\": {\"a:b\": 7, \"a:c\": 7}}";
    SeedItemTables();
    const Json tonic = {{"id", "tonic"}, {"name", "Moon Tonic"}, {"template_item", 0x112}};
    ASSERT_EQ(fixture.Call(owner, "items.register", Json::array({tonic})).status, ApiCallStatus::Ok);
    moonmod::ApplyChronicleTwoContent();
    const moonmod::ApiCallResult got = fixture.Call(owner, "items.get", Json::array({Json{{"id", std::string(owner) + ":tonic"}}}));
    ASSERT_EQ(got.status, ApiCallStatus::Ok) << got.message;
    EXPECT_TRUE(got.value.at("native_item_no").is_null()) << "a duplicate number in the file is refused, not guessed around";
    std::map<std::string, int> items;
    std::string error;
    EXPECT_FALSE(moonmod::LoadChronicleTwoContentSlots(moonmod::ContentSlotKind::Item, items, error));
    EXPECT_FALSE(error.empty());
}

TEST(MoonModContentWeapons, OwnerModelIconAndDescriptionReachTheNativeRow) {
    ContentFixture fixture("weapons");
    constexpr const char *owner = "test.moonmod.content-weapons";
    EXPECT_TRUE(fixture.Supported("weapons.register"));
    SeedItemTables();
    fixture.Own(owner, "moonblade.gltf", "mainchr/moonblade.gltf", "gltf-bytes", 10);
    fixture.Own(owner, "moonicon.png", "icons/moonicon.png", kTinyPng, sizeof(kTinyPng));

    const Json scaled = {{"id", "scaled"}, {"name", "Scaled"}, {"template_weapon", 0x20}, {"menu_scale", 2}};
    EXPECT_EQ(fixture.Call(owner, "weapons.register", Json::array({scaled})).status, ApiCallStatus::InvalidArgument);
    const Json unowned = {{"id", "unowned"}, {"name", "Unowned"}, {"template_weapon", 0x20}, {"model", "notmine"}};
    EXPECT_EQ(fixture.Call(owner, "weapons.register", Json::array({unowned})).status, ApiCallStatus::InvalidArgument);
    const Json nobody = {{"id", "nobody"}, {"name", "Nobody"}, {"template_weapon", 0x20}, {"owner", "goro"}};
    EXPECT_EQ(fixture.Call(owner, "weapons.register", Json::array({nobody})).status, ApiCallStatus::InvalidArgument);

    const Json blade = {{"id", "moonblade"}, {"name", "Moonblade"}, {"template_weapon", 0x20}, {"owner", "monica"},
                        {"model", "moonblade"}, {"icon", "moonicon"},
                        {"description", Json::array({"A blade of moonlight.", "Forged for Monica."})},
                        {"stats", {{"attack", 18}}}};
    const moonmod::ApiCallResult registered = fixture.Call(owner, "weapons.register", Json::array({blade}));
    ASSERT_EQ(registered.status, ApiCallStatus::Ok) << registered.message;
    EXPECT_EQ(registered.value.at("assets").at("owner_character"), "monica");
    EXPECT_EQ(registered.value.at("assets").at("model"), "mainchr/moonblade.gltf");
    EXPECT_EQ(registered.value.at("assets").at("icon"), "icons/moonicon.png");

    moonmod::ApplyChronicleTwoContent();
    const int item_no = NativeNumber(fixture, owner, "weapons.get", std::string(owner) + ":moonblade");
    ASSERT_GT(item_no, 0);
    const CDataCommon *common = GameItemDataManage.GetCommonData(item_no);
    ASSERT_NE(common, nullptr);
    EXPECT_EQ(common->type, ITEM_DATA_MONICA_MELEE) << "Max's melee template becomes Monica's for owner = monica";
    EXPECT_STREQ(common->file_name, "moonblade");
    EXPECT_EQ(common->message_no, moonmod::kModItemMessageBase);
    EXPECT_EQ(GetItemMessageNo(item_no, 1), moonmod::kModItemMessageBase + 10000);
    const auto description = moonmod::ChronicleTwoModMessageLine(GetItemMessageNo(item_no, 1));
    ASSERT_FALSE(description.empty());
    EXPECT_EQ(description.back(), MES_CODE_END);
    EXPECT_NE(std::ranges::find(description, MES_CODE_NEWLINE), description.end()) << "two lines";
    EXPECT_TRUE(moonmod::ChronicleTwoModMessageLine(moonmod::kModItemMessageBase + 10000 + 1).empty());
    EXPECT_EQ(moonmod::ChronicleTwoItemIconOverridePath(item_no), "icons/moonicon.png");
    EXPECT_EQ(GameItemDataManage.GetWeaponData(item_no)->status[0], 18);
    const auto encoded = dc2::text::EncodeMessageText("Ab");
    ASSERT_EQ(encoded.size(), 3u);
    EXPECT_EQ(encoded[0], 'A' - 0x20);
    EXPECT_EQ(encoded[2], MES_CODE_END);
}

TEST(MoonModContentItems, DescriptionAndIconOverrideRideTheRegisteredRow) {
    ContentFixture fixture("items");
    constexpr const char *owner = "test.moonmod.content-items";
    EXPECT_TRUE(fixture.Supported("items.register"));
    SeedItemTables();
    fixture.Own(owner, "cookie.png", "icons/cookie.png", kTinyPng, sizeof(kTinyPng));
    const Json modeled = {{"id", "modeled"}, {"name", "Modeled"}, {"template_item", 0x112}, {"model", "cookie"}};
    EXPECT_EQ(fixture.Call(owner, "items.register", Json::array({modeled})).status, ApiCallStatus::InvalidArgument)
        << "items take no model";
    const Json cookie = {{"id", "cookie"}, {"name", "Moon Cookie"}, {"template_item", 0x112}, {"icon", "cookie"},
                         {"description", "Baked under a full moon."}, {"effect", {{"kind", "heal"}, {"amount", 25}}}};
    const moonmod::ApiCallResult registered = fixture.Call(owner, "items.register", Json::array({cookie}));
    ASSERT_EQ(registered.status, ApiCallStatus::Ok) << registered.message;
    EXPECT_EQ(registered.value.at("assets").at("icon"), "icons/cookie.png");
    moonmod::ApplyChronicleTwoContent();
    const int item_no = NativeNumber(fixture, owner, "items.get", std::string(owner) + ":cookie");
    ASSERT_GT(item_no, 0);
    const CDataCommon *common = GameItemDataManage.GetCommonData(item_no);
    ASSERT_NE(common, nullptr);
    EXPECT_STREQ(common->name, "Moon Cookie");
    EXPECT_EQ(common->message_no, moonmod::kModItemMessageBase);
    EXPECT_FALSE(moonmod::ChronicleTwoModMessageLine(GetItemMessageNo(item_no, 1)).empty());
    EXPECT_EQ(moonmod::ChronicleTwoItemIconOverridePath(item_no), "icons/cookie.png");
    std::map<std::string, int> items;
    std::string error;
    ASSERT_TRUE(moonmod::LoadChronicleTwoContentSlots(moonmod::ContentSlotKind::Item, items, error)) << error;
    EXPECT_EQ(items.at(std::string(owner) + ":cookie"), item_no);
    const moonmod::ApiCallResult listed = fixture.Call(owner, "content.list", Json::array());
    ASSERT_EQ(listed.status, ApiCallStatus::Ok);
    EXPECT_EQ(listed.value.at("entries").at(0).at("assets").at("description"), "Baked under a full moon.");
}

TEST(MoonModContentNpcs, RegistersAVillagerListsItAndAnswersTalk) {
    ContentFixture fixture("npcs");
    constexpr const char *owner = "test.moonmod.content-npcs";
    EXPECT_TRUE(fixture.Supported("characters.register"));
    const Json trader = {{"id", "moon_trader"}, {"kind", "npc"}, {"name", "Moon Trader"}, {"map_no", 10},
                         {"model", "t01_02"}, {"position", {{"x", 120.5}, {"y", 0}, {"z", -40}}}, {"facing", 90}};
    const moonmod::ApiCallResult registered = fixture.Call(owner, "characters.register", Json::array({trader}));
    ASSERT_EQ(registered.status, ApiCallStatus::Ok) << registered.message;
    EXPECT_EQ(registered.value.at("id"), std::string(owner) + ":moon_trader");
    EXPECT_EQ(registered.value.at("kind"), "npc");
    EXPECT_EQ(registered.value.at("model"), "chara/t01_02.gltf");
    EXPECT_EQ(registered.value.at("status"), "registered");
    const int chara_no = registered.value.at("chara_no").get<int>();
    EXPECT_GE(chara_no, moonmod::kModVillagerCharaBase);
    EXPECT_EQ(fixture.Call(owner, "characters.register", Json::array({trader})).status, ApiCallStatus::InvalidArgument);
    Json bad = trader;
    bad["id"] = "bad";
    bad.erase("position");
    EXPECT_EQ(fixture.Call(owner, "characters.register", Json::array({bad})).status, ApiCallStatus::InvalidArgument);
    bad = trader;
    bad["id"] = "bad2";
    bad["extra"] = 1;
    EXPECT_EQ(fixture.Call(owner, "characters.register", Json::array({bad})).status, ApiCallStatus::InvalidArgument);

    const moonmod::ApiCallResult listed = fixture.Call(owner, "characters.list", Json::array());
    ASSERT_EQ(listed.status, ApiCallStatus::Ok) << listed.message;
    EXPECT_TRUE(std::ranges::any_of(listed.value, [owner](const Json &entry) {
        return entry.value("id", "") == std::string(owner) + ":moon_trader" && entry.value("kind", "") == "npc";
    }));
    const moonmod::ApiCallResult got = fixture.Call(owner, "characters.get", Json::array({std::string(owner) + ":moon_trader"}));
    ASSERT_EQ(got.status, ApiCallStatus::Ok) << got.message;
    EXPECT_EQ(got.value.at("name"), "Moon Trader");
    EXPECT_EQ(got.value.at("map_no"), 10);

    CSceneEventData event{};
    event.chara_no = chara_no;
    event.chara_slot = 9;
    EXPECT_TRUE(moonmod::ChronicleTwoModVillagerTalk(nullptr, &event)) << "no native talk event: game.npc.talk is queued";
    event.chara_no = 3;
    EXPECT_FALSE(moonmod::ChronicleTwoModVillagerTalk(nullptr, &event)) << "a native villager runs event 1000";
    EXPECT_EQ(moonmod::ChronicleTwoLoadModVillagers(nullptr, 10, 0x20, nullptr, 0), 0);

    moonmod::UnloadChronicleTwoNpcs(owner);
    EXPECT_TRUE(moonmod::ChronicleTwoNpcJson(std::string(owner) + ":moon_trader").is_null());
    event.chara_no = chara_no;
    EXPECT_FALSE(moonmod::ChronicleTwoModVillagerTalk(nullptr, &event));
}

TEST(MoonModContentPlayable, RegistersAHeroRoutesHisRecordAndPersistsItBesideTheSave) {
    ContentFixture fixture("playable");
    constexpr const char *owner = "test.moonmod.content-playable";
    EXPECT_TRUE(fixture.Supported("characters.register_playable"));
    fixture.Own(owner, "opar.gltf", "mainchr/opar.gltf", "gltf-bytes", 10);

    const Json bad_pack = {{"id", "ghost"}, {"name", "Ghost"}, {"pack", "mainchr/nothere.gltf"}};
    EXPECT_EQ(fixture.Call(owner, "characters.register_playable", Json::array({bad_pack})).status, ApiCallStatus::InvalidArgument);
    const Json bad_body = {{"id", "ghost"}, {"name", "Ghost"}, {"pack", "c02_base"}, {"fights_as", "toan"}};
    EXPECT_EQ(fixture.Call(owner, "characters.register_playable", Json::array({bad_body})).status, ApiCallStatus::InvalidArgument);

    const Json opar = {{"id", "opar"}, {"name", "Opar"}, {"pack", "mainchr/opar.gltf"}, {"fights_as", "monica"},
                       {"title", "Moon Sea Frog Mage"}, {"hp", 77}, {"defense", 9}};
    const moonmod::ApiCallResult registered = fixture.Call(owner, "characters.register_playable", Json::array({opar}));
    ASSERT_EQ(registered.status, ApiCallStatus::Ok) << registered.message;
    const std::string id = std::string(owner) + ":opar";
    EXPECT_EQ(registered.value.at("id"), id);
    const int index = registered.value.at("character_id").get<int>();
    EXPECT_EQ(index, moonmod::kRegisteredCharacterBase);
    EXPECT_EQ(registered.value.at("count"), moonmod::kRegisteredCharacterBase + 1);
    const Json native = {{"id", "lune"}, {"name", "Lune"}, {"pack", "c01_base"}};
    const moonmod::ApiCallResult second = fixture.Call(owner, "characters.register_playable", Json::array({native}));
    ASSERT_EQ(second.status, ApiCallStatus::Ok) << second.message;
    EXPECT_EQ(second.value.at("character_id"), index + 1);
    EXPECT_EQ(fixture.Call("test.moonmod.other", "characters.register_playable", Json::array({opar})).status, ApiCallStatus::InvalidArgument)
        << "another mod's id is refused";

    // The record routes through the user data accessors and the model name.
    CUserDataManager *user_data = GetUserDataMan();
    ASSERT_NE(user_data, nullptr);
    CHARA_DATA *data = user_data->GetCharaDataPtr(index);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data, moonmod::ChronicleTwoRegisteredCharaData(index));
    EXPECT_FLOAT_EQ(data->hp.max, 77.0f);
    EXPECT_EQ(data->defence, 9);
    EXPECT_EQ(user_data->GetCharaHpGage(index), &data->hp);
    EXPECT_EQ(user_data->GetCharaDataPtr(index + 5), nullptr);
    ASSERT_TRUE(MainCharaModelName(index, 0));
    EXPECT_EQ(*MainCharaModelName(index, 0), "opar.gltf");
    EXPECT_EQ(*MainCharaModelName(index + 1, 0), "c01_base2.gltf") << "a native pack follows Max's numbering";
    EXPECT_EQ(*MainCharaModelName(index + 1, 1), "c01_base.gltf");
    int body = -1;
    ASSERT_TRUE(moonmod::ChronicleTwoRegisteredCharacterBody(index, body));
    EXPECT_EQ(body, USER_CHARA_MONICA);

    const moonmod::ApiCallResult got = fixture.Call(owner, "characters.get", Json::array({id}));
    ASSERT_EQ(got.status, ApiCallStatus::Ok) << got.message;
    EXPECT_EQ(got.value.at("name"), "Opar");
    EXPECT_EQ(got.value.at("title"), "Moon Sea Frog Mage");
    EXPECT_EQ(got.value.at("fights_as"), "monica");
    EXPECT_EQ(got.value.at("kind"), "playable");
    EXPECT_FALSE(got.value.at("in_party").get<bool>());
    EXPECT_EQ(got.value.at("max_hp"), 77.0f);
    const moonmod::ApiCallResult listed = fixture.Call(owner, "characters.list", Json::array());
    ASSERT_EQ(listed.status, ApiCallStatus::Ok);
    EXPECT_TRUE(std::ranges::any_of(listed.value, [&id](const Json &entry) { return entry.value("id", "") == id; }));
    const moonmod::ApiCallResult healed = fixture.Call(owner, "player.damage", Json::array({Json{{"amount", 7}, {"character", id}}}));
    ASSERT_EQ(healed.status, ApiCallStatus::Ok) << healed.message;
    EXPECT_FLOAT_EQ(data->hp.now, 70.0f);
    const moonmod::ApiCallResult switched = fixture.Call(owner, "party.set_active", Json::array({Json{{"id", id}}}));
    EXPECT_EQ(switched.status, ApiCallStatus::Unsupported);
    EXPECT_NE(switched.message.find("unsupported"), std::string::npos);

    // A weapon owned by the hero joins the weapons of the character he fights as.
    SeedItemTables();
    const Json staff = {{"id", "staff"}, {"name", "Frog Staff"}, {"template_weapon", 0x20}, {"owner", id}};
    const moonmod::ApiCallResult weapon = fixture.Call(owner, "weapons.register", Json::array({staff}));
    ASSERT_EQ(weapon.status, ApiCallStatus::Ok) << weapon.message;
    EXPECT_EQ(weapon.value.at("assets").at("owner_character"), "monica");
    EXPECT_EQ(weapon.value.at("assets").at("holder"), id);

    // Persistence beside the save, keyed by the save identity, with tombstones for unregistered ids.
    const moonmod::ApiCallResult joined = fixture.Call(owner, "party.recruit", Json::array({Json{{"id", id}}}));
    ASSERT_EQ(joined.status, ApiCallStatus::Ok) << joined.message;
    EXPECT_TRUE(joined.value.at("changed").get<bool>());
    EXPECT_TRUE(moonmod::ChronicleTwoRegisteredCharacterJoined(index));
    EXPECT_EQ(fixture.Call(owner, "party.recruit", Json::array({Json{{"id", "nobody"}}})).status, ApiCallStatus::InvalidArgument);
    data->equip[CHARA_EQUIP_MELEE].item_no = 0x20;
    data->status_attr = 3;
    ASSERT_TRUE(moonmod::ChronicleTwoRegisteredCharactersSave("0000000000000001-0000000000000002"));
    ASSERT_TRUE(std::filesystem::exists(moonmod::ChronicleTwoRegisteredCharactersFile()));
    EXPECT_EQ(moonmod::ChronicleTwoRegisteredCharactersFile(), fixture.root / "saves" / "moonmod" / "characters.json");
    moonmod::ChronicleTwoResetRegisteredCharacterState();
    EXPECT_FLOAT_EQ(data->hp.now, 77.0f);
    EXPECT_FALSE(moonmod::ChronicleTwoRegisteredCharacterJoined(index));
    ASSERT_TRUE(moonmod::ChronicleTwoRegisteredCharactersLoad("0000000000000001-0000000000000002"));
    EXPECT_FLOAT_EQ(data->hp.now, 70.0f);
    EXPECT_EQ(data->equip[CHARA_EQUIP_MELEE].item_no, 0x20);
    EXPECT_EQ(data->status_attr, 3);
    EXPECT_TRUE(moonmod::ChronicleTwoRegisteredCharacterJoined(index));
    const moonmod::ApiCallResult party = fixture.Call(owner, "party.list", Json::array());
    ASSERT_EQ(party.status, ApiCallStatus::Ok);
    EXPECT_TRUE(std::ranges::any_of(party.value, [&id](const Json &entry) { return entry.value("id", "") == id; }));
    ASSERT_TRUE(moonmod::ChronicleTwoRegisteredCharactersLoad("0000000000000001-0000000000000003"));
    EXPECT_FLOAT_EQ(data->hp.now, 77.0f) << "another save starts him afresh";
    ASSERT_TRUE(moonmod::ChronicleTwoRegisteredCharactersLoad("0000000000000001-0000000000000002"));
    const moonmod::ApiCallResult left = fixture.Call(owner, "party.recruit", Json::array({Json{{"id", id}, {"joined", false}}}));
    ASSERT_EQ(left.status, ApiCallStatus::Ok) << left.message;
    EXPECT_FALSE(moonmod::ChronicleTwoRegisteredCharacterJoined(index));
    moonmod::SetChronicleTwoRegisteredCharacterJoined(index, true);
    user_data->Initialize();
    EXPECT_FALSE(moonmod::ChronicleTwoRegisteredCharacterJoined(index)) << "a new game resets the heroes";

    moonmod::UnloadChronicleTwoRegisteredCharacters(owner);
    EXPECT_EQ(user_data->GetCharaDataPtr(index), nullptr);
    std::ifstream file(moonmod::ChronicleTwoRegisteredCharactersFile());
    const Json saved = Json::parse(file, nullptr, false);
    ASSERT_TRUE(saved.is_object());
    EXPECT_TRUE(saved.at("saves").at("0000000000000001-0000000000000002").contains(id)) << "the record outlives the mod";
}

TEST(MoonModContentGeorama, PersistentDefinitionIdModelPartStockTownAndSaveRoundTrip) {
    ContentFixture fixture("georama");
    constexpr const char *owner = "test.moonmod.content-georama";
    EXPECT_TRUE(fixture.Supported("georama.register"));

    alignas(64) u_long128 storage[9000]{};
    mgCMemory scene_memory;
    scene_memory.stSetBuffer(storage, static_cast<int>(std::size(storage)));
    auto build_map = [&scene_memory](CEditMap &map, int town, CList<CMapParts> &native_node, CList<CMapParts> &other_node) {
        map.area_no = town;
        const int info_quads = static_cast<int>((sizeof(CEditPartsInfo) + 15u) / 16u + 2u);
        u_long128 *info_memory = scene_memory.Alloc(info_quads);
        auto *table = new (info_memory) CEditPartsInfo[1];
        table[0].id = 7;
        table[0].attr = EDIT_PARTS_ATR_QUARTER_TURN;
        table[0].max_num = 9;
        table[0].parts_name = mgCopyString(const_cast<char *>("NativePillarModel"), &scene_memory);
        table[0].edit_name = mgCopyString(const_cast<char *>("Native Pillar"), &scene_memory);
        native_node.data.SetName(const_cast<char *>("NativePillarModel"));
        other_node.data.SetName(const_cast<char *>("MoonLanternModel"));
        map.AddParts(&native_node);
        map.AddParts(&other_node);
        table[0].parts = &native_node.data;
        map.info_mngr.SetePartsInfoTable(table, 1);
    };
    CEditMap map3;
    CList<CMapParts> native3;
    CList<CMapParts> other3;
    build_map(map3, 3, native3, other3);

    const Json bad_scene = {{"id", "house"}, {"name", "Moon House"}, {"template_part_id", 7},
                            {"model", {{"scene", "gedit/mod/x.scn"}, {"part", "e01h05"}}}};
    const moonmod::ApiCallResult refused = fixture.Call(owner, "georama.register", Json::array({bad_scene}));
    EXPECT_EQ(refused.status, ApiCallStatus::InvalidArgument);
    EXPECT_NE(refused.message.find("unsupported"), std::string::npos);
    const Json lantern = {{"id", "lantern"}, {"name", "Moon Lantern"}, {"template_part_id", 7}, {"town_id", 3}, {"stock", 5},
                          {"model", {{"part", "MoonLanternModel"}}}, {"territory", {{"radius", 40}, {"height", 90}}}};
    const Json bench = {{"id", "bench"}, {"name", "Moon Bench"}, {"template_part_id", 7}};
    ASSERT_EQ(fixture.Call(owner, "georama.register", Json::array({lantern})).status, ApiCallStatus::Ok);
    ASSERT_EQ(fixture.Call(owner, "georama.register", Json::array({bench})).status, ApiCallStatus::Ok);
    moonmod::ApplyChronicleTwoGeorama(&map3, &scene_memory);
    const std::string lantern_id = std::string(owner) + ":lantern";
    const std::string bench_id = std::string(owner) + ":bench";
    const int lantern_no = moonmod::ChronicleTwoGeoramaNativePartId(lantern_id, 3);
    const int bench_no = moonmod::ChronicleTwoGeoramaNativePartId(bench_id, 3);
    // Ids are handed out from the top down (in the adapter's id order, not registration order).
    EXPECT_GE(lantern_no, moonmod::kModGeoramaPartIdTop - 1);
    EXPECT_GE(bench_no, moonmod::kModGeoramaPartIdTop - 1);
    EXPECT_LE(lantern_no, moonmod::kModGeoramaPartIdTop);
    EXPECT_LE(bench_no, moonmod::kModGeoramaPartIdTop);
    EXPECT_NE(lantern_no, bench_no);
    CEditPartsInfo *lantern_info = map3.info_mngr.GetePartsInfoAtID(lantern_no);
    ASSERT_NE(lantern_info, nullptr);
    EXPECT_EQ(lantern_info->parts, &other3.data) << "the model comes from the named scene part";
    EXPECT_STREQ(lantern_info->parts_name, "MoonLanternModel");
    EXPECT_FLOAT_EQ(lantern_info->territory_radius, 40.0f);
    EXPECT_FLOAT_EQ(lantern_info->territory_height, 90.0f);
    EXPECT_EQ(lantern_info->max_num, 9) << "the rest stays the template's";
    // The Make list (CEditMenu's make_list) takes every definition with an edit name and no NO_REFUND attribute.
    bool in_make_list = false;
    for (int index = 0; CEditPartsInfo *info = map3.info_mngr.GetePartsInfo(index); ++index) {
        if (info->id == lantern_no && info->edit_name != nullptr && std::string_view(info->edit_name) == "Moon Lantern" &&
            (info->attr & EDIT_PARTS_ATR_NO_REFUND) == 0) {
            in_make_list = true;
        }
    }
    EXPECT_TRUE(in_make_list);
    EXPECT_EQ(fixture.save->GetBuildPartsNum(lantern_no), 5) << "stock reaches the save once";
    EXPECT_EQ(fixture.save->GetBuildPartsNum(bench_no), 0);
    moonmod::ApplyChronicleTwoGeorama(&map3, &scene_memory);
    EXPECT_EQ(fixture.save->GetBuildPartsNum(lantern_no), 5);

    CEditMap map2;
    CList<CMapParts> native2;
    CList<CMapParts> other2;
    build_map(map2, 2, native2, other2);
    moonmod::ApplyChronicleTwoGeorama(&map2, &scene_memory);
    EXPECT_EQ(moonmod::ChronicleTwoGeoramaNativePartId(lantern_id, 2), -1) << "town_id = 3 keeps it out of town 2";
    EXPECT_EQ(moonmod::ChronicleTwoGeoramaNativePartId(bench_id, 2), bench_no) << "the same id in every town";

    const moonmod::ApiCallResult town = fixture.Call(owner, "georama.get", Json::array({Json{{"town_id", 3}}}));
    ASSERT_EQ(town.status, ApiCallStatus::Ok) << town.message;
    const auto piece = std::ranges::find(town.value.at("mod_pieces"), lantern_id,
                                         [](const Json &entry) { return entry.at("id").get<std::string>(); });
    ASSERT_NE(piece, town.value.at("mod_pieces").end());
    EXPECT_EQ(piece->at("extras").at("definition_id"), lantern_no);
    EXPECT_EQ(piece->at("extras").at("stock"), 5);
    EXPECT_EQ(piece->at("extras").at("town_id"), 3);

    // Another process registers the pieces the other way round: the slots file keeps their ids.
    moonmod::ResetChronicleTwoContentForTests();
    ASSERT_EQ(fixture.Call(owner, "georama.register", Json::array({bench})).status, ApiCallStatus::Ok);
    ASSERT_EQ(fixture.Call(owner, "georama.register", Json::array({lantern})).status, ApiCallStatus::Ok);
    CEditMap map3b;
    CList<CMapParts> native3b;
    CList<CMapParts> other3b;
    build_map(map3b, 3, native3b, other3b);
    moonmod::ApplyChronicleTwoGeorama(&map3b, &scene_memory);
    EXPECT_EQ(moonmod::ChronicleTwoGeoramaNativePartId(lantern_id, 3), lantern_no);
    EXPECT_EQ(moonmod::ChronicleTwoGeoramaNativePartId(bench_id, 3), bench_no);
    std::map<std::string, int> slots;
    std::string error;
    ASSERT_TRUE(moonmod::LoadChronicleTwoContentSlots(moonmod::ContentSlotKind::Georama, slots, error)) << error;
    EXPECT_EQ(slots.at(lantern_id), lantern_no);

    // A town layout that places the part survives the JSON save.
    SaveDocument document;
    TownPart placed{};
    placed.id = lantern_no;
    placed.state = 1;
    placed.angle = 6;
    placed.pos = {20, 0, 30};
    document.towns[3].parts.push_back(placed);
    const std::string text = SaveJsonWrite(document);
    SaveDocument reloaded;
    ASSERT_EQ(SaveJsonRead(text, reloaded), SaveJsonError::None);
    ASSERT_EQ(reloaded.towns[3].parts.size(), 1u);
    EXPECT_EQ(reloaded.towns[3].parts[0].id, lantern_no);
    EXPECT_EQ(reloaded.towns[3].parts[0].angle, 6);

    // The stock reaches a save once: a save that has the piece placed (and none built) gets none in a later process.
    CEditData *town3 = fixture.save->GetEditData(3);
    ASSERT_NE(town3, nullptr);
    fixture.save->AddBuildPartsNum(lantern_no, -fixture.save->GetBuildPartsNum(lantern_no));
    PortTownLayout(*town3).parts.push_back(placed);
    EXPECT_EQ(fixture.save->GetPlaceEditPartsNum(lantern_no), 1);
    moonmod::ResetChronicleTwoContentForTests();
    ASSERT_EQ(fixture.Call(owner, "georama.register", Json::array({lantern})).status, ApiCallStatus::Ok);
    CEditMap map3c;
    CList<CMapParts> native3c;
    CList<CMapParts> other3c;
    build_map(map3c, 3, native3c, other3c);
    moonmod::ApplyChronicleTwoGeorama(&map3c, &scene_memory);
    EXPECT_EQ(moonmod::ChronicleTwoGeoramaNativePartId(lantern_id, 3), lantern_no);
    EXPECT_EQ(fixture.save->GetBuildPartsNum(lantern_no), 0);
    PortClearTownLayout(*town3);
}
