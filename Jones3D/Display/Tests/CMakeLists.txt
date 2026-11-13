# Added: the HUD regressions compile the real JonesHud.c, Sound.c and sithSoundMixer.c with
# synthetic driver buffers and a test clock, then link the engine libraries for everything else.
add_executable(jonesHudTests JonesHudTest.c)
target_include_directories(jonesHudTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
target_compile_definitions(jonesHudTests PRIVATE J3D_TEST)

get_property(JONES3D_HUD_TEST_LIBS TARGET Libs PROPERTY INTERFACE_LINK_LIBRARIES)
# Same link grouping as jonesCaptureTests: macOS and MSVC rescan static libraries themselves.
if (APPLE OR MSVC)
    target_link_libraries(jonesHudTests PRIVATE
        ${JONES3D_HUD_TEST_LIBS}
        unity
    )
else()
    target_link_libraries(jonesHudTests PRIVATE
        -Wl,--start-group
        ${JONES3D_HUD_TEST_LIBS}
        unity
        -Wl,--end-group
        m
    )
endif()
if (J3D_PLATFORM_BABYLON)
    set_target_properties(jonesHudTests PROPERTIES LINKER_LANGUAGE CXX)
endif()

set_target_properties(jonesHudTests PROPERTIES
    FOLDER "Tests/Jones3D"
)

source_group("Tests" FILES
    JonesHudTest.c
)

add_test(NAME Jones3D.Hud COMMAND jonesHudTests)
set_tests_properties(Jones3D.Hud PROPERTIES
    LABELS "unit;Jones3D"
    TIMEOUT 60
)
