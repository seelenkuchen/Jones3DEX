# Added: the command-line parser is pure C, so its tests run without the rest of the game.
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

# Added: the video mode lookup JonesMain_FindClosestVideoMode (JonesMainTest.c). JonesMain.c needs
# the rest of the application layer, so the test links the Jones3DGame objects and the platform
# helpers of Platform/Null without its main(), like the Babylon column's Jones3D-headless. The
# tests only call the pure lookup function, so no platform is started. It ran in the Windows
# jones3dTests of the DirectX 9 Jones3D.exe until that went (ADR-0011); the capture cases that
# executable also ran are Jones3D.Capture above.
file(GLOB JONES3D_MAIN_TEST_PLATFORM_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../../Platform/Null/*.c")
list(FILTER JONES3D_MAIN_TEST_PLATFORM_SOURCES EXCLUDE REGEX "/Platform/Null/main\\.c$")

if (J3D_PLATFORM_BABYLON)
    # Added: the Babylon column's control scheme logic and its tests (M7.17a). The test supplies
    # the windowed/headless test double, so the real Platform/Babylon/JonesBabylonWindow.cpp is
    # not compiled here. The Babylon module replaces the split Null jonesConfig startup file.
    list(FILTER JONES3D_MAIN_TEST_PLATFORM_SOURCES EXCLUDE REGEX "/Platform/Null/JonesPlatformNullConfig\\.c$")
    list(APPEND JONES3D_MAIN_TEST_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/../../Platform/Babylon/JonesControlSchemes.c"
        "JonesControlSchemesTest.cpp"
    )
endif()

add_executable(jones3dTests
    jones3dTestRunner.c
    JonesMainTest.c
    ${JONES3D_MAIN_TEST_PLATFORM_SOURCES}
    ${JONES3D_MAIN_TEST_SOURCES}
)

if (J3D_PLATFORM_BABYLON)
    target_compile_definitions(jones3dTests PRIVATE
        JONES3D_CONTROL_SCHEMES_TEST
        # Added: keep Unity's malloc/free macros out of C++: inline STL code compiled here would
        # share one COMDAT with production and free CRT memory through unity_free, corrupting the
        # heap in Release.
        $<$<COMPILE_LANGUAGE:CXX>:UNITY_FIXTURE_NO_EXTRAS>
    )
endif()

target_include_directories(jones3dTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../..")

# Added: no group options on macOS and MSVC, as for jonesCaptureTests above.
if (APPLE OR MSVC)
    target_link_libraries(jones3dTests PRIVATE
        Jones3DGame
        ${JONES3D_CAPTURE_TEST_LIBS}
        unity
    )
else()
    target_link_libraries(jones3dTests PRIVATE
        -Wl,--start-group
        Jones3DGame
        ${JONES3D_CAPTURE_TEST_LIBS}
        unity
        -Wl,--end-group
    )
endif()
if (NOT APPLE AND NOT WIN32)
    target_link_libraries(jones3dTests PRIVATE m)
endif()
if (J3D_PLATFORM_BABYLON)
    set_target_properties(jones3dTests PROPERTIES LINKER_LANGUAGE CXX)
endif()

set_target_properties(jones3dTests PROPERTIES
    FOLDER "Tests/Jones3D"
)

source_group("Tests" FILES
    jones3dTestRunner.c
    JonesMainTest.c
)

add_test(NAME Jones3D.Main COMMAND jones3dTests)
set_tests_properties(Jones3D.Main PROPERTIES
    LABELS "unit;Jones3D"
    TIMEOUT 60
)
