# Added: the GPU test of the game's babylon::ui painter (ADR-0026 amendment), beside the
# std/render-on-Babylon tests. It compiles the painter and the first skin itself, links the Babylon
# column's std/rdroid stack and the SDL GPU device, and runs only with the Babylon column.
set(JONES3D_UI_TEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../..")

add_executable(Jones3DUiPainterTests
    JonesUiPainterTest.cpp
    JonesUiModalTest.cpp
    JonesUiSaveLoadTest.cpp
    JonesUiMainMenuTest.cpp
    JonesUiEndScreensTest.cpp
    JonesUiSettingsTest.cpp
    JonesUiPauseMenuTest.cpp
    JonesUiStoreTest.cpp
    JonesUiControlsTest.cpp
    JonesUiActionIdsTest.cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/../JonesUiPainter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../JonesUiSkin.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../JonesUiModal.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../JonesUiModalMessage.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../JonesWindowMode.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../JonesUiContext.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../JonesUiSaveLoad.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../JonesUiMainMenu.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../JonesUiEndScreens.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../JonesUiDisplaySettings.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../JonesUiSoundSettings.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../JonesUiPauseMenu.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../JonesUiGameplaySettings.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../JonesUiStore.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../JonesUiControls.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../JonesUiOptions.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../JonesBabylonMainSys.cpp"
    # Added: the control-scheme module the Controls screen reads and writes (M7.17a). It is part of
    # the Babylon column, not of Jones3DGame, so the test compiles it itself.
    "${CMAKE_CURRENT_SOURCE_DIR}/../JonesControlSchemes.c"
    # Added: the two application-layer modules the Gameplay screen calls. The rest of the game
    # layer is not linked, so the test needs only what these two compile units use.
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../Display/JonesReticle.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../Play/jonesInventory.c"
    # Added: the sound layer compiled with J3D_TEST, so the Game Over case can install a synthetic
    # sound bank without a game asset. The library's copy is not pulled because this object
    # defines every Sound function.
    "${JONES3D_UI_TEST_ROOT}/Libs/sound/Sound.c"
)

target_compile_definitions(Jones3DUiPainterTests PRIVATE J3D_TEST)

target_include_directories(Jones3DUiPainterTests PRIVATE
    "${JONES3D_UI_TEST_ROOT}"
)

get_property(JONES3D_UI_TEST_LIBS TARGET Libs PROPERTY INTERFACE_LINK_LIBRARIES)

if (APPLE OR MSVC)
    target_link_libraries(Jones3DUiPainterTests PRIVATE
        ${JONES3D_UI_TEST_LIBS}
        babylon::ui
        babylon::render
        babylon::font
        babylon::image
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
        babylon::font
        babylon::image
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

# Added: the painter's TrueType and PNG cases read the committed assets through the file system's
# base path, so the test executable needs Data/ beside it just like the game (ADR-0031).
add_custom_command(TARGET Jones3DUiPainterTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${JONES3D_UI_TEST_ROOT}/Data" "$<TARGET_FILE_DIR:Jones3DUiPainterTests>/Data"
    COMMENT "Copying Data/ next to Jones3DUiPainterTests")
