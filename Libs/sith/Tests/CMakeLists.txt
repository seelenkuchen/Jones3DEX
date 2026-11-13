set(SITH_TEST_SOURCES
    sithTestRunner.c
    sithGamesaveTest.c
    sithControlTest.c
    ../Cog/Tests/sithCogTest.c
    ../Engine/Tests/sithCameraTest.c
    ../Engine/Tests/sithModernCameraTest.c
    ../Engine/Tests/sithPhysicsTest.c
    ../World/Tests/sithTemplateTest.c
    ../World/Tests/sithThingTest.c
    ../World/Tests/sithVoiceTest.c
)

set(SITH_TESTED_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/../Cog/sithCog.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../Engine/sithPhysics.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../World/sithTemplate.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../World/sithThing.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../World/sithVoice.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../std/General/stdConffile.c"
)

set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../Cog/sithCog.c" PROPERTY COMPILE_DEFINITIONS
    sithConsole_PrintString=sithCogTest_PrintString
)

# Observe the fixed integration step and the obsolete rate setting only in the
# test build of sithPhysics.c; the sith library is built without these seams.
if (MSVC)
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../Engine/sithPhysics.c" APPEND PROPERTY COMPILE_OPTIONS
        "/FI${CMAKE_CURRENT_SOURCE_DIR}/../Engine/Tests/sithPhysicsTestSeams.h"
    )
else()
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../Engine/sithPhysics.c" APPEND PROPERTY COMPILE_OPTIONS
        "-include" "${CMAKE_CURRENT_SOURCE_DIR}/../Engine/Tests/sithPhysicsTestSeams.h"
    )
endif()

set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../World/sithTemplate.c" PROPERTY COMPILE_DEFINITIONS
    rdThing_FreeEntry=sithThingTest_FreeRdThingEntry
    stdMemory_Malloc=sithThingTest_MemoryMalloc
    stdMemory_Free=sithThingTest_MemoryFree
    stdHashtbl_New=sithThingTest_HashtblNew
    stdHashtbl_Free=sithThingTest_HashtblFree
    stdHashtbl_Add=sithThingTest_HashtblAdd
    stdHashtbl_Find=sithThingTest_HashtblFind
    stdHashtbl_Remove=sithThingTest_HashtblRemove
)

set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../World/sithThing.c" PROPERTY COMPILE_DEFINITIONS
    sithTemplate_GetTemplate=sithThingTest_GetTemplate
    sithModel_Load=sithThingTest_LoadModel
    sithSprite_Load=sithThingTest_LoadSprite
    sithParticle_Load=sithThingTest_LoadParticle
    sithPuppet_LoadPuppetClass=sithThingTest_LoadPuppetClass
    sithSoundClass_Load=sithThingTest_LoadSoundClass
    sithCog_Load=sithThingTest_LoadCog
    sithMaterial_Load=sithThingTest_LoadMaterial
    sithAIClass_Load=sithThingTest_LoadAIClass
    rdThing_FreeEntry=sithThingTest_FreeRdThingEntry
    rdThing_SetModel3=sithThingTest_SetModel3
    rdThing_SetSprite3=sithThingTest_SetSprite3
    rdThing_SetParticleCloud=sithThingTest_SetParticleCloud
    rdPuppet_New=sithThingTest_NewRdPuppet
    stdMemory_Malloc=sithThingTest_MemoryMalloc
    stdMemory_Free=sithThingTest_MemoryFree
    sithAI_AllocAIFrames=sithThingTest_AllocAIFrames
    stdHashtbl_New=sithThingTest_HashtblNew
    stdHashtbl_Free=sithThingTest_HashtblFree
    stdHashtbl_Add=sithThingTest_HashtblAdd
    stdHashtbl_Find=sithThingTest_HashtblFind
    stdConfig_GetInt=sithThingTest_ConfigGetInt
    stdConfig_Contains=sithThingTest_ConfigContains
    stdConfig_SetInt=sithThingTest_ConfigSetInt
    sithGetPerformanceLevel=sithThingTest_GetPerformanceLevel
    sithIntersect_IsSphereInSector=sithThingTest_IsSphereInSector
    sithSoundMixer_StopAllSoundsThing=sithThingTest_StopAllSoundsThing
    stdComm_IsGameActive=sithThingTest_IsGameActive
    stdComm_IsGameHost=sithThingTest_IsGameHost
)

set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../../std/General/stdConffile.c" PROPERTY COMPILE_DEFINITIONS
    stdMemory_Malloc=sithThingTest_MemoryMalloc
    stdMemory_Free=sithThingTest_MemoryFree
)

add_executable(sithTests
    ${SITH_TEST_SOURCES}
    ${SITH_TESTED_SOURCES}
)

target_link_libraries(sithTests PRIVATE
    sith
    sound
    unity
    w32util
)

# Added: the Babylon column runs this suite under the adapter context.
std_use_babylon_test_main(sithTests "${CMAKE_CURRENT_SOURCE_DIR}/sithTestRunner.c")

target_compile_definitions(sithTests PRIVATE
    J3D_TEST
    SITH_TEST_TV_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../World/Tests/tv"
)

set_target_properties(sithTests PROPERTIES
    FOLDER "Tests/Libs/sith"
)

source_group("Tests" FILES ${SITH_TEST_SOURCES})
source_group("Tested Sources" FILES ${SITH_TESTED_SOURCES})

add_test(NAME sith COMMAND sithTests)
# The local fixtures perform bounded in-memory I/O; allow slower hosts and file reads.
set_tests_properties(sith PROPERTIES
    LABELS "unit;sith"
    TIMEOUT 60
)

# Added: the game-side rumble triggers (ADR-0028). The Null column only: it links the three game
# functions from the sith library against the Null stdControl device side, so the adapter's test
# seam records exactly what each trigger forwarded. The Babylon column keeps its own platform and
# menu rumble coverage.
if (NOT J3D_PLATFORM_BABYLON)
    add_executable(sithRumbleTests
        ../World/Tests/sithRumbleTestRunner.c
        ../World/Tests/sithRumbleTest.c
        ../../std/Null/stdControlNull.c
        ../../std/Null/stdControlDevicesNull.c
    )
    target_compile_definitions(sithRumbleTests PRIVATE J3D_TEST)
    target_link_libraries(sithRumbleTests PRIVATE
        sith
        sound
        unity
        w32util
    )
    if (UNIX)
        target_link_libraries(sithRumbleTests PRIVATE m)
    endif()

    set_target_properties(sithRumbleTests PROPERTIES
        FOLDER "Tests/Libs/sith"
    )

    add_test(NAME sith.Rumble COMMAND sithRumbleTests)
    set_tests_properties(sith.Rumble PROPERTIES
        LABELS "unit;sith"
        TIMEOUT 60
    )
endif()
