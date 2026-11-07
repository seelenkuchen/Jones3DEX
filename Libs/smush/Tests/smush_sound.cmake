# Test list of the smush Sound area (Engine, Mixer, DsndNull, MixerAsm). Add this area's test files and tested library sources here (its test file is already listed):
#   list(APPEND SMUSH_TEST_SOURCES SmushSoundTest.c)
#   list(APPEND SMUSH_TESTED_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../<path>.c")
list(APPEND SMUSH_TEST_SOURCES
    SmushSoundTest.c
)

# Compiled into the test executable with J3D_TEST for the Engine_TestReset seam. Altered: Dsnd.c, the DirectSound
# driver, went with the Win32/DirectX game (ADR-0011); DsndNull.c, which replaced it, needs no seam and comes from the
# smush library.
list(APPEND SMUSH_TESTED_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/../insane/Sound/Engine.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../insane/Sound/Mixer.c"
)
