#pragma once

/**
 * @file
 * MoonMod content slots for Chronicle Two: the stable mod id of an appended item, weapon or Georama part mapped
 * to the number the game knows it by, kept beside the saves so a save written with a mod's numbers still names
 * the same content after another mod is added or removed.
 *
 * Files: `<save root>/moonmod/game-content/chronicle-two-{item,weapon,georama}-slots.json`, each
 * `{"format_version": 1, "<kind>": {"<mod:id>": number, ...}}`. An assignment is never removed (a tombstone keeps
 * its number reserved), as Chronicle's slot files do.
 */

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <string_view>

namespace moonmod {

enum class ContentSlotKind { Item, Weapon, Georama };

/** Game-path-relative name of the registry file of a kind. */
std::filesystem::path ChronicleTwoContentSlotFile(ContentSlotKind kind, const std::filesystem::path &save_root);

/** The directory the registries live in: `PathsSaveRoot()` unless a test overrode it with the setter below. */
std::filesystem::path ChronicleTwoContentSaveRoot();

/** Points the registries at `root` for a test; an empty path restores `PathsSaveRoot()`. */
void SetChronicleTwoContentSaveRootForTests(const std::filesystem::path &root);

/** Reads a kind's assignments (empty when the file does not exist). Returns false and sets `error` when it is unreadable. */
bool LoadChronicleTwoContentSlots(ContentSlotKind kind, std::map<std::string, int> &assignments, std::string &error);

/**
 * The number assigned to `stable_id`, allocating one when it has none: the first candidate `is_free` accepts that
 * no assignment of `kind` (nor, for items and weapons, of the other item-number kind) already holds, scanning
 * `first` toward `last` inclusive. Writes the registry before returning a new number.
 */
bool AllocateChronicleTwoContentSlot(ContentSlotKind kind, std::string_view stable_id, int first, int last,
                                     const std::function<bool(int)> &is_free, int &number, std::string &error);

} // namespace moonmod
