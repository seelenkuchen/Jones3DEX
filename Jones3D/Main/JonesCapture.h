#ifndef JONES3D_JONECAPTURE_H
#define JONES3D_JONECAPTURE_H
#include <j3dcore/j3d.h>

#include <stdbool.h>
#include <stddef.h>

J3D_EXTERN_C_START

/**
 * @brief Removes a "-capture <file>" option from pCmdLine.
 *
 * The option name is case-sensitive. A value may be quoted to keep spaces in it, for example
 * "C:\My Captures\a.rcap"; the quotes are removed from pPath. When the option is missing this
 * returns false and leaves both outputs as empty strings. When "-capture" has no value, the value
 * is empty or a result does not fit its buffer, it logs an error and returns false with both
 * outputs left empty. On success pPath receives the file name and pRest receives the command line
 * without the option and its value: the other tokens are unchanged, separated by single spaces,
 * with no leading or trailing space. pCmdLine is not modified.
 *
 * @param pCmdLine - The command line to parse. May be NULL (treated as no option).
 * @param pPath - Output buffer for the file name.
 * @param pathSize - Size of pPath in bytes, including the terminating NUL.
 * @param pRest - Output buffer for the remaining command line.
 * @param restSize - Size of pRest in bytes, including the terminating NUL.
 * @return true when the option was found and both outputs were written.
 */
bool JonesCapture_ParseCommandLine(const char* pCmdLine, char* pPath, size_t pathSize, char* pRest, size_t restSize);

J3D_EXTERN_C_END
#endif // JONES3D_JONECAPTURE_H
