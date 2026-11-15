#include <unity_fixture.h>

#include <Jones3D/Main/JonesData.h>
#include <Jones3D/Main/JonesDataLocate.h>

#include <std/General/std.h>
#include <std/General/stdUserDir.h>
#include <std/types.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#include <windows.h>
#define JONESDATALOCATE_TEST_MKDIR(path) _mkdir(path)
#define JONESDATALOCATE_TEST_RMDIR(path) _rmdir(path)
#define JONESDATALOCATE_TEST_GETPID _getpid
#else
#include <dirent.h>
#include <unistd.h>
#define JONESDATALOCATE_TEST_MKDIR(path) mkdir((path), 0700)
#define JONESDATALOCATE_TEST_RMDIR(path) rmdir(path)
#define JONESDATALOCATE_TEST_GETPID getpid
#endif

#define JONESDATALOCATE_TEST_MAX_NAME 512

static char g_aTempDir[JONESDATALOCATE_MAX_PATH];
static unsigned int g_tempDirCounter;

static tHostServices g_hostServices;
static bool g_bHostServices;

TEST_GROUP(JonesDataLocate);

static const char* JonesDataLocateTest_GetEnv(const char* pName)
{
    static char aValue[1024];
    size_t size = 0;

    aValue[0] = '\0';
    if ( getenv_s(&size, aValue, sizeof(aValue), pName) == 0 && size > 0 )
    {
        return aValue;
    }

    return NULL;
}

static const char* JonesDataLocateTest_TempBase(void)
{
#ifdef _WIN32
    static char aBase[JONESDATALOCATE_MAX_PATH];

    DWORD len = GetTempPathA((DWORD)sizeof(aBase), aBase);
    if ( len > 0 && (size_t)len < sizeof(aBase) )
    {
        while ( len > 1 && (aBase[len - 1] == '\\' || aBase[len - 1] == '/') )
        {
            aBase[--len] = '\0';
        }

        return aBase;
    }

    const char* pBase = JonesDataLocateTest_GetEnv("TEMP");
    return pBase && pBase[0] ? pBase : "C:\\Windows\\Temp";
#else
    const char* pBase = JonesDataLocateTest_GetEnv("TMPDIR");
    if ( !pBase || pBase[0] == '\0' )
    {
        pBase = JonesDataLocateTest_GetEnv("TEMP");
    }
    if ( !pBase || pBase[0] == '\0' )
    {
        pBase = JonesDataLocateTest_GetEnv("TMP");
    }
    if ( !pBase || pBase[0] == '\0' )
    {
        pBase = "/tmp";
    }

    return pBase;
#endif
}

static int JonesDataLocateTest_MakeTempDir(void)
{
    const char* pBase = JonesDataLocateTest_TempBase();

    for ( unsigned int attempt = 0; attempt < 1000; ++attempt )
    {
        int len = snprintf(g_aTempDir, sizeof(g_aTempDir), "%s/JonesDataLocateTest-%ld-%u",
            pBase, (long)JONESDATALOCATE_TEST_GETPID(), ++g_tempDirCounter);
        if ( len > 0 && (size_t)len < sizeof(g_aTempDir)
          && JONESDATALOCATE_TEST_MKDIR(g_aTempDir) == 0 )
        {
            return 0;
        }
    }

    g_aTempDir[0] = '\0';
    return -1;
}

static void JonesDataLocateTest_BuildPath(char* pOut, size_t outSize, const char* pDir,
    const char* pName)
{
    int len = snprintf(pOut, outSize, "%s/%s", pDir, pName);
    TEST_ASSERT_TRUE(len > 0 && (size_t)len < outSize);
}

static bool JonesDataLocateTest_IsDirectory(const char* pPath)
{
#ifdef _WIN32
    struct _stat st;
    return _stat(pPath, &st) == 0 && (st.st_mode & _S_IFDIR) != 0;
#else
    struct stat st;
    return stat(pPath, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static void JonesDataLocateTest_MakeDirs(const char* pPath)
{
    char aPath[JONESDATALOCATE_MAX_PATH];
    size_t len = strlen(pPath);
    TEST_ASSERT_TRUE(len + 1 < sizeof(aPath));
    memcpy(aPath, pPath, len + 1);

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

        char ch = aPath[i];
        aPath[i] = '\0';
        if ( aPath[0] != '\0' && !JonesDataLocateTest_IsDirectory(aPath) )
        {
            JONESDATALOCATE_TEST_MKDIR(aPath);
        }
        aPath[i] = ch;
    }

    if ( !JonesDataLocateTest_IsDirectory(aPath) )
    {
        JONESDATALOCATE_TEST_MKDIR(aPath);
    }

    TEST_ASSERT_TRUE(JonesDataLocateTest_IsDirectory(aPath));
}

static void JonesDataLocateTest_RemoveTree(const char* pDir)
{
    char aEntries[64][JONESDATALOCATE_TEST_MAX_NAME];
    size_t numEntries = 0;

#ifdef _WIN32
    char aPattern[JONESDATALOCATE_MAX_PATH];
    int len = snprintf(aPattern, sizeof(aPattern), "%s/*", pDir);
    if ( len <= 0 || (size_t)len >= sizeof(aPattern) )
    {
        return;
    }

    WIN32_FIND_DATAA data;
    HANDLE hFind = FindFirstFileA(aPattern, &data);
    if ( hFind == INVALID_HANDLE_VALUE )
    {
        return;
    }

    do
    {
        if ( strcmp(data.cFileName, ".") == 0 || strcmp(data.cFileName, "..") == 0 )
        {
            continue;
        }

        snprintf(aEntries[numEntries], sizeof(aEntries[numEntries]), "%s", data.cFileName);
        ++numEntries;
    } while ( numEntries < 64 && FindNextFileA(hFind, &data) );

    FindClose(hFind);
#else
    DIR* pDirHandle = opendir(pDir);
    if ( !pDirHandle )
    {
        return;
    }

    struct dirent* pEntry;
    while ( numEntries < 64 && (pEntry = readdir(pDirHandle)) != NULL )
    {
        if ( strcmp(pEntry->d_name, ".") == 0 || strcmp(pEntry->d_name, "..") == 0 )
        {
            continue;
        }

        snprintf(aEntries[numEntries], sizeof(aEntries[numEntries]), "%s", pEntry->d_name);
        ++numEntries;
    }

    closedir(pDirHandle);
#endif

    for ( size_t i = 0; i < numEntries; ++i )
    {
        char aPath[JONESDATALOCATE_MAX_PATH];
        int len = snprintf(aPath, sizeof(aPath), "%s/%s", pDir, aEntries[i]);
        if ( len <= 0 || (size_t)len >= sizeof(aPath) )
        {
            continue;
        }

        if ( JonesDataLocateTest_IsDirectory(aPath) )
        {
            JonesDataLocateTest_RemoveTree(aPath);
        }
        else
        {
            remove(aPath);
        }
    }

    JONESDATALOCATE_TEST_RMDIR(pDir);
}

static void JonesDataLocateTest_WriteFile(const char* pDir, const char* pName,
    const void* pData, size_t size)
{
    char aPath[JONESDATALOCATE_MAX_PATH];
    JonesDataLocateTest_BuildPath(aPath, sizeof(aPath), pDir, pName);

    FILE* pFile = NULL;
    TEST_ASSERT_EQUAL_INT(0, fopen_s(&pFile, aPath, "wb"));
    TEST_ASSERT_NOT_NULL(pFile);

    if ( size > 0 )
    {
        TEST_ASSERT_EQUAL_size_t(size, fwrite(pData, 1, size, pFile));
    }

    TEST_ASSERT_EQUAL_INT(0, fclose(pFile));
}

static void JonesDataLocateTest_WriteRequiredSet(const char* pDir)
{
    JonesDataLocateTest_WriteFile(pDir, "Jones3D.GOB", "jones", 5);
    JonesDataLocateTest_WriteFile(pDir, "CD1.GOB", "cd1", 3);
    JonesDataLocateTest_WriteFile(pDir, "CD2.GOB", "cd2", 3);
}

static bool JonesDataLocateTest_PathsEqual(const char* pPath1, const char* pPath2)
{
    char aPath1[JONESDATALOCATE_MAX_PATH];
    char aPath2[JONESDATALOCATE_MAX_PATH];

    snprintf(aPath1, sizeof(aPath1), "%s", pPath1);
    snprintf(aPath2, sizeof(aPath2), "%s", pPath2);

    for ( size_t i = 0; aPath1[i] != '\0'; ++i )
    {
        if ( aPath1[i] == '\\' )
        {
            aPath1[i] = '/';
        }
#ifdef _WIN32
        if ( aPath1[i] >= 'A' && aPath1[i] <= 'Z' )
        {
            aPath1[i] = (char)(aPath1[i] - 'A' + 'a');
        }
#endif
    }

    for ( size_t i = 0; aPath2[i] != '\0'; ++i )
    {
        if ( aPath2[i] == '\\' )
        {
            aPath2[i] = '/';
        }
#ifdef _WIN32
        if ( aPath2[i] >= 'A' && aPath2[i] <= 'Z' )
        {
            aPath2[i] = (char)(aPath2[i] - 'A' + 'a');
        }
#endif
    }

    return strcmp(aPath1, aPath2) == 0;
}

static void JonesDataLocateTest_SetEnv(JonesDataLocateEnv* pEnv, JonesDataLocatePlatform platform,
    const char* pDataOption, const char* pInstallPath, const char* pUserData,
    const char* pExecutableDir, const char* pWorkingDir)
{
    memset(pEnv, 0, sizeof(*pEnv));
    pEnv->platform       = platform;
    pEnv->pDataOption    = pDataOption;
    pEnv->pInstallPath   = pInstallPath;
    pEnv->pUserDataDir   = pUserData;
    pEnv->pExecutableDir = pExecutableDir;
    pEnv->pWorkingDir    = pWorkingDir;
}

static void* J3DAPI JonesDataLocateTest_Malloc(size_t size)
{
    return malloc(size);
}

static void J3DAPI JonesDataLocateTest_Free(void* pData)
{
    free(pData);
}

static void* J3DAPI JonesDataLocateTest_Realloc(void* pData, size_t size)
{
    return realloc(pData, size);
}

static int J3DAPI JonesDataLocateTest_Print(const char* pFormat, ...)
{
    va_list args;
    va_start(args, pFormat);
    int result = vfprintf(stderr, pFormat, args);
    va_end(args);
    return result;
}

static tFileHandle J3DAPI JonesDataLocateTest_FileOpen(const char* pFilename, const char* mode)
{
    FILE* pFile = NULL;
    if ( fopen_s(&pFile, pFilename, mode) != 0 )
    {
        return 0;
    }

    return (tFileHandle)(uintptr_t)pFile;
}

static int J3DAPI JonesDataLocateTest_FileClose(tFileHandle fh)
{
    return fclose((FILE*)(uintptr_t)fh);
}

static size_t J3DAPI JonesDataLocateTest_FileRead(tFileHandle fh, void* pDst, size_t size)
{
    return fread(pDst, 1, size, (FILE*)(uintptr_t)fh);
}

static char* J3DAPI JonesDataLocateTest_FileGets(tFileHandle fh, char* pStr, size_t size)
{
    return fgets(pStr, (int)size, (FILE*)(uintptr_t)fh);
}

static size_t J3DAPI JonesDataLocateTest_FileWrite(tFileHandle fh, const void* pData, size_t size)
{
    return fwrite(pData, 1, size, (FILE*)(uintptr_t)fh);
}

static int J3DAPI JonesDataLocateTest_FileEOF(tFileHandle fh)
{
    return feof((FILE*)(uintptr_t)fh);
}

static int J3DAPI JonesDataLocateTest_FileTell(tFileHandle fh)
{
    return (int)ftell((FILE*)(uintptr_t)fh);
}

static int J3DAPI JonesDataLocateTest_FileSeek(tFileHandle fh, int offset, int origin)
{
    return fseek((FILE*)(uintptr_t)fh, offset, origin);
}

static size_t J3DAPI JonesDataLocateTest_FileSize(const char* pFilename)
{
#ifdef _WIN32
    struct _stat64 st;
    if ( _stat64(pFilename, &st) != 0 )
    {
        return 0;
    }
#else
    struct stat st;
    if ( stat(pFilename, &st) != 0 )
    {
        return 0;
    }
#endif

    return (size_t)st.st_size;
}

static int JonesDataLocateTest_FilePrintf(tFileHandle fh, const char* pFormat, ...)
{
    va_list args;
    va_start(args, pFormat);
    int result = vfprintf((FILE*)(uintptr_t)fh, pFormat, args);
    va_end(args);
    return result;
}

static void JonesDataLocateTest_StartHostServices(void)
{
    memset(&g_hostServices, 0, sizeof(g_hostServices));
    g_hostServices.pStatusPrint  = JonesDataLocateTest_Print;
    g_hostServices.pMessagePrint = JonesDataLocateTest_Print;
    g_hostServices.pWarningPrint = JonesDataLocateTest_Print;
    g_hostServices.pErrorPrint   = JonesDataLocateTest_Print;
    g_hostServices.pDebugPrint   = JonesDataLocateTest_Print;
    g_hostServices.pMalloc       = JonesDataLocateTest_Malloc;
    g_hostServices.pFree         = JonesDataLocateTest_Free;
    g_hostServices.pRealloc      = JonesDataLocateTest_Realloc;
    g_hostServices.pFileOpen     = JonesDataLocateTest_FileOpen;
    g_hostServices.pFileClose    = JonesDataLocateTest_FileClose;
    g_hostServices.pFileRead     = JonesDataLocateTest_FileRead;
    g_hostServices.pFileGets     = JonesDataLocateTest_FileGets;
    g_hostServices.pFileWrite    = JonesDataLocateTest_FileWrite;
    g_hostServices.pFileEOF      = JonesDataLocateTest_FileEOF;
    g_hostServices.pFileTell     = JonesDataLocateTest_FileTell;
    g_hostServices.pFileSeek     = JonesDataLocateTest_FileSeek;
    g_hostServices.pFileSize     = JonesDataLocateTest_FileSize;
    g_hostServices.pFilePrintf   = JonesDataLocateTest_FilePrintf;

    std_g_pHS = &g_hostServices;
    g_bHostServices = true;
}

static void JonesDataLocateTest_StopHostServices(void)
{
    if ( g_bHostServices )
    {
        std_g_pHS = NULL;
        g_bHostServices = false;
    }
}

TEST_SETUP(JonesDataLocate)
{
    TEST_ASSERT_EQUAL_INT(0, JonesDataLocateTest_MakeTempDir());
    JonesDataLocateTest_StartHostServices();
    JonesDataLocate_Reset();
    JonesDataLocate_SetHostDirs(NULL, NULL);
    JonesData_SetReleaseTable(NULL, 0);
}

TEST_TEAR_DOWN(JonesDataLocate)
{
    JonesDataLocate_Reset();
    stdUserDir_Reset();
    JonesDataLocateTest_StopHostServices();
    if ( g_aTempDir[0] != '\0' )
    {
        JonesDataLocateTest_RemoveTree(g_aTempDir);
        g_aTempDir[0] = '\0';
    }
}

TEST(JonesDataLocate, TestLinuxCandidateOrderAndContent)
{
    JonesDataLocateEnv env;
    JonesDataLocateTest_SetEnv(&env, JONESDATALOCATE_PLATFORM_LINUX,
        "/opt/jones-data", "/games/infernal", "/home/player/.local/share/Jones3DEX/data",
        "/opt/jonesex/bin", "/work");

    JonesDataLocateCandidate aCandidates[JONESDATALOCATE_MAX_CANDIDATES];
    size_t numCandidates = JonesDataLocate_BuildCandidates(&env, aCandidates,
        JONESDATALOCATE_MAX_CANDIDATES);

    TEST_ASSERT_EQUAL_size_t(6, numCandidates);
    TEST_ASSERT_EQUAL_STRING("/opt/jones-data", aCandidates[0].aPath);
    TEST_ASSERT_EQUAL_INT(JONESDATALOCATE_RULE_DATA_OPTION, aCandidates[0].rule);
    TEST_ASSERT_EQUAL_STRING("/games/infernal", aCandidates[1].aPath);
    TEST_ASSERT_EQUAL_INT(JONESDATALOCATE_RULE_INSTALL_PATH, aCandidates[1].rule);
    TEST_ASSERT_EQUAL_STRING("/home/player/.local/share/Jones3DEX/data", aCandidates[2].aPath);
    TEST_ASSERT_EQUAL_INT(JONESDATALOCATE_RULE_USER_DATA, aCandidates[2].rule);
    TEST_ASSERT_EQUAL_STRING("/opt/jonesex/bin", aCandidates[3].aPath);
    TEST_ASSERT_EQUAL_INT(JONESDATALOCATE_RULE_EXECUTABLE_DIR, aCandidates[3].rule);
    TEST_ASSERT_EQUAL_STRING("/opt/jonesex/bin/GameData", aCandidates[4].aPath);
    TEST_ASSERT_EQUAL_INT(JONESDATALOCATE_RULE_EXECUTABLE_DIR, aCandidates[4].rule);
    TEST_ASSERT_EQUAL_STRING("/work", aCandidates[5].aPath);
    TEST_ASSERT_EQUAL_INT(JONESDATALOCATE_RULE_WORKING_DIR, aCandidates[5].rule);
}

TEST(JonesDataLocate, TestWindowsCandidateOrderAndContent)
{
    JonesDataLocateEnv env;
    JonesDataLocateTest_SetEnv(&env, JONESDATALOCATE_PLATFORM_WINDOWS,
        "D:\\Jones", "E:\\Games\\Jones", "C:\\Users\\Player\\AppData\\Roaming\\Jones3DEX\\data",
        "C:\\Games\\Jones3DEX", "C:\\Games\\Jones3DEX");

    JonesDataLocateCandidate aCandidates[JONESDATALOCATE_MAX_CANDIDATES];
    size_t numCandidates = JonesDataLocate_BuildCandidates(&env, aCandidates,
        JONESDATALOCATE_MAX_CANDIDATES);

    TEST_ASSERT_EQUAL_size_t(6, numCandidates);
    TEST_ASSERT_EQUAL_STRING("D:\\Jones", aCandidates[0].aPath);
    TEST_ASSERT_EQUAL_INT(JONESDATALOCATE_RULE_DATA_OPTION, aCandidates[0].rule);
    TEST_ASSERT_EQUAL_STRING("E:\\Games\\Jones", aCandidates[1].aPath);
    TEST_ASSERT_EQUAL_INT(JONESDATALOCATE_RULE_INSTALL_PATH, aCandidates[1].rule);
    TEST_ASSERT_EQUAL_STRING("C:\\Users\\Player\\AppData\\Roaming\\Jones3DEX\\data",
        aCandidates[2].aPath);
    TEST_ASSERT_EQUAL_INT(JONESDATALOCATE_RULE_USER_DATA, aCandidates[2].rule);
    TEST_ASSERT_EQUAL_STRING("C:\\Games\\Jones3DEX", aCandidates[3].aPath);
    TEST_ASSERT_EQUAL_INT(JONESDATALOCATE_RULE_EXECUTABLE_DIR, aCandidates[3].rule);
    TEST_ASSERT_EQUAL_STRING("C:\\Games\\Jones3DEX\\GameData", aCandidates[4].aPath);
    TEST_ASSERT_EQUAL_INT(JONESDATALOCATE_RULE_EXECUTABLE_DIR, aCandidates[4].rule);
    TEST_ASSERT_EQUAL_STRING("C:\\Games\\Jones3DEX", aCandidates[5].aPath);
    TEST_ASSERT_EQUAL_INT(JONESDATALOCATE_RULE_WORKING_DIR, aCandidates[5].rule);
}

TEST(JonesDataLocate, TestDataOptionBeatsInstallPath)
{
    char aDataDir[JONESDATALOCATE_MAX_PATH];
    char aInstallDir[JONESDATALOCATE_MAX_PATH];
    JonesDataLocateTest_BuildPath(aDataDir, sizeof(aDataDir), g_aTempDir, "data-option");
    JonesDataLocateTest_BuildPath(aInstallDir, sizeof(aInstallDir), g_aTempDir, "install-path");
    JonesDataLocateTest_MakeDirs(aDataDir);
    JonesDataLocateTest_MakeDirs(aInstallDir);
    JonesDataLocateTest_WriteRequiredSet(aDataDir);
    JonesDataLocateTest_WriteRequiredSet(aInstallDir);

    JonesDataLocateEnv env;
    JonesDataLocateTest_SetEnv(&env, JONESDATALOCATE_PLATFORM_LINUX,
        aDataDir, aInstallDir, NULL, NULL, NULL);

    JonesDataLocateResult result;
    TEST_ASSERT_TRUE(JonesDataLocate_Resolve(&env, &result));
    TEST_ASSERT_TRUE(JonesDataLocateTest_PathsEqual(aDataDir, result.aInstallPath));
    TEST_ASSERT_EQUAL_INT(JONESDATALOCATE_RULE_DATA_OPTION, result.rule);
}

TEST(JonesDataLocate, TestSearchSkipsIncompleteCandidate)
{
    char aIncomplete[JONESDATALOCATE_MAX_PATH];
    char aComplete[JONESDATALOCATE_MAX_PATH];
    JonesDataLocateTest_BuildPath(aIncomplete, sizeof(aIncomplete), g_aTempDir, "incomplete");
    JonesDataLocateTest_BuildPath(aComplete, sizeof(aComplete), g_aTempDir, "complete");
    JonesDataLocateTest_MakeDirs(aIncomplete);
    JonesDataLocateTest_MakeDirs(aComplete);
    JonesDataLocateTest_WriteFile(aIncomplete, "Jones3D.GOB", "jones", 5);
    JonesDataLocateTest_WriteRequiredSet(aComplete);

    JonesDataLocateEnv env;
    JonesDataLocateTest_SetEnv(&env, JONESDATALOCATE_PLATFORM_LINUX,
        aIncomplete, aComplete, NULL, NULL, NULL);

    JonesDataLocateResult result;
    TEST_ASSERT_TRUE(JonesDataLocate_Resolve(&env, &result));
    TEST_ASSERT_TRUE(JonesDataLocateTest_PathsEqual(aComplete, result.aInstallPath));
    TEST_ASSERT_EQUAL_INT(JONESDATALOCATE_RULE_INSTALL_PATH, result.rule);
}

TEST(JonesDataLocate, TestStartupFindsUserDataRule)
{
    char aUserDir[JONESDATALOCATE_MAX_PATH];
    char aDataDir[JONESDATALOCATE_MAX_PATH];
    char aResourceDir[JONESDATALOCATE_MAX_PATH];
    JonesDataLocateTest_BuildPath(aUserDir, sizeof(aUserDir), g_aTempDir, "user");
    JonesDataLocateTest_BuildPath(aDataDir, sizeof(aDataDir), aUserDir,
        JONESDATALOCATE_GAMEDATA_DIR);
    JonesDataLocateTest_BuildPath(aResourceDir, sizeof(aResourceDir), aDataDir, "Resource");
    JonesDataLocateTest_MakeDirs(aResourceDir);
    JonesDataLocateTest_WriteRequiredSet(aResourceDir);

    TEST_ASSERT_TRUE(stdUserDir_Set(aUserDir));

    JonesDataLocateResult result;
    TEST_ASSERT_TRUE(JonesDataLocate_Startup(NULL, &result));
    TEST_ASSERT_TRUE(result.bFound);
    TEST_ASSERT_EQUAL_INT(JONESDATALOCATE_RULE_USER_DATA, result.rule);
    TEST_ASSERT_TRUE(JonesDataLocateTest_PathsEqual(aDataDir, result.aInstallPath));
    TEST_ASSERT_TRUE(JonesDataLocateTest_PathsEqual(aResourceDir, result.aResourceDir));
}

TEST(JonesDataLocate, TestStartupFindsExecutableGameDataRule)
{
    char aUserDir[JONESDATALOCATE_MAX_PATH];
    char aExecutableDir[JONESDATALOCATE_MAX_PATH];
    char aDataDir[JONESDATALOCATE_MAX_PATH];
    char aResourceDir[JONESDATALOCATE_MAX_PATH];
    JonesDataLocateTest_BuildPath(aUserDir, sizeof(aUserDir), g_aTempDir, "user");
    JonesDataLocateTest_BuildPath(aExecutableDir, sizeof(aExecutableDir), g_aTempDir, "exe");
    JonesDataLocateTest_BuildPath(aDataDir, sizeof(aDataDir), aExecutableDir,
        JONESDATALOCATE_GAMEDATA_DIR);
    JonesDataLocateTest_BuildPath(aResourceDir, sizeof(aResourceDir), aDataDir, "Resource");
    JonesDataLocateTest_MakeDirs(aUserDir);
    JonesDataLocateTest_MakeDirs(aResourceDir);
    JonesDataLocateTest_WriteRequiredSet(aResourceDir);

    TEST_ASSERT_TRUE(stdUserDir_Set(aUserDir));
    JonesDataLocate_SetHostDirs(aExecutableDir, NULL);

    JonesDataLocateResult result;
    TEST_ASSERT_TRUE(JonesDataLocate_Startup(NULL, &result));
    TEST_ASSERT_TRUE(result.bFound);
    TEST_ASSERT_EQUAL_INT(JONESDATALOCATE_RULE_EXECUTABLE_DIR, result.rule);
    TEST_ASSERT_TRUE(JonesDataLocateTest_PathsEqual(aDataDir, result.aInstallPath));
    TEST_ASSERT_TRUE(JonesDataLocateTest_PathsEqual(aResourceDir, result.aResourceDir));
}

TEST_GROUP_RUNNER(JonesDataLocate)
{
    RUN_TEST_CASE(JonesDataLocate, TestLinuxCandidateOrderAndContent);
    RUN_TEST_CASE(JonesDataLocate, TestWindowsCandidateOrderAndContent);
    RUN_TEST_CASE(JonesDataLocate, TestDataOptionBeatsInstallPath);
    RUN_TEST_CASE(JonesDataLocate, TestSearchSkipsIncompleteCandidate);
    RUN_TEST_CASE(JonesDataLocate, TestStartupFindsUserDataRule);
    RUN_TEST_CASE(JonesDataLocate, TestStartupFindsExecutableGameDataRule);
}
