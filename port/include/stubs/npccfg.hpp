#pragma once

#include <npccfg.hpp>

/** Native party-character loading fills the party-character table. */
extern int           NpcBaseDataTotalNum asm("Npccfg_NpcBaseDataTotalNum");
extern NPC_BASE_DATA NpcBaseData[NPC_BASE_DATA_MAX] asm("Npccfg_NpcBaseData");
