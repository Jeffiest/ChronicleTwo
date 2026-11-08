# The web preset's toolchain file: finds Emscripten and hands over to its own toolchain file.
#
# EMSCRIPTEN_ROOT names the emscripten directory (the one holding emcc and cmake/Modules/Platform/Emscripten.cmake):
# an emsdk's upstream/emscripten, or a release tarball unpacked anywhere. Unset, EMSDK's upstream/emscripten is
# taken, then the directory of the emcc on PATH. `emcmake cmake --preset web` works too: emcmake's own
# -DCMAKE_TOOLCHAIN_FILE takes precedence over the preset's.

set(_ct_emscripten_root "$ENV{EMSCRIPTEN_ROOT}")
if(NOT _ct_emscripten_root AND DEFINED ENV{EMSDK})
    set(_ct_emscripten_root "$ENV{EMSDK}/upstream/emscripten")
endif()
if(NOT _ct_emscripten_root)
    find_program(_ct_emcc NAMES emcc emcc.py)
    if(_ct_emcc)
        get_filename_component(_ct_emcc "${_ct_emcc}" REALPATH)
        get_filename_component(_ct_emscripten_root "${_ct_emcc}" DIRECTORY)
    endif()
endif()
set(_ct_emscripten_toolchain "${_ct_emscripten_root}/cmake/Modules/Platform/Emscripten.cmake")
if(NOT EXISTS "${_ct_emscripten_toolchain}")
    message(FATAL_ERROR "Emscripten not found: set EMSCRIPTEN_ROOT to the emscripten directory (emsdk's "
        "upstream/emscripten), or EMSDK, or put emcc on PATH (looked at '${_ct_emscripten_toolchain}')")
endif()
include("${_ct_emscripten_toolchain}")
