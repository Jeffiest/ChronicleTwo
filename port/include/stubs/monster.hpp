#pragma once

// The monster slot limits of the current floor (port/src/monster.cpp).
int PortMonsterLatchLimits();
int PortMonsterActiveLimit();

namespace moonmod {
float ApplyChronicleTwoCombatDamageFormula(float damage);
}
