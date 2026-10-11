# The tools the PS2 build runs, and their flags: the one place to change them.

set(MIPS_TOOL_PREFIX mips-ps2-decompals-
    CACHE STRING "Prefix of the binutils-mips-ps2-decompals tools")
set(AS ${MIPS_TOOL_PREFIX}as)

# GNU as, for the split assembly of the library units and the data-only units.
set(AS_FLAGS -EL -march=r5900 -mabi=eabi -mno-pdr -non_shared -G0 -I ${INCLUDE_DIR})

# MWCC, through Satan's Fiddle (scripts/build/mwccgap.sh), for the game units.
# Satan's Fiddle controls compiler state; MWLD below continues to use plain wibo.
if(DEFINED ENV{SATANSFIDDLE})
    set(SATANSFIDDLE_DEFAULT "$ENV{SATANSFIDDLE}")
else()
    set(SATANSFIDDLE_DEFAULT satansfiddle)
endif()
set(SATANSFIDDLE "${SATANSFIDDLE_DEFAULT}"
    CACHE STRING "Satan's Fiddle executable or command")
if(DEFINED ENV{SATANSFIDDLE_CONFIG})
    set(SATANSFIDDLE_CONFIG_DEFAULT "$ENV{SATANSFIDDLE_CONFIG}")
else()
    set(SATANSFIDDLE_CONFIG_DEFAULT "${CMAKE_SOURCE_DIR}/scripts/build/satansfiddle.json")
endif()
set(SATANSFIDDLE_CONFIG "${SATANSFIDDLE_CONFIG_DEFAULT}"
    CACHE FILEPATH "Satan's Fiddle JSON compiler profile")

# `-MD` has it write the header dependencies the wrapper turns into a depfile.
set(MW_CC_DIR ${TOOLS_DIR}/compilers/mw/3.0-011126
    CACHE STRING "Directory holding mwccps2.exe")
set(CC_FLAGS -O3,p -strings readonly -c -Cpp_exceptions off -RTTI off
    -pragma "divbyzerocheck on" -i ${INCLUDE_DIR})
# Units built with other optimization settings than the rest of the game. Each
# list is appended after CC_FLAGS, so its options override CC_FLAGS'.
set(UNIT_CC_FLAGS_mg_dataset -O2,p)
set(UNIT_CC_FLAGS_mg_shadow -O2,p)
set(UNIT_CC_FLAGS_mg_tanime -O2,p)
set(UNIT_CC_FLAGS_mg_texture -O2,p)
set(UNIT_CC_FLAGS_password -O0)
set(UNIT_CC_FLAGS_scriptinterpreter -inline off)
set(CC_DEP_FLAGS -MD)

# MWLD, run under wibo, links the executable from every object in
# <build>/main_o_files, placed by the linker script.
set(MW_LD ${TOOLS_DIR}/compilers/mw/2.4-001213/mwldps2.exe
    CACHE STRING "The MWLD executable that links the game")
set(LD ${WIBO} ${MW_LD})
set(LD_FLAGS -map -nostdlib -m ENTRYPOINT -nodead -g)
set(LD_SCRIPT ${CONFIG_DIR}/${BASENAME}.lcf)
