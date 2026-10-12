# Current compiler flags and production callbacks are checked on ordinary game builds.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_custom_target(ct_runtime_contract_check
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/port/check_runtime_contracts.py
        --source ${PORT_DIR}/mg_visual.cpp --compile-commands ${CMAKE_BINARY_DIR}/compile_commands.json
        --negative-control
    DEPENDS ps2_include WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} VERBATIM)
if(TARGET ct_win_headers)
    add_dependencies(ct_runtime_contract_check ct_win_headers)
endif()
add_dependencies(ct_port ct_runtime_contract_check)
set(ct_runtime_tests "")
function(ct_runtime_test target name)
    target_compile_options(${target} PRIVATE -UNDEBUG)
    set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    ct_copy_windows_runtime(${target})
    if(CT_BUILD_TESTS)
        add_test(NAME RuntimeContract.${name} COMMAND $<TARGET_FILE:${target}> ${ARGN})
        set_tests_properties(RuntimeContract.${name} PROPERTIES SKIP_RETURN_CODE 77
            TIMEOUT ${CT_TEST_TIMEOUT} ENVIRONMENT "DC2_AUDIO=off")
    endif()
    set(ct_runtime_tests ${ct_runtime_tests} ${target} PARENT_SCOPE)
endfunction()
foreach(pair IN ITEMS "05_image_copy" "06_atlas_storage" "07_texture_lifetime" "08_hud_state" "12_sound_bridge" "12_weighted_models" "13_texture_tables")
    add_executable(ct_wp${pair}_test EXCLUDE_FROM_ALL ${PORT_DIR}/tests/wp${pair}_standalone.cpp)
    ct_link_game(ct_wp${pair}_test)
    ct_runtime_test(ct_wp${pair}_test ${pair} ${CMAKE_SOURCE_DIR}/data)
endforeach()
foreach(number IN ITEMS 01 02 03)
    if(number STREQUAL "01")
        set(name action_callbacks)
        set(mode fixture)
    elseif(number STREQUAL "02")
        set(name action_vm)
        set(mode fixture)
    else()
        set(name combat_consumer)
        set(mode fixture-motion)
    endif()
    set(callbacks ${CMAKE_BINARY_DIR}/runtime_contracts/wp${number}_callbacks.cpp)
    add_custom_command(OUTPUT ${callbacks}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/runtime_contracts
        COMMAND ${Python3_EXECUTABLE} ${HOST_ACTION_CALLBACKS} ${mode}
            ${CMAKE_BINARY_DIR}/ps2_src/actscript.cpp ${callbacks}
        DEPENDS ${CMAKE_BINARY_DIR}/ps2_src/actscript.cpp ${HOST_ACTION_CALLBACKS} VERBATIM)
    add_executable(ct_wp${number}_${name}_test EXCLUDE_FROM_ALL
        ${PORT_DIR}/tests/wp${number}_${name}_standalone.cpp ${PORT_DIR}/padcontrol.cpp ${callbacks})
    if(NOT number STREQUAL "01")
        target_sources(ct_wp${number}_${name}_test PRIVATE ${CMAKE_BINARY_DIR}/ps2_src/runscript.cpp)
    endif()
    target_link_libraries(ct_wp${number}_${name}_test PRIVATE ct_options kz_port_headers)
    target_link_options(ct_wp${number}_${name}_test PRIVATE ${CT_LINK_OPTIONS})
    target_include_directories(ct_wp${number}_${name}_test PRIVATE ${PORT_DIR} ${PORT_DIR}/tests)
    target_compile_options(ct_wp${number}_${name}_test PRIVATE
        -ftrivial-auto-var-init=zero -fno-strict-return -fno-strict-aliasing -fwrapv)
    add_dependencies(ct_wp${number}_${name}_test ps2_include)
    if(TARGET ct_win_headers)
        add_dependencies(ct_wp${number}_${name}_test ct_win_headers)
    endif()
    ct_runtime_test(ct_wp${number}_${name}_test ${number}_${name} ${CMAKE_SOURCE_DIR}/data)
endforeach()
add_executable(ct_wp04_archive_extents_test EXCLUDE_FROM_ALL
    ${PORT_DIR}/tests/wp04_archive_extents_standalone.cpp ${PORT_DIR}/dataread.cpp
    ${CMAKE_SOURCE_DIR}/tools/kazarov/port/platform/paths.cpp)
target_link_libraries(ct_wp04_archive_extents_test PRIVATE ct_options kz_port_headers)
target_include_directories(ct_wp04_archive_extents_test PRIVATE ${PORT_DIR})
target_link_options(ct_wp04_archive_extents_test PRIVATE ${CT_LINK_OPTIONS})
add_dependencies(ct_wp04_archive_extents_test ps2_include)
if(TARGET ct_win_headers)
    add_dependencies(ct_wp04_archive_extents_test ct_win_headers)
endif()
ct_runtime_test(ct_wp04_archive_extents_test 04_archive_extents ${CMAKE_SOURCE_DIR}/rom/pal/extracted/iso)
if(CT_BUILD_TESTS)
    add_test(NAME RuntimeContract.action_decoder COMMAND ${Python3_EXECUTABLE}
        ${PORT_DIR}/tests/wp02_action_vm_decoder_test.py ${CMAKE_SOURCE_DIR}/data)
    set_tests_properties(RuntimeContract.action_decoder PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT ${CT_TEST_TIMEOUT})
endif()
add_custom_target(ct_runtime_contract_tests DEPENDS ct_runtime_contract_check ${ct_runtime_tests})
foreach(target IN ITEMS darkchronicle_tests ct_completion_tests ct_completion_tests_headless)
    if(TARGET ${target})
        add_dependencies(${target} ct_runtime_contract_tests)
    endif()
endforeach()

set(ct_effect_vectors ${CMAKE_BINARY_DIR}/runtime_contracts/wp13_effect_vectors.cpp)
add_custom_command(OUTPUT ${ct_effect_vectors}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/runtime_contracts
    COMMAND ${Python3_EXECUTABLE} ${HOST_VECTOR_CONTRACTS} fixture
        ${CMAKE_BINARY_DIR}/ps2_src/effscript.cpp ${ct_effect_vectors}
    DEPENDS ${CMAKE_BINARY_DIR}/ps2_src/effscript.cpp ${HOST_VECTOR_CONTRACTS} ${HOST_ACTION_CALLBACKS} VERBATIM)
add_executable(ct_character_vectors_test EXCLUDE_FROM_ALL
    ${PORT_DIR}/tests/wp13_character_vectors_standalone.cpp ${ct_effect_vectors})
ct_link_game(ct_character_vectors_test)
ct_runtime_test(ct_character_vectors_test character_vectors)
add_dependencies(ct_runtime_contract_tests ct_character_vectors_test)
if(CT_BUILD_TESTS)
    add_test(NAME RuntimeContract.vector_rewrite COMMAND ${Python3_EXECUTABLE}
        ${HOST_VECTOR_CONTRACTS} test ${CMAKE_SOURCE_DIR}/ps2/src)
    set_tests_properties(RuntimeContract.vector_rewrite PROPERTIES TIMEOUT ${CT_TEST_TIMEOUT})
endif()
