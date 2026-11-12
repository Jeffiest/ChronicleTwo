# The browser build (PLATFORM WEB), included by port/CMakeLists.txt with Emscripten's toolchain (docs/WEB.md).
#
# The game and Kazarov's host code build to WebAssembly unchanged where they can:
# - SDL3 is built from source for Emscripten.
# - Browsers have no Vulkan. The renderer compiles against Vulkan's headers, and vulkan/ implements the
#   commands it uses on WebGPU (Emscripten's emdawnwebgpu port), gl/ on WebGL2; weak stubs that fail stand for
#   the rest (gen_vk_stubs.py).
# - The PS2 threads are Emscripten fibers (sce/eekernel_threads.cpp), which need Asyncify.
# - The game runs on a pthread (PROXY_TO_PTHREAD), so its blocking waits do not stall the page.
# - Kazarov's Rust core (the archive reader, the disc extractor) is built for wasm32-unknown-emscripten with a
#   standard library of its own that has atomics (tools/kazarov/cmake/KazarovRust.cmake).

set(CT_WEB_DIR ${CMAKE_CURRENT_LIST_DIR})
include(FetchContent)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Every unit, the libraries fetched below included, shares the thread and exception model.
add_compile_options(-pthread -fexceptions)
add_link_options(-pthread -fexceptions)

# A game link runs Asyncify and wasm-opt over the whole game in several GiB; darkchronicle and darkchronicle_gl
# linking at once would run a 16 GiB machine out of memory, so links take turns.
set_property(GLOBAL APPEND PROPERTY JOB_POOLS ct_web_link=1)
set(CMAKE_JOB_POOL_LINK ct_web_link)

# What the desktop build has and the browser cannot: Tracy's client, cpptrace's stack walking, FFmpeg's decoders
# and link-time optimisation over a link that Asyncify already makes the longest step of the build.
set(CT_ENABLE_TRACY OFF CACHE BOOL "" FORCE)
set(CT_LTO off CACHE STRING "" FORCE)
set(CT_SYMBOLS off CACHE STRING "" FORCE)
set(CT_BUILD_TESTS OFF CACHE BOOL "" FORCE)

# SDL3, static. patch_sdl.py fixes the dummy audio driver's timer, which headless runs use.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.18
    GIT_SHALLOW TRUE
    PATCH_COMMAND ${Python3_EXECUTABLE} ${CT_WEB_DIR}/patch_sdl.py <SOURCE_DIR>
    UPDATE_DISCONNECTED TRUE)
if(FETCHCONTENT_SOURCE_DIR_SDL3)
    # A source directory given by hand gets no PATCH_COMMAND; the patch changes nothing a second time.
    execute_process(COMMAND ${Python3_EXECUTABLE} ${CT_WEB_DIR}/patch_sdl.py ${FETCHCONTENT_SOURCE_DIR_SDL3}
        COMMAND_ERROR_IS_FATAL ANY)
endif()
FetchContent_MakeAvailable(SDL3)
if(NOT TARGET SDL3::SDL3)
    add_library(SDL3::SDL3 ALIAS SDL3-static)
endif()

# Vulkan's headers, and a stub for every command in place of a loader.
FetchContent_Declare(VulkanHeaders
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG v1.4.366
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(VulkanHeaders)
set(CT_VK_STUBS ${CMAKE_BINARY_DIR}/web/vk_stubs.cpp)
add_custom_command(OUTPUT ${CT_VK_STUBS}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/web
    COMMAND ${Python3_EXECUTABLE} ${CT_WEB_DIR}/gen_vk_stubs.py
        ${vulkanheaders_SOURCE_DIR}/include/vulkan/vulkan_core.h ${CT_VK_STUBS}
    DEPENDS ${CT_WEB_DIR}/gen_vk_stubs.py
    COMMENT "Generating the Vulkan stubs"
    VERBATIM)
add_library(ct_vk_stubs STATIC ${CT_VK_STUBS})
target_link_libraries(ct_vk_stubs PUBLIC Vulkan::Headers)
target_compile_options(ct_vk_stubs PRIVATE -Wno-unused-parameter)
add_library(Vulkan::Vulkan INTERFACE IMPORTED GLOBAL)
set_target_properties(Vulkan::Vulkan PROPERTIES INTERFACE_LINK_LIBRARIES ct_vk_stubs)

# Kazarov's Rust core for wasm32-unknown-emscripten (KazarovRust.cmake has the Emscripten branches). The desktop
# build gets it through tools/dc2data (port/cmake/data.cmake), a command the browser has no use for.
include(${KZ_ROOT}/cmake/KazarovRust.cmake)

# The renderer's shaders in WGSL, for Vulkan on WebGPU.
set(CT_WGSL ${CMAKE_BINARY_DIR}/web/wgsl.cpp)
file(GLOB CT_WGSL_SOURCES CONFIGURE_DEPENDS ${CT_WEB_DIR}/vulkan/shaders/*.wgsl)
add_custom_command(OUTPUT ${CT_WGSL}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/web
    COMMAND ${Python3_EXECUTABLE} ${CT_WEB_DIR}/vulkan/embed_wgsl.py ${CT_WEB_DIR}/vulkan/shaders ${CT_WGSL}
    DEPENDS ${CT_WEB_DIR}/vulkan/embed_wgsl.py ${CT_WGSL_SOURCES}
    COMMENT "Embedding the WGSL shaders"
    VERBATIM)

# What every unit of the port compiles with on the web, beside ct_options: include/ shadows clang's x86gprintrin.h
# for quill (include/x86gprintrin.h).
function(ct_web_options target)
    target_include_directories(${target} SYSTEM INTERFACE ${CT_WEB_DIR}/include)
endfunction()

# Kazarov's host library with the arena memory replaced (Emscripten's mmap has no reservations or guard
# pages).
function(ct_web_host target)
    set_source_files_properties(${KZ_PORT_ROOT}/platform/memory.cpp PROPERTIES HEADER_FILE_ONLY ON)
    target_sources(${target} PRIVATE ${CT_WEB_DIR}/src/memory.cpp)
endfunction()

# The two Vulkans the renderer is linked with, each an object library so the stubs' weak definitions never
# stand in for its commands: Vulkan on WebGPU (vulkan/, Emscripten's emdawnwebgpu port) for darkchronicle, and
# Vulkan on WebGL2 (gl/) for darkchronicle_gl, which the page loads where WebGPU draws the game wrong or slowly
# (docs/WEB.md). Both match the renderer's SPIR-V, compiled for the host library.
function(ct_web_vulkan host)
    add_library(ct_vk_webgpu OBJECT
        ${CT_WEB_DIR}/vulkan/device.cpp ${CT_WEB_DIR}/vulkan/objects.cpp ${CT_WEB_DIR}/vulkan/commands.cpp ${CT_WGSL})
    add_library(ct_vk_gl OBJECT
        ${CT_WEB_DIR}/gl/device.cpp ${CT_WEB_DIR}/gl/objects.cpp ${CT_WEB_DIR}/gl/commands.cpp
        ${CT_WEB_DIR}/gl/shaders.cpp)
    target_include_directories(ct_vk_webgpu PRIVATE ${CT_WEB_DIR}/vulkan ${CMAKE_BINARY_DIR}/kz_port/generated)
    target_include_directories(ct_vk_gl PRIVATE ${CT_WEB_DIR}/gl ${CMAKE_BINARY_DIR}/kz_port/generated)
    target_compile_options(ct_vk_webgpu PRIVATE --use-port=emdawnwebgpu)
    foreach(layer ct_vk_webgpu ct_vk_gl)
        target_link_libraries(${layer} PRIVATE Vulkan::Headers)
        target_compile_options(${layer} PRIVATE -Wall)
        # The SPIR-V headers are made with the host library.
        add_dependencies(${layer} ${host})
    endforeach()
endfunction()

# What a target linking the WebGPU or the WebGL2 Vulkan takes: the layer's objects and its link options.
function(ct_web_link_vulkan target renderer)
    if(renderer STREQUAL "gl")
        target_sources(${target} PRIVATE $<TARGET_OBJECTS:ct_vk_gl>)
        target_link_options(${target} PRIVATE -sMAX_WEBGL_VERSION=2 -sMIN_WEBGL_VERSION=2)
    else()
        target_sources(${target} PRIVATE $<TARGET_OBJECTS:ct_vk_webgpu>)
        target_link_options(${target} PRIVATE --use-port=emdawnwebgpu)
    endif()
endfunction()

# The desktop-only units of port/src, replaced: the first run (the disc is extracted in the browser's storage),
# the movie player (no FFmpeg), the crash reports (no cpptrace), with the browser's storage, the on-screen gamepad
# and the process exit beside them.
set(CT_WEB_PORT_EXCLUDE "/platform/firstrun\\.cpp$|/video/player\\.cpp$|/crash/(crash|crash_dialog|crash_report|crash_symbols)\\.cpp$")
function(ct_web_port target)
    target_sources(${target} PRIVATE ${CT_WEB_DIR}/src/firstrun.cpp ${CT_WEB_DIR}/src/movie.cpp
        ${CT_WEB_DIR}/src/crash.cpp ${CT_WEB_DIR}/src/storage.cpp ${CT_WEB_DIR}/src/exit.cpp ${CT_WEB_DIR}/src/touch.cpp
        ${CT_WEB_DIR}/src/threads.cpp)
endfunction()

# The page around the game (site/): copied beside darkchronicle.js, so the build directory serves as is.
add_custom_target(ct_web_site
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CT_WEB_DIR}/site ${CMAKE_BINARY_DIR}
    COMMENT "Copying the web page")

# What the page loads. The game runs on a pthread; the main thread keeps the page responsive, and the canvas
# is handed to the game's thread, where WebGPU or WebGL2 draws to it. WasmFS mounts the browser's storage
# (src/storage.cpp). glyphs/, fonts/ and the Luau scripts/, which the game reads beside its executable, are
# preloaded into the root with the gameplay seed assets/data.zip: the executable is ./this.program there. Memory may grow to 4 GiB: the disc's
# extraction holds a floor's worth of models and textures at a time.
set(CT_WEB_LINK_OPTIONS
    -sOFFSCREENCANVAS_SUPPORT=1
    "-sOFFSCREENCANVASES_TO_PTHREAD=#canvas"
    -sWASMFS=1
    "SHELL:--preload-file ${CMAKE_BINARY_DIR}/glyphs@/glyphs"
    "SHELL:--preload-file ${CMAKE_BINARY_DIR}/fonts@/fonts"
    "SHELL:--preload-file ${CMAKE_BINARY_DIR}/scripts@/scripts"
    # The gameplay seed (KazarovRust.cmake's KZ_GAMEPLAY_ARCHIVE), which extracting a disc in the page needs.
    "SHELL:--preload-file ${CMAKE_BINARY_DIR}/assets/data.zip@/assets/data.zip"
    -sPROXY_TO_PTHREAD=1
    -sASYNCIFY=1
    # The default allocator has one lock for every thread: the disc's extraction allocates on all of its
    # workers at once and went slower with four of them than with two.
    -sMALLOC=mimalloc
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=536870912
    -sMAXIMUM_MEMORY=4GB
    -sSTACK_SIZE=4194304
    -sDEFAULT_PTHREAD_STACK_SIZE=4194304
    -sPTHREAD_POOL_SIZE=4
    -sFORCE_FILESYSTEM=1
    -lwebsocket.js
    -sEXIT_RUNTIME=1
    -sENVIRONMENT=web,worker,node
    -sEXPORTED_RUNTIME_METHODS=FS,callMain,ENV
    "SHELL:--pre-js ${CT_WEB_DIR}/src/node_canvas.js"
    # The fetch backend with a bounded cache, for the server's game data (src/storage.cpp, WebMountServerData).
    "SHELL:--js-library ${CT_WEB_DIR}/src/fetch_cache.js"
    -Wl,--error-limit=0)
# The link reruns when the pre-js or the library changes (emcc reads them at link time, outside CMake's scan).
set(CT_WEB_LINK_DEPENDS ${CT_WEB_DIR}/src/node_canvas.js ${CT_WEB_DIR}/src/fetch_cache.js)

# The WebGPU probe (probe/): what the renderer's WebGPU backend relies on, with the game's thread model.
add_executable(webgpu_probe EXCLUDE_FROM_ALL ${CT_WEB_DIR}/probe/webgpu_probe.cpp)
set_target_properties(webgpu_probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_compile_options(webgpu_probe PRIVATE --use-port=emdawnwebgpu)
target_link_options(webgpu_probe PRIVATE
    --use-port=emdawnwebgpu
    -sPROXY_TO_PTHREAD=1
    -sASYNCIFY=1
    -sALLOW_MEMORY_GROWTH=1
    -sOFFSCREENCANVAS_SUPPORT=1
    "-sOFFSCREENCANVASES_TO_PTHREAD=#canvas"
    -sEXIT_RUNTIME=1
    -sENVIRONMENT=web,worker)
add_custom_command(TARGET webgpu_probe POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${CT_WEB_DIR}/probe/probe.html ${CMAKE_BINARY_DIR}
    VERBATIM)
add_dependencies(webgpu_probe ct_web_site)

# Kazarov's renderer tests against Vulkan on WebGPU and on WebGL2, for the browser (test/gfx_tests.html, whose
# ?renderer=gl loads the second). Not built by default: `cmake --build --preset web --target kazarov_gfx_tests
# kazarov_gfx_tests_gl`. The render thread and depth planes cases need what the web leaves out (a worker thread
# on the device, bindless textures).
function(ct_web_gfx_tests host)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.18.0
        GIT_SHALLOW TRUE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    file(GLOB tests CONFIGURE_DEPENDS ${KZ_PORT_ROOT}/tests/gfx_*_test.cpp)
    list(FILTER tests EXCLUDE REGEX "gfx_(render_thread|depth_planes)_test\\.cpp$")
    foreach(renderer webgpu gl)
        set(target kazarov_gfx_tests)
        if(renderer STREQUAL "gl")
            set(target kazarov_gfx_tests_gl)
        endif()
        add_executable(${target} EXCLUDE_FROM_ALL ${CT_WEB_DIR}/test/gfx_tests_main.cpp ${tests})
        set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
        target_include_directories(${target} PRIVATE ${KZ_PORT_INCLUDE_ROOT}/tests ${KZ_PORT_INCLUDE_ROOT}
            ${CMAKE_BINARY_DIR}/kz_port/generated ${CMAKE_BINARY_DIR}/generated/include)
        target_link_libraries(${target} PRIVATE ${host} kz_port_headers SDL3::SDL3 Vulkan::Vulkan
            fastgltf::fastgltf nlohmann_json::nlohmann_json GTest::gtest)
        ct_web_link_vulkan(${target} ${renderer})
        if(renderer STREQUAL "gl")
            # WebGL2 draws offscreen into a context on the page's canvas.
            target_link_options(${target} PRIVATE -sOFFSCREENCANVAS_SUPPORT=1 "-sOFFSCREENCANVASES_TO_PTHREAD=#canvas")
        endif()
        target_link_options(${target} PRIVATE
            -sPROXY_TO_PTHREAD=1
            -sASYNCIFY=1
            -sALLOW_MEMORY_GROWTH=1
            -sINITIAL_MEMORY=268435456
            -sMAXIMUM_MEMORY=4GB
            -sSTACK_SIZE=4194304
            -sDEFAULT_PTHREAD_STACK_SIZE=4194304
            -sPTHREAD_POOL_SIZE=4
            -sWASMFS=1
            -sEXIT_RUNTIME=1
            -sENVIRONMENT=web,worker
            -Wl,--error-limit=0)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy ${CT_WEB_DIR}/test/gfx_tests.html ${CMAKE_BINARY_DIR}
            VERBATIM)
    endforeach()
endfunction()
