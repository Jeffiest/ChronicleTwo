# darkchronicle_tests links the whole port and game so a test can exercise a replacement unit against the
# real game types. Each *_test.cpp holds GoogleTest cases; ctest runs one process per case, since the
# game's state is global.
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

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp)
add_executable(darkchronicle_tests ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp ${TEST_SOURCES})
set_target_properties(darkchronicle_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_include_directories(darkchronicle_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
ct_link_game(darkchronicle_tests)
kz_port_tests(darkchronicle_tests)
target_link_libraries(darkchronicle_tests PRIVATE GTest::gtest nlohmann_json::nlohmann_json)

# A case is a unit test: it finishes in milliseconds, and one that does not is cut off. The clock
# cases measure short sleeps, which the parallel gfx cases skew, so they run on their own. The cases that compile
# pipelines or create a device per case take longer than 5 s under a parallel run, on lavapipe and on Windows, so they get 30 s.
set(CT_TEST_TIMEOUT 5 CACHE STRING "Seconds a test case may run before it is cut off")
gtest_discover_tests(darkchronicle_tests
    DISCOVERY_MODE PRE_TEST
    TEST_FILTER -PlatformClock.*:Vu1Prog0Shader.*:GfxMeshDc2.*:MgTextureGpu.*:MgTanimeGpu.*:GfxMisc.PipelineCache
    PROPERTIES TIMEOUT ${CT_TEST_TIMEOUT})
gtest_discover_tests(darkchronicle_tests
    DISCOVERY_MODE PRE_TEST
    TEST_FILTER PlatformClock.*
    PROPERTIES TIMEOUT ${CT_TEST_TIMEOUT} RUN_SERIAL TRUE)
gtest_discover_tests(darkchronicle_tests
    DISCOVERY_MODE PRE_TEST
    TEST_FILTER Vu1Prog0Shader.*:GfxMeshDc2.*:MgTextureGpu.*:MgTanimeGpu.*:GfxMisc.PipelineCache
    PROPERTIES TIMEOUT 30)

add_dependencies(darkchronicle_tests darkchronicle)
# ctest reports a process that dies by abort() as failed even when PASS_REGULAR_EXPRESSION matches its
# output, so the executable runs under a script that checks the run and the file it writes, and the regex
# judges the script's verdict. Its shader compile loads the CPU, so it runs alone with the clock cases.
# The smoke data directory is empty on purpose, so the run stays in the logo fade and reaches frame 3 without game data.
set(SMOKE_SCRIPT ${CMAKE_CURRENT_BINARY_DIR}/smoke_headless.cmake)
file(WRITE ${SMOKE_SCRIPT} [=[
file(MAKE_DIRECTORY "$ENV{DC2_DATA}")
file(MAKE_DIRECTORY "$ENV{DC2_SAVE}")
file(REMOVE "$ENV{DC2_SAVE}/smoke.png")
execute_process(COMMAND "${EXE}" --headless --offscreen --frames 3 --screenshot "$ENV{DC2_SAVE}/smoke.png"
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 120)
set(log "${out}${err}")
message("${log}")
set(png "$ENV{DC2_SAVE}/smoke.png")
set(sig "")
if(EXISTS "${png}")
    file(READ "${png}" sig LIMIT 8 HEX)
endif()
string(FIND "${log}" "dataread: indexed " index_at)
string(FIND "${log}" "mglib: frame 3 written to" frame_at)
if(rc EQUAL 0 AND index_at GREATER -1 AND frame_at GREATER -1 AND sig STREQUAL "89504e470d0a1a0a")
    message("SMOKE_OK")
endif()
]=])
add_test(NAME Smoke.HeadlessFramesScreenshot
    COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:darkchronicle> -P ${SMOKE_SCRIPT})
set_tests_properties(Smoke.HeadlessFramesScreenshot PROPERTIES
    PASS_REGULAR_EXPRESSION "SMOKE_OK"
    ENVIRONMENT "DC2_SAVE=${CMAKE_BINARY_DIR}/smoke-save;DC2_DATA=${CMAKE_BINARY_DIR}/smoke-data"
    TIMEOUT 120 RUN_SERIAL TRUE)

file(GLOB ct_stream_cmake CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/cmake/*.cmake)
foreach(f IN LISTS ct_stream_cmake)
    include(${f})
endforeach()
