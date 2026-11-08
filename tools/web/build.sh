#!/usr/bin/env bash
# Configures and builds the web build (docs/WEB.md): `tools/web/build.sh [configure|build|all] [targets...]`.
#
# Environment:
#   EMSCRIPTEN_ROOT  the emscripten directory (emsdk's upstream/emscripten); else EMSDK, else emcc on PATH.
#   CT_DEPS_DIR      a directory of pre-cloned dependency sources, one per FetchContent name in lower case
#                    (corrosion, simdjson, fastgltf, luau, quill, googletest, SDL3, vulkanheaders): each present
#                    one is passed as FETCHCONTENT_SOURCE_DIR_<NAME>, for machines that cannot download archives.
#   CT_WEB_JOBS      parallel compile jobs (default: the CPU count). A link runs alone (web.cmake's job pool).
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
mode=${1:-all}
shift || true

if [[ -n "${EMSCRIPTEN_ROOT:-}" ]]; then
    export PATH="$EMSCRIPTEN_ROOT:$PATH"
elif [[ -n "${EMSDK:-}" ]]; then
    export PATH="$EMSDK/upstream/emscripten:$PATH"
fi
command -v emcc > /dev/null || { echo "emcc not found: set EMSCRIPTEN_ROOT or EMSDK" >&2; exit 1; }

deps=()
if [[ -n "${CT_DEPS_DIR:-}" ]]; then
    for dir in "$CT_DEPS_DIR"/*/; do
        [[ -d $dir ]] || continue
        name=$(basename "$dir")
        deps+=("-DFETCHCONTENT_SOURCE_DIR_${name^^}=${dir%/}")
    done
fi

if [[ $mode == configure || $mode == all ]]; then
    (cd "$root" && cmake --preset web "${deps[@]}" "$@")
fi
if [[ $mode == build || $mode == all ]]; then
    jobs=${CT_WEB_JOBS:-$(nproc)}
    targets=("$@")
    [[ ${#targets[@]} -gt 0 ]] || targets=(darkchronicle)
    (cd "$root" && cmake --build --preset web --parallel "$jobs" --target "${targets[@]}")
fi
