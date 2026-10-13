# dc2data extracts the PAL disc's data into a plain directory for the port. Its core is header-only,
# shared with the port's tests, so the tool links no game code.
add_executable(dc2data ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
set_target_properties(dc2data PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set_target_properties(dc2data PROPERTIES CXX_STANDARD 26 CXX_STANDARD_REQUIRED ON)
target_compile_options(dc2data PRIVATE -Wall)
