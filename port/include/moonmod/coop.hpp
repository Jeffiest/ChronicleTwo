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

class CColPrim;

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
// Tests: a visit runs, owned by this mod, as TitleBeginVisit leaves one (ResetForTests ends it).
void VisitStartForTests(std::string_view owner);
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
    // How the motion plays, as that player's game plays it (CharaMotionFlag: 0 loops, CHARA_MOTION_HOLD stops on its
    // last frame), and how fast (frames a step; negative: the motion's own pace).
    int         motion_flags = 0;
    float       motion_step = -1.0f;
    // How many frames ago the report was made, on that player's game's clock as this game reckons it. The avatar is
    // then shown on that clock a little behind, eased between the reports kept, and the shots and sounds told with an
    // age come at their own moments on it: moves, swings, trails and sounds keep their order and spacing. Negative:
    // shown as it comes, eased toward.
    int         age = -1;
    float       tint[3] = {-1.0f, -1.0f, -1.0f}; // ambient 0-128 each, or negative for none
    std::string name;
    bool        down = false; // that player is down: this game's monsters neither go for the hero nor strike it
    // That player watches one of the story's scenes in their game: their hero fades from sight where it stands, and is
    // nobody this game's monsters go for, until it is over (then it fades in where it is).
    bool        away = false;
    // That player rides the ridepod (character is then USER_CHARA_MAX) or wears a monster's form (USER_CHARA_MONICA).
    // The avatar is drawn as the ridepod or the monster when the parts or the form below are known and load; else
    // their hero's own body stands in for it, where they are and facing as they face, running while they move (their
    // motion numbers are the ridepod's or the monster's, not the hero's).
    bool        stand_in = false;
    // The ridepod's parts by item_no, in the order leg, arm, body, energy pack (0: not said), and the monster whose
    // form Monica wears, by its monster_id (-1: not said).
    int         ridepod[4] = {0, 0, 0, 0};
    int         form = -1;
};
bool AvatarSet(int slot, const AvatarState &state);
void AvatarClear(int slot); // -1: every slot
bool AvatarShown(int slot);
bool AvatarAt(int slot, float out[3]);
// The avatar bodies on the floor or in the town: made in free scene slots, stepped and drawn by the scene.
void AvatarsStep();
// Where an avatar's name goes on the screen (from its top left, in the screen's units): the point just over its head
// and the point it stands on, as measured when the scene last drew the body with its camera, and the name the mod gave
// (UTF-8). Each drawing is given once. False for an avatar the scene has not drawn since (none, hidden, elsewhere,
// behind the camera) or one with no name.
bool AvatarNameOnScreen(int slot, float head[2], float feet[2], std::string &name);
// How far an avatar is in sight, 0 to 1, as it fades in and out (its name fades with it); 0 for none.
float AvatarSeen(int slot);
// A mod's notice (ui.notify) in the game's own field message window, as an item found or a key taken is told, on a
// dungeon floor (MsgTaskMan); one that comes off a floor waits for one, a while. The text is the game's own from the
// mod's UTF-8: broken into lines and into windows the way the game's messages are. GameNoticeText is that text.
void                     FieldNotice(const std::string &utf8, int frames);
// coop_ui: a town has no message window of the floor's (MsgTaskMan), so co-op keeps one of its own, made as the floor
// makes its own: TownNoticesOpen says whether the player walks a town now (not in the editor, no question open), and
// TownNoticePrint queues one window of text (GameNoticeText's) in it for so many frames.
bool                     TownNoticesOpen();
void                     TownNoticePrint(const std::string &window, int frames);
// (line_letters: about how many letters to a line; a question's window is wider than a notice's)
std::vector<std::string> GameNoticeText(const std::string &utf8, std::size_t line_letters = 40);

// The other players' names over their heroes' heads (coop_avatar_names_moonmod.cpp), drawn with the game's own
// font in the 2D pass after the scene, with its camera: the choice panel's Draw2D calls it first, so a panel lies
// over them.
void DrawAvatarNames();
// Whether this game's player is free to be asked or told something: walking a floor or a town, or between actions
// in a town's Georama editor. Not in a scene or a talk, a menu, a photo, a door, a minigame, or a map loading: co-op's
// questions and notices wait those out, as the game's own never break into them.
bool PlayerFree();
// Whether this game plays one of the story's scenes now, in the town or on the floor it plays in: a drama scene, the
// kind the game lets the player skip with Start, from its start (event_func.cpp's InitDramaScene) while its skip is
// offered and the place's event has the game. Not a talk, a menu or a door. The other players' heroes fade from sight while one plays here, as the game's
// scenes keep their cast to themselves, and world.get says so (story_scene) so the others hide this player's hero.
bool StoryScenePlaying();
// Every frame (WorldStep): a scene the player has been free since is over for good, its skip left offered or not.
void StoryScenesStep();
void StoryScenesReset();

// ---- What a hero fires: shots and spells seen on the other screens (coop_world_moonmod.cpp) -------------------------
// actscript.cpp tells each shot this game's own hero fires (HeroFired); player.effects_take gives them to the mod,
// whose peers draw them from that player's avatar (world.avatar_effect): the same effect, flying the same way, with no
// attack in it (the shot's damage is the firing game's own, and reaches the host's monsters as that game's hits do).

enum class HeroShot : int {
    Gun = 1,        // Max's pistol: the bullet and the muzzle flash
    MachineGun = 2, // Max's machine gun or the ridepod's: a bullet of the burst
    Grenade = 3,    // Max's launcher: a grenade
    Laser = 4,      // Max's laser, or the ridepod's laser or beam: a shot (value: its look, LASER_VISUAL_* 0-4)
    Magic = 5,      // Monica's armband or her magic sword's charge (value: the effect base, amount: its tint)
    Trail = 6,      // a blade's trail: from frame0 to frame1 of the hero's weapon (value: its length, amount: the steps
                    // it fades over, target: the steps it holds first); pos and dir are not used
    Rocket = 7,     // the ridepod's launcher (value 0) or a missile (value 1, scattered here as the game scatters it)
    Throw = 8,      // an item held up to throw (amount 0), thrown to pos (1) or put away (2); value: its item_no
    Sound = 9,      // a sound of the hero's (its action script's _SE_PLAY): value, its number in the hero's sound bank,
                    // frame0, that bank by name (CH_000: snd2/chara/CH_000.sound.json); played from the avatar
    SoundLoop = 10, // a looped sound of the hero's (_SE_LOOP_PLAY: a charge, an engine): value and frame0 as Sound's,
                    // amount the frames it goes on after its last word; told again while the hero keeps it going
};
struct HeroEffect {
    int   kind = 0; // HeroShot
    float pos[3] = {0.0f, 0.0f, 0.0f};
    float dir[3] = {0.0f, 0.0f, 0.0f};
    int   value = 0;
    float amount = 0.0f;
    int   target = -1; // the monster's scene slot the shot was aimed at (the same slot in every game), or -1
    std::string frame0, frame1; // a trail's ends: frames of the weapon, by name
};
// actscript.cpp: a character fired; kept when it is this game's hero.
void                    HeroFired(const void *chara, HeroShot kind, const float *pos, const float *dir, int value = 0,
                                  float amount = 0.0f, int target = -1);
// actionchara.cpp: a character's blade began a trail (CActionChara::StepEffect); kept when it is this game's hero.
void                    HeroSwung(const void *chara, const char *frame0, const char *frame1, int length, int fade, int hold);
// actscript.cpp: a character's action script played one of its sounds (_SE_PLAY); kept when it is this game's hero.
void                    HeroSounded(const void *chara, int sound);
// actscript.cpp: a character's action script started or kept up a looped sound (_SE_LOOP_PLAY), to go on `keep` frames
// after; kept when it is this game's hero, at most every few frames for one sound.
void                    HeroLoopSounded(const void *chara, int sound, int keep);
// How many looped sounds an avatar has going (0-4), for tests.
int                     AvatarLoopsGoing(int slot);
std::vector<HeroEffect> TakeHeroEffects();
// Draws another player's shot from avatar slot (0-2). False when the avatar is not on this floor or the effect can't
// be made here. With an age (frames since it was fired, on that player's clock as AvatarState::age reckons it) it is
// drawn when the avatar, shown a little behind, comes to that moment: true then means it is kept for it.
bool                    AvatarEffect(int slot, const HeroEffect &effect, int age = -1);

// ---- Monsters: the host's (coop_world_moonmod.cpp) -----------------------------------------------------------------

struct Puppet {
    int   kind = -1, state = -1, hp = -1, max_hp = 0, motion = -1;
    float x = 0.0f, y = 0.0f, z = 0.0f, heading = 0.0f;
    int   motion_flags = 0;    // as AvatarState's: how the leader's monster plays its motion
    float motion_step = -1.0f; // and how fast
    int   age = -1;            // frames since the leader's report, on its clock: shown behind and eased, as avatars are
};
// A blow on a copy, as this game's CMonsterMan::CheckDamage took it, for the leader's game to show and take as its
// own: the damage row's kind and flags, the strongest element (-1 none), stagger and stun, whether the monster guarded,
// and where it landed from the monster's feet.
struct PuppetHit {
    int   slot = -1, damage = 0;
    int   kind = -1, element = -1, flags = 0, stagger = 0, stun = 0;
    bool  guard = false;
    float at[3] = {0.0f, 0.0f, 0.0f};
};
// A monster's blow on another player's hero, as the leader's game saw it land: the damage row's flags, stun, stagger
// and the share of it a guard lets through (critical_rate), its special status bits (poison and the rest), where it
// landed from the hero's feet and which way it drove.
struct AvatarHit {
    int   avatar = 0, monster = -1, damage = 0, kind = 0, flags = 0;
    int   stun = 0, stagger = 0, guard_rate = 0;
    long long status = 0;
    float at[3] = {0.0f, 0.0f, 0.0f}, toward[3] = {0.0f, 0.0f, 0.0f};
};
bool                   PuppetSet(int slot, const Puppet &puppet);
bool                   IsPuppet(int slot);
// This floor's monsters are copies of another game's: their own thinking rests (DNG_PAUSE_MONSTER_AI), while the
// floor's effects go on (dng_main.cpp asks).
bool                   MonstersMirrored();
// A copy whose monster died in the leader's game dies here as the game's own (CMonsterMan::ThinkHost plays its
// death). dng_main.cpp: some copy is dying, so the monsters think this frame; monster.cpp: every other one rests.
bool                   CopiesDying();
bool                   CopyRests(int slot);
void                   PuppetsClear();
std::vector<PuppetHit> TakePuppetHits();
std::vector<AvatarHit> TakeAvatarHits();
// Host: during a monster's turn the player stands at the nearest player (its own or an avatar).
void MonsterThinkBegin(int slot);
void MonsterThinkEnd();
// monster.cpp: a blow took this much of a monster's life; on a guest's copy it goes to the host instead, with how it
// landed (prim, the strongest element, whether the monster guarded) so the host's game shows it as its own.
void MonsterDamaged(int slot, int damage, const CColPrim *prim = nullptr, int element = -1, bool guard = false);

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
// ("counter"), the story's progress ("progress", id 0; taken as the higher of the two games'), who the story has in
// the party ("party", id 0 the members, 1 who can be changed to, 2 the mask), a townsperson who can join ("recruit",
// value {chara_no, status}, the status without its in-party bit, which is each player's own pick), a dungeon
// floor's record ("floor", id its place in the save's records, value its nine numbers), how many of a Georama part
// are built and waiting to be placed ("stock", id the part's number), an item one of the story's scripts gave the
// player ("gift", id the item's number, value how many; told only when a story flag, counter or progress changed within
// 300 frames of it, so not a chest's, a shop's or a floor's prize, which are the player's own; world.change_apply adds
// it to this game's bag) and a town's Georama ("town", id 0-4: only that
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
// How many gifts the register has told since it was reset (a gift is told only when the story moved around it).
int RegisterGiftsTold();

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
// The town on screen is written into its record as it stands: in its editor, and while its player walks free (a town
// often stands ahead of its record outside the editor too: the record is written only when the player leaves).
void TownLiveSync();
// The same at once, whatever the player is doing (a town about to be sent or merged into): the record is what the
// other game starts from and what its change is merged into, so a part only standing here would be lost in both.
void TownRecordFromLiveMap();
// Whether this town's live map may be written over its record: not while a rebuild from the record is pending for it
// (the map lacks the other player's change, and writing it would take that change away).
bool TownRecordWritable(int town);
void TownRebuildWanted(int town);
bool TownRebuildPending();
void TownRebuildStep();

// Whether the town's Georama editor is open and between actions, so its live map may be built again from its record
// with the player still in the editor.
bool TownEditorReady();
void TownBeforeSave();
int  TownRecordParts(int town); // the parts a town's record holds (its layout), or -1 when there is no record
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
    // Asked as the game asks "Go to the next floor?": the title and subtitle in its own message window, with its Yes
    // (the first option) and No (the second) under them, chosen left and right (ui.choice_open's style "yes_no", two
    // options, on a floor or in a town; at the title it is the panel).
    bool                      yes_no = false;
};
bool          ChoiceOpen(Choice choice);
// Closes the panel if this mod opened it, with no ui.choice_made (the mod closed it, so it knows); another mod's panel
// is left open. Answers whether a panel was closed.
bool          ChoiceCloseQuietly(std::string_view owner);
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
