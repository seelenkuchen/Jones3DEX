// Added: finds the game data (ADR-0021, M8.4). The candidate list builder is pure and takes its
// environment as values, so both path styles can be tested without a real file system. Probing the
// file system stays separate. The setup wizard installs the data, so this module no longer searches
// stores or asks for a folder.
#include "JonesDataLocate.h"

#include <Jones3D/Main/JonesData.h>

#include <std/General/std.h>
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
#define JONESDATALOCATE_STAT _stat
#define JONESDATALOCATE_STAT_ISDIR(mode) (((mode) & _S_IFDIR) != 0)
#define JONESDATALOCATE_STAT_ISREG(mode) (((mode) & _S_IFREG) != 0)
#else
#define JONESDATALOCATE_MKDIR(path) mkdir((path), 0700)
#define JONESDATALOCATE_STAT stat
#define JONESDATALOCATE_STAT_ISDIR(mode) S_ISDIR(mode)
#define JONESDATALOCATE_STAT_ISREG(mode) S_ISREG(mode)
#endif

#define JONESDATALOCATE_MAX_NAME 64

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
    char aUserData[JONESDATALOCATE_MAX_PATH];
} JonesDataLocateEnvironmentStorage;

static char JonesDataLocate_aHostExecutableDir[JONESDATALOCATE_MAX_PATH];
static char JonesDataLocate_aHostWorkingDir[JONESDATALOCATE_MAX_PATH];

static const char* JonesDataLocate_pDataPath;

static void JonesDataLocate_LogStatus(const char* pFormat, ...);
static bool JonesDataLocate_Join(char* pOut, size_t outSize, JonesDataLocatePlatform platform,
    const char* pDir, const char* pName);
static void JonesDataLocate_NormalizePath(char* pOut, size_t outSize, const char* pPath,
    JonesDataLocatePlatform platform);
static bool JonesDataLocate_PathsEqual(const char* pPath1, const char* pPath2,
    JonesDataLocatePlatform platform);
static bool JonesDataLocate_GetFileSize(const char* pPath, int64_t* pSize);
static bool JonesDataLocate_FindKnownFileInDir(const char* pDir, const char* pName, char* pOutPath,
    size_t outSize);
static bool JonesDataLocate_HasJones3D(const char* pDir);
static size_t JonesDataLocate_CollectMissingRequired(const char* pDir, char* pOut, size_t outSize);
static void JonesDataLocate_MakeInstallPath(const char* pCandidate, const char* pResourceDir,
    JonesDataLocatePlatform platform, char* pOut, size_t outSize);
static void JonesDataLocate_CompleteResult(const char* pCandidate, const char* pResourceDir,
    JonesDataLocateRule rule, JonesDataLocatePlatform platform, JonesDataLocateResult* pOut);
static void JonesDataLocate_AddCandidate(JonesDataLocateCandidate* aOut, size_t maxOut,
    size_t* pNumOut, JonesDataLocatePlatform platform, const char* pPath,
    JonesDataLocateRule rule);
static void JonesDataLocate_FillEnvironment(JonesDataLocateEnvironmentStorage* pStorage);

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

static bool JonesDataLocate_HasJones3D(const char* pDir)
{
    return JonesDataLocate_FindKnownFileInDir(pDir, "Jones3D.GOB", NULL, 0);
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

    // Added: "next to the game" installs keep the data in GameData besides the executable.
    char aExecutableDataDir[JONESDATALOCATE_MAX_PATH];
    if ( pEnv->pExecutableDir && pEnv->pExecutableDir[0] != '\0'
      && JonesDataLocate_Join(aExecutableDataDir, sizeof(aExecutableDataDir), pEnv->platform,
            pEnv->pExecutableDir, JONESDATALOCATE_GAMEDATA_DIR) )
    {
        JonesDataLocate_AddCandidate(aOut, maxOut, &numOut, pEnv->platform, aExecutableDataDir,
            JONESDATALOCATE_RULE_EXECUTABLE_DIR);
    }

    JonesDataLocate_AddCandidate(aOut, maxOut, &numOut, pEnv->platform, pEnv->pWorkingDir,
        JONESDATALOCATE_RULE_WORKING_DIR);

    return numOut;
}

void JonesDataLocate_SetDataPath(const char* pPath)
{
    JonesDataLocate_pDataPath = pPath;
}

void JonesDataLocate_Reset(void)
{
    JonesDataLocate_pDataPath = NULL;
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

    fprintf(stderr, "Game data: not found.\n");
    return false;
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

static void JonesDataLocate_FillEnvironment(JonesDataLocateEnvironmentStorage* pStorage)
{
    memset(pStorage, 0, sizeof(*pStorage));

    JonesDataLocatePlatform platform = JONESDATALOCATE_PLATFORM_LINUX;
#ifdef _WIN32
    platform = JONESDATALOCATE_PLATFORM_WINDOWS;
#endif

    if ( !stdUserDir_MakePath(pStorage->aUserData, sizeof(pStorage->aUserData),
            JONESDATALOCATE_GAMEDATA_DIR) )
    {
        pStorage->aUserData[0] = '\0';
    }

    pStorage->env.platform       = platform;
    pStorage->env.pDataOption    = JonesDataLocate_pDataPath;
    pStorage->env.pInstallPath   = "";
    pStorage->env.pUserDataDir   = pStorage->aUserData;
    pStorage->env.pExecutableDir = JonesDataLocate_aHostExecutableDir;
    pStorage->env.pWorkingDir    = JonesDataLocate_aHostWorkingDir;
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
