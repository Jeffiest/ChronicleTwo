#pragma once

/**
 * @file
 * MoonMod Georama parts for Chronicle Two beyond the data-only template copies the adapter appends: a persistent
 * definition id (content_slots.hpp) so a saved town keeps naming the same part across launches and mod changes,
 * the town the part belongs to, its built stock, its model (a part of the town's loaded scene) and its territory.
 */

#include <string>
#include <string_view>

#include "moonmod/api.hpp"

class CEditInfoMngr;
class CEditMap;
class CEditPartsInfo;
class mgCMemory;

namespace moonmod {

/** Highest definition id a mod part takes; they are handed out downward from here (the menus list ids below 0x80). */
inline constexpr int kModGeoramaPartIdTop = 0x7F;
/** Lowest definition id a mod part takes. */
inline constexpr int kModGeoramaPartIdBottom = 0x40;

/**
 * Reads the extra keys of a `georama.register` definition for `qualified_id`: `town_id` (0-4; omitted means every
 * town), `stock` (1-999 built parts given to the save once), `model = {part = <scene part name>}`,
 * `territory = {radius, height}` (world units). Returns false with `error` on a bad value.
 */
bool ChronicleTwoGeoramaParseExtras(std::string_view mod_id, std::string_view qualified_id, const Json &definition,
                                    std::string &error);

/** Number of keys in `definition` that ChronicleTwoGeoramaParseExtras consumes. */
std::size_t ChronicleTwoGeoramaExtraKeyCount(const Json &definition);

/** Forgets the extras of a piece that was never materialized. */
void ChronicleTwoGeoramaForgetExtras(std::string_view qualified_id);

/** Whether the piece materializes in `town_id` (its own town, or every town when it named none). */
bool ChronicleTwoGeoramaPieceAppliesToTown(std::string_view qualified_id, int town_id);

/**
 * The piece's persistent definition id, allocated on first use from kModGeoramaPartIdTop downward among ids no
 * definition of `manager` holds; -1 with `error` when the registry fails or the persisted id is taken by a native
 * part of this town.
 */
int ChronicleTwoGeoramaPersistentPartId(std::string_view qualified_id, CEditInfoMngr &manager, std::string &error);

/**
 * Applies the extras to the copied definition of a materialized piece: binds `model.part` from the map's loaded
 * parts (the template's model stays when the part is absent), sets the territory, and gives the save the piece's
 * `stock` while the save holds none of it built or placed in any town (so a save that has had the piece never gets it twice).
 */
void ChronicleTwoGeoramaApplyExtras(std::string_view qualified_id, CEditPartsInfo &copy, CEditMap &map,
                                    mgCMemory &memory, int town_id);

/** The extras of a piece for the API's JSON: `{town_id, stock, model, territory, definition_id}` where set. */
Json ChronicleTwoGeoramaExtrasJson(std::string_view qualified_id);

void ResetChronicleTwoContentGeoramaForTests();

} // namespace moonmod
