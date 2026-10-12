# The x64 port: the game's code built with clang as C++26 for the host.
#
# ps2/src holds the game's code as the PS2 build compiles it. A definition port/src tags PORT_OVERRIDE
# replaces the one ps2/src gives the same name: each unit of ps2/src is compiled without those
# definitions (tools/kazarov/scripts/port_override.py) and linked with port/src. That is how the PS2-only
# parts are replaced: the SDK, the Metrowerks runtime, the functions that touch the hardware and the
# functions written in assembly.

if(NOT CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER clang++)
endif()
enable_language(CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(KZ_ROOT ${CMAKE_SOURCE_DIR}/tools/kazarov)
if(NOT EXISTS ${KZ_ROOT}/include/kazarov/port.h)
    message(FATAL_ERROR "${KZ_ROOT} is empty: run git submodule update --init tools/kazarov")
endif()

include(${KZ_ROOT}/cmake/KazarovPort.cmake)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
kz_port_host_library(ct_host)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/HostOverrides.cmake)
ct_apply_host_overrides(ct_host ${CMAKE_CURRENT_SOURCE_DIR}/src ${KZ_ROOT})

# ps2/include is compiled through a mirror in the build tree with the fixups of scripts/port/header_fixups.py
# applied. The mirror is written only where its content changes, so an unchanged header keeps its timestamp.
set(PS2_INCLUDE_SRC ${CMAKE_SOURCE_DIR}/ps2/include)
set(PS2_INCLUDE_DIR ${CMAKE_BINARY_DIR}/ps2_include)
set(PS2_INCLUDE_STAMP ${CMAKE_BINARY_DIR}/ps2_include.stamp)
set(HEADER_FIXUPS_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/port/header_fixups.py)
set(LONG64_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/port/long64.py)
file(GLOB_RECURSE PS2_INCLUDE_FILES CONFIGURE_DEPENDS LIST_DIRECTORIES false ${PS2_INCLUDE_SRC}/*)
execute_process(COMMAND ${Python3_EXECUTABLE} ${HEADER_FIXUPS_SCRIPT} ${PS2_INCLUDE_SRC} ${PS2_INCLUDE_DIR} ${PS2_INCLUDE_STAMP}
    RESULT_VARIABLE header_fixups_result
    OUTPUT_VARIABLE header_fixups_output
    ERROR_VARIABLE header_fixups_error)
if(NOT header_fixups_result EQUAL 0)
    message(FATAL_ERROR "header_fixups.py failed (${header_fixups_result}):\n${header_fixups_output}${header_fixups_error}")
endif()
add_custom_command(OUTPUT ${PS2_INCLUDE_STAMP}
    COMMAND ${Python3_EXECUTABLE} ${HEADER_FIXUPS_SCRIPT} ${PS2_INCLUDE_SRC} ${PS2_INCLUDE_DIR} ${PS2_INCLUDE_STAMP}
    DEPENDS ${PS2_INCLUDE_FILES} ${HEADER_FIXUPS_SCRIPT} ${LONG64_SCRIPT}
    COMMENT "Mirroring ps2/include with the header fixups"
    VERBATIM)
add_custom_target(ps2_include DEPENDS ${PS2_INCLUDE_STAMP})

set(PS2_DIR ${CMAKE_SOURCE_DIR}/ps2/src)
set(PORT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
file(GLOB_RECURSE PS2_SOURCES CONFIGURE_DEPENDS RELATIVE ${PS2_DIR} ${PS2_DIR}/*.cpp)
file(GLOB_RECURSE PORT_SOURCES CONFIGURE_DEPENDS ${PORT_DIR}/*.cpp)
list(FILTER PORT_SOURCES EXCLUDE REGEX "/tests/")
include(${CMAKE_CURRENT_LIST_DIR}/cmake/NativeOwnership.cmake)
if(CT_NATIVE_SUPERSEDED_SOURCES)
    list(REMOVE_ITEM PORT_SOURCES ${CT_NATIVE_SUPERSEDED_SOURCES})
endif()
# These game-specific sources are built by dedicated targets or by the data phase.
list(FILTER PORT_SOURCES EXCLUDE REGEX "/host_overrides/|/platform/firstrun\\.cpp$")
# Scratch files left beside a unit are not units.
list(FILTER PS2_SOURCES EXCLUDE REGEX "(^|/)tmp[^/]*$")
set(CT_PS2_SOURCE_FILES "")
foreach(unit IN LISTS PS2_SOURCES)
    list(APPEND CT_PS2_SOURCE_FILES ${PS2_DIR}/${unit})
endforeach()

set(PS2_UNITS "")
# C2 ships only the PAL release, so PAL is the one region the port is built for.
set(PORT_DEFINITIONS PORT PAL)
list(TRANSFORM PORT_DEFINITIONS PREPEND --define= OUTPUT_VARIABLE port_override_defines)
# ps2/src spells the 64-bit long that long64.py turns into the long long port/src has to spell.
list(APPEND port_override_defines --long-is-64)
# The names port/src's PORT_OVERRIDE definitions link under. PORT_OVERRIDE_SOURCES is written only when
# its content changes, and a unit or stub header appearing or vanishing changes no timestamp, so it is a
# dependency of its own.
# Checked parser extensions and the native ownership exclusions live outside Kazarov.
set(PORT_OVERRIDE_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/port/port_override.py)
set(PORT_OVERRIDES ${CMAKE_BINARY_DIR}/port_overrides.txt)
set(PORT_OVERRIDE_SOURCES ${CMAKE_BINARY_DIR}/port_override_sources.txt)
set(PORT_OVERRIDE_EXCLUDES "")
foreach(superseded IN LISTS CT_NATIVE_SUPERSEDED_SOURCES)
    list(APPEND PORT_OVERRIDE_EXCLUDES --exclude ${superseded})
endforeach()
file(GLOB_RECURSE PORT_OVERRIDE_STUBS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/port/include/stubs/*.hpp)
string(JOIN "\n" port_override_sources ${PORT_SOURCES} ${PS2_SOURCES} ${PORT_OVERRIDE_STUBS})
file(CONFIGURE OUTPUT ${PORT_OVERRIDE_SOURCES} CONTENT "${port_override_sources}\n" @ONLY)
add_custom_command(OUTPUT ${PORT_OVERRIDES}
    COMMAND ${Python3_EXECUTABLE} ${PORT_OVERRIDE_SCRIPT} list ${PORT_DIR}
        ${PORT_OVERRIDE_EXCLUDES} -o ${PORT_OVERRIDES}
    DEPENDS ${PORT_SOURCES} ${PORT_OVERRIDE_SOURCES} ${PORT_OVERRIDE_SCRIPT}
    COMMENT "Listing port/src's PORT_OVERRIDE definitions"
    VERBATIM)
# port/include/stubs/<unit>.hpp, when it exists, is included ahead of its unit: it declares what the port
# defines in place of code PORT leaves out, and renames what the unit takes from MWCC alone.
set(CT_PS2_HOST_DIR ${CMAKE_BINARY_DIR}/ps2_host/src)
set(CT_PS2_HOST_STAMP ${CMAKE_BINARY_DIR}/ps2_host.stamp)
set(CT_PS2_HOST_FIXUPS ${CMAKE_SOURCE_DIR}/scripts/port/upstream_host_fixups.py)
set(CT_PS2_HOST_FILES "")
set(CT_PS2_SOURCE_FILES "")
foreach(unit IN LISTS PS2_SOURCES)
    list(APPEND CT_PS2_HOST_FILES ${CT_PS2_HOST_DIR}/${unit})
    list(APPEND CT_PS2_SOURCE_FILES ${PS2_DIR}/${unit})
endforeach()
if(EXISTS ${CT_PS2_HOST_FIXUPS})
    add_custom_command(OUTPUT ${CT_PS2_HOST_STAMP}
        COMMAND ${Python3_EXECUTABLE} ${CT_PS2_HOST_FIXUPS} ${PS2_DIR} ${CT_PS2_HOST_DIR} ${CT_PS2_HOST_STAMP}
        BYPRODUCTS ${CT_PS2_HOST_FILES}
        DEPENDS ${CT_PS2_SOURCE_FILES} ${CT_PS2_HOST_FIXUPS} ${CMAKE_SOURCE_DIR}/scripts/port/upstream_host_fixups.patch
        COMMENT "Preparing PS2 source mirror for the host port"
        VERBATIM)
    add_custom_target(ct_ps2_host_fixups DEPENDS ${CT_PS2_HOST_STAMP})
endif()

set(WIN_REWRITE_SCRIPT ${CMAKE_SOURCE_DIR}/scripts/port/win_rewrite.py)
set(HOST_SCRIPT_STORAGE ${CMAKE_SOURCE_DIR}/scripts/port/host_script_storage.py)
set(HOST_MENU_POINTERS ${CMAKE_SOURCE_DIR}/scripts/port/host_menu_pointers.py)
set(HOST_PHOTO_NAMES ${CMAKE_SOURCE_DIR}/scripts/port/host_photo_names.py)
set(HOST_COMBAT_TRACE ${CMAKE_SOURCE_DIR}/scripts/port/host_combat_trace.py)
set(HOST_ACTION_CALLBACKS ${CMAKE_SOURCE_DIR}/scripts/port/wp01_action_callbacks.py)
set(HOST_LOADING_MEMORY ${CMAKE_SOURCE_DIR}/scripts/port/wp12_loading_memory.py)
set(HOST_VECTOR_CONTRACTS ${CMAKE_SOURCE_DIR}/scripts/port/host_vector_contracts.py)
set(HOST_SCRIPT_STORAGE_UNITS effscript.cpp)
set(WIN_REWRITE_UNITS "")
if(WIN32)
    execute_process(COMMAND ${Python3_EXECUTABLE} ${WIN_REWRITE_SCRIPT} units
        OUTPUT_VARIABLE WIN_REWRITE_UNITS OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    file(GLOB_RECURSE PS2_HEADERS CONFIGURE_DEPENDS LIST_DIRECTORIES false ${PS2_INCLUDE_SRC}/*)
    set(PS2_MIRRORED_HEADERS "")
    foreach(header_file IN LISTS PS2_HEADERS)
        file(RELATIVE_PATH relative_header ${PS2_INCLUDE_SRC} ${header_file})
        list(APPEND PS2_MIRRORED_HEADERS ${PS2_INCLUDE_DIR}/${relative_header})
    endforeach()
    set(PS2_WIN_REWRITE_STAMP ${CMAKE_BINARY_DIR}/ps2_include.win_rewrite.stamp)
    add_custom_command(OUTPUT ${PS2_WIN_REWRITE_STAMP}
        COMMAND ${Python3_EXECUTABLE} ${WIN_REWRITE_SCRIPT} headers ${PS2_INCLUDE_DIR} ${PS2_INCLUDE_DIR}
        COMMAND ${CMAKE_COMMAND} -E touch ${PS2_WIN_REWRITE_STAMP}
        BYPRODUCTS ${PS2_MIRRORED_HEADERS}
        DEPENDS ${PS2_INCLUDE_STAMP} ${PS2_HEADERS} ${WIN_REWRITE_SCRIPT}
        COMMENT "Applying Windows source compatibility rewrites to mirrored headers"
        VERBATIM)
    add_custom_target(ct_win_headers DEPENDS ${PS2_WIN_REWRITE_STAMP})
endif()

foreach(unit IN LISTS PS2_SOURCES)
    set(copy ${CMAKE_BINARY_DIR}/ps2_src/${unit})
    set(original ${PS2_DIR}/${unit})
    if(EXISTS ${CT_PS2_HOST_FIXUPS})
        set(original ${CT_PS2_HOST_DIR}/${unit})
    endif()
    string(REGEX REPLACE "\\.cpp$" ".hpp" header ${CMAKE_SOURCE_DIR}/port/include/stubs/${unit})
    if(EXISTS ${header})
        set(stubs --stubs ${header})
    else()
        set(stubs "")
        set(header "")
    endif()
    set(ct_transform_commands "")
    set(ct_transform_deps "")
    if(WIN32 AND unit IN_LIST WIN_REWRITE_UNITS)
        list(APPEND ct_transform_commands COMMAND ${Python3_EXECUTABLE} ${WIN_REWRITE_SCRIPT} unit ${original} ${copy})
        list(APPEND ct_transform_deps ${WIN_REWRITE_SCRIPT})
    endif()
    if(unit IN_LIST HOST_SCRIPT_STORAGE_UNITS)
        list(APPEND ct_transform_commands COMMAND ${Python3_EXECUTABLE} ${HOST_SCRIPT_STORAGE} unit src/${unit} ${copy})
        list(APPEND ct_transform_deps ${HOST_SCRIPT_STORAGE} ${CMAKE_SOURCE_DIR}/port/include/script_storage.hpp)
    endif()
    if(unit STREQUAL "effscript.cpp" OR unit STREQUAL "event_func.cpp")
        list(APPEND ct_transform_commands COMMAND ${Python3_EXECUTABLE} ${HOST_VECTOR_CONTRACTS} unit ${copy})
        list(APPEND ct_transform_deps ${HOST_VECTOR_CONTRACTS} ${HOST_ACTION_CALLBACKS})
    endif()
    if(unit STREQUAL "menuop.cpp")
        list(APPEND ct_transform_commands COMMAND ${Python3_EXECUTABLE} ${HOST_MENU_POINTERS} unit src/${unit} ${copy})
        list(APPEND ct_transform_deps ${HOST_MENU_POINTERS})
    endif()
    if(unit STREQUAL "inventmn.cpp")
        list(APPEND ct_transform_commands COMMAND ${Python3_EXECUTABLE} ${HOST_PHOTO_NAMES} ${copy})
        list(APPEND ct_transform_deps ${HOST_PHOTO_NAMES} ${CMAKE_SOURCE_DIR}/port/include/photo_name_storage.hpp)
    endif()
    if(unit STREQUAL "actscript.cpp")
        list(APPEND ct_transform_commands COMMAND ${Python3_EXECUTABLE} ${HOST_ACTION_CALLBACKS} unit src/${unit} ${copy})
        list(APPEND ct_transform_deps ${HOST_ACTION_CALLBACKS})
    endif()
    set(ct_post_transform_commands "")
    if(unit STREQUAL "nowload.cpp")
        list(APPEND ct_post_transform_commands COMMAND ${Python3_EXECUTABLE} ${HOST_LOADING_MEMORY} ${copy})
        list(APPEND ct_transform_deps ${HOST_LOADING_MEMORY})
    endif()
    if(unit STREQUAL "dng_main.cpp" OR unit STREQUAL "actionchara.cpp" OR unit STREQUAL "actscript.cpp")
        list(APPEND ct_transform_commands COMMAND ${Python3_EXECUTABLE} ${HOST_COMBAT_TRACE} src/${unit} ${copy})
        list(APPEND ct_transform_deps ${HOST_COMBAT_TRACE} ${CMAKE_SOURCE_DIR}/port/include/combat_trace.hpp)
    endif()
    add_custom_command(OUTPUT ${copy}
        COMMAND ${Python3_EXECUTABLE} ${PORT_OVERRIDE_SCRIPT} strip --overrides ${PORT_OVERRIDES} ${stubs}
            ${port_override_defines} ${original} ${copy}
        ${ct_transform_commands}
        COMMAND ${Python3_EXECUTABLE} ${LONG64_SCRIPT} ${copy}
        ${ct_post_transform_commands}
        DEPENDS ${original} ${PORT_OVERRIDES} ${PORT_OVERRIDE_SOURCES} ${PORT_OVERRIDE_SCRIPT} ${header} ${ct_transform_deps} ${LONG64_SCRIPT}
        COMMENT "Removing overridden definitions from ps2/src/${unit}"
        VERBATIM)
    list(APPEND PS2_UNITS ${copy})
    list(APPEND PS2_ORIGINALS ${PS2_DIR}/${unit} ${header})
    if(header)
        set_property(SOURCE ${copy} APPEND PROPERTY COMPILE_OPTIONS -include ${header})
        set_property(SOURCE ${copy} APPEND PROPERTY OBJECT_DEPENDS ${header})
    endif()
endforeach()
# A tag that names no definition in ps2/src stops the build: its replacement would be unused wherever
# the two signatures differ.
set(PORT_OVERRIDES_CHECKED ${CMAKE_BINARY_DIR}/port_overrides.checked)
add_custom_command(OUTPUT ${PORT_OVERRIDES_CHECKED}
    COMMAND ${Python3_EXECUTABLE} ${PORT_OVERRIDE_SCRIPT} check --overrides ${PORT_OVERRIDES}
        --stubs ${CMAKE_SOURCE_DIR}/port/include/stubs ${port_override_defines} ${PS2_DIR}
        -o ${PORT_OVERRIDES_CHECKED}
    DEPENDS ${PS2_ORIGINALS} ${PORT_OVERRIDES} ${PORT_OVERRIDE_SOURCES} ${PORT_OVERRIDE_SCRIPT}
    COMMENT "Checking that every PORT_OVERRIDE definition replaces one of ps2/src's"
    VERBATIM)

# The game was written for MWCC, which accepts what clang rejects by default: pointers truncated to
# 32-bit integers, narrowing in braced initialisers and the register keyword.
add_library(ct_options INTERFACE)
target_compile_definitions(ct_options INTERFACE ${PORT_DEFINITIONS})
target_include_directories(ct_options INTERFACE
    ${CMAKE_SOURCE_DIR}/port/include ${KZ_ROOT}/include ${PS2_INCLUDE_DIR} ${PS2_INCLUDE_DIR}/sce)
# The PS2-sized struct assertions fail for host structs that contain pointers; enable only for audits.
option(CT_STATIC_ASSERT "Make the game's STATIC_ASSERTs check" OFF)
if(CT_STATIC_ASSERT)
    target_compile_definitions(ct_options INTERFACE PORT_STATIC_ASSERT)
endif()
target_compile_options(ct_options INTERFACE
    "SHELL:-include \"${CMAKE_SOURCE_DIR}/port/include/port.h\""
    -fms-extensions -ffunction-sections -fdata-sections
    -Wno-register -Wno-c++11-narrowing -Wno-return-mismatch)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/movies.cmake)
# arm64 clang fuses a*b+c into one rounding by default and x86-64 has no FMA in its baseline; off
# everywhere so the game's arithmetic rounds the same on both.
target_compile_options(ct_options INTERFACE -ffp-contract=off)
# Constructor aliases would point C1 into a COMDAT section lld discards under --allow-multiple-definition.
if(WIN32)
    target_compile_options(ct_options INTERFACE -Xclang -mno-constructor-aliases)
endif()

if(WIN32 AND (NOT MINGW OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang"))
    message(FATAL_ERROR "The Windows build requires clang/llvm-mingw")
endif()

add_library(ct_ps2 OBJECT ${PS2_UNITS} ${PORT_OVERRIDES_CHECKED})
target_link_libraries(ct_ps2 PRIVATE ct_options)
add_dependencies(ct_ps2 ps2_include)
if(TARGET ct_ps2_host_fixups)
    add_dependencies(ct_ps2 ct_ps2_host_fixups)
endif()
if(TARGET ct_win_headers)
    add_dependencies(ct_ps2 ct_win_headers)
endif()
# The game reads locals it never sets and falls off the end of functions that return a value, and
# ps2/src keeps both because the match depends on them. MWCC gave each a defined if arbitrary result;
# these flags give the port a defined one too, with the type punning and wraparound the code assumes.
target_compile_options(ct_ps2 PRIVATE -ftrivial-auto-var-init=zero -fno-strict-return
    -fno-strict-aliasing -fwrapv)
# What is left on warns about behaviour the port may still need to answer for. What is turned off
# describes how MWCC-era source is spelled: string literals as char *, MWCC's pragmas, 32-bit pointer
# casts and the unused names and expressions it leaves behind. ps2/src cannot change those without
# changing what MWCC compiles.
target_compile_options(ct_ps2 PRIVATE -Wall -Wno-writable-strings -Wno-unknown-pragmas
    -Wno-mismatched-tags -Wno-missing-braces -Wno-char-subscripts -Wno-overloaded-virtual
    -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -Wno-int-to-void-pointer-cast
    -Wno-void-pointer-to-int-cast -Wno-unused-variable -Wno-unused-but-set-variable
    -Wno-unused-local-typedef -Wno-unused-label -Wno-unused-function -Wno-unused-value
    -Wno-unused-const-variable -Wno-undefined-internal -Wno-self-assign -Wno-deprecated-volatile)

set(PORT_LIB_SOURCES ${PORT_SOURCES})
list(FILTER PORT_LIB_SOURCES EXCLUDE REGEX "/tests/|/main\\.cpp$")

kz_port_sce_sources(CT_SCE_SOURCES LIBS libvu0 libpad libmc sifdev sifrpc eekernel libcdvd libgraph libdma libpkt
    eekernel_threads libdev libmpeg libsdr modmsin sifdma)
foreach(family IN LISTS CT_NATIVE_SDK_FAMILIES)
    list(REMOVE_ITEM CT_SCE_SOURCES
        ${KZ_PORT_ROOT}/sce/${family}.cpp ${KZ_PORT_ROOT}/sce/stubs/${family}.cpp)
endforeach()
add_library(ct_port OBJECT ${PORT_LIB_SOURCES} ${CT_SCE_SOURCES})
target_link_libraries(ct_port PRIVATE ct_options SDL3::SDL3 Vulkan::Vulkan nlohmann_json::nlohmann_json kz_port_headers)
add_dependencies(ct_port ps2_include)
if(TARGET ct_win_headers)
    add_dependencies(ct_port ct_win_headers)
endif()
target_include_directories(ct_port PRIVATE ${PORT_DIR})
target_include_directories(ct_port BEFORE PRIVATE ${PORT_DIR}/host_overrides)
target_compile_options(ct_port PRIVATE -Wall -Wno-mismatched-tags -Wno-unused-label
    -Wno-overloaded-virtual -Wno-unused-function)

# Shared by the game and its tests. Names the PS2 build tells apart per object are renamed by
# port/include/stubs, so no symbol is defined twice. The executable is position-independent on every
# platform: no game object has to sit below 4 GiB, because pointer-to-32-bit casts are widened in port/src.
if(WIN32)
    set(CT_LINK_OPTIONS -fuse-ld=lld -Wl,--gc-sections -Wl,--error-limit=0)
elseif(APPLE)
    set(CT_LINK_OPTIONS -Wl,-dead_strip)
else()
    set(CT_LINK_OPTIONS -fuse-ld=lld -pie -Wl,-z,separate-code -Wl,--gc-sections -Wl,--error-limit=0)
endif()

function(ct_link_game target)
    target_sources(${target} PRIVATE $<TARGET_OBJECTS:ct_port> $<TARGET_OBJECTS:ct_host> $<TARGET_OBJECTS:ct_ps2>)
    add_dependencies(${target} ps2_include)
    target_link_libraries(${target} PRIVATE ct_options kz_port_headers SDL3::SDL3 Vulkan::Vulkan)
    target_include_directories(${target} PRIVATE ${PORT_DIR})
    target_include_directories(${target} BEFORE PRIVATE ${PORT_DIR}/host_overrides)
    target_compile_options(${target} PRIVATE -Wall -Wno-mismatched-tags -Wno-unused-label
        -Wno-overloaded-virtual -Wno-unused-function)
    target_link_options(${target} PRIVATE ${CT_LINK_OPTIONS})
endfunction()

add_executable(darkchronicle ${PORT_DIR}/main.cpp)
set_target_properties(darkchronicle PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
ct_link_game(darkchronicle)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/WindowsRuntime.cmake)
ct_copy_windows_runtime(darkchronicle)

include(GNUInstallDirs)
install(TARGETS darkchronicle RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

option(CT_BUILD_TESTS "Build darkchronicle_tests" ON)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/data.cmake)
if(CT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(src/tests)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/cmake/CompatibilityTargets.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/CompletionTargets.cmake)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/RuntimeContracts.cmake)
