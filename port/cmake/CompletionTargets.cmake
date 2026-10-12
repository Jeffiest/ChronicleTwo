# Component contracts run without gameplay or user saves.
foreach(name IN ITEMS model_storage async_contract dc2_save_roundtrip)
    add_executable(ct_${name}_test EXCLUDE_FROM_ALL ${PORT_DIR}/tests/${name}_standalone.cpp)
    ct_link_game(ct_${name}_test)
    set_target_properties(ct_${name}_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    target_compile_options(ct_${name}_test PRIVATE -UNDEBUG)
endforeach()
foreach(name IN ITEMS menu_model_memory serialized_records)
    add_executable(ct_${name}_test EXCLUDE_FROM_ALL ${PORT_DIR}/tests/${name}_standalone.cpp)
    set_target_properties(ct_${name}_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
    target_include_directories(ct_${name}_test PRIVATE ${CMAKE_SOURCE_DIR}/port/include)
    target_compile_options(ct_${name}_test PRIVATE -UNDEBUG)
endforeach()
add_executable(ct_sound_stream_data_test EXCLUDE_FROM_ALL
    ${PORT_DIR}/tests/sound_stream_data_standalone.cpp ${PORT_DIR}/sound_stream_data.cpp)
target_compile_features(ct_sound_stream_data_test PRIVATE cxx_std_20)
target_compile_options(ct_sound_stream_data_test PRIVATE -UNDEBUG)
add_executable(ct_pss_demux_test EXCLUDE_FROM_ALL
    ${PORT_DIR}/tests/pss_demux_standalone.cpp ${PORT_DIR}/video/pss.cpp)
target_compile_features(ct_pss_demux_test PRIVATE cxx_std_20)
target_compile_options(ct_pss_demux_test PRIVATE -UNDEBUG)
set_target_properties(ct_sound_stream_data_test ct_pss_demux_test
    PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
add_custom_target(ct_active_contract_check
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/port/check_active_contracts.py
        --build-dir ${CMAKE_BINARY_DIR} --check
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/port/tests/test_active_contracts_batch05.py
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} VERBATIM)
add_dependencies(ct_ps2 ct_active_contract_check)
function(ct_add_completion_suite target compatibility)
add_custom_target(${target}
    COMMAND $<TARGET_FILE:ct_menu_model_memory_test>
    COMMAND $<TARGET_FILE:ct_model_storage_test>
    COMMAND $<TARGET_FILE:ct_serialized_records_test>
    COMMAND $<TARGET_FILE:ct_sound_stream_data_test>
    COMMAND $<TARGET_FILE:ct_pss_demux_test>
    COMMAND $<TARGET_FILE:ct_dc2_save_roundtrip_test>
    COMMAND $<TARGET_FILE:ct_async_contract_test>
    DEPENDS ct_menu_model_memory_test ct_model_storage_test ct_serialized_records_test
        ct_sound_stream_data_test ct_pss_demux_test ct_dc2_save_roundtrip_test ct_async_contract_test
        ${compatibility}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} VERBATIM)
endfunction()
ct_add_completion_suite(ct_completion_tests ct_compatibility_tests)
ct_add_completion_suite(ct_completion_tests_headless ct_compatibility_tests_headless)
