#pragma once

// Capacity the port build adds to the item tables of ps2/src/gamedata.cpp for mod items and weapons.

constexpr int MOONMOD_ITEM_CAPACITY = 32;   /**< Data-only mod items beyond the ITEM_DATA_MAX native usable item records. */
constexpr int MOONMOD_WEAPON_CAPACITY = 32; /**< Data-only mod weapons beyond the WEAPON_DATA_MAX native weapon records. */

/** Clears the mod item and weapon registrations and the item number table; called by CGameData::Initialize. */
void ResetMoonModGameData();

/**
 * Asks the next RegisterMoonModItem/Weapon to take `item_no` (a persisted number) when it is still free; the
 * request is consumed by that registration, which otherwise scans down from ITEM_NO_MAX - 1 as before.
 */
void SetMoonModNextItemNo(int item_no);
