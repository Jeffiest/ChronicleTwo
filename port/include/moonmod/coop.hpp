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
#include "moonmod/coop_party.hpp"
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
    int         map_no = -1; // or the map itself (world.get().map_no): a town with no Georama map has no other name
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
    // What that player's hero wears and carries, by item_no: the hat or hair piece, the clothes, the shoes and the
    // second weapon (Max's gun, Monica's armband). Under 1: as this game's own hero of that name has it.
    int         head = -1, clothes = -1, feet = -1, sub = -1;
    float       x = 0.0f, y = 0.0f, z = 0.0f, heading = 0.0f;
    int         motion = 0;
    float       tint[3] = {-1.0f, -1.0f, -1.0f}; // ambient 0-128 each, or negative for none
    std::string name;
    bool        down = false; // that player is down: this game's monsters neither go for the hero nor strike it
    // That player rides the ridepod or wears a monster's form, which this game cannot draw yet: their hero's own body
    // stands in for it, where they are and facing as they face, in its first motion (their motion numbers are the
    // ridepod's or the monster's, not the hero's).
    bool        stand_in = false;
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
// dng_event.cpp, before a floor's layout is built: publishes game.dungeon.generating and, when a mod set a seed,
// starts the game's generator from it, so a preset floor's pick follows the seed as a random floor's layout does.
void               FloorBuildBegin();
// automap.cpp: the seed a random floor's layout is built from (the mod's, else the game's own).
int                FloorSeed(int native_seed);
// event_func.cpp, around the floor's automatic placing of its chests (stage 1) and of its monsters (stage 2): with a
// mod's seed each stage starts from a number made of it, whatever was drawn in between.
void               FloorPlacing(int stage);
void               FloorPlaced(int stage);
std::optional<int> LastFloorSeed();
// world.get().floor_layout: a number for the floor as built (cells, chests and what they hold, monsters and where),
// the same in two games that built the same floor; none until the floor's monsters are placed.
std::optional<std::string> FloorLayoutHash();

// ---- The world as it changes (coop_register_moonmod.cpp) -------------------------------------------------------------

// world.watch_set: the mod is told (game.world.changed {kind, id, value}) of each change this game makes to the parts
// of the save two games share while they play: a story flag ("flag", value true or false), a story counter
// ("counter"), the story's progress ("progress", id 0), who the story has in the party ("party", id 0 the members, 1
// who can be changed to, 2 the mask), a townsperson who can join ("recruit", value {chara_no, status}), a dungeon
// floor's record ("floor", id its place in the save's records, value its nine numbers), how many of a Georama part
// are built and waiting to be placed ("stock", id the part's number), an item one of the story's scripts gave the
// player ("gift", id the item's number, value how many; not a chest's or a floor's prize, which are the player's
// own; world.change_apply adds it to this game's bag) and a town's Georama ("town", id 0-4: only that
// it changed; the mod sends the town with georama.town_export). A town or a floor beginning (a save loaded, a visit
// begun) is where the two games start from, not a change.
void RegisterWatch(std::string_view owner, bool enabled);
bool RegisterWatched();
// The world was replaced whole (save.world_import): what stands now is what the games share.
void RegisterRebase();
// Each frame: tell the watching mod what changed (a bounded number a frame; the rest the next).
void RegisterStep();
// world.change_apply: another game's change, written to the save and not told back. A floor's record is merged
// with this game's (flags together, higher counts, the better time), and what this game holds beyond it is told.
bool RegisterApply(std::string_view kind, int id, const Json &value, std::string &error);
// georama.town_import: the town as it stands after another game's is what the games share (no "town" change told).
void RegisterTownAgreed(int town);

// ---- Down, not out (coop_down_moonmod.cpp) --------------------------------------------------------------------------
// player.down_rule_set {enabled}: while it holds, a hero whose life runs out on a floor plays their death and stays
// lying (player.get().down), struck by nothing and moved by nobody, instead of the game's death event (change hero,
// or game over). player.revive {hp, x, y, z} stands them up; player.give_up lets the game's death event run.
void SetDownRule(std::string_view owner, bool enabled);
bool DownRule();
bool HeroRevive(int hp, const float *at, std::string &error);
bool HeroGiveUp(std::string &error);
void DownStep();
void DownOwnerUnloaded(std::string_view owner);
void DownReset();

// ---- A town as it is built (coop_town_moonmod.cpp) ------------------------------------------------------------------
// A town's Georama is its record in the save and, while the town is on screen, the live map built from it; the game
// brings the two together only when its player leaves the editor or walks into the town. For two games:
// the builder's live map is written into the record as it is built (TownLiveSync, from the register's look at the
// towns, so each part placed is a "town" change), and a game that takes a town its player stands in builds its live
// map from the record again in place (no walk back to the entrance): at once when the player walks free, else when
// the scene or menu is over or the player comes out of a house, and at the latest before the game next writes that
// live map into the record (TownBeforeSave, from EditDataSave).
bool TownOnScreen(int town); // the town loop runs that town (its own map or a house's inside)
void TownLiveSync();
void TownRebuildWanted(int town);
bool TownRebuildPending();
void TownRebuildStep();
void TownBeforeSave();
int  TownLiveParts(); // the parts standing in the town on screen (its live map), or -1 when no town's own map is
void TownReset();
void RegisterOwnerUnloaded(std::string_view owner);
void RegisterReset();

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
