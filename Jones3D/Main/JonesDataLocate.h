// Added: finds the player's game data (ADR-0021, M8.4). The search order is the --data option,
// installPath from Jones.cfg, <user dir>/data, the executable and working directories, and a set
// of known Steam and GOG locations. The module is game-layer code: it never calls Babylon. A
// windowed adapter registers a prompt handler for the folder picker; without one the module
// reports the problem on stderr and fails.
#ifndef JONES3D_JONESDATALOCATE_H
#define JONES3D_JONESDATALOCATE_H
#include <j3dcore/j3d.h>

#include <stdbool.h>
#include <stddef.h>

J3D_EXTERN_C_START

/** Longest candidate, install or resource path kept, including the terminating NUL. */
#define JONESDATALOCATE_MAX_PATH 1024

/** Most candidates one search keeps. */
#define JONESDATALOCATE_MAX_CANDIDATES 64

/** Most Steam libraries one environment keeps, roots and libraryfolders.vdf entries together. */
#define JONESDATALOCATE_MAX_STEAM_LIBRARIES 32

/** Longest prompt message, including the terminating NUL. */
#define JONESDATALOCATE_MESSAGE_MAX 4096

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
    JONESDATALOCATE_RULE_STEAM,
    JONESDATALOCATE_RULE_GOG,
    JONESDATALOCATE_RULE_CHOSEN,
} JonesDataLocateRule;

/** One folder to probe. */
typedef struct JonesDataLocateCandidate
{
    char aPath[JONESDATALOCATE_MAX_PATH];
    JonesDataLocateRule rule;
} JonesDataLocateCandidate;

/** A bounded list of paths, used for Steam libraries. */
typedef struct JonesDataLocatePathList
{
    char aPaths[JONESDATALOCATE_MAX_STEAM_LIBRARIES][JONESDATALOCATE_MAX_PATH];
    size_t numPaths;
} JonesDataLocatePathList;

/** The values the pure candidate builder needs. Unset values are NULL or "". */
typedef struct JonesDataLocateEnv
{
    JonesDataLocatePlatform platform;
    const char* pDataOption;      /**< --data, the first rule. */
    const char* pInstallPath;     /**< installPath from Jones.cfg. */
    const char* pUserDataDir;     /**< <user dir>/data, or "data" in portable mode. */
    const char* pExecutableDir;   /**< Folder holding the executable. */
    const char* pWorkingDir;      /**< Current working directory. */
    const char* pHome;            /**< Player's home directory. */
    const char* pProgramFiles;    /**< %ProgramFiles%, Windows only. */
    const char* pProgramFilesX86; /**< %ProgramFiles(x86)%, Windows only. */
    const JonesDataLocatePathList* pSteamLibraries; /**< Roots plus parsed libraryfolders.vdf entries. */
} JonesDataLocateEnv;

/** The outcome of a search or of the prompt loop. All storage is inside the struct. */
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

/**
 * @brief The prompt hook for a missing or incomplete installation.
 *
 * Shows pMessage and asks for a folder. Returns true and writes the chosen folder to pOutDir when
 * the player picked one, false when the player quit or no dialog could be shown. The caller
 * validates the folder and asks again with a message naming its missing files.
 */
typedef bool (*JonesDataLocatePromptHandler)(const char* pMessage, char* pOutDir, size_t outSize,
    void* pUserData);

/** The human-readable name of a rule, for the status line. Never NULL. */
const char* JonesDataLocate_RuleName(JonesDataLocateRule rule);

/** Builds the ordered candidate list. Pure: it only reads pEnv. Returns the number written. */
size_t JonesDataLocate_BuildCandidates(const JonesDataLocateEnv* pEnv,
    JonesDataLocateCandidate* aOut, size_t maxOut);

/**
 * @brief Parses the "path" values of a steamapps/libraryfolders.vdf text.
 *
 * Pure and defensive: a line is ignored when it is malformed, a path is empty, or a path does not
 * fit JONESDATALOCATE_MAX_PATH. Over-long lines are skipped without affecting the lines after them.
 */
void JonesDataLocate_ParseLibraryFolders(const char* pText, JonesDataLocatePathList* pOut);

/** Stores the --data option for the next JonesDataLocate_Startup. NULL or "" clears it. */
void JonesDataLocate_SetDataPath(const char* pPath);

/**
 * @brief Provides the executable and working directories the host already knows.
 *
 * Set by the host before JonesMain_Startup. NULL or "" means unknown, and that rule is skipped.
 */
void JonesDataLocate_SetHostDirs(const char* pExecutableDir, const char* pWorkingDir);

/**
 * @brief Provides the environment values the store search needs.
 *
 * Set by the host before JonesMain_Startup. pHome is the player's home; pProgramFiles and
 * pProgramFilesX86 are the Windows program folders. NULL or "" means unknown, and that candidate
 * source is skipped.
 */
void JonesDataLocate_SetHostEnvironment(const char* pHome, const char* pProgramFiles,
    const char* pProgramFilesX86);

/** Registers the prompt hook. NULL restores the stderr-only default. */
void JonesDataLocate_SetPromptHandler(JonesDataLocatePromptHandler pfHandler, void* pUserData);

/** Clears the --data option and the prompt hook. */
void JonesDataLocate_Reset(void);

/**
 * @brief Probes candidates in order.
 *
 * The first candidate whose resource directory has Jones3D.GOB, CD1.GOB and CD2.GOB wins. The
 * first incomplete candidate (Jones3D.GOB present, a required file missing) is remembered in pOut
 * for the prompt message. Returns true when a complete candidate was found.
 */
bool JonesDataLocate_Search(const JonesDataLocateCandidate* aCandidates, size_t numCandidates,
    JonesDataLocateResult* pOut);

/**
 * @brief Builds candidates, searches them, then asks through the prompt handler when needed.
 *
 * On success writes the install and resource directories and the rule that found them. A folder
 * chosen through the handler is written to installPath in Jones.cfg when the config is started.
 * Without a handler, or when the handler returns false, the message is printed to stderr and the
 * function returns false.
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

/**
 * @brief --install-data: copies the known files from pDir to <user dir>/data/Resource.
 * @return 0 when the copy is complete, 1 otherwise.
 */
int JonesDataLocate_InstallData(const char* pDir);

/**
 * @brief Copies the known files from pSourceDir to pTargetDataDir/Resource, then checks them.
 * @return 0 when the copy is complete, 1 otherwise.
 */
int JonesDataLocate_InstallDataTo(const char* pSourceDir, const char* pTargetDataDir);

J3D_EXTERN_C_END
#endif // JONES3D_JONESDATALOCATE_H
