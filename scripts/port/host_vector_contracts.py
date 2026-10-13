"""Check and widen effect callback buffers passed to four-component vector APIs."""
from pathlib import Path
import re
import sys
from wp01_action_callbacks import extract_functions

RULES = {
    "effscript.cpp": {
        "_SCN_GET_CHR_POS": ("pos",), "_SCN_GET_CHR_ROT": ("rot",),
        "_SCN_GET_ENTRY_OBJ_POS": ("pos",), "_SCN_GET_CHR_ENTOBJ_POS": ("pos",),
        "_DIST_VECTOR": ("vector",), "_DIST_VECTOR2": ("from", "to"),
        "_GET_EOH_POS": ("pos",),
    },
    "event_func.cpp": {
        "_EOH_GET_POS": ("pos",), "_EOH_GET_ROT": ("rot",),
        "_MINIMAP_DOOR_ENABLE": ("pos",), "_POST_TREASURE_BOX": ("position",),
        "_RANDOM_CIRCLE_GET_POS": ("pos",),
    },
}

def verify(text):
    for name in set(re.findall(r"^(?:static )?(?:int|void|float)\s+(_\w+)\([^;{}]*\)\s*\{", text, re.MULTILINE)):
        for body in extract_functions(text, name):
            for var in re.findall(r"\bfloat\s+(\w+)\[3\]", body):
                first = rf"\b(?:GetStackVector|GetPosition|GetRotation|GetWorldPosition0)\(\s*{var}\b"
                last = rf"\b(?:GetEntryObjectPos|GetPos|GetRot)\([^;\n]*,\s*{var}\s*\)"
                if re.search(first, body) or re.search(last, body):
                    raise ValueError(f"{name}: three-float {var} passed to a four-component vector writer")

def transform(text, key="effscript.cpp"):
    text = text.replace("\r\n", "\n")
    for name, buffers in RULES[key].items():
        bodies = extract_functions(text, name)
        if len(bodies) != 1:
            raise ValueError(f"{name}: expected one callback")
        body = bodies[0]
        pattern = r"^([ \t]*)float[ \t]+(" + "|".join(buffers) + r")\[3\];$"
        fixed, count = re.subn(pattern,
                              r"\1alignas(16) float \2[4];", body, flags=re.MULTILINE)
        if count != len(buffers):
            raise ValueError(f"{name}: expected {len(buffers)} three-float API buffers, found {count}")
        text = text.replace(body, fixed, 1)
    verify(text)
    return text

def fixture(text):
    out = ['#include "character.hpp"\n#include "runscript.hpp"\nnamespace {\n'
           'struct ProbeScene { CCharacter2 *chara; CCharacter2 *GetCharacter(int) { return chara; } };\n'
           'ProbeScene scene; ProbeScene *now_scene = &scene;\n']
    for name in ("GetStackInt", "GetStackFloat", "GetStackVector", "SetStack",
                 "_SCN_GET_CHR_ENTOBJ_POS", "_DIST_VECTOR", "_DIST_VECTOR2"):
        out.extend(extract_functions(text, name))
        out.append("\n")
    out.append('}\nint PortEffectEntryPosition(CCharacter2 *chara, RS_STACKDATA *slots, int argc) {\n'
               'scene.chara = chara; return _SCN_GET_CHR_ENTOBJ_POS(slots, argc);\n}\n'
               'int PortEffectDistance(RS_STACKDATA *slots, int argc) { return _DIST_VECTOR(slots, argc); }\n'
               'int PortEffectDistance2(RS_STACKDATA *slots, int argc) { return _DIST_VECTOR2(slots, argc); }\n')
    headers = "\n".join(line for line in text.splitlines() if line.startswith("#include"))
    return headers + "\n" + "\n".join(out)

if __name__ == "__main__":
    mode = sys.argv[1]
    source = Path(sys.argv[2])
    if mode == "unit":
        source.write_text(transform(source.read_text(encoding="utf-8"), source.name), encoding="utf-8")
    elif mode == "fixture":
        Path(sys.argv[3]).write_text(fixture(source.read_text(encoding="utf-8")), encoding="utf-8")
    elif mode == "test":
        for key, rules in RULES.items():
            text = (source / key).read_text(encoding="utf-8")
            fixed = transform(text, key)
            for name in rules:
                assert "[3]" not in "".join(extract_functions(fixed, name))
            for bad in (fixed, text.replace(next(iter(rules)) + "(", "_SOURCE_DRIFT(", 1)):
                try:
                    transform(bad, key)
                except ValueError:
                    continue
                raise SystemExit("negative control escaped vector contract gate")
            mutant = fixed + '\nstatic int _VECTOR_MUTANT(RS_STACKDATA *s, int n) { float v[3]; GetStackVector(v, s); return 1; }\n'
            try:
                verify(mutant)
            except ValueError:
                continue
            raise SystemExit("new undersized vector writer escaped contract gate")
        print("twelve effect/event callback vector contracts, drift, repeated rewrite and new-buffer controls passed")
    else:
        raise SystemExit("expected unit, fixture or test")
