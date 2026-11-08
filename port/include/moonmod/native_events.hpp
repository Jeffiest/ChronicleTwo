#pragma once

namespace moonmod {

void ChronicleTwoNativeItemPickedUp(int item_no, int count);
void ChronicleTwoPhotoTaken(int neta_id, int map_no, int npc_no, int monster_no, bool used);
void ChronicleTwoInventionCreated(int item_id, int neta0, int neta1, int neta2);
void ChronicleTwoSphedaCompleted(int dungeon_id, int floor_id, int clear_value);

} // namespace moonmod
