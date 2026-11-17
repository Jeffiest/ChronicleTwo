// Co-op's world (moonmod/coop.hpp): the other players' heroes on the floor or in the town, the host's monsters
// mirrored on a guest, the doors one player opens for everyone, and the seed a floor is built from.

#include "moonmod/coop.hpp"
#include "moonmod/coop_down.hpp"
#include "moonmod/chronicle_two_adapter.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

#include "character.hpp"
#include "charasetup.hpp"
#include "dataread.hpp"
#include "dng_event.hpp"
#include "dng_main.hpp"
#include "dng_object.hpp"
#include "editctrl.hpp"
#include "effscript.hpp"
#include "gamedata.hpp"
#include "mainloop.hpp"
#include "maintex.hpp"
#include "menuchr.hpp"
#include "cameracontrol.hpp"
#include "mglib.hpp"
#include "mg_math.hpp"
#include "mg_memory.hpp"
#include "monster.hpp"
#include "savedata.hpp"
#include "scene.hpp"
#include "sceneevent.hpp"
#include "scenesnd.hpp"
#include "scenevillager.hpp"
#include "userdata.hpp"
#include "audio/sound_runtime.hpp"
#include "snd_mngr.hpp"
#include "platform/arena.hpp"

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

// The others are shown this many frames behind their own game's clock (as the reports' ages give it): a report every
// four frames and room for the network's unevenness, so there is nearly always a next report to ease toward.
constexpr int kDelay = 8;
constexpr int kMaxAge = 600;
// This game's frames, counted by WorldStep: the clock the reports' ages are taken on.
int g_clock = 0;

// One report from another game: where its hero (or monster) stood and what it played, on this game's clock.
struct Sample {
    int   t = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f, heading = 0.0f;
    int   motion = -1, flags = 0;
    float step = -1.0f;
};

// Keeps a report, in its time's order. One older than a report kept, or the same as the last, is let go; one far
// from the last (a warp, the stairs) starts the trail over; one after a long quiet (standing still) starts from the
// last as of one report before it, so a walk starts when it did and is not drawn out over the quiet.
void TrailHeard(std::vector<Sample> &trail, const Sample &n) {
    if (!trail.empty()) {
        const Sample &last = trail.back();
        if (n.t <= last.t) return;
        if (n.x == last.x && n.y == last.y && n.z == last.z && n.heading == last.heading && n.motion == last.motion &&
            n.flags == last.flags && n.step == last.step) {
            return;
        }
        if (std::hypot(n.x - last.x, n.z - last.z) > kJump) {
            trail.clear();
        } else if (n.t - last.t > 2 * kDelay) {
            Sample held = last;
            held.t = n.t - kDelay / 2;
            trail.push_back(held);
        }
    }
    trail.push_back(n);
    if (trail.size() > 32) trail.erase(trail.begin());
}

// The trail as it stood kDelay frames ago: eased between the two reports around that moment, playing the motion
// reported last before it. (A trail that has run out stands at its last report.)
Sample TrailAt(std::vector<Sample> &trail) {
    const int render = g_clock - kDelay;
    while (trail.size() >= 2 && trail[1].t <= render) trail.erase(trail.begin());
    const Sample &a = trail.front();
    if (trail.size() == 1 || render <= a.t) return a;
    const Sample &b = trail[1];
    const float   u = static_cast<float>(render - a.t) / static_cast<float>(b.t - a.t);
    Sample        s = a;
    s.x = Ease(a.x, b.x, u);
    s.y = Ease(a.y, b.y, u);
    s.z = Ease(a.z, b.z, u);
    s.heading = TurnToward(a.heading, b.heading, u);
    return s;
}
// The scene slots an avatar takes: the dungeon's last event slots, a town's last talk slots (the game fills them
// from the first), and texture blocks past the ones the town's and dungeon's setup use.
constexpr int kDungeonSlotBase = DNG_CHARA_SLOT_EVENT + DNG_CHARA_EVENT_NUM - kAvatars;
constexpr int kTownSlotBase = EDIT_CHARA_SLOT_OTHER_END - kAvatars;
constexpr int kTextureBlockBase = 0x70;
constexpr int kFileBytes = 6 * 1024 * 1024;

// One hero's body in a scene slot: the scene steps and draws it; the step follows the reports kDelay frames behind
// (At), playing each motion as that player's game plays it, and the draw may tint the ambient light.
class AvatarBody final : public CCharacter2 {
public:
    AvatarState target;
    // What hangs on the body's frames: the hat, the weapon in the hand, the second weapon. Each follows a frame of
    // the body (as CActionChara::SetRef has the hero's own parts do), stepped and drawn after it.
    std::vector<std::shared_ptr<CCharacter2>> parts;
    float       at[3] = {0.0f, 0.0f, 0.0f}, heading = 0.0f;
    bool        placed = false;
    // Drawn as the ridepod or the monster itself (not the hero standing in): it plays the reported motions. The
    // ridepod's parts play them too, as the game's own CActionChara::SetMotion has every part of the chain do.
    bool        shaped = false;
    bool        parts_move = false;
    int         motion = -1, flags = -1;
    std::vector<Sample> trail; // the reports kept, oldest first (TrailHeard, TrailAt); empty: the target, eased toward
    int         stand_in_gait = -1, stand_in_shown = -1; // a stand-in: 1 running, 0 standing
    int         town = 0;
    // In and out of sight as the game's own monsters come and go (view_alpha, a twentieth a frame): from nothing where
    // it first stands or lands after a jump, and to nothing where it stands when the player goes (world.avatar_clear
    // with both in one place), then gone.
    float       fade = 0.0f;
    bool        leaving = false;

    void Step() override {
        const Sample now = trail.empty() ? Sample{g_clock, target.x, target.y, target.z, target.heading, target.motion,
                                                  target.motion_flags, target.motion_step}
                                         : TrailAt(trail);
        const float  to[3] = {now.x, now.y, now.z};
        const float  far = std::hypot(to[0] - at[0], to[2] - at[2]);
        float        pace = far; // how far it goes this frame
        if (!placed || far > kJump) {
            std::copy(to, to + 3, at);
            heading = now.heading;
            placed = true;
            pace = 0.0f;
            if (!leaving) fade = 0.0f;
        } else if (trail.empty()) {
            // (reports with no time: about a fifth of the way each frame)
            for (int axis = 0; axis < 3; ++axis) at[axis] = Ease(at[axis], to[axis], 0.2f);
            heading = TurnToward(heading, now.heading, 0.3f);
            pace = far * 0.2f;
        } else {
            std::copy(to, to + 3, at);
            heading = now.heading;
        }
        SetPosition(at[0], at[1], at[2]);
        SetRotation(0.0f, heading, 0.0f);
        if (target.stand_in && !shaped) {
            // The hero standing in for a ridepod or a monster's form: their motion numbers are not the hero's, so
            // the hero runs while the reports move and stands when they stop (the names every hero's body has).
            if (pace > 0.4f) stand_in_gait = 1;
            else if (pace < 0.1f || stand_in_gait < 0) stand_in_gait = 0;
            if (stand_in_gait != stand_in_shown) {
                stand_in_shown = stand_in_gait;
                char run[] = "\x91\x96\x82\xE8", stand[] = "\x97\xA7\x82\xBF";
                SetMotion(stand_in_gait != 0 ? run : stand, 0);
                motion = flags = -1;
            }
        } else {
            stand_in_gait = stand_in_shown = -1;
            // As that player's game plays it: a run, a walk or a stand loops, a swing or a fall stops on its last
            // frame, and a walk goes at the stick's pace (actionchara.cpp sets each so).
            if (now.motion != motion || now.flags != flags) {
                motion = now.motion;
                flags = now.flags;
                SetMotion(std::max(0, motion), flags);
                if (parts_move) {
                    for (const std::shared_ptr<CCharacter2> &part : parts) part->SetMotion(std::max(0, motion), flags);
                }
            }
            if (now.step >= 0.0f) {
                SetStep(now.step);
                if (parts_move) {
                    for (const std::shared_ptr<CCharacter2> &part : parts) part->SetStep(now.step);
                }
            }
        }
        fade = std::clamp(fade + (leaving ? -0.05f : 0.05f), 0.0f, 1.0f);
        alpha = fade;
        for (const std::shared_ptr<CCharacter2> &part : parts) part->alpha = fade;
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
        label_drawn = MeasureLabel();
        return drawn;
    }

    // Where the player's name goes on the screen, measured as the scene draws the body, with the camera it is drawn
    // with: the points over its head and under its feet. DrawAvatarNames takes it in the 2D pass after the scene, once.
    bool  label_drawn = false;
    float label_head[2] = {0.0f, 0.0f}, label_feet[2] = {0.0f, 0.0f};

private:
    // A world point's place on the screen (from its top left, in the screen's own units), or false when it is behind
    // the camera or off the screen.
    static bool OnScreen(float *point, float out[2]) {
        float clip[4];
        sceVu0ApplyMatrix(clip, mgRenderInfo.world_screen, point);
        if (!(clip[3] >= 1.0f)) return false; // behind the eye, or at it
        int screen[4];
        if (mgTransWorldScreen(screen, point) == 0) return false;
        out[0] = static_cast<float>(screen[0]) / 16.0f;
        out[1] = static_cast<float>(screen[1]) / 16.0f;
        return std::isfinite(out[0]) && std::isfinite(out[1]);
    }

    // The highest joint of the body and its parts as they are drawn (the head's, or the hat's, or the ridepod's), and
    // a fifth of the height again over it for the hair and the hat's crown. A body with no joints to read is taken
    // by its info file's height.
    bool MeasureLabel() {
        float feet[4] = {at[0], at[1], at[2], 1.0f};
        float top = -1.0e30f;
        const auto reach = [&top](CCharacter2 &chara) {
            mgCFrame *root = chara.CObjectFrame::frame;
            if (root == nullptr || root->frame_list == nullptr) return;
            for (int i = 0; i < root->frame_num; ++i) {
                if (root->frame_list[i] == nullptr) continue;
                float joint[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                root->frame_list[i]->GetWorldPosition0(joint);
                if (std::isfinite(joint[1])) top = std::max(top, joint[1]);
            }
        };
        // (a hero's own joints only: a weapon held up would lift the name in every swing; its head joint sits at the
        // neck, so a fifth again reaches over the hair. The ridepod's and a monster's joints are their shape's own.)
        reach(*this);
        if (shaped) {
            for (const std::shared_ptr<CCharacter2> &part : parts) reach(*part);
        }
        const float over = shaped ? 1.1f : 1.2f;
        float       height = top - feet[1];
        if (!(height > 1.0f && height < 1000.0f)) height = (body_height > 1.0f ? body_height : 16.0f) / over;
        float head[4] = {feet[0], feet[1] + height * over, feet[2], 1.0f};
        return OnScreen(head, label_head) && OnScreen(feet, label_feet);
    }
};

struct Avatar {
    bool                        wanted = false;
    AvatarState                 target;
    std::shared_ptr<AvatarBody> body;
    int                         loaded_character = -1;
    int                         loaded_town = -1;
    std::array<int, 12>         loaded_look{}; // what the body was loaded as (LookOf): a new look is loaded in its place
    int                         slot = -1;
    CScene                     *scene = nullptr;
    int                         loop = -1;
    int                         held_effect = -1; // the effect of an item it holds up to throw (its slot under the avatar's), or -1
    // Shots, spells and sounds told with an age, kept for the moment the avatar comes to them: (that frame, the effect).
    std::vector<std::pair<int, HeroEffect>> pending;
    // Its own memory: the file it reads and the stacks its model, motions and images live in.
    std::vector<unsigned char>  file;
    std::vector<unsigned char>  arena;
    mgCMemory                   memory;
    mgCMemory                   stacks[CHARA_MEM_STACK_NUM];
    // Its hero's sounds: a bank of the game's (snd2/chara/<sound_name>.sound.json) on a sound port of its own
    // (SND_PORT_COOP + its index), so its shots, swings and voice are heard, and the effects it owns play theirs.
    std::string                 sound_name;
    unsigned int                se_bank = 0xFFFFFFFFu;
    mgCMemory                   sound_memory;
    // Its looped sounds, kept up as the game's own CLoopSeMngr keeps a hero's: each goes on until `keep` frames pass
    // with no word that it still goes (count, the frames since the last), then stops.
    struct LoopSound {
        int sound = -1;
        int keep = 0;
        int count = 0;
    };
    std::array<LoopSound, 4> loops{};
};

std::array<Avatar, kAvatars> g_avatars;
constexpr int                kLoopVoice = 13; // the voice the game's hero plays its loops on (_SE_LOOP_PLAY); ours are on a port of their own

// Stops an avatar's looped sounds (it goes, or its bank does).
void AvatarLoopsStop(Avatar &a) {
    for (Avatar::LoopSound &l : a.loops) {
        if (l.sound >= 0 && a.se_bank != 0xFFFFFFFFu) sndSeStop(a.se_bank, l.sound, kLoopVoice);
        l = Avatar::LoopSound{};
    }
}

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
    // (what it was drawing goes with it: an item held up, a shot in flight that follows it)
    if (a.slot >= 0 && GetNowLoopNo() == LOOP_DUNGEON && FxScriptMan != nullptr && a.scene == DngMainScene) {
        FxScriptMan->ClearEffectFromChrid(a.slot);
    }
    a.held_effect = -1;
    a.pending.clear();
    AvatarLoopsStop(a);
    if (a.se_bank != 0xFFFFFFFFu) sndDeletePort(SND_PORT_COOP + index);
    a.se_bank = 0xFFFFFFFFu;
    a.sound_name.clear();
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

// A hero's sound bank by name, as snd2/chara names them (CH_000) and a message carries it: letters, digits, _ only.
bool SoundNameOk(const std::string &name) {
    return !name.empty() && name.size() < 32 &&
           std::ranges::all_of(name, [](unsigned char c) { return std::isalnum(c) != 0 || c == '_'; });
}

// The memory a hero's sound bank's tables take (the sounds themselves go to the sound driver): one block per avatar,
// made the first time and used again for every bank after (as item_icon3d keeps one block for its models).
constexpr int kSoundQuadwords = 16 * 1024; // 256 KiB
char          g_sound_arena_keys[kAvatars];

// Loads the bank an avatar's sounds come from onto its own port, unless it is the one there. The avatar's body (and so
// every effect it owns) plays from it. False when the bank can't be loaded: the avatar is then silent, as before.
bool LoadAvatarSound(Avatar &a, int index, const std::string &name) {
    // (a bank that would not load is not tried again at every sound: only when another is asked for)
    if (a.sound_name == name) return a.se_bank != 0xFFFFFFFFu;
    if (!SoundNameOk(name)) return false;
    const int port = SND_PORT_COOP + index;
    AvatarLoopsStop(a);
    sndDeletePort(port);
    a.se_bank = 0xFFFFFFFFu;
    a.sound_name = name;
    a.sound_memory.stSetBuffer(reinterpret_cast<u_long128 *>(ArenaBlock(&g_sound_arena_keys[index],
                                                                         std::size_t{kSoundQuadwords} * sizeof(u_long128))),
                               kSoundQuadwords);
    const std::string path = "snd2/chara/" + name + ".sound.json";
    const unsigned    bank = SoundLoadBundlePath(port, path, &a.sound_memory);
    if (bank == static_cast<unsigned>(-1)) {
        std::fprintf(stderr, "co-op: avatar %d's sounds (%s) did not load\n", index + 1, path.c_str());
        sndDeletePort(port);
        if (a.body) a.body->sound_info.se_bank = 0xFFFFFFFFu;
        return false;
    }
    a.se_bank = bank;
    if (a.body) a.body->sound_info.se_bank = bank;
    std::fprintf(stderr, "co-op: avatar %d's sounds are %s (%d bytes of tables)\n", index + 1, name.c_str(),
                 a.sound_memory.stack_used * static_cast<int>(sizeof(u_long128)));
    return true;
}

// The bank a hero's sounds come from, by name, as the game picks it (dc2::character::SoundAssetPath): Max's by his
// gun, Monica's own, the ridepod's by its arm. Empty for a monster form, which has none.
std::string HeroSoundName(CUserDataManager *user, int unit) {
    const std::string path = dc2::character::SoundAssetPath(user, unit);
    static constexpr std::string_view kDir = "snd2/chara/", kExt = ".sound.json";
    if (path.size() <= kDir.size() + kExt.size() || !path.starts_with(kDir) || !path.ends_with(kExt)) return {};
    return path.substr(kDir.size(), path.size() - kDir.size() - kExt.size());
}

// What an avatar is drawn as, all in one row: when any of it changes the body is loaded again in its place.
std::array<int, 12> LookOf(const AvatarState &s) {
    return {s.character, s.stand_in ? 1 : 0, s.head, s.clothes, s.feet, s.weapon, s.sub, s.ridepod[0], s.ridepod[1], s.ridepod[2], s.ridepod[3], s.form};
}

// The shapes an avatar's body can be loaded as.
enum class Shape { Hero, Ridepod, Monster };

// The shape a report asks for: the ridepod or the monster on a floor when the other game said which parts or which
// form, else the hero (standing in, if the player rides or wears a form).
Shape WantedShape(const AvatarState &s, int town) {
    if (town != 0 || !s.stand_in) return Shape::Hero;
    if (s.character == USER_CHARA_MAX && std::ranges::all_of(s.ridepod, [](int item_no) { return item_no > 0; })) return Shape::Ridepod;
    if (s.character == USER_CHARA_MONICA && s.form >= 0) return Shape::Monster;
    return Shape::Hero;
}

// A frame by name on the body or on any part already hung on it, as CActionChara::SearchObject looks down the chain.
mgCFrame *FindFrame(AvatarBody &body, const char *name) {
    if (body.CObjectFrame::frame != nullptr) {
        if (mgCFrame *found = body.CObjectFrame::frame->SearchFrame(name); found != nullptr) return found;
    }
    for (const std::shared_ptr<CCharacter2> &part : body.parts) {
        if (part->CObjectFrame::frame == nullptr) continue;
        if (mgCFrame *found = part->CObjectFrame::frame->SearchFrame(name); found != nullptr) return found;
    }
    return nullptr;
}

// Builds one body of a shape into the avatar's own memory: the file buffer and the stacks the game sizes for that
// character (GetCharaMemAllocPtr). Gives nullptr when a file is missing or a frame to hang a part on is not there.
std::shared_ptr<AvatarBody> Build(Avatar &a, int index, int town, Shape shape) {
    const int character = a.target.character;
    const int memory_kind = shape == Shape::Ridepod ? USER_CHARA_ROBO : shape == Shape::Monster ? USER_CHARA_MONSTER : USER_CHARA_MAX;
    const int quadwords = GetCharaMemAllocSize() + CHARA_MEM_STACK_NUM * 4 + 64;
    a.arena.assign(static_cast<std::size_t>(quadwords) * 16 + 64, 0);
    a.memory.stSetBuffer(reinterpret_cast<u_long128 *>(Aligned(a.arena.data())), quadwords);
    if (!GetCharaMemAllocPtr(&a.memory, a.stacks, memory_kind, town)) return nullptr;
    a.file.assign(kFileBytes + 64, 0);
    const std::span<unsigned char> buffer(Aligned(a.file.data()), kFileBytes);
    auto load = [&buffer](const std::string &path) {
        std::string mutable_path = path;
        int         size = 0;
        return !path.empty() && LoadFile(mutable_path.data(), buffer, &size) > 0 && size > 0;
    };
    auto *pack = reinterpret_cast<unsigned int *>(buffer.data());
    auto  body = std::make_shared<AvatarBody>();
    body->Initialize();
    body->target = a.target;
    body->town = town;
    const int block = kTextureBlockBase + index;
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
    // A part loaded from a file and hung on a frame of the body or of a part before it; false when either is missing.
    const auto hang_file = [&](const std::string &path, const char *profile, int stack, const char *frame_name) {
        if (body->CObjectFrame::frame == nullptr || !load(path)) return false;
        auto part = std::make_shared<CCharacter2>();
        part->Initialize();
        part->LoadPack(pack, profile, &a.stacks[stack], &a.stacks[stack], &a.stacks[stack], block, body.get());
        mgCFrame *at = FindFrame(*body, frame_name);
        if (part->CObjectFrame::frame == nullptr || at == nullptr) return false;
        part->CObjectFrame::frame->DeleteReference();
        part->CObjectFrame::frame->SetReference(at);
        body->parts.push_back(std::move(part));
        return true;
    };

    if (shape == Shape::Ridepod) {
        // The ridepod as the game builds it (charasetup.cpp: SetupMainUnit, SetupRobo, BuildRoboPartsInfoCore): the
        // leg is the body, then the arm on the leg's body<n> frame (n the arm's own number), the body on "spine", Max
        // seated in it ("mints", one model per body type and outfit) on "joint2", the energy pack on "joint1" and
        // Max's hat on "hat"; last, the arm's "ude" frame takes the matrix of the body's arm<n> frame.
        const int leg = a.target.ridepod[0], arm = a.target.ridepod[1], hull = a.target.ridepod[2], energy = a.target.ridepod[3];
        CDataRoboPart *arm_info = GetRoboPartInfoData(arm), *hull_info = GetRoboPartInfoData(hull);
        const auto     leg_name = ItemFileName(leg, 0), arm_name = ItemFileName(arm, 0), hull_name = ItemFileName(hull, 0),
                   energy_name = ItemFileName(energy, 0);
        if (arm_info == nullptr || hull_info == nullptr || !leg_name || !arm_name || !hull_name || !energy_name) return nullptr;
        CGameData *game_data = GetGameDataPt();
        CSaveData *save = GetSaveData();
        int        costume = worn(a.target.clothes, CHARA_EQUIP_CLOTHES) - (game_data != nullptr ? game_data->GetDataTypeStartListNo(ITEM_DATA_MAX_CLOTHES) : 0);
        costume = std::clamp(costume, 0, 5);
        int body_type = hull_info->offset_no;
        if (save != nullptr && save->GetBitFlag(SAVE_FLAG_COSTUME_UNLOCK)) body_type += 10;
        char mints[32];
        std::snprintf(mints, sizeof(mints), "mints%02d%c.gltf", body_type, static_cast<char>('a' + costume));
        const std::string arm_frame = "body" + std::to_string(arm_info->GetOffsetNo());
        const std::string mount_frame = "arm" + std::to_string(hull_info->GetOffsetNo());
        if (!load(dc2::character::ModelAssetPath("dungeon/robo", *leg_name, true))) return nullptr;
        body->LoadPack(pack, "info", &a.stacks[0], &a.stacks[0], &a.stacks[0], block, nullptr);
        if (!hang_file(dc2::character::ModelAssetPath("dungeon/robo", *arm_name, true), "info", 1, arm_frame.c_str()) ||
            !hang_file(dc2::character::ModelAssetPath("dungeon/robo", *hull_name, true), "info", 2, "spine") ||
            !hang_file(dc2::character::ModelAssetPath("dungeon/robo", mints, false), "info", 2, "joint2") ||
            !hang_file(dc2::character::ModelAssetPath("dungeon/robo", *energy_name, true), "info", 3, "joint1")) {
            return nullptr;
        }
        // (Max's hat goes on if he wears one; the ridepod is whole without it)
        if (const auto hat = ItemFilePath(worn(a.target.head, CHARA_EQUIP_HEAD), 0)) hang_file(*hat, "info", 2, "hat");
        mgCFrame *ude = FindFrame(*body, "ude"), *mount = FindFrame(*body, mount_frame.c_str());
        if (ude == nullptr || mount == nullptr) return nullptr;
        sceVu0FMATRIX matrix;
        sceVu0CopyMatrix(matrix, mount->trans_matrix);
        ude->SetTransMatrix(matrix);
        body->shaped = true;
        body->parts_move = true;
        return body;
    }

    if (shape == Shape::Monster) {
        // A monster's form as the game loads it for Monica (SetupMainUnit): the monster's model, read with its own
        // profile name.
        const auto model = GetMonsterModelFile(a.target.form, MONSTER_FILE_MODEL);
        const auto config = GetMonsterModelFile(a.target.form, MONSTER_FILE_CONFIG);
        if (!model || !config || model->empty() || config->empty() || !load("dungeon/monster/" + *model)) return nullptr;
        body->LoadPack(pack, config->c_str(), &a.stacks[0], &a.stacks[0], &a.stacks[0], block, nullptr);
        if (body->CObjectFrame::frame == nullptr) return nullptr;
        body->shaped = true;
        return body;
    }

    const std::optional<std::string> model = MainCharaModelName(character, town);
    if (!model || model->empty() || !load(dc2::character::ModelAssetPath(town != 0 ? "chara" : "mainchr", *model, false))) return nullptr;
    body->LoadPack(pack, "info", &a.stacks[0], &a.stacks[0], &a.stacks[0], block, nullptr);
    const int clothes = worn(a.target.clothes, CHARA_EQUIP_CLOTHES), feet = worn(a.target.feet, CHARA_EQUIP_FEET);
    if (const auto path = ItemFilePath(clothes, 0); clothes > 0 && path && load(*path)) {
        body->LoadSkin(pack, const_cast<char *>("info"), const_cast<char *>(""), &a.stacks[1], block);
    }
    if (const auto path = ItemFilePath(feet, 0); feet > 0 && path && load(*path)) {
        body->LoadSkin(pack, const_cast<char *>("info"), const_cast<char *>(""), &a.stacks[5], block);
    }
    // The parts, each on the body's frame the game hangs it on (charasetup.cpp, SetupMainUnit): the weapons only on
    // a floor, except Monica's armband, which she wears in town too.
    const bool monica = character == USER_CHARA_MONICA;
    const auto hang = [&](int item_no, int stack, const char *frame_name) {
        if (item_no <= 0) return;
        if (const auto path = ItemFilePath(item_no, 0)) hang_file(*path, "info", stack, frame_name);
    };
    if (town == 0) hang(worn(a.target.weapon, CHARA_EQUIP_MELEE), 2, monica ? "sword_hand" : "ef00");
    if (town == 0 || monica) hang(worn(a.target.sub, CHARA_EQUIP_SUB), 3, monica ? "wr" : "gun_hand");
    hang(worn(a.target.head, CHARA_EQUIP_HEAD), 4, monica ? "ac" : "hat");
    // The blade's trail, made as the game makes its hero's (charasetup.cpp, SetupMainUnit), for world.avatar_effect.
    if (town == 0) SetSwordBlurEffect(body.get(), &a.stacks[2], character);
    return body;
}

bool Load(int index, CScene *scene, int slot, int town) {
    Avatar &a = g_avatars[static_cast<std::size_t>(index)];
    Unload(index);
    // The ridepod or the monster when asked for and it loads; else (or when it does not) the hero, standing in.
    const Shape wanted = WantedShape(a.target, town);
    std::shared_ptr<AvatarBody> body = Build(a, index, town, wanted);
    if (!body && wanted != Shape::Hero) {
        std::fprintf(stderr, "co-op: avatar %d's %s did not load; the hero stands in\n", index + 1, wanted == Shape::Ridepod ? "ridepod" : "monster form");
        body = Build(a, index, town, Shape::Hero);
    } else if (body && wanted != Shape::Hero) {
        std::fprintf(stderr, "co-op: avatar %d is drawn as the %s\n", index + 1, wanted == Shape::Ridepod ? "ridepod" : "monster form");
    }
    if (!body) return false;
    const int block = kTextureBlockBase + index;
    if (scene->AssignChara(slot, dc2::scene::CharacterResource::FromOwned(body), "coop_avatar") != slot) return false;
    scene->SetType(SCENE_DATA_CHARA, slot, SCENE_CHARA_TYPE_EVENT);
    scene->SetCharaTexb(slot, block);
    scene->ResetStatus(SCENE_DATA_CHARA, slot, SCENE_CHARA_HIDE | SCENE_CHARA_NO_MODEL);
    scene->SetActive(SCENE_DATA_CHARA, slot);
    body->SetPosition(a.target.x, a.target.y, a.target.z);
    body->sound_info.se_bank = 0xFFFFFFFFu; // (none until its bank is loaded: an effect it owns plays nothing)
    a.body = std::move(body);
    if (town == 0 && wanted == Shape::Hero) {
        const int   gun = a.target.sub;
        std::string name = "CH_020";
        if (a.target.character == USER_CHARA_MAX) {
            char text[16];
            std::snprintf(text, sizeof(text), "CH_%03d", gun >= 0x16 && gun <= 0x28 ? gun - 0x16 : 0);
            name = text;
        }
        LoadAvatarSound(a, index, name);
    }
    a.loaded_character = a.target.character;
    a.loaded_town = town;
    a.loaded_look = LookOf(a.target);
    a.slot = slot;
    a.scene = scene;
    a.loop = GetNowLoopNo();
    return true;
}

// ---- Monsters ------------------------------------------------------------------------------------------------------

struct PuppetSlot {
    bool   used = false;
    Puppet host;
    int    motion = -1, flags = -1;
    std::vector<Sample> trail; // the leader's reports kept (TrailHeard, TrailAt)
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

// A copy's sight is this game's own: whether it is near this game's hero, and its fade in and out of sight and away
// from the camera, as CMonsterMan::ThinkHost works them out for a monster that thinks here. A copy's thinking rests
// (DNG_PAUSE_MONSTER_AI), and with it all of that: a copy placed out of sight stayed out of sight, never drawn, while
// blows still reached it.
void PuppetSight(CActiveMonster *monster) {
    auto *hero = DngMainScene != nullptr ? DngMainScene->GetCharacter(DNG_CHARA_SLOT_PARTY) : nullptr;
    if (hero == nullptr) {
        monster->view_state = MONSTER_VIEW_IN;
        monster->view_alpha = 1.0f;
        return;
    }
    float hero_at[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float own_at[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    hero->GetPosition(hero_at);
    monster->GetPosition(own_at);
    monster->target_dist = mgDistVector(hero_at, own_at);
    if (auto *camera = static_cast<CCameraControl *>(DngMainScene->GetCamera(DngMainScene->active_camera)); camera != nullptr) {
        float camera_at[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        camera->GetPos(camera_at);
        monster->camera_dist = mgDistVector(camera_at, own_at);
    }
    // (a copy is never put to rest for the number of monsters about: the leader already chose which ones act)
    monster->CheckView(MONSTER_ACTIVE_MAX + 1);
    if (monster->view_state == MONSTER_VIEW_FADE_OUT) {
        monster->view_alpha -= 0.05f;
        if (monster->view_alpha <= 0.0f) {
            monster->view_alpha = 0.0f;
            monster->view_state = MONSTER_VIEW_OUT;
        }
    } else if (monster->view_state == MONSTER_VIEW_FADE_IN) {
        monster->view_alpha += 0.05f;
        if (!(monster->view_alpha < 1.0f)) {
            monster->view_alpha = 1.0f;
            monster->view_state = MONSTER_VIEW_IN;
        }
    }
    if (monster->camera_dist < 4.0f * monster->body_width) {
        monster->camera_alpha = std::max(0.0f, monster->camera_alpha - 1.0f / 12.0f);
    } else {
        monster->camera_alpha = std::min(1.0f, monster->camera_alpha + 1.0f / 12.0f);
    }
}

void PuppetsStep() {
    if (!InDungeon() || BattleAreaScene == nullptr) {
        if (std::ranges::any_of(g_puppets, &PuppetSlot::used)) PuppetsClear();
        return;
    }
    // What the copies' own motions start (a motion's entry effects) is drawn only: the leader's monsters strike, not
    // these copies (and their effects are no longer held frozen with the copies' thinking, dng_main.cpp).
    std::vector<const _EFF_SCRIPT *> before;
    if (FxScriptMan != nullptr && std::ranges::any_of(g_puppets, &PuppetSlot::used)) {
        for (const _EFF_SCRIPT *s = FxScriptMan->head; s != nullptr; s = s->next) before.push_back(s);
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
        // (on the leader's clock a little behind, eased between its reports, as the other players' heroes are; each
        // motion played as the leader's monster plays it, looping or held on its last frame)
        const Sample now = p.trail.empty() ? Sample{g_clock, p.host.x, p.host.y, p.host.z, p.host.heading, p.host.motion,
                                                    p.host.motion_flags, p.host.motion_step}
                                           : TrailAt(p.trail);
        monster->SetPosition(now.x, now.y, now.z);
        monster->SetRotation(0.0f, now.heading, 0.0f);
        if (now.motion >= 0 && (now.motion != p.motion || now.flags != p.flags)) {
            p.motion = now.motion;
            p.flags = now.flags;
            monster->SetMotion(now.motion, now.flags);
        }
        if (now.step >= 0.0f) monster->SetStep(now.step);
        // Its own thoughts are paused with every monster's: the copy still animates, and comes into sight here.
        monster->Step();
        PuppetSight(monster);
    }
    if (FxScriptMan != nullptr && any) {
        for (_EFF_SCRIPT *s = FxScriptMan->head; s != nullptr; s = s->next) {
            if (std::ranges::find(before, s) == before.end()) s->shown_only = true;
        }
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
    if (a.body) {
        a.body->leaving = false; // (back before it was gone: it comes back into sight where it is)
        a.body->target = state;
        if (state.age >= 0) {
            TrailHeard(a.body->trail, {g_clock - std::min(state.age, kMaxAge), state.x, state.y, state.z, state.heading, state.motion,
                                  state.motion_flags, state.motion_step});
        } else {
            a.body->trail.clear();
        }
    }
    return true;
}

void AvatarClear(int slot) {
    for (int i = 0; i < kAvatars; ++i) {
        if (slot >= 0 && slot != i) continue;
        Avatar &a = g_avatars[static_cast<std::size_t>(i)];
        a.wanted = false;
        // Seen here, in this place: the hero goes from sight where it stands (AvatarsStep lets it go at nothing). It is
        // nobody a monster goes for or strikes, and carries no name, from now.
        if (a.body && a.scene != nullptr && a.scene == PlaceScene() && a.slot >= 0 && a.scene->GetCharacter(a.slot) == a.body.get() &&
            a.body->fade > 0.0f) {
            a.body->leaving = true;
            continue;
        }
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

bool AvatarNameOnScreen(int slot, float head[2], float feet[2], std::string &name) {
    if (!AvatarShown(slot)) return false;
    const Avatar &a = g_avatars[static_cast<std::size_t>(slot)];
    // (measured when the scene drew the body; a body the scene did not draw since has no place for a name)
    const bool drawn = std::exchange(a.body->label_drawn, false);
    if (!drawn || !a.body->placed || a.target.name.empty()) return false;
    std::copy(a.body->label_head, a.body->label_head + 2, head);
    std::copy(a.body->label_feet, a.body->label_feet + 2, feet);
    name = a.target.name;
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
            if (scene != nullptr && scene == a.scene && scene == DngMainScene && FxScriptMan != nullptr && a.slot >= 0) {
                FxScriptMan->ClearEffectFromChrid(a.slot); // (the same floor, its slot given to another: our effects go)
            }
            a.held_effect = -1; // (on another floor, the floor's effects went with it)
            a.pending.clear();
            AvatarLoopsStop(a);
            a.loaded_character = -1;
            a.slot = -1;
            a.scene = nullptr;
        }
        if (!a.wanted && a.body && a.body->leaving && a.body->fade <= 0.0f) Unload(i); // (gone from sight: gone)
        if (!a.wanted || scene == nullptr) continue;
        const int town = InTown() ? 1 : 0;
        // (the other player changed hero, what they wear, or climbed into the ridepod or took a form)
        const bool changed = LookOf(a.target) != a.loaded_look;
        if (a.loaded_character != a.target.character || a.loaded_town != town || !a.body || changed) {
            const int slot = (town != 0 ? kTownSlotBase : kDungeonSlotBase) + i;
            // (the slot holding this avatar's own body is not taken: a new hero, outfit or shape loads in its place,
            // Load putting the old body out first)
            const bool taken = scene->IsActive(SCENE_DATA_CHARA, slot) != 0 && scene->GetCharacter(slot) != nullptr &&
                               (a.body == nullptr || scene->GetCharacter(slot) != a.body.get());
            if (taken || !Load(i, scene, slot, town)) {
                a.wanted = false; // a hero that can't be shown isn't tried every frame
                std::fprintf(stderr, "co-op: avatar %d can't be shown here (slot %d %s)\n", i + 1, slot, taken ? "taken" : "failed to load");
                continue;
            }
        }
        a.body->target = a.target;
    }
}

// ---- What a hero fires -----------------------------------------------------------------------------------------------

namespace {

std::vector<HeroEffect> g_hero_effects;
constexpr std::size_t   kHeroEffectsKept = 64; // a mod that stops taking them loses the oldest, not the game's memory

// The muzzle flash every gun of Max's shows at the muzzle.
char kMuzzleFlash[] = "\x83}\x83Y\x83\x8B\x83t\x83\x89\x83" "b\x83V\x83\x85";
char kBullet[] = "\x8F" "e\x92" "e";

// An effect base this game has not loaded (its own hero is the other one: Max's gun effects in Monica's game, her
// magic in Max's), loaded as the floor's scripts load theirs (_LOAD_EFFECT_SCRIPT): into the scene's third stack, at
// the floor's level, so it goes with the floor.
bool EnsureBase(CEffectScriptMan &fx, int base_no) {
    if (base_no < 0 || DngMainScene == nullptr) return false;
    for (const EFF_SPT_BASE *loaded : fx.base) {
        if (loaded != nullptr && loaded->base_no == base_no) return true;
    }
    const int level = std::exchange(fx.level, 3);
    const int built = fx.LoadBaseEffSpt(base_no, DngMainScene->GetStack(3), -1);
    fx.level = level;
    if (built < 0) std::fprintf(stderr, "co-op: effect base %d can't be loaded for another player's shot\n", base_no);
    else std::fprintf(stderr, "co-op: effect base %d loaded for another player's shots\n", base_no);
    return built >= 0;
}

// The last effect made, marked as drawn only (its script makes no attack), from a point.
bool Drawn(CEffectScriptMan &fx, float *pos) {
    if (fx.now == nullptr) return false;
    fx.now->shown_only = true;
    fx.SetScriptVect1(pos, 0, -1);
    return true;
}

bool MakeEffect(CEffectScriptMan &fx, char *name, float *pos) {
    // (by number: the by-name call answers the effect's slot, which is -1 for one made without a slot, as these are)
    const int base_no = fx.SearchBaseNo(name);
    if (!EnsureBase(fx, base_no) || fx.CreateEffSpt(base_no, 0, 0) == nullptr) {
        std::fprintf(stderr, "co-op: effect %d not made for another player's shot\n", base_no);
        return false;
    }
    return Drawn(fx, pos);
}

int TargetHere(int target) {
    // a monster slot of the floor that holds a monster here too; else the shot flies straight
    if (target < DNG_CHARA_SLOT_MONSTER || target >= DNG_CHARA_SLOT_MONSTER_END || DngMainScene == nullptr) return -1;
    return DngMainScene->GetCharacter(target) != nullptr ? target : -1;
}

} // namespace

void HeroFired(const void *chara, HeroShot kind, const float *pos, const float *dir, int value, float amount, int target) {
    if (chara == nullptr || chara != MainChara__2 || !InDungeon()) return;
    HeroEffect e;
    e.kind = static_cast<int>(kind);
    for (int axis = 0; axis < 3; ++axis) {
        e.pos[axis] = pos[axis];
        e.dir[axis] = dir[axis];
    }
    e.value = value;
    e.amount = amount;
    e.target = target;
    if (g_hero_effects.size() >= kHeroEffectsKept) g_hero_effects.erase(g_hero_effects.begin());
    g_hero_effects.push_back(e);
}

// A frame's name as a trail message carries it: letters, digits and _ only, and short.
static bool FrameNameOk(const std::string &name) {
    return !name.empty() && name.size() < 32 &&
           std::ranges::all_of(name, [](unsigned char c) { return std::isalnum(c) != 0 || c == '_'; });
}

void HeroSounded(const void *chara, int sound) {
    if (chara == nullptr || chara != MainChara__2 || !InDungeon() || DngUserData == nullptr || sound < 0 || sound > 255) return;
    HeroEffect e;
    e.kind = static_cast<int>(HeroShot::Sound);
    e.frame0 = HeroSoundName(DngUserData, DngUserData->active_chr_no);
    if (!SoundNameOk(e.frame0)) return;
    e.frame1 = e.frame0;
    e.value = sound;
    float at[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    MainChara__2->GetPosition(at);
    for (int axis = 0; axis < 3; ++axis) e.pos[axis] = at[axis];
    if (g_hero_effects.size() >= kHeroEffectsKept) g_hero_effects.erase(g_hero_effects.begin());
    g_hero_effects.push_back(e);
}

// A looped sound is kept up every frame the hero's script says so; it is told at most every kLoopTellEvery frames for
// one sound (the avatar's side keeps it that much longer), and again at once when it starts afresh.
constexpr int kLoopTellEvery = 8;
static std::array<std::pair<int, int>, 8> g_loops_told{}; // (sound + 1, the frame it was last told), 0 for none

void HeroLoopSounded(const void *chara, int sound, int keep) {
    if (chara == nullptr || chara != MainChara__2 || !InDungeon() || DngUserData == nullptr || sound < 0 || sound > 255) return;
    keep = std::clamp(keep, 0, 600);
    std::pair<int, int> *told = &g_loops_told[0];
    for (std::pair<int, int> &t : g_loops_told) {
        if (t.first == sound + 1) {
            told = &t;
            break;
        }
        if (t.second < told->second) told = &t; // (else the one told longest ago makes room)
    }
    const bool known = told->first == sound + 1;
    if (keep == 0) {
        if (!known) return; // (a stop, as the game takes no time to keep it: for a loop never told, nothing)
    } else if (known && g_clock - told->second >= 0 && g_clock - told->second < kLoopTellEvery) {
        return;
    }
    HeroEffect e;
    e.kind = static_cast<int>(HeroShot::SoundLoop);
    e.frame0 = HeroSoundName(DngUserData, DngUserData->active_chr_no);
    if (!SoundNameOk(e.frame0)) return;
    *told = keep == 0 ? std::pair{0, 0} : std::pair{sound + 1, g_clock};
    e.frame1 = e.frame0;
    e.value = sound;
    e.amount = static_cast<float>(keep);
    float at[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    MainChara__2->GetPosition(at);
    for (int axis = 0; axis < 3; ++axis) e.pos[axis] = at[axis];
    if (g_hero_effects.size() >= kHeroEffectsKept) g_hero_effects.erase(g_hero_effects.begin());
    g_hero_effects.push_back(e);
}

void HeroSwung(const void *chara, const char *frame0, const char *frame1, int length, int fade, int hold) {
    if (chara == nullptr || chara != MainChara__2 || !InDungeon() || frame0 == nullptr || frame1 == nullptr) return;
    HeroEffect e;
    e.kind = static_cast<int>(HeroShot::Trail);
    e.frame0 = frame0;
    e.frame1 = frame1;
    if (!FrameNameOk(e.frame0) || !FrameNameOk(e.frame1)) return;
    e.value = length;
    e.amount = static_cast<float>(fade);
    e.target = hold;
    if (g_hero_effects.size() >= kHeroEffectsKept) g_hero_effects.erase(g_hero_effects.begin());
    g_hero_effects.push_back(std::move(e));
}

std::vector<HeroEffect> TakeHeroEffects() { return std::exchange(g_hero_effects, {}); }

// As the firing game's own actscript.cpp draws each shot (ShotNormalGun and the others), less the attack: no collision
// primitive, so it strikes nothing here (the firing game's hits are what count).
static bool AvatarEffectNow(int slot, const HeroEffect &e) {
    if (!AvatarShown(slot) || !InDungeon() || FxScriptMan == nullptr || DngStatus.mode != DNG_STATUS_FIELD) return false;
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(e.pos[axis]) || !std::isfinite(e.dir[axis])) return false;
    }
    CEffectScriptMan &fx = *FxScriptMan;
    float pos[4] = {e.pos[0], e.pos[1], e.pos[2], 1.0f};
    float dir[4] = {e.dir[0], e.dir[1], e.dir[2], 0.0f};
    switch (static_cast<HeroShot>(e.kind)) {
        case HeroShot::Sound: {
            // played from where the avatar stands, as an effect plays a sound at a place (_SE_PLAY of effscript.cpp)
            Avatar &a = g_avatars[static_cast<std::size_t>(slot)];
            if (!a.body || e.value < 0 || e.value > 255 || !LoadAvatarSound(a, slot, e.frame0)) return false;
            float at[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            a.body->GetPosition(at);
            at[3] = 1.0f;
            float volume = 0.0f, pan = 0.0f;
            sndGetVolPan(&volume, &pan, at, 160.0f, 1200.0f);
            sndSePlayVPf(a.se_bank, e.value, volume, pan, 0);
            return true;
        }
        case HeroShot::SoundLoop: {
            // begun, or kept going, as CLoopSeMngr::SeLoopPlayStop does; AvatarLoopsStep plays it from the avatar
            Avatar &a = g_avatars[static_cast<std::size_t>(slot)];
            if (!a.body || e.value < 0 || e.value > 255 || !LoadAvatarSound(a, slot, e.frame0)) return false;
            Avatar::LoopSound *loop = nullptr;
            for (Avatar::LoopSound &l : a.loops) {
                if (l.sound == e.value) {
                    loop = &l;
                    break;
                }
                if (loop == nullptr && l.sound < 0) loop = &l;
            }
            if (loop == nullptr) return false; // (four going at once: a fifth is not heard)
            const bool going = loop->sound == e.value;
            if (!(e.amount > 0.0f)) { // (kept no frames: stopped now)
                if (going) sndSeStop(a.se_bank, loop->sound, kLoopVoice);
                if (going) *loop = Avatar::LoopSound{};
                return true;
            }
            loop->sound = e.value;
            // (told every few frames, not every one: it goes on that much longer between words)
            loop->keep = std::clamp(static_cast<int>(e.amount), 1, 600) + kLoopTellEvery;
            loop->count = going ? 1 : 0;
            return true;
        }
        case HeroShot::Gun: {
            if (!MakeEffect(fx, kBullet, pos)) return false;
            float flight[4];
            sceVu0ScaleVector(flight, dir, 20.0f);
            fx.SetScriptVect2(flight, 0, -1);
            MakeEffect(fx, kMuzzleFlash, pos);
            return true;
        }
        case HeroShot::MachineGun: {
            const float length = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
            if (!(length > 0.0001f)) return false;
            MachineGun.Set(pos, dir);
            const int index = MachineGun.index;
            // (Set takes the next free bullet; when every bullet flies it takes none, and the one at index is another's)
            if (index >= 0 && index < MACHINE_GUN_BULLET_NUM && MachineGun.life[index] == 90 &&
                MachineGun.start_pos[index][0] == pos[0] && MachineGun.start_pos[index][2] == pos[2]) {
                MachineGun.col_prim_id[index] = -1;
            }
            MakeEffect(fx, kMuzzleFlash, pos);
            return true;
        }
        case HeroShot::Grenade: {
            float muzzle[4];
            sceVu0ScaleVector(muzzle, dir, 500.0f);
            sceVu0AddVector(muzzle, pos, muzzle);
            dir[1] += 0.1f;
            CRocketLauncher *launcher = RocketLauncher.Get();
            if (launcher == nullptr) return false;
            launcher->SetPos(pos, muzzle, dir);
            launcher->target_chara = TargetHere(e.target);
            launcher->speed = 20.0f;
            launcher->homing_delay = 4;
            launcher->homing_time = launcher->target_chara >= 0 ? 30 : 0;
            launcher->col_prim_id = -1;
            MakeEffect(fx, kMuzzleFlash, pos);
            return true;
        }
        case HeroShot::Laser: {
            // 0-2 Max's laser (ShotLaserGun: from 20 ahead of the muzzle, straight); 3 the ridepod's laser and 4 its
            // beam (actscript.cpp's _SHOT: from the muzzle, as their look flies and homes)
            const int type = std::clamp(e.value, 0, 4);
            const bool ridepod = type >= LASER_VISUAL_CYAN;
            float muzzle[4], aim[4];
            sceVu0ScaleVector(muzzle, dir, ridepod ? 0.0f : 20.0f);
            sceVu0AddVector(muzzle, pos, muzzle);
            sceVu0ScaleVector(aim, dir, 500.0f);
            sceVu0AddVector(aim, ridepod ? pos : muzzle, aim);
            CLaserGun *laser = LaserGun.Get();
            if (laser == nullptr) return false;
            laser->SetPos(muzzle, aim, dir);
            laser->target_chara = ridepod ? TargetHere(e.target) : -1;
            if (!ridepod) {
                laser->speed = 30.0f;
                laser->homing_delay = 99999;
                laser->homing_time = 0;
            }
            laser->SetVisualCode(type);
            laser->col_prim_id = -1;
            if (MakeEffect(fx, kMuzzleFlash, ridepod ? muzzle : pos)) {
                // (the flash in the shot's colours, as ShotLaserGun and _SHOT have them)
                static constexpr float kColours[5][4] = {{64, 128, 64, 128}, {64, 64, 128, 128}, {128, 32, 128, 180},
                                                         {0, 128, 128, 160}, {128, 64, 0, 160}};
                fx.SetValue(0, 1, 0, -1);
                for (int i = 0; i < 4; ++i) fx.SetValue(i + 1, kColours[type][i], 0, -1);
            }
            return true;
        }
        case HeroShot::Throw: {
            // As CActionChara::EntryThrowItem, ThrowItemObject and RemoveThrowItem do it, owned by the avatar (the
            // effect keeps the item on its owner's "item" frame until it is thrown).
            Avatar &a = g_avatars[static_cast<std::size_t>(slot)];
            const int owner = a.slot;
            const int phase = static_cast<int>(e.amount);
            if (phase == 2 || phase == 0) { // put away, or a new one held up: the one held goes
                if (a.held_effect >= 0) fx.DeleteEffSpt(owner, a.held_effect);
                a.held_effect = -1;
                if (phase == 2) return true;
            }
            if (phase == 1) {
                if (a.held_effect < 0) return false;
                fx.SetScriptProgNo(EFFECT_SCRIPT_PROG_FINISH, owner, a.held_effect);
                fx.SetScriptVect1(pos, owner, a.held_effect);
                a.held_effect = -1;
                return true;
            }
            if (phase != 0 || e.value <= 0 || e.value >= ITEM_NO_MAX) return false;
            if (a.body->shaped || FindFrame(*a.body, "item") == nullptr) return false; // (nothing to hold it up with)
            // The item's model, as EntryThrowItem picks it from the dungeon's throwables (the first for any other).
            static constexpr int kThrown[] = {0x114, 0x118, 0x119, 0x11A, 0x11B, 0x11C, 0x11D, 0x11E, 0x11F,
                                              0x120, 0x121, 0x122, 0x123, 0x124, ITEM_ID_GIFT_CAPSULE, 0x186, 0x187, 0x133};
            int model = 0;
            for (int i = 0; i < static_cast<int>(std::size(kThrown)); ++i) {
                if (kThrown[i] == e.value) model = i;
            }
            char thrown[] = "\x8E\xE8\x93\x8A\x82\xB0\x94\x9A\x92" "e";
            const int base_no = fx.SearchBaseNo(thrown);
            if (!EnsureBase(fx, base_no)) return false;
            _EFF_SCRIPT *held = fx.CreateEffSpt(base_no, owner, 1);
            if (held == nullptr) return false;
            held->shown_only = true;
            a.held_effect = held->slot;
            fx.SetValue(0, 1, owner, held->slot);
            fx.SetValue(1, e.value, owner, held->slot);
            if (ActionScriptEnv.item_chara != nullptr) {
                fx.SetCharacter(&ActionScriptEnv.item_chara[model], owner, held->slot);
                fx.SetTexb(ActionScriptEnv.texb, owner, held->slot);
            }
            return true;
        }
        case HeroShot::Rocket: {
            float ahead[4];
            sceVu0ScaleVector(ahead, dir, 500.0f);
            sceVu0AddVector(ahead, pos, ahead);
            const bool missile = e.value == 1;
            if (missile) {
                // Scattered as _SHOT scatters a missile, from numbers of our own: the game's draws are its own play's.
                static std::uint32_t scatter = 12345u;
                const auto next = [] {
                    scatter = scatter * 1664525u + 1013904223u;
                    return static_cast<float>(scatter >> 8) / 16777216.0f; // 0 to 1
                };
                dir[0] += dir[2] * (next() - 0.5f);
                dir[2] += dir[0] * (next() - 0.5f);
                dir[1] += next();
            }
            CRocketLauncher *launcher = RocketLauncher.Get();
            if (launcher == nullptr) return false;
            launcher->SetPos(pos, ahead, dir);
            launcher->target_chara = TargetHere(e.target);
            if (!missile) {
                launcher->speed = 20.0f;
                launcher->homing_delay = 2;
                launcher->homing_time = 30;
            }
            launcher->col_prim_id = -1;
            return true;
        }
        case HeroShot::Magic: {
            if (!EnsureBase(fx, e.value) || fx.CreateEffSpt(e.value, 0, 0) == nullptr || !Drawn(fx, pos)) return false;
            fx.SetScriptVect2(dir, 0, -1);
            fx.SetValue(0, std::isfinite(e.amount) ? e.amount : 0.0f, -1, -1);
            fx.SetScriptTargetId(TargetHere(e.target), -1, -1);
            return true;
        }
        case HeroShot::Trail: {
            // The avatar's own blade draws it (its trail made in Build), between the same two frames of its weapon.
            AvatarBody &body = *g_avatars[static_cast<std::size_t>(slot)].body;
            if (body.shaped || body.sword_effect[0] == nullptr || !FrameNameOk(e.frame0) || !FrameNameOk(e.frame1)) return false;
            mgCFrame *start = FindFrame(body, e.frame0.c_str()), *end = FindFrame(body, e.frame1.c_str());
            if (start == nullptr || end == nullptr) return false;
            body.sword_effect[0]->StartEffect(start, end, std::clamp(e.value, 1, 64), std::clamp(static_cast<int>(e.amount), 0, 120),
                                              std::clamp(e.target, 0, 120));
            return true;
        }
    }
    return false;
}

// ---- Monsters: the API ---------------------------------------------------------------------------------------------

namespace {

// Gone on the leader: gone here, without a death (no drops, no count). The slot is freed as the game frees a dead
// monster's (CMonsterMan::ThinkHost): the kind goes too, since the floor's clear counts every slot of a script
// monster's kind, so a copy left with it would keep this game's floor from ever being cleared.
void FreeMonsterSlot(int slot) {
    CActiveMonster *monster = Monster(slot);
    if (monster == nullptr || (monster->state == ACTIVE_MONSTER_NONE && monster->chara_kind != ACTION_KIND_SCRIPT)) return;
    monster->state = ACTIVE_MONSTER_NONE;
    monster->chara_kind = ACTION_KIND_NONE;
    monster->damage_req = ACTION_DAMAGE_REQ_NONE;
    monster->life = 0;
    if (DngMainScene != nullptr) DngMainScene->ResetActive(SCENE_DATA_CHARA, DNG_CHARA_SLOT_MONSTER + slot);
}

// The kinds a slot could not be given, each said once on the log (slot * 65536 + kind).
std::vector<int> g_unmade;

// A monster the leader's game gained after the floor was built (a mod's spawn, a boss's summons, an event's) is in
// no slot here: one of its kind is made in the leader's slot, where it stands there, through the game's own
// placement (CMonsterMan::SetActiveMonster), and becomes the copy. A monster of another kind in that slot is not on
// the leader's floor and goes first. A kind this game does not know (a mod's enemy no mod here registered) is not
// made: that slot alone is refused.
CActiveMonster *MakePuppet(int slot, const Puppet &puppet) {
    if (ActiveMonster == nullptr || DngMainScene == nullptr) return nullptr;
    std::string why;
    const int   refer = moonmod::ChronicleTwoMonsterReferFor(puppet.kind, why);
    if (refer < 0) {
        const int key = slot * 65536 + puppet.kind;
        if (std::ranges::find(g_unmade, key) == g_unmade.end() && g_unmade.size() < 256) {
            g_unmade.push_back(key);
            std::fprintf(stderr, "co-op: the leader's monster in slot %d (kind %d) can't be made here: %s\n", slot, puppet.kind, why.c_str());
        }
        return nullptr;
    }
    FreeMonsterSlot(slot);
    float at[4] = {puppet.x, puppet.y, puppet.z, 1.0f};
    float turn[4] = {0.0f, puppet.heading, 0.0f, 1.0f};
    CActiveMonster *made = ActiveMonster->SetActiveMonster(refer, at, turn, -1, slot);
    if (made == nullptr || made->chara_type != DNG_CHARA_SLOT_MONSTER + slot) return nullptr;
    DngMainScene->SetActive(SCENE_DATA_CHARA, DNG_CHARA_SLOT_MONSTER + slot);
    std::fprintf(stderr, "co-op: made the leader's monster in slot %d (kind %d) here\n", slot, puppet.kind);
    return made;
}

} // namespace

bool PuppetSet(int slot, const Puppet &puppet) {
    if (!OnFloor() || ActiveMonster == nullptr || slot < 0 || slot >= MONSTER_ACTIVE_MAX) return false;
    CActiveMonster *monster = Monster(slot);
    PuppetSlot &p = g_puppets[static_cast<std::size_t>(slot)];
    if (puppet.state <= 0 || puppet.kind < 0) {
        FreeMonsterSlot(slot);
        p = PuppetSlot{};
        return true;
    }
    if (monster == nullptr || monster->state == ACTIVE_MONSTER_NONE || monster->monster_id != puppet.kind) {
        // Struck down on the leader already: nothing to make here.
        if (puppet.state != ACTIVE_MONSTER_LIVE || puppet.hp <= 0) {
            if (monster != nullptr && monster->monster_id != puppet.kind) FreeMonsterSlot(slot);
            p = PuppetSlot{};
            return true;
        }
        monster = MakePuppet(slot, puppet);
        if (monster == nullptr) return false;
        p = PuppetSlot{};
    }
    p.used = true;
    p.host = puppet;
    if (puppet.age >= 0) {
        TrailHeard(p.trail, {g_clock - std::min(puppet.age, kMaxAge), puppet.x, puppet.y, puppet.z, puppet.heading, puppet.motion,
                        puppet.motion_flags, puppet.motion_step});
    } else {
        p.trail.clear();
    }
    monster->life = std::clamp(puppet.hp, 0, 1000000);
    if (puppet.max_hp > 0) monster->max_life = puppet.max_hp;
    if (puppet.state == ACTIVE_MONSTER_DEAD && monster->state == ACTIVE_MONSTER_LIVE) {
        monster->life = 0;
        monster->damage_req = ACTION_DAMAGE_REQ_DEAD;
    }
    return true;
}

bool IsPuppet(int slot) { return slot >= 0 && slot < MONSTER_ACTIVE_MAX && g_puppets[static_cast<std::size_t>(slot)].used; }

bool MonstersMirrored() { return g_puppets_pause; }

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

void MonsterDamaged(int slot, int damage, const CColPrim *prim, int element, bool guard) {
    if (!IsPuppet(slot)) return;
    // The copy keeps the host's life: the blow goes to the host's monster. One that took nothing (a guard, a monster
    // it can't hurt) goes too, for the host's game to show it so.
    CActiveMonster *monster = Monster(slot);
    if (monster != nullptr) monster->life = std::max(0, g_puppets[static_cast<std::size_t>(slot)].host.hp);
    if (damage < 0 || (damage == 0 && prim == nullptr) || g_puppet_hits.size() >= 64) return;
    PuppetHit hit{slot, damage};
    if (prim != nullptr && prim->param != nullptr) {
        hit.kind = prim->param->kind;
        hit.flags = prim->param->hit_flags;
        hit.stagger = prim->param->stagger;
        hit.stun = prim->param->stun_time;
        hit.element = element;
        hit.guard = guard;
        float feet[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        if (monster != nullptr) monster->GetPosition(feet);
        for (int k = 0; k < 3; ++k) hit.at[k] = prim->hit_pos[k] - feet[k];
    }
    g_puppet_hits.push_back(hit);
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
        if (!a.wanted || !a.body || a.scene != DngMainScene || a.slot < 0 || a.target.down) continue;
        for (CColPrim &prim : ColPrimMan.prim) {
            if (!prim.active || prim.param == nullptr || (prim.target & DAMAGE_TARGET_PLAYER) == 0) continue;
            if (!prim.IsHit(DngMainScene, a.slot)) continue; // (a blow that lands marks the avatar as struck by it)
            if (grace > 0) continue;
            grace = kAvatarGrace;
            // kind as the mod reads it: 2 a small hit (a flinch), 3 a blow that knocks down
            const int kind = (prim.param->hit_flags & DAMAGE_HIT_KNOCKDOWN) != 0 ? 3 : 2;
            if (g_avatar_hits.size() < 64) {
                AvatarHit hit{i + 1, prim.attacker, std::max(1, static_cast<int>(prim.damage)), kind, static_cast<int>(prim.param->hit_flags)};
                hit.stun = prim.param->stun_time;
                hit.stagger = prim.param->stagger;
                hit.guard_rate = prim.param->critical_rate;
                hit.status = static_cast<long long>(prim.status);
                float feet[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                a.body->GetPosition(feet);
                for (int k = 0; k < 3; ++k) {
                    hit.at[k] = prim.hit_pos[k] - feet[k];
                    hit.toward[k] = prim.hit_vec[k];
                }
                g_avatar_hits.push_back(hit);
            }
            // Here, it lands as a blow on this game's own hero does: a spark where it struck and the floor's hit
            // sound (the hero's cry is that player's game's, and comes with its sounds).
            HitEffectSet(DngMainScene, prim.hit_pos, prim.param->hit_flags);
            sndSePlay(DngMainScene->se_battle_id, 0x16, 0);
            break;
        }
    }
}

bool AvatarEffect(int slot, const HeroEffect &e, int age) {
    if (age < 0) return AvatarEffectNow(slot, e);
    if (!AvatarShown(slot) || !InDungeon() || FxScriptMan == nullptr || DngStatus.mode != DNG_STATUS_FIELD) return false;
    Avatar &a = g_avatars[static_cast<std::size_t>(slot)];
    if (a.pending.size() >= 64) a.pending.erase(a.pending.begin()); // (a flood loses its oldest, not the game's memory)
    a.pending.emplace_back(g_clock - std::min(age, kMaxAge) + kDelay, e);
    return true;
}

// The others' shots, spells and sounds whose moment has come on their avatars' clocks (AvatarEffect with an age).
static void AvatarEffectsDue() {
    for (int i = 0; i < kAvatars; ++i) {
        Avatar &a = g_avatars[static_cast<std::size_t>(i)];
        if (a.pending.empty()) continue;
        std::vector<HeroEffect> due;
        for (auto it = a.pending.begin(); it != a.pending.end();) {
            if (it->first <= g_clock) {
                due.push_back(std::move(it->second));
                it = a.pending.erase(it);
            } else {
                ++it;
            }
        }
        for (const HeroEffect &e : due) AvatarEffectNow(i, e);
    }
}

// ---- Notices on the play screen --------------------------------------------------------------------------------------

namespace {
struct Notice {
    std::string text;
    int         until = 0, frames = 0;
};
std::vector<Notice> g_notices;
constexpr std::size_t kNoticeBytes = 127; // MESSAGE_TASK::text, less its end
constexpr std::size_t kNoticeLines = 3;   // lines to a window
} // namespace

std::vector<std::string> GameNoticeText(const std::string &utf8, std::size_t line_letters) {
    // Words in the game's text: ASCII as it is; any other letter as the game writes one ([UNIxxxx], dung_progtxt_*);
    // a bracket would open one of the game's codes, so it is a parenthesis.
    std::vector<std::pair<std::string, std::size_t>> words; // (the word's text, its letters)
    std::string word;
    std::size_t letters = 0;
    const auto  end_word = [&] {
        if (!word.empty()) words.emplace_back(std::exchange(word, {}), std::exchange(letters, 0));
    };
    for (std::size_t i = 0; i < utf8.size();) {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        if (c < 0x80) {
            ++i;
            if (c == ' ' || c < 0x20) {
                end_word();
                continue;
            }
            word += c == '[' ? '(' : c == ']' ? ')' : static_cast<char>(c);
            ++letters;
            continue;
        }
        const int    extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
        unsigned int code = extra == 3 ? c & 0x07u : extra == 2 ? c & 0x0Fu : c & 0x1Fu;
        std::size_t  j = i + 1;
        for (int k = 0; k < extra && j < utf8.size(); ++k, ++j) code = (code << 6) | (static_cast<unsigned char>(utf8[j]) & 0x3Fu);
        i = extra == 0 ? i + 1 : j;
        char named[16];
        if (extra == 0 || code > 0xFFFF) {
            word += '?';
        } else {
            std::snprintf(named, sizeof(named), "[UNI%04x]", code);
            word += named;
        }
        ++letters;
    }
    end_word();
    // Lines of about forty letters, broken at spaces (a word longer than a line has one to itself), and windows of
    // three lines that fit the window's text.
    std::vector<std::string> lines;
    std::string              line;
    std::size_t              line_count = 0;
    for (auto &[text, count] : words) {
        if (!line.empty() && line_count + 1 + count > std::max<std::size_t>(line_letters, 8)) {
            lines.push_back(std::exchange(line, {}));
            line_count = 0;
        }
        if (!line.empty()) {
            line += ' ';
            ++line_count;
        }
        line += text;
        line_count += count;
    }
    if (!line.empty()) lines.push_back(line);
    std::vector<std::string> windows;
    std::string              window;
    std::size_t              in_window = 0;
    for (std::string &l : lines) {
        if (l.size() > kNoticeBytes) l.resize(kNoticeBytes); // (one line past the window's text: cut, as a last resort)
        if (!window.empty() && (in_window == kNoticeLines || window.size() + 1 + l.size() > kNoticeBytes)) {
            windows.push_back(std::exchange(window, {}));
            in_window = 0;
        }
        if (!window.empty()) window += '\n';
        window += l;
        ++in_window;
    }
    if (!window.empty()) windows.push_back(window);
    return windows;
}

void FieldNotice(const std::string &utf8, int frames) {
    for (std::string &text : GameNoticeText(utf8)) {
        if (g_notices.size() >= 8) g_notices.erase(g_notices.begin());
        g_notices.push_back({std::move(text), g_clock + 600, std::clamp(frames, 90, 240)});
    }
}

// The notices waiting, into the floor's message window when the player is on one, playing (as the game's own are).
static void NoticesStep() {
    if (g_notices.empty()) return;
    std::erase_if(g_notices, [](const Notice &n) { return n.until < g_clock; });
    if (TownNoticesOpen()) { // (in a town, the window co-op keeps there)
        for (Notice &n : g_notices) TownNoticePrint(n.text, n.frames);
        g_notices.clear();
        return;
    }
    if (!InDungeon() || DngStatus.mode != DNG_STATUS_FIELD || MsgTaskMan.mes == nullptr) return;
    for (Notice &n : g_notices) MsgTaskMan.Print(n.text.data(), n.frames, 8, 0);
    g_notices.clear();
}

// The avatars' looped sounds, a frame on, as CLoopSeMngr::Step plays a hero's: begun, then followed where the avatar
// stands, and stopped once no word came for their time. Off the field (a menu, an event) they stop, and begin again
// on the next word.
static void AvatarLoopsStep() {
    const bool field = InDungeon() && DngStatus.mode == DNG_STATUS_FIELD;
    for (int i = 0; i < kAvatars; ++i) {
        Avatar &a = g_avatars[static_cast<std::size_t>(i)];
        if (std::ranges::none_of(a.loops, [](const Avatar::LoopSound &l) { return l.sound >= 0; })) continue;
        if (!field || !AvatarShown(i) || a.se_bank == 0xFFFFFFFFu) {
            AvatarLoopsStop(a);
            continue;
        }
        float at[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        a.body->GetPosition(at);
        at[3] = 1.0f;
        float volume = 0.0f, pan = 0.0f;
        sndGetVolPan(&volume, &pan, at, 160.0f, 1200.0f);
        for (Avatar::LoopSound &l : a.loops) {
            if (l.sound < 0) continue;
            if (l.count == 0) {
                sndSePlayVPf(a.se_bank, l.sound, volume, pan, kLoopVoice);
            } else {
                sndSetSeVolf(a.se_bank, l.sound, volume, kLoopVoice);
                sndSetSePanf(a.se_bank, l.sound, pan, kLoopVoice);
            }
            if (l.count >= l.keep) {
                sndSeStop(a.se_bank, l.sound, kLoopVoice);
                l = Avatar::LoopSound{};
                continue;
            }
            ++l.count;
        }
    }
}

int AvatarLoopsGoing(int slot) {
    if (slot < 0 || slot >= kAvatars) return 0;
    return static_cast<int>(std::ranges::count_if(g_avatars[static_cast<std::size_t>(slot)].loops,
                                                  [](const Avatar::LoopSound &l) { return l.sound >= 0; }));
}

void WorldStep() {
    ++g_clock;
    NoticesStep();
    AvatarsStep();
    AvatarEffectsDue();
    AvatarLoopsStep();
    AvatarHitsStep();
    PuppetsStep();
    SharedEventsStep();
}

void WorldReset() {
    for (int i = 0; i < kAvatars; ++i) {
        g_avatars[static_cast<std::size_t>(i)].wanted = false;
        g_avatars[static_cast<std::size_t>(i)].pending.clear();
        AvatarLoopsStop(g_avatars[static_cast<std::size_t>(i)]);
        g_avatars[static_cast<std::size_t>(i)].body.reset();
        g_avatars[static_cast<std::size_t>(i)].loaded_character = -1;
        g_avatars[static_cast<std::size_t>(i)].scene = nullptr;
        g_avatars[static_cast<std::size_t>(i)].slot = -1;
    }
    for (PuppetSlot &p : g_puppets) p = PuppetSlot{};
    g_puppets_pause = false;
    g_unmade.clear();
    g_puppet_hits.clear();
    g_avatar_hits.clear();
    g_hero_effects.clear();
    g_player_swapped = false;
    g_events_out.clear();
    g_waiting = Waiting{};
    g_granted = Waiting{};
    g_remote_running = false;
    g_next_seed.reset();
    g_last_seed.reset();
}

} // namespace moonmod::coop
