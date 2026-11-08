#pragma once

/**
 * @file
 * MoonMod playable characters for Chronicle Two past Max and Monica (`characters.register_playable`): a registered
 * hero has an index from USER_CHARA_NUM, fights as Max or Monica (that character's action, weapons and menus),
 * wears his own pack (`mainchr/<stem>.gltf`), and keeps his status (hp, defence, equipment, active items) in a
 * table outside CUserDataManager that is written beside the save as `<save root>/moonmod/characters.json`, never in
 * the retail save image.
 */

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "common.h"
#include "moonmod/api.hpp"

struct CHARA_DATA;
struct COMMON_GAGE;

namespace moonmod {

/** Registered hero records the port holds; indices USER_CHARA_NUM .. USER_CHARA_NUM + kRegisteredCharacterSlots - 1. */
inline constexpr int kRegisteredCharacterSlots = 12;
/** First character index of a registered hero (USER_CHARA_NUM, kept here so userdata.hpp need not change). */
inline constexpr int kRegisteredCharacterBase = 4;

/** `characters.register_playable`: see CONTENT.md. Returns {id, character_id, count}. */
ApiCallResult RegisterChronicleTwoPlayable(std::string_view mod_id, const Json &args);

/** Whether `id` (a qualified mod id) names a registered hero; sets his character index. */
bool ChronicleTwoRegisteredCharacterIndex(std::string_view id, int &character_index);

/** The qualified id of the hero at `character_index`, or empty when none is registered there. */
std::string ChronicleTwoRegisteredCharacterId(int character_index);

/** The qualified id of the hero at `character_index` as a C string that lives while he is registered, or null. */
const char *ChronicleTwoRegisteredCharacterIdText(int character_index);

/** The character a registered hero fights as (USER_CHARA_MAX or USER_CHARA_MONICA); false when not registered. */
bool ChronicleTwoRegisteredCharacterBody(int character_index, int &body);

/** The pack file name (`<stem>.gltf`, under `mainchr/`) of a registered hero, or none. */
std::optional<std::string> ChronicleTwoRegisteredCharacterPack(int character_index, int alternate);

/** The hero's status record, or null when `character_index` is not a registered hero. */
CHARA_DATA *ChronicleTwoRegisteredCharaData(int character_index);

/** Whether the hero has joined the party (API-side membership; the save's party mask is u16 and keeps ids 0..3). */
bool ChronicleTwoRegisteredCharacterJoined(int character_index);
void SetChronicleTwoRegisteredCharacterJoined(int character_index, bool joined);

/** The hero's record for `characters.list`/`characters.get`/`party.list`. */
Json ChronicleTwoRegisteredCharacterJson(int character_index, int active_character_id);

/** Every registered hero's record, in index order. */
Json ChronicleTwoRegisteredCharactersJson(int active_character_id);

/** Resets every hero's status to his registration's start values and leaves the party (a new game). */
void ChronicleTwoResetRegisteredCharacterState();

/**
 * Persistence beside the save: `identity` is the save's sidecar identity (memcard.cpp's fingerprint and counter).
 * Save writes the heroes' records under that identity, keeping other identities and unregistered ids; Load reads
 * them (a missing entry starts afresh) and returns false when the file is present but unreadable.
 */
bool ChronicleTwoRegisteredCharactersSave(std::string_view identity);
bool ChronicleTwoRegisteredCharactersLoad(std::string_view identity);
std::filesystem::path ChronicleTwoRegisteredCharactersFile();

/** Forgets the heroes a mod registered (their saved records stay in the file). */
void UnloadChronicleTwoRegisteredCharacters(std::string_view owner);

void ResetChronicleTwoRegisteredCharactersForTests();

} // namespace moonmod
