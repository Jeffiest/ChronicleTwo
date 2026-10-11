#include <cstdint>

#include "menucommon.hpp"

// Retail aligns by rounding the pointer's 32-bit value; on the host the address is 64-bit, so
// the same round-to-64-bytes is done on the full value.
u_long128 *MenuCalcBufAlignment(u_long128 *buffer) {
    std::uintptr_t size = reinterpret_cast<std::uintptr_t>(buffer);

    if ((size % 64) != 0) {
        size = ((size >> 6) + 1) << 6;
    }

    return reinterpret_cast<u_long128 *>(size);
}
