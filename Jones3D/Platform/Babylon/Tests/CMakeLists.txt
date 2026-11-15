# Added: the GPU and logic tests of the game's menus (ADR-0026, ADR-0035), beside the
# std/render-on-Babylon tests. They compile the menu assets, skin, context and screens, link the
# Babylon column's std stack and the SDL GPU device, and run only with the Babylon column.
set(JONES3D_UI_TEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../..")

add_executable(Jones3DUiPainterTests
    JonesUiRenderTest.cpp
    JonesUiModalTest.cpp
    JonesUiPromptsTest.cpp
    JonesUiSaveLoadTest.cpp
    JonesUiMainMenuTest.cpp
    JonesUiEndScreensTest.cpp
    JonesUiSettingsTest.cpp
    JonesUiPauseMenuTest.cpp
    JonesUiStoreTest.cpp
    JonesUiControlsTest.cpp
    JonesUiActionIdsTest.cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/../JonesUiAssets.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../JonesUiSkin.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../JonesUiPrompts.cpp"
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
        babylon::uirender
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
        babylon::uirender
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

# Added: the menu asset cases read the committed Data/ui files through the file system's base path,
# so the test executable needs Data/ beside it just like the game (ADR-0031).
add_custom_command(TARGET Jones3DUiPainterTests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${JONES3D_UI_TEST_ROOT}/Data" "$<TARGET_FILE_DIR:Jones3DUiPainterTests>/Data"
    COMMENT "Copying Data/ next to Jones3DUiPainterTests")

# Added: the setup wizard's model and screen builders (M8 wizard, W4a). The model tests use
# NullPlatform, NullProcess and temporary folders holding fake game data; the screen test lays
# every screen out with a NullPainter, so neither needs a GPU or game assets.
add_executable(Jones3DSetupWizardTests
    JonesSetupWizardTest.cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/../JonesSetupWizard.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../JonesSetupWindow.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../JonesSetupHost.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../JonesGogInstaller.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../JonesUiSkin.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../Main/JonesInstall.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../Main/JonesData.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../Main/JonesDataLocate.c"
)

target_compile_definitions(Jones3DSetupWizardTests PRIVATE J3D_TEST)

target_include_directories(Jones3DSetupWizardTests PRIVATE
    "${JONES3D_UI_TEST_ROOT}"
)

get_property(JONES3D_SETUP_TEST_LIBS TARGET Libs PROPERTY INTERFACE_LINK_LIBRARIES)

if (APPLE OR MSVC)
    target_link_libraries(Jones3DSetupWizardTests PRIVATE
        ${JONES3D_SETUP_TEST_LIBS}
        babylon::ui
        babylon::uirender
        babylon::process
        babylon::render
        babylon::font
        babylon::image
        babylon_doctest
        $<IF:$<TARGET_EXISTS:SDL3::SDL3-static>,SDL3::SDL3-static,SDL3::SDL3>
    )
else()
    target_link_libraries(Jones3DSetupWizardTests PRIVATE
        -Wl,--start-group
        ${JONES3D_SETUP_TEST_LIBS}
        -Wl,--end-group
        babylon::ui
        babylon::uirender
        babylon::process
        babylon::render
        babylon::font
        babylon::image
        babylon_doctest
        $<IF:$<TARGET_EXISTS:SDL3::SDL3-static>,SDL3::SDL3-static,SDL3::SDL3>
        m
    )
endif()

set_target_properties(Jones3DSetupWizardTests PROPERTIES
    FOLDER "Tests/Jones3D"
    LINKER_LANGUAGE CXX
)

add_test(NAME Jones3D.SetupWizard COMMAND Jones3DSetupWizardTests)
set_tests_properties(Jones3D.SetupWizard PROPERTIES
    LABELS "unit;Jones3D"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    TIMEOUT 120
)

# The GOG installer driver is driven by babylon::process::NullProcess, so its test needs no
# installer, innoextract program or window.
add_executable(Jones3DGogInstallerTests
    JonesGogInstallerTest.cpp
    "${CMAKE_CURRENT_SOURCE_DIR}/../JonesGogInstaller.cpp"
)

target_include_directories(Jones3DGogInstallerTests PRIVATE
    "${JONES3D_UI_TEST_ROOT}"
)

target_link_libraries(Jones3DGogInstallerTests PRIVATE
    babylon::process
    babylon_doctest
)

set_target_properties(Jones3DGogInstallerTests PROPERTIES
    FOLDER "Tests/Jones3D"
    LINKER_LANGUAGE CXX
)

add_test(NAME Jones3D.GogInstaller COMMAND Jones3DGogInstallerTests)
set_tests_properties(Jones3D.GogInstaller PROPERTIES
    LABELS "unit;Jones3D"
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    TIMEOUT 120
)
