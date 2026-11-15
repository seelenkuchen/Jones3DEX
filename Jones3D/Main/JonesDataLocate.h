// Added: finds the player's game data (ADR-0021, M8.4). The search order is the --data option,
// installPath from Jones.cfg, <user dir>/GameData, the executable directory and its GameData
// subfolder, and the working directory. The setup wizard installs the data, so the game itself no
// longer searches stores or asks for a folder. The module is game-layer code: it never calls
// Babylon.
#ifndef JONES3D_JONESDATALOCATE_H
#define JONES3D_JONESDATALOCATE_H
#include <j3dcore/j3d.h>

#include <stdbool.h>
#include <stddef.h>

J3D_EXTERN_C_START

/** Folder name of the game files below the chosen base or the user directory. */
#define JONESDATALOCATE_GAMEDATA_DIR "GameData"

/** Longest candidate, install or resource path kept, including the terminating NUL. */
#define JONESDATALOCATE_MAX_PATH 1024

/** Most candidates one search keeps. */
#define JONESDATALOCATE_MAX_CANDIDATES 64

/** The path style a candidate list is built for. Both styles are available on every host so the
 *  list builder can be tested for Linux and Windows without changing the host. */
typedef enum JonesDataLocatePlatform
{
    JONESDATALOCATE_PLATFORM_LINUX = 0,
    JONESDATALOCATE_PLATFORM_WINDOWS = 1,
} JonesDataLocatePlatform;

/** The rule that produced a candidate. The order of the values is the search order. */
typedef enum JonesDataLocateRule
{
    JONESDATALOCATE_RULE_DATA_OPTION = 0,
    JONESDATALOCATE_RULE_INSTALL_PATH,
    JONESDATALOCATE_RULE_USER_DATA,
    JONESDATALOCATE_RULE_EXECUTABLE_DIR,
    JONESDATALOCATE_RULE_WORKING_DIR,
} JonesDataLocateRule;

/** One folder to probe. */
typedef struct JonesDataLocateCandidate
{
    char aPath[JONESDATALOCATE_MAX_PATH];
    JonesDataLocateRule rule;
} JonesDataLocateCandidate;

/** The values the pure candidate builder needs. Unset values are NULL or "". */
typedef struct JonesDataLocateEnv
{
    JonesDataLocatePlatform platform;
    const char* pDataOption;      /**< --data, the first rule. */
    const char* pInstallPath;     /**< installPath from Jones.cfg. */
    const char* pUserDataDir;     /**< <user dir>/GameData, or "GameData" in portable mode. */
    const char* pExecutableDir;   /**< Folder holding the executable. */
    const char* pWorkingDir;      /**< Current working directory. */
} JonesDataLocateEnv;

/** The outcome of a search. All storage is inside the struct. Every path uses the separator
 *  style of the platform the candidate list was built for ('/' on Linux, '\\' on Windows);
 *  JonesFile accepts that spelling. */
typedef struct JonesDataLocateResult
{
    bool bFound;
    JonesDataLocateRule rule;
    char aInstallPath[JONESDATALOCATE_MAX_PATH]; /**< Folder to pass to JonesFile_SetInstallPath. */
    char aResourceDir[JONESDATALOCATE_MAX_PATH]; /**< Folder holding the GOB files. */
    bool bIncomplete;                            /**< An incomplete candidate was seen, none complete. */
    char aIncompleteDir[JONESDATALOCATE_MAX_PATH];
    char aMissingFiles[256];                     /**< Comma-separated required files the incomplete one lacks. */
} JonesDataLocateResult;

/** The human-readable name of a rule, for the status line. Never NULL. */
const char* JonesDataLocate_RuleName(JonesDataLocateRule rule);

/** Builds the ordered candidate list. Pure: it only reads pEnv. Returns the number written. */
size_t JonesDataLocate_BuildCandidates(const JonesDataLocateEnv* pEnv,
    JonesDataLocateCandidate* aOut, size_t maxOut);

/** Stores the --data option for the next JonesDataLocate_Startup. NULL or "" clears it. */
void JonesDataLocate_SetDataPath(const char* pPath);

/**
 * @brief Provides the executable and working directories the host already knows.
 *
 * Set by the host before JonesMain_Startup. NULL or "" means unknown, and that rule is skipped.
 */
void JonesDataLocate_SetHostDirs(const char* pExecutableDir, const char* pWorkingDir);

/** Clears the --data option. */
void JonesDataLocate_Reset(void);

/**
 * @brief Probes candidates in order.
 *
 * The first candidate whose resource directory has Jones3D.GOB, CD1.GOB and CD2.GOB wins. The
 * first incomplete candidate (Jones3D.GOB present, a required file missing) is remembered in pOut.
 * Returns true when a complete candidate was found.
 */
bool JonesDataLocate_Search(const JonesDataLocateCandidate* aCandidates, size_t numCandidates,
    JonesDataLocateResult* pOut);

/**
 * @brief Builds candidates, searches them and returns the result.
 *
 * On success writes the install and resource directories and the rule that found them. When
 * nothing is found it prints one plain "Game data: not found." line to stderr and returns false.
 */
bool JonesDataLocate_Resolve(const JonesDataLocateEnv* pEnv, JonesDataLocateResult* pOut);

/**
 * @brief The game's entry point: builds the environment from the user directory and the host.
 *
 * @param pConfigInstallPath - installPath read from Jones.cfg, as the second candidate.
 * @param pOut - Result, filled when game data was found.
 * @return true when the data was found.
 */
bool JonesDataLocate_Startup(const char* pConfigInstallPath, JonesDataLocateResult* pOut);

/**
 * @brief --hash-data: prints JonesData_PrintHashRows for the resource dir in pDir.
 * @return 0 on success, 1 with a message on stderr otherwise.
 */
int JonesDataLocate_HashData(const char* pDir);

J3D_EXTERN_C_END
#endif // JONES3D_JONESDATALOCATE_H
