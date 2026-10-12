// Production-contract fixture for the action callbacks _GET_BTN, _CHECK_PAUSE, _GET_PAD_HISTORY and
// _RESET_PAD_HISTORY. Their bodies come from the generated host copy of ps2/src/actscript.cpp (extracted by
// scripts/port/wp01_action_callbacks.py); CPadControl is the port's production class. Argument shapes follow
// the retail PAL program mainchr/c01.stb: _GET_BTN(button, &result) is two slots, the RS_PTR in slot 1.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "actionchara.hpp"
#include "actscript.hpp"
#include "dng_main.hpp"
#include "mainloop.hpp"
#include "padcontrol.hpp"

struct PortWp01Entry {
    const char *name;
    int (*fn)(RS_STACKDATA *, int);
};
extern const PortWp01Entry kPortWp01Callbacks[];

namespace {
int Call(const char *name, RS_STACKDATA *stack, int argc) {
    for (const PortWp01Entry *e = kPortWp01Callbacks; e->name; ++e) {
        if (std::strcmp(e->name, name) == 0) return e->fn(stack, argc);
    }
    std::fprintf(stderr, "FAIL: no extracted callback %s\n", name);
    std::exit(2);
}
} // namespace

#define PortWp01CheckPause(s, n) Call("_CHECK_PAUSE", s, n)
#define PortWp01GetBtn(s, n) Call("_GET_BTN", s, n)
#define PortWp01GetPadHistory(s, n) Call("_GET_PAD_HISTORY", s, n)
#define PortWp01ResetPadHistory(s, n) Call("_RESET_PAD_HISTORY", s, n)
#define PortWp01GetAcumuPad(s, n) Call("_GET_ACUMU_PAD", s, n)
#define PortWp01ResetAcumuPad(s, n) Call("_RESET_ACUMU_PAD", s, n)

ACTION_INFO action_info;
CScene     *nowScene__2 = nullptr;
CPadControl PadCtrl;

// padcontrol.cpp's Update reads CGamePad; the fixture never calls it, so the link needs only these.
float CGamePad::GetRXf() { return 0; }
float CGamePad::GetRYf() { return 0; }
float CGamePad::GetLXf() { return 0; }
float CGamePad::GetLYf() { return 0; }
int CGamePad::On(int) { return 0; }
int CGamePad::Down(int) { return 0; }
int CGamePad::Up(int) { return 0; }

namespace {

constexpr int kCanary = 0x5A5A5A5A;
int           g_failures = 0;

bool Check(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++g_failures;
    }
    return condition;
}

// One script argument window plus the output cell it points at, bracketed by canaries.
struct Window {
    RS_STACKDATA before;
    RS_STACKDATA slot[4];
    RS_STACKDATA after;
    RS_STACKDATA result;

    Window() {
        std::memset(this, 0, sizeof(*this));
        before.type = after.type = RS_INT;
        before.val.i = after.val.i = kCanary;
        result.type = RS_INT;
        result.val.i = kCanary;
    }
    void Int(int index, int value) { slot[index].type = RS_INT; slot[index].val.i = value; }
    void Out(int index) { slot[index].type = RS_PTR; slot[index].val.p = &result; }
    bool Guards() const { return before.val.i == kCanary && after.val.i == kCanary; }
};

bool Untouched(const Window &w, int index, int type, int value) {
    return w.slot[index].type == type && w.slot[index].val.i == value;
}

void TestGetBtn() {
    PadCtrl.Initialize();
    PadCtrl.RegisterBtn(PAD_BTN_ACTION_CONFIRM, 0x1, 0);
    PadCtrl.RegisterBtn(PAD_BTN_ACTION_HELD, 0x2, 0);
    PadCtrl.btn[PAD_BTN_ACTION_CONFIRM].value = 1; // A/Cross pressed this frame
    PadCtrl.btn[PAD_BTN_ACTION_HELD].value = 0;

    // c01.stb: PUSH_CONST 50 ; LOAD_ADDR local ; EXT 43, so argc is 2 with the pointer in slot 1.
    Window w;
    w.Int(0, PAD_BTN_ACTION_CONFIRM);
    w.Out(1);
    Check(PortWp01GetBtn(&w.slot[0], 2) == 1, "_GET_BTN accepts (button, &result)");
    Check(w.result.val.i == 1, "_GET_BTN delivers the pressed state through the RS_PTR in slot 1");
    Check(Untouched(w, 0, RS_INT, PAD_BTN_ACTION_CONFIRM), "_GET_BTN leaves the integer input slot alone");
    Check(w.slot[1].type == RS_PTR && w.slot[1].val.p == &w.result, "_GET_BTN leaves the pointer slot alone");
    Check(w.Guards(), "_GET_BTN stays inside its argument window");

    // The held button (c01.stb uses 56) reads 0 and must overwrite the canary with the released state.
    Window held;
    held.Int(0, PAD_BTN_ACTION_HELD);
    held.Out(1);
    Check(PortWp01GetBtn(&held.slot[0], 2) == 1, "_GET_BTN accepts the held button");
    Check(held.result.val.i == 0, "_GET_BTN reports release (value 0), not a stale canary");

    PadCtrl.btn[PAD_BTN_ACTION_HELD].value = 1;
    Window hold2;
    hold2.Int(0, PAD_BTN_ACTION_HELD);
    hold2.Out(1);
    PortWp01GetBtn(&hold2.slot[0], 2);
    Check(hold2.result.val.i == 1, "_GET_BTN reports hold");

    // Legitimate gates: an unbound or out-of-range logical button reads 0; wrong argc is rejected untouched.
    Window unbound;
    unbound.Int(0, PAD_BTN_ACTION_CONFIRM + 3);
    unbound.Out(1);
    PortWp01GetBtn(&unbound.slot[0], 2);
    Check(unbound.result.val.i == 0, "_GET_BTN reads 0 for an unbound button");
    Window bad;
    bad.Int(0, PAD_BTN_ACTION_CONFIRM);
    bad.Out(1);
    Check(PortWp01GetBtn(&bad.slot[0], 0) == 0 && bad.result.val.i == kCanary,
          "_GET_BTN rejects argc 0 without writing");

    // Negative control: a result routed to an integer slot must not be written anywhere.
    Window orphan;
    orphan.Int(0, PAD_BTN_ACTION_CONFIRM);
    orphan.Int(1, 77);
    PortWp01GetBtn(&orphan.slot[0], 2);
    Check(orphan.result.val.i == kCanary && Untouched(orphan, 1, RS_INT, 77),
          "an integer slot 1 is not an output and stays unchanged");
}

void TestCheckPause() {
    alignas(CScene) static unsigned char scene_storage[sizeof(CScene)];
    std::memset(scene_storage, 0, sizeof(scene_storage));
    nowScene__2 = reinterpret_cast<CScene *>(scene_storage);
    nowScene__2->battle_area.pause_flag = 0x4100; // value logged by the failed playtest

    Window hit;
    hit.Int(0, 0x4000);
    hit.Out(1);
    Check(PortWp01CheckPause(&hit.slot[0], 2) == 1, "_CHECK_PAUSE accepts (mask, &result)");
    Check(hit.result.val.i == 0x4000, "_CHECK_PAUSE delivers pause_flag & mask through slot 1");
    Check(Untouched(hit, 0, RS_INT, 0x4000) && hit.Guards(), "_CHECK_PAUSE keeps input slot and canaries");

    Window miss;
    miss.Int(0, 0x0001);
    miss.Out(1);
    PortWp01CheckPause(&miss.slot[0], 2);
    Check(miss.result.val.i == 0, "_CHECK_PAUSE reports an unset bit as 0");

    Window bad;
    bad.Int(0, 0x4000);
    bad.Out(1);
    Check(PortWp01CheckPause(&bad.slot[0], 1) == 0 && bad.result.val.i == kCanary,
          "_CHECK_PAUSE rejects the wrong argc without writing");
}

void TestPadHistory() {
    alignas(CActionChara) static unsigned char chara_storage[sizeof(CActionChara)];
    std::memset(chara_storage, 0, sizeof(chara_storage));
    CActionChara *chara = reinterpret_cast<CActionChara *>(chara_storage);
    action_info.chara = chara;
    chara->pad_history = 1; // logged by the failed playtest

    // c01a.stb: LOAD_ADDR local ; EXT 45, so argc is 1 with the pointer in slot 0.
    Window w;
    w.Out(0);
    Check(PortWp01GetPadHistory(&w.slot[0], 1) == 1, "_GET_PAD_HISTORY accepts (&result)");
    Check(w.result.val.i == 1 && w.Guards(), "_GET_PAD_HISTORY delivers history through slot 0");
    Window none;
    Check(PortWp01GetPadHistory(&none.slot[0], 0) == 0 && none.result.val.i == kCanary,
          "_GET_PAD_HISTORY rejects argc 0");

    Window reset;
    reset.Out(0);
    Check(PortWp01ResetPadHistory(&reset.slot[0], 0) == 1 && chara->pad_history == 0,
          "_RESET_PAD_HISTORY clears history with no arguments");
    Check(reset.result.val.i == kCanary, "_RESET_PAD_HISTORY writes no result");

    chara->acumu_pad = 12;
    Window acumu;
    acumu.Out(0);
    PortWp01GetAcumuPad(&acumu.slot[0], 1);
    Check(acumu.result.val.i == 12, "_GET_ACUMU_PAD delivers the charge count through slot 0");
    PortWp01ResetAcumuPad(&acumu.slot[0], 0);
    Check(chara->acumu_pad == 0, "_RESET_ACUMU_PAD clears the charge count");
}

} // namespace

int main() {
    TestGetBtn();
    TestCheckPause();
    TestPadHistory();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d wp01 action callback check(s) failed\n", g_failures);
        return 1;
    }
    std::puts("wp01 action callback fixture passed");
    return 0;
}
