# darkchronicle_tests links the whole port and game so a test can exercise a
# replacement unit against the real game types. Each *_test.cpp holds
# GoogleTest cases; ctest runs one process per case, since the game's state is
# global.
include(FetchContent)
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.18.0
    GIT_SHALLOW TRUE
    FIND_PACKAGE_ARGS NAMES GTest)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
include(GoogleTest)

# The cases here are the ones that build against this game; Kazarov's shared host cases are added
# by kz_port_tests. The other *_test.cpp files here still include another game's headers.
set(TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/occlusion_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/game_stream_mixer_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/native_math_contract_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/collision_asm_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/data_extract_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dataread_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/editcoll_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/editmap2_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/eekernel_threads_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/gameutil_asm_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/long64_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/memcard_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mg_frame_asm_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mg_math_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mg_memory_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mg_texture_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mglib_time_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/platform_config_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/platform_pad_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/vu1_prog0_shader_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/smoke_test.cpp)
add_executable(darkchronicle_tests ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp ${TEST_SOURCES})
set_target_properties(darkchronicle_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_include_directories(darkchronicle_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/data.cmake)
ct_link_game(darkchronicle_tests)
kz_port_tests(darkchronicle_tests)
# Written against another game's libmc declarations (a raw byte table for sceMcGetDir), which this
# game's SDK headers type differently.
get_target_property(test_sources darkchronicle_tests SOURCES)
list(FILTER test_sources EXCLUDE REGEX "/platform_mc_test\\.cpp$")
set_property(TARGET darkchronicle_tests PROPERTY SOURCES ${test_sources})
target_link_libraries(darkchronicle_tests PRIVATE GTest::gtest)

# A case is a unit test: it finishes in milliseconds, and one that does not is cut off. The clock
# cases measure short sleeps, which the parallel gfx cases skew, so they run on their own.
set(CT_TEST_TIMEOUT 5 CACHE STRING "Seconds a test case may run before it is cut off")

# GS packet assertions describe the upstream reference implementation. The native
# renderer submits host draw state instead, so keep these contracts in a separate target.
add_executable(ct_drawprim_reference_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/mg_drawprim_asm_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/drawprim_reference.cpp
    ${PORT_DIR}/mg_drawprim_asm.cpp)
target_link_libraries(ct_drawprim_reference_tests PRIVATE ct_options GTest::gtest_main)
target_include_directories(ct_drawprim_reference_tests PRIVATE ${PORT_DIR})
set_target_properties(ct_drawprim_reference_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
add_dependencies(ct_drawprim_reference_tests ps2_include)
if(TARGET ct_win_headers)
    add_dependencies(ct_drawprim_reference_tests ct_win_headers)
endif()
add_dependencies(darkchronicle_tests ct_drawprim_reference_tests)
gtest_discover_tests(ct_drawprim_reference_tests
    DISCOVERY_MODE PRE_TEST
    TEST_PREFIX UpstreamReference.
    PROPERTIES TIMEOUT ${CT_TEST_TIMEOUT})
gtest_discover_tests(darkchronicle_tests
    DISCOVERY_MODE PRE_TEST
    TEST_FILTER -PlatformClock.*
    PROPERTIES TIMEOUT ${CT_TEST_TIMEOUT})
gtest_discover_tests(darkchronicle_tests
    DISCOVERY_MODE PRE_TEST
    TEST_FILTER PlatformClock.*
    PROPERTIES TIMEOUT ${CT_TEST_TIMEOUT} RUN_SERIAL TRUE)
