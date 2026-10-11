#include "common.h"

#include "mg_frame.hpp"
#include "stubs/mg_frame.hpp"

void test1(float (*corners)[4], float (*screen)[4], float (*matrix)[4], float *out_max, float *out_min) {
    PS2_UNIMPLEMENTED();
}

void test2(float *out_max, float *out_min) {
    PS2_UNIMPLEMENTED();
}

int mgInsideScreen(float (*corners)[4], float (*matrix)[4], float *out_max, float *out_min) {
    PS2_UNIMPLEMENTED();
}

void mgCFrame::GetLocalMatrix(float (*matrix)[4]) {
    PS2_UNIMPLEMENTED();
}

void mgCFrame::GetBBoardMatrix(int mode, float (*matrix)[4], mgRENDER_INFO *info) {
    PS2_UNIMPLEMENTED();
}

void mgCFrame::GetLWMatrix(float (*matrix)[4]) {
    PS2_UNIMPLEMENTED();
}

void mgCFrame::GetLWMatrixTopBottom(float (*matrix)[4]) {
    PS2_UNIMPLEMENTED();
}

int mgCFrame::GetDrawRect(mgVu0FBOX *rect, mgCDrawManager *manager) {
    PS2_UNIMPLEMENTED();
}
