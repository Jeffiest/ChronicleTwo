// Shared harness for the wp02/wp03 real-asset action fixtures: production CRunScript (generated host
// runscript.cpp), the extracted production callbacks looked up by name, recorders for every other ext number,
// and the script half of CActionChara::RunScript. Include from exactly one translation unit per executable.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include "actionchara.hpp"
#include "actscript.hpp"
#include "dng_main.hpp"
#include "mainloop.hpp"
#include "padcontrol.hpp"
#include "runscript.hpp"

struct PortWp01Entry {
    const char *name;
    int (*fn)(RS_STACKDATA *, int);
};
extern const PortWp01Entry kPortWp01Callbacks[];

inline ACTION_INFO action_info;
inline CScene     *nowScene__2 = nullptr;
inline CPadControl PadCtrl;
float CGamePad::GetRXf() { return 0; }
float CGamePad::GetRYf() { return 0; }
float CGamePad::GetLXf() { return 0; }
float CGamePad::GetLYf() { return 0; }
int CGamePad::On(int) { return 0; }
int CGamePad::Down(int) { return 0; }
int CGamePad::Up(int) { return 0; }

namespace {

struct Call {
    int         frame;
    int         ext;
    int         argc;
    std::string args;
};
std::vector<Call> g_calls;
bool              g_trace = false;
int               g_frame = -1; // -1 while INIT runs

int (*g_real[ACTION_EXT_FUNC_MAX])(RS_STACKDATA *, int);

// Every ext number routes through here: the production body (when bound) runs first so a recorded RS_PTR
// shows the value the callback delivered; unbound numbers are record-only.
template <int N>
int Record(RS_STACKDATA *stack, int argc) {
    int result = g_real[N] ? g_real[N](stack, argc) : 1;
    std::string text;
    for (int i = 0; i < argc && i < 6; ++i) {
        char piece[48];
        if (stack[i].type == RS_PTR) std::snprintf(piece, sizeof piece, " &%d", stack[i].val.p->val.i);
        else if (stack[i].type == RS_FLOAT) std::snprintf(piece, sizeof piece, " %gf", stack[i].val.f);
        else std::snprintf(piece, sizeof piece, " %d", stack[i].val.i);
        text += piece;
    }
    g_calls.push_back({g_frame, N, argc, text});
    return result;
}

template <int... I>
void FillRecorders(int (**table)(RS_STACKDATA *, int), std::integer_sequence<int, I...>) {
    ((table[I] = &Record<I>), ...);
}

struct ExtBinding {
    const char *name;
    int         number;
};
const ExtBinding kBindings[] = {{"_PROG_SET", ACTION_EXT_PROG_SET},
                                {"_PROG_GET", ACTION_EXT_PROG_GET},
                                {"_GET_ATTK_TYPE", ACTION_EXT_GET_ATTK_TYPE},
                                {"_GET_MOVE_TYPE", ACTION_EXT_GET_MOVE_TYPE},
                                {"_SET_MOVE_SPEED", ACTION_EXT_SET_MOVE_SPEED},
                                {"_CHECK_PAUSE", ACTION_EXT_CHECK_PAUSE},
                                {"_GET_BTN", ACTION_EXT_GET_BTN},
                                {"_GET_PAD_HISTORY", ACTION_EXT_GET_PAD_HISTORY},
                                {"_RESET_PAD_HISTORY", ACTION_EXT_RESET_PAD_HISTORY},
                                {"_GET_ACUMU_PAD", ACTION_EXT_GET_ACUMU_PAD},
                                {"_RESET_ACUMU_PAD", ACTION_EXT_RESET_ACUMU_PAD}};

int (*g_table[ACTION_EXT_FUNC_MAX])(RS_STACKDATA *, int);

void BindTable() {
    FillRecorders(g_table, std::make_integer_sequence<int, ACTION_EXT_FUNC_MAX>{});
    for (const ExtBinding &b : kBindings) {
        for (const PortWp01Entry *e = kPortWp01Callbacks; e->name; ++e) {
            if (std::strcmp(e->name, b.name) == 0) g_real[b.number] = e->fn;
        }
    }
}

int g_failures = 0;
bool Check(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++g_failures;
    }
    return condition;
}

struct Harness {
    alignas(16) std::vector<unsigned char> image;
    std::vector<RS_STACKDATA> values = std::vector<RS_STACKDATA>(0x80);
    std::vector<RS_CALLDATA>  calls = std::vector<RS_CALLDATA>(0x200);
    CRunScript                script;
    alignas(CActionChara) unsigned char chara_storage[sizeof(CActionChara)];
    alignas(CScene) unsigned char       scene_storage[sizeof(CScene)];
    CActionChara *chara;
    int           prog_no = ACTION_PROG_RUNNING;
    int           acumu = 0;

    explicit Harness(const std::vector<unsigned char> &bytes) : image(bytes) {
        std::memset(chara_storage, 0, sizeof chara_storage);
        std::memset(scene_storage, 0, sizeof scene_storage);
        chara = reinterpret_cast<CActionChara *>(chara_storage);
        nowScene__2 = reinterpret_cast<CScene *>(scene_storage);
        action_info.chara = chara;
        script.load(reinterpret_cast<RS_PROG_HEADER *>(image.data()), values.data(), 0x80, calls.data(), 0x200);
        script.ext_func(g_table, ACTION_EXT_FUNC_MAX);
        PadCtrl.Initialize();
        PadCtrl.RegisterBtn(PAD_BTN_ACTION_CONFIRM, 0x1, 0);
        PadCtrl.RegisterBtn(PAD_BTN_ACTION_HELD, 0x2, 0);
    }

    // Script half of CActionChara::RunScript for one frame.
    void Frame(int confirm, int held) {
        PadCtrl.btn[PAD_BTN_ACTION_CONFIRM].value = confirm;
        PadCtrl.btn[PAD_BTN_ACTION_HELD].value = held;
        chara->pad_history |= PadCtrl.Btn(PAD_BTN_ACTION_CONFIRM);
        if (PadCtrl.Btn(PAD_BTN_ACTION_HELD) != 0) chara->acumu_pad++;
        else chara->acumu_pad = 0;
        if (prog_no != ACTION_PROG_RUNNING) {
            if (script.check_program(prog_no) != 0) {
                script.run(prog_no);
                prog_no = ACTION_PROG_RUNNING;
            }
        } else {
            script.resume();
            if (script.end != 0) prog_no = ACTION_PROG_MAIN;
        }
    }
};

bool ReadFile(const std::string &path, std::vector<unsigned char> &out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}


} // namespace
