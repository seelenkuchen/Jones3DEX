# Added: the GPU test of the game's babylon::ui painter (ADR-0026 amendment), beside the
# std/render-on-Babylon tests. It compiles the painter and the first skin itself, links the Babylon
# column's std/rdroid stack and the SDL GPU device, and runs only with the Babylon column.
set(JONES3D_UI_TEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../..")

add_executable(Jones3DUiPainterTests
    JonesUiPainterTest.cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/../JonesUiPainter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../JonesUiSkin.cpp"
)

target_include_directories(Jones3DUiPainterTests PRIVATE
    "${JONES3D_UI_TEST_ROOT}"
)

get_property(JONES3D_UI_TEST_LIBS TARGET Libs PROPERTY INTERFACE_LINK_LIBRARIES)

if (APPLE OR MSVC)
    target_link_libraries(Jones3DUiPainterTests PRIVATE
        ${JONES3D_UI_TEST_LIBS}
        babylon::ui
        babylon::render
        babylon_doctest
        # Added: SDL_getenv reads BABYLON_REQUIRE_GPU. Babylon links SDL privately, so its headers
        # come from the SDL target itself (static when Babylon fetched SDL).
        $<IF:$<TARGET_EXISTS:SDL3::SDL3-static>,SDL3::SDL3-static,SDL3::SDL3>
    )
else()
    target_link_libraries(Jones3DUiPainterTests PRIVATE
        -Wl,--start-group
        ${JONES3D_UI_TEST_LIBS}
        -Wl,--end-group
        babylon::ui
        babylon::render
        babylon_doctest
        $<IF:$<TARGET_EXISTS:SDL3::SDL3-static>,SDL3::SDL3-static,SDL3::SDL3>
        m
    )
endif()

set_target_properties(Jones3DUiPainterTests PROPERTIES
    FOLDER "Tests/Jones3D"
    LINKER_LANGUAGE CXX
)

# The SDL GPU cases need SDL's video subsystem; the offscreen driver needs no display. Without a GPU
# the case prints a skip and passes, unless BABYLON_REQUIRE_GPU=1.
add_test(NAME Jones3D.UiPainter COMMAND Jones3DUiPainterTests)
set_tests_properties(Jones3D.UiPainter PROPERTIES
    LABELS "unit;Jones3D"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    ENVIRONMENT "SDL_VIDEO_DRIVER=$<IF:$<PLATFORM_ID:Darwin>,dummy,offscreen>"
    TIMEOUT 120
)
