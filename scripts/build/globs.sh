#!/bin/sh
# Keep <build dir>/globbed.txt -- the sources and headers the build files are
# generated from -- up to date, rewriting it only when a file has been added
# or removed.
#
#   scripts/build/globs.sh <build dir>
#
# ps2/CMakeLists.txt names globbed.txt in CMAKE_CONFIGURE_DEPENDS, so ninja
# regenerates the build files exactly when that set changes. That is what
# CONFIGURE_DEPENDS globs would do, but those re-run on every `cmake --build`
# and print "Re-checking globbed directories..." each time.
set -eu

cd "$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"

dir=$1
mkdir -p "$dir"
out=$dir/globbed.txt
listing=$(
    {
        find ps2/src -type f \( -name '*.cpp' -o -name '*.c' \)
        find ps2/include -type f \( -name '*.hpp' -o -name '*.h' \)
    } 2>/dev/null | LC_ALL=C sort
)
if [ ! -f "$out" ] || [ "$(cat "$out")" != "$listing" ]; then
    printf '%s\n' "$listing" > "$out"
fi
