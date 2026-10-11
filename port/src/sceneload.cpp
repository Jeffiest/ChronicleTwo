#include "common.h"

#include "scene.hpp"
#include "scenesnd.hpp"
#include "sceneload.hpp"

// Step timing reads the PS2 timer 0 at a fixed address; the host has no such register.
PORT_OVERRIDE int CScene::LoadMapFromMemory(int map_no, int step, SCN_LOADMAP_INFO2 *info) {
    PS2_UNIMPLEMENTED();
}
