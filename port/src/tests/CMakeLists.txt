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
    ${CMAKE_CURRENT_SOURCE_DIR}/platform_config_test.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/smoke_test.cpp)
add_executable(darkchronicle_tests ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp ${TEST_SOURCES})
set_target_properties(darkchronicle_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_include_directories(darkchronicle_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
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
gtest_discover_tests(darkchronicle_tests
    DISCOVERY_MODE PRE_TEST
    TEST_FILTER -PlatformClock.*
    PROPERTIES TIMEOUT ${CT_TEST_TIMEOUT})
gtest_discover_tests(darkchronicle_tests
    DISCOVERY_MODE PRE_TEST
    TEST_FILTER PlatformClock.*
    PROPERTIES TIMEOUT ${CT_TEST_TIMEOUT} RUN_SERIAL TRUE)
