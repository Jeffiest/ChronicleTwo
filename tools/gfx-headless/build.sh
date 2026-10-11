#!/bin/sh
# Compiles the gfx bring-up check against the dc_host objects of a configured port build tree.
#
# Usage (from the repository root, after configuring and building dc_host):
#   tools/gfx-headless/build.sh port/build/pc [output]
#
# dc_host must have been built first: ninja -C port/build/pc dc_host
set -e
build="${1:?usage: build.sh <build-dir> [output]}"
out="${2:-gfx-headless}"
src="$(dirname "$0")/main.cpp"
cxx="${CXX:-clang++-19}"

objects=$(find "$build/port/CMakeFiles/dc_host.dir" -name '*.o')
[ -n "$objects" ] || { echo "no dc_host objects under $build; build dc_host first" >&2; exit 1; }

# shellcheck disable=SC2086
exec "$cxx" -std=c++26 -g -O0 -I port/src "$src" $objects -lSDL3 -lvulkan -pthread -o "$out"
