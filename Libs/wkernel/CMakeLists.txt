project(wkernel C)

file(GLOB_RECURSE SOURCES *.c)
file(GLOB_RECURSE HEADERS *.h)

# Added: The Null platform has no Windows kernel services, so only the wkernelNull.c stub
# is compiled (the Win32 wkernel.c went with the DirectX game, ADR-0011).
# Added: the Babylon column runs its wkernel on the Babylon platform event queue: wkernelBabylon.cpp
# replaces wkernelNull.c there, on the std adapter's pump and the babylon::platform window.
if(J3D_PLATFORM_BABYLON)
    enable_language(CXX)
    add_library(${PROJECT_NAME} STATIC wkernelBabylon.cpp ${HEADERS})
else()
    file(GLOB NULL_SOURCES *Null.c)
    add_library(${PROJECT_NAME} STATIC ${NULL_SOURCES} ${HEADERS})
endif()

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(${PROJECT_NAME} PUBLIC j3dcore std)
if(J3D_PLATFORM_BABYLON)
    target_link_libraries(${PROJECT_NAME} PUBLIC babylon::platform)
endif()

# Organize source files in Visual Studio
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES} ${HEADERS})

# Added: the Babylon column's wkernel suite (the column's own event loop entry points).
if(JONES3D_BUILD_TESTS AND J3D_PLATFORM_BABYLON)
    add_subdirectory(Tests)
endif()


