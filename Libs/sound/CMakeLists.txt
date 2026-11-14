project(sound C CXX)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS *.c)
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS *.h)

list(FILTER SOURCES EXCLUDE REGEX ".*/Tests/.*")
list(FILTER HEADERS EXCLUDE REGEX ".*/Tests/.*")

# Added: the Babylon column replaces the Null sound driver with the adapter that plays on the
# babylon::audio::Device the composition root installs. The Null column keeps DriverNull.c.
if (J3D_PLATFORM_BABYLON)
    list(FILTER SOURCES EXCLUDE REGEX "/DriverNull\\.c$")
    list(APPEND SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/Babylon/DriverBabylon.cpp")
endif()

add_library(${PROJECT_NAME} STATIC ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(${PROJECT_NAME} PUBLIC j3dcore)
target_link_libraries(${PROJECT_NAME} PUBLIC rdroid)
target_link_libraries(${PROJECT_NAME} PUBLIC std)

# Added: the Babylon driver plays on the Babylon audio interface (ADR-0007, ADR-0010).
if (J3D_PLATFORM_BABYLON)
    target_link_libraries(${PROJECT_NAME} PUBLIC babylon::audio)
endif()

# Organize source files in Visual Studio
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES} ${HEADERS})

if (JONES3D_BUILD_TESTS)
    add_subdirectory(Tests)
endif()

# Added: the Babylon column's driver tests live next to the adapter they cover.
if (JONES3D_BUILD_TESTS AND J3D_PLATFORM_BABYLON)
    add_subdirectory(Babylon/Tests)
endif()
