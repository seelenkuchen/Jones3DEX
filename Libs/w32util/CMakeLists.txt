project(w32util C)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS *.c)
file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS *.h)

# Altered: only the wuRegistryNull.c stub is left; the Win32 registry version (wuRegistry.c) went
# with the DirectX game (ADR-0011).
add_library(${PROJECT_NAME} STATIC ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(${PROJECT_NAME} PUBLIC j3dcore std)

# Organize source files in Visual Studio
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES} ${HEADERS})
