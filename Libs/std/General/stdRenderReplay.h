// Added: M6.0c render capture replay. Reads an RCAP v1 file and makes the recorded renderer calls
// again through the public std3D_/stdDisplay_ functions, so a capture written during the replay can
// be compared with the original.
#ifndef STD_STDRENDERREPLAY_H
#define STD_STDRENDERREPLAY_H
#include <stddef.h>
#include <j3dcore/j3d.h>

// Added: the per-frame observer of the Babylon replay's image output. It is called after each
// replayed stdDisplay_Update record with that frame's index, counting from 0. C++ linkage keeps it
// assignable from the adapter's C++ callback; C sees an ordinary function pointer either way.
typedef void (*StdRenderReplayFrameCallback)(size_t frameIndex, void* pUserData);

J3D_EXTERN_C_START

/**
 * @brief Replay the render capture at pInPath.
 *
 * Reads the RCAP v1 header and every record, rebuilding the recorded arguments, blobs, handles and
 * fog density, then calls the public renderer functions. When pOutPath is not NULL the replay opens
 * a render capture there under the input file's column name before the first record and closes it
 * when the replay ends. A replay with capture on reproduces the original file byte for byte unless
 * the input contains records the replay does not reproduce (skipped calls, unknown record ids) or a
 * call returns different values.
 *
 * Needs the std host services (std_g_pHS) set up. Logs every error with STDLOG_ERROR. Returns 0
 * when the replay is done, 1 on a malformed or inconsistent input file (including a file that cannot
 * be opened), and 2 on usage errors (pInPath is NULL).
 */
int stdRenderReplay_Run(const char* pInPath, const char* pOutPath);

/**
 * @brief Replay the render capture at pInPath, calling pFrameCallback after every replayed
 * stdDisplay_Update record.
 *
 * Same inputs, outputs and return values as stdRenderReplay_Run. pFrameCallback (which may be NULL)
 * runs on the replay thread before the next record is read; it must not make renderer calls. A
 * failed image write is the callback's business and does not change the replay result.
 */
int stdRenderReplay_RunWithFrameCallback(
    const char* pInPath,
    const char* pOutPath,
    StdRenderReplayFrameCallback pFrameCallback,
    void* pUserData
);

J3D_EXTERN_C_END
#endif // STD_STDRENDERREPLAY_H
