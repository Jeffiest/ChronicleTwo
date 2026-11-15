#pragma once

// Co-op's split of a Chronicle Two save (save.world_export/import, save.player_export/import): a visit plays the
// host's world with the guest's own things. The world is what the host's save says of the story and the places: the
// story flags and counters, each map's flags, the day and time, where the host is, the Georama parts held and each
// town's record and layout, dungeon progress, the quests, the monster book and the fishing tournament. A player's own
// things are what Max, Monica and the ridepod have and carry: their status and equipment, the bag, the bait, the
// aquarium, the badges, the townsfolk in the party, inventions and photos, Gilda, medals, the fishing records and the
// costumes. The host's save keeps each guest's things; the guest's own save is never written while visiting.
//
// Both travel as text: a tag ("DCW2" a world, "DCP2" a player), then each field's number, size and bytes, base64.
// A fixed field whose size differs from this build's (another build's layout, another capacity) refuses the whole
// import; a town's layout is a variable field, checked whole before anything is written.
//
// The bytes are packed before they become base64 (what repeats is written once), marked by "!" after the tag:
// a save's records are mostly empty, and unpacked a world is several hundred kilobytes, over what an operation may
// answer and far more than a mod's messages should carry. Text without the mark is read as plain base64.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

class CSaveData;
struct TownLayout;

namespace moonmod::coop {

std::string ExportWorld(const CSaveData &save);
bool        ImportWorld(std::string_view text, CSaveData &save, std::string &error);
std::string ExportPlayer(const CSaveData &save);
bool        ImportPlayer(std::string_view text, CSaveData &save, std::string &error);

// One town's Georama (georama.town_export/import): its save record and its live layout (PortTownLayout), for co-op
// to carry a town edited in one game to the others. "DCT2", the town's number, ":", then the fields, base64.
std::string ExportTown(const CSaveData &save, int town);
bool        ImportTown(std::string_view text, int town, CSaveData &save, std::string &error);
// Another game's edit of a town, merged into this save's: base is the town as that game had it when its player opened
// the editor, edited as they left it. Only what they changed comes across: a house, a river grid or a record field
// they changed takes their value, the parts they placed are added and those they took away taken away; anything
// they left alone stays as this save has it now, so two players building at once both keep their work.
bool MergeTown(std::string_view base, std::string_view edited, int town, CSaveData &save, std::string &error);

// An export with its bytes unpacked: the tag, then plain base64 (what "!" was packed from). For the tests, and for
// looking at an export by hand; every import reads either form. Text that is not a packed export comes back as it is.
std::string PlainText(std::string_view text);

// A number for a town's Georama as it stands (its record and its live layout): the same while nothing of it
// changes, another when a part, a house, the river or a condition does. For telling that a town changed.
std::uint64_t TownFingerprint(const CSaveData &save, int town);

// A town's layout as bytes and back (the variable field of a town; exposed for the tests).
std::string LayoutBytes(const TownLayout &layout);
bool        LayoutFromBytes(std::string_view bytes, TownLayout &layout, std::string &error);

// The host's guests (save.account_*): each guest's own things, kept beside the host's saves as
// <save root>/moonmod/coop_accounts.json, written with every change and read on first use.
struct Account {
    std::string id;     // the guest's game's own id for its player
    std::string name;
    std::string player; // save.player_export text
};
inline constexpr std::size_t kAccountsMax = 64;
inline constexpr std::size_t kAccountMax = 1024 * 1024; // bytes of one account's text

const Account               *AccountGet(std::string_view id);
bool                         AccountSet(Account account, std::string &error);
bool                         AccountRemove(std::string_view id);
std::vector<const Account *> Accounts();
void                         AccountsClear();
bool                         AccountsSave(const std::filesystem::path &directory);
void                         AccountsLoad(const std::filesystem::path &directory);
void                         AccountsDelete(const std::filesystem::path &directory);
bool                         ValidAccountId(std::string_view id);
// The game's accounts folder (<save root>/moonmod); the accounts are read from it the first time they are asked for
// and written to it with each change. A test points it elsewhere.
void                         SetAccountsDirectory(std::filesystem::path directory);
const std::filesystem::path &AccountsDirectory();
// Reads the accounts once, from AccountsDirectory, before the first access.
void                         AccountsEnsureLoaded();
// Writes the accounts to AccountsDirectory (after save.account_set/remove).
bool                         AccountsStore();

// A player's own saves (save.slots_list, save.slot_player_export/import): a player who joins a room picks one of
// their saves, its things come along, and they are written back into it. Only the things: the save's story, towns
// and dungeon records are left as they are. Numbered saves only (not the autosave, which the game writes over).
struct SlotInfo {
    int           slot = 0;      // the save's file number
    bool          ready = false; // readable, complete and of this version
    int           progress = 0;  // how far its story is (CSaveData::game_progress); the chapter follows from it
    int           chapter = 0;
    std::uint64_t play_time = 0; // frames played
};
std::vector<SlotInfo> SlotsList();
// The chapter a story progress stands for, as GetNowChapter gives it for a loaded save.
int                   ChapterOfProgress(int progress);
bool                  SlotPlayerExport(int slot, std::string &text, int &progress, std::string &error);
// Only a JSON save is written (an old memory-card save is refused until the game has converted it). The save keeps
// its own hero and its own townsfolk (party_chara); the rest of the player's things are the text's. Before the first
// write to a save in a run of the game the file is copied to
// <save root>/moonmod/coop-own-saves/<its name>.YYYYMMDD-HHMMSS-NN<ext>: a new copy each run, never over an older
// one, and the newest five of each save kept.
bool                  SlotPlayerImport(int slot, std::string_view text, std::string &error);
// Forgets which saves were copied aside in this run, so the next write copies again (as a new run of the game does).
void                  SlotCopiesForget();

} // namespace moonmod::coop
