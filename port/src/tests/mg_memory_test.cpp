#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>

#include "mg_memory.hpp"
#include "mg_memory_port.hpp"
#include "platform/memory.hpp"
#include "platform_fixture.hpp"

namespace {

constexpr int kQuadwords = 4096;
constexpr std::size_t kBytes = static_cast<std::size_t>(kQuadwords) * 16;
constexpr int kHeader = static_cast<int>((sizeof(mgMEMORY_BLOCK) + 15) / 16);

alignas(64) u_long128 ordinary_storage[kQuadwords];

enum class Source { Ordinary, AboveFourGiB };

std::uintptr_t AddressOf(const void *pointer) {
    return reinterpret_cast<std::uintptr_t>(pointer);
}

} // namespace

class MgMemory : public testing::TestWithParam<Source> {
protected:
    void SetUp() override {
        if (GetParam() == Source::Ordinary) {
            std::memset(ordinary_storage, 0, sizeof ordinary_storage);
            buffer_ = ordinary_storage;
            return;
        }

        arena_ = ArenaMemoryMap(kBytes);
        if (!IsAbove4GiB(arena_.base)) {
            GTEST_SKIP() << "the platform maps no memory above 4 GiB";
        }
        buffer_ = reinterpret_cast<u_long128 *>(arena_.base);
    }

    void TearDown() override {
        ArenaMemoryUnmap(arena_);
    }

    bool Inside(const void *pointer, int quadwords) const {
        const std::uintptr_t start = AddressOf(buffer_);
        const std::uintptr_t at = AddressOf(pointer);
        return at >= start && at + static_cast<std::uintptr_t>(quadwords) * 16 <= start + kBytes;
    }

    u_long128 *buffer_ = nullptr;
    ArenaMemory arena_{};
};

TEST_P(MgMemory, StackAllocationStopsAtRetailCapacity) {
    mgCMemory memory;
    memory.stSetBuffer(buffer_, 40);

    // Retail refuses a request whose total would reach the capacity, so 39 of 40 quadwords are usable.
    for (int i = 0; i < 39; ++i) {
        ASSERT_NE(memory.Alloc(1), nullptr) << i;
    }

    EXPECT_EQ(memory.stGetUsed(), 39);
    EXPECT_EQ(memory.stGetRest(), 1);
    EXPECT_EQ(memory.Alloc(1), nullptr);
    EXPECT_EQ(memory.stAlloc(1), nullptr);
    EXPECT_EQ(memory.Alloc(0), nullptr);
    EXPECT_EQ(memory.Alloc(-1), nullptr);
    EXPECT_EQ(memory.stGetUsed(), 39);
}

TEST_P(MgMemory, StackAllocationsAreContiguousAndInside) {
    mgCMemory memory;
    memory.stSetBuffer(buffer_, 100);

    u_long128 *first = memory.Alloc(3);
    u_long128 *second = memory.stAlloc(5);

    EXPECT_EQ(first, buffer_);
    EXPECT_EQ(second, buffer_ + 3);
    EXPECT_EQ(memory.stGetTop(), buffer_ + 8);
    EXPECT_TRUE(Inside(first, 3));
    EXPECT_TRUE(Inside(second, 5));
}

TEST_P(MgMemory, Stack64AlignmentAdvancesToTheBoundary) {
    mgCMemory memory;
    memory.stSetBuffer(buffer_ + 1, 100);

    memory.Align64();
    EXPECT_EQ(memory.stGetUsed(), 3);
    EXPECT_EQ(AddressOf(memory.stGetTop()) & 63, 0u);

    memory.Align64();
    EXPECT_EQ(memory.stGetUsed(), 3);

    u_long128 *aligned = memory.stAlloc64(2);
    EXPECT_EQ(aligned, buffer_ + 4);
    EXPECT_EQ(AddressOf(aligned) & 63, 0u);
    EXPECT_EQ(memory.stGetUsed(), 5);
}

TEST_P(MgMemory, Stack64AlignmentClampsAtTheEnd) {
    mgCMemory memory;
    memory.stSetBuffer(buffer_ + 1, 2);

    memory.stAlign64();
    EXPECT_EQ(memory.stGetUsed(), 2);
    EXPECT_EQ(memory.stGetRest(), 0);
}

TEST_P(MgMemory, StackLockRefusesAllocationAndAlignment) {
    mgCMemory memory;
    memory.stSetBuffer(buffer_ + 1, 100);
    memory.lock = 1;

    EXPECT_EQ(memory.Alloc(1), nullptr);
    EXPECT_EQ(memory.stAlloc64(1), nullptr);
    memory.stAlign64();
    EXPECT_EQ(memory.stGetUsed(), 0);

    memory.stReset();
    EXPECT_EQ(memory.lock, 0);
    EXPECT_NE(memory.Alloc(1), nullptr);
}

TEST_P(MgMemory, StackTestDoesNotClaim) {
    mgCMemory memory;
    memory.stSetBuffer(buffer_, 10);

    EXPECT_EQ(memory.stAllocTest(9), buffer_);
    EXPECT_EQ(memory.stAllocTest(10), nullptr);
    EXPECT_EQ(memory.stGetUsed(), 0);
}

TEST_P(MgMemory, StackPushPopAndReset) {
    mgCMemory memory;
    memory.stSetBuffer(buffer_, 100);

    memory.Alloc(10);
    const int mark = memory.stGetUsed();
    u_long128 *scratch = memory.Alloc(20);
    EXPECT_EQ(scratch, buffer_ + 10);

    // Retail has no pop; the mark is restored through the public used count.
    memory.stack_used = mark;
    EXPECT_EQ(memory.Alloc(20), scratch);

    memory.stReset();
    EXPECT_EQ(memory.stGetUsed(), 0);
    EXPECT_EQ(memory.Alloc(1), buffer_);
}

TEST_P(MgMemory, StackAllocationsAreQuadwordAlignedAndInside) {
    mgCMemory memory;
    memory.stSetBuffer(buffer_ + 1, kQuadwords - 1);

    for (int size : {1, 2, 7, 16, 33}) {
        u_long128 *p = memory.Alloc(size);
        ASSERT_NE(p, nullptr);
        EXPECT_EQ(AddressOf(p) & 15, 0u);
        EXPECT_TRUE(Inside(p, size));

        u_long128 *aligned = memory.stAlloc64(size);
        ASSERT_NE(aligned, nullptr);
        EXPECT_EQ(AddressOf(aligned) & 63, 0u);
        EXPECT_TRUE(Inside(aligned, size));
    }
}

TEST_P(MgMemory, HeapFirstGapHoldsTheStackRegion) {
    mgCMemory memory;
    memory.SetHeapMem(buffer_, kQuadwords);

    u_long128 *region = memory.StartStackMode(1, 0);
    ASSERT_NE(region, nullptr);
    EXPECT_EQ(region, buffer_ + 2 * kHeader);
    EXPECT_EQ(memory.stGetSize(), kQuadwords - 3 * kHeader);
    EXPECT_TRUE(Inside(region, memory.stGetSize()));
    EXPECT_EQ(memory.stGetUsed(), 0);
}

TEST_P(MgMemory, HeapFreeMakesTheGapReusable) {
    mgCMemory memory;
    memory.SetHeapMem(buffer_, kQuadwords);

    u_long128 *first = memory.StartStackMode(1, 0);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(memory.Alloc(4), nullptr);
    memory.EndStackMode();

    u_long128 *second = memory.StartStackMode(1, 0);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second, buffer_ + 10);
    memory.EndStackMode();

    memory.Free(first);
    EXPECT_EQ(memory.StartStackMode(1, 0), first);
}

TEST_P(MgMemory, HeapFitModeNeedsMoreThanTheRequest) {
    mgCMemory memory;
    memory.SetHeapMem(buffer_, kQuadwords);

    EXPECT_EQ(memory.StartStackMode(3, kQuadwords - 3 * kHeader), nullptr);
    EXPECT_EQ(memory.StartStackMode(3, kQuadwords - 3 * kHeader - 1), buffer_ + 2 * kHeader);
    EXPECT_EQ(memory.stGetSize(), kQuadwords - 3 * kHeader);
}

TEST_P(MgMemory, HeapLargestModeAndUnknownModes) {
    mgCMemory memory;
    memory.SetHeapMem(buffer_, kQuadwords);

    EXPECT_EQ(memory.StartStackMode(0, 0), nullptr);
    EXPECT_EQ(memory.StartStackMode(2, 0), buffer_ + 2 * kHeader);
}

TEST_P(MgMemory, HeapLargestModeSizesTheStackFromTheChosenGap) {
    mgCMemory memory;
    memory.SetHeapMem(buffer_, kQuadwords);

    // Block a leaves a 1002-quadword gap at the front; block b fills the tail, so the last gap examined is
    // one quadword. Mode 2 must size the region from the front gap it chooses.
    u_long128 *a = memory.StartStackMode(1, 0);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(memory.Alloc(1000), nullptr);
    memory.EndStackMode();

    u_long128 *b = memory.StartStackMode(1, 0);
    ASSERT_NE(b, nullptr);
    ASSERT_NE(memory.Alloc(memory.stGetSize() - 1), nullptr);
    memory.EndStackMode();

    memory.Free(a);
    EXPECT_EQ(memory.StartStackMode(2, 0), buffer_ + 2 * kHeader);
    EXPECT_EQ(memory.stGetSize(), 1002 - kHeader);
    EXPECT_TRUE(Inside(buffer_ + 2 * kHeader, memory.stGetSize()));
}

TEST_P(MgMemory, HeapStackModeRefusesPastItsRegion) {
    mgCMemory memory;
    memory.SetHeapMem(buffer_, kQuadwords);

    ASSERT_NE(memory.StartStackMode(1, 0), nullptr);
    ASSERT_NE(memory.Alloc(memory.stGetSize() - 1), nullptr);
    EXPECT_EQ(memory.Alloc(1), nullptr);
    EXPECT_EQ(memory.stGetUsed(), memory.stGetSize() - 1);
}

TEST_P(MgMemory, HeapReturnsNullWhenNoGapFits) {
    mgCMemory memory;
    memory.SetHeapMem(buffer_, kQuadwords);

    ASSERT_NE(memory.StartStackMode(1, 0), nullptr);
    ASSERT_NE(memory.Alloc(memory.stGetSize() - 1), nullptr);
    memory.EndStackMode();

    // One quadword is left between the stack block and the terminating header.
    EXPECT_EQ(memory.StartStackMode(1, 0), nullptr);
    EXPECT_EQ(memory.StartStackMode(2, 0), nullptr);
    EXPECT_EQ(memory.StartStackMode(3, 0), nullptr);
}

TEST_P(MgMemory, HeapClearRestoresAnEmptyHeap) {
    mgCMemory memory;
    memory.SetHeapMem(buffer_, kQuadwords);

    ASSERT_NE(memory.StartStackMode(1, 0), nullptr);
    memory.EndStackMode();

    memory.ClearHeapMem();
    EXPECT_EQ(memory.StartStackMode(1, 0), buffer_ + 2 * kHeader);
    EXPECT_EQ(memory.stGetSize(), kQuadwords - 3 * kHeader);
}

TEST_P(MgMemory, HeapRefusesBuffersTooSmallOrNull) {
    mgCMemory memory;
    memory.SetHeapMem(nullptr, kQuadwords);
    EXPECT_EQ(memory.StartStackMode(1, 0), nullptr);

    memory.SetHeapMem(buffer_, 15);
    EXPECT_EQ(memory.StartStackMode(1, 0), nullptr);

    memory.SetHeapMem(buffer_, -1);
    EXPECT_EQ(memory.heap_top, nullptr);
    EXPECT_EQ(memory.StartStackMode(1, 0), nullptr);
}

TEST_P(MgMemory, HeapPointersAreAlignedInsideAndOrdered) {
    mgCMemory memory;
    memory.SetHeapMem(buffer_, kQuadwords);

    u_long128 *previous_end = nullptr;
    for (int size : {3, 5, 7, 9}) {
        u_long128 *region = memory.StartStackMode(1, 0);
        ASSERT_NE(region, nullptr);
        EXPECT_EQ(AddressOf(region) & 15, 0u);
        EXPECT_TRUE(Inside(region, memory.stGetSize()));
        if (previous_end != nullptr) {
            EXPECT_GT(region, previous_end);
        }

        ASSERT_NE(memory.Alloc(size), nullptr);
        EXPECT_TRUE(Inside(region, size));
        previous_end = region + size;
        memory.EndStackMode();
    }
}

TEST_P(MgMemory, HeapStoresPointersThatRoundTrip) {
    mgCMemory memory;
    memory.SetHeapMem(buffer_, kQuadwords);

    u_long128 *region = memory.StartStackMode(1, 0);
    ASSERT_NE(region, nullptr);
    memory.EndStackMode();

    // Free finds the block by the data pointer its header stores, so a truncated header pointer fails here.
    if (GetParam() == Source::AboveFourGiB) {
        EXPECT_TRUE(IsAbove4GiB(region));
    }

    memory.Free(region);
    EXPECT_EQ(memory.StartStackMode(1, 0), region);
}

TEST_P(MgMemory, HeapFreeOfAnUnknownPointerAborts) {
    mgCMemory memory;
    memory.SetHeapMem(buffer_, kQuadwords);
    ASSERT_NE(memory.StartStackMode(1, 0), nullptr);
    memory.EndStackMode();

    KZ_ASSERT_ABORT([&] { memory.Free(buffer_ + 3); });
}

TEST_P(MgMemory, UsageTracksTheStackHighWaterMark) {
    mgCMemory memory;
    memory.stSetBuffer(buffer_, 100);

    ASSERT_NE(memory.Alloc(10), nullptr);
    ASSERT_NE(memory.Alloc(20), nullptr);
    memory.stReset();
    ASSERT_NE(memory.Alloc(5), nullptr);

    const mgMemoryUsage usage = MemoryUsage(memory);
    EXPECT_EQ(usage.capacity, 100);
    EXPECT_EQ(usage.peak, 30);

    MemoryForget(memory);
}

TEST_P(MgMemory, UsageResetsWhenTheBufferIsSetAgain) {
    mgCMemory memory;
    memory.stSetBuffer(buffer_, 100);
    ASSERT_NE(memory.Alloc(40), nullptr);

    memory.stSetBuffer(buffer_, 64);
    const mgMemoryUsage reset = MemoryUsage(memory);
    EXPECT_EQ(reset.capacity, 64);
    EXPECT_EQ(reset.peak, 0);

    ASSERT_NE(memory.Alloc(3), nullptr);
    EXPECT_EQ(MemoryUsage(memory).peak, 3);

    MemoryForget(memory);
}

TEST_P(MgMemory, UsageTracksTheHeapHighWaterMark) {
    mgCMemory memory;
    memory.SetHeapMem(buffer_, kQuadwords);

    ASSERT_NE(memory.StartStackMode(1, 0), nullptr);
    ASSERT_NE(memory.Alloc(10), nullptr);
    memory.EndStackMode();

    u_long128 *second = memory.StartStackMode(1, 0);
    ASSERT_NE(second, nullptr);
    ASSERT_NE(memory.Alloc(20), nullptr);
    memory.EndStackMode();

    // Two allocated blocks of 10 and 20 quadwords, each with a two-quadword header.
    const int two_blocks = 2 * kHeader + 30;
    EXPECT_EQ(MemoryUsage(memory).peak, two_blocks);

    memory.Free(buffer_ + 2 * kHeader);
    const mgMemoryUsage after_free = MemoryUsage(memory);
    EXPECT_EQ(after_free.capacity, kQuadwords);
    EXPECT_EQ(after_free.peak, two_blocks);

    MemoryForget(memory);
}

INSTANTIATE_TEST_SUITE_P(Buffers, MgMemory, testing::Values(Source::Ordinary, Source::AboveFourGiB),
                         [](const testing::TestParamInfo<Source> &info) {
                             return std::string(info.param == Source::Ordinary ? "Ordinary" : "AboveFourGiB");
                         });
