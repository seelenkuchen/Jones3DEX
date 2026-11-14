# Added: runs the windowed data tool with no display. CTest unset/empties DISPLAY and
# WAYLAND_DISPLAY for this script; the tool must fail on the empty folder, not on SDL video.
if ( NOT DEFINED TOOL OR NOT DEFINED HASH_DIR )
    message(FATAL_ERROR "RunHashDataTool.cmake needs TOOL and HASH_DIR")
endif()

file(REMOVE_RECURSE "${HASH_DIR}")
file(MAKE_DIRECTORY "${HASH_DIR}")

execute_process(
    COMMAND "${TOOL}" --hash-data "${HASH_DIR}"
    OUTPUT_VARIABLE toolOutput
    ERROR_VARIABLE toolError
    RESULT_VARIABLE toolResult
)

message("${toolOutput}${toolError}")

if ( NOT toolResult EQUAL 1 )
    message(FATAL_ERROR "expected --hash-data to exit 1, got ${toolResult}")
endif()
