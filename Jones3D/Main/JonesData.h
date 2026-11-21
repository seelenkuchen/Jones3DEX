#ifndef JONES3D_JONESDATA_H
#define JONES3D_JONESDATA_H
#include <j3dcore/j3d.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

J3D_EXTERN_C_START

/** The game files the data check knows about, in report order: Jones3D.GOB, CD1.GOB, CD2.GOB,
 *  jonesopn.snm, jonesopn_3.snm, jonesopn_4.snm, jonesopn_5.snm and jonesopn_8.snm. */
#define JONESDATA_MAX_FILES 8

/** Longest file path the report and the cache accept, including the terminating NUL. */
#define JONESDATA_MAX_PATH 1024

/** Length of a SHA-256 as lower-case hexadecimal characters, without the terminating NUL. */
#define JONESDATA_HASH_LENGTH 64

/** Size of a buffer holding a SHA-256 as lower-case hexadecimal, including the terminating NUL. */
#define JONESDATA_HASH_SIZE (JONESDATA_HASH_LENGTH + 1)

/** One row of a known release: a file name, its exact size and its SHA-256. */
typedef struct JonesDataReleaseFile
{
    const char* pName;
    int64_t size;
    char aSha256[JONESDATA_HASH_SIZE];
} JonesDataReleaseFile;

/** A known release: a name and the file rows that belong to it. */
typedef struct JonesDataRelease
{
    const char* pName;
    JonesDataReleaseFile aFiles[JONESDATA_MAX_FILES];
    size_t numFiles;
} JonesDataRelease;

/** The check result for one known file. */
typedef struct JonesDataFile
{
    const char* pName;                 /**< The known name; not owned by the caller. */
    bool bRequired;                    /**< Required files are Jones3D.GOB, CD1.GOB and CD2.GOB. */
    bool bPresent;                     /**< An exactly, lower- or upper-case named file was found. */
    char aPath[JONESDATA_MAX_PATH];    /**< Actual path found; empty when the file is missing. */
    int64_t size;                      /**< Size in bytes; 0 when missing. */
    char aSha256[JONESDATA_HASH_SIZE]; /**< Lower-case hex; empty when missing or not hashed. */
    bool bMatchesRelease;              /**< The file matches a row of an active release. */
} JonesDataFile;

/** The result of JonesData_CheckDir. All storage is inside the struct and owned by it. */
typedef struct JonesDataReport
{
    JonesDataFile aFiles[JONESDATA_MAX_FILES];
    size_t numFiles;
    bool bComplete;                  /**< Every required file is present. */
    const char* pRelease;            /**< Name of the one matching release, or NULL. */
    size_t numMissingRequired;       /**< Number of required files that are not present. */
} JonesDataReport;

/**
 * @brief Finds the game's resource directory.
 *
 * Returns true when pDir itself, pDir/Resource or pDir/resource contains Jones3D.GOB (in exact,
 * lower or upper case) and writes that directory to pOutDir. On failure, and when the directory
 * path does not fit pOutDir, returns false and writes an empty string; it never truncates pDir.
 *
 * @param pDir - The folder to inspect. May not be NULL.
 * @param pOutDir - Output buffer for the resource directory.
 * @param outSize - Size of pOutDir in bytes, including the terminating NUL.
 * @return true when a resource directory was found and written.
 */
bool JonesData_FindResourceDir(const char* pDir, char* pOutDir, size_t outSize);

/**
 * @brief Checks the known files in a resource directory against the active release table.
 *
 * Fills every field of pOut and returns true when every required file is present, whether or not
 * its release is known. A file is looked up by its exact name first, then all lower case, then all
 * upper case, so copies from CDs work on case-sensitive file systems.
 *
 * Only files whose size matches a row for that name in the active release table are hashed. The
 * hash of every other present file stays empty. A file matches a release row when both its size
 * and hash match. pOut->pRelease names the one release whose rows match every present file and
 * whose required files are all present; it is NULL otherwise.
 *
 * The cache at pCachePath (may be NULL) reuses hashes of files whose path, size and modification
 * time are unchanged. After the check it is rewritten with the files that have a hash. A missing or
 * unreadable cache is not an error, and malformed cache lines are ignored.
 *
 * @param pResourceDir - Directory holding the game files. May be NULL only to report all missing.
 * @param pCachePath - Cache file path, or NULL for no cache.
 * @param pOut - Output report. May not be NULL.
 * @return true when every required file is present.
 */
bool JonesData_CheckDir(const char* pResourceDir, const char* pCachePath, JonesDataReport* pOut);

/**
 * @brief Replaces the release table used by JonesData_CheckDir.
 *
 * The table and every string it points to must stay alive while it is in use. Passing a NULL
 * aReleases restores the built-in table. This function is for tests and tools; the game keeps the
 * built-in table.
 *
 * @param aReleases - Release rows to use, or NULL to restore the built-in table.
 * @param numReleases - Number of entries in aReleases.
 */
void JonesData_SetReleaseTable(const JonesDataRelease* aReleases, size_t numReleases);

/**
 * @brief Logs a short human-readable summary of a report (ADR-0021).
 *
 * A known release gets one status line naming it. An incomplete report gets one error line per
 * missing required file. An otherwise unknown report gets one warning line per present file that
 * did not match a known release, or one warning when every present file matches a row but the rows
 * come from different releases. Logging is a no-op before stdStartup.
 *
 * @param pReport - Report to log. May be NULL.
 */
void JonesData_LogReport(const JonesDataReport* pReport);

/**
 * @brief Prints known files that are present as C initializer rows.
 *
 * Hashes every present known file, ignoring the size fast path, and writes one row per file in
 * known-file order: `{ "CD1.GOB", 353666116, "ac97..." },`. This is for contributors adding a
 * release to the table.
 *
 * @param pResourceDir - Directory holding the game files.
 * @param pOut - Stream to write to.
 * @return true when at least one known file was found.
 */
bool JonesData_PrintHashRows(const char* pResourceDir, FILE* pOut);

/**
 * @brief Hashes a file with SHA-256 and writes lower-case hexadecimal.
 *
 * Streams the file in 64 KiB blocks. For tests and tools.
 *
 * @param pPath - File to hash.
 * @param pHashOut - Output buffer of at least JONESDATA_HASH_SIZE bytes; empty on failure.
 * @param outSize - Size of pHashOut in bytes, including the terminating NUL.
 * @return true when the file was read and the hash written.
 */
bool JonesData_Sha256File(const char* pPath, char* pHashOut, size_t outSize);

J3D_EXTERN_C_END
#endif // JONES3D_JONESDATA_H
