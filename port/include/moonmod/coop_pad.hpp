#pragma once

// input/gamepad.cpp: while a co-op choice panel is open (ui.choice_open: "Join them on the floor?", "Swap heroes?")
// the pad's buttons are the panel's, so the press that picks an option does not also swing the weapon, talk to a
// villager or open the menu. The sticks stay the game's: the hero can still walk while the player decides.
//
// Declared apart from moonmod/coop.hpp, which brings nlohmann/json and must come before the game's headers.

namespace moonmod::coop {

// True while a panel is open, except while the panel itself is reading the pad.
bool PanelHoldsPad();

} // namespace moonmod::coop
