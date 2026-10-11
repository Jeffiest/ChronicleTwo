# The x64 port: the game's code built with clang as C++26 for the host.
#
# ps2/src holds the game as the PS2 build compiles it. A definition port/src
# tags PORT_OVERRIDE replaces the one ps2/src gives the same name: the port
# compiles a copy of each unit without those definitions
# (tools/kazarov/scripts/port_override.py) and links it with port/src. That is
# how the PS2-only parts are replaced: the SDK, the Metrowerks runtime, the
# functions that touch the hardware directly and those written in assembly.
#
# The host layer (gfx, audio, platform and the shared sce sources) is Kazarov's
# (tools/kazarov/port); port/src holds this game's side of it.
#
# The port is always the PAL release.

if(NOT CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER clang++)
endif()
enable_language(CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Python runs ${KZ_ROOT}/scripts/port_override.py.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
# SDL3 owns the window and input; Vulkan renders.
find_package(SDL3 REQUIRED CONFIG)
find_package(Vulkan REQUIRED)
# nlohmann/json reads config.json: the installed package where there is one, fetched otherwise.
include(FetchContent)
FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    FIND_PACKAGE_ARGS CONFIG)
FetchContent_MakeAvailable(nlohmann_json)

set(KZ_ROOT ${CMAKE_SOURCE_DIR}/tools/kazarov)
if(NOT EXISTS ${KZ_ROOT}/include/kazarov/port.h)
    message(FATAL_ERROR "${KZ_ROOT} is empty: run git submodule update --init tools/kazarov")
endif()
include(${KZ_ROOT}/cmake/KazarovPort.cmake)

set(PS2_DIR ${CMAKE_SOURCE_DIR}/ps2/src)
set(PORT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
file(GLOB_RECURSE PS2_SOURCES CONFIGURE_DEPENDS RELATIVE ${PS2_DIR} ${PS2_DIR}/*.cpp)
file(GLOB_RECURSE PORT_SOURCES CONFIGURE_DEPENDS ${PORT_DIR}/*.cpp)
list(FILTER PORT_SOURCES EXCLUDE REGEX "/tests/")
# Kazarov's units this game replaces with its own copies, built into the host library instead.
file(GLOB_RECURSE HOST_OVERRIDE_SOURCES CONFIGURE_DEPENDS ${PORT_DIR}/host_overrides/*.cpp)
list(FILTER PORT_SOURCES EXCLUDE REGEX "/host_overrides/")
# The first-run extractor drives the data tool; that lands with the data phase.
list(FILTER PORT_SOURCES EXCLUDE REGEX "/platform/firstrun\\.cpp$")
list(FILTER PS2_SOURCES EXCLUDE REGEX "(^|/)tmp[^/]*$")

# CMenuItemUse::UseItem keeps a CItemUseTarget in an anonymous union so that no constructor runs
# for it, which MWCC accepts and clang deletes, and ps2/src cannot change. The port compiles a
# copy with the same storage spelled out, until the function is replaced from port/src.
set(MENUCLS1_COPY ${CMAKE_BINARY_DIR}/ps2_copies/menucls1.cpp)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${PS2_DIR}/menucls1.cpp)
file(READ ${PS2_DIR}/menucls1.cpp menucls1_text)
string(REPLACE "    union {\n        CItemUseTarget target_data;\n    };\n"
    "    alignas(CItemUseTarget) u_char target_data_storage[sizeof(CItemUseTarget)];\n    CItemUseTarget &target_data = *reinterpret_cast<CItemUseTarget *>(target_data_storage);\n"
    menucls1_patched "${menucls1_text}")
if(menucls1_patched STREQUAL menucls1_text)
    message(FATAL_ERROR "ps2/src/menucls1.cpp no longer holds the union port/CMakeLists.txt rewrites")
endif()
# Written only when it changes, so a configure does not recompile the unit.
set(menucls1_current "")
if(EXISTS ${MENUCLS1_COPY})
    file(READ ${MENUCLS1_COPY} menucls1_current)
endif()
if(NOT menucls1_current STREQUAL menucls1_patched)
    file(WRITE ${MENUCLS1_COPY} "${menucls1_patched}")
endif()

set(PS2_UNITS "")
# What both halves are compiled with (dc_options below). PORT leaves out the
# functions ps2/src writes in MIPS assembly.
set(PORT_DEFINITIONS PORT PAL)
list(TRANSFORM PORT_DEFINITIONS PREPEND --define= OUTPUT_VARIABLE port_override_defines)
# The names port/src's PORT_OVERRIDE definitions link under. The list is rewritten
# only when it changes, so other edits to port/src rebuild nothing of ps2/src.
# PORT_OVERRIDE_SOURCES changes when a unit or a stub header comes or goes, which no
# timestamp shows.
# Interim copy of Kazarov's port_override.py with two parser fixes this game needs; drop it once those land upstream.
set(PORT_OVERRIDE_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/port/port_override.py)
set(PORT_OVERRIDES ${CMAKE_BINARY_DIR}/port_overrides.txt)
set(PORT_OVERRIDE_SOURCES ${CMAKE_BINARY_DIR}/port_override_sources.txt)
file(GLOB_RECURSE PORT_OVERRIDE_STUBS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/port/include/stubs/*.hpp)
string(JOIN "\n" port_override_sources ${PORT_SOURCES} ${PS2_SOURCES} ${PORT_OVERRIDE_STUBS})
file(CONFIGURE OUTPUT ${PORT_OVERRIDE_SOURCES} CONTENT "${port_override_sources}\n" @ONLY)
add_custom_command(OUTPUT ${PORT_OVERRIDES}
    COMMAND ${Python3_EXECUTABLE} ${PORT_OVERRIDE_SCRIPT} list ${PORT_DIR} -o ${PORT_OVERRIDES}
    DEPENDS ${PORT_SOURCES} ${PORT_OVERRIDE_SOURCES} ${PORT_OVERRIDE_SCRIPT}
    COMMENT "Listing port/src's PORT_OVERRIDE definitions"
    VERBATIM)
# What is compiled of a unit is its copy without the definitions of those names.
# port/include/stubs/<unit>.hpp, if it exists, is included ahead of that unit
# of ps2/src: it declares what the port defines in place of code PORT leaves
# out, and supplies or renames what the unit takes from MWCC alone.
set(PS2_ORIGINALS "")
foreach(unit IN LISTS PS2_SOURCES)
    set(copy ${CMAKE_BINARY_DIR}/ps2_src/${unit})
    if(unit STREQUAL "menucls1.cpp")
        set(original ${MENUCLS1_COPY})
    else()
        set(original ${PS2_DIR}/${unit})
    endif()
    string(REGEX REPLACE "\\.cpp$" ".hpp" header ${CMAKE_SOURCE_DIR}/port/include/stubs/${unit})
    if(EXISTS ${header})
        set(stubs --stubs ${header})
    else()
        set(stubs "")
        set(header "")
    endif()
    add_custom_command(OUTPUT ${copy}
        COMMAND ${Python3_EXECUTABLE} ${PORT_OVERRIDE_SCRIPT} strip --overrides ${PORT_OVERRIDES} ${stubs}
            ${port_override_defines} ${original} ${copy}
        DEPENDS ${original} ${PORT_OVERRIDES} ${PORT_OVERRIDE_SOURCES} ${PORT_OVERRIDE_SCRIPT} ${header}
        COMMENT "Removing overridden definitions from ps2/src/${unit}"
        VERBATIM)
    list(APPEND PS2_UNITS ${copy})
    list(APPEND PS2_ORIGINALS ${PS2_DIR}/${unit} ${header})
    if(header)
        set_property(SOURCE ${copy} APPEND PROPERTY COMPILE_OPTIONS -include ${header})
        set_property(SOURCE ${copy} APPEND PROPERTY OBJECT_DEPENDS ${header})
    endif()
endforeach()
# A tag that names no definition in ps2/src stops the build: its replacement
# would be unused wherever the two signatures differ.
set(PORT_OVERRIDES_CHECKED ${CMAKE_BINARY_DIR}/port_overrides.checked)
add_custom_command(OUTPUT ${PORT_OVERRIDES_CHECKED}
    COMMAND ${Python3_EXECUTABLE} ${PORT_OVERRIDE_SCRIPT} check --overrides ${PORT_OVERRIDES}
        --stubs ${CMAKE_SOURCE_DIR}/port/include/stubs ${port_override_defines} ${PS2_DIR}
        -o ${PORT_OVERRIDES_CHECKED}
    DEPENDS ${PS2_ORIGINALS} ${PORT_OVERRIDES} ${PORT_OVERRIDE_SOURCES} ${PORT_OVERRIDE_SCRIPT}
    COMMENT "Checking that every PORT_OVERRIDE definition replaces one of ps2/src's"
    VERBATIM)

# What both halves are compiled with. The game was written for MWCC, which
# accepts constructs clang rejects by default: pointers truncated to 32-bit
# integers, narrowing in braced initialisers and the register keyword.
add_library(dc_options INTERFACE)
target_compile_definitions(dc_options INTERFACE ${PORT_DEFINITIONS})
target_include_directories(dc_options INTERFACE
    ${CMAKE_SOURCE_DIR}/port/include ${KZ_ROOT}/include ${CMAKE_SOURCE_DIR}/ps2/include ${CMAKE_SOURCE_DIR}/ps2/include/sce)
target_compile_options(dc_options INTERFACE
    "SHELL:-include ${CMAKE_SOURCE_DIR}/port/include/port.h"
    -fms-extensions -ffunction-sections -fdata-sections
    -Wno-register -Wno-c++11-narrowing -Wno-return-mismatch)
# x86-64's baseline has no FMA; the game's arithmetic must round the same
# wherever it runs.
target_compile_options(dc_options INTERFACE -ffp-contract=off)

add_library(dc_ps2 OBJECT ${PS2_UNITS} ${PORT_OVERRIDES_CHECKED})
target_link_libraries(dc_ps2 PRIVATE dc_options)
# Retail's code reads locals it never set and falls off the end of functions
# that return a value, and ps2/src keeps both because the match depends on
# them. MWCC gave each a defined if arbitrary result; these flags give the port
# a defined one too, along with the type punning and wraparound the code assumes.
target_compile_options(dc_ps2 PRIVATE -ftrivial-auto-var-init=zero -fno-strict-return
    -fno-strict-aliasing -fwrapv)
# What is left on warns about behaviour the port may still need to answer for;
# what is turned off describes how MWCC-era source is spelled.
target_compile_options(dc_ps2 PRIVATE -Wall -Wno-writable-strings -Wno-unknown-pragmas
    -Wno-mismatched-tags -Wno-missing-braces -Wno-char-subscripts -Wno-overloaded-virtual
    -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -Wno-int-to-void-pointer-cast
    -Wno-void-pointer-to-int-cast -Wno-unused-variable -Wno-unused-but-set-variable
    -Wno-unused-local-typedef -Wno-unused-label -Wno-unused-function -Wno-unused-value
    -Wno-unused-const-variable -Wno-undefined-internal -Wno-self-assign -Wno-deprecated-volatile)

# Everything in port/src except main.cpp is an object library rather than an
# archive: an archive member only enters the link when something references
# it, and the port's static initialisers are referenced by nothing.
set(PORT_LIB_SOURCES ${PORT_SOURCES})
list(FILTER PORT_LIB_SOURCES EXCLUDE REGEX "/tests/|/main\\.cpp$")

# Kazarov's sce sources, compiled here with this game's SDK headers and port.h like the rest of
# port/src/sce. Its libcdvd, libgraph, libmc, libpad, sifdev and sifrpc are smaller than this
# game's, and its stubs/ for libdma, libgraph and libpkt abort where this game's pass quietly, so
# those stay in port/src/sce.
kz_port_sce_sources(KZ_SCE_SOURCES LIBS eekernel libvu0)
list(APPEND PORT_LIB_SOURCES ${KZ_SCE_SOURCES})

# gfx, audio and platform never see a game header, so the host library is built without dc_options.
kz_port_host_library(dc_host)
# These Kazarov units differ from what this game needs (see the notes at the top of each file in
# host_overrides/); the copies replace them, and are checked against the pinned originals so that a
# Kazarov update cannot slip past them.
get_target_property(host_sources dc_host SOURCES)
list(FILTER host_sources EXCLUDE REGEX "/port/(gfx/(pipelines|resources)|audio/mixer)\\.cpp$")
set_property(TARGET dc_host PROPERTY SOURCES ${host_sources} ${HOST_OVERRIDE_SOURCES})
target_include_directories(dc_host BEFORE PRIVATE ${PORT_DIR}/host_overrides)
set(HOST_OVERRIDE_BASES
    gfx/pipelines.cpp b2187d17501cb9d2f056c0792ffbaaa751e6fb067cfcb0d30a49eb25e5d4d5de
    gfx/resources.cpp 32db6591dea7a9d4713846082b5e958d7fe22d90b2963359429dc64a225a0050
    audio/mixer.cpp 72413488cb79f69fad65ce0731afa338fa254fe588a13d990605a381f0bf89a1
    audio/mixer.hpp 3a5c1a8a2d1f2265b394d85ec494b7b9d0145390e0a0b8b1fc93eb27931de1aa)
list(LENGTH HOST_OVERRIDE_BASES override_count)
math(EXPR override_last "${override_count} - 1")
foreach(index RANGE 0 ${override_last} 2)
    math(EXPR sha_index "${index} + 1")
    list(GET HOST_OVERRIDE_BASES ${index} unit)
    list(GET HOST_OVERRIDE_BASES ${sha_index} expected)
    file(SHA256 ${KZ_ROOT}/port/${unit} actual)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "tools/kazarov/port/${unit} changed since port/src/host_overrides/${unit} was "
            "made from it: merge the change into the override, then update HOST_OVERRIDE_BASES")
    endif()
endforeach()

add_library(dc_port OBJECT ${PORT_LIB_SOURCES})
target_link_libraries(dc_port PRIVATE dc_options SDL3::SDL3 Vulkan::Vulkan kz_port_headers)
target_include_directories(dc_port BEFORE PRIVATE ${PORT_DIR}/host_overrides)
target_include_directories(dc_port PRIVATE ${PORT_DIR})
target_compile_options(dc_port PRIVATE -Wall -Wno-mismatched-tags -Wno-unused-label
    -Wno-overloaded-virtual -Wno-unused-function)

# Shared by the game and its tests: the port's objects, the game's and the
# link flags they need. The executable is position-independent on every
# platform: the game's casts of pointers to 32-bit integers are all widened in
# port/src, so nothing has to sit below 4 GiB.
set(DC_LINK_OPTIONS -fuse-ld=lld -pie -Wl,-z,separate-code -Wl,--gc-sections -Wl,--error-limit=0)

function(dc_link_game target)
    target_sources(${target} PRIVATE $<TARGET_OBJECTS:dc_port> $<TARGET_OBJECTS:dc_host> $<TARGET_OBJECTS:dc_ps2>)
    target_link_libraries(${target} PRIVATE dc_options SDL3::SDL3 Vulkan::Vulkan kz_port_headers)
    target_include_directories(${target} BEFORE PRIVATE ${PORT_DIR}/host_overrides)
    target_include_directories(${target} PRIVATE ${PORT_DIR})
    target_compile_options(${target} PRIVATE -Wall -Wno-mismatched-tags -Wno-unused-label
        -Wno-overloaded-virtual -Wno-unused-function)
    target_link_options(${target} PRIVATE ${DC_LINK_OPTIONS})
endfunction()

if(EXISTS ${PORT_DIR}/main.cpp)
    add_executable(darkchronicle ${PORT_DIR}/main.cpp)
    set_target_properties(darkchronicle PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    dc_link_game(darkchronicle)

    include(GNUInstallDirs)
    install(TARGETS darkchronicle RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# The tests use an installed GoogleTest, or fetch one at configure time; a
# build with neither turns them off.
option(DC_BUILD_TESTS "Build the port's tests" OFF)
if(DC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(src/tests)
endif()
