#!/bin/sh
# Compile one game unit through tools/mwccgap and leave a depfile Ninja can
# read.
#
#   mwccgap.sh <object> <depfile> <source> [mwcc args...]
#
# No source carries an INCLUDE_ASM or INCLUDE_RODATA marker any more, so
# mwccgap compiles the source as written; the marker pass, and the options
# below that pass reads, stay for the submodule's interface.
# scripts/build/postprocess_object.py then gives every section retail's name,
# type, flags and alignment.
#
# A marker names its file's directory from the repository root
# (`ps2/asm/<region>/nonmatchings/<unit>`), so the prefix below is `.`.
#
# A unit's files are assembled several at a time; MWCCGAP_AS_JOBS says how
# many, and 1 makes it one after another.
#
# The marker pass reads a temporary file named `.c`, so mwcc cannot tell the
# language from the extension and `-lang` is passed explicitly.
#
# Argument order matters. mwccgap takes the two positionals first and passes
# everything it does not recognise to mwcc, and `--as-flags` takes a list, so
# it has to come last or it swallows the compiler's flags.
set -e

obj=$1
dep=$2
src=$3
shift 3

case "$src" in
    *.c) lang=c ;;
    *)   lang=c++ ;;
esac

: "${MWCCGAP_DIR:=tools/mwccgap}"

# A submodule; a clone without --recursive leaves the directory empty and the
# failure is otherwise a confusing "No such file or directory" from python.
if [ ! -f "$MWCCGAP_DIR/mwccgap.py" ]; then
    echo "$0: $MWCCGAP_DIR is empty -- run: git submodule update --init" >&2
    exit 1
fi

: "${MW_DIR:=tools/compilers/mw/3.0-011126}"
: "${MIPS_TOOL_PREFIX:=mips-ps2-decompals-}"
# mwcc's <> search list.
: "${LIB_INCLUDE_DIRS:=ps2/include/std;ps2/include/sce}"

# Both mwccgap passes use the same source identity, including its temporary .c.
: "${SATANSFIDDLE:=satansfiddle}"
: "${SATANSFIDDLE_CONFIG:=$(pwd)/scripts/build/satansfiddle.json}"
export SATANSFIDDLE SATANSFIDDLE_CONFIG
SATANSFIDDLE_TRANSLATION_UNIT=$(basename "$src")
export SATANSFIDDLE_TRANSLATION_UNIT
if ! command -v "$SATANSFIDDLE" >/dev/null 2>&1; then
    echo "mwccgap.sh: Satan's Fiddle is unavailable; build it and set SATANSFIDDLE to its executable." >&2
    exit 1
fi
if [ ! -f "$SATANSFIDDLE_CONFIG" ]; then
    echo "mwccgap.sh: no Satan's Fiddle JSON profile at $SATANSFIDDLE_CONFIG" >&2
    exit 1
fi

mkdir -p "$(dirname "$obj")"

MWCIncludes=$LIB_INCLUDE_DIRS \
PYTHONPATH=$MWCCGAP_DIR \
python3 "$MWCCGAP_DIR/mwccgap.py" "$src" "$obj" \
    --mwcc-path "$MW_DIR/mwccps2.exe" \
    --use-wibo \
    --wibo-path scripts/build/satansfiddle-wibo.py \
    --as-path "${MIPS_TOOL_PREFIX}as" \
    --as-march r5900 \
    --as-mabi eabi \
    --asm-dir-prefix . \
    -lang "$lang" \
    "$@" \
    --as-flags -mno-pdr -non_shared -G0 -Ips2/include < /dev/null

python3 scripts/build/postprocess_object.py "$obj"

# MWCC writes its dependency map to `<stem>.d` in the working directory. The
# first of mwccgap's two compiles reads the real source, so that pass leaves
# the map under the source's stem; the second reads a temporary file and leaves
# one under that name, which is swept up below. The map is rewritten with
# absolute POSIX paths and the object as its target.
base=$(basename "$src")
raw="${base%.*}.d"

if [ -f "$raw" ]; then
    awk -v target="$obj" -v root="$(pwd)" '
        { gsub(/\r/, ""); gsub(/\\$/, ""); line = line " " $0 }
        END {
            sub(/^[^:]*:/, "", line)
            gsub(/\\/, "/", line)
            n = split(line, deps, /[ \t]+/)
            printf "%s/%s:", root, target
            for (i = 1; i <= n; i++) {
                path = deps[i]
                sub(/^[A-Za-z]:\//, "/", path)
                # The compiler wrapper can prepend its temporary object as another
                # dependency-map target. A target is not a source dependency.
                if (path == "" || path ~ /:$/)
                    continue
                if (path !~ /^\//)
                    path = root "/" path
                printf " \\\n\t%s", path
            }
            printf "\n"
        }
    ' "$raw" > "$dep"
    rm -f "$raw"
elif [ -n "$dep" ]; then
    # Without `-gccdep`/`-MD` mwcc writes no map; an empty rule keeps Ninja
    # from rebuilding forever.
    printf '%s:\n' "$(pwd)/$obj" > "$dep"
fi

rm -f tmp*.d
