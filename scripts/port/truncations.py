#!/usr/bin/env python3
"""List every cast of a pointer to an integer narrower than a pointer in the port's build.

    truncations.py [--build port/build/pc] [--libclang PATH] [--out docs/port/truncations.md]
                   [--json FILE] [--jobs N] [--only REGEX] [--check]

The game casts pointers to int (MWCC's 32-bit ABI). The port builds for a 64-bit host, where the
heap and stack pointers do not fit those ints, so every cast whose value comes back as a pointer and
can run has to be widened in port/src or replaced by a PORT_OVERRIDE definition. This tool finds the
casts with libclang, using the compile commands of the port's build (CMAKE_EXPORT_COMPILE_COMMANDS is
on), and sorts them. A ps2/src unit is read as it is, not as the copy the port compiles without the
definitions port/src overrides, so the bodies port/src replaces are listed too:

- round-trip: the value comes back as a pointer: cast back, stored or passed into an int that is
  cast back somewhere (followed across units by USR), returned from a function whose result is
  cast back, or an absolute address added to a pointer as an offset.
- low-bits: only the low bits matter: masks, modulo, shifts, a difference of two truncated
  pointers, a truthiness test, a pointer field that holds a file offset added to a base.
- escapes: stored where the tool cannot follow it (memory, an aggregate, an unknown callee) and
  never seen cast back; read by hand.
- resolved: a round trip whose value only becomes a pointer again in replaced functions.

Each site carries the pointer's origin where it can be derived (image, stack, arena, parameter,
field, ...) and its state: replaced when the enclosing function's signature is listed in the build's
port_overrides.txt (a PORT_OVERRIDE definition in port/src replaces it), open otherwise.

The mask literals of the PS2's address aliases (0x0FFFFFFF, 0x20000000) are recorded on the cast
rows whose expression they mask or set. A text scan of ps2/src and port/src lists every line holding
one of them, marking the lines that also hold a truncating cast.

The analysis reads source only, so the build need not be a Debug one; it needs the compile commands
of a configured build (port/build/pc, or any other port build directory through --build).

An open round trip in linked code (its function is defined in the build's darkchronicle, per nm) is
assigned the phase whose work package replaces that function: PHASE_OF maps (unit, function) to it.
--check fails on an open round trip in linked code that PHASE_OF does not cover, and on a PHASE_OF entry
that matches no open round trip.

Needs the clang Python bindings: `pip install libclang` (bundles the library) or the `clang`
package with a system libclang, found through --libclang, LIBCLANG or the usual LLVM paths.
"""

import argparse
import collections
import functools
import glob
import json
import multiprocessing
import os
import re
import shlex
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SOURCE_DIRS = ("ps2/src/", "port/src/", "ps2/include/", "port/include/")

ci = None

# Functions and fields whose pointers point into the game's mgCMemory buffers (ps2/include/mg_memory.hpp).
ARENA_CALLS = {"Alloc", "stAlloc", "stAlloc64", "stAllocTest", "stGetTop", "StartStackMode", "mgCopyString"}
ARENA_FIELDS = {"heap", "heap_top", "stack", "stack_bytes", "stack_block"}

# The PS2's address aliases: 0x0FFFFFFF keeps the physical address, 0x20000000 selects the uncached alias.
ADDRESS_MASKS = {0x0FFFFFFF, 0x20000000}
ADDRESS_MASK_TEXT = re.compile(r"0x0*(?:FFFFFFF|20000000)\b", re.IGNORECASE)
TRUNCATING_CAST_TEXT = re.compile(r"\(\s*(?:u_int|int|s32|u32|unsigned\s+int|signed\s+int)\s*\)")

# Open round trips in linked code, by (unit, function), each the phase whose work package replaces that function
# (docs/PC_PORT_PLAN.md).
PHASE_OF = {
    ("ps2/src/ezmidi.cpp", "ezTransToIOP2"): "Phase 5",
    ("ps2/src/mg_sprite.cpp", "mgC3DSprite::Draw"): "Phase 4",
    ("ps2/src/mg_sprite.cpp", "mgCSprite::Draw"): "Phase 4",
    ("ps2/src/mg_visual.cpp", "mgCVisualFixMDT::DataAssignMDT"): "Phase 4",
    ("ps2/src/runscript_opcodes.cpp", "_SET_INDEXOBJ_SIZE"): "Phase 7",
    ("ps2/src/runscript_opcodes.cpp", "_GET_INDEXOBJ_SIZE"): "Phase 7",
    ("ps2/src/vlgr_info.cpp", "niNPC_INFO_NUM"): "Phase 7",
    ("ps2/src/water.cpp", "CWater::Draw"): "Phase 4",
}


def load_libclang(explicit):
    global ci
    import clang.cindex as cindex

    ci = cindex
    candidates = [explicit, os.environ.get("LIBCLANG")]
    try:
        libdir = subprocess.run(["llvm-config", "--libdir"], capture_output=True, text=True).stdout.strip()
        candidates += glob.glob(os.path.join(libdir, "libclang*.so*")) if libdir else []
    except OSError:
        pass
    candidates += sorted(glob.glob("/usr/lib/llvm-*/lib/libclang-*.so*"), reverse=True)
    candidates += ["/opt/homebrew/opt/llvm/lib/libclang.dylib", "/usr/local/opt/llvm/lib/libclang.dylib"]
    for candidate in candidates:
        if candidate and os.path.exists(candidate):
            try:
                cindex.Config.set_library_file(candidate)
                cindex.Index.create()
                return
            except Exception:
                cindex.Config.loaded = False
    cindex.Index.create()  # the bundled library of `pip install libclang`


def canon(path):
    # realpath stats every component, which on a network or container mount costs milliseconds per call and
    # dominated the run. Paths here are compile-command or repo paths, so lexical normalisation is enough.
    return os.path.normpath(os.path.abspath(path))


def rel(path):
    path = canon(path)
    return os.path.relpath(path, ROOT) if path.startswith(ROOT + os.sep) else path


def ours(path):
    return path is not None and rel(path).startswith(SOURCE_DIRS)


def is_cast(cursor):
    K = ci.CursorKind
    return cursor.kind in (K.CSTYLE_CAST_EXPR, K.CXX_STATIC_CAST_EXPR, K.CXX_REINTERPRET_CAST_EXPR,
                           K.CXX_FUNCTIONAL_CAST_EXPR)


def pointerish(type_):
    T = ci.TypeKind
    kind = type_.get_canonical().kind
    return kind in (T.POINTER, T.CONSTANTARRAY, T.INCOMPLETEARRAY, T.VARIABLEARRAY, T.DEPENDENTSIZEDARRAY,
                    T.FUNCTIONPROTO, T.FUNCTIONNOPROTO, T.NULLPTR)


def integral(type_):
    T = ci.TypeKind
    canonical = type_.get_canonical()
    if canonical.kind == T.ENUM:
        return True
    return canonical.kind in (T.CHAR_U, T.UCHAR, T.CHAR16, T.CHAR32, T.USHORT, T.UINT, T.ULONG, T.ULONGLONG,
                              T.CHAR_S, T.SCHAR, T.WCHAR, T.SHORT, T.INT, T.LONG, T.LONGLONG, T.BOOL)


def narrow(type_):
    return integral(type_) and 0 < type_.get_canonical().get_size() < 8


def operand(cast):
    children = [c for c in cast.get_children() if c.kind != ci.CursorKind.TYPE_REF and
                not c.kind.is_reference() and c.kind != ci.CursorKind.NAMESPACE_REF]
    exprs = [c for c in children if c.kind.is_expression()]
    return exprs[-1] if exprs else None


def peel(cursor):
    K = ci.CursorKind
    while cursor is not None and cursor.kind in (K.PAREN_EXPR, K.UNEXPOSED_EXPR):
        children = list(cursor.get_children())
        if len(children) != 1:
            break
        cursor = children[0]
    return cursor


def text(cursor, limit=120):
    tokens = [t.spelling for t in cursor.get_tokens()]
    joined = " ".join(tokens)
    joined = re.sub(r" ?([()\[\],.;]) ?", r"\1", joined).replace("-> ", "->").replace(" ->", "->")
    return joined if len(joined) <= limit else joined[: limit - 3] + "..."


def unary_op(cursor):
    tokens = [t.spelling for t in cursor.get_tokens()]
    return tokens[0] if tokens else ""


def binop(cursor):
    try:
        return cursor.binary_operator.name
    except Exception:
        return "Unknown"


def address_mask(cursor):
    """The address-alias literal an operand is, as text, or None."""
    cursor = peel(cursor)
    if cursor is None or cursor.kind != ci.CursorKind.INTEGER_LITERAL:
        return None
    tokens = [t.spelling for t in cursor.get_tokens()]
    if not tokens:
        return None
    try:
        value = int(tokens[0].rstrip("uUlL"), 0)
    except ValueError:
        return None
    return "0x%08X" % value if value in ADDRESS_MASKS else None


def is_global_var(decl):
    K = ci.CursorKind
    if decl.kind != K.VAR_DECL:
        return False
    if decl.storage_class in (ci.StorageClass.STATIC, ci.StorageClass.EXTERN):
        return True
    parent = decl.semantic_parent
    return parent is not None and parent.kind in (K.TRANSLATION_UNIT, K.NAMESPACE, K.STRUCT_DECL,
                                                   K.CLASS_DECL, K.LINKAGE_SPEC)


def origin(cursor, depth=0):
    """Where the pointer the cursor evaluates to points."""
    K = ci.CursorKind
    cursor = peel(cursor)
    if cursor is None or depth > 4:
        return "unknown"
    kind = cursor.kind
    if kind == K.STRING_LITERAL:
        return "image (literal)"
    if kind == K.CXX_THIS_EXPR:
        return "this"
    if is_cast(cursor):
        inner = operand(cursor)
        return origin(inner, depth + 1) if inner is not None and pointerish(inner.type) else "integer"
    if kind == K.UNARY_OPERATOR and unary_op(cursor) == "&":
        inner = [c for c in cursor.get_children()]
        return address_of(inner[0], depth) if inner else "unknown"
    if kind == K.BINARY_OPERATOR and binop(cursor) in ("Add", "Sub"):
        for child in cursor.get_children():
            if pointerish(child.type):
                return origin(child, depth + 1)
    if kind == K.ARRAY_SUBSCRIPT_EXPR:
        # An element that is itself a pointer was loaded from memory; an array row decays in place.
        if cursor.type.get_canonical().kind == ci.TypeKind.POINTER:
            return "field"
        return origin(next(cursor.get_children()), depth + 1)
    if kind == K.UNARY_OPERATOR and unary_op(cursor) == "*":
        return "field"
    if kind == K.CONDITIONAL_OPERATOR:
        parts = list(cursor.get_children())[1:]
        kinds = {origin(p, depth + 1) for p in parts}
        return kinds.pop() if len(kinds) == 1 else "mixed"
    if kind == K.CALL_EXPR:
        name = cursor.spelling or "?"
        return "arena (%s)" % name if name in ARENA_CALLS else "call (%s)" % name
    if kind == K.DECL_REF_EXPR:
        decl = cursor.referenced
        if decl is None:
            return "unknown"
        if decl.kind == K.FUNCTION_DECL or decl.kind == K.CXX_METHOD:
            return "image (function)"
        if decl.kind == K.PARM_DECL:
            return "parameter"
        if decl.kind == K.VAR_DECL:
            array = decl.type.get_canonical().kind in (ci.TypeKind.CONSTANTARRAY, ci.TypeKind.INCOMPLETEARRAY)
            if decl.spelling in ARENA_FIELDS:
                return "arena (%s)" % decl.spelling
            if is_global_var(decl):
                return "image" if array else "global pointer"
            if array:
                return "stack"
            init = [c for c in decl.get_children() if c.kind.is_expression()]
            if init:
                inner = origin(init[-1], depth + 1)
                return inner if inner != "unknown" else "local pointer"
            return "local pointer"
        return "unknown"
    if kind == K.MEMBER_REF_EXPR:
        decl = cursor.referenced
        if (decl is not None and decl.spelling in ARENA_FIELDS) or cursor.spelling in ARENA_FIELDS:
            return "arena (%s)" % cursor.spelling
        if decl is not None and decl.type.get_canonical().kind in (ci.TypeKind.CONSTANTARRAY,):
            children = list(cursor.get_children())
            return "member array of " + (origin_of_object(children[0], depth) if children else "this")
        return "field"
    return "unknown"


def origin_of_object(cursor, depth):
    """Where an object expression (the left of a member access) lives."""
    cursor = peel(cursor)
    K = ci.CursorKind
    if cursor is None:
        return "unknown"
    if cursor.kind == K.DECL_REF_EXPR and cursor.referenced is not None:
        decl = cursor.referenced
        if decl.kind == K.VAR_DECL:
            if pointerish(decl.type):
                return origin(cursor, depth + 1)
            return "image" if is_global_var(decl) else "stack"
        if decl.kind == K.PARM_DECL:
            return "parameter"
    if cursor.kind == K.CXX_THIS_EXPR:
        return "this"
    return origin(cursor, depth + 1)


def address_of(cursor, depth):
    K = ci.CursorKind
    cursor = peel(cursor)
    if cursor is None:
        return "unknown"
    if cursor.kind == K.DECL_REF_EXPR and cursor.referenced is not None:
        decl = cursor.referenced
        if decl.kind in (K.FUNCTION_DECL, K.CXX_METHOD):
            return "image (function)"
        if decl.kind == K.VAR_DECL:
            if decl.spelling in ARENA_FIELDS:
                return "image (%s)" % decl.spelling
            return "image" if is_global_var(decl) else "stack"
        if decl.kind == K.PARM_DECL:
            return "stack (parameter)"
    if cursor.kind == K.ARRAY_SUBSCRIPT_EXPR:
        return origin(next(cursor.get_children()), depth + 1)
    if cursor.kind == K.MEMBER_REF_EXPR:
        children = list(cursor.get_children())
        if not children:
            return "this"
        base = peel(children[0])
        if base is not None and pointerish(base.type):
            return origin(base, depth + 1)
        return origin_of_object(base, depth)
    if cursor.kind == K.UNARY_OPERATOR and unary_op(cursor) == "*":
        return origin(next(cursor.get_children()), depth + 1)
    return "unknown"


def refs(cursor):
    """USRs of the integer-typed variables, fields and calls an expression reads."""
    K = ci.CursorKind
    found = set()
    stack = [cursor]
    while stack:
        node = stack.pop()
        if node.kind in (K.DECL_REF_EXPR, K.MEMBER_REF_EXPR):
            decl = node.referenced
            if decl is not None and decl.kind in (K.VAR_DECL, K.PARM_DECL, K.FIELD_DECL) and integral(decl.type):
                usr = decl.get_usr()
                if usr:
                    found.add(usr)
        elif node.kind == K.CALL_EXPR:
            decl = node.referenced
            if decl is not None and integral(node.type):
                usr = decl.get_usr()
                if usr:
                    found.add("ret:" + usr)
        stack.extend(node.get_children())
    return found


def target_of(cursor):
    """The USR an assignment's left side names, or None for memory the tool cannot follow."""
    K = ci.CursorKind
    cursor = peel(cursor)
    if cursor is None:
        return None
    if cursor.kind in (K.DECL_REF_EXPR, K.MEMBER_REF_EXPR):
        decl = cursor.referenced
        if decl is not None and decl.kind in (K.VAR_DECL, K.PARM_DECL, K.FIELD_DECL):
            return decl.get_usr() or None
    if cursor.kind == K.ARRAY_SUBSCRIPT_EXPR:
        base = peel(next(cursor.get_children()))
        if base is not None and base.kind in (K.DECL_REF_EXPR, K.MEMBER_REF_EXPR):
            decl = base.referenced
            if decl is not None and decl.type.get_canonical().kind == ci.TypeKind.CONSTANTARRAY:
                return decl.get_usr() or None
    return None


def callee_param(call, argument):
    decl = call.referenced
    if decl is None:
        return None, call.spelling
    args = list(call.get_arguments())
    index = next((i for i, a in enumerate(args) if a == argument), None)
    params = list(decl.get_arguments()) if decl.kind in (ci.CursorKind.FUNCTION_DECL, ci.CursorKind.CXX_METHOD,
                                                         ci.CursorKind.CONSTRUCTOR) else []
    if index is None or index >= len(params):
        return None, decl.spelling
    return params[index].get_usr() or None, decl.spelling


def contains_truncation(cursor):
    stack = [cursor]
    while stack:
        node = stack.pop()
        if is_cast(node) and narrow(node.type):
            inner = operand(node)
            if inner is not None and pointerish(inner.type):
                return True
        stack.extend(node.get_children())
    return False


def narrowed_later(ancestors):
    """A pointer cast to a full-width integer that an implicit conversion then narrows."""
    K = ci.CursorKind
    for parent in reversed(ancestors):
        if parent.kind in (K.PAREN_EXPR, K.UNEXPOSED_EXPR) or is_cast(parent) or (
                parent.kind == K.BINARY_OPERATOR and binop(parent) in ("Add", "Sub", "And", "Or", "Xor", "Shr",
                                                                        "Shl", "Rem", "Mul", "Div")):
            if integral(parent.type) and narrow(parent.type):
                return True
            if not integral(parent.type):
                return False
            continue
        if parent.kind == K.VAR_DECL or (parent.kind == K.BINARY_OPERATOR and binop(parent) == "Assign"):
            return narrow(parent.type)
        return False
    return False


def context(cast, ancestors, function_usr, ops):
    """Where the truncated value goes: (kind, detail, usr or None); ops collects the operators on the way."""
    K = ci.CursorKind
    node = cast
    for parent in reversed(ancestors):
        kind = parent.kind
        if kind in (K.PAREN_EXPR, K.UNEXPOSED_EXPR):
            node = parent
            continue
        if is_cast(parent):
            if pointerish(parent.type):
                return "cast back", text(parent, 60), None
            if integral(parent.type):
                node = parent
                continue
            return "converted", parent.type.spelling, None
        if kind == K.BINARY_OPERATOR:
            op = binop(parent)
            children = list(parent.get_children())
            other = children[1] if children and children[0] == node else (children[0] if children else None)
            if op in ("Add", "Sub") and other is not None and pointerish(other.type):
                return "pointer offset", text(parent, 60), None
            if op == "Sub" and other is not None and contains_truncation(other):
                return "difference", "", None
            if op in ("And", "Or"):
                mask = address_mask(other)
                if mask is not None:
                    ops.append("mask " + mask)
            if op in ("And", "Rem", "Shr", "Shl"):
                ops.append(op)
                node = parent
                continue
            if op in ("Add", "Sub", "Or", "Xor", "Mul", "Div"):
                ops.append(op)
                node = parent
                continue
            if op in ("LT", "GT", "LE", "GE", "EQ", "NE"):
                if other is not None and contains_truncation(other):
                    return "compare truncated", op, None
                return "compare", op, None
            if op in ("LAnd", "LOr"):
                return "test", "", None
            if op == "Assign":
                if children and children[0] == node:
                    return "other", "assigned to", None
                usr = target_of(children[0])
                return ("store", text(children[0], 60), usr) if usr else ("store to memory", text(children[0], 60), None)
            if op == "Comma":
                return "discarded", "", None
            return "other", op, None
        if kind == K.COMPOUND_ASSIGNMENT_OPERATOR:
            children = list(parent.get_children())
            if children and pointerish(children[0].type):
                return "pointer offset", text(parent, 60), None
            usr = target_of(children[0]) if children else None
            return ("store", text(children[0], 60), usr) if usr else ("store to memory", text(parent, 60), None)
        if kind == K.ARRAY_SUBSCRIPT_EXPR:
            return "index", text(parent, 60), None
        if kind == K.VAR_DECL:
            return "store", parent.spelling, parent.get_usr() or None
        if kind == K.CALL_EXPR:
            usr, name = callee_param(parent, node)
            return ("argument", name, usr) if usr else ("argument to unknown", name or "?", None)
        if kind == K.RETURN_STMT:
            return "return", "", "ret:" + function_usr if function_usr else None
        if kind in (K.IF_STMT, K.WHILE_STMT, K.DO_STMT, K.FOR_STMT, K.SWITCH_STMT):
            return "test", "", None
        if kind == K.CONDITIONAL_OPERATOR:
            children = list(parent.get_children())
            if children and children[0] == node:
                return "test", "", None
            node = parent
            continue
        if kind == K.UNARY_OPERATOR:
            op = unary_op(parent)
            if op == "!":
                return "test", "", None
            node = parent
            continue
        if kind in (K.INIT_LIST_EXPR, K.CXX_CONSTRUCT_EXPR):
            return "store to memory", "initialiser", None
        if kind == K.COMPOUND_STMT:
            return "discarded", "", None
        return "other", str(kind).split(".")[-1], None
    return "other", "", None


FUNCTION_KINDS = None


def qualified(cursor):
    parts = []
    while cursor is not None and cursor.kind != ci.CursorKind.TRANSLATION_UNIT:
        if cursor.spelling:
            parts.append(cursor.spelling)
        cursor = cursor.semantic_parent
    return "::".join(reversed(parts))


def signature(function):
    """The function's qualified name and parameter types, spelled as port_overrides.txt spells them."""
    if function is None:
        return ""
    params = ",".join(argument.type.spelling for argument in function.get_arguments())
    return re.sub(r"\s+", "", qualified(function) + "(" + params + ")")


def walk(path, args):
    global FUNCTION_KINDS
    K = ci.CursorKind
    FUNCTION_KINDS = (K.FUNCTION_DECL, K.CXX_METHOD, K.CONSTRUCTOR, K.DESTRUCTOR, K.CONVERSION_FUNCTION,
                      K.FUNCTION_TEMPLATE)
    index = ci.Index.create()
    try:
        tu = index.parse(path, args=args, options=0)
    except ci.TranslationUnitLoadError as error:
        return {"file": path, "error": str(error)}
    errors = [str(d) for d in tu.diagnostics if d.severity >= ci.Diagnostic.Error]
    casts = []
    back = []
    sinks = set()
    edges = []

    def visit(cursor, ancestors, function):
        kind = cursor.kind
        if kind in FUNCTION_KINDS and cursor.is_definition():
            function = cursor
        if is_cast(cursor):
            inner = operand(cursor)
            if inner is not None:
                if narrow(cursor.type) and pointerish(inner.type):
                    record(cursor, inner, ancestors, function)
                elif integral(cursor.type) and pointerish(inner.type) and narrowed_later(ancestors):
                    record(cursor, inner, ancestors, function)
                elif pointerish(cursor.type) and integral(inner.type):
                    read = refs(inner)
                    sinks.update(read)
                    if read and cursor.location.file is not None:
                        back.append({
                            "file": rel(cursor.location.file.name),
                            "line": cursor.location.line,
                            "column": cursor.location.column,
                            "expression": text(cursor),
                            "refs": sorted(read),
                            "function": qualified(function) if function is not None else "(file scope)",
                            "signature": signature(function),
                        })
        elif kind == K.COMPOUND_ASSIGNMENT_OPERATOR or (kind == K.BINARY_OPERATOR and binop(cursor) == "Assign"):
            children = list(cursor.get_children())
            if len(children) == 2 and integral(children[0].type):
                target = target_of(children[0])
                if target:
                    edges.extend((source, target) for source in refs(children[1]))
            if len(children) == 2 and pointerish(children[0].type) and kind == K.COMPOUND_ASSIGNMENT_OPERATOR:
                sinks.update(refs(children[1]))
        elif kind == K.VAR_DECL and integral(cursor.type):
            init = [c for c in cursor.get_children() if c.kind.is_expression()]
            if init and cursor.get_usr():
                edges.extend((source, cursor.get_usr()) for source in refs(init[-1]))
        elif kind == K.CALL_EXPR and cursor.referenced is not None:
            params = list(cursor.referenced.get_arguments()) if cursor.referenced.kind in FUNCTION_KINDS else []
            for argument, param in zip(cursor.get_arguments(), params):
                if integral(param.type) and param.get_usr():
                    edges.extend((source, param.get_usr()) for source in refs(argument))
        elif kind == K.RETURN_STMT and function is not None and integral(function.result_type):
            for child in cursor.get_children():
                edges.extend((source, "ret:" + function.get_usr()) for source in refs(child))
        elif kind == K.BINARY_OPERATOR and binop(cursor) in ("Add", "Sub"):
            children = list(cursor.get_children())
            if len(children) == 2 and any(pointerish(c.type) for c in children):
                for child in children:
                    if integral(child.type) and not contains_truncation(child):
                        sinks.update("offset:" + usr for usr in refs(child))
        ancestors.append(cursor)
        for child in cursor.get_children():
            location = child.location
            if location.file is not None and not ours(location.file.name):
                continue
            visit(child, ancestors, function)
        ancestors.pop()

    def record(cast, inner, ancestors, function):
        location = cast.location
        if location.file is None:
            return
        function_usr = function.get_usr() if function is not None else None
        ops = []
        kind, detail, usr = context(cast, ancestors, function_usr, ops)
        casts.append({
            "file": rel(location.file.name),
            "line": location.line,
            "column": location.column,
            "expression": text(cast),
            "type": cast.type.spelling,
            "origin": origin(inner),
            "context": kind,
            "detail": detail,
            "usr": usr,
            "ops": ops,
            "masks": sorted({op[len("mask "):] for op in ops if op.startswith("mask ")}),
            "function": qualified(function) if function is not None else "(file scope)",
            "signature": signature(function),
            "static": function is not None and function.linkage == ci.LinkageKind.INTERNAL,
        })

    for child in tu.cursor.get_children():
        location = child.location
        if location.file is not None and ours(location.file.name):
            visit(child, [], None)
    return {"file": rel(path), "casts": casts, "back": back, "sinks": sorted(sinks), "edges": edges,
            "errors": errors[:5]}


def unit_entries(build):
    """The compile commands of the game's units and port/src's, each ps2/src unit under its own path."""
    with open(os.path.join(build, "compile_commands.json")) as f:
        entries = json.load(f)
    copies = os.path.join(canon(build), "ps2_src") + os.sep
    for entry in entries:
        path = canon(os.path.join(entry["directory"], entry["file"]))
        if path.startswith(copies):
            args = shlex.split(entry.pop("command")) if "command" in entry else entry["arguments"]
            entry["arguments"] = [a for a in args if canon(os.path.join(entry["directory"], a)) != path]
            entry["file"] = os.path.join(ROOT, "ps2", "src", path[len(copies):])
    return [e for e in entries if rel(e["file"]).startswith(("ps2/src/", "port/src/")) and
            "/tests/" not in e["file"]]


@functools.lru_cache(maxsize=None)
def resource_dir(compiler):
    # One compiler serves every unit; asking it once per unit cost a process spawn each.
    return subprocess.run([compiler, "-print-resource-dir"], capture_output=True, text=True).stdout.strip()


def command_args(entry):
    args = shlex.split(entry["command"]) if "command" in entry else list(entry["arguments"])
    out = []
    skip = False
    for arg in args[1:]:
        if skip:
            skip = False
            continue
        if arg in ("-o", "-c"):
            skip = arg == "-o"
            continue
        if canon(os.path.join(entry["directory"], arg)) == canon(entry["file"]):
            continue
        out.append(arg)
    # libclang needs to find its own resource headers through the same compiler.
    resource = resource_dir(args[0])
    if resource:
        out += ["-resource-dir", resource]
    return out


def init_worker(libclang):
    load_libclang(libclang)


def work(entry):
    os.chdir(entry["directory"])
    return walk(entry["file"], command_args(entry))


def load_overrides(build):
    """The signatures port/src's PORT_OVERRIDE definitions replace, from the build's port_overrides.txt."""
    path = os.path.join(build, "port_overrides.txt")
    try:
        with open(path) as f:
            return {re.sub(r"\s+", "", line) for line in f if line.strip()}
    except OSError:
        sys.exit("truncations.py: %s is missing; build the port first (its port_overrides.txt lists the replacements)"
                 % path)


def classify(site, sinks, reach):
    context = site["context"]
    origin = site["origin"]
    # A mask or a remainder keeps only the low bits; shifts can be undone on the way back.
    masked = any(op in ("And", "Rem") for op in site["ops"])
    if context in ("cast back", "compare"):
        return "round-trip"
    if context == "pointer offset":
        absolute = not (origin in ("field", "integer", "unknown") or origin.startswith("member array"))
        return "round-trip" if absolute else "low-bits"
    if context in ("store", "argument", "return"):
        if site["usr"] and site["usr"] in reach and not masked:
            return "round-trip"
        return "low-bits" if masked else "escapes"
    if context in ("store to memory", "argument to unknown", "other", "converted"):
        return "low-bits" if masked else "escapes"
    return "low-bits"


def reaching(sinks, edges):
    """Every USR whose value flows, through integer assignments, into a cast back to a pointer."""
    backward = collections.defaultdict(set)
    for source, target in edges:
        backward[target].add(source)
    reach = set(s for s in sinks if not s.startswith("offset:"))
    reach.update(s[len("offset:"):] for s in sinks if s.startswith("offset:"))
    stack = list(reach)
    while stack:
        usr = stack.pop()
        for source in backward.get(usr, ()):
            if source not in reach:
                reach.add(source)
                stack.append(source)
    return reach


def carriers(sites, edges):
    """The integers that hold a truncated pointer on its way back, named after the first that took it."""
    forward = collections.defaultdict(set)
    for source, target in edges:
        forward[source].add(target)
    carried = {}
    for site in sites:
        if site["class"] != "round-trip" or not site["usr"]:
            continue
        name = site["detail"] or site["function"]
        stack = [site["usr"]]
        while stack:
            usr = stack.pop()
            if usr in carried:
                continue
            carried[usr] = name
            stack.extend(forward.get(usr, ()))
    return carried


def resolve_at_readers(sites, edges, returns, totals):
    """A stored round trip whose value only becomes a pointer again in replaced functions (their
    replacement recovers the pointer) is harmless: class "resolved"."""
    forward = collections.defaultdict(set)
    for source, target in edges:
        forward[source].add(target)
    for site in sites:
        if site["class"] != "round-trip" or not site["usr"]:
            continue
        closure = set()
        stack = [site["usr"]]
        while stack:
            usr = stack.pop()
            if usr not in closure:
                closure.add(usr)
                stack.extend(forward.get(usr, ()))
        readers = [r for r in returns if closure.intersection(r["refs"])]
        site["readers"] = sorted({r["function"] for r in readers if r["state"] == "open"})
        if readers and all(r["state"] == "replaced" for r in readers):
            totals[(site["class"], site["state"])] -= 1
            site["class"] = "resolved"
            site["detail"] += " (read back only in " + ", ".join(sorted({r["function"] for r in readers})) + ")"
            totals[(site["class"], site["state"])] += 1


def state(site_signature, overrides):
    return "replaced" if site_signature and site_signature in overrides else "open"


def linked_functions(binary):
    """The qualified names of the functions the binary defines."""
    try:
        listing = subprocess.run(["nm", "-C", "--defined-only", binary], capture_output=True, text=True,
                                 check=True).stdout
    except (OSError, subprocess.CalledProcessError) as error:
        sys.exit("truncations.py: cannot list the symbols of %s: %s" % (binary, error))
    names = set()
    for line in listing.splitlines():
        parts = line.split(" ", 2)
        if len(parts) == 3:
            names.add(parts[2].split("(", 1)[0])
    return names


def assign_phases(sites, linked):
    for site in sites:
        if site["class"] != "round-trip" or site["state"] != "open":
            site["phase"] = "-"
        elif site["function"] not in linked and site["function"] != "(file scope)":
            site["phase"] = "not linked"
        else:
            # File-scope initialisers run at start-up, so they are always linked.
            site["phase"] = PHASE_OF.get((site["file"], site["function"]), "unassigned")


def scan_literals(paths):
    """Lines of ps2/src and port/src holding an address-alias literal, and whether they also hold a truncating cast."""
    rows = []
    for base in paths:
        for path in sorted(glob.glob(os.path.join(ROOT, base, "**", "*.[ch]pp"), recursive=True)) + sorted(
                glob.glob(os.path.join(ROOT, base, "**", "*.[ch]"), recursive=True)):
            if "/tests/" in path.replace(os.sep, "/"):
                continue
            try:
                with open(path, encoding="latin-1") as f:
                    lines = f.read().splitlines()
            except OSError:
                continue
            for number, line in enumerate(lines, 1):
                if ADDRESS_MASK_TEXT.search(line):
                    rows.append({"file": rel(path), "line": number, "text": line.strip(),
                                 "cast": TRUNCATING_CAST_TEXT.search(line) is not None})
    return rows


def esc(value):
    return value.replace("|", "\\|")


def row_use(site):
    use = site["context"] + (": " + site["detail"] if site["detail"] else "")
    if site.get("masks"):
        use += " (masked " + ", ".join(site["masks"]) + ")"
    return use


def markdown(sites, totals, returns, literals, build, out):
    lines = []
    w = lines.append
    classes = ["round-trip", "resolved", "escapes", "low-bits"]
    states = ["replaced", "open"]
    build_dir = os.path.relpath(os.path.abspath(build), ROOT).replace(os.sep, "/")

    w("# Pointer truncations")
    w("")
    w("Generated by `scripts/port/truncations.py` from the compile commands, `port_overrides.txt` and the symbols of "
      "`%s/darkchronicle`; do not" % build_dir)
    w("edit by hand. Regenerate with `tools/kazarov/bin/kazarov shell python3 scripts/port/truncations.py --build %s "
      "--out docs/port/truncations.md`." % build_dir)
    w("")
    w("A row is one cast of a pointer to an integer narrower than the pointer (or a full-width integer an implicit")
    w("narrowing then truncates) in `ps2/src`, `port/src` and their headers, grouped by the unit it is read in. The")
    w("host's pointers are 64-bit, so a **round-trip** row whose value is read as a pointer again is a fault waiting")
    w("for a run; each one still open is a place where a ps2/src unit truncates.")
    w("")
    w("- **state**: `replaced` when the enclosing function's signature is listed in the build's `port_overrides.txt`")
    w("  (a `PORT_OVERRIDE` definition in port/src replaces it, so ps2/src's body is never compiled); `open` otherwise.")
    w("- **round-trip**: the value comes back as a pointer (cast back, stored or passed into an int that is cast back")
    w("  somewhere, returned from a function whose result is cast back, or an absolute address added to a pointer).")
    w("- **resolved**: a round trip whose value only becomes a pointer again in replaced functions.")
    w("- **low-bits**: only the low bits are used (masks, modulo, shifts, differences of two truncated pointers,")
    w("  tests, comparisons, a pointer field holding a file offset added to a base).")
    w("- **escapes**: stored into memory, an aggregate or an unknown callee, and never seen cast back; read by hand.")
    w("- **masked**: an address-alias literal (`0x0FFFFFFF`, `0x20000000`) is applied to the value on the way.")
    w("- **phase**: for an open round trip, the phase whose work package replaces its function; `not linked` when the")
    w("  binary does not define it, `unassigned` when `PHASE_OF` lacks it (`--check` fails); `-` for other rows.")
    w("")
    w("Origin is where the pointer points, where the expression shows it: **image** (a global, a literal, a function),")
    w("**stack**, **arena** (an `mgCMemory` buffer, through `Alloc`, `stAlloc64` or its heap and stack fields),")
    w("**parameter**, **field**, **global pointer**, **local pointer**, **call**, **this**.")
    w("")
    w("## Counts")
    w("")
    w("| class | replaced | open | total |")
    w("|---|---|---|---|")
    for cls in classes:
        row = [totals[(cls, s)] for s in states]
        w("| %s | %d | %d | %d |" % (cls, row[0], row[1], sum(row)))
    total_rows = [totals[(c, s)] for c in classes for s in states]
    replaced = sum(totals[(c, "replaced")] for c in classes)
    w("| **all** | %d | %d | %d |" % (replaced, sum(total_rows) - replaced, sum(total_rows)))
    w("")
    open_round_trips = [s for s in sites if s["class"] == "round-trip" and s["state"] == "open"]
    units = collections.Counter(s["file"] for s in open_round_trips)
    w("Open round trips by unit: " + (", ".join("%s %d" % (k, v) for k, v in sorted(units.items())) or "none") + ".")
    w("")
    open_linked = [s for s in sites if s["phase"] not in ("-", "not linked")]
    w("## Open round trips in linked code (%d)" % len(open_linked))
    w("")
    w("Each is assigned the phase whose work package replaces its function (`PHASE_OF` in `scripts/port/truncations.py`).")
    w("")
    w("| unit | line | function | phase | expression |")
    w("|---|---|---|---|---|")
    for s in open_linked:
        w("| %s | %d | %s | %s | `%s` |" % (s["file"], s["line"], esc(s["function"]), s["phase"],
                                           esc(s["expression"]).replace("`", "'")))
    w("")
    w("## Address-alias literals (text scan, %d lines)" % len(literals))
    w("")
    w("Every line of `ps2/src` and `port/src` holding `0x0FFFFFFF` or `0x20000000`. A line marked **cast** also holds")
    w("a `(int)`, `(u_int)`, `(s32)` or `(u32)` cast, which is the truncation the masks act on. The AST rows above carry")
    w("the same literals in their **masked** column when the masked expression holds a pointer cast.")
    w("")
    w("| unit | line | cast | text |")
    w("|---|---|---|---|")
    for row in literals:
        w("| %s | %d | %s | `%s` |" % (row["file"], row["line"], "yes" if row["cast"] else "no",
                                         esc(row["text"][:140]).replace("`", "'")))
    w("")

    by_unit = collections.OrderedDict()
    for site in sites:
        by_unit.setdefault(site["file"], []).append(site)
    for unit, rows in by_unit.items():
        w("## %s (%d rows)" % (unit, len(rows)))
        w("")
        w("| line | function | state | class | expression | origin | use | phase |")
        w("|---|---|---|---|---|---|---|---|")
        for s in rows:
            w("| %d | %s | %s | %s | `%s` | %s | %s | %s |" % (
                s["line"], esc(s["function"]), s["state"], s["class"],
                esc(s["expression"]).replace("`", "'"), esc(s["origin"]), esc(row_use(s)), esc(s["phase"])))
        w("")

    w("## Where round-trip values become pointers again (%d)" % len(returns))
    w("")
    w("Casts from an integer to a pointer that read an integer a round-trip row above stored into, followed through")
    w("integer assignments, arguments and returns. A round trip is fixed at either end: keep the pointer where it is")
    w("stored, or resolve the integer where it comes back.")
    w("")
    w("| site | function | state | expression | carries |")
    w("|---|---|---|---|---|")
    for s in returns:
        w("| %s:%d | %s | %s | `%s` | %s |" % (s["file"], s["line"], esc(s["function"]), s["state"],
                                             esc(s["expression"]).replace("`", "'"), esc(s["carrier"])))
    w("")
    with open(out, "w") as f:
        f.write("\n".join(lines))


def check(sites, totals, returns, literals, build, out):
    import tempfile
    with tempfile.TemporaryDirectory() as scratch:
        fresh = os.path.join(scratch, "truncations.md")
        markdown(sites, totals, returns, literals, build, fresh)
        with open(fresh) as f:
            wanted = f.read()
    try:
        with open(out) as f:
            committed = f.read()
    except OSError:
        committed = None
    status = 0
    if committed != wanted:
        print("truncations.py: %s is stale; run scripts/port/truncations.py and commit it" % rel(out),
              file=sys.stderr)
        status = 1
    for site in sites:
        if site["phase"] == "unassigned":
            print("truncations.py: %s:%d: %s: open round trip in linked code has no phase; add it to PHASE_OF: %s"
                  % (site["file"], site["line"], site["function"], site["expression"]), file=sys.stderr)
            status = 1
    open_keys = {(s["file"], s["function"]) for s in sites if s["class"] == "round-trip" and s["state"] == "open"}
    for key in sorted(PHASE_OF):
        if key not in open_keys:
            print("truncations.py: PHASE_OF entry (%s, %s) matches no open round trip; remove it" % key,
                  file=sys.stderr)
            status = 1
    return status


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--build", default=os.path.join(ROOT, "port/build/pc"))
    parser.add_argument("--libclang")
    parser.add_argument("--out", default=os.path.join(ROOT, "docs/port/truncations.md"))
    parser.add_argument("--json")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    parser.add_argument("--only", help="regular expression over source paths, for a quick run")
    parser.add_argument("--check", action="store_true",
                        help="write nothing; fail if --out differs from what would be written or if a "
                             "round trip is open")
    options = parser.parse_args()

    try:
        load_libclang(options.libclang)
    except ImportError:
        sys.exit("truncations.py: the clang Python bindings are missing (pip install libclang)")
    overrides = load_overrides(options.build)
    entries = unit_entries(options.build)
    if options.only:
        entries = [e for e in entries if re.search(options.only, e["file"])]
    with multiprocessing.Pool(options.jobs, initializer=init_worker, initargs=(options.libclang,)) as pool:
        results = pool.map(work, entries, chunksize=1)

    sinks = set()
    edges = []
    seen = {}
    backs = {}
    for result in results:
        if "error" in result:
            print("truncations.py: %s: %s" % (result["file"], result["error"]), file=sys.stderr)
            continue
        for error in result["errors"]:
            print("truncations.py: %s: %s" % (result["file"], error), file=sys.stderr)
        sinks.update(result["sinks"])
        edges.extend(result["edges"])
        for site in result["casts"]:
            seen.setdefault((site["file"], site["line"], site["column"]), site)
        for site in result["back"]:
            backs.setdefault((site["file"], site["line"], site["column"]), site)
    reach = reaching(sinks, edges)
    sites = sorted(seen.values(), key=lambda s: (s["file"], s["line"], s["column"]))
    totals = collections.Counter()
    for site in sites:
        site["class"] = classify(site, sinks, reach)
        site["state"] = state(site["signature"], overrides)
        totals[(site["class"], site["state"])] += 1
    carried = carriers(sites, edges)
    returns = []
    for site in sorted(backs.values(), key=lambda s: (s["file"], s["line"], s["column"])):
        names = [usr for usr in site["refs"] if usr in carried]
        if names:
            site["carrier"] = ", ".join(sorted({carried[usr] for usr in names}))
            site["state"] = state(site["signature"], overrides)
            returns.append(site)
    resolve_at_readers(sites, edges, returns, totals)
    assign_phases(sites, linked_functions(os.path.join(options.build, "darkchronicle")))
    literals = scan_literals(("ps2/src", "port/src"))
    if options.check:
        return check(sites, totals, returns, literals, options.build, options.out)
    os.makedirs(os.path.dirname(os.path.abspath(options.out)), exist_ok=True)
    markdown(sites, totals, returns, literals, options.build, options.out)
    if options.json:
        with open(options.json, "w") as f:
            json.dump(sites, f, indent=1)
    for cls in ("round-trip", "resolved", "escapes", "low-bits"):
        print("%-10s replaced %4d  open %4d" % (cls, totals[(cls, "replaced")], totals[(cls, "open")]))
    print("%d rows, %d address-alias literal lines" % (len(sites), len(literals)))


if __name__ == "__main__":
    sys.exit(main())
