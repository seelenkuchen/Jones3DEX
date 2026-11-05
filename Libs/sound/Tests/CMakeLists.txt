set(SOUND_TEST_SOURCES
    soundTestRunner.c
    AudioLibTest.c
    SoundTest.c
)

set(SOUND_TESTED_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/../Sound.c"
)

add_executable(soundTests
    ${SOUND_TEST_SOURCES}
    ${SOUND_TESTED_SOURCES}
)

target_link_libraries(soundTests PRIVATE
    sound
    unity
)

# Added: the Babylon column runs this suite under the adapter context.
std_use_babylon_test_main(soundTests "${CMAKE_CURRENT_SOURCE_DIR}/soundTestRunner.c")

target_compile_definitions(soundTests PRIVATE
    J3D_TEST
    AUDIOLIB_TEST_TV_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tv"
)

set_target_properties(soundTests PROPERTIES
    FOLDER "Tests/Libs/sound"
)

source_group("Tests" FILES ${SOUND_TEST_SOURCES})
source_group("Tested Sources" FILES ${SOUND_TESTED_SOURCES})

add_test(NAME sound COMMAND soundTests)
# The in-memory suite normally finishes in under a second; allow slower hosts.
set_tests_properties(sound PROPERTIES
    LABELS "unit;sound"
    TIMEOUT 60
)
