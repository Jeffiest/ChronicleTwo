#pragma once

// event_func.cpp defines its own VectMatMul; the retail table renames this unit's copy apart
// so the PS2 link accepts both. The port's merge needs the same.
#define VectMatMul EventEdit_VectMatMul
