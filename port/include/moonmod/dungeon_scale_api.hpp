#pragma once

// MoonMod: the operations of moonmod/dungeon_scale.hpp, dungeons.set_modifiers and dungeons.set_enemy_scale.

#include <optional>
#include <string_view>

#include "moonmod/api.hpp"
#include "moonmod/runtime.hpp"

namespace moonmod::scale {

std::optional<ApiCallResult> Call(std::string_view mod_id, std::string_view path, const Json &args);
void                         AddCapabilities(RuntimeConfig &config);
void                         OwnerUnloaded(std::string_view mod_id);
void                         ResetForTests();

} // namespace moonmod::scale
