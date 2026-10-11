# Fast, opt-in compatibility executables used by scripts/port/run_compatibility_tests.py.
set(CT_TESTS ${PORT_DIR}/tests)
add_executable(ct_arena_memory_test EXCLUDE_FROM_ALL
    ${CT_TESTS}/mg_memory_arena_standalone.cpp ${PORT_DIR}/mg_memory.cpp)
target_link_libraries(ct_arena_memory_test PRIVATE ct_options)
add_executable(ct_menu_loader_test EXCLUDE_FROM_ALL
    ${CT_TESTS}/menu_loader_standalone.cpp ${PORT_DIR}/menucommon.cpp ${PORT_DIR}/mg_memory.cpp)
target_link_libraries(ct_menu_loader_test PRIVATE ct_options)
add_executable(ct_villager_outputs_test EXCLUDE_FROM_ALL ${CT_TESTS}/villager_outputs_standalone.cpp)
target_link_libraries(ct_villager_outputs_test PRIVATE ct_options)
if(WIN32)
    foreach(target IN ITEMS ct_arena_memory_test ct_menu_loader_test ct_villager_outputs_test)
        add_dependencies(${target} ct_win_headers)
    endforeach()
endif()

foreach(test_name IN ITEMS script_storage monster_memory input_lifecycle color_capture photo_name_storage)
    if(test_name STREQUAL "script_storage")
        set(test_source ${CT_TESTS}/script_storage.cpp)
    elseif(test_name STREQUAL "monster_memory")
        set(test_source ${CT_TESTS}/monster_memory_standalone.cpp)
    elseif(test_name STREQUAL "input_lifecycle")
        set(test_source ${CT_TESTS}/input_lifecycle_test.cpp)
    elseif(test_name STREQUAL "color_capture")
        set(test_source ${CT_TESTS}/color_capture_standalone.cpp)
    else()
        set(test_source ${CT_TESTS}/photo_name_storage_standalone.cpp)
    endif()
    add_executable(ct_${test_name}_test EXCLUDE_FROM_ALL ${test_source})
    set_target_properties(ct_${test_name}_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    ct_link_game(ct_${test_name}_test)
    target_compile_options(ct_${test_name}_test PRIVATE -UNDEBUG)
endforeach()

set(CT_MENU_OPTION_FIXTURE ${CMAKE_BINARY_DIR}/tests/menu_option_body.cpp)
add_custom_command(OUTPUT ${CT_MENU_OPTION_FIXTURE}
    COMMAND ${Python3_EXECUTABLE} ${HOST_MENU_POINTERS} fixture src/menuop.cpp
        ${CMAKE_BINARY_DIR}/ps2_src/menuop.cpp ${CT_MENU_OPTION_FIXTURE}
    DEPENDS ${CMAKE_BINARY_DIR}/ps2_src/menuop.cpp ${HOST_MENU_POINTERS}
    VERBATIM)
add_executable(ct_menu_option_test EXCLUDE_FROM_ALL
    ${CT_TESTS}/menu_option_standalone.cpp ${CT_MENU_OPTION_FIXTURE})
set_target_properties(ct_menu_option_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_link_libraries(ct_menu_option_test PRIVATE ct_options)
target_compile_options(ct_menu_option_test PRIVATE -UNDEBUG)
if(WIN32)
    add_dependencies(ct_menu_option_test ct_win_headers)
endif()

add_custom_target(ct_compatibility_audit
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/port/compatibility_audit.py --check
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/port/test_host_script_storage.py
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/port/test_host_menu_pointers.py
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/port/test_host_photo_names.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} VERBATIM)
add_custom_target(ct_pointer_array_check
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/port/check_host_pointer_arrays.py
        --generated ${CMAKE_BINARY_DIR}/ps2_src --host ${PORT_DIR}
    DEPENDS ${PS2_UNITS} WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} VERBATIM)
add_dependencies(ct_ps2 ct_compatibility_audit ct_pointer_array_check)

function(ct_add_compatibility_suite target headless)
    if(headless)
        set(capture_args --skip-capture)
        set(capture_dependency "")
    else()
        set(capture_args --capture $<TARGET_FILE:ct_color_capture_test>)
        set(capture_dependency ct_color_capture_test)
    endif()
    add_custom_target(${target}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/port/tests/test_compatibility_audit.py
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/port/tests/test_host_pointer_arrays.py
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/port/run_compatibility_tests.py
            --arena $<TARGET_FILE:ct_arena_memory_test>
            --scripts $<TARGET_FILE:ct_script_storage_test>
            --monsters $<TARGET_FILE:ct_monster_memory_test>
            --input $<TARGET_FILE:ct_input_lifecycle_test>
            ${capture_args}
            --menus $<TARGET_FILE:ct_menu_loader_test>
            --options $<TARGET_FILE:ct_menu_option_test>
            --villagers $<TARGET_FILE:ct_villager_outputs_test>
            --photos $<TARGET_FILE:ct_photo_name_storage_test>
        DEPENDS ct_arena_memory_test ct_script_storage_test ct_monster_memory_test
            ct_input_lifecycle_test ${capture_dependency} ct_menu_loader_test ct_menu_option_test
            ct_villager_outputs_test ct_photo_name_storage_test
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} VERBATIM)
endfunction()
ct_add_compatibility_suite(ct_compatibility_tests FALSE)
ct_add_compatibility_suite(ct_compatibility_tests_headless TRUE)

# Existing scripts and CI can keep their compatibility target names.
add_custom_target(dc_compatibility_tests DEPENDS ct_compatibility_tests)
add_custom_target(dc_compatibility_tests_headless DEPENDS ct_compatibility_tests_headless)
