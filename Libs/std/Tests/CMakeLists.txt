set(STD_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(STD_GENERAL_SOURCE_DIR "${STD_SOURCE_DIR}/General")
set(STD_GENERAL_TEST_DIR "${STD_GENERAL_SOURCE_DIR}/Tests")

set(STD_TEST_SUPPORT_SOURCES
    stdTestRunner.c
    stdGeneralTest.c
)

set(STD_GENERAL_TEST_SOURCES
    "${STD_GENERAL_TEST_DIR}/stdTest.c"
    "${STD_GENERAL_TEST_DIR}/stdMemoryTest.c"
    "${STD_GENERAL_TEST_DIR}/stdMathTest.c"
    "${STD_GENERAL_TEST_DIR}/stdUtilTest.c"
    "${STD_GENERAL_TEST_DIR}/stdHashtblTest.c"
    "${STD_GENERAL_TEST_DIR}/stdFnamesTest.c"
    "${STD_GENERAL_TEST_DIR}/stdLinkListTest.c"
    "${STD_GENERAL_TEST_DIR}/stdEffectTest.c"
    "${STD_GENERAL_TEST_DIR}/stdColorTest.c"
    "${STD_GENERAL_TEST_DIR}/stdCircBufTest.c"
    "${STD_GENERAL_TEST_DIR}/stdConffileTest.c"
    "${STD_GENERAL_TEST_DIR}/stdConfigTest.c"
    "${STD_GENERAL_TEST_DIR}/stdRenderCaptureTest.c"
    "${STD_GENERAL_TEST_DIR}/stdFileUtilTest.c"
    "${STD_GENERAL_TEST_DIR}/stdJSONTest.c"
    "${STD_GENERAL_TEST_DIR}/stdStrTableTest.c"
    # Added: the suites of surviving code that only the Win32/DirectX build ran (M6.3f): stdGob.c and
    # stdPlatform.c, and the header contracts of std3D, stdDisplay, stdControl, stdComm and stdWin95,
    # restored from std/Win95/Tests against the Null backends below.
    "${STD_GENERAL_TEST_DIR}/stdGobTest.c"
    "${STD_GENERAL_TEST_DIR}/stdPlatformTest.c"
    "${STD_GENERAL_TEST_DIR}/std3DTest.c"
    "${STD_GENERAL_TEST_DIR}/stdDisplayTest.c"
    "${STD_GENERAL_TEST_DIR}/stdControlTest.c"
    "${STD_GENERAL_TEST_DIR}/stdCommTest.c"
    "${STD_GENERAL_TEST_DIR}/stdWin95Test.c"
)

set(STD_GENERAL_TESTED_SOURCES
    "${STD_GENERAL_SOURCE_DIR}/std.c"
    "${STD_GENERAL_SOURCE_DIR}/stdFile.c"
    "${STD_GENERAL_SOURCE_DIR}/stdBmp.c"
    "${STD_GENERAL_SOURCE_DIR}/stdConffile.c"
    "${STD_GENERAL_SOURCE_DIR}/stdConfig.c"
    "${STD_GENERAL_SOURCE_DIR}/stdFileUtil.c"
    "${STD_GENERAL_SOURCE_DIR}/stdJSON.c"
    "${STD_GENERAL_SOURCE_DIR}/stdMath.c"
    "${STD_GENERAL_SOURCE_DIR}/stdMemory.c"
    "${STD_GENERAL_SOURCE_DIR}/stdPlatform.c"
    "${STD_GENERAL_SOURCE_DIR}/stdStrTable.c"
    "${STD_GENERAL_SOURCE_DIR}/stdUtil.c"
    "${STD_GENERAL_SOURCE_DIR}/stdHashtbl.c"
    "${STD_GENERAL_SOURCE_DIR}/stdFnames.c"
    "${STD_GENERAL_SOURCE_DIR}/stdGob.c"
    "${STD_GENERAL_SOURCE_DIR}/stdLinkList.c"
    "${STD_GENERAL_SOURCE_DIR}/stdEffect.c"
    "${STD_GENERAL_SOURCE_DIR}/stdColor.c"
    "${STD_GENERAL_SOURCE_DIR}/stdCircBuf.c"
    "${STD_GENERAL_SOURCE_DIR}/stdRenderCapture.c"
)

# Added: The Null platform runs only the portable std/General suites. stdConsoleNull.c is not listed:
# stdGeneralTest.c provides the stdConsole_WriteConsole stub the way it does on Windows.
# stdCvarNull.c is the Null column's simulation cvar seam, covered by stdCvarNullTest.c below; the
# Babylon column covers its replacement, stdCvarBabylon.cpp, in stdBabylonTests.
set(STD_NULL_TESTED_SOURCES
    "${STD_SOURCE_DIR}/Null/stdFileUtilPosix.c"
    "${STD_SOURCE_DIR}/Null/stdPlatformNull.c"
    "${STD_SOURCE_DIR}/Null/stdPlatformTimeNull.c"
    "${STD_SOURCE_DIR}/Null/stdCvarNull.c"
    # Added: the renderer forwarding layer and the Null backends it forwards to, tested by
    # stdRenderCaptureForwardTest.c (the std tests only link the files they compile here).
    "${STD_SOURCE_DIR}/Null/std3DNull.c"
    "${STD_SOURCE_DIR}/Null/stdDisplayNull.c"
    "${STD_GENERAL_SOURCE_DIR}/std3DCapture.c"
    "${STD_GENERAL_SOURCE_DIR}/stdDisplayCapture.c"
    # Added: the replay library calls the public renderer functions above.
    "${STD_GENERAL_SOURCE_DIR}/stdRenderReplay.c"
    # Added: the Null backends of stdControl (with the Null column's scripted device side), stdComm and
    # stdWin95, tested by stdControlTest.c, stdCommTest.c and stdWin95Test.c. The Babylon column links
    # the same stdControlNull.c in the game, with its own device side.
    "${STD_SOURCE_DIR}/Null/stdControlNull.c"
    "${STD_SOURCE_DIR}/Null/stdControlDevicesNull.c"
    "${STD_SOURCE_DIR}/Null/stdCommNull.c"
    "${STD_SOURCE_DIR}/Null/stdWin95Null.c"
)

# Altered: every std/General test is listed above except stdBmpTest.c: stdBmp_WriteVBuffer and
# stdBmp_Load lived in the Win32 backend (std/Win95/stdBmpWin32.c) and went with it (ADR-0011), so it
# tests nothing that is still built. stdPlatformTest.c lost its Windows-only cases (stack frame
# probes, minidumps, the CreateProcessA death child) and covers the shared stdPlatform.c and the Null
# platform layer. stdCvarNullTest.c covers the Null column's simulation cvar seam.
list(APPEND STD_GENERAL_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/stdCvarNullTest.c"
    # Added: the renderer forwarding layer records through the Null backend.
    "${CMAKE_CURRENT_SOURCE_DIR}/stdRenderCaptureForwardTest.c"
    # Added: the replay reads those captures back through the same Null forwarders.
    "${CMAKE_CURRENT_SOURCE_DIR}/stdRenderReplayTest.c"
)
set(STD_TESTED_SOURCES ${STD_GENERAL_TESTED_SOURCES} ${STD_NULL_TESTED_SOURCES})

# Added: the Babylon column on Windows skips stdTests: it is the Null column's suite over the POSIX file
# utilities (Null/stdFileUtilPosix.c: dirent, fnmatch), which Windows does not build. The same
# std/General suites run there through the Babylon files adapter as std.OnBabylon below.
if (NOT WIN32)
add_executable(stdTests
    ${STD_TEST_SUPPORT_SOURCES}
    ${STD_GENERAL_TEST_SOURCES}
    ${STD_TESTED_SOURCES}
)

target_compile_definitions(stdTests PRIVATE
    J3D_TEST
    STD_GENERAL_TEST_TV_DIR="${STD_GENERAL_TEST_DIR}/tv"
)

target_include_directories(stdTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Added: The Null platform compiles its own stdPlatformNull.c/stdFileUtilPosix.c, whose
# stdBackendTypes.h lives here.
target_include_directories(stdTests PRIVATE
    "${STD_SOURCE_DIR}/Null"
)

target_link_libraries(stdTests PRIVATE
    j3dcore
    j3dTestSupport
    jansson::jansson
)

set_target_properties(stdTests PROPERTIES
    FOLDER "Tests/Libs/std"
)

source_group("Test Support" FILES ${STD_TEST_SUPPORT_SOURCES})
source_group("Tests" FILES ${STD_GENERAL_TEST_SOURCES})
source_group("Tested Sources" FILES ${STD_TESTED_SOURCES})

add_test(NAME std COMMAND stdTests)
set_tests_properties(std PROPERTIES
    LABELS "unit;std"
)
endif()

# Added: the same suites once more through the Babylon files adapter: the Null platform's file
# sources General/stdFile.c and Null/stdFileUtilPosix.c are replaced by the adapter files and the
# adapter context, and a C++ entry installs the context and calls stdTestRunner.c's renamed entry.
# Only the Babylon column builds it.
if (J3D_PLATFORM_BABYLON)
    set(STD_BABYLON_SOURCE_DIR "${STD_SOURCE_DIR}/Babylon")

    set(STD_BABYLON_TESTED_SOURCES ${STD_TESTED_SOURCES})
    list(REMOVE_ITEM STD_BABYLON_TESTED_SOURCES
        "${STD_GENERAL_SOURCE_DIR}/stdFile.c"
        "${STD_SOURCE_DIR}/Null/stdFileUtilPosix.c"
    )
    list(APPEND STD_BABYLON_TESTED_SOURCES
        "${STD_BABYLON_SOURCE_DIR}/stdBabylon.cpp"
        "${STD_BABYLON_SOURCE_DIR}/stdBabylonCvars.cpp"
        "${STD_BABYLON_SOURCE_DIR}/stdBabylonPath.cpp"
        "${STD_BABYLON_SOURCE_DIR}/stdFileBabylon.cpp"
        "${STD_BABYLON_SOURCE_DIR}/stdFileUtilBabylon.cpp"
    )
    # Added: on Windows the Null time file (clock_gettime, nanosleep) does not build; the suites run
    # on the Babylon clock adapter there, which the game's Babylon column links everywhere.
    if (WIN32)
        list(REMOVE_ITEM STD_BABYLON_TESTED_SOURCES "${STD_SOURCE_DIR}/Null/stdPlatformTimeNull.c")
        list(APPEND STD_BABYLON_TESTED_SOURCES "${STD_BABYLON_SOURCE_DIR}/stdPlatformTimeBabylon.cpp")
    endif()

    add_executable(stdTestsBabylon
        stdTestRunner.c
        stdGeneralTest.c
        ${STD_GENERAL_TEST_SOURCES}
        ${STD_BABYLON_TESTED_SOURCES}
        "${STD_BABYLON_SOURCE_DIR}/Tests/stdBabylonTestMain.cpp"
    )

    # Added: stdTestRunner.c is shared with stdTests, and the source-level entry rename in
    # std_use_babylon_test_main would rename that executable's main too; this target therefore
    # renames the entry with a target definition. Everything else matches what the function does.
    target_compile_definitions(stdTestsBabylon PRIVATE
        J3D_TEST
        J3D_TEST_RUNNER_ENTRY=j3dTestRunner_Main
        STD_GENERAL_TEST_TV_DIR="${STD_GENERAL_TEST_DIR}/tv"
    )

    # The test sources include <std/...> (via Libs through j3dcore), the Null backend types and
    # the adapter headers next to the tested sources.
    target_include_directories(stdTestsBabylon PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${STD_SOURCE_DIR}/Null"
    )

    target_link_libraries(stdTestsBabylon PRIVATE
        j3dcore
        j3dTestSupport
        jansson::jansson
        babylon::time
        babylon::files
        babylon::platform
        babylon::cvars
    )

    set_target_properties(stdTestsBabylon PROPERTIES
        FOLDER "Tests/Libs/std"
        LINKER_LANGUAGE CXX
    )

    source_group("Tested Sources" FILES ${STD_BABYLON_TESTED_SOURCES})

    # Run in a private working directory: the suites create their temporary files and directories
    # relative to it, and the std suite does the same next door.
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/OnBabylon")
    add_test(NAME std.OnBabylon COMMAND stdTestsBabylon)
    set_tests_properties(std.OnBabylon PROPERTIES
        LABELS "std;unit"
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/OnBabylon"
    )
endif()
