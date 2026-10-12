#include "runscript.hpp"
#include "script_storage.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {

void check_builder(mgCMemory &memory, std::size_t value_count, std::size_t call_count,
                   const char *owner) {
    auto *values = port_script_storage::allocate<RS_STACKDATA>(&memory, value_count, owner);
    auto *calls = port_script_storage::allocate<RS_CALLDATA>(&memory, call_count, owner);
    auto *sentinel = port_script_storage::allocate<unsigned int>(&memory, 1, owner);

    values[value_count - 1].type = 0x1234;
    values[value_count - 1].val.i = 0x5678;
    calls[call_count - 1].ret = reinterpret_cast<vmcode_t *>(sentinel);
    calls[call_count - 1].frame = reinterpret_cast<RS_STACKDATA *>(sentinel);
    calls[call_count - 1].func = reinterpret_cast<funcdata *>(sentinel);
    *sentinel = 0x13579BDF;

    assert(reinterpret_cast<std::byte *>(calls) >=
           reinterpret_cast<std::byte *>(values + value_count));
    assert(reinterpret_cast<std::byte *>(sentinel) >=
           reinterpret_cast<std::byte *>(calls + call_count));
    assert(values[value_count - 1].type == 0x1234);
    assert(values[value_count - 1].val.i == 0x5678);
    assert(calls[call_count - 1].ret == reinterpret_cast<vmcode_t *>(sentinel));
    assert(calls[call_count - 1].frame == reinterpret_cast<RS_STACKDATA *>(sentinel));
    assert(calls[call_count - 1].func == reinterpret_cast<funcdata *>(sentinel));
    assert(*sentinel == 0x13579BDF);
}

int run(const char *case_name) {
    if (std::strcmp(case_name, "null-arena") == 0) {
        (void)port_script_storage::allocate<RS_STACKDATA>(nullptr, 1, "null-arena");
        return 3;
    }
    if (std::strcmp(case_name, "small-arena") == 0) {
        mgCMemory memory;
        std::array<u_long128, 1> backing{};
        memory.stSetBuffer(backing.data(), static_cast<int>(backing.size()));
        memory.lock = 1;
        (void)port_script_storage::allocate<RS_STACKDATA>(&memory, 128, "small-arena");
        return 3;
    }
    if (std::strcmp(case_name, "size-overflow") == 0) {
        (void)port_script_storage::blocks_for<RS_CALLDATA>(std::numeric_limits<std::size_t>::max());
        return 3;
    }
    if (std::strcmp(case_name, "normal") != 0) {
        std::fprintf(stderr, "unknown script storage case: %s\n", case_name);
        return 2;
    }

    static_assert(sizeof(RS_STACKDATA) == 16);
    static_assert(sizeof(RS_CALLDATA) == 24);
    mgCMemory memory;
    std::array<u_long128, 2048> backing{};
    memory.stSetBuffer(backing.data(), static_cast<int>(backing.size()));

    check_builder(memory, 128, 512, "event/action/monster");
    memory.stReset();
    check_builder(memory, 64, 2, "effect");
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    if (argc > 2) {
        std::fprintf(stderr, "usage: script_storage [normal|null-arena|small-arena|size-overflow]\n");
        return 2;
    }
    return run(argc == 2 ? argv[1] : "normal");
}
