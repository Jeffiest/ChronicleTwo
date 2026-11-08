#pragma once

// Online co-op for Chronicle Two (docs/COOP.md): the adapter operations the moonmod.coop mod drives, and the hooks the
// game units call. The host's game is the world; a guest visits it in a fresh game that writes no save. Everything
// here lives in port/src/moonmod/coop_*_moonmod.cpp; the game units carry one-line calls:
//
//   main.cpp            FrameStep()                 once a frame, after the runtime pumps
//   automap.cpp         FloorSeed(seed)             the seed a new floor is built from (dungeons.set_seed)
//   dng_main.cpp        MapEventStarted(...)        a map event the player starts (shared doors)
//                       Draw2D()                    the choice panel over the dungeon
//   editloop.cpp        Draw2D()                    the choice panel over a town
//   monster.cpp         MonsterThinkBegin/End(slot) a monster's turn looks at the nearest player (host)
//   save_slots.cpp      SaveWritesHeld()            a visit writes no save
//   title.cpp           Title*()                    the mod's title mode rows, its page and a visit's start

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "moonmod/api.hpp"
#include "moonmod/runtime.hpp"

struct CSceneEventData;
struct MENU_INIT_ARG;
class CCharacter2;

namespace moonmod::coop {

// ---- The adapter operations (coop_api_moonmod.cpp) -----------------------------------------------------------------

// The co-op operations: nullopt when the path is not one of them (the adapter goes on), else the result.
std::optional<ApiCallResult> Call(std::string_view mod_id, const ApiOperationSpec &operation, const Json &args);
// The capabilities of every co-op operation: supported, or unsupported with the reason (net.* without a socket).
void AddCapabilities(RuntimeConfig &config);
// A mod unloaded: its title modes, its avatars and its session go.
void OwnerUnloaded(std::string_view mod_id);
// Once a frame: the shared events and the choice panel advance, a visit's end is noticed.
void FrameStep();
// Everything co-op holds goes back to the start (tests).
void ResetForTests();
// player.get and world.get carry co-op's extra fields: heading, motion, the weapon and the town hero's place;
// map_no, town_map, editing and the floor's seed (the adapter calls these as it builds its answers).
void ExtendPlayer(Json &player);
void ExtendWorld(Json &world);
// The number of the motion a character plays (what CCharacter2::SetMotion(int) takes), or -1.
int  MotionNumberOf(CCharacter2 &chara);
// The world's and the screens' frame steps and resets (FrameStep and ResetForTests call them).
void WorldStep();
void WorldReset();
void UiStep();
void UiOwnerUnloaded(std::string_view owner);
void UiReset();

// ---- The session: a story, or a visit (coop_ui_moonmod.cpp) --------------------------------------------------------

struct VisitRequest {
    std::string owner;   // the mod that asked
    std::string mode_id; // its title mode
    int         map = 0; // the town (area 0-4) the visit starts in
    std::string world;   // save.world_export text
    std::string player;  // save.player_export text, or empty for a new account
};
bool                   VisitActive();
const std::string     *VisitOwner();
bool                   VisitRequestPending();
void                   QueueVisit(VisitRequest request);
// Ends a visit: game.session.ended with the reason, and the title is asked for (the caller changes the loop).
void                   EndVisit(std::string_view reason);
Json                   SessionJson();

// ---- Avatars: the other players' heroes (coop_world_moonmod.cpp) ---------------------------------------------------

inline constexpr int kAvatars = 3;

struct AvatarState {
    int         character = 0; // USER_CHARA_MAX or USER_CHARA_MONICA
    int         weapon = -1;   // a weapon's item_no, or none
    float       x = 0.0f, y = 0.0f, z = 0.0f, heading = 0.0f;
    int         motion = 0;
    float       tint[3] = {-1.0f, -1.0f, -1.0f}; // ambient 0-128 each, or negative for none
    std::string name;
};
bool AvatarSet(int slot, const AvatarState &state);
void AvatarClear(int slot); // -1: every slot
bool AvatarShown(int slot);
bool AvatarAt(int slot, float out[3]);
// The avatar bodies on the floor or in the town: made in free scene slots, stepped and drawn by the scene.
void AvatarsStep();

// ---- Monsters: the host's (coop_world_moonmod.cpp) -----------------------------------------------------------------

struct Puppet {
    int   kind = -1, state = -1, hp = -1, max_hp = 0, motion = -1;
    float x = 0.0f, y = 0.0f, z = 0.0f, heading = 0.0f;
};
struct PuppetHit {
    int slot = -1, damage = 0;
};
struct AvatarHit {
    int avatar = 0, monster = -1, damage = 0, kind = 0, flags = 0;
};
bool                   PuppetSet(int slot, const Puppet &puppet);
bool                   IsPuppet(int slot);
void                   PuppetsClear();
std::vector<PuppetHit> TakePuppetHits();
std::vector<AvatarHit> TakeAvatarHits();
// Host: during a monster's turn the player stands at the nearest player (its own or an avatar).
void MonsterThinkBegin(int slot);
void MonsterThinkEnd();
// monster.cpp: a blow took this much of a monster's life; on a guest's copy it goes to the host instead.
void MonsterDamaged(int slot, int damage);

// ---- Shared events: doors and gates (coop_world_moonmod.cpp) -------------------------------------------------------

struct SharedEvent {
    int   script = -1; // the event point's number
    int   mode = 0;    // MapEventCheck the player started it with
    int   ext = 0;     // reserved (DC1's extension flag); 0
    float pos[3] = {0.0f, 0.0f, 0.0f};
    float dir[3] = {0.0f, 0.0f, 0.0f};
};
// dng_main.cpp: the player started a map event (a door, a gate, a switch) with the action or item button.
void                     MapEventStarted(int check_type, const CSceneEventData &data);
std::vector<SharedEvent> TakeSharedEvents();
// Queues another player's event to run here once the player is free; false when one is already waiting.
bool                     RunSharedEvent(const SharedEvent &event);
bool                     SharedEventWaiting();

// ---- Floors (coop_world_moonmod.cpp) -------------------------------------------------------------------------------

// dungeons.set_seed: the next new floor is built from this seed, once.
void               SetNextFloorSeed(std::optional<std::uint32_t> seed);
// automap.cpp: the seed the floor is built from, publishing game.dungeon.generating first.
int                FloorSeed(int native_seed);
std::optional<int> LastFloorSeed();

// ---- The choice panel (coop_ui_moonmod.cpp) -----------------------------------------------------------------------

struct ChoiceOption {
    std::string title, subtitle;
};
struct Choice {
    std::string               owner, id, title, subtitle;
    bool                      cancel = true;
    std::vector<ChoiceOption> options;
    std::size_t               cursor = 0;
};
bool          ChoiceOpen(Choice choice);
bool          ChoiceIsOpen();
const Choice *OpenChoice();
// Every frame while open: the pad moves the cursor; a pick or a cancel publishes ui.choice_made to the owner.
void          ChoiceStep();
// The panel over the game (dng_main.cpp, editloop.cpp, title.cpp).
void          Draw2D();

// ---- Title modes (coop_ui_moonmod.cpp) -----------------------------------------------------------------------------

inline constexpr std::size_t kTitleModeLimit = 2;
inline constexpr std::size_t kTitleModeLineLimit = 8;
inline constexpr std::size_t kTitleModeEntryLimit = 8;

struct TitleModeEntry {
    std::string id, label;
    bool        enabled = true;
};
struct TitleMode {
    std::string                 owner, id, local_id, label, title, subtitle, hint;
    std::vector<std::string>    lines;
    std::vector<TitleModeEntry> entries;
    std::size_t                 cursor = 0;
    bool                        root = true;
};
ApiCallResult                 RegisterTitleMode(std::string_view owner, const Json &args);
ApiCallResult                 SetTitleMode(std::string_view owner, const Json &args);
ApiCallResult                 RemoveTitleMode(std::string_view owner, const Json &args);
const std::vector<TitleMode> &TitleModes();
const TitleMode              *OpenTitleMode();
Json                          TitleModeJson(const TitleMode &mode);

// title.cpp: the rows under the game's own (0 when no mod registered one), opening one, the page's keys and draw.
int  TitleExtraRows();
bool TitleOpenRow(int row);
bool TitlePageOpen();
void TitleClosePage();
enum class TitlePageInput { None, Up, Down, Confirm, Back };
enum class TitlePageOutcome { Stay, ReturnToTitle, StartVisit };
TitlePageOutcome TitlePageStep(TitlePageInput input);
// The title's rows and page, drawn over the title (after the game's rows; y is the first extra row's top).
void TitleDrawRows(int x, int y, int row_height, float alpha, int selected_row);
void TitleDrawPage();
// The visit begins: a fresh save takes the host's world and the guest's things; menu_arg's result names the loop
// and map for the title's load path (TitleLoop's next_loop 2). False, with game.session.failed, when it can't.
bool TitleBeginVisit(MENU_INIT_ARG &menu_arg);

} // namespace moonmod::coop
