#pragma once

// Mod map identities appended after the native map catalog; mapselect.hpp includes this in the port build.

/**
 * Returns the number of native map records loaded from map/map*.cfg.
 *
 * This count is used by PC-port APIs that enumerate the native map catalog.
 */
int GetMapNameCount();

/**
 * Appends a bounded MoonMod-authored map identity to the runtime catalog.
 * The initial supported authored-map type is a standalone native interior map.
 * Returns the runtime map index, or -1 when the name conflicts or capacity is reached.
 */
int RegisterMoonModMapName(const char *name, const char *title);

/** Returns whether map_no refers to an appended MoonMod map identity. */
int IsMoonModMapName(int map_no);

/** Clears appended map identities between isolated native unit tests. */
void ResetMoonModMapNamesForTests();

/** Gives the record of appended map index (0 = first appended map), or NULL. */
MAP_NAME_INFO *GetMoonModMapNameInfo(int index);
