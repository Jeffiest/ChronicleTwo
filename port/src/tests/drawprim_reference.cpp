#include "common.h"

#include "mg_drawprim.hpp"
#include "mglib.hpp"

// This fixture is linked only by the isolated GS-packet reference test target.
int mgScreenOffx = 0;
int mgScreenOffy = 0;

// The test methods do not inspect these unrelated subobjects.
mgCDrawEnv::mgCDrawEnv() {}
mgCTexture::mgCTexture() {}

// Keep the retail defaults used by the four methods under test.
mgCDrawPrim::mgCDrawPrim() {
    memory = nullptr;
    vif_packet = nullptr;
    draw_manager = nullptr;
    disabled = 1;
    q = 1.0f;
    *reinterpret_cast<u_long *>(&prim) = MG_GS_PRIM_FST;
    bilinear = 1;
    z_mask = MG_Z_MASK_WRITE;
    coord = 0;
    offset_x = 0;
    offset_y = 0;
}

void mgCDrawPrim::GetOffset(int *x, int *y) {
    *x = coord == 0 ? mgScreenOffx << 4 : 0;
    *y = coord == 0 ? mgScreenOffy << 4 : 0;
    *x += offset_x;
    *y += offset_y;
}

void mgCDrawPrim::Vertex4(int x, int y, int z) {
    int offset_x = 0;
    int offset_y = 0;
    GetOffset(&offset_x, &offset_y);
    u_long *packet = command_write;
    packet[0] = ((long long)z << 32) | ((long long)(x + offset_x) | ((long long)(y + offset_y) << 16));
    packet[1] = 5;
    command_write += 2;
}

void mgCDrawPrim::Color(int r, int g, int b, int a) {
    u_long *packet = command_write;
    u_int q = q_bits;
    packet[0] = ((u_long)q << 32) | ((long long)r | ((long long)g << 8) | ((long long)b << 16) |
                 ((long long)a << 24));
    packet[1] = 1;
    command_write += 2;
}