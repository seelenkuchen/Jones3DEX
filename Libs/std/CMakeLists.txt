project(std C CXX)

file(GLOB_RECURSE SOURCES *.c)
file(GLOB_RECURSE HEADERS *.h)

list(FILTER SOURCES EXCLUDE REGEX ".*/Tests/.*")
list(FILTER HEADERS EXCLUDE REGEX ".*/Tests/.*")

# Added: the replay tool's main() lives under Tools/ and is built as its own executable below, not
# as part of the std library.
list(FILTER SOURCES EXCLUDE REGEX ".*/Tools/.*")
list(FILTER HEADERS EXCLUDE REGEX ".*/Tools/.*")

# Altered: the Win95 backend tree (DX6/DX9 device code, Windows console/comm services and the
# Win32-specific files) went with the Win32/DirectX game (ADR-0011); the Null backend is the only
# one left. stdGob.c is built here: its memory-mapped-file branch, which is dead because the
# streamed path is always chosen, is the only part guarded out for non-Windows.

# Added: the Babylon column is the Null column with the stdPlatform time functions, the host file
# services and the simulation cvar seam moved onto Babylon. Drop the Null time file, the stdFile CRT
# wrappers, the Posix file utilities, the Null device read of stdControl and the fixed simulation
# cvar table, and compile the adapters in their place; the .cpp files are never globbed (the GLOB
# above is .c only), so they are added explicitly.
if (J3D_PLATFORM_BABYLON)
    list(FILTER SOURCES EXCLUDE REGEX "stdPlatformTimeNull\\.c$")
    list(FILTER SOURCES EXCLUDE REGEX "/General/stdFile\\.c$")
    list(FILTER SOURCES EXCLUDE REGEX "/Null/stdFileUtilPosix\\.c$")
    list(FILTER SOURCES EXCLUDE REGEX "/Null/stdControlDevicesNull\\.c$")
    list(FILTER SOURCES EXCLUDE REGEX "/Null/stdCvarNull\\.c$")
    list(APPEND SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/Babylon/stdBabylon.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Babylon/stdBabylonCvars.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Babylon/stdBabylonEvents.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Babylon/stdPlatformTimeBabylon.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Babylon/stdBabylonPath.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Babylon/stdFileBabylon.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Babylon/stdFileUtilBabylon.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Babylon/stdControlDevicesBabylon.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Babylon/stdCvarBabylon.cpp"
    )
endif()

# Make project target
add_library(${PROJECT_NAME} STATIC ${SOURCES} ${HEADERS})

# Add global includes
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Added: The Null platform provides its own replacement backend types header here.
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Null)

# Link libraries
target_link_libraries(${PROJECT_NAME} PUBLIC
    j3dcore
)

# Added: the Babylon column's adapters run on the Babylon clock, file system, platform event queue,
# input state and cvar registry.
if (J3D_PLATFORM_BABYLON)
    target_link_libraries(${PROJECT_NAME} PUBLIC
        babylon::time
        babylon::files
        babylon::platform
        babylon::input
        babylon::cvars
    )
endif()

# Added: the Null platform links no DirectX backend, but stdJSON.c still needs jansson.
target_link_libraries(${PROJECT_NAME} PUBLIC
    jansson::jansson
)

# Added: the render capture replay tool. The plain Null column needs no Babylon host context, so the
# tool is built there only; the Babylon column still builds the replay library and its tests.
if (NOT J3D_PLATFORM_BABYLON)
    add_executable(Jones3D-replay Tools/stdRenderReplayMain.c)
    target_link_libraries(Jones3D-replay PRIVATE
        ${PROJECT_NAME}
        j3dcore
    )

    if (UNIX)
        target_link_libraries(Jones3D-replay PRIVATE m)
    endif()

    set_target_properties(Jones3D-replay PROPERTIES
        FOLDER "Tools"
    )
endif()

# Organize source files in Visual Studio
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES} ${HEADERS})

if (JONES3D_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

# Added: the Babylon column's adapter test suite lives next to the adapter it covers.
if (JONES3D_BUILD_TESTS AND J3D_PLATFORM_BABYLON)
    add_subdirectory(Babylon/Tests)
endif()
