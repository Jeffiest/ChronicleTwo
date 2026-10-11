#include "memcard.hpp"

#include <cstddef>

// The save record and the game options need no host code: every body in ps2/src/savedata.cpp
// is plain data work - story flags and counters, the Georama numbers, the fishing tournament
// schedule, the sub-games' records and SV_CONFIG_OPTION's defaults - and all of it compiles
// and behaves the same on the host, so the definitions there stand. What the port adds
// around them lives elsewhere:
//
//  - port/src/memcard.cpp builds the save image (SAVEDATA_FORMAT) and carries it to and from
//    the card, so the option block is saved and loaded with the rest of CSaveData;
//  - port/src/platform/config.cpp loads and stores the port's own settings in
//    <save root>/config.json at start-up and exit, DC1's arrangement.
//
// The state that is stored in fixed places of that image has to keep its retail offsets and
// sizes, because the save menu reads a file's first 0x2800 bytes for its slot records and the
// menu's free-space math counts on MC_DATA_SIZE_TYPE's constants. These are tripwires for the
// headers changing underneath them.

static_assert(sizeof(SV_CONFIG_OPTION) == 0x40, "the options keep their stored size");
static_assert(sizeof(CSubGameData) == 0x5470, "the bonus data keeps its stored size");
static_assert(offsetof(SAVEDATA_FORMAT, check_digit_half) == 0x28, "the header layout");
static_assert(offsetof(SAVEDATA_FORMAT, unique_counter) == 0x78, "the header layout");
static_assert(offsetof(SAVEDATA_FORMAT, save_data) == 0x80, "the header layout");

static_assert(offsetof(SAVEDATA_FORMAT, save_data) + offsetof(CSaveData, play_time) < 0x2800,
              "the slot header read holds the play time");
static_assert(offsetof(SAVEDATA_FORMAT, save_data) + offsetof(CSaveData, game_progress) < 0x2800,
              "the slot header read holds the story progress");
