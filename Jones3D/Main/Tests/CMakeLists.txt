set(JONES3D_TEST_SOURCES
    jones3dTestRunner.c
    JonesMainTest.c
    JonesCaptureTest.c
)

# Added: the command-line parser is pure C, so its tests also run on the non-Windows columns without
# the Windows platform or the rest of the game.
if (J3D_PLATFORM_NULL)
    add_executable(jonesCaptureTests
        JonesCaptureTestRunner.c
        JonesCaptureTest.c
        "${CMAKE_CURRENT_SOURCE_DIR}/../JonesCapture.c"
    )

    target_include_directories(jonesCaptureTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../..")

    get_property(JONES3D_CAPTURE_TEST_LIBS TARGET Libs PROPERTY INTERFACE_LINK_LIBRARIES)
    if (APPLE)
        target_link_libraries(jonesCaptureTests PRIVATE
            ${JONES3D_CAPTURE_TEST_LIBS}
            unity
        )
    else()
        target_link_libraries(jonesCaptureTests PRIVATE
            -Wl,--start-group
            ${JONES3D_CAPTURE_TEST_LIBS}
            unity
            -Wl,--end-group
            m
        )
    endif()

    set_target_properties(jonesCaptureTests PROPERTIES
        FOLDER "Tests/Jones3D"
    )

    source_group("Tests" FILES
        JonesCaptureTestRunner.c
        JonesCaptureTest.c
    )

    add_test(NAME Jones3D.Capture COMMAND jonesCaptureTests)
    set_tests_properties(Jones3D.Capture PROPERTIES
        LABELS "unit;Jones3D"
        TIMEOUT 60
    )
    return()
endif()

# The test target links the game sources directly, mirroring how the standalone Jones3D exe is built, because
# JonesMain.c depends on the rest of the game layer. The tests only call the pure lookup function
# JonesMain_FindClosestVideoMode, so the fixed-address retail globals are never touched.
get_filename_component(JONES3D_TEST_GAME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
file(GLOB_RECURSE JONES3D_TEST_GAME_SOURCES "${JONES3D_TEST_GAME_ROOT}/*.c")
list(FILTER JONES3D_TEST_GAME_SOURCES EXCLUDE REGEX ".*/Tests/.*")
list(FILTER JONES3D_TEST_GAME_SOURCES EXCLUDE REGEX ".*/WinMain\\.c$")
# Added: Windows test builds never compile the Null platform backend sources.
list(FILTER JONES3D_TEST_GAME_SOURCES EXCLUDE REGEX ".*/Platform/Null/.*")
# Added: Windows test builds never compile the Babylon column's platform sources either.
list(FILTER JONES3D_TEST_GAME_SOURCES EXCLUDE REGEX ".*/Platform/Babylon/.*")

add_executable(jones3dTests
    ${JONES3D_TEST_SOURCES}
    ${JONES3D_TEST_GAME_SOURCES}
)

target_include_directories(jones3dTests PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../../..")

target_link_libraries(jones3dTests PRIVATE
    Libs
    unity
    comctl32
)

set_target_properties(jones3dTests PROPERTIES
    FOLDER "Tests/Jones3D"
)

source_group("Tests" FILES ${JONES3D_TEST_SOURCES})
source_group("Tested Sources" FILES ${JONES3D_TEST_GAME_SOURCES})

set(JONES3D_TEST_BACKEND_LABEL "")
if (J3D_DIRECTX6)
    set(JONES3D_TEST_BACKEND_LABEL "DirectX6")
elseif (J3D_DIRECTX9)
    set(JONES3D_TEST_BACKEND_LABEL "DirectX9")
endif()

add_test(NAME Jones3D.Main COMMAND jones3dTests)
set_tests_properties(Jones3D.Main PROPERTIES
    LABELS "unit;Jones3D;${JONES3D_TEST_BACKEND_LABEL}"
    TIMEOUT 60
)
