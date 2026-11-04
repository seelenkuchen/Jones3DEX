// Added: M6.0c render capture replay. Reads an RCAP v1 file and makes the recorded renderer calls
// again through the public std3D_/stdDisplay_ functions, so a capture written during the replay can
// be compared with the original.
#ifndef STD_STDRENDERREPLAY_H
#define STD_STDRENDERREPLAY_H
#include <j3dcore/j3d.h>

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

J3D_EXTERN_C_END
#endif // STD_STDRENDERREPLAY_H
