// Co-op's shared world as it changes (moonmod/coop.hpp, "The world as it changes"): the parts of the save that are
// the world's and change while two games play, told to a mod as they change in this game and taken from it as they
// change in another.
//
// Nothing in the game calls this. Once a frame, while a town or a floor runs, the live save is compared with what the
// two games last agreed on (the baseline); each difference is one game.world.changed event for the mod that asked to
// watch, and becomes the baseline. A change that comes from the other game (world.change_apply) is written to the save
// and to the baseline together, so it is not told back. A floor's record is merged, not replaced: what either game
// achieved on a floor stands.

#include "moonmod/coop.hpp"
#include "moonmod/coop_save.hpp"
#include "moonmod/coop_story.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <vector>

#include "dng_main.hpp"
#include "gamedata.hpp"
#include "mainloop.hpp"
#include "savedata.hpp"
#include "savedatadungeon.hpp"
#include "userdata.hpp"

namespace moonmod::coop {

namespace {

constexpr int kFlags = SAVE_BIT_FLAG_MAX;
constexpr int kCounters = SAVE_SHORT_FLAG_MAX;
constexpr int kRecruits = 32;
constexpr int kFloors = SAVE_DUNGEON_FLOOR_NUM;
constexpr int kChangesAFrame = 48; // a scene sets a handful; a loaded save differs everywhere and is not a change
constexpr int kTowns = SAVE_EDIT_DATA_MAX;
constexpr int kStock = SAVE_BUILD_PARTS_MAX;
constexpr int kTownEvery = 20; // frames between two looks at the towns' Georama (a look reads every town whole)

// What the two games share of the save, as last agreed.
struct Shared {
    std::array<u32, kFlags / 32>         flags{};
    std::array<s16, kCounters>           counters{};
    s32                                  progress = 0;
    std::array<int, 3>                   party{};
    std::array<std::array<int, 2>, kRecruits> recruits{};
    std::array<DNG_FLOOR_SAVE, kFloors>  floors{};
    std::array<std::uint64_t, kTowns>    towns{}; // each town's Georama, as TownFingerprint
    std::array<s16, kStock>              stock{}; // Georama parts built and waiting to be placed
};

// What the story's scripts gave this game's player since the last step: {item_no, count}.
std::vector<std::array<int, 2>> g_gifts;
bool                            g_system_script = false; // a dungeon's system script runs: its gifts are the player's own
constexpr std::size_t           kGiftsKept = 32;

std::unique_ptr<Shared> g_shared;
std::string             g_watcher; // the mod told of changes; empty: nobody watches
bool                    g_running = false; // the last frame was a town's or a floor's (else the next one rebases)

bool InPlay() {
    const int loop = GetNowLoopNo();
    return loop == LOOP_EDIT || loop == LOOP_DUNGEON;
}

int PartyField(const CUserDataManager &user, int field) {
    return field == 0 ? user.party_member : field == 1 ? user.chara_change : user.chara_change_mask;
}

void SetPartyField(CUserDataManager &user, int field, int value) {
    if (field == 0) user.party_member = static_cast<u16>(value);
    else if (field == 1) user.chara_change = static_cast<u16>(value);
    else user.chara_change_mask = static_cast<u8>(value);
}

Json FloorJson(const DNG_FLOOR_SAVE &floor) {
    return Json::array({floor.unk_0, floor.fast_destroy_time, floor.unk_8, floor.unk_a, floor.unk_b, floor.spheda_clear, floor.flag,
                        floor.kill_count, floor.visit_count});
}

bool SameFloor(const DNG_FLOOR_SAVE &a, const DNG_FLOOR_SAVE &b) { return std::memcmp(&a, &b, sizeof(a)) == 0; }

// What either game did on a floor stands: its flags together, the higher counts, the better time.
DNG_FLOOR_SAVE MergeFloor(const DNG_FLOOR_SAVE &mine, const DNG_FLOOR_SAVE &theirs) {
    DNG_FLOOR_SAVE out = mine;
    out.unk_0 = std::max(mine.unk_0, theirs.unk_0);
    if (theirs.fast_destroy_time > 0 && (mine.fast_destroy_time <= 0 || theirs.fast_destroy_time < mine.fast_destroy_time)) {
        out.fast_destroy_time = theirs.fast_destroy_time;
    }
    out.unk_8 = std::max(mine.unk_8, theirs.unk_8);
    out.unk_a = std::max(mine.unk_a, theirs.unk_a);
    out.unk_b = std::max(mine.unk_b, theirs.unk_b);
    out.spheda_clear = std::max(mine.spheda_clear, theirs.spheda_clear);
    out.flag = static_cast<u16>(mine.flag | theirs.flag);
    out.kill_count = std::max(mine.kill_count, theirs.kill_count);
    out.visit_count = std::max(mine.visit_count, theirs.visit_count);
    return out;
}

void Rebase(const CSaveData &save) {
    if (!g_shared) g_shared = std::make_unique<Shared>();
    Shared &shared = *g_shared;
    std::copy(std::begin(save.bit_flag), std::end(save.bit_flag), shared.flags.begin());
    std::copy(std::begin(save.short_flag), std::end(save.short_flag), shared.counters.begin());
    shared.progress = save.game_progress;
    for (int field = 0; field < 3; ++field) shared.party[static_cast<std::size_t>(field)] = PartyField(save.user_data, field);
    for (int i = 0; i < kRecruits; ++i) {
        shared.recruits[static_cast<std::size_t>(i)] = {save.user_data.party_chara[i].chara_no, save.user_data.party_chara[i].status};
    }
    std::copy(std::begin(save.save_dungeon.floor_info), std::end(save.save_dungeon.floor_info), shared.floors.begin());
    for (int town = 0; town < kTowns; ++town) shared.towns[static_cast<std::size_t>(town)] = TownFingerprint(save, town);
    std::copy(std::begin(save.build_parts_num), std::end(save.build_parts_num), shared.stock.begin());
}

} // namespace

void RegisterWatch(std::string_view owner, bool enabled) {
    if (!enabled) {
        if (g_watcher == owner) g_watcher.clear();
        return;
    }
    g_watcher = std::string(owner);
    g_running = false; // the world as it stands now is where the two games begin
}

bool RegisterWatched() { return !g_watcher.empty(); }

void RegisterRebase() { g_running = false; }

void RegisterOwnerUnloaded(std::string_view owner) {
    if (g_watcher == owner) g_watcher.clear();
}

void RegisterReset() {
    g_watcher.clear();
    g_running = false;
    g_shared.reset();
    g_gifts.clear();
    g_system_script = false;
}

void SystemScriptBegan() { g_system_script = true; }

void StoryGave(int item_no, int count) {
    if (g_watcher.empty() || item_no <= 0 || count <= 0) return;
    if (GetNowLoopNo() == LOOP_DUNGEON && g_system_script) return; // a chest, a prize: each player's own
    if (g_gifts.size() < kGiftsKept) g_gifts.push_back({item_no, std::min(count, 999)});
}

void RegisterStep() {
    // A dungeon's system script is over when the floor is the player's again.
    if (g_system_script && (GetNowLoopNo() != LOOP_DUNGEON || DngStatus.mode != DNG_STATUS_EVENT)) g_system_script = false;
    if (g_watcher.empty()) {
        g_gifts.clear();
        return;
    }
    CSaveData *save = GetSaveData();
    if (save == nullptr || !InPlay()) {
        g_running = false;
        return;
    }
    if (!g_running) { // a town or a floor begins (a save loaded, a visit begun): nothing to tell
        Rebase(*save);
        g_running = true;
        return;
    }
    Shared &shared = *g_shared;
    int     told = 0;
    const auto tell = [&told](const char *kind, int id, Json value) {
        ++told;
        SharedRuntime().PublishToMod(g_watcher, "game.world.changed",
                                     {{"kind", kind}, {"id", id}, {"value", std::move(value)}, {"frame", SharedRuntime().Frame()}});
    };
    // What a scene gave this player: told once, as it was given (not a difference of the save: the bag is the
    // player's own, and only the story's gifts are for both).
    for (const std::array<int, 2> &gift : g_gifts) tell("gift", gift[0], gift[1]);
    g_gifts.clear();
    for (int word = 0; word < kFlags / 32 && told < kChangesAFrame; ++word) {
        u32 differs = save->bit_flag[word] ^ shared.flags[static_cast<std::size_t>(word)];
        for (int bit = 0; differs != 0 && bit < 32 && told < kChangesAFrame; ++bit) {
            const u32 mask = 1u << bit;
            if ((differs & mask) == 0) continue;
            differs &= ~mask;
            shared.flags[static_cast<std::size_t>(word)] ^= mask;
            tell("flag", word * 32 + bit, (save->bit_flag[word] & mask) != 0);
        }
    }
    for (int i = 0; i < kCounters && told < kChangesAFrame; ++i) {
        if (save->short_flag[i] == shared.counters[static_cast<std::size_t>(i)]) continue;
        shared.counters[static_cast<std::size_t>(i)] = save->short_flag[i];
        tell("counter", i, save->short_flag[i]);
    }
    if (save->game_progress != shared.progress && told < kChangesAFrame) {
        shared.progress = save->game_progress;
        tell("progress", 0, save->game_progress);
    }
    for (int field = 0; field < 3 && told < kChangesAFrame; ++field) {
        const int now = PartyField(save->user_data, field);
        if (now == shared.party[static_cast<std::size_t>(field)]) continue;
        shared.party[static_cast<std::size_t>(field)] = now;
        tell("party", field, now);
    }
    for (int i = 0; i < kRecruits && told < kChangesAFrame; ++i) {
        const std::array<int, 2> now = {save->user_data.party_chara[i].chara_no, save->user_data.party_chara[i].status};
        if (now == shared.recruits[static_cast<std::size_t>(i)]) continue;
        shared.recruits[static_cast<std::size_t>(i)] = now;
        tell("recruit", i, Json::array({now[0], now[1]}));
    }
    for (int i = 0; i < kFloors && told < kChangesAFrame; ++i) {
        const DNG_FLOOR_SAVE &now = save->save_dungeon.floor_info[i];
        if (SameFloor(now, shared.floors[static_cast<std::size_t>(i)])) continue;
        shared.floors[static_cast<std::size_t>(i)] = now;
        tell("floor", i, FloorJson(now));
    }
    for (int i = 0; i < kStock && told < kChangesAFrame; ++i) {
        if (save->build_parts_num[i] == shared.stock[static_cast<std::size_t>(i)]) continue;
        shared.stock[static_cast<std::size_t>(i)] = save->build_parts_num[i];
        tell("stock", i, save->build_parts_num[i]);
    }
    // A town's Georama: only that it changed. The mod sends the town itself (georama.town_export), which is far
    // more than a value, and a town it takes from another game is agreed as it arrives (RegisterTownAgreed). A
    // town being built is first written into its record (TownLiveSync), so each part placed is a change.
    if (SharedRuntime().Frame() % kTownEvery == 0) {
        TownLiveSync();
        for (int town = 0; town < kTowns && told < kChangesAFrame; ++town) {
            const std::uint64_t now = TownFingerprint(*save, town);
            if (now == shared.towns[static_cast<std::size_t>(town)]) continue;
            shared.towns[static_cast<std::size_t>(town)] = now;
            tell("town", town, static_cast<std::uint64_t>(now & 0xFFFFFFFFull));
        }
    }
}

void RegisterTownAgreed(int town) {
    CSaveData *save = GetSaveData();
    if (!g_shared || save == nullptr || town < 0 || town >= kTowns) return;
    g_shared->towns[static_cast<std::size_t>(town)] = TownFingerprint(*save, town);
}

bool RegisterApply(std::string_view kind, int id, const Json &value, std::string &error) {
    CSaveData *save = GetSaveData();
    if (save == nullptr) {
        error = "the game has no save data yet";
        return false;
    }
    if (!g_shared || !g_running) { // not in play yet: the save takes it, and the next town or floor begins from it
        if (!g_shared) g_shared = std::make_unique<Shared>();
        Rebase(*save);
    }
    Shared    &shared = *g_shared;
    const auto whole = [&value](long long low, long long high, long long &out) {
        if (!value.is_number_integer()) return false;
        out = value.get<long long>();
        return out >= low && out <= high;
    };
    long long number = 0;
    if (kind == "flag") {
        if (id < 0 || id >= kFlags || !value.is_boolean()) {
            error = "a flag is {id = 0-" + std::to_string(kFlags - 1) + ", value = true or false}";
            return false;
        }
        const u32 mask = 1u << (id % 32);
        const bool on = value.get<bool>();
        u32 &live = save->bit_flag[id / 32], &agreed = shared.flags[static_cast<std::size_t>(id / 32)];
        live = on ? (live | mask) : (live & ~mask);
        agreed = on ? (agreed | mask) : (agreed & ~mask);
        return true;
    }
    if (kind == "counter") {
        if (id < 0 || id >= kCounters || !whole(-32768, 32767, number)) {
            error = "a counter is {id = 0-" + std::to_string(kCounters - 1) + ", value = -32768 to 32767}";
            return false;
        }
        save->short_flag[id] = shared.counters[static_cast<std::size_t>(id)] = static_cast<s16>(number);
        return true;
    }
    if (kind == "progress") {
        if (id != 0 || !whole(0, 1000, number)) {
            error = "the story's progress is {id = 0, value = 0-1000}";
            return false;
        }
        save->game_progress = shared.progress = static_cast<s32>(number);
        return true;
    }
    if (kind == "party") {
        if (id < 0 || id > 2 || !whole(0, id == 2 ? 0xFF : 0xFFFF, number)) {
            error = "a party field is {id = 0-2, value = its bits}";
            return false;
        }
        SetPartyField(save->user_data, id, static_cast<int>(number));
        shared.party[static_cast<std::size_t>(id)] = static_cast<int>(number);
        return true;
    }
    if (kind == "recruit") {
        if (id < 0 || id >= kRecruits || !value.is_array() || value.size() != 2 || !value[0].is_number_integer() || !value[1].is_number_integer()) {
            error = "a townsperson is {id = 0-31, value = {chara_no, status}}";
            return false;
        }
        const long long chara_no = value[0].get<long long>(), status = value[1].get<long long>();
        if (chara_no < -1 || chara_no > 32767 || status < 0 || status > 0xFFFF) {
            error = "a townsperson is {id = 0-31, value = {chara_no, status}}";
            return false;
        }
        save->user_data.party_chara[id].chara_no = static_cast<s16>(chara_no);
        save->user_data.party_chara[id].status = static_cast<u16>(status);
        shared.recruits[static_cast<std::size_t>(id)] = {static_cast<int>(chara_no), static_cast<int>(status)};
        return true;
    }
    if (kind == "floor") {
        if (id < 0 || id >= kFloors || !value.is_array() || value.size() != 9) {
            error = "a floor's record is {id = 0-" + std::to_string(kFloors - 1) + ", value = its nine numbers}";
            return false;
        }
        long long fields[9] = {};
        static constexpr long long kHigh[9] = {0x7FFFFFFF, 0x7FFFFFFF, 0xFFFF, 0xFF, 0xFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
        for (int i = 0; i < 9; ++i) {
            if (!value[static_cast<std::size_t>(i)].is_number_integer()) {
                error = "a floor's record is nine whole numbers";
                return false;
            }
            fields[i] = value[static_cast<std::size_t>(i)].get<long long>();
            if (fields[i] < 0 || fields[i] > kHigh[i]) {
                error = "a floor's record has a number out of range";
                return false;
            }
        }
        DNG_FLOOR_SAVE theirs{};
        theirs.unk_0 = static_cast<s32>(fields[0]);
        theirs.fast_destroy_time = static_cast<s32>(fields[1]);
        theirs.unk_8 = static_cast<u16>(fields[2]);
        theirs.unk_a = static_cast<u8>(fields[3]);
        theirs.unk_b = static_cast<u8>(fields[4]);
        theirs.spheda_clear = static_cast<u16>(fields[5]);
        theirs.flag = static_cast<u16>(fields[6]);
        theirs.kill_count = static_cast<u16>(fields[7]);
        theirs.visit_count = static_cast<u16>(fields[8]);
        DNG_FLOOR_SAVE &live = save->save_dungeon.floor_info[id];
        live = MergeFloor(live, theirs);
        // What they sent is what they have: if this game's record holds more, the next step tells them the rest.
        shared.floors[static_cast<std::size_t>(id)] = theirs;
        return true;
    }
    if (kind == "gift") {
        if (id <= 0 || id >= ITEM_NO_MAX || !whole(1, 999, number)) {
            error = "a gift is {id = an item's number, value = 1-999}";
            return false;
        }
        // Into this player's own bag, as the script gave it to the other's: what does not fit goes where the game
        // puts what does not fit, and an item there is only one of is not given twice.
        save->user_data.GetItem(id, static_cast<int>(number));
        return true;
    }
    if (kind == "stock") {
        if (id < 0 || id >= kStock || !whole(0, 32767, number)) {
            error = "a part's stock is {id = 0-" + std::to_string(kStock - 1) + ", value = 0-32767}";
            return false;
        }
        save->build_parts_num[id] = shared.stock[static_cast<std::size_t>(id)] = static_cast<s16>(number);
        return true;
    }
    error = "kind must be flag, counter, progress, party, recruit, floor, stock or gift";
    return false;
}

} // namespace moonmod::coop
