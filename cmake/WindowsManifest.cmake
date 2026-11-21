# Added: the Windows application manifest that turns on the UTF-8 process code page. The game,
# the headless runner and the tests pass UTF-8 paths (SDL, the file pickers, the user directory)
# to C runtime calls such as fopen_s, _stat and _mkdir, which use the ANSI code page unless the
# manifest asks for UTF-8 (Windows 10 1903+). Earlier systems ignore the setting.
#
#   j3d_add_windows_manifest(<target>)
#
# adds the manifest to an executable target on Windows: MSVC embeds a .manifest source with
# mt.exe, MinGW embeds the same file through a generated resource script. Non-Windows hosts and
# other compilers do nothing. Call it after add_executable().

set(JONES3D_WINDOWS_MANIFEST "${CMAKE_CURRENT_LIST_DIR}/windows/utf8.manifest")

# Added: windres resolves the manifest name in the .rc file itself, so give it an absolute path
# with forward slashes. The script is shared by every target; each compiles it into its own
# resource object.
if (WIN32 AND MINGW)
    file(TO_CMAKE_PATH "${JONES3D_WINDOWS_MANIFEST}" jones3d_windows_manifest_rc_path)
    set(JONES3D_WINDOWS_MANIFEST_RC "${CMAKE_BINARY_DIR}/jones3d-utf8-manifest.rc")
    file(WRITE "${JONES3D_WINDOWS_MANIFEST_RC}"
        "1 24 \"${jones3d_windows_manifest_rc_path}\"\n")
endif()

function(j3d_add_windows_manifest target)
    if (NOT WIN32)
        return()
    endif()
    if (NOT TARGET ${target})
        message(FATAL_ERROR "j3d_add_windows_manifest(${target}): no such target")
    endif()

    if (MSVC)
        target_sources(${target} PRIVATE "${JONES3D_WINDOWS_MANIFEST}")
    elseif (MINGW)
        target_sources(${target} PRIVATE "${JONES3D_WINDOWS_MANIFEST_RC}")
    else()
        message(WARNING "j3d_add_windows_manifest(${target}): "
            "no manifest support for ${CMAKE_CXX_COMPILER_ID}")
    endif()
endfunction()
