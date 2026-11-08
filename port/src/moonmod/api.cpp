#include "moonmod/api.hpp"

#include <array>
#include <ranges>

namespace moonmod {
namespace {

// Names remain stable within API major 1. A port advertises only operations it has
// actually bound to game state; every other catalog entry is queryable as unsupported.
constexpr ApiOperationSpec kApiCatalog[] = {
    {"info.get", "info", "get", "any", "", "Read game, build, platform, and MoonFactory API identity."},
    {"events.list", "events", "list", "any", "", "List stable runtime and game events available to this port."},
    {"events.subscribe", "events", "subscribe", "any", "", "Subscribe to a documented lifecycle or simulation event."},
    {"events.unsubscribe", "events", "unsubscribe", "any", "", "Remove an event subscription owned by this mod."},
    {"player.get", "player", "get", "any", "game.read", "Read the active player and controlled character state."},
    {"player.set", "player", "set", "simulation", "game.write", "Set current HP or Chronicle maximum HP, or Chronicle water, within the selected character's native bounds."},
    {"player.heal", "player", "heal", "simulation", "game.write", "Apply bounded healing to the active player character through native game state."},
    {"player.damage", "player", "damage", "simulation", "game.write", "Apply bounded health damage to the active player character through native game state."},
    {"player.status_add", "player", "status_add", "simulation", "game.write", "Apply a registered status effect to a player."},
    {"player.status_remove", "player", "status_remove", "simulation", "game.write", "Remove a mod-owned or supported status effect."},
    {"player.thirst_get", "player", "thirst_get", "any", "game.read", "Read the active character's water gauge, capacity, and native thirst drain rate in Chronicle."},
    {"player.thirst_set", "player", "thirst_set", "simulation", "game.write", "Set or restore the active or named character's water gauge with native cap clamping in Chronicle."},
    {"player.ridepod_get", "player", "ridepod_get", "any", "game.read", "Read the ridepod's energy, core capacity, defence, voice state, and fitted parts in Chronicle Two."},
    {"player.ridepod_set", "player", "ridepod_set", "simulation", "game.write", "Adjust the ridepod's energy, capacity, shield kits, voice state, or fitted parts in Chronicle Two."},
    {"player.transform_get", "player", "transform_get", "any", "game.read", "Read Monica's monster badge levels, experience, unlock state, and active form in Chronicle Two."},
    {"player.transform_set", "player", "transform_set", "simulation", "game.write", "Adjust a monster badge's level, experience, or unlock state in Chronicle Two."},
    {"party.list", "party", "list", "any", "game.read", "Read party members and their stable handles."},
    {"party.active", "party", "active", "any", "game.read", "Read the active party member."},
    {"party.set_active", "party", "set_active", "simulation", "game.write", "Change the active party member when the game allows it."},
    {"party.town_active", "party", "town_active", "simulation", "game.read", "Read Chronicle's selected playable body for the active town scene."},
    {"party.town_set_active", "party", "town_set_active", "simulation", "game.write", "Select a joined Chronicle party character as the town avatar through its native town model and motion path."},
    {"party.recruit", "party", "recruit", "simulation", "game.write", "Recruit a supported character through the game rules."},
    {"economy.get", "economy", "get", "any", "game.read", "Read the current wallet balance and native currency limit."},
    {"economy.add", "economy", "add", "simulation", "game.write", "Add currency through the game's wallet rules."},
    {"economy.spend", "economy", "spend", "simulation", "game.write", "Spend currency when the wallet has sufficient funds."},
    {"world.get", "world", "get", "any", "game.read", "Read world, map, region, current dungeon-floor identity, and native adventure day where exposed."},
    {"world.transition", "world", "transition", "simulation", "game.write", "Request a validated map, region, or floor transition."},
    {"world.register_dungeon", "world", "register_dungeon", "content", "content.register", "Register additional floors on a supported native dungeon map."},
    {"world.set_time", "world", "set_time", "simulation", "game.write", "Set time as a normalized fraction of the native world cycle."},
    {"world.story_flag_get", "world", "story_flag_get", "any", "game.read", "Read a native story flag by its game-specific numeric id."},
    {"world.story_flag_set", "world", "story_flag_set", "simulation", "game.write", "Set or clear a native story flag by its game-specific numeric id."},
    {"world.story_value_get", "world", "story_value_get", "any", "game.read", "Read a native integer story counter."},
    {"world.story_value_set", "world", "story_value_set", "simulation", "game.write", "Set a native integer story counter within the game's storage range."},
    {"world.map_flag_get", "world", "map_flag_get", "any", "game.read", "Read a native per-map flag such as treasure-box state."},
    {"world.map_flag_set", "world", "map_flag_set", "simulation", "game.write", "Set or clear a native per-map flag."},
    {"entities.query", "entities", "query", "any", "game.read", "Query native entities using filters supported by the active game adapter."},
    {"entities.get", "entities", "get", "any", "game.read", "Resolve a generation-checked entity handle."},
    {"entities.set_stats", "entities", "set_stats", "simulation", "game.write", "Set bounded health, defense, EXP, Gilda, or validated native drop-item fields on a live Chronicle enemy."},
    {"entities.scale_set", "entities", "scale_set", "simulation", "game.write", "Set an owner-scoped visual size multiplier on a live Chronicle enemy; native scale is restored when the owner is unloaded."},
    {"entities.spawn", "entities", "spawn", "simulation", "game.write", "Spawn a registered entity definition."},
    {"entities.despawn", "entities", "despawn", "simulation", "game.write", "Despawn an entity spawned by this mod."},
    {"entities.set_active_limit", "entities", "set_active_limit", "simulation", "game.write", "Set how many monsters stay awake at once and report the floor's monster slot count."},
    {"combat.get", "combat", "get", "any", "game.read", "Read the supported native enemy health and status snapshot."},
    {"combat.damage", "combat", "damage", "simulation", "game.write", "Apply bounded scripted damage through a generation-checked enemy handle."},
    {"combat.status_add", "combat", "status_add", "simulation", "game.write", "Apply a supported timed status to a live enemy through its generation-checked handle."},
    {"combat.heal", "combat", "heal", "simulation", "game.write", "Restore bounded health to a live native enemy."},
    {"combat.register_player_formula", "combat", "register_player_formula", "simulation", "game.write", "Register an owned modifier for native incoming player damage, optionally filtered by weapon or character."},
    {"combat.register_formula", "combat", "register_formula", "content", "content.register", "Register an owned damage modifier, optionally filtered by weapon, enemy model, or Chronicle target HP, with chance, uniform random, and Chronicle timed-ramp multipliers."},
    {"locations.list", "locations", "list", "any", "game.read", "List built-in and registered mod locations supported by the active game."},
    {"locations.get", "locations", "get", "any", "game.read", "Read a built-in or registered location and native availability status."},
    {"locations.register", "locations", "register", "content", "content.register", "Register a namespaced map or location definition with transitions and resources."},
    {"dungeons.list", "dungeons", "list", "any", "game.read", "List native dungeons and mod-registered additions supported by this game."},
    {"dungeons.get", "dungeons", "get", "any", "game.read", "Read a native dungeon or registered extension and its floors."},
    {"dungeons.register", "dungeons", "register", "content", "content.register", "Register additional floors on a supported native dungeon map."},
    {"dungeons.selection", "dungeons", "selection", "any", "game.read", "Read the active Chronicle entrance floor selection, or nil."},
    {"dungeons.set_badges", "dungeons", "set_badges", "simulation", "game.write", "Publish owned native floor-list tier badges 0..5."},
    {"ui.open_skills", "ui", "open_skills", "simulation", "content.register", "Open the owning package skill tree in the manager."},
    {"dungeons.set_modifiers", "dungeons", "set_modifiers", "simulation", "game.write", "Set owned additive generation bonuses in percent (0..300): rooms, monsters, chests, chest_value; {} clears this owner."},
    {"dungeons.set_population", "dungeons", "set_population", "simulation", "game.write", "Scale generated monster and chest targets by realized room count using inclusive per-room min/max ratios; native minimums and engine slot limits still apply; {} clears this owner."},
    {"dungeons.set_enemy_survival", "dungeons", "set_enemy_survival", "simulation", "game.write", "Set this mod's chance for eligible ordinary Chronicle enemies to survive one lethal hit; {} clears this owner."},
    {"dungeons.set_layout", "dungeons", "set_layout", "simulation", "game.write", "Set room count, room area and room sizes for floors built after the call; an empty table restores the native layout."},
    {"dungeons.set_exit_room", "dungeons", "set_exit_room", "simulation", "game.write", "Turn the walk-through exit room on or off for floors built after the call."},
    {"dungeons.floor_rules_get", "dungeons", "floor_rules_get", "any", "game.read", "Read active and per-floor dungeon restriction rules such as Limited Zones or White/Red Seals."},
    {"dungeons.floor_rules_set", "dungeons", "floor_rules_set", "simulation", "game.write", "Override active or per-floor dungeon restriction rules such as Limited Zones or White/Red Seals."},
    {"dungeons.loot_get", "dungeons", "loot_get", "any", "game.read", "Read active floor treasure chest contents and monster drop tables."},
    {"dungeons.loot_override", "dungeons", "loot_override", "simulation", "game.write", "Override floor treasure chest contents or monster drop tables."},
    {"dungeons.back_floor_get", "dungeons", "back_floor_get", "any", "game.read", "Read Chronicle back-floor state, the active floor gate key, and whether the key is held."},
    {"dungeons.back_floor_enter", "dungeons", "back_floor_enter", "simulation", "game.write", "Queue Chronicle's native back-floor transition without consuming the gate key."},
    {"dungeons.spheda_get", "dungeons", "spheda_get", "any", "game.read", "Read the active Chronicle Two Spheda challenge, pin, ball, par, and gauge state."},
    {"dungeons.spheda_spawn", "dungeons", "spheda_spawn", "simulation", "game.write", "Reset and activate the active Chronicle Two Spheda course's pin and ball placement."},
    {"georama.get", "georama", "get", "any", "game.read", "Read settlement reconstruction state and placed pieces."},
    {"georama.element_set", "georama", "element_set", "simulation", "game.write", "Toggle a defined optional visual element on a native Georama plot."},
    {"georama.request_set", "georama", "request_set", "simulation", "game.write", "Set a saved native Georama request's completion state."},
    {"georama.request_known_set", "georama", "request_known_set", "simulation", "game.write", "Set whether a saved native Georama request is known to the player."},
    {"georama.condition_set", "georama", "condition_set", "simulation", "game.write", "Set whether a saved native Georama analysis condition is met."},
    {"georama.register", "georama", "register", "content", "content.register", "Register a data-only Georama piece variant inheriting native model and placement rules."},
    {"georama.place", "georama", "place", "simulation", "game.write", "Place a registered georama piece through native placement rules."},
    {"georama.remove", "georama", "remove", "simulation", "game.write", "Remove a placed georama piece through native rules."},
    {"georama.river_set", "georama", "river_set", "simulation", "game.write", "Set or clear one cell in a saved town's native river grid."},
    {"georama.atla_list", "georama", "atla_list", "any", "game.read", "Inspect Atla spheres on the active Chronicle dungeon floor."},
    {"georama.atla_collect", "georama", "atla_collect", "simulation", "game.write", "Collect an Atla sphere and release its Georama part or chip through Chronicle's native save path."},
    {"georama.satisfaction_get", "georama", "satisfaction_get", "any", "game.read", "Read saved Georama request completion and per-plot reconstruction state for Chronicle towns."},
    {"skills.list", "skills", "list", "any", "game.read", "List mod-owned skill trees and progression nodes."},
    {"skills.get", "skills", "get", "any", "game.read", "Read mod-owned skill ranks, prerequisites, and unlock state."},
    {"skills.register", "skills", "register", "content", "content.register", "Register a namespaced mod-owned skill tree and progression nodes."},
    {"skills.unlock", "skills", "unlock", "simulation", "game.write", "Advance a skill rank after mod-defined prerequisites pass."},
    {"skills.set_rank", "skills", "set_rank", "simulation", "game.write", "Set a skill rank within its registered progression limits."},
    {"models.register", "models", "register", "content", "content.register", "Register a package-owned model and its logical resource ids."},
    {"models.unregister", "models", "unregister", "content", "content.register", "Remove an unused model binding owned by this mod."},
    {"models.replace", "models", "replace", "content", "content.register", "Replace a model through conflict-checked native asset bindings."},
    {"models.resolve", "models", "resolve", "any", "game.read", "Resolve a native monster model table entry and report its asset paths and override state."},
    {"models.remove", "models", "remove", "content", "content.register", "Restore a native model row previously replaced by this mod."},
    {"animations.register", "animations", "register", "content", "content.register", "Bind a model-embedded motion index to a stable mod animation id."},
    {"animations.play", "animations", "play", "simulation", "game.write", "Play a registered animation on a compatible entity."},
    {"animations.stop", "animations", "stop", "simulation", "game.write", "Stop an animation started by this mod."},
    {"combat.styles_list", "combat", "styles_list", "any", "game.read", "List fighting-style definitions supported by the active game."},
    {"combat.styles_get", "combat", "styles_get", "any", "game.read", "Read a fighting-style move set, rules, and compatibility."},
    {"combat.styles_register", "combat", "styles_register", "content", "content.register", "Register a fighting style and its move/animation bindings."},
    {"combat.styles_set_active", "combat", "styles_set_active", "simulation", "game.write", "Switch to a compatible fighting style through native combat rules."},
    {"scripts.register_function", "scripts", "register_function", "content", "content.register", "Register a namespaced callable function exposed by this mod."},
    {"scripts.list_functions", "scripts", "list_functions", "any", "game.read", "List this mod's exported functions and exports from its loaded declared dependencies."},
    {"scripts.call_function", "scripts", "call_function", "simulation", "game.write", "Call an explicitly exposed mod function with validated JSON arguments."},
    {"scripts.unregister_function", "scripts", "unregister_function", "content", "content.register", "Remove a callable function owned by this mod."},
    {"items.list", "items", "list", "any", "game.read", "List built-in and registered mod item definitions."},
    {"items.get", "items", "get", "any", "game.read", "Read one item definition by stable id."},
    {"items.register", "items", "register", "content", "content.register", "Register a data-defined item without replacing a game asset."},
    {"items.give", "items", "give", "simulation", "game.write", "Give a built-in or registered item to an inventory."},
    {"items.remove", "items", "remove", "simulation", "game.write", "Remove a quantity of an item from an inventory."},
    {"items.use", "items", "use", "simulation", "game.write", "Use an owned item through validated native item rules."},
    {"shops.list", "shops", "list", "any", "game.read", "List native town and dungeon shops and their item lineups."},
    {"shops.get", "shops", "get", "any", "game.read", "Read one native shop's goods, currency mode, and item buy/sell prices."},
    {"shops.register_item", "shops", "register_item", "content", "content.register", "Add a native or mod-registered item or weapon to a shop's goods list."},
    {"shops.remove_item", "shops", "remove_item", "content", "content.register", "Remove an item or weapon from a shop's goods list."},
    {"shops.set_items", "shops", "set_items", "content", "content.register", "Replace a native Chronicle shop's item list with an owner-restored list of up to 20 goods."},
    {"shops.set_price", "shops", "set_price", "content", "content.register", "Set the buy and sell prices for an item or weapon in native shops."},
    {"weapons.list", "weapons", "list", "any", "game.read", "List built-in and registered weapon definitions."},
    {"weapons.get", "weapons", "get", "any", "game.read", "Read a weapon definition and current state."},
    {"weapons.definition_set", "weapons", "definition_set", "content", "content.register", "Apply an owner-scoped override to a built-in Chronicle weapon definition; Chronicle restores the original data when that owner unloads."},
    {"weapons.register", "weapons", "register", "content", "content.register", "Register a data-defined weapon and supported behavior."},
    {"weapons.give", "weapons", "give", "simulation", "game.write", "Give a native or registered weapon to a compatible character."},
    {"weapons.equip", "weapons", "equip", "simulation", "game.write", "Equip a weapon through the game's equipment rules."},
    {"weapons.remove", "weapons", "remove", "simulation", "game.write", "Remove one owned, unequipped Chronicle weapon instance."},
    {"weapons.upgrade", "weapons", "upgrade", "simulation", "game.write", "Upgrade a weapon through supported progression rules."},
    {"weapons.stats_set", "weapons", "stats_set", "simulation", "game.write", "Set bounded combat stats on one owned Chronicle weapon instance."},
    {"weapons.durability_set", "weapons", "durability_set", "simulation", "game.write", "Set current durability on one owned Chronicle weapon instance within its native capacity."},
    {"weapons.abs_get", "weapons", "abs_get", "any", "game.read", "Read accumulated ABS experience, next-level ABS threshold, and synthesis points on an owned weapon."},
    {"weapons.abs_add", "weapons", "abs_add", "simulation", "game.write", "Grant ABS experience or synthesis points to an owned weapon."},
    {"weapons.attachments_get", "weapons", "attachments_get", "any", "game.read", "Read fitted attachments or synthesis capacity on an owned weapon."},
    {"weapons.attachment_insert", "weapons", "attachment_insert", "simulation", "game.write", "Insert an attachment into an owned weapon slot or synthesize an attachment into a weapon."},
    {"weapons.attachment_remove", "weapons", "attachment_remove", "simulation", "game.write", "Remove a fitted attachment from an owned weapon slot back to the attachment bag."},
    {"weapons.build_up_list", "weapons", "build_up_list", "any", "game.read", "List native and mod-registered build-up evolution targets for a weapon."},
    {"weapons.build_up_register", "weapons", "build_up_register", "content", "content.register", "Register a custom build-up evolution path from a source weapon to a target weapon."},
    {"weapons.synthesize", "weapons", "synthesize", "simulation", "game.write", "Status-break a qualifying owned weapon into a SynthSphere attachment in Chronicle."},
    {"weapons.spectrumize", "weapons", "spectrumize", "simulation", "game.write", "Spectrumize an owned weapon or item into a synthesized attachment in Chronicle Two."},
    {"photos.ideas_list", "photos", "ideas_list", "any", "game.read", "List learnt photo ideas, carried photos, and photographed subject ids in Chronicle Two."},
    {"photos.idea_unlock", "photos", "idea_unlock", "simulation", "game.write", "Learn a Chronicle Two photo idea through the native invention save data."},
    {"photos.scoops_list", "photos", "scoops_list", "any", "game.read", "List Chronicle Two scoops with known, obtained, photographed, and learnt state."},
    {"inventions.list", "inventions", "list", "any", "game.read", "List native and mod-registered Chronicle Two invention recipes."},
    {"inventions.register", "inventions", "register", "content", "content.register", "Register a namespaced three-idea Chronicle Two invention recipe and its materials."},
    {"inventions.craft", "inventions", "craft", "simulation", "game.write", "Craft an unlocked native or mod-registered invention through native inventory and material rules."},
    {"characters.list", "characters", "list", "any", "game.read", "List character definitions and visible instances."},
    {"characters.get", "characters", "get", "any", "game.read", "Read character stats, state, and stable handle."},
    {"characters.register", "characters", "register", "content", "content.register", "Register a data-defined enemy or town villager (kind = 'npc')."},
    {"characters.register_playable", "characters", "register_playable", "content", "content.register", "Register a playable character past the game's own (pack, portrait, the character he fights as); never in a save."},
    {"characters.spawn", "characters", "spawn", "simulation", "game.write", "Spawn a registered character or enemy."},
    {"quests.get", "quests", "get", "any", "game.read", "Read this mod's quest definitions and objective progress."},
    {"quests.register", "quests", "register", "content", "content.register", "Register namespaced quest objectives and optional currency, item, or weapon rewards."},
    {"quests.start", "quests", "start", "simulation", "game.write", "Start a registered quest."},
    {"quests.advance", "quests", "advance", "simulation", "game.write", "Advance an objective through validated quest state."},
    {"scripts.list_hooks", "scripts", "list_hooks", "any", "game.read", "List supported native script and simulation hooks."},
    {"scripts.call", "scripts", "call", "simulation", "game.write", "Call the active native hook implementation once from its owned override callback."},
    {"scripts.override", "scripts", "override", "content", "scripts.override", "Install or replace an owned callback for a supported native script or simulation hook."},
    {"content.list", "content", "list", "any", "game.read", "List registered typed data definitions owned by enabled mods."},
    {"content.register", "content", "register", "content", "content.register", "Register a typed item, weapon, character, dungeon, or Georama piece definition supported by this game."},
    {"content.unregister", "content", "unregister", "content", "content.register", "Remove a mod-owned pending definition before it is materialized in native game tables."},
    {"save.get", "save", "get", "any", "game.read", "Read namespaced save data and migration version."},
    {"save.set", "save", "set", "simulation", "game.write", "Write bounded namespaced save data."},
    {"save.slot_id", "save", "slot_id", "any", "game.read", "Read the active Chronicle native save slot number for slot-scoped mod state."},
    {"save.migrate", "save", "migrate", "load", "game.write", "Run an owned save migration at the save-load boundary."},
    {"input.bind", "input", "bind", "any", "game.write", "Bind a namespaced action to a keyboard or controller input."},
    {"input.wheel_mode_set", "input", "wheel_mode_set", "simulation", "game.write", "Route mouse-wheel notches to Left/Right or Up/Down during play; an empty request restores native camera-zoom routing."},
    {"input.unbind", "input", "unbind", "any", "game.write", "Remove an input binding owned by this mod."},
    {"input.state", "input", "state", "any", "game.read", "Read a named action state without exposing raw device memory."},
    {"input.consume", "input", "consume", "input", "game.write", "Consume a named action in the active input context."},
    {"input.axis", "input", "axis", "any", "game.read", "Read a processed gamepad stick or trigger axis as a normalized value."},
    {"input.context_push", "input", "context_push", "any", "game.write", "Push an owner-scoped modal input context onto the priority stack."},
    {"input.context_pop", "input", "context_pop", "any", "game.write", "Remove an input context owned by this mod using its token."},
    {"input.context_get", "input", "context_get", "any", "game.read", "Read the active input context and priority stack."},
    {"audio.play", "audio", "play", "simulation", "game.write", "Play a registered sound or music cue."},
    {"input.block_native", "input", "block_native", "any", "game.write", "Hide named pad buttons from the game itself for a few frames (mods still read them)."},
    {"audio.stop", "audio", "stop", "simulation", "game.write", "Stop playback owned by this mod."},
    {"audio.register", "audio", "register", "content", "content.register", "Register a supported audio resource."},
    {"render.layer_register", "render", "layer_register", "render", "content.register", "Register a bounded owner-scoped HUD text layer."},
    {"render.layer_remove", "render", "layer_remove", "render", "content.register", "Remove a HUD text layer owned by this mod."},
    {"render.bar_register", "render", "bar_register", "render", "content.register", "Register or update a bounded owner-scoped HUD progress bar."},
    {"render.bar_remove", "render", "bar_remove", "render", "content.register", "Remove a HUD progress bar owned by this mod."},
    {"render.camera_get", "render", "camera_get", "any", "game.read", "Read the active dungeon camera pose."},
    {"render.camera_set", "render", "camera_set", "render", "game.write", "Set bounded distance, height, or angle on the active dungeon follow camera."},
    {"render.town_camera_set", "render", "town_camera_set", "simulation", "game.write", "Enable or disable an owner-scoped overhead camera during safe town walking."},
    {"ui.register", "ui", "register", "content", "content.register", "Register an owned button in the in-game MoonFactory manager."},
    {"ui.remove", "ui", "remove", "content", "content.register", "Remove an owned MoonFactory manager button."},
    {"ui.notify", "ui", "notify", "any", "game.write", "Show a bounded notice in the mod manager."},
    {"physics.query", "physics", "query", "simulation", "game.read", "Find live monster actor origins inside a sphere; this does not test collision geometry."},
    {"physics.velocity_get", "physics", "velocity_get", "simulation", "game.read", "Read velocity from a stable entity handle."},
    {"physics.velocity_set", "physics", "velocity_set", "simulation", "game.write", "Set velocity through the simulation adapter."},
    {"resources.load", "resources", "load", "any", "content.register", "Load a package-owned resource using adapter rules."},
    {"resources.release", "resources", "release", "any", "content.register", "Release a resource handle owned by this mod."},
    {"resources.resolve", "resources", "resolve", "any", "game.read", "Resolve a logical resource id and report its source."},
    {"resources.override", "resources", "override", "content", "content.register", "Override a native asset path with a package resource."},
    {"resources.unoverride", "resources", "unoverride", "content", "content.register", "Remove a native asset override owned by this mod."},
    {"localization.register", "localization", "register", "content", "content.register", "Register mod-owned localized strings."},
    {"localization.get", "localization", "get", "any", "game.read", "Resolve a localized string with fallback rules."},
    {"commands.register", "commands", "register", "content", "content.register", "Register a namespaced developer or player command."},
    {"commands.execute", "commands", "execute", "ui", "game.write", "Execute an enabled command through its owning callback."},
    {"commands.unregister", "commands", "unregister", "content", "content.register", "Remove a command owned by this mod."},
    {"compat.list", "compat", "list", "any", "", "List API operation support for this game and build."},
    {"compat.supports", "compat", "supports", "any", "", "Query support for a versioned API operation."},
    {"debug.inspect", "debug", "inspect", "any", "debug.inspect", "Read a bounded snapshot of the current native scene context."},
    {"debug.profile", "debug", "profile", "any", "debug.inspect", "Read bounded per-mod resource and registration counters."},
    {"debug.overlay_register", "debug", "overlay_register", "render", "debug.inspect", "Register or update an owner-scoped one-line diagnostic overlay."},
    {"api.list", "api", "list", "any", "", "List all catalogued operations and capability status."},
    {"api.supports", "api", "supports", "any", "", "Query one operation capability by canonical path."},
    {"api.call", "api", "call", "any", "", "Invoke an operation by canonical path."},
};

constexpr EventSpec kEventCatalog[] = {
    {"runtime.started", "runtime", "both", "The MoonFactory runtime has started."},
    {"runtime.stopping", "runtime", "both", "The MoonFactory runtime is stopping."},
    {"runtime.loaded", "runtime", "both", "A mod package has loaded."},
    {"runtime.unloaded", "runtime", "both", "A mod package has unloaded."},
    {"settings.changed", "settings", "both", "A setting owned by this mod changed in the manager."},
    {"ui.action", "ui", "both", "A manager action owned by this mod was activated."},
    {"game.tick", "game", "both", "A simulation frame was pumped."},
    {"game.scene.will_enter", "scene", "both", "A scene is about to initialize."},
    {"game.scene.entered", "scene", "both", "A scene finished initialization."},
    {"game.dungeon.generating", "dungeons", "both", "Before a Chronicle procedural floor is generated; dungeon_id, floor_id, back_floor."},
    {"game.world.location_changed", "world", "both", "A world location changed; published at scene initialization when detectable and polled within scenes."},
    {"game.item_used", "items", "both", "A mod API item-use call completed."},
    {"game.player.thirst_changed", "player", "chronicle", "The active character's Chronicle water gauge changed, drained empty, or was restored."},
    {"game.photo.taken", "photos", "chronicle-two", "Max took a photo; reports its subject, map, and scoop or idea recognition state."},
    {"game.invention.created", "inventions", "chronicle-two", "A Chronicle Two invention was discovered in the menu or crafted through the API."},
    {"game.spheda.completed", "dungeons", "chronicle-two", "A Chronicle Two Spheda challenge was recorded as cleared."},
    {"game.item_given", "items", "both", "An item or weapon was added through a MoonFactory grant API."},
    {"game.item_picked_up", "items", "both", "A native dungeon pickup was added to the player's inventory."},
    {"game.item_removed", "items", "both", "An item was removed through the MoonFactory inventory API."},
    {"game.shop.purchased", "shops", "both", "A shop purchase or shop inventory change completed."},
    {"game.enemy_spawned", "characters", "both", "A registered mod-owned enemy spawned successfully."},
    {"game.enemy_defeated", "combat", "both", "The game accepted an enemy death."},
    {"game.enemy_hit", "combat", "chronicle", "A native weapon hit damaged an enemy; includes attacker, weapon, target, model, element, and final damage."},
    {"game.weapon.upgraded", "weapons", "both", "A native weapon upgrade changed the weapon."},
    {"game.weapon.equipped", "weapons", "both", "The active weapon changed through a supported API."},
    {"game.weapon.abs_gained", "weapons", "both", "An owned weapon gained ABS experience or synthesis points."},
    {"game.weapon.built_up", "weapons", "both", "A weapon evolved into a build-up target or synthesized into a sphere/spectol."},
    {"game.weapon.broken", "weapons", "both", "A weapon broke or was status-broken / spectrumized."},
    {"game.chest.opened", "dungeons", "both", "A dungeon treasure chest was opened or its loot table was updated."},
    {"game.floor.cleared", "dungeons", "both", "A dungeon floor was cleared or its restriction rules changed."},
    {"game.party.active_changed", "party", "both", "The active party member changed through a supported API."},
    {"game.party.recruited", "party", "chronicle-two", "A townsperson joined or replaced the current companion."},
    {"game.npc.talk", "characters", "both", "The player pressed talk at a town villager the mod registered (characters.register kind = 'npc') that has no native talk event."},
    {"game.story_flag_changed", "world", "both", "A native story flag changed through a supported API."},
    {"game.story_value_changed", "world", "both", "A native story counter changed through a supported API."},
    {"game.map_flag_changed", "world", "both", "A native map flag changed through a supported API."},
    {"game.georama.request_changed", "georama", "both", "A native Georama request changed through a supported API."},
    {"game.georama.element_changed", "georama", "chronicle", "A native Chronicle plot element changed."},
    {"game.georama.condition_changed", "georama", "chronicle-two", "A Chronicle Two analysis condition changed."},
    {"game.georama.request_known_changed", "georama", "chronicle-two", "A Chronicle Two Georama request became known or unknown."},
    {"game.georama.river_changed", "georama", "chronicle-two", "A Chronicle Two river cell changed."},
    {"game.georama.piece_placed", "georama", "both", "A Georama piece was placed."},
    {"game.georama.piece_removed", "georama", "both", "A Georama piece was removed."},
    {"quests.started", "quests", "both", "A registered quest became active."},
    {"quests.objective_advanced", "quests", "both", "A quest objective advanced."},
    {"quests.completed", "quests", "both", "All objectives in a quest are complete."},
    {"skills.train_requested", "skills", "both", "The player requested training a skill in the owning package tree."},
    {"skills.rank_changed", "skills", "both", "A skill rank changed."},
    {"skills.unlocked", "skills", "both", "A skill's rank first became positive."},
};

constexpr std::array<std::string_view, 8> kKnownPermissions = {
    "storage.read", "storage.write", "game.read", "game.write", "content.register", "scripts.override",
    "debug.inspect", "native.extension",
};

} // namespace

std::span<const ApiOperationSpec> ApiCatalog() { return kApiCatalog; }
std::span<const EventSpec> EventCatalog() { return kEventCatalog; }

const ApiOperationSpec *FindApiOperation(std::string_view path) {
    const auto found = std::ranges::find(kApiCatalog, path, &ApiOperationSpec::path);
    return found == std::end(kApiCatalog) ? nullptr : &*found;
}

bool IsCoreApiOperation(std::string_view path) {
    return path == "info.get" || path == "compat.list" || path == "compat.supports" ||
           path == "api.list" || path == "api.supports" || path == "api.call" ||
           path == "events.list" || path == "events.subscribe" || path == "events.unsubscribe" ||
           path == "save.get" || path == "save.set" ||
           path == "save.migrate" ||
           path == "quests.get" || path == "quests.register" || path == "quests.start" ||
           path == "quests.advance" ||
           path == "localization.register" || path == "localization.get" || path == "resources.load" ||
           path == "resources.resolve" || path == "resources.release" || path == "resources.override" ||
           path == "resources.unoverride" || path == "ui.register" ||
           path == "ui.open_skills" || path == "ui.remove" || path == "ui.notify" || path == "commands.register" ||
           path == "commands.execute" || path == "commands.unregister" ||
           path == "scripts.register_function" || path == "scripts.list_functions" || path == "scripts.call_function" ||
           path == "scripts.unregister_function" || path == "scripts.list_hooks" || path == "scripts.call" ||
           path == "scripts.override" ||
           path == "skills.list" || path == "skills.get" || path == "skills.register" ||
           path == "skills.unlock" || path == "skills.set_rank" ||
           path == "debug.profile";
}

namespace {

std::string UnsupportedReason(std::string_view game_id, const ApiOperationSpec &operation) {
    const std::string game = game_id == "chronicle-two" ? "Chronicle Two" : "Chronicle";
    if (operation.path == "weapons.definition_set" && game_id == "chronicle-two") {
        return "Chronicle Two does not expose mutable native base weapon definitions; this owner-scoped override is currently bound only in Chronicle.";
    }
    if (operation.path == "shops.set_items" && game_id == "chronicle-two") {
        return "Chronicle Two shop catalogs are not exposed as a mutable native list; this owner-restored shop replacement is currently bound only in Chronicle.";
    }
    if (operation.path == "save.slot_id" && game_id == "chronicle-two") {
        return "Chronicle Two does not yet expose a verified native save-slot identifier.";
    }
    if (operation.domain == "photos") {
        return "Chronicle has no photography, idea, or scoop system; this domain is bound only in Chronicle Two.";
    }
    if (operation.domain == "inventions") {
        return "Chronicle has no photo-invention system; this domain is bound only in Chronicle Two.";
    }
    if (operation.domain == "player") {
        if (game_id == "chronicle") {
            if (operation.operation == "ridepod_get" || operation.operation == "ridepod_set") {
                return "Chronicle has no ridepod; this operation is bound only in Chronicle Two.";
            }
            if (operation.operation == "transform_get" || operation.operation == "transform_set") {
                return "Chronicle has no monster-badge transformation system; this operation is bound only in Chronicle Two.";
            }
        } else if (operation.operation == "thirst_get" || operation.operation == "thirst_set") {
            return "Chronicle Two has no per-character water gauge; its thirst ailment is exposed through player.status_add/remove.";
        }
    }
    if (operation.domain == "locations") {
        return game + " does not yet load mod-authored maps or their transition metadata.";
    }
    if (operation.domain == "dungeons") {
        if (game_id == "chronicle-two") {
            if (operation.operation == "back_floor_get" || operation.operation == "back_floor_enter") {
                return "Chronicle Two has no Dark Cloud back-floor state; back-floor entry is bound only in Chronicle.";
            }
            return "Chronicle Two can append floors to existing dungeons, but does not yet register complete new dungeon graphs.";
        }
        if (operation.operation == "spheda_get" || operation.operation == "spheda_spawn") {
            return "Chronicle has no Spheda minigame; Spheda operations are bound only in Chronicle Two.";
        }
        return "Chronicle can append procedural floors to native dungeons 0-4, but does not yet register standalone maps, custom geometry, or authored room assets.";
    }
    if (operation.domain == "georama") {
        if (game_id == "chronicle-two") {
            if (operation.path == "georama.atla_list" || operation.path == "georama.atla_collect") {
                return "Chronicle Two has no Atla spheres; Atla collection is bound only in Chronicle.";
            }
            if (operation.path == "georama.satisfaction_get") {
                return "Chronicle Two Georama progression is exposed through its native request, condition, and river APIs; the Chronicle satisfaction view is not bound.";
            }
            if (operation.path == "georama.element_set") {
                return "Chronicle Two's Georama pieces do not use Chronicle's per-plot optional-element table.";
            }
            if (operation.operation == "request_set") {
                return "Chronicle Two can reveal existing requests and change analysis conditions, but request completion rewards and linked native progression are not bound yet.";
            }
            return "This Chronicle Two Georama operation has no verified native binding yet.";
        }
        return "Chronicle does not expose separate saved analysis-condition or river-grid state; its Georama APIs operate on native plot elements, requests, and placed pieces.";
    }
    if (operation.domain == "entities") {
        if (game_id == "chronicle-two") {
            return "Chronicle Two supports live dungeon-monster handles and can despawn enemies created by characters.spawn; NPC and world-object spawning are not bound yet.";
        }
        return "Chronicle supports generation-checked enemy handles and owner-only despawn for mod-spawned non-mimics; NPC and world-object spawning are not bound yet.";
    }
    if (operation.domain == "combat") {
        if (game_id == "chronicle-two") {
            if (operation.path.starts_with("combat.styles_")) {
                return "Fighting-style work is deferred while authored model rigs and native move-animation bindings remain out of scope.";
            }
            if (operation.path == "combat.register_formula") {
                return "This Chronicle Two build has no combat-formula registry or native damage hook.";
            }
            return "This Chronicle Two combat operation has no verified native binding.";
        }
        if (operation.path == "combat.register_formula") {
            return "This Chronicle build has no combat-formula registry or native damage hook.";
        }
        return "Fighting-style work is deferred while authored model rigs and native move-animation bindings remain out of scope.";
    }
    if (operation.domain == "characters") {
        if (game_id == "chronicle-two" && operation.operation == "spawn") {
            return "Chronicle Two can spawn registered enemies only while an active dungeon manager and inherited game assets are available.";
        }
        if (game_id == "chronicle-two" && operation.operation == "register") {
            return "Chronicle Two can register data-defined enemy variants that inherit a native model and AI script, and town villagers (kind = 'npc').";
        }
        return "Playable-character records are readable, but mod-defined characters and spawn routines are not bound yet.";
    }
    if (operation.domain == "models") {
        return "The port has no package-backed native model loader binding for this operation yet.";
    }
    if (operation.domain == "animations") {
        return "The port has no validated native rig and animation binding for this operation yet.";
    }
    if (operation.domain == "scripts") {
        return "The active scene event-program hook is supported; this operation does not target any other native script VM.";
    }
    if (operation.domain == "content") {
        return "Typed item and weapon paths exist, but the generic content registry is not bound to native game tables yet.";
    }
    if (operation.domain == "render") {
        return "Bounded follow-camera orbit changes are available; owned render callbacks and draw layers are not bound.";
    }
    if (operation.domain == "physics") {
        if (game_id == "chronicle-two") {
            return "Chronicle Two exposes live-monster velocity reads and writes plus bounded sphere queries over live monster actor origins; native collider and world-geometry tests are not exposed.";
        }
        if (operation.path == "physics.query") {
            return "Chronicle can query live monster actor origins inside a bounded sphere; native collider and world-geometry tests are not exposed.";
        }
        if (operation.path == "physics.velocity_set") {
            return "Chronicle writes native enemy movement intent; the next AI or movement step may replace it.";
        }
        return "Chronicle exposes live enemy movement intent through physics.velocity_get; collision geometry queries are not exposed.";
    }
    if (operation.domain == "weapons" && operation.operation == "upgrade") {
        return game + " weapon upgrade progression is not yet bound to the mod API.";
    }
    if (operation.path == "weapons.synthesize" && game_id == "chronicle-two") {
        return "Chronicle Two uses weapons.spectrumize to spectrumize weapons and items into synthesized attachments.";
    }
    if (operation.path == "weapons.spectrumize" && game_id == "chronicle") {
        return "Chronicle uses weapons.synthesize to status-break level 5+ weapons into SynthSphere attachments.";
    }
    if (operation.path == "weapons.attachment_remove" && game_id == "chronicle-two") {
        return "Chronicle Two synthesizes attachments permanently into weapons and does not have removable attachment slots.";
    }
    if (operation.domain == "party" && game_id == "chronicle") {
        if (operation.path == "party.recruit") {
            return "Chronicle has a story-ordered playable cast but no separate NPC companion recruitment routine.";
        }
        return "Chronicle native party switching is available only during safe active-dungeon play.";
    }
    return game + " has no tested native binding for " + std::string(operation.path) + " yet.";
}

} // namespace

void CompleteApiCapabilityLedger(std::string_view game_id, std::vector<ApiCapability> &capabilities) {
    for (const ApiOperationSpec &operation : ApiCatalog()) {
        if (IsCoreApiOperation(operation.path)) continue;
        const auto found = std::ranges::find(capabilities, operation.path, &ApiCapability::path);
        if (found != capabilities.end()) continue;
        capabilities.push_back({std::string(operation.path), false, UnsupportedReason(game_id, operation)});
    }
}

std::string_view ApiCallStatusName(ApiCallStatus status) {
    switch (status) {
        case ApiCallStatus::Ok: return "ok";
        case ApiCallStatus::Unsupported: return "unsupported";
        case ApiCallStatus::InvalidArgument: return "invalid_argument";
        case ApiCallStatus::PermissionDenied: return "permission_denied";
        case ApiCallStatus::PhaseViolation: return "phase_violation";
        case ApiCallStatus::Failed: return "failed";
    }
    return "failed";
}

bool IsKnownPermission(std::string_view permission) {
    return std::ranges::find(kKnownPermissions, permission) != kKnownPermissions.end();
}

} // namespace moonmod
