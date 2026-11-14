project(smush C CXX)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS *.c)
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS *.h)

list(FILTER SOURCES EXCLUDE REGEX ".*/Tests/.*")
list(FILTER HEADERS EXCLUDE REGEX ".*/Tests/.*")

# Added: the platform layers of the timer and the sound driver. The Null column keeps the pthread tick thread
# (TimerNull.c) and the driver that publishes no SNDDRIVER (DsndNull.c), so its movies are skipped. The Babylon column
# (every Babylon host, Windows included) polls the timer on the platform clock (TimerPolled.c, no thread) and runs the
# SNDDRIVER that pushes the cutscene audio onto a Babylon stream voice (DsndBabylon.cpp) in place of DsndSilent.c. The
# Windows tick thread (TimerWin32.c) went: Windows only builds the Babylon column.
if (J3D_PLATFORM_BABYLON)
    list(FILTER SOURCES EXCLUDE REGEX "TimerNull\\.c$")
    list(FILTER SOURCES EXCLUDE REGEX "DsndNull\\.c$")
    list(FILTER SOURCES EXCLUDE REGEX "DsndSilent\\.c$")
    list(APPEND SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/insane/Sound/DsndBabylon.cpp")
else()
    list(FILTER SOURCES EXCLUDE REGEX "TimerPolled\\.c$")
    list(FILTER SOURCES EXCLUDE REGEX "DsndSilent\\.c$")
endif()

add_library(${PROJECT_NAME} STATIC ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(${PROJECT_NAME} PUBLIC j3dcore)
target_link_libraries(${PROJECT_NAME} PUBLIC rdroid)
target_link_libraries(${PROJECT_NAME} PUBLIC std)

# Added: the Babylon SMUSH driver pushes on the Babylon audio interface (ADR-0007, ADR-0010).
if (J3D_PLATFORM_BABYLON)
    target_link_libraries(${PROJECT_NAME} PUBLIC babylon::audio)
endif()

# Added: the Babylon column's driver is C++, so dependents must link with the C++ runtime.
if (J3D_PLATFORM_BABYLON)
    set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)
else()
    set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE C)
endif()

# Organize source files in Visual Studio
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES} ${HEADERS})

if (JONES3D_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

# Added: the Babylon column's SMUSH sound driver tests live next to the adapter they cover.
if (JONES3D_BUILD_TESTS AND J3D_PLATFORM_BABYLON)
    add_subdirectory(Babylon/Tests)
endif()
