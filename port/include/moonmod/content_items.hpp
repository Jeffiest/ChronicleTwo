#pragma once

/**
 * @file
 * MoonMod items and weapons for Chronicle Two beyond the data-only rows `gamedata_moonmod.cpp` appends: their
 * persistent item numbers (content_slots.hpp), their own name and description text in the item message tables,
 * an owned PNG as menu icon, an owned `mainchr/<stem>.gltf` as the weapon's model, and the character a weapon is
 * for (Max, Monica or a registered hero, who fights as one of them).
 */

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "common.h"
#include "moonmod/api.hpp"

class mgCTexture;

namespace dc2::text {
/** Encodes UTF-8 text (ASCII, Latin-1, `\n`, `{XXXX}` raw codes) as message words ending in MES_CODE_END. */
std::vector<std::uint16_t> EncodeMessageText(std::string_view text);
} // namespace dc2::text

namespace moonmod {

/** First message number of a mod item: slot `n` describes itself at `kModItemMessageBase + n + 10000`. */
inline constexpr int kModItemMessageBase = 30000;
/** Rows of mod text: one per data-only mod item or weapon (MOONMOD_ITEM_CAPACITY + MOONMOD_WEAPON_CAPACITY). */
inline constexpr int kModItemMessageSlots = 64;

/**
 * Reads the extra keys of an item or weapon definition (`description`, `icon`, `model`, `owner`; `menu_scale` is
 * refused, Chronicle Two's menu has no per-weapon scale) for the content `qualified_id` of `mod_id`. Owned files
 * must already be the mod's resource overrides: `icons/<stem>.png`, `mainchr/<stem>.gltf`. Returns false with
 * `error` on a bad value; a definition without these keys always passes.
 */
bool ChronicleTwoContentParseExtras(std::string_view mod_id, std::string_view qualified_id, const Json &definition,
                                    bool weapon, std::string &error);

/** Number of keys in `definition` that ChronicleTwoContentParseExtras consumes. */
std::size_t ChronicleTwoContentExtraKeyCount(const Json &definition);

/** Forgets the extras of a content id that was never materialized. */
void ChronicleTwoContentForgetExtras(std::string_view qualified_id);

/**
 * The persistent item number of `qualified_id` (allocating one from the top of the item numbers when it has none)
 * and tells the native registration to use it. False with `error` when the registry cannot be read or written.
 */
bool ChronicleTwoContentReserveItemNumber(std::string_view qualified_id, bool weapon, int &item_no, std::string &error);

/**
 * Applies the extras to the native row of `item_no` once it exists: the owner's item type, the model stem, the
 * message number and its text, and the icon override.
 */
void ChronicleTwoContentApplyExtras(std::string_view qualified_id, int item_no);

/** `{model, icon, description, owner, holder}` of a content id's extras for the API's JSON (empty object when none). */
Json ChronicleTwoContentAssetsJson(std::string_view qualified_id);

/** The text of a mod message line, or empty when `line_id` is not one. */
std::span<const u16> ChronicleTwoModMessageLine(int line_id);

/** The icon texture a mod registered for `item_no`, made on first use, or null. */
mgCTexture *ChronicleTwoItemIconOverride(int item_no);

/** The game path of `item_no`'s icon override (`icons/<stem>.png`), or empty. */
std::string ChronicleTwoItemIconOverridePath(int item_no);

/**
 * Resolves a weapon's `owner` ("max", "monica" or a registered hero's id) to the character whose weapons it
 * joins (USER_CHARA_MAX or USER_CHARA_MONICA) and the id that holds it.
 */
bool ChronicleTwoResolveWeaponOwner(std::string_view owner, int &combat_character, std::string &holder_id,
                                    std::string &error);

void ResetChronicleTwoContentItemsForTests();

} // namespace moonmod
