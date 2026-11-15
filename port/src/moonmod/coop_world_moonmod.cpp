// Co-op's world (moonmod/coop.hpp): the other players' heroes on the floor or in the town, the host's monsters
// mirrored on a guest, the doors one player opens for everyone, and the seed a floor is built from.

#include "moonmod/coop.hpp"
#include "moonmod/coop_down.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

#include "character.hpp"
#include "charasetup.hpp"
#include "dataread.hpp"
#include "dng_event.hpp"
#include "dng_main.hpp"
#include "editctrl.hpp"
#include "gamedata.hpp"
#include "mainloop.hpp"
#include "mglib.hpp"
#include "mg_memory.hpp"
#include "monster.hpp"
#include "savedata.hpp"
#include "scene.hpp"
#include "sceneevent.hpp"
#include "scenesnd.hpp"
#include "scenevillager.hpp"
#include "userdata.hpp"

namespace moonmod::coop {

namespace {

float Dist3(const float *a, const float *b) {
    const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

float Ease(float from, float to, float rate) { return from + (to - from) * rate; }

float TurnToward(float from, float to, float rate) {
    constexpr float kTau = 2.0f * std::numbers::pi_v<float>;
    float           d = std::fmod(to - from, kTau);
    if (d > std::numbers::pi_v<float>) d -= kTau;
    if (d < -std::numbers::pi_v<float>) d += kTau;
    return from + d * rate;
}

// ---- Avatars --------------------------------------------------------------------------------------------------------

// Past this from where it stands, an avatar jumps instead of easing (a warp, the stairs, a new floor).
constexpr float kJump = 160.0f;
// The scene slots an avatar takes: the dungeon's last event slots, a town's last talk slots (the game fills them
// from the first), and texture blocks past the ones the town's and dungeon's setup use.
constexpr int kDungeonSlotBase = DNG_CHARA_SLOT_EVENT + DNG_CHARA_EVENT_NUM - kAvatars;
constexpr int kTownSlotBase = EDIT_CHARA_SLOT_OTHER_END - kAvatars;
constexpr int kTextureBlockBase = 0x70;
constexpr int kFileBytes = 6 * 1024 * 1024;

// One hero's body in a scene slot: the scene steps and draws it; the step eases to the last report and the draw
// tints the ambient light (a guest in blue).
class AvatarBody final : public CCharacter2 {
public:
    AvatarState target;
    // What hangs on the body's frames: the hat, the weapon in the hand, the second weapon. Each follows a frame of
    // the body (as CActionChara::SetRef has the hero's own parts do), stepped and drawn after it.
    std::vector<std::shared_ptr<CCharacter2>> parts;
    float       at[3] = {0.0f, 0.0f, 0.0f}, heading = 0.0f;
    bool        placed = false;
    int         motion = -1;
    int         stand_in_gait = -1, stand_in_shown = -1; // a stand-in: 1 running, 0 standing
    int         town = 0;

    void Step() override {
        const float to[3] = {target.x, target.y, target.z};
        const float far = std::hypot(to[0] - at[0], to[2] - at[2]);
        if (!placed || far > kJump) {
            std::copy(to, to + 3, at);
            heading = target.heading;
            placed = true;
        } else {
            // About a fifth of the way each frame: a report every six frames lands just as the next one comes.
            for (int axis = 0; axis < 3; ++axis) at[axis] = Ease(at[axis], to[axis], 0.2f);
            heading = TurnToward(heading, target.heading, 0.3f);
        }
        SetPosition(at[0], at[1], at[2]);
        SetRotation(0.0f, heading, 0.0f);
        if (target.stand_in) {
            // The hero standing in for a ridepod or a monster's form: their motion numbers are not the hero's, so
            // the hero runs while the reports move and stands when they stop (the names every hero's body has).
            if (far > 2.0f) stand_in_gait = 1;
            else if (far < 0.5f || stand_in_gait < 0) stand_in_gait = 0;
            if (stand_in_gait != stand_in_shown) {
                stand_in_shown = stand_in_gait;
                char run[] = "\x91\x96\x82\xE8", stand[] = "\x97\xA7\x82\xBF";
                SetMotion(stand_in_gait != 0 ? run : stand, 0);
                motion = -1;
            }
        } else {
            stand_in_gait = stand_in_shown = -1;
            if (target.motion != motion) {
                motion = target.motion;
                SetMotion(std::max(0, motion), 2);
            }
        }
        CCharacter2::Step();
        for (const std::shared_ptr<CCharacter2> &part : parts) part->Step();
    }

    int DrawDirect() override {
        const bool tinted = target.tint[0] >= 0.0f;
        float      saved[4] = {};
        if (tinted) {
            mgGetAmbient(saved);
            float ambient[4] = {target.tint[0], target.tint[1], target.tint[2], saved[3]};
            mgSetAmbient(ambient);
        }
        const int drawn = CCharacter2::DrawDirect();
        for (const std::shared_ptr<CCharacter2> &part : parts) part->DrawDirect();
        if (tinted) mgSetAmbient(saved);
        return drawn;
    }
};

struct Avatar {
    bool                        wanted = false;
    AvatarState                 target;
    std::shared_ptr<AvatarBody> body;
    int                         loaded_character = -1;
    int                         loaded_town = -1;
    int                         loaded_outfit[5] = {-1, -1, -1, -1, -1}; // head, clothes, feet, weapon, second weapon as asked
    int                         slot = -1;
    CScene                     *scene = nullptr;
    int                         loop = -1;
    // Its own memory: the file it reads and the stacks its model, motions and images live in.
    std::vector<unsigned char>  file;
    std::vector<unsigned char>  arena;
    mgCMemory                   memory;
    mgCMemory                   stacks[CHARA_MEM_STACK_NUM];
};

std::array<Avatar, kAvatars> g_avatars;

bool InDungeon() { return GetNowLoopNo() == LOOP_DUNGEON && DngMainScene != nullptr && MainChara__2 != nullptr; }
// A floor's monsters and map events need its scene, not the player's body.
bool OnFloor() { return GetNowLoopNo() == LOOP_DUNGEON && DngMainScene != nullptr; }
bool InTown() { return GetNowLoopNo() == LOOP_EDIT && GetMainScene() != nullptr; }

CScene *PlaceScene() {
    if (InDungeon()) return DngMainScene;
    if (InTown()) return GetMainScene();
    return nullptr;
}

void Unload(int index) {
    Avatar &a = g_avatars[static_cast<std::size_t>(index)];
    if (a.scene != nullptr && a.slot >= 0 && a.scene->GetCharacter(a.slot) == a.body.get()) {
        a.scene->ResetActive(SCENE_DATA_CHARA, a.slot);
    }
    a.body.reset();
    a.loaded_character = -1;
    a.loaded_town = -1;
    a.slot = -1;
    a.scene = nullptr;
    a.loop = -1;
    a.file.clear();
    a.file.shrink_to_fit();
    a.arena.clear();
    a.arena.shrink_to_fit();
}

unsigned char *Aligned(unsigned char *raw) {
    return reinterpret_cast<unsigned char *>((reinterpret_cast<std::uintptr_t>(raw) + 63) & ~std::uintptr_t{63});
}

bool Load(int index, CScene *scene, int slot, int town) {
    Avatar &a = g_avatars[static_cast<std::size_t>(index)];
    Unload(index);
    const int character = a.target.character;
    const std::optional<std::string> model = MainCharaModelName(character, town);
    if (!model || model->empty()) return false;
    const int quadwords = GetCharaMemAllocSize() + CHARA_MEM_STACK_NUM * 4 + 64;
    a.arena.assign(static_cast<std::size_t>(quadwords) * 16 + 64, 0);
    a.memory.stSetBuffer(reinterpret_cast<u_long128 *>(Aligned(a.arena.data())), quadwords);
    if (!GetCharaMemAllocPtr(&a.memory, a.stacks, USER_CHARA_MAX, town)) return false;
    a.file.assign(kFileBytes + 64, 0);
    const std::span<unsigned char> buffer(Aligned(a.file.data()), kFileBytes);
    auto load = [&buffer](const std::string &path) {
        std::string mutable_path = path;
        int         size = 0;
        return !path.empty() && LoadFile(mutable_path.data(), buffer, &size) > 0 && size > 0;
    };
    auto body = std::make_shared<AvatarBody>();
    body->Initialize();
    body->target = a.target;
    body->town = town;
    const int block = kTextureBlockBase + index;
    if (!load(dc2::character::ModelAssetPath(town != 0 ? "chara" : "mainchr", *model, false))) return false;
    body->LoadPack(reinterpret_cast<unsigned int *>(buffer.data()), "info", &a.stacks[0], &a.stacks[0], &a.stacks[0], block, nullptr);
    // What the hero wears and carries: the other player's own, as their game said it; what it did not say is as this
    // game's own hero of that name has it.
    constexpr int kEquipSlots = 5; // CHARA_DATA::equip, by CHARA_EQUIP_SLOT
    int           own[kEquipSlots] = {};
    if (CUserDataManager *user = GetUserDataMan(); user != nullptr) {
        if (CHARA_DATA *data = user->GetCharaDataPtr(character); data != nullptr) {
            for (int slot_no = 0; slot_no < kEquipSlots; ++slot_no) own[slot_no] = data->equip[slot_no].item_no;
        }
    }
    const auto worn = [&own](int said, int equip_slot) { return said > 0 ? said : own[equip_slot]; };
    const int  clothes = worn(a.target.clothes, CHARA_EQUIP_CLOTHES), feet = worn(a.target.feet, CHARA_EQUIP_FEET);
    if (const auto path = ItemFilePath(clothes, 0); clothes > 0 && path && load(*path)) {
        body->LoadSkin(reinterpret_cast<unsigned int *>(buffer.data()), const_cast<char *>("info"), const_cast<char *>(""), &a.stacks[1], block);
    }
    if (const auto path = ItemFilePath(feet, 0); feet > 0 && path && load(*path)) {
        body->LoadSkin(reinterpret_cast<unsigned int *>(buffer.data()), const_cast<char *>("info"), const_cast<char *>(""), &a.stacks[5], block);
    }
    // The parts, each on the body's frame the game hangs it on (charasetup.cpp, SetupMainUnit): the weapons only on
    // a floor, except Monica's armband, which she wears in town too.
    const bool monica = character == USER_CHARA_MONICA;
    const auto hang = [&](int item_no, int stack, const char *frame_name) {
        if (item_no <= 0 || body->CObjectFrame::frame == nullptr) return;
        const auto path = ItemFilePath(item_no, 0);
        if (!path || !load(*path)) return;
        auto part = std::make_shared<CCharacter2>();
        part->Initialize();
        part->LoadPack(reinterpret_cast<unsigned int *>(buffer.data()), "info", &a.stacks[stack], &a.stacks[stack], &a.stacks[stack], block, body.get());
        mgCFrame *at = body->CObjectFrame::frame->SearchFrame(const_cast<char *>(frame_name));
        if (part->CObjectFrame::frame == nullptr || at == nullptr) return;
        part->CObjectFrame::frame->DeleteReference();
        part->CObjectFrame::frame->SetReference(at);
        body->parts.push_back(std::move(part));
    };
    if (town == 0) hang(worn(a.target.weapon, CHARA_EQUIP_MELEE), 2, monica ? "sword_hand" : "ef00");
    if (town == 0 || monica) hang(worn(a.target.sub, CHARA_EQUIP_SUB), 3, monica ? "wr" : "gun_hand");
    hang(worn(a.target.head, CHARA_EQUIP_HEAD), 4, monica ? "ac" : "hat");
    if (scene->AssignChara(slot, dc2::scene::CharacterResource::FromOwned(body), "coop_avatar") != slot) return false;
    scene->SetType(SCENE_DATA_CHARA, slot, SCENE_CHARA_TYPE_EVENT);
    scene->SetCharaTexb(slot, block);
    scene->ResetStatus(SCENE_DATA_CHARA, slot, SCENE_CHARA_HIDE | SCENE_CHARA_NO_MODEL);
    scene->SetActive(SCENE_DATA_CHARA, slot);
    body->SetPosition(a.target.x, a.target.y, a.target.z);
    a.body = std::move(body);
    a.loaded_character = character;
    a.loaded_town = town;
    const int asked[5] = {a.target.head, a.target.clothes, a.target.feet, a.target.weapon, a.target.sub};
    std::copy(asked, asked + 5, a.loaded_outfit);
    a.slot = slot;
    a.scene = scene;
    a.loop = GetNowLoopNo();
    return true;
}

// ---- Monsters ------------------------------------------------------------------------------------------------------

struct PuppetSlot {
    bool   used = false;
    Puppet host;
    int    motion = -1;
};
std::vector<PuppetSlot>  g_puppets(static_cast<std::size_t>(MONSTER_ACTIVE_MAX));
std::vector<PuppetHit>   g_puppet_hits;
std::vector<AvatarHit>   g_avatar_hits;
bool                     g_puppets_pause = false;
float                    g_saved_player[4] = {0.0f, 0.0f, 0.0f, 1.0f};
bool                     g_player_swapped = false;

CActiveMonster *Monster(int slot) {
    if (!OnFloor() || ActiveMonster == nullptr || slot < 0 || slot >= MONSTER_ACTIVE_MAX) return nullptr;
    return ActiveMonster->GetActiveMonster(slot);
}

// The avatars on this floor: where each stands.
int AvatarsHere(std::array<std::array<float, 3>, kAvatars> &at, std::array<bool, kAvatars> &here) {
    int count = 0;
    for (int i = 0; i < kAvatars; ++i) {
        // (a player who is down is nobody a monster goes for)
        here[static_cast<std::size_t>(i)] = !g_avatars[static_cast<std::size_t>(i)].target.down && AvatarAt(i, at[static_cast<std::size_t>(i)].data());
        count += here[static_cast<std::size_t>(i)] ? 1 : 0;
    }
    return count;
}

void PuppetsStep() {
    if (!InDungeon() || BattleAreaScene == nullptr) {
        if (std::ranges::any_of(g_puppets, &PuppetSlot::used)) PuppetsClear();
        return;
    }
    bool any = false;
    for (int slot = 0; slot < MONSTER_ACTIVE_MAX; ++slot) {
        PuppetSlot &p = g_puppets[static_cast<std::size_t>(slot)];
        if (!p.used) continue;
        CActiveMonster *monster = Monster(slot);
        if (monster == nullptr) {
            p.used = false;
            continue;
        }
        any = true;
        if (p.host.state <= 0 || p.host.hp == 0) continue;
        monster->SetPosition(p.host.x, p.host.y, p.host.z);
        monster->SetRotation(0.0f, p.host.heading, 0.0f);
        if (p.host.motion >= 0 && p.host.motion != p.motion) {
            p.motion = p.host.motion;
            monster->SetMotion(p.host.motion, 2);
        }
        // Its own thoughts are paused with every monster's: the copy still animates.
        monster->Step();
    }
    // A guest's floor is the host's: the monsters' own scripts rest while any copy stands.
    if (any && !g_puppets_pause) {
        BattleAreaScene->pause_flag |= DNG_PAUSE_MONSTER_AI;
        g_puppets_pause = true;
    } else if (!any && g_puppets_pause) {
        BattleAreaScene->pause_flag &= ~DNG_PAUSE_MONSTER_AI;
        g_puppets_pause = false;
    }
}

// ---- Shared events -------------------------------------------------------------------------------------------------

std::vector<SharedEvent> g_events_out;
bool                     g_remote_running = false; // the event now running came from another player
struct Waiting {
    bool        on = false;
    SharedEvent event;
    int         frames_left = 0;
    int         keys[2] = {-1, -1};
    int         counts[2] = {0, 0};
};
Waiting g_waiting;
Waiting g_granted; // the keys lent to a running remote event, taken back when it ends
constexpr int kWaitFrames = 600;

void SharedEventsStep() {
    if (!InDungeon() || DngUserData == nullptr) {
        g_waiting = Waiting{};
        g_granted = Waiting{};
        g_remote_running = false;
        g_events_out.clear();
        return;
    }
    // A remote event ran to its end: the key it was lent and didn't use goes back.
    if (g_remote_running && DngStatus.mode == DNG_STATUS_FIELD && !DngMainScene->event_run) {
        for (int k = 0; k < 2; ++k) {
            const int key = g_granted.keys[k];
            if (key <= 0) continue;
            const int now = DngUserData->GetNumSameItem(key);
            if (now > g_granted.counts[k]) DngUserData->DeleteItem(key, now - g_granted.counts[k]);
        }
        g_granted = Waiting{};
        g_remote_running = false;
    }
    if (!g_waiting.on) return;
    if (--g_waiting.frames_left <= 0) {
        g_waiting.on = false;
        return;
    }
    if (DngStatus.mode != DNG_STATUS_FIELD || DngMainScene->event_run || g_remote_running) return;
    float           pos[4] = {g_waiting.event.pos[0], g_waiting.event.pos[1], g_waiting.event.pos[2], 1.0f};
    CSceneEventData data{};
    if (!DngMainScene->GetMapEvent(pos, g_waiting.event.mode, &data) || data.event.point_no != g_waiting.event.script) {
        // Not this floor's point (another layout): dropped, so the next one may come.
        g_waiting.on = false;
        return;
    }
    // The key it asks for is taken as given: lent now, and taken back if the script leaves it.
    const int dungeon = DngStatus.dungeon_no;
    const int floor = DngSaveDataDungeon != nullptr && dungeon >= 0 && dungeon < SAVE_DUNGEON_NUM ? DngSaveDataDungeon->floor_id[dungeon] : 0;
    g_granted = Waiting{};
    if (dungeon >= 0 && dungeon < SAVE_DUNGEON_NUM) {
        g_granted.keys[0] = GetGateKeyIndex(dungeon, floor);
        g_granted.keys[1] = GetKeyDoorIndex(dungeon, floor);
        for (int k = 0; k < 2; ++k) {
            if (g_granted.keys[k] <= 0) continue;
            g_granted.counts[k] = DngUserData->GetNumSameItem(g_granted.keys[k]);
            DngUserData->GetItem(g_granted.keys[k], 1);
        }
    }
    g_remote_running = true;
    DngMainScene->RunEvent(data.event.point_no, &data);
    g_waiting.on = false;
}

// ---- Floors ----------------------------------------------------------------------------------------------------------

std::optional<std::uint32_t> g_next_seed;
std::optional<int>           g_last_seed;

} // namespace

// ---- Avatars: the API ----------------------------------------------------------------------------------------------

bool AvatarSet(int slot, const AvatarState &state) {
    if (slot < 0 || slot >= kAvatars || (state.character != USER_CHARA_MAX && state.character != USER_CHARA_MONICA)) return false;
    Avatar &a = g_avatars[static_cast<std::size_t>(slot)];
    a.target = state;
    a.wanted = true;
    if (a.body) a.body->target = state;
    return true;
}

void AvatarClear(int slot) {
    for (int i = 0; i < kAvatars; ++i) {
        if (slot >= 0 && slot != i) continue;
        g_avatars[static_cast<std::size_t>(i)].wanted = false;
        Unload(i);
    }
}

bool AvatarShown(int slot) {
    if (slot < 0 || slot >= kAvatars) return false;
    const Avatar &a = g_avatars[static_cast<std::size_t>(slot)];
    return a.wanted && a.body != nullptr && a.scene == PlaceScene() && a.scene != nullptr;
}

bool AvatarAt(int slot, float out[3]) {
    if (!AvatarShown(slot) || !InDungeon()) return false;
    const Avatar &a = g_avatars[static_cast<std::size_t>(slot)];
    if (!a.body->placed) return false;
    std::copy(a.body->at, a.body->at + 3, out);
    return true;
}

void AvatarsStep() {
    CScene *scene = PlaceScene();
    const int loop = GetNowLoopNo();
    for (int i = 0; i < kAvatars; ++i) {
        Avatar &a = g_avatars[static_cast<std::size_t>(i)];
        // Another place, or the scene gave the slot to someone else: the body goes; the mod's wish stays.
        if (a.body && (scene != a.scene || loop != a.loop || scene == nullptr || scene->GetCharacter(a.slot) != a.body.get())) {
            a.body.reset(); // the scene's slot was reset with the scene; nothing of ours to take back
            a.loaded_character = -1;
            a.slot = -1;
            a.scene = nullptr;
        }
        if (!a.wanted || scene == nullptr) continue;
        const int town = InTown() ? 1 : 0;
        const int asked[5] = {a.target.head, a.target.clothes, a.target.feet, a.target.weapon, a.target.sub};
        const bool changed = !std::equal(asked, asked + 5, a.loaded_outfit); // the other player changed what they wear
        if (a.loaded_character != a.target.character || a.loaded_town != town || !a.body || changed) {
            const int slot = (town != 0 ? kTownSlotBase : kDungeonSlotBase) + i;
            const bool taken = scene->IsActive(SCENE_DATA_CHARA, slot) != 0 && scene->GetCharacter(slot) != nullptr;
            if (taken || !Load(i, scene, slot, town)) {
                a.wanted = false; // a hero that can't be shown isn't tried every frame
                std::fprintf(stderr, "co-op: avatar %d can't be shown here (slot %d %s)\n", i + 1, slot, taken ? "taken" : "failed to load");
                continue;
            }
        }
        a.body->target = a.target;
    }
}

// ---- Monsters: the API ---------------------------------------------------------------------------------------------

bool PuppetSet(int slot, const Puppet &puppet) {
    CActiveMonster *monster = Monster(slot);
    if (monster == nullptr) return false;
    PuppetSlot &p = g_puppets[static_cast<std::size_t>(slot)];
    if (puppet.state <= 0 || puppet.kind < 0) {
        // Gone on the host: gone here, without a death (no drops, no count).
        if (monster->state != ACTIVE_MONSTER_NONE) {
            monster->state = ACTIVE_MONSTER_NONE;
            monster->life = 0;
            if (DngMainScene != nullptr) DngMainScene->ResetActive(SCENE_DATA_CHARA, DNG_CHARA_SLOT_MONSTER + slot);
        }
        p = PuppetSlot{};
        return true;
    }
    if (monster->state == ACTIVE_MONSTER_NONE || (monster->monster_id != puppet.kind && puppet.kind >= 0)) return false;
    p.used = true;
    p.host = puppet;
    monster->life = std::clamp(puppet.hp, 0, 1000000);
    if (puppet.max_hp > 0) monster->max_life = puppet.max_hp;
    if (puppet.state == ACTIVE_MONSTER_DEAD && monster->state == ACTIVE_MONSTER_LIVE) {
        monster->life = 0;
        monster->damage_req = ACTION_DAMAGE_REQ_DEAD;
    }
    return true;
}

bool IsPuppet(int slot) { return slot >= 0 && slot < MONSTER_ACTIVE_MAX && g_puppets[static_cast<std::size_t>(slot)].used; }

void PuppetsClear() {
    for (PuppetSlot &p : g_puppets) p = PuppetSlot{};
    if (g_puppets_pause && BattleAreaScene != nullptr) BattleAreaScene->pause_flag &= ~DNG_PAUSE_MONSTER_AI;
    g_puppets_pause = false;
}

std::vector<PuppetHit> TakePuppetHits() {
    std::vector<PuppetHit> out;
    out.swap(g_puppet_hits);
    return out;
}

std::vector<AvatarHit> TakeAvatarHits() {
    std::vector<AvatarHit> out;
    out.swap(g_avatar_hits);
    return out;
}

void MonsterDamaged(int slot, int damage) {
    if (!IsPuppet(slot) || damage <= 0) return;
    // The copy keeps the host's life: the blow goes to the host's monster.
    if (CActiveMonster *monster = Monster(slot); monster != nullptr) monster->life = std::max(0, g_puppets[static_cast<std::size_t>(slot)].host.hp);
    if (g_puppet_hits.size() < 64) g_puppet_hits.push_back({slot, damage});
}

void MonsterThinkBegin(int slot) {
    MonsterThinkEnd();
    if (!InDungeon()) return;
    std::array<std::array<float, 3>, kAvatars> at{};
    std::array<bool, kAvatars>                 here{};
    if (AvatarsHere(at, here) == 0) return;
    CActiveMonster *monster = Monster(slot);
    if (monster == nullptr) return;
    float monster_at[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    monster->GetPosition(monster_at);
    MainChara__2->GetPosition(g_saved_player);
    const float *best = nullptr;
    // (this game's own hero, down, is the last one a monster goes for)
    float        best_d = HeroIsDown() ? 1.0e30f : Dist3(g_saved_player, monster_at);
    for (int i = 0; i < kAvatars; ++i) {
        if (!here[static_cast<std::size_t>(i)]) continue;
        const float d = Dist3(at[static_cast<std::size_t>(i)].data(), monster_at);
        if (d < best_d) {
            best_d = d;
            best = at[static_cast<std::size_t>(i)].data();
        }
    }
    if (best == nullptr) return;
    // For this monster's turn the player stands at the nearest player.
    MainChara__2->SetPosition(best[0], best[1], best[2]);
    g_player_swapped = true;
}

void MonsterThinkEnd() {
    if (g_player_swapped && MainChara__2 != nullptr) MainChara__2->SetPosition(g_saved_player[0], g_saved_player[1], g_saved_player[2]);
    g_player_swapped = false;
}

// ---- Shared events: the API ----------------------------------------------------------------------------------------

void MapEventStarted(int check_type, const CSceneEventData &data) {
    if (check_type == 0 || g_remote_running || !OnFloor()) return; // touched, or another player's
    if (g_events_out.size() >= 8) g_events_out.erase(g_events_out.begin());
    SharedEvent e;
    e.script = data.event.point_no;
    e.mode = check_type;
    for (int i = 0; i < 3; ++i) {
        e.pos[i] = data.position[i];
        e.dir[i] = data.rotation[i];
    }
    g_events_out.push_back(e);
}

std::vector<SharedEvent> TakeSharedEvents() {
    std::vector<SharedEvent> out;
    out.swap(g_events_out);
    return out;
}

bool RunSharedEvent(const SharedEvent &event) {
    if (g_waiting.on || !OnFloor()) return false;
    g_waiting = Waiting{};
    g_waiting.on = true;
    g_waiting.event = event;
    g_waiting.frames_left = kWaitFrames;
    return true;
}

bool SharedEventWaiting() { return g_waiting.on; }

// ---- Floors: the API -------------------------------------------------------------------------------------------------

void SetNextFloorSeed(std::optional<std::uint32_t> seed) { g_next_seed = seed; }

namespace {

// The seed a mod gave the floor being built, kept until the next floor: the layout, then the chests, then the
// monsters each start from a number made of it, so two games given one seed build one floor. Without a mod's seed
// nothing is reseeded and the floor is the game's own.
std::optional<int> g_floor_seed;
bool               g_build_begun = false; // FloorBuildBegin has asked the mods for this floor's seed
std::uint32_t      g_hash_layout = 0, g_hash_chests = 0, g_hash_monsters = 0;
bool               g_hash_ready = false;

// Asks the mods (game.dungeon.generating) and takes the seed one of them set, if any.
void TakeFloorSeed() {
    if (SharedRuntime().Running()) {
        const int dungeon = DngSaveDataDungeon != nullptr ? DngSaveDataDungeon->stage_id : DngStatus.dungeon_no;
        const int floor = DngSaveDataDungeon != nullptr && dungeon >= 0 && dungeon < SAVE_DUNGEON_NUM ? DngSaveDataDungeon->floor_id[dungeon] : -1;
        SharedRuntime().Publish("game.dungeon.generating", {{"dungeon_id", dungeon}, {"floor_id", floor}, {"back_floor", false}});
    }
    g_floor_seed.reset();
    if (g_next_seed) {
        g_floor_seed = static_cast<int>(*g_next_seed & 0x7FFFFFFFu);
        g_next_seed.reset();
    }
}

// The number a stage of the build starts from: the floor's seed, turned over once for each stage.
unsigned StageSeed(int seed, int stage) {
    std::uint32_t value = static_cast<std::uint32_t>(seed) ^ (0x9E3779B9u * static_cast<std::uint32_t>(stage + 1));
    value ^= value >> 16;
    value *= 0x7FEB352Du;
    value ^= value >> 15;
    value *= 0x846CA68Bu;
    value ^= value >> 16;
    return value & 0x7FFFFFFFu;
}

void HashBytes(std::uint32_t &hash, const void *data, std::size_t size) {
    const auto *bytes = static_cast<const unsigned char *>(data);
    for (std::size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 16777619u;
}

template <typename T>
void HashValue(std::uint32_t &hash, T value) {
    HashBytes(hash, &value, sizeof(value));
}

// A place to the nearest unit: two games that put a thing in one spot must agree, whatever their rounding.
void HashPlace(std::uint32_t &hash, const float *position) {
    for (int axis = 0; axis < 3; ++axis) HashValue(hash, static_cast<std::int32_t>(std::lround(position[axis])));
}

} // namespace

void FloorBuildBegin() {
    TakeFloorSeed();
    g_build_begun = true;
    g_last_seed.reset();
    g_hash_layout = g_hash_chests = g_hash_monsters = 0;
    g_hash_ready = false;
    if (g_floor_seed) {
        // A preset floor picks its layout with the game's generator before any seed of its own (and a random floor
        // seeds itself again in FloorSeed): the pick follows the floor's seed too.
        srand(StageSeed(*g_floor_seed, 0));
        g_last_seed = *g_floor_seed;
    }
}

int FloorSeed(int native_seed) {
    if (!g_build_begun) TakeFloorSeed(); // a floor built without FloorBuildBegin (the tests): ask here
    g_build_begun = false;
    const int seed = g_floor_seed ? *g_floor_seed : native_seed;
    g_last_seed = seed;
    return seed;
}

void FloorPlacing(int stage) {
    if (g_floor_seed) srand(StageSeed(*g_floor_seed, stage));
}

void FloorPlaced(int stage) {
    if (!OnFloor()) return;
    if (stage == 1) {
        // The layout as it was built, and the chests where they were put with what they hold.
        std::uint32_t layout = 2166136261u;
        HashValue(layout, static_cast<std::int32_t>(AutoMapGen.grid_w));
        HashValue(layout, static_cast<std::int32_t>(AutoMapGen.grid_h));
        for (const CAutoMapParts &cell : AutoMapGen.GridCells()) {
            HashValue(layout, cell.kind);
            HashValue(layout, cell.parts_no);
            HashValue(layout, cell.room_no);
            HashValue(layout, cell.link);
        }
        g_hash_layout = layout;
        std::uint32_t chests = 2166136261u;
        if (DngMainScene != nullptr && DngMainScene->battle_area.treasure_box != nullptr) {
            for (CTreasureBox &box : DngMainScene->battle_area.treasure_box->box) {
                if (box.state == TREASURE_BOX_STATE_NONE) continue;
                float position[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                box.GetPosition(position);
                HashPlace(chests, position);
                HashValue(chests, box.item[0]);
                HashValue(chests, box.item[1]);
            }
        }
        g_hash_chests = chests;
    } else if (stage == 2) {
        std::uint32_t monsters = 2166136261u;
        for (int slot = 0; slot < MONSTER_ACTIVE_MAX; ++slot) {
            CActiveMonster *monster = Monster(slot);
            if (monster == nullptr || monster->state == ACTIVE_MONSTER_NONE) continue;
            float position[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            monster->GetPosition(position);
            HashValue(monsters, static_cast<std::int32_t>(slot));
            HashValue(monsters, static_cast<std::int32_t>(monster->monster_id));
            HashPlace(monsters, position);
        }
        g_hash_monsters = monsters;
        g_hash_ready = true;
    }
}

std::optional<std::string> FloorLayoutHash() {
    if (!g_hash_ready || !OnFloor()) return std::nullopt;
    char text[32];
    std::snprintf(text, sizeof(text), "%08x-%08x-%08x", g_hash_layout, g_hash_chests, g_hash_monsters);
    return std::string(text);
}

std::optional<int> LastFloorSeed() { return g_last_seed; }

// ---- Each frame ------------------------------------------------------------------------------------------------------

// A monster's blow on another player's hero. The game tests each attack's primitives against the bodies of the
// scene's characters (CColPrim::IsHit); the hero's own body takes what lands on it in CActionChara::CheckDamage. An
// avatar is a scene character too, so the same test says when a blow aimed at the player's side lands on it; what
// lands is kept for monsters.mirror_get, whose mod sends it to that player's game. After a blow an avatar cannot be
// struck again for as long as the hero's own grace lasts.
std::array<int, kAvatars> g_avatar_grace{};
constexpr int             kAvatarGrace = 45;

void AvatarHitsStep() {
    if (!InDungeon() || DngStatus.mode != DNG_STATUS_FIELD) return;
    for (int i = 0; i < kAvatars; ++i) {
        const Avatar &a = g_avatars[static_cast<std::size_t>(i)];
        int          &grace = g_avatar_grace[static_cast<std::size_t>(i)];
        if (grace > 0) --grace;
        if (!a.body || a.scene != DngMainScene || a.slot < 0 || a.target.down) continue;
        for (CColPrim &prim : ColPrimMan.prim) {
            if (!prim.active || prim.param == nullptr || (prim.target & DAMAGE_TARGET_PLAYER) == 0) continue;
            if (!prim.IsHit(DngMainScene, a.slot)) continue; // (a blow that lands marks the avatar as struck by it)
            if (grace > 0) continue;
            grace = kAvatarGrace;
            // kind as the mod reads it: 2 a small hit (a flinch), 3 a blow that knocks down
            const int kind = (prim.param->hit_flags & DAMAGE_HIT_KNOCKDOWN) != 0 ? 3 : 2;
            if (g_avatar_hits.size() < 64) {
                g_avatar_hits.push_back({i + 1, prim.attacker, std::max(1, static_cast<int>(prim.damage)), kind, static_cast<int>(prim.param->hit_flags)});
            }
            break;
        }
    }
}

void WorldStep() {
    AvatarsStep();
    AvatarHitsStep();
    PuppetsStep();
    SharedEventsStep();
}

void WorldReset() {
    for (int i = 0; i < kAvatars; ++i) {
        g_avatars[static_cast<std::size_t>(i)].wanted = false;
        g_avatars[static_cast<std::size_t>(i)].body.reset();
        g_avatars[static_cast<std::size_t>(i)].loaded_character = -1;
        g_avatars[static_cast<std::size_t>(i)].scene = nullptr;
        g_avatars[static_cast<std::size_t>(i)].slot = -1;
    }
    for (PuppetSlot &p : g_puppets) p = PuppetSlot{};
    g_puppets_pause = false;
    g_puppet_hits.clear();
    g_avatar_hits.clear();
    g_player_swapped = false;
    g_events_out.clear();
    g_waiting = Waiting{};
    g_granted = Waiting{};
    g_remote_running = false;
    g_next_seed.reset();
    g_last_seed.reset();
}

} // namespace moonmod::coop
