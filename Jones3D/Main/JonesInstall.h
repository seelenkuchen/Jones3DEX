// Added: the game data install engine (M8 wizard, W2). It finds the places a player's copy of
// Indiana Jones and the Infernal Machine can come from, builds a copy plan into the user data
// directory, and runs it with progress, cancel and a free-space check. This is game-layer code:
// it never calls Babylon or a platform API. The platform layer fills JonesInstallHost with the
// values and services that differ per host; tests fill a fake. Copies go through the engine's
// j3dcrt-mapped stdio.
#ifndef JONES3D_JONESINSTALL_H
#define JONES3D_JONESINSTALL_H
#include <j3dcore/j3d.h>

#include <Jones3D/Main/JonesData.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

J3D_EXTERN_C_START

/** Longest source, plan or target path kept, including the terminating NUL. */
#define JONESINSTALL_MAX_PATH 1024

/** Most sources one search keeps. */
#define JONESINSTALL_MAX_SOURCES 64

/** Most host-provided volume or registry paths one host lists. */
#define JONESINSTALL_MAX_HOST_PATHS 64

/** Most files one plan keeps. */
#define JONESINSTALL_MAX_PLAN_ITEMS 256

/** Longest destination path below the target data directory, including the terminating NUL. */
#define JONESINSTALL_MAX_RELATIVE 512

/** Longest human-readable result message, including the terminating NUL. */
#define JONESINSTALL_MAX_MESSAGE 1024

/** Longest comma-separated list of missing required files, including the terminating NUL. */
#define JONESINSTALL_MAX_MISSING 256

/** The path style a host uses. Both styles are built on every host so source lists can be
 *  tested for Linux and Windows without changing the host. */
typedef enum JonesInstallPlatform
{
    JONESINSTALL_PLATFORM_LINUX   = 0,
    JONESINSTALL_PLATFORM_WINDOWS = 1,
} JonesInstallPlatform;

/** Where a source of the game data comes from. */
typedef enum JonesInstallSourceKind
{
    JONESINSTALL_SOURCE_STEAM         = 0, /**< A game folder inside a Steam library. */
    JONESINSTALL_SOURCE_GOG_INSTALLED = 1, /**< An installed GOG copy. */
    JONESINSTALL_SOURCE_GOG_INSTALLER = 2, /**< A GOG offline installer file, not yet extracted. */
    JONESINSTALL_SOURCE_DISC          = 3, /**< A mounted disc of the original release. */
    JONESINSTALL_SOURCE_FOLDER        = 4, /**< A folder the player chose. */
} JonesInstallSourceKind;

/** One source of the game data. All storage is inside the struct. */
typedef struct JonesInstallSource
{
    JonesInstallSourceKind kind;
    char aPath[JONESINSTALL_MAX_PATH]; /**< Folder, or installer file for GOG_INSTALLER. */
    bool bComplete;                    /**< The three required GOBs are present. */
    int discNumber;                    /**< 1 or 2 for a disc, 0 otherwise. */
} JonesInstallSource;

/** A bounded list of sources. */
typedef struct JonesInstallSourceList
{
    JonesInstallSource aSources[JONESINSTALL_MAX_SOURCES];
    size_t numSources;
} JonesInstallSourceList;

/** A bounded list of host paths (mounted volumes or GOG registry install folders). */
typedef struct JonesInstallPathList
{
    char aPaths[JONESINSTALL_MAX_HOST_PATHS][JONESINSTALL_MAX_PATH];
    size_t numPaths;
} JonesInstallPathList;

/**
 * @brief Reports the bytes available for a new file below pPath.
 *
 * pPath is the target data directory; it may not exist yet, in which case the implementation
 * should report the free space of its nearest existing parent. Map it to the volume holding the
 * path, not to the process working directory.
 *
 * @param pPath - Folder the install writes to. Never NULL.
 * @param pOutFreeBytes - Receives the free byte count. Never NULL.
 * @param pUserData - The value from JonesInstallHost::pFreeBytesUserData.
 * @return true when the count was written; false when the host cannot tell, which skips the
 *         free-space refusal in JonesInstall_Run.
 */
typedef bool (*JonesInstallGetFreeBytesFunc)(const char* pPath, int64_t* pOutFreeBytes,
    void* pUserData);

/**
 * @brief The host values and services the install engine needs.
 *
 * Every string is borrowed and must stay valid for the duration of the call. NULL and "" mean
 * unknown; the source or check that needs the value is skipped.
 */
typedef struct JonesInstallHost
{
    JonesInstallPlatform platform; /**< Separator style and source set to search. */

    JonesInstallGetFreeBytesFunc pfGetFreeBytes; /**< May be NULL to skip the space check. */
    void* pFreeBytesUserData;

    const char* pHome;            /**< HOME, the base of the Linux store and download paths. */
    const char* pXdgDataHome;     /**< XDG_DATA_HOME, for the wizard's target folder. */
    const char* pXdgDownloadDir;  /**< XDG download folder, additionally searched for installers. */
    const char* pProgramFiles;    /**< %ProgramFiles%, Windows only. */
    const char* pProgramFilesX86; /**< %ProgramFiles(x86)%, Windows only. */
    const char* pAppData;         /**< %APPDATA%, for the wizard's target folder. */

    const JonesInstallPathList* pVolumeRoots; /**< Mount bases or volume roots to probe for discs. */
    const JonesInstallPathList* pGogRegistryPaths; /**< Installed GOG folders from the registry. */
} JonesInstallHost;

/** The human-readable name of a source kind, for the wizard. Never NULL. */
const char* JonesInstall_SourceKindName(JonesInstallSourceKind kind);

/**
 * @brief Collects every game data source the host can see, in a stable order.
 *
 * Steam: the Linux and Windows Steam roots and the libraries listed in each
 * steamapps/libraryfolders.vdf. GOG: the registry folders the host lists, the default GOG and
 * GOG Galaxy folders, and GOG offline installer files in the download folders. Discs: volume
 * roots, or their immediate subdirectories on Linux, that hold install/license.txt and CD1.GOB
 * or CD2.GOB in the root or its Resource folder. Only existing folders and files are added.
 *
 * A source is complete when JonesData_FindResourceDir finds the resource folder and it holds
 * Jones3D.GOB, CD1.GOB and CD2.GOB. A GOG installer file is never complete: it needs extraction
 * first, and a disc usually needs another disc.
 *
 * Duplicate paths are added once. pOut is fully written even when the function returns false.
 *
 * @param pHost - Host values. May not be NULL.
 * @param pOut - Output list. May not be NULL.
 * @return true when at least one source was added.
 */
bool JonesInstall_FindSources(const JonesInstallHost* pHost, JonesInstallSourceList* pOut);

/**
 * @brief Fills a source for a folder the player chose.
 *
 * The folder must exist. It is added with kind JONESINSTALL_SOURCE_FOLDER and its completeness
 * checked like an installed copy.
 *
 * @param pHost - Host values, for the path style. May not be NULL.
 * @param pFolder - The chosen folder. May not be NULL.
 * @param pOut - Output source. May not be NULL.
 * @return true when the folder exists and pOut was filled.
 */
bool JonesInstall_GetFolderSource(const JonesInstallHost* pHost, const char* pFolder,
    JonesInstallSource* pOut);

/** One file of a plan. */
typedef struct JonesInstallPlanItem
{
    char aSource[JONESINSTALL_MAX_PATH];      /**< Absolute source path, in the host's style. */
    char aDestination[JONESINSTALL_MAX_RELATIVE]; /**< Path below the target, always '/'-separated. */
    int64_t size;                             /**< Size in bytes. */
} JonesInstallPlanItem;

/** A copy plan into a target data directory. All storage is inside the struct. */
typedef struct JonesInstallPlan
{
    JonesInstallPlanItem aItems[JONESINSTALL_MAX_PLAN_ITEMS];
    size_t numItems;
    int64_t totalBytes;              /**< Sum of the item sizes. */
    char aTargetDir[JONESINSTALL_MAX_PATH]; /**< Data directory the relative paths are below. */
    JonesInstallPlatform platform;
    bool bComplete;                  /**< Every required GOB is in the plan. */
    bool bNeedsDisc2;                /**< The source is disc 1 and CD2.GOB was not found. */
    char aMissing[JONESINSTALL_MAX_MISSING]; /**< Comma-separated required files not in the plan. */
} JonesInstallPlan;

/**
 * @brief Builds the copy plan from an install, disc or extracted installer folder.
 *
 * The plan holds the known resource files (Jones3D.GOB, CD1.GOB, CD2.GOB, jonesopn*.snm) found
 * in pSourceRoot or pSourceRoot/Resource, every file in Resource/Cog, KeySets/<name>.kfg, the help
 * files at the source root, and with bCopySaves SaveGames/<name>.nds. Names are matched
 * case-insensitively. Sources are recorded with their actual spelling; destinations use the
 * canonical name. A save is left out when the target already has that file.
 *
 * Items are sorted by destination so the plan is stable across hosts. A plan that needs disc 2
 * says so in bNeedsDisc2, and aMissing names every required GOB the plan lacks.
 *
 * @param platform - Separator style of pSourceRoot and pTargetDataDir.
 * @param pSourceRoot - Folder holding the data, an extracted installer, or a disc root.
 * @param pTargetDataDir - Data directory the relative destinations are below.
 * @param bCopySaves - Also copy SaveGames/<name>.nds that the target does not have.
 * @param pOut - Output plan. May not be NULL.
 * @return true when at least one file was planned and pOut was filled; false on invalid
 *         arguments, a plan that would overflow its limits, or no files.
 */
bool JonesInstall_BuildPlan(JonesInstallPlatform platform, const char* pSourceRoot,
    const char* pTargetDataDir, bool bCopySaves, JonesInstallPlan* pOut);

/** Called as the copy advances. May be NULL. */
typedef void (*JonesInstallProgressFunc)(int64_t bytesDone, int64_t bytesTotal,
    const char* pCurrentDestination, void* pUserData);

/** The outcome of JonesInstall_Run. */
typedef enum JonesInstallRunResult
{
    JONESINSTALL_RUN_OK        = 0,
    JONESINSTALL_RUN_CANCELLED = 1,
    JONESINSTALL_RUN_ERROR     = 2,
} JonesInstallRunResult;

/** The run result plus the message for the wizard. */
typedef struct JonesInstallRunOutcome
{
    JonesInstallRunResult result;
    char aMessage[JONESINSTALL_MAX_MESSAGE]; /**< Empty on success; why on error or cancel. */
    bool bNeedsDisc2;                        /**< Copied from the plan. */
} JonesInstallRunOutcome;

/**
 * @brief Runs a plan: checks free space, copies every file and reports progress.
 *
 * Before copying, the engine asks the host for the free bytes at the target directory and
 * refuses when they are below the plan total plus 5 %. A host that cannot tell skips the check.
 *
 * Each file is written to "<destination>.part" and renamed over the destination, so an error or
 * a cancel never leaves a truncated data file; the .part file is removed. A destination that
 * already has the source size is left alone and still counts as done. Existing destinations of
 * another size are replaced. Missing folders are created. The source is only read.
 *
 * The progress callback gets the bytes done, the plan total, and the relative destination of
 * the current file; skipped bytes count as done. The cancel flag is read before each file and
 * between copy blocks. The first error stops the run and names the file and the failed step.
 *
 * @param pPlan - Plan to run. May not be NULL.
 * @param pHost - Host for the free-space check; may be NULL, which skips it.
 * @param pfProgress - Progress callback, or NULL.
 * @param pProgressUserData - Passed to pfProgress.
 * @param pbCancel - Set to true to cancel; may be NULL.
 * @param pOut - Output outcome. May not be NULL.
 * @return JONESINSTALL_RUN_OK, JONESINSTALL_RUN_CANCELLED or JONESINSTALL_RUN_ERROR; pOut has
 *         the same result and the message for the last two.
 */
JonesInstallRunResult JonesInstall_Run(const JonesInstallPlan* pPlan, const JonesInstallHost* pHost,
    JonesInstallProgressFunc pfProgress, void* pProgressUserData, const bool* pbCancel,
    JonesInstallRunOutcome* pOut);

/**
 * @brief Checks the data installed below pTargetDataDir, for the wizard's last screen.
 *
 * Finds the resource folder with JonesData_FindResourceDir and runs JonesData_CheckDir with no
 * cache. When no resource folder exists, every known file is reported missing.
 *
 * @param pTargetDataDir - Data directory the install wrote to.
 * @param pOut - Output report. May not be NULL.
 * @return true when every required file is present.
 */
bool JonesInstall_CheckTarget(const char* pTargetDataDir, JonesDataReport* pOut);

J3D_EXTERN_C_END
#endif // JONES3D_JONESINSTALL_H
