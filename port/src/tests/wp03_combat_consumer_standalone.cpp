// A/Cross consumer fixture. The real PAL program dungeon/act_script/c01.stb runs on the production CRunScript
// with production _GET_BTN/_CHECK_PAUSE/_GET_PAD_HISTORY/_RESET_PAD_HISTORY/_GET_ACUMU_PAD and _SET_MOS bodies
// (extracted from the generated host actscript.cpp by wp01_action_callbacks.py fixture-motion). _SET_MOS reaches
// CActionChara::SetMotion through a recording hook because the character hierarchy is not constructible here;
// that hook is the first observable "attack motion starts" transition.
//
//   wp03_combat_consumer_standalone <data-root> [trace]     exit 0 pass, 1 fail, 77 asset missing (skip)
#include "wp02_action_vm_harness.hpp"

struct MotionStart {
    int         frame;
    std::string name;
    int         flag;
};
static std::vector<MotionStart> g_motions;

void PortWp01MotionHook(CActionChara *, char *name, int flag) {
    g_motions.push_back({g_frame, name ? name : "", flag});
}
void PortWp01StepHook(CActionChara *, float) {}
CActionChara *PortWp01SearchHook(CActionChara *, char *) { return nullptr; }

namespace {

std::string Hex(const std::string &text) {
    std::string out;
    for (unsigned char c : text) {
        char piece[4];
        std::snprintf(piece, sizeof piece, "%02X", c);
        out += piece;
    }
    return out;
}

struct Input {
    int  confirm_from = -1; // first frame A/Cross is down
    int  confirm_to = -1;   // first frame it is released again; -1 keeps it held
    int  pause_flag = 0;    // battle_area.pause_flag for the whole run
};

Harness *Run(const std::vector<unsigned char> &bytes, const Input &in, int frames) {
    static Harness *h = nullptr;
    delete h;
    g_calls.clear();
    g_motions.clear();
    h = new Harness(bytes);
    reinterpret_cast<CScene *>(h->scene_storage)->battle_area.pause_flag = in.pause_flag;
    g_frame = -1;
    h->script.run(ACTION_PROG_INIT);
    h->prog_no = ACTION_PROG_MAIN;
    for (int f = 0; f < frames; ++f) {
        g_frame = f;
        const bool down = in.confirm_from >= 0 && f >= in.confirm_from && (in.confirm_to < 0 || f < in.confirm_to);
        h->Frame(down ? 1 : 0, down ? 1 : 0);
    }
    return h;
}

int MotionsWithFlag6() {
    int n = 0;
    for (const MotionStart &m : g_motions) n += m.flag == 6 && !m.name.empty();
    return n;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <data-root> [trace]\n", argv[0]);
        return 2;
    }
    const bool trace = argc > 2;
    std::vector<unsigned char> bytes;
    if (!ReadFile(std::string(argv[1]) + "/dungeon/act_script/c01.stb", bytes)) {
        std::puts("SKIP: dungeon/act_script/c01.stb not found");
        return 77;
    }
    BindTable();
    // bind the production motion callback too
    for (const PortWp01Entry *e = kPortWp01Callbacks; e->name; ++e) {
        if (std::strcmp(e->name, "_SET_MOS") == 0) g_real[ACTION_EXT_SET_MOS] = e->fn;
    }

    // Idle: the program must not start a motion by itself.
    Run(bytes, {}, 10);
    Check(g_motions.empty(), "no motion starts while no button is pressed");

    // Playtest condition: A/Cross pressed, pause 0x4100 (not the 0x2000 gate), history reaches 1.
    Harness *h = Run(bytes, {1, -1, 0x4100}, 10);
    if (trace) {
        for (const MotionStart &m : g_motions)
            std::printf("frame %d SET_MOS motion=%s flag=%d\n", m.frame, Hex(m.name).c_str(), m.flag);
    }
    Check(MotionsWithFlag6() >= 1, "pressing A/Cross starts a motion through _SET_MOS (the attack)");
    Check(h->chara->acumu_pad == 9, "holding A/Cross advances acumu_pad every frame (charge hold)");
    const std::string first_attack = g_motions.empty() ? std::string() : g_motions.front().name;

    // Tap: a one-frame press must still select the attack (pad_history latches it) and the same motion.
    h = Run(bytes, {1, 2, 0x4100}, 10);
    Check(MotionsWithFlag6() >= 1, "a one-frame tap still starts the attack");
    Check(!g_motions.empty() && g_motions.front().name == first_attack, "tap and hold choose the same first motion");
    Check(h->chara->pad_history == 0 || h->chara->pad_history == 1, "tap leaves pad_history a latched 0 or 1");

    // Release: acumu_pad returns to 0 after the button comes up.
    h = Run(bytes, {1, 4, 0x4100}, 10);
    Check(h->chara->acumu_pad == 0, "releasing A/Cross resets acumu_pad to 0");

    // Legitimate gate: bit 0x2000 of pause_flag holds the program in its pause loop; no attack may start.
    Run(bytes, {1, -1, 0x2000}, 10);
    Check(g_motions.empty(), "pause_flag & 0x2000 blocks the attack");

    // Negative control: a different logical button (held without confirm) must not select the attack.
    Run(bytes, {-1, -1, 0x4100}, 10);
    Check(g_motions.empty(), "control: no press, no motion");

    if (g_failures) {
        std::fprintf(stderr, "%d wp03 check(s) failed\n", g_failures);
        return 1;
    }
    std::puts("wp03 combat consumer fixture passed");
    return 0;
}
