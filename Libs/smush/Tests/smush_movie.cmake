# Test list of the smush Movie area (M6.8): the Babylon column's SNDDRIVER (DsndBabylon.cpp), polled timer
# (TimerPolled.c) and SmushPlay end to end on a synthetic stream. The group is empty on the Null column.
list(APPEND SMUSH_TEST_SOURCES
    SmushMovieTest.c
)

# Compiled into the test executable with J3D_TEST for the TimerPolled_SetClockForTest seam (the fake clock); the smush
# library's copy stays out of the link, as for Engine.c (smush_sound.cmake).
if (J3D_PLATFORM_BABYLON)
    list(APPEND SMUSH_TESTED_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/../insane/Timer/TimerPolled.c"
    )
endif()
