function(ct_copy_windows_runtime target)
    if(NOT WIN32)
        return()
    endif()
    get_filename_component(CT_MINGW_BIN ${CMAKE_CXX_COMPILER} DIRECTORY)
    set(CT_MINGW_RUNTIME ${CT_MINGW_BIN}/../x86_64-w64-mingw32/bin)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SDL3::SDL3>
            ${CT_MINGW_RUNTIME}/libc++.dll
            ${CT_MINGW_RUNTIME}/libunwind.dll
            ${CT_MINGW_RUNTIME}/libwinpthread-1.dll
            $<TARGET_FILE_DIR:${target}>
        VERBATIM)
endfunction()
