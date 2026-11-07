project(smush C)

file(GLOB_RECURSE SOURCES *.c)
file(GLOB_RECURSE HEADERS *.h)

list(FILTER SOURCES EXCLUDE REGEX ".*/Tests/.*")
list(FILTER HEADERS EXCLUDE REGEX ".*/Tests/.*")

# Added: the platform layers of the timer and the sound driver. The Null column keeps the pthread tick thread
# (TimerNull.c) and the driver that publishes no SNDDRIVER (DsndNull.c), so its movies are skipped. The Babylon column
# (every Babylon host, Windows included) polls the timer on the platform clock (TimerPolled.c, no thread) and runs the
# silent SNDDRIVER (DsndSilent.c), so its movies play without sound until M7. The Windows tick thread (TimerWin32.c)
# went: Windows only builds the Babylon column.
if (J3D_PLATFORM_BABYLON)
    list(FILTER SOURCES EXCLUDE REGEX "TimerNull\\.c$")
    list(FILTER SOURCES EXCLUDE REGEX "DsndNull\\.c$")
else()
    list(FILTER SOURCES EXCLUDE REGEX "TimerPolled\\.c$")
    list(FILTER SOURCES EXCLUDE REGEX "DsndSilent\\.c$")
endif()

add_library(${PROJECT_NAME} STATIC ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(${PROJECT_NAME} PUBLIC j3dcore)
target_link_libraries(${PROJECT_NAME} PUBLIC rdroid)
target_link_libraries(${PROJECT_NAME} PUBLIC std)

set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE C)

# Organize source files in Visual Studio
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES} ${HEADERS})

if (JONES3D_BUILD_TESTS)
    add_subdirectory(Tests)
endif()
