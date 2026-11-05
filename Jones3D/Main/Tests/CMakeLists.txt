# Added: the command-line parser is pure C, so its tests run without the rest of the game.
# Altered: the Windows jones3dTests (JonesMainTest.c, jones3dTestRunner.c), which linked the whole
# game layer of the DirectX 9 Jones3D.exe, went with the Win32/DirectX game (ADR-0011).
add_executable(jonesCaptureTests
    JonesCaptureTestRunner.c
    JonesCaptureTest.c
    "${CMAKE_CURRENT_SOURCE_DIR}/../JonesCapture.c"
)

target_include_directories(jonesCaptureTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../..")

get_property(JONES3D_CAPTURE_TEST_LIBS TARGET Libs PROPERTY INTERFACE_LINK_LIBRARIES)
# Added: MSVC's link.exe has no group options and no m.lib; like ld64 it rescans the static
# libraries itself, so the Babylon column under MSVC links as on macOS.
if (APPLE OR MSVC)
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
