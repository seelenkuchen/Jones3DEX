// Added: finds, asks for and installs the game data (ADR-0021, M8.4). The candidate list builder
// and the libraryfolders.vdf parser are pure and take their environment as values, so both path
// styles can be tested without a real home directory. Probing the file system stays separate.
#include "JonesDataLocate.h"

#include <Jones3D/Main/JonesData.h>

#include <sith/Main/sithMain.h>

#include <std/General/std.h>
#include <std/General/stdConfig.h>
#include <std/General/stdMemory.h>
#include <std/General/stdUserDir.h>
#include <std/General/stdUtil.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <direct.h>
#define JONESDATALOCATE_MKDIR(path) _mkdir(path)
#define JONESDATALOCATE_STAT _stat
#define JONESDATALOCATE_STAT_ISDIR(mode) (((mode) & _S_IFDIR) != 0)
#define JONESDATALOCATE_STAT_ISREG(mode) (((mode) & _S_IFREG) != 0)
#else
#define JONESDATALOCATE_MKDIR(path) mkdir((path), 0700)
#define JONESDATALOCATE_STAT stat
#define JONESDATALOCATE_STAT_ISDIR(mode) S_ISDIR(mode)
#define JONESDATALOCATE_STAT_ISREG(mode) S_ISREG(mode)
#endif

// TODO: [COMPAT] confirm the Steam install folder name
#define JONESDATALOCATE_STEAM_NAME "Indiana Jones and the Infernal Machine"
#define JONESDATALOCATE_MAX_NAME   64
#define JONESDATALOCATE_VDF_LINE_MAX 2048
#define JONESDATALOCATE_VDF_MAX_SIZE (1024u * 1024u)
#define JONESDATALOCATE_COPY_SIZE   65536u
#define JONESDATALOCATE_MAX_ATTEMPTS 16

#define JONESDATALOCATE_NEEDS_PREFIX \
    "Jones3D EX needs the game data from your copy of Indiana Jones and the Infernal Machine " \
    "(GOG, Steam or CD): Jones3D.GOB, CD1.GOB and CD2.GOB."
#define JONESDATALOCATE_CHOOSE_TEXT \
    " Choose the game's folder or the Resource folder inside it."
#define JONESDATALOCATE_NOT_FOUND_MESSAGE JONESDATALOCATE_NEEDS_PREFIX JONESDATALOCATE_CHOOSE_TEXT
#define JONESDATALOCATE_HINT "Start Jones3D EX with --data <folder>."
#define JONESDATALOCATE_HEADLESS_HINT "Start with --data <folder>."

typedef struct JonesDataLocateKnownFile
{
    const char* pName;
    bool bRequired;
} JonesDataLocateKnownFile;

// Added: the same known files JonesData checks, in report order. The three GOB archives are the
// only required ones.
static const JonesDataLocateKnownFile JonesDataLocate_aKnownFiles[JONESDATA_MAX_FILES] =
{
    { "Jones3D.GOB",    true  },
    { "CD1.GOB",        true  },
    { "CD2.GOB",        true  },
    { "jonesopn.snm",   false },
    { "jonesopn_3.snm", false },
    { "jonesopn_4.snm", false },
    { "jonesopn_5.snm", false },
    { "jonesopn_8.snm", false },
};

typedef struct JonesDataLocateEnvironmentStorage
{
    JonesDataLocateEnv env;
    JonesDataLocatePathList steam;
    char aUserData[JONESDATALOCATE_MAX_PATH];
} JonesDataLocateEnvironmentStorage;

static char JonesDataLocate_aHostExecutableDir[JONESDATALOCATE_MAX_PATH];
static char JonesDataLocate_aHostWorkingDir[JONESDATALOCATE_MAX_PATH];
static char JonesDataLocate_aHostHome[JONESDATALOCATE_MAX_PATH];
static char JonesDataLocate_aHostProgramFiles[JONESDATALOCATE_MAX_PATH];
static char JonesDataLocate_aHostProgramFilesX86[JONESDATALOCATE_MAX_PATH];

static const char* JonesDataLocate_pDataPath;
static JonesDataLocatePromptHandler JonesDataLocate_pfPromptHandler;
static void* JonesDataLocate_pPromptUserData;

static void JonesDataLocate_LogStatus(const char* pFormat, ...);
static bool JonesDataLocate_Join(char* pOut, size_t outSize, JonesDataLocatePlatform platform,
    const char* pDir, const char* pName);
static void JonesDataLocate_NormalizePath(char* pOut, size_t outSize, const char* pPath,
    JonesDataLocatePlatform platform);
static bool JonesDataLocate_PathsEqual(const char* pPath1, const char* pPath2,
    JonesDataLocatePlatform platform);
static bool JonesDataLocate_IsDirectory(const char* pPath);
static bool JonesDataLocate_GetFileSize(const char* pPath, int64_t* pSize);
static bool JonesDataLocate_MakeDirs(const char* pPath);
static bool JonesDataLocate_FindKnownFileInDir(const char* pDir, const char* pName, char* pOutPath,
    size_t outSize);
static bool JonesDataLocate_HasAnyKnownFile(const char* pDir);
static bool JonesDataLocate_HasJones3D(const char* pDir);
static bool JonesDataLocate_FindAnyResourceDir(const char* pDir, char* pOutDir, size_t outSize);
static size_t JonesDataLocate_CollectMissingRequired(const char* pDir, char* pOut, size_t outSize);
static void JonesDataLocate_MakeInstallPath(const char* pCandidate, const char* pResourceDir,
    JonesDataLocatePlatform platform, char* pOut, size_t outSize);
static void JonesDataLocate_CompleteResult(const char* pCandidate, const char* pResourceDir,
    JonesDataLocateRule rule, JonesDataLocatePlatform platform, JonesDataLocateResult* pOut);
static void JonesDataLocate_BuildMissingMessage(const char* pDir, const char* pMissingFiles,
    char* pOut, size_t outSize);
static void JonesDataLocate_AddCandidate(JonesDataLocateCandidate* aOut, size_t maxOut,
    size_t* pNumOut, JonesDataLocatePlatform platform, const char* pPath,
    JonesDataLocateRule rule);
static void JonesDataLocate_AddSteamRoot(JonesDataLocatePathList* pOut,
    JonesDataLocatePlatform platform, const char* pRoot);
static void JonesDataLocate_AddSteamPath(JonesDataLocatePathList* pOut,
    JonesDataLocatePlatform platform, const char* pPath);
static void JonesDataLocate_AddDefaultSteamRoots(JonesDataLocatePathList* pOut,
    JonesDataLocatePlatform platform, const char* pHome, const char* pProgramFiles,
    const char* pProgramFilesX86);
static void JonesDataLocate_FillEnvironment(JonesDataLocateEnvironmentStorage* pStorage);
static bool JonesDataLocate_CopyFile(const char* pSource, const char* pDest);
static void JonesDataLocate_PrintReport(const JonesDataReport* pReport);

static void JonesDataLocate_LogStatus(const char* pFormat, ...)
{
    va_list args;
    char aText[1024];

    if ( !std_g_pHS )
    {
        return;
    }

    va_start(args, pFormat);
    vsnprintf(aText, sizeof(aText), pFormat, args);
    va_end(args);

    STDLOG_STATUS("%s", aText);
}

static bool JonesDataLocate_Join(char* pOut, size_t outSize, JonesDataLocatePlatform platform,
    const char* pDir, const char* pName)
{
    if ( !pOut || outSize == 0 || !pName )
    {
        return false;
    }

    const char chSeparator = platform == JONESDATALOCATE_PLATFORM_WINDOWS ? '\\' : '/';
    size_t dirLen = pDir ? strlen(pDir) : 0;
    bool bSlash   = dirLen > 0 && (pDir[dirLen - 1] == '/' || pDir[dirLen - 1] == '\\');
    size_t needed = dirLen + (bSlash || dirLen == 0 ? 0 : 1) + strlen(pName) + 1;
    if ( needed > outSize )
    {
        return false;
    }

    if ( dirLen > 0 )
    {
        memcpy(pOut, pDir, dirLen);
        if ( !bSlash )
        {
            pOut[dirLen++] = chSeparator;
        }
    }

    memcpy(&pOut[dirLen], pName, strlen(pName) + 1);
    return true;
}

// Rewrites separators to the host's style. Engine paths may mix them, and the direct file calls
// below do not resolve backslashes the way j3dcrt_fopen does.
static void JonesDataLocate_UsePlatformSeparators(char* pPath, JonesDataLocatePlatform platform)
{
    if ( !pPath )
    {
        return;
    }

    const char chSeparator = platform == JONESDATALOCATE_PLATFORM_WINDOWS ? '\\' : '/';
    for ( size_t i = 0; pPath[i] != '\0'; ++i )
    {
        if ( pPath[i] == '/' || pPath[i] == '\\' )
        {
            pPath[i] = chSeparator;
        }
    }
}

static void JonesDataLocate_NormalizePath(char* pOut, size_t outSize, const char* pPath,
    JonesDataLocatePlatform platform)
{
    if ( !pOut || outSize == 0 )
    {
        return;
    }

    size_t numOut = 0;
    if ( pPath )
    {
        for ( size_t i = 0; pPath[i] && numOut + 1 < outSize; ++i )
        {
            char ch = pPath[i];
            if ( ch == '\\' )
            {
                ch = '/';
            }

            if ( platform == JONESDATALOCATE_PLATFORM_WINDOWS
                && ch >= 'A' && ch <= 'Z' )
            {
                ch = (char)(ch - 'A' + 'a');
            }

            pOut[numOut++] = ch;
        }
    }

    while ( numOut > 1 && pOut[numOut - 1] == '/' )
    {
        --numOut;
    }

    pOut[numOut] = '\0';
}

static bool JonesDataLocate_PathsEqual(const char* pPath1, const char* pPath2,
    JonesDataLocatePlatform platform)
{
    char aPath1[JONESDATALOCATE_MAX_PATH];
    char aPath2[JONESDATALOCATE_MAX_PATH];

    JonesDataLocate_NormalizePath(aPath1, sizeof(aPath1), pPath1, platform);
    JonesDataLocate_NormalizePath(aPath2, sizeof(aPath2), pPath2, platform);
    return strcmp(aPath1, aPath2) == 0;
}

static bool JonesDataLocate_IsDirectory(const char* pPath)
{
    if ( !pPath || pPath[0] == '\0' )
    {
        return false;
    }

    struct JONESDATALOCATE_STAT st;
    return JONESDATALOCATE_STAT(pPath, &st) == 0 && JONESDATALOCATE_STAT_ISDIR(st.st_mode);
}

static bool JonesDataLocate_GetFileSize(const char* pPath, int64_t* pSize)
{
    struct JONESDATALOCATE_STAT st;
    if ( !pPath || JONESDATALOCATE_STAT(pPath, &st) != 0 || !JONESDATALOCATE_STAT_ISREG(st.st_mode) )
    {
        return false;
    }

    if ( pSize )
    {
        *pSize = (int64_t)st.st_size;
    }

    return true;
}

static bool JonesDataLocate_MakeDirs(const char* pPath)
{
    if ( !pPath || pPath[0] == '\0' )
    {
        return false;
    }

    char aPath[JONESDATALOCATE_MAX_PATH];
    size_t len = strlen(pPath);
    if ( len + 1 > sizeof(aPath) )
    {
        return false;
    }

    memcpy(aPath, pPath, len + 1);
    while ( len > 1 && (aPath[len - 1] == '/' || aPath[len - 1] == '\\') )
    {
        aPath[--len] = '\0';
    }

    if ( JonesDataLocate_IsDirectory(aPath) )
    {
        return true;
    }

    for ( size_t i = 1; aPath[i] != '\0'; ++i )
    {
        if ( aPath[i] != '/' && aPath[i] != '\\' )
        {
            continue;
        }

        if ( i == 2 && aPath[1] == ':' )
        {
            continue;
        }

        char chSeparator = aPath[i];
        aPath[i] = '\0';
        if ( aPath[0] != '\0' && !JonesDataLocate_IsDirectory(aPath) )
        {
            JONESDATALOCATE_MKDIR(aPath);
        }
        aPath[i] = chSeparator;
    }

    if ( !JonesDataLocate_IsDirectory(aPath) )
    {
        JONESDATALOCATE_MKDIR(aPath);
    }

    return JonesDataLocate_IsDirectory(aPath);
}

static void JonesDataLocate_MakeCaseVariant(char* pOut, const char* pName, bool bUpper)
{
    size_t i = 0;
    for ( ; pName[i] && i + 1 < JONESDATALOCATE_MAX_NAME; ++i )
    {
        char ch = pName[i];
        if ( ch >= 'a' && ch <= 'z' && bUpper )
        {
            ch = (char)(ch - 'a' + 'A');
        }
        else if ( ch >= 'A' && ch <= 'Z' && !bUpper )
        {
            ch = (char)(ch - 'A' + 'a');
        }

        pOut[i] = ch;
    }

    pOut[i] = '\0';
}

static bool JonesDataLocate_TryKnownFile(const char* pDir, const char* pName, char* pOutPath,
    size_t outSize)
{
    char aPath[JONESDATALOCATE_MAX_PATH];

    if ( !JonesDataLocate_Join(aPath, sizeof(aPath), JONESDATALOCATE_PLATFORM_LINUX, pDir, pName) )
    {
        return false;
    }

    if ( !JonesDataLocate_GetFileSize(aPath, NULL) )
    {
        return false;
    }

    if ( outSize > 0 )
    {
        size_t len = strlen(aPath);
        if ( len + 1 > outSize )
        {
            return false;
        }

        memcpy(pOutPath, aPath, len + 1);
    }

    return true;
}

static bool JonesDataLocate_FindKnownFileInDir(const char* pDir, const char* pName, char* pOutPath,
    size_t outSize)
{
    char aVariant[JONESDATALOCATE_MAX_NAME];

    if ( JonesDataLocate_TryKnownFile(pDir, pName, pOutPath, outSize) )
    {
        return true;
    }

    JonesDataLocate_MakeCaseVariant(aVariant, pName, false);
    if ( strcmp(aVariant, pName) != 0 && JonesDataLocate_TryKnownFile(pDir, aVariant, pOutPath, outSize) )
    {
        return true;
    }

    JonesDataLocate_MakeCaseVariant(aVariant, pName, true);
    if ( strcmp(aVariant, pName) != 0 && JonesDataLocate_TryKnownFile(pDir, aVariant, pOutPath, outSize) )
    {
        return true;
    }

    return false;
}

static bool JonesDataLocate_HasAnyKnownFile(const char* pDir)
{
    for ( size_t i = 0; i < STD_ARRAYLEN(JonesDataLocate_aKnownFiles); ++i )
    {
        if ( JonesDataLocate_FindKnownFileInDir(pDir, JonesDataLocate_aKnownFiles[i].pName, NULL, 0) )
        {
            return true;
        }
    }

    return false;
}

static bool JonesDataLocate_HasJones3D(const char* pDir)
{
    return JonesDataLocate_FindKnownFileInDir(pDir, "Jones3D.GOB", NULL, 0);
}

static bool JonesDataLocate_FindAnyResourceDir(const char* pDir, char* pOutDir, size_t outSize)
{
    if ( pOutDir && outSize > 0 )
    {
        pOutDir[0] = '\0';
    }

    if ( !pDir || pDir[0] == '\0' || !pOutDir || outSize == 0 )
    {
        return false;
    }

    char aResourceDir[JONESDATALOCATE_MAX_PATH];
    if ( JonesData_FindResourceDir(pDir, aResourceDir, sizeof(aResourceDir)) )
    {
        size_t len = strlen(aResourceDir);
        if ( len + 1 > outSize )
        {
            return false;
        }

        memcpy(pOutDir, aResourceDir, len + 1);
        return true;
    }

    static const char* aSubDirs[] = { "Resource", "resource" };
    if ( JonesDataLocate_HasAnyKnownFile(pDir) )
    {
        size_t len = strlen(pDir);
        if ( len + 1 > outSize )
        {
            return false;
        }

        memcpy(pOutDir, pDir, len + 1);
        return true;
    }

    for ( size_t i = 0; i < STD_ARRAYLEN(aSubDirs); ++i )
    {
        char aSubDir[JONESDATALOCATE_MAX_PATH];
        if ( !JonesDataLocate_Join(aSubDir, sizeof(aSubDir), JONESDATALOCATE_PLATFORM_LINUX, pDir, aSubDirs[i]) )
        {
            continue;
        }

        if ( JonesDataLocate_HasAnyKnownFile(aSubDir) )
        {
            size_t len = strlen(aSubDir);
            if ( len + 1 > outSize )
            {
                return false;
            }

            memcpy(pOutDir, aSubDir, len + 1);
            return true;
        }
    }

    return false;
}

static size_t JonesDataLocate_CollectMissingRequired(const char* pDir, char* pOut, size_t outSize)
{
    size_t numMissing = 0;
    size_t len = 0;

    if ( pOut && outSize > 0 )
    {
        pOut[0] = '\0';
    }

    for ( size_t i = 0; i < STD_ARRAYLEN(JonesDataLocate_aKnownFiles); ++i )
    {
        const JonesDataLocateKnownFile* pFile = &JonesDataLocate_aKnownFiles[i];
        if ( !pFile->bRequired || JonesDataLocate_FindKnownFileInDir(pDir, pFile->pName, NULL, 0) )
        {
            continue;
        }

        ++numMissing;
        if ( pOut && outSize > 0 )
        {
            const char* pSeparator = len == 0 ? "" : ", ";
            size_t needed = len + strlen(pSeparator) + strlen(pFile->pName) + 1;
            if ( needed <= outSize )
            {
                memcpy(&pOut[len], pSeparator, strlen(pSeparator) + 1);
                len += strlen(pSeparator);
                memcpy(&pOut[len], pFile->pName, strlen(pFile->pName) + 1);
                len += strlen(pFile->pName);
            }
        }
    }

    return numMissing;
}

static void JonesDataLocate_MakeInstallPath(const char* pCandidate, const char* pResourceDir,
    JonesDataLocatePlatform platform, char* pOut, size_t outSize)
{
    char aUpperDir[JONESDATALOCATE_MAX_PATH];
    bool bUpper = JonesDataLocate_Join(aUpperDir, sizeof(aUpperDir), platform, pCandidate, "Resource")
        && JonesDataLocate_PathsEqual(aUpperDir, pResourceDir, platform);

    const char* pInstall = (strcmp(pCandidate, pResourceDir) == 0 || bUpper) ? pCandidate : pResourceDir;
    size_t len = strlen(pInstall);
    if ( len + 1 > outSize )
    {
        len = outSize - 1;
    }

    memcpy(pOut, pInstall, len);
    pOut[len] = '\0';
}

static void JonesDataLocate_CompleteResult(const char* pCandidate, const char* pResourceDir,
    JonesDataLocateRule rule, JonesDataLocatePlatform platform, JonesDataLocateResult* pOut)
{
    memset(pOut, 0, sizeof(*pOut));
    pOut->bFound = true;
    pOut->rule   = rule;
    JonesDataLocate_MakeInstallPath(pCandidate, pResourceDir, platform, pOut->aInstallPath,
        sizeof(pOut->aInstallPath));

    size_t len = strlen(pResourceDir);
    if ( len + 1 > sizeof(pOut->aResourceDir) )
    {
        len = sizeof(pOut->aResourceDir) - 1;
    }

    memcpy(pOut->aResourceDir, pResourceDir, len);
    pOut->aResourceDir[len] = '\0';
}

static void JonesDataLocate_BuildMissingMessage(const char* pDir, const char* pMissingFiles,
    char* pOut, size_t outSize)
{
    if ( !pOut || outSize == 0 )
    {
        return;
    }

    if ( pMissingFiles && pMissingFiles[0] )
    {
        snprintf(pOut, outSize, "%s The folder '%s' is missing: %s.%s",
            JONESDATALOCATE_NEEDS_PREFIX, pDir ? pDir : "", pMissingFiles,
            JONESDATALOCATE_CHOOSE_TEXT);
    }
    else
    {
        snprintf(pOut, outSize, "%s", JONESDATALOCATE_NOT_FOUND_MESSAGE);
    }
}

static void JonesDataLocate_AddCandidate(JonesDataLocateCandidate* aOut, size_t maxOut,
    size_t* pNumOut, JonesDataLocatePlatform platform, const char* pPath,
    JonesDataLocateRule rule)
{
    if ( !aOut || !pNumOut || *pNumOut >= maxOut || !pPath || pPath[0] == '\0' )
    {
        return;
    }

    size_t len = strlen(pPath);
    if ( len + 1 > sizeof(aOut[*pNumOut].aPath) )
    {
        return;
    }

    memcpy(aOut[*pNumOut].aPath, pPath, len + 1);
    JonesDataLocate_UsePlatformSeparators(aOut[*pNumOut].aPath, platform);
    aOut[*pNumOut].rule = rule;
    ++*pNumOut;
}

const char* JonesDataLocate_RuleName(JonesDataLocateRule rule)
{
    switch ( rule )
    {
        case JONESDATALOCATE_RULE_DATA_OPTION:
            return "the --data option";

        case JONESDATALOCATE_RULE_INSTALL_PATH:
            return "installPath in Jones.cfg";

        case JONESDATALOCATE_RULE_USER_DATA:
            return "the user data directory";

        case JONESDATALOCATE_RULE_EXECUTABLE_DIR:
            return "the executable directory";

        case JONESDATALOCATE_RULE_WORKING_DIR:
            return "the working directory";

        case JONESDATALOCATE_RULE_STEAM:
            return "a Steam library";

        case JONESDATALOCATE_RULE_GOG:
            return "a GOG or store folder";

        case JONESDATALOCATE_RULE_CHOSEN:
            return "the folder chosen at startup";

        default:
            return "an unknown rule";
    }
}

size_t JonesDataLocate_BuildCandidates(const JonesDataLocateEnv* pEnv,
    JonesDataLocateCandidate* aOut, size_t maxOut)
{
    size_t numOut = 0;

    if ( !pEnv || !aOut )
    {
        return 0;
    }

    JonesDataLocate_AddCandidate(aOut, maxOut, &numOut, pEnv->platform, pEnv->pDataOption,
        JONESDATALOCATE_RULE_DATA_OPTION);
    JonesDataLocate_AddCandidate(aOut, maxOut, &numOut, pEnv->platform, pEnv->pInstallPath,
        JONESDATALOCATE_RULE_INSTALL_PATH);
    JonesDataLocate_AddCandidate(aOut, maxOut, &numOut, pEnv->platform, pEnv->pUserDataDir,
        JONESDATALOCATE_RULE_USER_DATA);
    JonesDataLocate_AddCandidate(aOut, maxOut, &numOut, pEnv->platform, pEnv->pExecutableDir,
        JONESDATALOCATE_RULE_EXECUTABLE_DIR);
    JonesDataLocate_AddCandidate(aOut, maxOut, &numOut, pEnv->platform, pEnv->pWorkingDir,
        JONESDATALOCATE_RULE_WORKING_DIR);

    if ( pEnv->pSteamLibraries )
    {
        for ( size_t i = 0; i < pEnv->pSteamLibraries->numPaths; ++i )
        {
            if ( pEnv->pSteamLibraries->aPaths[i][0] == '\0' )
            {
                continue;
            }

            char aSteamApps[JONESDATALOCATE_MAX_PATH];
            char aCommon[JONESDATALOCATE_MAX_PATH];
            char aGame[JONESDATALOCATE_MAX_PATH];

            if ( !JonesDataLocate_Join(aSteamApps, sizeof(aSteamApps), pEnv->platform,
                    pEnv->pSteamLibraries->aPaths[i], "steamapps")
              || !JonesDataLocate_Join(aCommon, sizeof(aCommon), pEnv->platform, aSteamApps, "common")
              || !JonesDataLocate_Join(aGame, sizeof(aGame), pEnv->platform, aCommon, JONESDATALOCATE_STEAM_NAME) )
            {
                continue;
            }

            JonesDataLocate_AddCandidate(aOut, maxOut, &numOut, pEnv->platform, aGame, JONESDATALOCATE_RULE_STEAM);
        }
    }

    if ( pEnv->platform == JONESDATALOCATE_PLATFORM_WINDOWS )
    {
        char aGog[JONESDATALOCATE_MAX_PATH];
        char aGogGame[JONESDATALOCATE_MAX_PATH];

        if ( JonesDataLocate_Join(aGog, sizeof(aGog), pEnv->platform, "C:\\GOG Games", JONESDATALOCATE_STEAM_NAME) )
        {
            JonesDataLocate_AddCandidate(aOut, maxOut, &numOut, pEnv->platform, aGog, JONESDATALOCATE_RULE_GOG);
        }

        if ( pEnv->pProgramFilesX86 && pEnv->pProgramFilesX86[0] != '\0'
          && JonesDataLocate_Join(aGog, sizeof(aGog), pEnv->platform, pEnv->pProgramFilesX86, "GOG Galaxy")
          && JonesDataLocate_Join(aGogGame, sizeof(aGogGame), pEnv->platform, aGog, "Games")
          && JonesDataLocate_Join(aGog, sizeof(aGog), pEnv->platform, aGogGame, JONESDATALOCATE_STEAM_NAME) )
        {
            JonesDataLocate_AddCandidate(aOut, maxOut, &numOut, pEnv->platform, aGog, JONESDATALOCATE_RULE_GOG);
        }
    }
    else if ( pEnv->pHome && pEnv->pHome[0] != '\0' )
    {
        char aDir[JONESDATALOCATE_MAX_PATH];
        char aGame[JONESDATALOCATE_MAX_PATH];

        if ( JonesDataLocate_Join(aDir, sizeof(aDir), pEnv->platform, pEnv->pHome, "GOG Games")
          && JonesDataLocate_Join(aGame, sizeof(aGame), pEnv->platform, aDir, JONESDATALOCATE_STEAM_NAME) )
        {
            JonesDataLocate_AddCandidate(aOut, maxOut, &numOut, pEnv->platform, aGame, JONESDATALOCATE_RULE_GOG);
        }

        if ( JonesDataLocate_Join(aDir, sizeof(aDir), pEnv->platform, pEnv->pHome, "Games")
          && JonesDataLocate_Join(aGame, sizeof(aGame), pEnv->platform, aDir, JONESDATALOCATE_STEAM_NAME) )
        {
            JonesDataLocate_AddCandidate(aOut, maxOut, &numOut, pEnv->platform, aGame, JONESDATALOCATE_RULE_GOG);
        }

        if ( JonesDataLocate_Join(aDir, sizeof(aDir), pEnv->platform, pEnv->pHome, "Games")
          && JonesDataLocate_Join(aGame, sizeof(aGame), pEnv->platform, aDir, "Heroic")
          && JonesDataLocate_Join(aDir, sizeof(aDir), pEnv->platform, aGame, JONESDATALOCATE_STEAM_NAME) )
        {
            JonesDataLocate_AddCandidate(aOut, maxOut, &numOut, pEnv->platform, aDir, JONESDATALOCATE_RULE_GOG);
        }
    }

    return numOut;
}

static bool JonesDataLocate_ParseQuoted(const char** ppCur, char* pOut, size_t outSize)
{
    const char* pCur = *ppCur;
    size_t numOut = 0;

    while ( *pCur == ' ' || *pCur == '\t' )
    {
        ++pCur;
    }

    if ( *pCur != '"' )
    {
        return false;
    }

    ++pCur;

    while ( *pCur && *pCur != '"' )
    {
        char ch = *pCur++;
        if ( ch == '\\' && (*pCur == '\\' || *pCur == '"') )
        {
            ch = *pCur++;
        }

        if ( numOut + 1 >= outSize )
        {
            return false;
        }

        pOut[numOut++] = ch;
    }

    if ( *pCur != '"' )
    {
        return false;
    }

    ++pCur;
    pOut[numOut] = '\0';
    *ppCur = pCur;
    return true;
}

void JonesDataLocate_ParseLibraryFolders(const char* pText, JonesDataLocatePathList* pOut)
{
    if ( !pOut )
    {
        return;
    }

    pOut->numPaths = 0;

    if ( !pText )
    {
        return;
    }

    const char* pCur = pText;
    while ( *pCur )
    {
        const char* pLineEnd = pCur;
        while ( *pLineEnd && *pLineEnd != '\n' && *pLineEnd != '\r' )
        {
            ++pLineEnd;
        }

        size_t lineLen = (size_t)(pLineEnd - pCur);
        if ( lineLen < JONESDATALOCATE_VDF_LINE_MAX )
        {
            char aLine[JONESDATALOCATE_VDF_LINE_MAX];
            memcpy(aLine, pCur, lineLen);
            aLine[lineLen] = '\0';

            const char* pLine = aLine;
            char aKey[JONESDATALOCATE_MAX_NAME];
            char aValue[JONESDATALOCATE_MAX_PATH];

            if ( JonesDataLocate_ParseQuoted(&pLine, aKey, sizeof(aKey)) && streqi(aKey, "path")
              && JonesDataLocate_ParseQuoted(&pLine, aValue, sizeof(aValue))
              && aValue[0] != '\0' && pOut->numPaths < JONESDATALOCATE_MAX_STEAM_LIBRARIES )
            {
                memcpy(pOut->aPaths[pOut->numPaths++], aValue, strlen(aValue) + 1);
            }
        }

        pCur = pLineEnd;
        while ( *pCur == '\r' || *pCur == '\n' )
        {
            ++pCur;
        }
    }
}

void JonesDataLocate_SetDataPath(const char* pPath)
{
    JonesDataLocate_pDataPath = pPath;
}

void JonesDataLocate_SetPromptHandler(JonesDataLocatePromptHandler pfHandler, void* pUserData)
{
    JonesDataLocate_pfPromptHandler = pfHandler;
    JonesDataLocate_pPromptUserData = pUserData;
}

void JonesDataLocate_Reset(void)
{
    JonesDataLocate_pDataPath       = NULL;
    JonesDataLocate_pfPromptHandler = NULL;
    JonesDataLocate_pPromptUserData = NULL;
}

bool JonesDataLocate_Search(const JonesDataLocateCandidate* aCandidates, size_t numCandidates,
    JonesDataLocateResult* pOut)
{
    if ( !pOut )
    {
        return false;
    }

    memset(pOut, 0, sizeof(*pOut));
    if ( !aCandidates )
    {
        return false;
    }

    for ( size_t i = 0; i < numCandidates; ++i )
    {
        const JonesDataLocateCandidate* pCandidate = &aCandidates[i];
        char aResourceDir[JONESDATALOCATE_MAX_PATH];

        if ( !JonesData_FindResourceDir(pCandidate->aPath, aResourceDir, sizeof(aResourceDir)) )
        {
            continue;
        }

        char aMissing[256];
        size_t numMissing = JonesDataLocate_CollectMissingRequired(aResourceDir, aMissing, sizeof(aMissing));
        if ( numMissing == 0 )
        {
            JonesDataLocate_CompleteResult(pCandidate->aPath, aResourceDir, pCandidate->rule,
                JONESDATALOCATE_PLATFORM_LINUX, pOut);
            return true;
        }

        if ( !pOut->bIncomplete && JonesDataLocate_HasJones3D(aResourceDir) )
        {
            pOut->bIncomplete = true;
            STD_STRCPY(pOut->aIncompleteDir, pCandidate->aPath);
            STD_STRCPY(pOut->aMissingFiles, aMissing);
        }
    }

    return false;
}

static bool JonesDataLocate_ValidateChosen(const char* pDir, JonesDataLocateRule rule,
    JonesDataLocatePlatform platform, JonesDataLocateResult* pOut)
{
    char aResourceDir[JONESDATALOCATE_MAX_PATH];

    if ( !JonesDataLocate_FindAnyResourceDir(pDir, aResourceDir, sizeof(aResourceDir)) )
    {
        return false;
    }

    if ( JonesDataLocate_CollectMissingRequired(aResourceDir, NULL, 0) != 0 )
    {
        return false;
    }

    JonesDataLocate_CompleteResult(pDir, aResourceDir, rule, platform, pOut);
    return true;
}

bool JonesDataLocate_Resolve(const JonesDataLocateEnv* pEnv, JonesDataLocateResult* pOut)
{
    if ( !pEnv || !pOut )
    {
        return false;
    }

    JonesDataLocateCandidate aCandidates[JONESDATALOCATE_MAX_CANDIDATES];
    size_t numCandidates = JonesDataLocate_BuildCandidates(pEnv, aCandidates,
        STD_ARRAYLEN(aCandidates));

    if ( JonesDataLocate_Search(aCandidates, numCandidates, pOut) )
    {
        JonesDataLocate_LogStatus("Game data: using '%s' (found by %s).\n",
            pOut->aInstallPath, JonesDataLocate_RuleName(pOut->rule));
        return true;
    }

    char aMessage[JONESDATALOCATE_MESSAGE_MAX];
    if ( pOut->bIncomplete )
    {
        JonesDataLocate_BuildMissingMessage(pOut->aIncompleteDir, pOut->aMissingFiles, aMessage,
            sizeof(aMessage));
    }
    else
    {
        snprintf(aMessage, sizeof(aMessage), "%s", JONESDATALOCATE_NOT_FOUND_MESSAGE);
    }

    if ( !JonesDataLocate_pfPromptHandler )
    {
        fprintf(stderr, "\nERROR: %s\n%s\n", aMessage, JONESDATALOCATE_HEADLESS_HINT);
        return false;
    }

    for ( size_t attempt = 0; attempt < JONESDATALOCATE_MAX_ATTEMPTS; ++attempt )
    {
        char aChosenDir[JONESDATALOCATE_MAX_PATH];
        if ( !JonesDataLocate_pfPromptHandler(aMessage, aChosenDir, sizeof(aChosenDir),
                JonesDataLocate_pPromptUserData) )
        {
            return false;
        }

        if ( JonesDataLocate_ValidateChosen(aChosenDir, JONESDATALOCATE_RULE_CHOSEN, pEnv->platform, pOut) )
        {
            if ( stdConfig_HasStarted() )
            {
                (void)stdConfig_SetString(SITH_CFG_INSTALLPATH, pOut->aInstallPath);
            }

            JonesDataLocate_LogStatus("Game data: using '%s' (found by %s).\n",
                pOut->aInstallPath, JonesDataLocate_RuleName(pOut->rule));
            return true;
        }

        char aResourceDir[JONESDATALOCATE_MAX_PATH];
        char aMissing[256] = { 0 };
        const char* pMissingDir = aChosenDir;
        if ( JonesDataLocate_FindAnyResourceDir(aChosenDir, aResourceDir, sizeof(aResourceDir)) )
        {
            pMissingDir = aResourceDir;
            JonesDataLocate_CollectMissingRequired(aResourceDir, aMissing, sizeof(aMissing));
        }
        else
        {
            JonesDataLocate_CollectMissingRequired(aChosenDir, aMissing, sizeof(aMissing));
        }

        JonesDataLocate_BuildMissingMessage(pMissingDir, aMissing, aMessage, sizeof(aMessage));
    }

    fprintf(stderr, "%s\n%s\n", aMessage, JONESDATALOCATE_HINT);
    return false;
}

static void JonesDataLocate_AddSteamPath(JonesDataLocatePathList* pOut,
    JonesDataLocatePlatform platform, const char* pPath)
{
    if ( !pOut || !pPath || pPath[0] == '\0' )
    {
        return;
    }

    for ( size_t i = 0; i < pOut->numPaths; ++i )
    {
        if ( JonesDataLocate_PathsEqual(pOut->aPaths[i], pPath, platform) )
        {
            return;
        }
    }

    size_t len = strlen(pPath);
    if ( pOut->numPaths >= JONESDATALOCATE_MAX_STEAM_LIBRARIES || len + 1 > JONESDATALOCATE_MAX_PATH )
    {
        return;
    }

    memcpy(pOut->aPaths[pOut->numPaths], pPath, len + 1);
    ++pOut->numPaths;
}

static char* JonesDataLocate_ReadTextFile(const char* pPath)
{
    FILE* pFile = NULL;
    if ( fopen_s(&pFile, pPath, "rb") != 0 )
    {
        return NULL;
    }

    if ( fseek(pFile, 0, SEEK_END) != 0 )
    {
        fclose(pFile);
        return NULL;
    }

    long size = ftell(pFile);
    if ( size < 0 || (unsigned long)size > JONESDATALOCATE_VDF_MAX_SIZE )
    {
        fclose(pFile);
        return NULL;
    }

    if ( fseek(pFile, 0, SEEK_SET) != 0 )
    {
        fclose(pFile);
        return NULL;
    }

    char* pText = (char*)STDMALLOC((size_t)size + 1);
    if ( !pText )
    {
        fclose(pFile);
        return NULL;
    }

    size_t numRead = fread(pText, 1, (size_t)size, pFile);
    fclose(pFile);

    if ( numRead != (size_t)size )
    {
        STDFREE(pText);
        return NULL;
    }

    pText[numRead] = '\0';
    return pText;
}

static void JonesDataLocate_AddSteamRoot(JonesDataLocatePathList* pOut,
    JonesDataLocatePlatform platform, const char* pRoot)
{
    if ( !JonesDataLocate_IsDirectory(pRoot) )
    {
        return;
    }

    JonesDataLocate_AddSteamPath(pOut, platform, pRoot);

    char aSteamApps[JONESDATALOCATE_MAX_PATH];
    char aVdfPath[JONESDATALOCATE_MAX_PATH];
    if ( !JonesDataLocate_Join(aSteamApps, sizeof(aSteamApps), platform, pRoot, "steamapps")
      || !JonesDataLocate_Join(aVdfPath, sizeof(aVdfPath), platform, aSteamApps, "libraryfolders.vdf") )
    {
        return;
    }

    char* pText = JonesDataLocate_ReadTextFile(aVdfPath);
    if ( !pText )
    {
        return;
    }

    JonesDataLocatePathList parsed;
    JonesDataLocate_ParseLibraryFolders(pText, &parsed);
    STDFREE(pText);

    for ( size_t i = 0; i < parsed.numPaths; ++i )
    {
        if ( JonesDataLocate_IsDirectory(parsed.aPaths[i]) )
        {
            JonesDataLocate_AddSteamPath(pOut, platform, parsed.aPaths[i]);
        }
    }
}

static void JonesDataLocate_AddDefaultSteamRoots(JonesDataLocatePathList* pOut,
    JonesDataLocatePlatform platform, const char* pHome, const char* pProgramFiles,
    const char* pProgramFilesX86)
{
    char aRoot[JONESDATALOCATE_MAX_PATH];
    char aSub[JONESDATALOCATE_MAX_PATH];

    if ( platform == JONESDATALOCATE_PLATFORM_WINDOWS )
    {
        if ( JonesDataLocate_Join(aRoot, sizeof(aRoot), platform, pProgramFilesX86, "Steam") )
        {
            JonesDataLocate_AddSteamRoot(pOut, platform, aRoot);
        }

        if ( JonesDataLocate_Join(aRoot, sizeof(aRoot), platform, pProgramFiles, "Steam") )
        {
            JonesDataLocate_AddSteamRoot(pOut, platform, aRoot);
        }

        return;
    }

    if ( JonesDataLocate_Join(aRoot, sizeof(aRoot), platform, pHome, ".steam") )
    {
        if ( JonesDataLocate_Join(aSub, sizeof(aSub), platform, aRoot, "steam") )
        {
            JonesDataLocate_AddSteamRoot(pOut, platform, aSub);
        }
    }

    if ( JonesDataLocate_Join(aRoot, sizeof(aRoot), platform, pHome, ".local") )
    {
        if ( JonesDataLocate_Join(aSub, sizeof(aSub), platform, aRoot, "share") )
        {
            if ( JonesDataLocate_Join(aRoot, sizeof(aRoot), platform, aSub, "Steam") )
            {
                JonesDataLocate_AddSteamRoot(pOut, platform, aRoot);
            }
        }
    }

    if ( JonesDataLocate_Join(aRoot, sizeof(aRoot), platform, pHome, ".var") )
    {
        if ( JonesDataLocate_Join(aSub, sizeof(aSub), platform, aRoot, "app") )
        {
            if ( JonesDataLocate_Join(aRoot, sizeof(aRoot), platform, aSub,
                    "com.valvesoftware.Steam/.local/share/Steam") )
            {
                JonesDataLocate_AddSteamRoot(pOut, platform, aRoot);
            }
        }
    }
}

static void JonesDataLocate_CopyEnvironmentString(char* pOut, size_t outSize, const char* pValue)
{
    if ( !pOut || outSize == 0 )
    {
        return;
    }

    pOut[0] = '\0';
    if ( !pValue )
    {
        return;
    }

    size_t len = strlen(pValue);
    if ( len + 1 > outSize )
    {
        return;
    }

    memcpy(pOut, pValue, len + 1);
}

void JonesDataLocate_SetHostDirs(const char* pExecutableDir, const char* pWorkingDir)
{
    JonesDataLocate_CopyEnvironmentString(JonesDataLocate_aHostExecutableDir,
        sizeof(JonesDataLocate_aHostExecutableDir), pExecutableDir);
    JonesDataLocate_CopyEnvironmentString(JonesDataLocate_aHostWorkingDir,
        sizeof(JonesDataLocate_aHostWorkingDir), pWorkingDir);
}

void JonesDataLocate_SetHostEnvironment(const char* pHome, const char* pProgramFiles,
    const char* pProgramFilesX86)
{
    JonesDataLocate_CopyEnvironmentString(JonesDataLocate_aHostHome,
        sizeof(JonesDataLocate_aHostHome), pHome);
    JonesDataLocate_CopyEnvironmentString(JonesDataLocate_aHostProgramFiles,
        sizeof(JonesDataLocate_aHostProgramFiles), pProgramFiles);
    JonesDataLocate_CopyEnvironmentString(JonesDataLocate_aHostProgramFilesX86,
        sizeof(JonesDataLocate_aHostProgramFilesX86), pProgramFilesX86);
}

static void JonesDataLocate_FillEnvironment(JonesDataLocateEnvironmentStorage* pStorage)
{
    memset(pStorage, 0, sizeof(*pStorage));

    JonesDataLocatePlatform platform = JONESDATALOCATE_PLATFORM_LINUX;
#ifdef _WIN32
    platform = JONESDATALOCATE_PLATFORM_WINDOWS;
#endif

    if ( !stdUserDir_MakePath(pStorage->aUserData, sizeof(pStorage->aUserData), "data") )
    {
        pStorage->aUserData[0] = '\0';
    }

    JonesDataLocate_AddDefaultSteamRoots(&pStorage->steam, platform,
        JonesDataLocate_aHostHome, JonesDataLocate_aHostProgramFiles,
        JonesDataLocate_aHostProgramFilesX86);

    pStorage->env.platform         = platform;
    pStorage->env.pDataOption      = JonesDataLocate_pDataPath;
    pStorage->env.pInstallPath     = "";
    pStorage->env.pUserDataDir     = pStorage->aUserData;
    pStorage->env.pExecutableDir   = JonesDataLocate_aHostExecutableDir;
    pStorage->env.pWorkingDir      = JonesDataLocate_aHostWorkingDir;
    pStorage->env.pHome            = JonesDataLocate_aHostHome;
    pStorage->env.pProgramFiles    = JonesDataLocate_aHostProgramFiles;
    pStorage->env.pProgramFilesX86 = JonesDataLocate_aHostProgramFilesX86;
    pStorage->env.pSteamLibraries  = &pStorage->steam;
}

bool JonesDataLocate_Startup(const char* pConfigInstallPath, JonesDataLocateResult* pOut)
{
    if ( !pOut )
    {
        return false;
    }

    JonesDataLocateEnvironmentStorage storage;
    JonesDataLocate_FillEnvironment(&storage);
    storage.env.pInstallPath = pConfigInstallPath ? pConfigInstallPath : "";
    return JonesDataLocate_Resolve(&storage.env, pOut);
}

int JonesDataLocate_HashData(const char* pDir)
{
    char aResourceDir[JONESDATALOCATE_MAX_PATH];
    if ( !pDir || pDir[0] == '\0'
      || !JonesData_FindResourceDir(pDir, aResourceDir, sizeof(aResourceDir)) )
    {
        fprintf(stderr, "ERROR: no game data found in '%s'.\n", pDir ? pDir : "");
        return 1;
    }

    if ( !JonesData_PrintHashRows(aResourceDir, stdout) )
    {
        fprintf(stderr, "ERROR: could not read the game data in '%s'.\n", aResourceDir);
        return 1;
    }

    return 0;
}

static bool JonesDataLocate_CopyFile(const char* pSource, const char* pDest)
{
    FILE* pIn = NULL;
    if ( fopen_s(&pIn, pSource, "rb") != 0 )
    {
        fprintf(stderr, "ERROR: cannot open '%s'.\n", pSource);
        return false;
    }

    FILE* pOut = NULL;
    if ( fopen_s(&pOut, pDest, "wb") != 0 )
    {
        fprintf(stderr, "ERROR: cannot create '%s'.\n", pDest);
        fclose(pIn);
        return false;
    }

    bool bOk = true;
    char aBuffer[JONESDATALOCATE_COPY_SIZE];
    size_t numRead;
    while ( (numRead = fread(aBuffer, 1, sizeof(aBuffer), pIn)) > 0 )
    {
        if ( fwrite(aBuffer, 1, numRead, pOut) != numRead )
        {
            bOk = false;
            break;
        }
    }

    if ( ferror(pIn) )
    {
        bOk = false;
    }

    if ( fclose(pOut) != 0 )
    {
        bOk = false;
    }

    fclose(pIn);

    if ( !bOk )
    {
        remove(pDest);
    }

    return bOk;
}

static void JonesDataLocate_PrintReport(const JonesDataReport* pReport)
{
    if ( !pReport )
    {
        return;
    }

    if ( pReport->pRelease )
    {
        printf("Game data: known release '%s'.\n", pReport->pRelease);
        return;
    }

    if ( !pReport->bComplete )
    {
        for ( size_t i = 0; i < pReport->numFiles; ++i )
        {
            const JonesDataFile* pFile = &pReport->aFiles[i];
            if ( pFile->bRequired && !pFile->bPresent )
            {
                fprintf(stderr, "Game data: missing required file '%s'.\n", pFile->pName);
            }
        }

        return;
    }

    for ( size_t i = 0; i < pReport->numFiles; ++i )
    {
        const JonesDataFile* pFile = &pReport->aFiles[i];
        if ( pFile->bPresent && !pFile->bMatchesRelease )
        {
            fprintf(stderr, "Game data: '%s' does not match a known release.\n", pFile->pName);
        }
    }
}

int JonesDataLocate_InstallDataTo(const char* pSourceDir, const char* pTargetDataDir)
{
    JonesDataLocatePlatform platform = JONESDATALOCATE_PLATFORM_LINUX;
#ifdef _WIN32
    platform = JONESDATALOCATE_PLATFORM_WINDOWS;
#endif

    if ( !pSourceDir || pSourceDir[0] == '\0' || !pTargetDataDir || pTargetDataDir[0] == '\0' )
    {
        fprintf(stderr, "ERROR: --install-data needs a source and a target folder.\n");
        return 1;
    }

    char aSourceDir[JONESDATALOCATE_MAX_PATH];
    size_t sourceLen = strlen(pSourceDir);
    if ( sourceLen + 1 > sizeof(aSourceDir) )
    {
        fprintf(stderr, "ERROR: the source path is too long.\n");
        return 1;
    }

    memcpy(aSourceDir, pSourceDir, sourceLen + 1);
    JonesDataLocate_UsePlatformSeparators(aSourceDir, platform);

    char aSourceResource[JONESDATALOCATE_MAX_PATH];
    if ( !JonesDataLocate_FindAnyResourceDir(aSourceDir, aSourceResource, sizeof(aSourceResource)) )
    {
        fprintf(stderr, "ERROR: no game data found in '%s'.\n", pSourceDir);
        return 1;
    }

    char aTargetDataDir[JONESDATALOCATE_MAX_PATH];
    size_t targetLen = strlen(pTargetDataDir);
    if ( targetLen + 1 > sizeof(aTargetDataDir) )
    {
        fprintf(stderr, "ERROR: the target path is too long.\n");
        return 1;
    }

    memcpy(aTargetDataDir, pTargetDataDir, targetLen + 1);
    JonesDataLocate_UsePlatformSeparators(aTargetDataDir, platform);

    char aTargetResource[JONESDATALOCATE_MAX_PATH];
    if ( !JonesDataLocate_Join(aTargetResource, sizeof(aTargetResource), platform, aTargetDataDir,
            "Resource") )
    {
        fprintf(stderr, "ERROR: the target path is too long.\n");
        return 1;
    }

    if ( JonesDataLocate_PathsEqual(aSourceResource, aTargetResource, platform) )
    {
        fprintf(stderr, "ERROR: the source and the target are the same folder.\n");
        return 1;
    }

    if ( !JonesDataLocate_MakeDirs(aTargetResource) )
    {
        fprintf(stderr, "ERROR: cannot create '%s'.\n", aTargetResource);
        return 1;
    }

    for ( size_t i = 0; i < STD_ARRAYLEN(JonesDataLocate_aKnownFiles); ++i )
    {
        const JonesDataLocateKnownFile* pFile = &JonesDataLocate_aKnownFiles[i];
        char aSourcePath[JONESDATALOCATE_MAX_PATH];
        if ( !JonesDataLocate_FindKnownFileInDir(aSourceResource, pFile->pName, aSourcePath,
                sizeof(aSourcePath)) )
        {
            continue;
        }

        char aTargetPath[JONESDATALOCATE_MAX_PATH];
        if ( !JonesDataLocate_Join(aTargetPath, sizeof(aTargetPath), platform, aTargetResource,
                pFile->pName) )
        {
            fprintf(stderr, "ERROR: the target path for '%s' is too long.\n", pFile->pName);
            continue;
        }

        int64_t sourceSize;
        int64_t targetSize;
        if ( JonesDataLocate_GetFileSize(aSourcePath, &sourceSize)
          && JonesDataLocate_GetFileSize(aTargetPath, &targetSize)
          && sourceSize == targetSize )
        {
            printf("unchanged %s\n", pFile->pName);
            continue;
        }

        char aTempPath[JONESDATALOCATE_MAX_PATH + 8];
        int tempLen = snprintf(aTempPath, sizeof(aTempPath), "%s.tmp", aTargetPath);
        if ( tempLen <= 0 || (size_t)tempLen >= sizeof(aTempPath) )
        {
            fprintf(stderr, "ERROR: the temporary path for '%s' is too long.\n", pFile->pName);
            continue;
        }

        if ( !JonesDataLocate_CopyFile(aSourcePath, aTempPath) )
        {
            fprintf(stderr, "ERROR: could not copy '%s'.\n", aSourcePath);
            continue;
        }

        remove(aTargetPath);
        if ( rename(aTempPath, aTargetPath) != 0 )
        {
            remove(aTempPath);
            fprintf(stderr, "ERROR: could not rename '%s' to '%s'.\n", aTempPath, aTargetPath);
            continue;
        }

        printf("copied %s\n", pFile->pName);
    }

    JonesDataReport report;
    bool bComplete = JonesData_CheckDir(aTargetResource, NULL, &report);
    JonesDataLocate_PrintReport(&report);
    return bComplete ? 0 : 1;
}

int JonesDataLocate_InstallData(const char* pDir)
{
    char aTargetData[JONESDATALOCATE_MAX_PATH];
    if ( !stdUserDir_MakePath(aTargetData, sizeof(aTargetData), "data") )
    {
        fprintf(stderr, "ERROR: the user data path is too long.\n");
        return 1;
    }

    return JonesDataLocate_InstallDataTo(pDir, aTargetData);
}
