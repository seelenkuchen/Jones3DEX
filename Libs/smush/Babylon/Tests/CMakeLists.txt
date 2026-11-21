# Added: the Babylon column's SMUSH sound driver tests. They compile the adapter and the polled timer
# directly with J3D_TEST and run against the babylon::audio::NullDevice the shared test main installs, so
# the queue and the timer clock move only when a test says so.
set(SMUSHBABYLON_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../insane")

add_executable(smushDsndBabylonTests
    DsndBabylonTestRunner.cpp
    DsndBabylonTest.cpp
    "${SMUSHBABYLON_SOURCE_DIR}/Sound/DsndBabylon.cpp"
    "${SMUSHBABYLON_SOURCE_DIR}/Timer/TimerPolled.c"
)

target_compile_definitions(smushDsndBabylonTests PRIVATE
    J3D_TEST
    # Added: keep Unity's malloc/free macros out of C++: inline STL code compiled here would share
    # one COMDAT with production and free CRT memory through unity_free, corrupting the heap in Release.
    $<$<COMPILE_LANGUAGE:CXX>:UNITY_FIXTURE_NO_EXTRAS>
)

set_property(SOURCE DsndBabylonTestRunner.cpp APPEND PROPERTY COMPILE_DEFINITIONS
    "J3D_TEST_RUNNER_ENTRY=j3dTestRunner_Main"
)

target_include_directories(smushDsndBabylonTests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../.."
)

target_link_libraries(smushDsndBabylonTests PRIVATE
    smush
    babylon::audio
    unity
)

set_target_properties(smushDsndBabylonTests PROPERTIES
    FOLDER "Tests/Libs/smush"
    LINKER_LANGUAGE CXX
)

# Added: the shared Babylon test main installs the adapter context, a Null audio device included.
std_use_babylon_test_main(smushDsndBabylonTests "${CMAKE_CURRENT_SOURCE_DIR}/DsndBabylonTestRunner.cpp")

source_group("Tested Sources" FILES
    "${SMUSHBABYLON_SOURCE_DIR}/Sound/DsndBabylon.cpp"
    "${SMUSHBABYLON_SOURCE_DIR}/Timer/TimerPolled.c"
)

add_test(NAME smush.DsndBabylon COMMAND smushDsndBabylonTests)
set_tests_properties(smush.DsndBabylon PROPERTIES
    LABELS "unit;smush"
    TIMEOUT 60
)
