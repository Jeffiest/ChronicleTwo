"""Add bounded passive combat observations to generated host copies only."""
import pathlib
import sys


def transform(unit: str, text: str) -> str:
    def once(old: str, new: str) -> None:
        nonlocal text
        if text.count(old) != 1:
            raise RuntimeError(f"{unit}: combat trace anchor changed: {old[:60]!r}")
        text = text.replace(old, new)

    if unit == "src/dng_main.cpp":
        old = "    if (!DngStatus.eye_view) {\n        if (!DebugPause && !(BattleAreaScene->pause_flag & DNG_PAUSE_PLAYER_CONTROL)) {"
        hook = "    port_combat_trace::Record(1, PadCtrl.Btn(PAD_BTN_ACTION_CONFIRM), PadCtrl.Btn(PAD_BTN_ACTION_HELD), DngStatus.eye_view, BattleAreaScene->pause_flag, DebugPause, MainChara__2->prog_no, MainChara__2->script.end, MainChara__2->pad_history, MainChara__2->acumu_pad);\n"
        once(old, hook + old)
    elif unit == "src/actionchara.cpp":
        hook = "    port_combat_trace::Record({stage}, PadCtrl.Btn(PAD_BTN_ACTION_CONFIRM), PadCtrl.Btn(PAD_BTN_ACTION_HELD), -1, -1, -1, prog_no, script.end, pad_history, acumu_pad);\n"
        once("    history = action_info.chara->pad_history;", hook.format(stage=2) + "    history = action_info.chara->pad_history;")
        old = "        if (script.end != 0) {\n            prog_no = ACTION_PROG_MAIN;\n        }\n    }\n\n    GetPosition(position);"
        once(old, old.replace("    GetPosition(position);", hook.format(stage=3) + "    GetPosition(position);"))
    else:
        raise RuntimeError(f"unsupported combat trace unit: {unit}")
    return '#include "combat_trace.hpp"\n' + text


if __name__ == "__main__":
    path = pathlib.Path(sys.argv[2])
    path.write_text(transform(sys.argv[1], path.read_text(encoding="utf-8")), encoding="utf-8")
