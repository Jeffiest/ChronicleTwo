#!/usr/bin/env python3
"""Fix action-script callbacks whose output slot depends on C++ argument evaluation order.

Retail action programs call _GET_BTN(button, &result) and _CHECK_PAUSE(mask, &result): the input sits in
slot 0 and the RS_PTR output in slot 1. ps2/src/actscript.cpp writes
    SetStack(stack, f(GetStackInt(stack++)))
relying on the callee argument being evaluated before the destination. clang evaluates left to right, so
the destination is slot 0 (an integer), SetStack's RS_PTR test fails and the result is silently dropped.

    wp01_action_callbacks.py unit src/actscript.cpp <generated copy>    rewrite in place (exact anchors)
    wp01_action_callbacks.py fixture <generated copy> <out.cpp>         extract callback bodies for the test
    wp01_action_callbacks.py fixture-motion <generated copy> <out.cpp>  same plus _SET_MOS behind three test hooks
    wp01_action_callbacks.py audit <ps2/src> [file ...]                  list same-statement stack++/stack uses
    wp01_action_callbacks.py asset <act_script dir>                       check retail call shapes of ext 33/43/45/46

Each anchor is count-checked so source drift stops the build instead of leaving the defect behind.
"""

import pathlib
import re
import sys

RULES = {
    "src/actscript.cpp": [
        (r"^([ \t]*)SetStack\(stack, static_cast<int>\(pause->pause_flag & GetStackInt\(stack\+\+\)\)\);$",
         r"\1int mask = GetStackInt(stack++);\n\1SetStack(stack, static_cast<int>(pause->pause_flag & mask));", 1),
        (r"^([ \t]*)SetStack\(stack, PadCtrl\.Btn\(GetStackInt\(stack\+\+\)\)\);$",
         r"\1int button = GetStackInt(stack++);\n\1SetStack(stack, PadCtrl.Btn(button));", 1),
    ],
}

FIXTURE_FUNCTIONS = ("GetStackInt", "GetStackFloat", "GetStackString", "SetStack", "_CHECK_PAUSE", "_GET_BTN", "_GET_PAD_HISTORY",
                     "_RESET_PAD_HISTORY", "_GET_ACUMU_PAD", "_RESET_ACUMU_PAD", "_PROG_SET", "_PROG_GET",
                     "_GET_ATTK_TYPE", "_GET_MOVE_TYPE", "_SET_MOVE_SPEED")


def transform(key, text):
    if key not in RULES:
        raise ValueError(f"no host action callback rules for {key}")
    for pattern, replacement, count in RULES[key]:
        found = len(re.findall(pattern, text, flags=re.MULTILINE))
        if found != count:
            raise ValueError(f"{key}: expected {count} callback site(s), found {found}")
        text = re.sub(pattern, replacement, text, count=count, flags=re.MULTILINE)
    return text


def scan_body_end(text, opening):
    depth = 0
    state = None
    index = opening
    while index < len(text):
        char = text[index]
        following = text[index + 1] if index + 1 < len(text) else ""
        if state == "line":
            if char == "\n":
                state = None
        elif state == "block":
            if char == "*" and following == "/":
                state = None
                index += 1
        elif state in ('"', "'"):
            if char == "\\":
                index += 1
            elif char == state:
                state = None
        elif char == "/" and following == "/":
            state = "line"
        elif char == "/" and following == "*":
            state = "block"
        elif char in ('"', "'"):
            state = char
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return index + 1
        index += 1
    raise ValueError("unclosed function body")


def extract_functions(text, name):
    """Every definition of `name` (overloads included), signature through closing brace."""
    pattern = re.compile(r"^(?:static )?(?:(?:int|void|float) |char ?\*)" + re.escape(name) + r"\([^;{]*\)\s*\{", re.MULTILINE)
    found = []
    for match in pattern.finditer(text):
        opening = match.end() - 1
        found.append(text[match.start():scan_body_end(text, opening)])
    if not found:
        raise ValueError(f"src/actscript.cpp: no definition of {name}")
    return found


# Test-only seams for the motion callback: CActionChara's virtual motion methods need a constructed character
# hierarchy, so the consumer fixture routes just these three member calls to observable hooks. Each anchor is
# count-checked; every other statement of the production body is untouched.
MOTION_SEAMS = (
    ("target->SetMotion(motion, flag, 1);", "PortWp01MotionHook(target, motion, flag);"),
    ("target->SetStep(speed);", "PortWp01StepHook(target, speed);"),
    ("target = target->SearchChara(chara_name);", "target = PortWp01SearchHook(target, chara_name);"),
)


def fixture_source(text, motion=False):
    # Passive scene observations are outside the standalone callback contract.
    text = "".join(line for line in text.splitlines(keepends=True)
                   if not line.lstrip().startswith(("port_combat_trace::Record(4,", "port_combat_trace::Record(5,")))
    functions = FIXTURE_FUNCTIONS + (("_SET_MOS",) if motion else ())
    out = ['#include "actscript.hpp"\n#include "actionchara.hpp"\n#include "padcontrol.hpp"\n'
           '#include "dng_main.hpp"\n#include "mainloop.hpp"\n\n']
    for name in functions:
        defs = extract_functions(text, name)
        expected = 2 if name == "SetStack" else 1
        if len(defs) != expected:
            raise ValueError(f"src/actscript.cpp: expected {expected} definition(s) of {name}, found {len(defs)}")
        if motion and name == "_SET_MOS":
            body = defs[0]
            for old, new in MOTION_SEAMS:
                if body.count(old) != 1:
                    raise ValueError(f"src/actscript.cpp: expected one `{old}` in _SET_MOS, found {body.count(old)}")
                body = body.replace(old, new)
            defs = [body]
            out.append("void PortWp01MotionHook(CActionChara *, char *, int);\n"
                       "void PortWp01StepHook(CActionChara *, float);\n"
                       "CActionChara *PortWp01SearchHook(CActionChara *, char *);\n\n")
        out.extend(d.replace("static ", "", 1) if name[0] == "_" else d for d in defs)
        out.append("\n\n")
    # Tests look the production bodies up by name; the file-local helpers stay private to the extract.
    entries = ", ".join('{"%s", &%s}' % (n, n) for n in functions if n[0] == "_")
    out.append("struct PortWp01Entry { const char *name; int (*fn)(RS_STACKDATA *, int); };\n"
               "extern const PortWp01Entry kPortWp01Callbacks[] = {" + entries + ", {nullptr, nullptr}};\n")
    return "".join(out)


def audit(paths):
    """Statements that both post-increment a stack cursor and read it again: evaluation-order hazards."""
    findings = []
    for path in paths:
        text = pathlib.Path(path).read_bytes().decode("latin-1")
        for match in re.finditer(r"[^;{}]*\b(\w+)\+\+[^;{}]*;", text):
            statement = match.group(0)
            var = match.group(1)
            if var not in ("stack", "next", "slot", "args", "sp"):
                continue
            uses = len(re.findall(r"\b" + var + r"\b", statement))
            if uses >= 2 and "for (" not in statement and not re.search(
                    r"\b" + var + r"\+\+\s*[,;)]\s*\w*\s*=", statement):
                findings.append((pathlib.Path(path).name, text.count("\n", 0, match.start()) + 1,
                                 " ".join(statement.split())))
    return findings


def asset_shapes(directory):
    """Retail argument shapes the callbacks must honour: (ext, argc, operand kinds) -> site count per program."""
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
    import wp02_action_vm as vm

    expected = {43: (2, ("PUSH_CONST", "LOAD_ADDR")), 45: (1, ("LOAD_ADDR",)), 46: (0, ()),
                33: (2, ("PUSH_CONST", "LOAD_ADDR"))}
    report = {}
    for path in sorted(pathlib.Path(directory).glob("*.stb")):
        program = vm.Program(path.read_bytes())
        if program.version != 2:
            continue  # STB programs belong to other script tables
        for _, number, argc, operands in vm.ext_sites(program, set(expected)):
            want_argc, want_kinds = expected[number]
            kinds = tuple(o[0] for o in operands)
            if argc != want_argc or kinds != want_kinds:
                raise ValueError(f"{path.name}: ext {number} has argc {argc} {kinds}, expected {want_argc} {want_kinds}")
            report[(path.name, number)] = report.get((path.name, number), 0) + 1
    return report


def main(argv):
    if len(argv) == 4 and argv[1] == "unit":
        key, path = argv[2], pathlib.Path(argv[3])
        original = path.read_bytes()
        updated = transform(key, original.decode("latin-1")).encode("latin-1")
        if original != updated:
            path.write_bytes(updated)
        return
    if len(argv) == 4 and argv[1] in ("fixture", "fixture-motion"):
        source, destination = pathlib.Path(argv[2]), pathlib.Path(argv[3])
        updated = fixture_source(source.read_bytes().decode("latin-1"), argv[1] == "fixture-motion").encode("latin-1")
        destination.parent.mkdir(parents=True, exist_ok=True)
        if not destination.exists() or destination.read_bytes() != updated:
            destination.write_bytes(updated)
        return
    if len(argv) >= 3 and argv[1] == "audit":
        root = pathlib.Path(argv[2])
        names = argv[3:] or ["actscript.cpp", "effscript.cpp", "runscript_opcodes.cpp", "event_func.cpp"]
        for name, line, statement in audit([root / n for n in names]):
            print(f"{name}:{line}: {statement}")
        return
    if len(argv) == 3 and argv[1] == "asset":
        report = asset_shapes(argv[2])
        if not any(number == 43 for _, number in report):
            raise ValueError("no _GET_BTN site found in the supplied programs")
        for (name, number), count in sorted(report.items()):
            print(f"{name} ext {number}: {count} site(s) match")
        return
    raise SystemExit(__doc__)


if __name__ == "__main__":
    try:
        main(sys.argv)
    except (OSError, ValueError) as error:
        raise SystemExit(str(error))
