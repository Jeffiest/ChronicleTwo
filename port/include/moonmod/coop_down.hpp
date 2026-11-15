#pragma once

// Co-op's "down, not out" as the game's own units ask about it (coop_down_moonmod.cpp). Apart from moonmod/coop.hpp
// so the units that ask need no JSON.

class CActionChara;

namespace moonmod::coop {

// The hero's death is about to run (CActionChara::RunScript, the death request): true when the rule holds and the
// hero goes down instead. The caller then leaves the game's death event (1200: change hero, or game over) unraised.
bool HeroFalls(const CActionChara *chara);
// The hero lies down under the rule: no new death is asked for, and the script that laid them down is not left.
bool HeroIsDown();
bool HeroHeldDown(const CActionChara *chara);

} // namespace moonmod::coop
