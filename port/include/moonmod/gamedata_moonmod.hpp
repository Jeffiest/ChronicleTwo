#pragma once

// Capacity the port build adds to the item tables of ps2/src/gamedata.cpp for mod items and weapons.

constexpr int MOONMOD_ITEM_CAPACITY = 32;   /**< Data-only mod items beyond the 162 native usable items. */
constexpr int MOONMOD_WEAPON_CAPACITY = 32; /**< Data-only mod weapons beyond the 116 native weapons. */

/** Clears the mod item and weapon registrations and the item number table; called by CGameData::Initialize. */
void ResetMoonModGameData();
