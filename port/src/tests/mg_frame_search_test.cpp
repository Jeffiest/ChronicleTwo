#include <gtest/gtest.h>

#include "mg_frame.hpp"

TEST(MgFrameSearch, KeepsChildBeforeSiblingOrder) {
    char root_name[] = "root";
    char first_name[] = "first";
    char target[] = "target";

    mgCFrame root;
    mgCFrame first;
    mgCFrame child;
    mgCFrame sibling;
    root.name = root_name;
    root.frame_num = 3;
    root.child = &first;
    first.parent = &root;
    first.name = first_name;
    first.child = &child;
    child.parent = &first;
    child.name = target;
    first.brother = &sibling;
    sibling.parent = &root;
    sibling.name = target;

    EXPECT_EQ(root.SearchFrame(target), &child);
}

TEST(MgFrameSearch, StopsWhenAFrameTreeContainsACycle) {
    char root_name[] = "root";
    char first_name[] = "first";
    char second_name[] = "second";
    char absent[] = "absent";

    mgCFrame root;
    mgCFrame first;
    mgCFrame second;
    root.name = root_name;
    root.frame_num = 2;
    root.child = &first;
    first.parent = &root;
    first.name = first_name;
    first.child = &second;
    second.parent = &first;
    second.name = second_name;
    second.brother = &first;

    EXPECT_EQ(root.SearchFrame(absent), nullptr);
}

TEST(MgFrameSearch, StopsOnTheNineFrameOpeningEventCycle) {
    char     absent[] = "absent";
    char     names[9][8] = {"root", "frame1", "frame2", "frame3", "frame4", "frame5", "frame6", "frame7", "frame8"};
    mgCFrame frames[9];

    for (int i = 0; i < 9; ++i) {
        frames[i].name = names[i];
        frames[i].child = nullptr;
        frames[i].brother = nullptr;
        frames[i].frame_num = 9;
    }

    frames[0].child = &frames[1];
    frames[1].child = &frames[2];
    frames[2].child = &frames[3];
    frames[3].child = &frames[4];
    frames[4].child = &frames[5];
    frames[5].child = &frames[6];
    frames[6].child = &frames[7];
    frames[7].child = &frames[8];
    frames[8].brother = &frames[0];

    EXPECT_EQ(frames[0].SearchFrame(absent), nullptr);
}

TEST(MgFrameSearch, CycleDoesNotHideLaterSibling) {
    char root_name[] = "root";
    char first_name[] = "first";
    char cycle_name[] = "cycle";
    char target_name[] = "target";

    mgCFrame root;
    mgCFrame first;
    mgCFrame cycle;
    mgCFrame target;
    root.name = root_name;
    root.frame_num = 3;
    root.child = &first;
    first.name = first_name;
    first.brother = &target;
    first.child = &cycle;
    cycle.name = cycle_name;
    cycle.brother = &cycle;
    target.name = target_name;

    EXPECT_EQ(root.SearchFrame(target_name), &target);
}
