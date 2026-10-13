#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "character.hpp"
#include "mg_frame.hpp"
#include "runscript.hpp"

int PortEffectEntryPosition(CCharacter2 *, RS_STACKDATA *, int);
int PortEffectDistance(RS_STACKDATA *, int);
int PortEffectDistance2(RS_STACKDATA *, int);

static void CheckDistanceVM() {
    struct Program {
        RS_PROG_HEADER header{};
        RS_PROGDATA entry{};
        funcdata function{};
        vmcode_t instructions[32]{};
    } program;
    std::memcpy(program.header.magic, "SB2", 4);
    program.header.main = offsetof(Program, function);
    program.header.code = offsetof(Program, instructions);
    program.header.prog = offsetof(Program, entry);
    program.header.prog_num = 1;
    program.entry = {200, program.header.main};
    program.function.local = 2;
    int used = 0;
    auto emit = [&](int op, int arg1 = 0, int arg2 = 0) {
        program.instructions[used++] = {op, arg1, {arg2}};
    };
    auto real = [&](float value) {
        emit(RS_OP_PUSH_CONST, RS_CONST_FLOAT);
        program.instructions[used - 1].arg2_float = value;
    };
    emit(RS_OP_PUSH_CONST, RS_CONST_INT, 7);
    emit(RS_OP_PUSH_CONST, RS_CONST_INT, 3);
    real(4.0f);
    emit(RS_OP_PUSH_CONST, RS_CONST_INT, 0);
    emit(RS_OP_LOAD_ADDR, 0, RS_ADDR_LOCAL);
    emit(RS_OP_EXT, 5);
    emit(RS_OP_WAIT);
    emit(RS_OP_PUSH_CONST, RS_CONST_INT, 8);
    for (int value : {1, 2, 3, 4, 6, 3}) emit(RS_OP_PUSH_CONST, RS_CONST_INT, value);
    emit(RS_OP_LOAD_ADDR, 1, RS_ADDR_LOCAL);
    emit(RS_OP_EXT, 8);
    emit(RS_OP_WAIT);
    emit(RS_OP_END);
    int (*callbacks[9])(RS_STACKDATA *, int){};
    callbacks[7] = PortEffectDistance;
    callbacks[8] = PortEffectDistance2;
    struct Values { std::uint64_t before = 123; RS_STACKDATA slots[64]{}; std::uint64_t after = 456; } values;
    RS_CALLDATA calls[2]{};
    for (int repeat = 0; repeat < 128; ++repeat) {
        CRunScript vm;
        vm.load(&program.header, values.slots, 64, calls, 2);
        vm.ext_func(callbacks, 9);
        assert(vm.run(200) == 1);
        assert(vm.frame[0].val.f == 5.0f);
        vm.resume();
        assert(vm.frame[1].val.f == 5.0f);
        vm.resume();
        assert(vm.end == 1 && vm.pc == nullptr);
        assert(values.before == 123 && values.after == 456);
    }
    RS_STACKDATA args[7]{};
    assert(PortEffectDistance(args, 3) == 0);
    assert(PortEffectDistance2(args, 6) == 0);
}

#ifdef _WIN32
extern "C" __declspec(dllimport) unsigned long __stdcall SetErrorMode(unsigned long);
#endif

int main() {
#ifdef _WIN32
    SetErrorMode(0x0002);
#endif
    CheckDistanceVM();
    mgCFrame frame;
    frame.parent = nullptr;
    frame.reference = 0;
    frame.changed = 0;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) frame.lw_matrix[r][c] = r == c ? 1.0f : 0.0f;
    frame.lw_matrix[3][0] = 17.0f;
    frame.lw_matrix[3][1] = 29.0f;
    frame.lw_matrix[3][2] = 43.0f;
    struct alignas(16) Guarded { float before = 123; float value[4]{}; float after = 456; } out;
    assert(reinterpret_cast<std::uintptr_t>(out.value) % 16 != 0);
    frame.GetWorldPosition0(out.value);
    assert(out.before == 123 && out.after == 456);
    for (int i = 0; i < 4; ++i) assert(out.value[i] == frame.lw_matrix[3][i]);

    CCharacter2 chara;
    chara.entry_frame[0] = chara.entry_frame[1] = &frame;
    for (int entry = 0; entry < CHARA_ENTRY_FRAME_MAX; ++entry) {
        assert(chara.GetEntryObjectPos(entry, out.value));
        assert(out.before == 123 && out.after == 456);
        RS_STACKDATA args[5]{}, values[3]{};
        args[0].type = args[1].type = RS_INT;
        args[1].val.i = entry;
        for (int i = 0; i < 3; ++i) {
            args[i + 2].type = RS_PTR;
            args[i + 2].val.p = &values[i];
            values[i].val.f = -77.0f;
        }
        assert(PortEffectEntryPosition(&chara, args, 5) == 1);
        for (int i = 0; i < 3; ++i) assert(values[i].val.f == frame.lw_matrix[3][i]);
        for (auto &v : values) v.val.f = -77.0f;
        assert(PortEffectEntryPosition(&chara, args, 4) == 0);
        assert(PortEffectEntryPosition(nullptr, args, 5) == 0);
        for (auto &v : values) assert(v.val.f == -77.0f);
    }
    std::puts("effect distance callbacks through VM x128, vector positions and entry frames passed");
}
