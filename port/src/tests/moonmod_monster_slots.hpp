#pragma once

#include <algorithm>
#include <array>
#include <initializer_list>
#include <memory>

#include "dng_main.hpp"
#include "effscript.hpp"
#include "monster.hpp"
#include "scene.hpp"
#include "scoped_global_object.hpp"

namespace moonmod_test {

/**
 * Binds owned monsters into a scene's monster slots and a manager, as a dungeon's CMonsterMan::Initialize does:
 * the manager reaches its monsters only through the scene's slots. Initialize resets every bound monster, so a
 * test sets the monsters' fields after constructing this.
 */
class MonsterSlots {
public:
    MonsterSlots(CMonsterMan &manager, CScene &scene, std::initializer_list<int> slots) {
        scene.chara_num = std::max<int>(scene.chara_num, DNG_CHARA_SLOT_MONSTER_END);
        for (const int slot : slots) {
            monsters_[static_cast<std::size_t>(slot)] = std::make_shared<CActiveMonster>();
            scene.AssignChara(DNG_CHARA_SLOT_MONSTER + slot,
                              dc2::scene::CharacterResource::FromOwned(monsters_[static_cast<std::size_t>(slot)]));
        }
        manager.Initialize(&scene);
    }

    CActiveMonster &operator[](int slot) { return *monsters_[static_cast<std::size_t>(slot)]; }

private:
    dc2::test::ScopedGlobalObject<FxScriptMan>                       effects_;
    std::array<std::shared_ptr<CActiveMonster>, MONSTER_ACTIVE_MAX> monsters_{};
};

} // namespace moonmod_test
