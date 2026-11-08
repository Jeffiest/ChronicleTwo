# The toolchain this tree is built and checked with on Linux x86-64 (DC2_PLAN.md "Building here").
# One source for a person and for tools/gauntlet/run.py, which reads the CT_* variables and the
# configure flags from this file. Source it, then: ct_configure && ct_build
#
#   . tools/gauntlet/env.sh
#   ct_configure            # cmake --preset linux-x64 with the flags below
#   ct_build [target...]    # ninja in port/build/pc

CT_ROOT="${CT_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/../.." && pwd)}"
CT_BUILD_DIR="${CT_BUILD_DIR:-$CT_ROOT/port/build/pc}"
CT_WEB_BUILD_DIR="${CT_WEB_BUILD_DIR:-$CT_ROOT/port/build/web}"
CT_PRESET="${CT_PRESET:-linux-x64}"
CT_JOBS="${CT_JOBS:-$(nproc 2>/dev/null || echo 4)}"

CT_CC="${CT_CC:-/usr/bin/clang-20}"
CT_CXX="${CT_CXX:-/usr/bin/clang++-20}"
CT_LLD="${CT_LLD:-/usr/bin/ld.lld-20}"
CT_CLANG_VERSION="20"
CT_RUST_VERSION="1.99"
CT_SDL3_VERSION="3.4.18"
CT_SDL3_DIR="${CT_SDL3_DIR:-/usr/local/lib/cmake/SDL3}"
CT_VULKAN_HEADERS="${CT_VULKAN_HEADERS:-/tmp/claude-0/-home-user/14c1cbb1-4170-5568-a0ba-a53f372cbb96/scratchpad/Vulkan-Headers/include}"
CT_VULKAN_MIN="1.4"
CT_FFMPEG_DIR="${CT_FFMPEG_DIR:-/opt/ffmpeg-sys}"
CT_DEPS="${CT_DEPS:-/opt/deps}"
CT_LAVAPIPE_ICD="${CT_LAVAPIPE_ICD:-/usr/share/vulkan/icd.d/lvp_icd.json}"
CT_EMSDK="${CT_EMSDK:-/opt/emsdk-dl/install/emscripten}"
CT_EMCC_VERSION="6.0.13"
EM_CONFIG="${EM_CONFIG:-/root/.emscripten}"
CT_NODE="${CT_NODE:-/opt/node22/bin/node}"
# Agents sharing port/build/pc take turns at ctest through marker files <CT_LOCK_DIR>/ctest-lock.<tag>: a ctest
# stage waits while another tag's file exists and holds its own while it runs (GAUNTLET.md "Agent protocol").
CT_LOCK_DIR="${CT_LOCK_DIR:-/tmp/claude-0/-home-user/14c1cbb1-4170-5568-a0ba-a53f372cbb96/scratchpad}"
CT_LOCK_TAG="${CT_LOCK_TAG:-F}"
export EM_CONFIG

# kazarov_core compiles its cxx.cc with cc-rs outside CMake's flags: without these, dc2data links a
# libstdc++ std::string against the game's libc++ one and fails.
CXXFLAGS="${CXXFLAGS:--stdlib=libc++}"
CXXSTDLIB="${CXXSTDLIB:-c++}"
export CXXFLAGS CXXSTDLIB

# Every -D the PC configure takes beyond the preset, one per line.
CT_CMAKE_FLAGS="
-DCMAKE_C_COMPILER=$CT_CC
-DCMAKE_CXX_COMPILER=$CT_CXX
-DCMAKE_CXX_FLAGS=-stdlib=libc++
-DCMAKE_EXE_LINKER_FLAGS=-stdlib=libc++ -fuse-ld=lld
-DCMAKE_SHARED_LINKER_FLAGS=-stdlib=libc++ -fuse-ld=lld
-DCMAKE_LINKER=$CT_LLD
-DCT_BUILD_TESTS=ON
-DSDL3_DIR=$CT_SDL3_DIR
-DVulkan_INCLUDE_DIR=$CT_VULKAN_HEADERS
-DKZ_FFMPEG_DIR=$CT_FFMPEG_DIR
-DFETCHCONTENT_SOURCE_DIR_CORROSION=$CT_DEPS/corrosion
-DFETCHCONTENT_SOURCE_DIR_SIMDJSON=$CT_DEPS/simdjson
-DFETCHCONTENT_SOURCE_DIR_FASTGLTF=$CT_DEPS/fastgltf
-DFETCHCONTENT_SOURCE_DIR_LUAU=$CT_DEPS/luau
-DFETCHCONTENT_SOURCE_DIR_QUILL=$CT_DEPS/quill
-DFETCHCONTENT_SOURCE_DIR_CPPTRACE=$CT_DEPS/cpptrace
-DFETCHCONTENT_SOURCE_DIR_LIBDWARF=$CT_DEPS/libdwarf
-DFETCHCONTENT_SOURCE_DIR_TRACY=$CT_DEPS/tracy
-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=$CT_DEPS/googletest
"

# Kazarov wants lib{avutil,swresample,swscale,avcodec,avformat}.a under KZ_FFMPEG_DIR/lib and the headers
# under KZ_FFMPEG_DIR/include. Ubuntu's FFmpeg 6.1 ships shared libraries only, so the tree is one-line
# linker scripts (INPUT(/usr/lib/x86_64-linux-gnu/libavformat.so.60)) and an include symlink.
ct_ffmpeg_tree() {
    local libdir=/usr/lib/x86_64-linux-gnu
    local name so
    [ -e "$CT_FFMPEG_DIR/include" ] || { mkdir -p "$CT_FFMPEG_DIR" && ln -s /usr/include/x86_64-linux-gnu "$CT_FFMPEG_DIR/include"; }
    mkdir -p "$CT_FFMPEG_DIR/lib"
    for name in avutil swresample swscale avcodec avformat; do
        [ -e "$CT_FFMPEG_DIR/lib/lib$name.a" ] && continue
        so=$(ls "$libdir/lib$name.so."* 2>/dev/null | grep -E '\.so\.[0-9]+$' | head -1)
        [ -n "$so" ] || { echo "ct_ffmpeg_tree: no $libdir/lib$name.so.N (apt install libavformat-dev and friends)" >&2; return 1; }
        echo "INPUT($so)" > "$CT_FFMPEG_DIR/lib/lib$name.a"
    done
}

ct_configure() {
    ct_ffmpeg_tree || return 1
    local flags=()
    local line
    while IFS= read -r line; do
        [ -n "$line" ] && flags+=("$line")
    done <<< "$CT_CMAKE_FLAGS"
    cmake --preset "$CT_PRESET" -S "$CT_ROOT" -B "$CT_BUILD_DIR" "${flags[@]}" "$@"
}

ct_build() {
    cmake --build "$CT_BUILD_DIR" --parallel "$CT_JOBS" "$@"
}

ct_env() {
    set | grep -E '^(CT_[A-Z0-9_]*|EM_CONFIG|CXXFLAGS|CXXSTDLIB)=' | sort
}
