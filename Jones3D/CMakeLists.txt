cmake_minimum_required(VERSION 3.15)
project(Jones3D)

# Added: The Null platform compiles the application layer into an object library so native builds
# validate it. The Platform/Null headless runner and the tests are excluded from the object library;
# the headless runner links it in below.
# Altered: the Windows-only Platform/Win32 files and the DirectX 9 Jones3D.exe went with the
# Win32/DirectX game (ADR-0011).
file(GLOB_RECURSE JONES3D_GAME_SOURCES CONFIGURE_DEPENDS *.c)
file(GLOB_RECURSE JONES3D_GAME_HEADERS CONFIGURE_DEPENDS *.h)
list(FILTER JONES3D_GAME_SOURCES EXCLUDE REGEX ".*/Tests/.*")
list(FILTER JONES3D_GAME_HEADERS EXCLUDE REGEX ".*/Tests/.*")
list(FILTER JONES3D_GAME_SOURCES EXCLUDE REGEX ".*/Platform/Null/.*")
list(FILTER JONES3D_GAME_HEADERS EXCLUDE REGEX ".*/Platform/Null/.*")
# Added: the Babylon column's platform directory (JonesBabylon.c) is linked into the
# executables that own it, never globbed into the shared application layer.
list(FILTER JONES3D_GAME_SOURCES EXCLUDE REGEX ".*/Platform/Babylon/.*")
list(FILTER JONES3D_GAME_HEADERS EXCLUDE REGEX ".*/Platform/Babylon/.*")

add_library(Jones3DGame OBJECT ${JONES3D_GAME_SOURCES} ${JONES3D_GAME_HEADERS})

# Same include directory as the Jones3D targets built for Windows.
target_include_directories(Jones3DGame PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

target_link_libraries(Jones3DGame PUBLIC Libs)

# Added: The Null headless runner: the application layer object library plus the
# Platform/Null main(). Link groups/repeated libs resolve the static-library cycles.
file(GLOB JONES3D_NULL_SOURCES CONFIGURE_DEPENDS Platform/Null/*.c)

# Added: the game's babylon::ui painter and first skin (ADR-0026 amendment). They are part of the
# Babylon column only, compiled into its executables and its painter test.
set(JONES3D_BABYLON_UI_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/JonesUiPainter.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/JonesUiPainter.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/JonesUiSkin.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/JonesUiSkin.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/JonesUiPrompts.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/JonesUiPrompts.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/JonesUiModal.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/JonesUiModal.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/JonesWindowMode.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/JonesWindowMode.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/JonesUiModalMessage.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/JonesUiContext.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/JonesUiContext.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/JonesUiSaveLoad.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/JonesUiSaveLoad.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/JonesUiMainMenu.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/JonesUiMainMenu.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/JonesUiEndScreens.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/JonesUiEndScreens.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/JonesUiDisplaySettings.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/JonesUiDisplaySettings.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/JonesUiSoundSettings.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/JonesUiSoundSettings.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/JonesUiPauseMenu.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/JonesUiPauseMenu.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/JonesUiGameplaySettings.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/JonesUiGameplaySettings.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/JonesUiStore.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/JonesUiStore.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/JonesUiControls.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/JonesUiControls.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/JonesUiOptions.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/JonesUiOptions.h"
)

# Added: the Babylon column's control scheme files (M7.17a), shared by the headless and windowed
# executables and the unit tests.
set(JONES3D_CONTROL_SCHEME_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/JonesControlSchemes.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/JonesControlSchemes.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/JonesBabylonWindow.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/JonesBabylonWindow.h"
)

# Added: the Null message/insert-CD stubs, split from JonesPlatformNull.c so the Babylon column
# can provide its in-game screens for exactly those two and keep the other Null stubs.
set(JONES3D_NULL_MESSAGE_DIALOG_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Null/JonesPlatformNullMessageDialog.c"
)

# Added: the Null save/load/exit stubs, split from JonesPlatformNull.c so the Babylon column can
# provide its in-game screens for exactly those three.
set(JONES3D_NULL_SAVELOAD_DIALOG_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Null/JonesPlatformNullSaveLoad.c"
)

# Added: the Null start-mode mapping and main menu stub, split so the Babylon column can provide
# its real menu.
set(JONES3D_NULL_MAINMENU_DIALOG_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Null/JonesPlatformNullMainMenu.c"
)

# Added: the Null statistics/level-completed/game-over stubs, split from JonesPlatformNull.c so
# the Babylon column can provide its in-game screens for exactly those three.
set(JONES3D_NULL_ENDSCREENS_DIALOG_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Null/JonesPlatformNullEndScreens.c"
)

# Added: the Null Display and Sound settings stubs, split from JonesPlatformNull.c so the Babylon
# column can provide its in-game screens for exactly those two.
set(JONES3D_NULL_SETTINGS_DIALOG_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Null/JonesPlatformNullSettingsDialog.c"
)

# Added: the Null pause menu stub, split from JonesPlatformNull.c so the Babylon column can provide
# the in-game screen and the headless runner still answers at once.
set(JONES3D_NULL_PAUSEMENU_DIALOG_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Null/JonesPlatformNullPauseMenu.c"
)

# Added: the Null Gameplay settings stub, split from JonesPlatformNull.c so the Babylon column can
# provide its in-game screen.
set(JONES3D_NULL_GAMEPLAY_DIALOG_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Null/JonesPlatformNullGameplayDialog.c"
)

# Added: the Null jonesConfig startup/shutdown and active-scheme no-op, split from
# JonesPlatformNull.c so the Babylon column's control scheme module provides them.
set(JONES3D_NULL_CONFIG_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Null/JonesPlatformNullConfig.c"
)

# Added: the Null Controls dialog stub, split from JonesPlatformNull.c so the Babylon column can
# provide its in-game screen (JonesUiControls.cpp). The headless runner keeps this stub.
set(JONES3D_NULL_CONTROLOPTIONS_DIALOG_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Null/JonesPlatformNullControlOptions.c"
)

# Added: the Null PostCloseMessage stub, split from JonesPlatformNull.c so the Babylon column's
# own JonesBabylonMainSys.cpp can record the quit request that really ends its loop.
set(JONES3D_NULL_MAINSYS_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Null/JonesPlatformNullMainSys.c"
)

# Added: the Babylon column's JonesMainSys replacements, compiled into its headless and windowed
# executables and the UI tests.
set(JONES3D_BABYLON_MAINSYS_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/JonesBabylonMainSys.cpp"
)

# Added: the Babylon column swaps the Null main() for the Platform/Babylon composition root.
set(JONES3D_HEADLESS_SOURCES ${JONES3D_NULL_SOURCES})
if (J3D_PLATFORM_BABYLON)
    enable_language(CXX)
    list(REMOVE_ITEM JONES3D_HEADLESS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Null/main.c")
    # Added: the Babylon headless runner uses the Babylon message screens too; with no window
    # they answer the Null value without blocking, so the Null stub file must not be linked.
    list(REMOVE_ITEM JONES3D_HEADLESS_SOURCES "${JONES3D_NULL_MESSAGE_DIALOG_SOURCE}")
    list(REMOVE_ITEM JONES3D_HEADLESS_SOURCES "${JONES3D_NULL_SAVELOAD_DIALOG_SOURCE}")
    list(REMOVE_ITEM JONES3D_HEADLESS_SOURCES "${JONES3D_NULL_MAINMENU_DIALOG_SOURCE}")
    list(REMOVE_ITEM JONES3D_HEADLESS_SOURCES "${JONES3D_NULL_ENDSCREENS_DIALOG_SOURCE}")
    list(REMOVE_ITEM JONES3D_HEADLESS_SOURCES "${JONES3D_NULL_SETTINGS_DIALOG_SOURCE}")
    list(REMOVE_ITEM JONES3D_HEADLESS_SOURCES "${JONES3D_NULL_PAUSEMENU_DIALOG_SOURCE}")
    list(REMOVE_ITEM JONES3D_HEADLESS_SOURCES "${JONES3D_NULL_GAMEPLAY_DIALOG_SOURCE}")
    list(REMOVE_ITEM JONES3D_HEADLESS_SOURCES "${JONES3D_NULL_CONFIG_SOURCE}")
    list(REMOVE_ITEM JONES3D_HEADLESS_SOURCES "${JONES3D_NULL_CONTROLOPTIONS_DIALOG_SOURCE}")
    list(REMOVE_ITEM JONES3D_HEADLESS_SOURCES "${JONES3D_NULL_MAINSYS_SOURCE}")
    list(APPEND JONES3D_HEADLESS_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/main.cpp"
        ${JONES3D_BABYLON_UI_SOURCES}
        ${JONES3D_CONTROL_SCHEME_SOURCES}
        ${JONES3D_BABYLON_MAINSYS_SOURCE}
    )
endif()

add_executable(Jones3D-headless ${JONES3D_HEADLESS_SOURCES})
target_include_directories(Jones3D-headless PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)

if (J3D_PLATFORM_BABYLON)
    set_target_properties(Jones3D-headless PROPERTIES LINKER_LANGUAGE CXX)
endif()

# Added: The same "Libs" interface dependencies as the Windows Jones3D targets link,
# repeated inside a GNU ld start/end group so the static-library cycles resolve.
get_property(JONES3D_LIBS_INTERFACE TARGET Libs PROPERTY INTERFACE_LINK_LIBRARIES)

# Added: --start-group/--end-group are GNU ld and lld options; macOS's ld64 has no such options
# and rescans static libraries by itself, so the Apple branch links the same libraries without
# the group flags. Linux and Windows link lines are unchanged.
# Added: MSVC's link.exe has no group options either and also resolves symbols across all static
# libraries on its own, so the Babylon column under MSVC takes the same branch.
if (APPLE OR MSVC)
    target_link_libraries(Jones3D-headless PRIVATE
        Jones3DGame
        ${JONES3D_LIBS_INTERFACE}
    )
else()
    target_link_libraries(Jones3D-headless PRIVATE
        -Wl,--start-group
        Jones3DGame
        ${JONES3D_LIBS_INTERFACE}
        -Wl,--end-group
    )
endif()

# Added: The Null platform links libm directly (the Windows CRT math functions come
# with the CRT shim wherever they are shimmed). macOS has no libm: its math functions are
# in libSystem, which the driver always links, so naming it there would fail to resolve.
# Added: on Windows the math functions are in the CRT, and MSVC has no m.lib at all.
if (NOT APPLE AND NOT WIN32)
    target_link_libraries(Jones3D-headless PRIVATE m)
endif()

if (J3D_PLATFORM_BABYLON)
    # Added: the painter's TrueType and PNG support (ADR-0031).
    target_link_libraries(Jones3D-headless PRIVATE babylon::ui babylon::font babylon::image)
endif()

# Added: the windowed Jones3D of the Babylon column: the Platform/Babylon composition root
# and run, plus the Platform/Null helpers except its main() and the headless runner. Linked
# exactly like Jones3D-headless. Jones3D-headless itself never sees these sources.
if (J3D_PLATFORM_BABYLON)
    set(JONES3D_WINDOWED_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/mainWindowed.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/Platform/Babylon/JonesBabylon.c"
        ${JONES3D_BABYLON_UI_SOURCES}
        ${JONES3D_CONTROL_SCHEME_SOURCES}
        ${JONES3D_BABYLON_MAINSYS_SOURCE}
    )
    foreach(JONES3D_NULL_SOURCE ${JONES3D_NULL_SOURCES})
        if (NOT JONES3D_NULL_SOURCE MATCHES "/Platform/Null/(main|JonesHeadless)\\.c$"
            AND NOT JONES3D_NULL_SOURCE STREQUAL "${JONES3D_NULL_MESSAGE_DIALOG_SOURCE}"
            AND NOT JONES3D_NULL_SOURCE STREQUAL "${JONES3D_NULL_SAVELOAD_DIALOG_SOURCE}"
            AND NOT JONES3D_NULL_SOURCE STREQUAL "${JONES3D_NULL_MAINMENU_DIALOG_SOURCE}"
            AND NOT JONES3D_NULL_SOURCE STREQUAL "${JONES3D_NULL_ENDSCREENS_DIALOG_SOURCE}"
            AND NOT JONES3D_NULL_SOURCE STREQUAL "${JONES3D_NULL_SETTINGS_DIALOG_SOURCE}"
            AND NOT JONES3D_NULL_SOURCE STREQUAL "${JONES3D_NULL_PAUSEMENU_DIALOG_SOURCE}"
            AND NOT JONES3D_NULL_SOURCE STREQUAL "${JONES3D_NULL_GAMEPLAY_DIALOG_SOURCE}"
            AND NOT JONES3D_NULL_SOURCE STREQUAL "${JONES3D_NULL_CONFIG_SOURCE}"
            AND NOT JONES3D_NULL_SOURCE STREQUAL "${JONES3D_NULL_CONTROLOPTIONS_DIALOG_SOURCE}"
            AND NOT JONES3D_NULL_SOURCE STREQUAL "${JONES3D_NULL_MAINSYS_SOURCE}")
            list(APPEND JONES3D_WINDOWED_SOURCES ${JONES3D_NULL_SOURCE})
        endif()
    endforeach()

    # Added: on Windows this is a console-subsystem program with the plain main() of
    # mainWindowed.cpp: SDL3 needs no SDL_main on desktop Windows, so MinGW and MSVC link it
    # as is, and its stderr diagnostics stay visible like on the other hosts.
    add_executable(Jones3D ${JONES3D_WINDOWED_SOURCES})
    target_include_directories(Jones3D PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
    set_target_properties(Jones3D PROPERTIES LINKER_LANGUAGE CXX)

    # Added: no group flags on macOS and MSVC, as for Jones3D-headless above.
    if (APPLE OR MSVC)
        target_link_libraries(Jones3D PRIVATE
            Jones3DGame
            ${JONES3D_LIBS_INTERFACE}
        )
    else()
        target_link_libraries(Jones3D PRIVATE
            -Wl,--start-group
            Jones3DGame
            ${JONES3D_LIBS_INTERFACE}
            -Wl,--end-group
        )
    endif()
    if (NOT APPLE AND NOT WIN32)
        target_link_libraries(Jones3D PRIVATE m)
    endif()

    target_link_libraries(Jones3D PRIVATE babylon::ui babylon::font babylon::image)
endif()

if (JONES3D_BUILD_TESTS)
    add_subdirectory(Main/Tests)
    add_subdirectory(Display/Tests)
    if (J3D_PLATFORM_BABYLON)
        add_subdirectory(Platform/Babylon/Tests)
    endif()

    # Added: --version answers before any engine code runs, so these tests need no game data.
    # The expected text is built from the root project's version so a version bump updates the tests.
    string(REPLACE "." "\\." JONES3D_VERSION_REGEX "${OpenJones3D_VERSION}")
    set(JONES3D_VERSION_REGEX "^${JONES3D_PRODUCT_NAME} v${JONES3D_VERSION_REGEX}")
    add_test(NAME Jones3D.Version COMMAND Jones3D-headless --version)
    set_tests_properties(Jones3D.Version PROPERTIES
        LABELS "unit;Jones3D"
        PASS_REGULAR_EXPRESSION "${JONES3D_VERSION_REGEX}"
    )
    if (TARGET Jones3D)
        add_test(NAME Jones3D.WindowedVersion COMMAND Jones3D --version)
        set_tests_properties(Jones3D.WindowedVersion PROPERTIES
            LABELS "unit;Jones3D"
            PASS_REGULAR_EXPRESSION "${JONES3D_VERSION_REGEX}"
        )
    endif()
endif()

# Added: the committed UI assets (fonts, prompt PNGs, licenses) sit next to
# the Babylon-column executables and the same tree installs next to them
# (ADR-0031). The game finds it below FileSystem::basePath(), so a checkout
# run needs no configuration path.
if (J3D_PLATFORM_BABYLON)
    set(JONES3D_UI_DATA_DIR "${CMAKE_SOURCE_DIR}/Data")
    foreach (JONES3D_BABYLON_EXECUTABLE Jones3D-headless Jones3D)
        if (TARGET ${JONES3D_BABYLON_EXECUTABLE})
            add_custom_command(TARGET ${JONES3D_BABYLON_EXECUTABLE} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${JONES3D_UI_DATA_DIR}" "$<TARGET_FILE_DIR:${JONES3D_BABYLON_EXECUTABLE}>/Data"
                COMMENT "Copying Data/ next to ${JONES3D_BABYLON_EXECUTABLE}")
        endif()
    endforeach()

    install(TARGETS Jones3D-headless Jones3D RUNTIME DESTINATION ".")
    install(DIRECTORY "${JONES3D_UI_DATA_DIR}" DESTINATION ".")
endif()
