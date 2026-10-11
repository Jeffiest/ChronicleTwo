# The PS2 build: compiles every game unit with MWCC through Satan's Fiddle,
# assembles the libraries and data-only units from the split, links the
# executable with MWLD and verifies it against retail.
#
# Targets:
#   setup             check the disc, extract it and split the executable
#   disassemble       split the executable again
#   elf               link <build>/SCES_511.90
#   build (default)   elf, then verify it against retail
#   verify_extracted  check the files extracted from the disc
#   iso               master <build>/Dark Chronicle (PAL Build).iso
#   run               boot that image in PCSX2
#   objdiff, ctx      objdiff's configuration and m2c's context (cmake/Objdiff.cmake)

set(BASENAME SCES_511.90)
set(RELEASE PAL)
set(RELEASE_DIR pal)

set(SRC_DIR ps2/src)
set(INCLUDE_DIR ps2/include)
set(CONFIG_DIR ps2/config/${RELEASE_DIR})
set(ASM_DIR ps2/asm/${RELEASE_DIR})
set(TOOLS_DIR tools)
set(SCRIPTS_DIR scripts)
set(ROM_DIR rom/${RELEASE_DIR})
set(RETAIL_ISO "${ROM_DIR}/Dark Chronicle (${RELEASE}).iso")
set(EXTRACTED_DIR ${ROM_DIR}/extracted/iso)
set(EXTRACTED_ELF ${EXTRACTED_DIR}/${BASENAME})

file(RELATIVE_PATH BUILD_DIR ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR})
set(OUTPUT ${BUILD_DIR}/${BASENAME})
set(BUILT_ISO "${BUILD_DIR}/Dark Chronicle (${RELEASE} Build).iso")
set(STAMP_DIR ${BUILD_DIR}/stamps)
set(ROM_STAMP ${STAMP_DIR}/rom.verified)
set(EXTRACT_STAMP ${STAMP_DIR}/extracted)
set(SPLIT_STAMP ${STAMP_DIR}/disassembled)

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/${RETAIL_ISO}"
   AND NOT EXISTS "${CMAKE_SOURCE_DIR}/${EXTRACTED_ELF}")
    message(FATAL_ERROR
        "Nothing to split ${ASM_DIR}/ from. Place the ${RELEASE} disc image at "
        "${RETAIL_ISO}.")
endif()

find_program(PYTHON NAMES python3 python REQUIRED HINTS $ENV{VIRTUAL_ENV}/bin)
set(WIBO wibo)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(Toolchain)
include(ObjectLists)

# Configure reads main.yaml through layout.py, so either changing re-runs it.
# scripts/build/globs.sh rewrites globbed.txt when a source or header is added
# or removed; CONFIGURE_DEPENDS globs would re-check on every build instead.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/${CONFIG_DIR}/main.yaml
    ${CMAKE_SOURCE_DIR}/${SCRIPTS_DIR}/build/layout.py)
if(EXISTS ${CMAKE_BINARY_DIR}/globbed.txt)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        ${CMAKE_BINARY_DIR}/globbed.txt)
endif()

set(MWCCGAP_SOURCES ${CMAKE_SOURCE_DIR}/${TOOLS_DIR}/mwccgap/mwccgap/elf.py)

set(SATANSFIDDLE_DEPENDENCIES
    ${CMAKE_SOURCE_DIR}/${SCRIPTS_DIR}/build/satansfiddle-wibo.py
    ${SATANSFIDDLE_CONFIG})
find_program(SATANSFIDDLE_EXECUTABLE NAMES "${SATANSFIDDLE}" NO_CACHE)
if(SATANSFIDDLE_EXECUTABLE)
    list(APPEND SATANSFIDDLE_DEPENDENCIES ${SATANSFIDDLE_EXECUTABLE})
endif()

#
# Objects
#

# Game units, compiled from ps2/src/<unit>.cpp.
layout_rows(--list-units unit_rows)
set(CPP_OBJS "")
set(MISSING_SOURCES "")
foreach(row IN LISTS unit_rows)
    string(REPLACE "\t" ";" parts "${row}")
    list(GET parts 0 kind)
    list(GET parts 1 unit)
    list(GET parts 2 source)
    if(kind STREQUAL "cpp")
        if(NOT EXISTS ${CMAKE_SOURCE_DIR}/${source})
            list(APPEND MISSING_SOURCES ${source})
        endif()
        set(obj ${BUILD_DIR}/obj/${unit}.cpp.o)
        list(APPEND CPP_OBJS ${obj})
        add_cpp_object(${obj} ${source} ${unit})
    endif()
endforeach()
if(MISSING_SOURCES)
    list(JOIN MISSING_SOURCES "\n  " missing)
    message(FATAL_ERROR "main.yaml names game units with no source:\n  ${missing}")
endif()

# Library units and data-only units, assembled from the split.
layout_rows(--list-assembled assembled_rows)
set(ASM_OBJS "")
set(DATA_OBJS "")
set(SPLIT_FILES "")
foreach(row IN LISTS assembled_rows)
    string(REPLACE "\t" ";" parts "${row}")
    list(GET parts 0 kind)
    list(GET parts 1 reference)
    list(GET parts 2 obj)
    list(LENGTH parts nparts)
    set(fixup_args "")
    if(nparts GREATER 3)
        list(GET parts 3 fixup_text)
        separate_arguments(fixup_args UNIX_COMMAND "${fixup_text}")
    endif()
    set(obj ${BUILD_DIR}/${obj})
    if(kind STREQUAL "data")
        list(APPEND DATA_OBJS ${obj})
    else()
        list(APPEND ASM_OBJS ${obj})
    endif()
    list(APPEND SPLIT_FILES ${CMAKE_SOURCE_DIR}/${reference})
    add_asm_object(${obj} ${reference} "${fixup_args}")
endforeach()

# A data-only unit is linked only when the linker script places its object;
# one the script writes itself (`.rdata`) stays out, or a wildcard would pull
# in a second copy.
if(EXISTS ${CMAKE_SOURCE_DIR}/${LD_SCRIPT})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        ${CMAKE_SOURCE_DIR}/${LD_SCRIPT})
    file(READ ${CMAKE_SOURCE_DIR}/${LD_SCRIPT} lcf_text)
    set(placed "")
    foreach(obj IN LISTS DATA_OBJS)
        get_filename_component(name ${obj} NAME)
        string(FIND "${lcf_text}" "${name}" at)
        if(at GREATER_EQUAL 0)
            list(APPEND placed ${obj})
        endif()
    endforeach()
    set(DATA_OBJS ${placed})
endif()

set(ALL_OBJS ${CPP_OBJS} ${ASM_OBJS} ${DATA_OBJS})
make_object_dirs("${ALL_OBJS}")

#
# Link
#

# MWLD reads its objects from main_o_files: the units in link order, as
# layout.py gives it for build/pal, then the data-only objects the linker
# script places.
layout_rows(--link-order link_rows)
set(LINK_ORDER "")
foreach(path IN LISTS link_rows)
    string(REGEX REPLACE "^build/${RELEASE_DIR}/" "${BUILD_DIR}/" path "${path}")
    list(APPEND LINK_ORDER ${path})
endforeach()
list(APPEND LINK_ORDER ${DATA_OBJS})
list(REMOVE_DUPLICATES LINK_ORDER)
list(JOIN LINK_ORDER "\n" link_text)
set(MAIN_O_FILES ${BUILD_DIR}/main_o_files)
file(GENERATE OUTPUT ${CMAKE_SOURCE_DIR}/${MAIN_O_FILES} CONTENT "${link_text}\n")

set(ABS_OBJS "")
foreach(obj IN LISTS ALL_OBJS)
    list(APPEND ABS_OBJS ${CMAKE_SOURCE_DIR}/${obj})
endforeach()

add_custom_command(
    OUTPUT ${CMAKE_SOURCE_DIR}/${OUTPUT}
    COMMAND ${LD} ${LD_FLAGS} -o ${OUTPUT} ${LD_SCRIPT} @${MAIN_O_FILES}
    DEPENDS ${ABS_OBJS}
            ${CMAKE_SOURCE_DIR}/${LD_SCRIPT}
            ${CMAKE_SOURCE_DIR}/${MAIN_O_FILES}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Linking ${OUTPUT}"
    VERBATIM)

add_custom_target(elf DEPENDS ${CMAKE_SOURCE_DIR}/${OUTPUT})

# The compiler flags, for the diffing and decompiling tools.
string(JOIN " " CC_FLAGS_TEXT ${CC_FLAGS})
file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/compiler_flags.txt
     CONTENT "${CC_FLAGS_TEXT}\n")

#
# Verification
#

# BUILD_DIR names the build directory being verified.
set(VERIFY ${CMAKE_COMMAND} -E env BUILD_DIR=${BUILD_DIR}
    ${PYTHON} -u ${SCRIPTS_DIR}/build/verify.py)

add_custom_target(build ALL
    COMMAND ${VERIFY} -c
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    USES_TERMINAL
    VERBATIM)
add_dependencies(build elf)

add_custom_target(verify_extracted
    COMMAND ${VERIFY} -e
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    USES_TERMINAL
    VERBATIM)

#
# Setup: the disc, what is extracted from it, and the split
#

# ps2/asm/ is never committed; it is split from the executable extracted from
# the disc, which is checked against rom/pal/extracted.sha256 first. The disc
# itself is checked and extracted only when it is present.
if(EXISTS "${CMAKE_SOURCE_DIR}/${RETAIL_ISO}")
    add_custom_command(
        OUTPUT ${CMAKE_SOURCE_DIR}/${ROM_STAMP}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${STAMP_DIR}
        COMMAND ${CMAKE_COMMAND} -E chdir ${ROM_DIR} sha256sum -c checksum.sha256
        COMMAND ${CMAKE_COMMAND} -E touch ${ROM_STAMP}
        DEPENDS "${CMAKE_SOURCE_DIR}/${RETAIL_ISO}"
                ${CMAKE_SOURCE_DIR}/${ROM_DIR}/checksum.sha256
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Checking the retail disc image"
        USES_TERMINAL
        VERBATIM)

    add_custom_command(
        OUTPUT ${CMAKE_SOURCE_DIR}/${EXTRACT_STAMP}
        COMMAND ${PYTHON} -u ${SCRIPTS_DIR}/build/extract.py
        COMMAND ${CMAKE_COMMAND} -E touch ${EXTRACT_STAMP}
        DEPENDS ${CMAKE_SOURCE_DIR}/${ROM_STAMP}
                ${CMAKE_SOURCE_DIR}/${SCRIPTS_DIR}/build/extract.py
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Extracting the disc"
        USES_TERMINAL
        VERBATIM)
    set(EXTRACTED ${CMAKE_SOURCE_DIR}/${EXTRACT_STAMP})
else()
    set(EXTRACTED ${CMAKE_SOURCE_DIR}/${EXTRACTED_ELF})
endif()

set(SPLIT_COMMANDS
    COMMAND ${CMAKE_COMMAND} -E chdir rom sha256sum -c ${RELEASE_DIR}/extracted.sha256
    COMMAND ${PYTHON} -u ${SCRIPTS_DIR}/build/symbols.py --check
    COMMAND ${PYTHON} -u ${SCRIPTS_DIR}/build/disassemble.py
    COMMAND ${CMAKE_COMMAND} -E make_directory ${STAMP_DIR}
    COMMAND ${CMAKE_COMMAND} -E touch ${SPLIT_STAMP})

file(GLOB SPLAT_EXTENSIONS ${CMAKE_SOURCE_DIR}/${TOOLS_DIR}/splat_ext/*.py)

# The files the objects are assembled from are named as the split's
# byproducts, so a build of any target knows the split makes them.
add_custom_command(
    OUTPUT ${CMAKE_SOURCE_DIR}/${SPLIT_STAMP}
    BYPRODUCTS ${SPLIT_FILES}
    ${SPLIT_COMMANDS}
    DEPENDS ${EXTRACTED}
            ${CMAKE_SOURCE_DIR}/${ROM_DIR}/extracted.sha256
            ${CMAKE_SOURCE_DIR}/${CONFIG_DIR}/main.yaml
            ${CMAKE_SOURCE_DIR}/${CONFIG_DIR}/main.symbols.txt
            ${CMAKE_SOURCE_DIR}/${SCRIPTS_DIR}/build/symbols.py
            ${CMAKE_SOURCE_DIR}/${SCRIPTS_DIR}/build/disassemble.py
            ${CMAKE_SOURCE_DIR}/${SCRIPTS_DIR}/build/layout.py
            ${SPLAT_EXTENSIONS}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Splitting the executable"
    USES_TERMINAL
    VERBATIM)

add_custom_target(setup DEPENDS ${CMAKE_SOURCE_DIR}/${SPLIT_STAMP})

add_custom_target(disassemble
    ${SPLIT_COMMANDS}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    USES_TERMINAL
    VERBATIM)

#
# The disc image
#

if(EXISTS "${CMAKE_SOURCE_DIR}/${RETAIL_ISO}")
    add_custom_command(
        OUTPUT "${CMAKE_SOURCE_DIR}/${BUILT_ISO}"
        COMMAND ${PYTHON} -u ${SCRIPTS_DIR}/build/make_iso.py
                --source "${RETAIL_ISO}" --out "${BUILT_ISO}" ${OUTPUT}
        DEPENDS ${CMAKE_SOURCE_DIR}/${OUTPUT}
                ${CMAKE_SOURCE_DIR}/${ROM_STAMP}
                ${CMAKE_SOURCE_DIR}/${SCRIPTS_DIR}/build/make_iso.py
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Mastering ${BUILT_ISO}"
        USES_TERMINAL
        VERBATIM)
    add_custom_target(iso DEPENDS "${CMAKE_SOURCE_DIR}/${BUILT_ISO}")
else()
    add_custom_target(iso
        COMMAND ${CMAKE_COMMAND} -E echo "Mastering a disc needs ${RETAIL_ISO}."
        COMMAND ${CMAKE_COMMAND} -E false
        VERBATIM)
endif()

add_custom_target(run
    COMMAND ${SCRIPTS_DIR}/host/pcsx2.sh "${BUILT_ISO}"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    USES_TERMINAL
    VERBATIM)
add_dependencies(run iso)

#
# objdiff and m2c
#

include(Objdiff)
