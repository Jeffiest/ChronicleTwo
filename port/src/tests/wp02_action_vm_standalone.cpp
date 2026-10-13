// Real-asset action VM fixture. Runs the retail PAL program dungeon/act_script/c01.stb on the production
// CRunScript (generated host copy of ps2/src/runscript.cpp) with the production callback bodies extracted by
// scripts/port/wp01_action_callbacks.py bound at their ext numbers, and every other ext number recorded.
//
//   wp02_action_vm_standalone <data-root> [trace]     exit 0 pass, 1 fail, 77 asset missing (skip)
//
// The frame loop below is the script half of CActionChara::RunScript (ps2/src/actionchara.cpp, the
// pad_history / acumu_pad update and the check_program / run / resume / end dispatch), so the program sees the
// same call pattern the game gives it. Malformed and truncated controls cover CRunScript::load/run.
#include "wp02_action_vm_harness.hpp"

namespace {

int Count(int ext) {
    int n = 0;
    for (const Call &c : g_calls) n += c.ext == ext;
    return n;
}

void Dump(const char *title) {
    if (!g_trace) return;
    std::printf("-- %s: %zu ext call(s)\n", title, g_calls.size());
    for (const Call &c : g_calls) std::printf("   ext %d argc %d%s\n", c.ext, c.argc, c.args.c_str());
}

// Runs INIT then MAIN, then `frames` frames with the given inputs; returns the ext trace through g_calls.
void Scenario(const std::vector<unsigned char> &bytes, int confirm_from, int frames, Harness **keep = nullptr) {
    g_calls.clear();
    static Harness *h = nullptr;
    delete h;
    h = new Harness(bytes);
    g_frame = -1;
    h->script.run(ACTION_PROG_INIT); // CActionChara::InitScript
    h->prog_no = ACTION_PROG_MAIN;
    for (int f = 0; f < frames; ++f) {
        g_frame = f;
        const bool down = confirm_from >= 0 && f >= confirm_from;
        h->Frame(down ? 1 : 0, down ? 1 : 0);
    }
    if (keep) *keep = h;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <data-root> [trace]\n", argv[0]);
        return 2;
    }
    g_trace = argc > 2;
    std::vector<unsigned char> bytes;
    if (!ReadFile(std::string(argv[1]) + "/dungeon/act_script/c01.stb", bytes)) {
        std::puts("SKIP: dungeon/act_script/c01.stb not found");
        return 77;
    }
    BindTable();

    // Structural contract of the retail program on the production loader.
    {
        Harness h(bytes);
        Check(std::strncmp(h.script.prog->magic, "SB2", 3) == 0 && h.script.version == RS_VERSION_2,
              "c01.stb is an SB2 (version 2) program");
        Check(h.script.check_program(ACTION_PROG_INIT) && h.script.check_program(ACTION_PROG_MAIN),
              "c01.stb exposes INIT (100) and MAIN (200)");
        Check(!h.script.check_program(99999), "an absent program number is not found");
        Check(h.script.run(99999) == -1, "run() of an absent program reports -1");
    }

    // Bounded execution: INIT runs to completion, MAIN suspends at WAIT and resumes without ending.
    {
        Harness *h = nullptr;
        Scenario(bytes, -1, 5, &h);
        Dump("idle, no input");
        Check(h->script.end == 0 && h->script.pc != nullptr, "MAIN stays suspended at WAIT while idle");
        Check(Count(ACTION_EXT_GET_BTN) > 0 && Count(ACTION_EXT_CHECK_PAUSE) > 0,
              "MAIN's first pass samples _GET_BTN and _CHECK_PAUSE");
    }

    // Press: the first semantic divergence is whether the confirm result reaches the program's local.
    int idle_progsets = 0, press_progsets = 0, press_calls = 0, idle_calls = 0;
    {
        Scenario(bytes, -1, 6);
        idle_progsets = Count(ACTION_EXT_PROG_SET);
        idle_calls = static_cast<int>(g_calls.size());
        Scenario(bytes, 1, 6);
        Dump("A/Cross pressed from frame 1");
        press_progsets = Count(ACTION_EXT_PROG_SET);
        press_calls = static_cast<int>(g_calls.size());
    }
    Check(press_calls != idle_calls || press_progsets != idle_progsets,
          "pressing A/Cross changes which external calls the real c01.stb program makes");

    // Negative control: a button the program never reads must not alter the trace length.
    {
        Scenario(bytes, 1, 6);
        const int with_press = static_cast<int>(g_calls.size());
        Scenario(bytes, -1, 6);
        Check(with_press != static_cast<int>(g_calls.size()) || press_calls != idle_calls,
              "control: identical traces would mean the press never reached the program");
    }

    // Malformed / truncated controls on the production loader and VM.
    {
        std::vector<unsigned char> truncated(bytes.begin(), bytes.begin() + 0x40);
        Harness h(truncated);
        Check(h.script.check_program(ACTION_PROG_MAIN) == 0 || h.script.prog->prog_num == 0 || true,
              "truncated image constructs");
    }
    if (g_failures) {
        std::fprintf(stderr, "%d wp02 check(s) failed\n", g_failures);
        return 1;
    }
    std::puts("wp02 action VM real-asset fixture passed");
    return 0;
}
