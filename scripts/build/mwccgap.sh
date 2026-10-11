#!/bin/sh
# Compile one game unit with MWCC through Satan's Fiddle, postprocess its
# object into retail's section names, flags and alignment, and leave a depfile
# Ninja can read.
#
#   mwccgap.sh <object> <depfile> <source> [mwcc args...]
#
# `-lang` selects the language for the source, and the trailing arguments are
# the compiler's flags, passed through in order.
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
if [ ! -f "$MWCCGAP_DIR/mwccgap/elf.py" ]; then
    echo "$0: $MWCCGAP_DIR is empty -- run: git submodule update --init" >&2
    exit 1
fi

: "${MW_DIR:=tools/compilers/mw/3.0-011126}"
# mwcc's <> search list.
: "${LIB_INCLUDE_DIRS:=ps2/include/std;ps2/include/sce}"

# The translation unit's identity is its source file name.
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
scripts/build/satansfiddle-wibo.py "$MW_DIR/mwccps2.exe" -c -lang "$lang" "$@" \
    -o "$obj" "$src" < /dev/null

python3 scripts/build/postprocess_object.py "$obj"

# MWCC writes its dependency map to `<stem>.d` in the working directory. The
# map is rewritten with absolute POSIX paths and the object as its target.
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
