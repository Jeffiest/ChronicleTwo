#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <limits>

#include "mg_drawprim.hpp"
#include "mglib.hpp"

namespace {

void ReadLanes(const u_long128 *quad, int *lanes) {
    memcpy(lanes, quad, sizeof(int) * 4);
}

} // namespace

TEST(MgDrawPrimAsm, Data0TruncatesEachLaneTowardZero) {
    mgCDrawPrim prim;
    alignas(16) u_long128 buffer[2] = {};
    prim.write = buffer;

    float data[4] = {1.5f, -2.75f, 100.9f, 0.0f};
    prim.Data0(data);

    int lanes[4];
    ReadLanes(buffer, lanes);
    EXPECT_EQ(lanes[0], 1);
    EXPECT_EQ(lanes[1], -2);
    EXPECT_EQ(lanes[2], 100);
    EXPECT_EQ(lanes[3], 0);
}

TEST(MgDrawPrimAsm, Data0AdvancesWriteByOneQuadword) {
    mgCDrawPrim prim;
    alignas(16) u_long128 buffer[2] = {};
    prim.write = buffer;

    float data[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    prim.Data0(data);

    EXPECT_EQ(prim.write, buffer + 1);
}

TEST(MgDrawPrimAsm, Data0SaturatesPerSign) {
    mgCDrawPrim prim;
    alignas(16) u_long128 buffer[1] = {};
    prim.write = buffer;

    float data[4] = {3.0e10f, -3.0e10f, 2147483520.0f, 2147483648.0f};
    prim.Data0(data);

    int lanes[4];
    ReadLanes(buffer, lanes);
    EXPECT_EQ(lanes[0], std::numeric_limits<int>::max());
    EXPECT_EQ(lanes[1], std::numeric_limits<int>::min());
    EXPECT_EQ(lanes[2], 2147483520);
    EXPECT_EQ(lanes[3], std::numeric_limits<int>::max());
}

TEST(MgDrawPrimAsm, Data0ZeroAndNegativeZeroGiveZero) {
    mgCDrawPrim prim;
    alignas(16) u_long128 buffer[1] = {};
    prim.write = buffer;

    float data[4] = {0.0f, -0.0f, -0.0f, 0.0f};
    prim.Data0(data);

    int lanes[4];
    ReadLanes(buffer, lanes);
    EXPECT_EQ(lanes[0], 0);
    EXPECT_EQ(lanes[1], 0);
    EXPECT_EQ(lanes[2], 0);
    EXPECT_EQ(lanes[3], 0);
}

TEST(MgDrawPrimAsm, Data4ScalesBy16BeforeTruncating) {
    mgCDrawPrim prim;
    alignas(16) u_long128 buffer[1] = {};
    prim.write = buffer;

    float data[4] = {1.5f, -2.75f, 0.0625f, 3.9f};
    prim.Data4(data);

    int lanes[4];
    ReadLanes(buffer, lanes);
    EXPECT_EQ(lanes[0], 24);
    EXPECT_EQ(lanes[1], -44);
    EXPECT_EQ(lanes[2], 1);
    EXPECT_EQ(lanes[3], 62);
}

TEST(MgDrawPrimAsm, Data4SaturatesPerSignAfterScaling) {
    mgCDrawPrim prim;
    alignas(16) u_long128 buffer[1] = {};
    prim.write = buffer;

    float data[4] = {134217728.0f, -134217728.0f, 0.0f, 0.0f};
    prim.Data4(data);

    int lanes[4];
    ReadLanes(buffer, lanes);
    EXPECT_EQ(lanes[0], std::numeric_limits<int>::max());
    EXPECT_EQ(lanes[1], std::numeric_limits<int>::min());
    EXPECT_EQ(lanes[2], 0);
    EXPECT_EQ(lanes[3], 0);
}

TEST(MgDrawPrimAsm, Data4ZeroAndNegativeZeroGiveZero) {
    mgCDrawPrim prim;
    alignas(16) u_long128 buffer[1] = {};
    prim.write = buffer;

    float data[4] = {-0.0f, 0.0f, -0.0f, 0.0f};
    prim.Data4(data);

    int lanes[4];
    ReadLanes(buffer, lanes);
    EXPECT_EQ(lanes[0], 0);
    EXPECT_EQ(lanes[1], 0);
    EXPECT_EQ(lanes[2], 0);
    EXPECT_EQ(lanes[3], 0);
}

TEST(MgDrawPrimAsm, VertexPacksXYAndZIntoOneWord) {
    mgCDrawPrim prim;
    prim.coord = 1;
    prim.offset_x = 0;
    prim.offset_y = 0;
    alignas(8) u_long buffer[2] = {};
    prim.command_write = buffer;

    float pos[4] = {10.25f, 3.5f, 7.75f, 1.0f};
    prim.Vertex(pos);

    EXPECT_EQ(buffer[0], 0x00000007003800A4ULL);
    EXPECT_EQ(buffer[1], 5ULL);
    EXPECT_EQ(prim.command_write, buffer + 2);
}

TEST(MgDrawPrimAsm, VertexAddsScreenOffsetInSixteenths) {
    mgCDrawPrim prim;
    prim.coord = 1;
    prim.offset_x = 8;
    prim.offset_y = 16;
    alignas(8) u_long buffer[2] = {};
    prim.command_write = buffer;

    float pos[4] = {10.25f, 3.5f, 7.75f, 1.0f};
    prim.Vertex(pos);

    EXPECT_EQ(buffer[0], 0x00000007004800ACULL);
    EXPECT_EQ(buffer[1], 5ULL);
}

TEST(MgDrawPrimAsm, VertexZeroPositionPacksOnlyTheBuilderOffset) {
    mgCDrawPrim prim;
    prim.coord = 1;
    prim.offset_x = 8;
    prim.offset_y = 16;
    alignas(8) u_long buffer[2] = {};
    prim.command_write = buffer;

    float pos[4] = {-0.0f, -0.0f, -0.0f, 1.0f};
    prim.Vertex(pos);

    EXPECT_EQ(buffer[0], 0x0000000000100008ULL);
    EXPECT_EQ(buffer[1], 5ULL);
}

TEST(MgDrawPrimAsm, VertexNegativeZSignExtendsIntoHighWord) {
    mgCDrawPrim prim;
    prim.coord = 1;
    prim.offset_x = 0;
    prim.offset_y = 0;
    alignas(8) u_long buffer[2] = {};
    prim.command_write = buffer;

    float pos[4] = {10.25f, 3.5f, -3.5f, 1.0f};
    prim.Vertex(pos);

    EXPECT_EQ(buffer[0], 0xFFFFFFFD003800A4ULL);
    EXPECT_EQ(buffer[1], 5ULL);
}

TEST(MgDrawPrimAsm, VertexScreenOffsetPathAddsScreenThenBuilderOffset) {
    const int saved_x = mgScreenOffx;
    const int saved_y = mgScreenOffy;
    mgScreenOffx = 3;
    mgScreenOffy = 5;

    mgCDrawPrim prim;
    prim.coord = 0;
    prim.offset_x = 8;
    prim.offset_y = 16;
    alignas(8) u_long buffer[2] = {};
    prim.command_write = buffer;

    float pos[4] = {10.25f, 3.5f, 7.75f, 1.0f};
    prim.Vertex(pos);

    mgScreenOffx = saved_x;
    mgScreenOffy = saved_y;

    EXPECT_EQ(buffer[0], 0x00000007009800DCULL);
    EXPECT_EQ(buffer[1], 5ULL);
}

TEST(MgDrawPrimAsm, ColorPacksBytesWithQBits) {
    mgCDrawPrim prim;
    alignas(8) u_long buffer[2] = {};
    prim.command_write = buffer;

    float color[4] = {128.0f, 64.5f, 255.0f, 0.0f};
    prim.Color(color);

    EXPECT_EQ(buffer[0], 0x3F80000000FF4080ULL);
    EXPECT_EQ(buffer[1], 1ULL);
    EXPECT_EQ(prim.command_write, buffer + 2);
}

TEST(MgDrawPrimAsm, ColorZeroAndNegativeZeroGiveZeroComponents) {
    mgCDrawPrim prim;
    alignas(8) u_long buffer[2] = {};
    prim.command_write = buffer;

    float color[4] = {-0.0f, 0.0f, -0.0f, 0.0f};
    prim.Color(color);

    EXPECT_EQ(buffer[0] & 0xFFFFFFFFULL, 0ULL);
    EXPECT_EQ(buffer[1], 1ULL);
}
