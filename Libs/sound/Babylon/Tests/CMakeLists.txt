# Added: the Babylon column's sound driver tests. They compile the adapter and the ADPCM library
# directly and run against the babylon::audio::NullDevice the test main installs, so voice
# positions advance only when the test says so and the platform clock is a NullClock.
set(SOUNDBABYLON_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")

add_executable(soundDriverBabylonTests
    soundBabylonTestRunner.cpp
    soundBabylonTestMain.cpp
    DriverBabylonTest.cpp
    "${SOUNDBABYLON_SOURCE_DIR}/Babylon/DriverBabylon.cpp"
    "${SOUNDBABYLON_SOURCE_DIR}/AudioLib.c"
)

target_compile_definitions(soundDriverBabylonTests PRIVATE
    J3D_TEST
)

set_property(SOURCE soundBabylonTestRunner.cpp APPEND PROPERTY COMPILE_DEFINITIONS
    "J3D_TEST_RUNNER_ENTRY=j3dTestRunner_Main")

target_include_directories(soundDriverBabylonTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../.."
)

target_link_libraries(soundDriverBabylonTests PRIVATE
    std
    babylon::audio
    unity
)

set_target_properties(soundDriverBabylonTests PROPERTIES
    FOLDER "Tests/Libs/sound"
    LINKER_LANGUAGE CXX
)

source_group("Tested Sources" FILES
    "${SOUNDBABYLON_SOURCE_DIR}/Babylon/DriverBabylon.cpp"
    "${SOUNDBABYLON_SOURCE_DIR}/AudioLib.c"
)

add_test(NAME sound.Babylon COMMAND soundDriverBabylonTests)
set_tests_properties(sound.Babylon PROPERTIES
    LABELS "unit;sound"
    TIMEOUT 60
)
