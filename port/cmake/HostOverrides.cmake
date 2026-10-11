# Call after kz_port_host_library(ct_host). These game-specific host units replace
# the corresponding pinned Kazarov sources and are hash-checked against their base.
function(ct_apply_host_overrides target port_dir kz_root)
    file(GLOB_RECURSE CT_HOST_OVERRIDE_SOURCES CONFIGURE_DEPENDS
        ${port_dir}/host_overrides/*.cpp)
    get_target_property(CT_HOST_SOURCES ${target} SOURCES)
    list(FILTER CT_HOST_SOURCES EXCLUDE REGEX "/port/(gfx/(pipelines|resources)|audio/mixer)\\.cpp$")
    set_property(TARGET ${target} PROPERTY SOURCES ${CT_HOST_SOURCES} ${CT_HOST_OVERRIDE_SOURCES})
    target_include_directories(${target} BEFORE PRIVATE ${port_dir}/host_overrides)
    set(CT_HOST_OVERRIDE_BASES
        gfx/pipelines.cpp b2187d17501cb9d2f056c0792ffbaaa751e6fb067cfcb0d30a49eb25e5d4d5de
        gfx/resources.cpp 32db6591dea7a9d4713846082b5e958d7fe22d90b2963359429dc64a225a0050
        audio/mixer.cpp 72413488cb79f69fad65ce0731afa338fa254fe588a13d990605a381f0bf89a1
        audio/mixer.hpp 3a5c1a8a2d1f2265b394d85ec494b7b9d0145390e0a0b8b1fc93eb27931de1aa)
    list(LENGTH CT_HOST_OVERRIDE_BASES CT_HOST_OVERRIDE_COUNT)
    math(EXPR CT_HOST_OVERRIDE_LAST "${CT_HOST_OVERRIDE_COUNT} - 1")
    foreach(index RANGE 0 ${CT_HOST_OVERRIDE_LAST} 2)
        math(EXPR hash_index "${index} + 1")
        list(GET CT_HOST_OVERRIDE_BASES ${index} unit)
        list(GET CT_HOST_OVERRIDE_BASES ${hash_index} expected)
        file(SHA256 ${kz_root}/port/${unit} actual)
        if(NOT actual STREQUAL expected)
            message(FATAL_ERROR "tools/kazarov/port/${unit} changed since port/src/host_overrides/${unit} was made from it; merge the change and update CT_HOST_OVERRIDE_BASES")
        endif()
    endforeach()
endfunction()
