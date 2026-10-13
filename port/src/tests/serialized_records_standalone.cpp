#include <cassert>
#include <cstdint>

#include "../serialized_record_contracts.hpp"

using namespace serialized_record_contracts;

int main() {
    constexpr uint8_t hd4_record[12] = {0x18, 0, 0, 0, 0x34, 0x12, 0, 0, 7, 0, 0, 0};
    static_assert(ReadLe32(hd4_record) == 0x18);
    static_assert(ReadLe32(hd4_record + 4) == 0x1234);
    static_assert(ReadLe32(hd4_record + 8) == 7);
    static_assert(RecordTableFits(0x100, 0x30, 12));
    static_assert(!RecordTableFits(0x100, 0x31, 12));
    static_assert(!RecordTableFits(0x20, 0x30, 12));
    static_assert(RangeFits(0x60, 0x40, 0x20));
    static_assert(!RangeFits(0x60, 0x41, 0x20));

    uint8_t unaligned[5] = {0xEE, 0x78, 0x56, 0x34, 0x12};
    assert(ReadLe32(unaligned + 1) == 0x12345678);
    assert(RangeFits(0x4C, 0x40, 12));
    assert(!RangeFits(0x4C, 0x48, 12));
    return 0;
}
