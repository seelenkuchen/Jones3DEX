#include <unity_fixture.h>

#include <Jones3D/Main/JonesData.h>
#include <Jones3D/Main/JonesDataLocate.h>
#include <Jones3D/Main/JonesInstall.h>

#include <std/General/std.h>
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
#define JONESINSTALL_TEST_MKDIR(path) _mkdir(path)
#define JONESINSTALL_TEST_RMDIR(path) _rmdir(path)
#define JONESINSTALL_TEST_GETPID _getpid
#else
#include <dirent.h>
#include <unistd.h>
#define JONESINSTALL_TEST_MKDIR(path) mkdir((path), 0700)
#define JONESINSTALL_TEST_RMDIR(path) rmdir(path)
#define JONESINSTALL_TEST_GETPID getpid
#endif

#define JONESINSTALL_TEST_MAX_NAME 512
#define JONESINSTALL_TEST_STEAM_NAME "Indiana Jones and the Infernal Machine"
#define JONESINSTALL_TEST_GOG_INSTALLER "setup_indiana_jones_and_the_infernal_machine_1.2.exe"

static char g_aTempDir[JONESINSTALL_MAX_PATH];
static unsigned int g_tempDirCounter;

static tHostServices g_hostServices;
static bool g_bHostServices;

static bool g_bFreeBytesKnown;
static int64_t g_freeBytes;

static size_t g_numProgressCalls;
static int64_t g_lastDone;
static int64_t g_lastTotal;
static char g_lastDestination[JONESINSTALL_MAX_PATH];
static bool g_bCancelAtProgress;
static bool* g_pbCancelTarget;

TEST_GROUP(JonesInstall);

static const char* JonesInstallTest_GetEnv(const char* pName)
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

static const char* JonesInstallTest_TempBase(void)
{
#ifdef _WIN32
    static char aBase[JONESINSTALL_MAX_PATH];

    DWORD len = GetTempPathA((DWORD)sizeof(aBase), aBase);
    if ( len > 0 && (size_t)len < sizeof(aBase) )
    {
        while ( len > 1 && (aBase[len - 1] == '\\' || aBase[len - 1] == '/') )
        {
            aBase[--len] = '\0';
        }

        return aBase;
    }

    const char* pBase = JonesInstallTest_GetEnv("TEMP");
    return pBase && pBase[0] ? pBase : "C:\\Windows\\Temp";
#else
    const char* pBase = JonesInstallTest_GetEnv("TMPDIR");
    if ( !pBase || pBase[0] == '\0' )
    {
        pBase = JonesInstallTest_GetEnv("TEMP");
    }
    if ( !pBase || pBase[0] == '\0' )
    {
        pBase = JonesInstallTest_GetEnv("TMP");
    }
    if ( !pBase || pBase[0] == '\0' )
    {
        pBase = "/tmp";
    }

    return pBase;
#endif
}

static int JonesInstallTest_MakeTempDir(void)
{
    const char* pBase = JonesInstallTest_TempBase();

    for ( unsigned int attempt = 0; attempt < 1000; ++attempt )
    {
        int len = snprintf(g_aTempDir, sizeof(g_aTempDir), "%s/JonesInstallTest-%ld-%u",
            pBase, (long)JONESINSTALL_TEST_GETPID(), ++g_tempDirCounter);
        if ( len > 0 && (size_t)len < sizeof(g_aTempDir)
          && JONESINSTALL_TEST_MKDIR(g_aTempDir) == 0 )
        {
            return 0;
        }
    }

    g_aTempDir[0] = '\0';
    return -1;
}

static void JonesInstallTest_BuildPath(char* pOut, size_t outSize, const char* pDir,
    const char* pName)
{
    int len = snprintf(pOut, outSize, "%s/%s", pDir, pName);
    TEST_ASSERT_TRUE(len > 0 && (size_t)len < outSize);
}

static bool JonesInstallTest_IsDirectory(const char* pPath)
{
#ifdef _WIN32
    struct _stat st;
    return _stat(pPath, &st) == 0 && (st.st_mode & _S_IFDIR) != 0;
#else
    struct stat st;
    return stat(pPath, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static void JonesInstallTest_MakeDirs(const char* pPath)
{
    char aPath[JONESINSTALL_MAX_PATH];
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
        if ( aPath[0] != '\0' && !JonesInstallTest_IsDirectory(aPath) )
        {
            JONESINSTALL_TEST_MKDIR(aPath);
        }
        aPath[i] = ch;
    }

    if ( !JonesInstallTest_IsDirectory(aPath) )
    {
        JONESINSTALL_TEST_MKDIR(aPath);
    }

    TEST_ASSERT_TRUE(JonesInstallTest_IsDirectory(aPath));
}

static void JonesInstallTest_RemoveTree(const char* pDir)
{
    char aEntries[64][JONESINSTALL_TEST_MAX_NAME];
    size_t numEntries = 0;

#ifdef _WIN32
    char aPattern[JONESINSTALL_MAX_PATH];
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
        char aPath[JONESINSTALL_MAX_PATH];
        int len = snprintf(aPath, sizeof(aPath), "%s/%s", pDir, aEntries[i]);
        if ( len <= 0 || (size_t)len >= sizeof(aPath) )
        {
            continue;
        }

        if ( JonesInstallTest_IsDirectory(aPath) )
        {
            JonesInstallTest_RemoveTree(aPath);
        }
        else
        {
            remove(aPath);
        }
    }

    JONESINSTALL_TEST_RMDIR(pDir);
}

static void JonesInstallTest_WriteFile(const char* pDir, const char* pName,
    const void* pData, size_t size)
{
    char aPath[JONESINSTALL_MAX_PATH];
    JonesInstallTest_BuildPath(aPath, sizeof(aPath), pDir, pName);

    FILE* pFile = NULL;
    TEST_ASSERT_EQUAL_INT(0, fopen_s(&pFile, aPath, "wb"));
    TEST_ASSERT_NOT_NULL(pFile);

    if ( size > 0 )
    {
        TEST_ASSERT_EQUAL_size_t(size, fwrite(pData, 1, size, pFile));
    }

    TEST_ASSERT_EQUAL_INT(0, fclose(pFile));
}

static void JonesInstallTest_WriteText(const char* pDir, const char* pName, const char* pText)
{
    JonesInstallTest_WriteFile(pDir, pName, pText, strlen(pText));
}

static size_t JonesInstallTest_ReadWhole(const char* pPath, char* pOut, size_t outSize)
{
    FILE* pFile = NULL;
    TEST_ASSERT_EQUAL_INT(0, fopen_s(&pFile, pPath, "rb"));
    TEST_ASSERT_NOT_NULL(pFile);

    size_t numRead = fread(pOut, 1, outSize - 1, pFile);
    pOut[numRead] = '\0';
    TEST_ASSERT_EQUAL_INT(0, fclose(pFile));
    return numRead;
}

static bool JonesInstallTest_FileExists(const char* pPath)
{
#ifdef _WIN32
    struct _stat st;
    return _stat(pPath, &st) == 0;
#else
    struct stat st;
    return stat(pPath, &st) == 0;
#endif
}

static void JonesInstallTest_WriteRequiredSet(const char* pResourceDir)
{
    JonesInstallTest_MakeDirs(pResourceDir);
    JonesInstallTest_WriteFile(pResourceDir, "Jones3D.GOB", "jones", 5);
    JonesInstallTest_WriteFile(pResourceDir, "CD1.GOB", "cd1", 3);
    JonesInstallTest_WriteFile(pResourceDir, "CD2.GOB", "cd2", 3);
}

static void JonesInstallTest_WriteDisc1(const char* pRoot)
{
    char aInstall[JONESINSTALL_MAX_PATH];
    char aResource[JONESINSTALL_MAX_PATH];
    JonesInstallTest_BuildPath(aInstall, sizeof(aInstall), pRoot, "install");
    JonesInstallTest_BuildPath(aResource, sizeof(aResource), pRoot, "Resource");
    JonesInstallTest_MakeDirs(aInstall);
    JonesInstallTest_MakeDirs(aResource);
    JonesInstallTest_WriteText(aInstall, "license.txt", "license");
    JonesInstallTest_WriteFile(aResource, "CD1.GOB", "cd1", 3);
}

static void JonesInstallTest_WriteDisc2(const char* pRoot)
{
    char aResource[JONESINSTALL_MAX_PATH];
    JonesInstallTest_BuildPath(aResource, sizeof(aResource), pRoot, "Resource");
    JonesInstallTest_MakeDirs(aResource);
    JonesInstallTest_WriteFile(aResource, "CD2.GOB", "cd2", 3);
}

static bool JonesInstallTest_PathsEqual(const char* pPath1, const char* pPath2)
{
    char aPath1[JONESINSTALL_MAX_PATH];
    char aPath2[JONESINSTALL_MAX_PATH];

    snprintf(aPath1, sizeof(aPath1), "%s", pPath1);
    snprintf(aPath2, sizeof(aPath2), "%s", pPath2);

    for ( size_t i = 0; aPath1[i] != '\0'; ++i )
    {
        if ( aPath1[i] == '\\' )
        {
            aPath1[i] = '/';
        }
        if ( aPath1[i] >= 'A' && aPath1[i] <= 'Z' )
        {
            aPath1[i] = (char)(aPath1[i] - 'A' + 'a');
        }
    }

    for ( size_t i = 0; aPath2[i] != '\0'; ++i )
    {
        if ( aPath2[i] == '\\' )
        {
            aPath2[i] = '/';
        }
        if ( aPath2[i] >= 'A' && aPath2[i] <= 'Z' )
        {
            aPath2[i] = (char)(aPath2[i] - 'A' + 'a');
        }
    }

    return strcmp(aPath1, aPath2) == 0;
}

static const JonesInstallSource* JonesInstallTest_FindSource(const JonesInstallSourceList* pList,
    JonesInstallSourceKind kind, const char* pPath)
{
    for ( size_t i = 0; i < pList->numSources; ++i )
    {
        if ( pList->aSources[i].kind == kind
          && JonesInstallTest_PathsEqual(pList->aSources[i].aPath, pPath) )
        {
            return &pList->aSources[i];
        }
    }

    return NULL;
}

static const JonesInstallPlanItem* JonesInstallTest_FindItem(const JonesInstallPlan* pPlan,
    const char* pDestination)
{
    for ( size_t i = 0; i < pPlan->numItems; ++i )
    {
        if ( strcmp(pPlan->aItems[i].aDestination, pDestination) == 0 )
        {
            return &pPlan->aItems[i];
        }
    }

    return NULL;
}

static void JonesInstallTest_CheckNoPartFiles(const char* pDir)
{
#ifdef _WIN32
    char aPattern[JONESINSTALL_MAX_PATH];
    snprintf(aPattern, sizeof(aPattern), "%s/*", pDir);
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

        char aPath[JONESINSTALL_MAX_PATH];
        snprintf(aPath, sizeof(aPath), "%s/%s", pDir, data.cFileName);
        if ( (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 )
        {
            JonesInstallTest_CheckNoPartFiles(aPath);
        }
        else
        {
            TEST_ASSERT_NULL(strstr(data.cFileName, ".part"));
        }
    } while ( FindNextFileA(hFind, &data) );

    FindClose(hFind);
#else
    DIR* pDirHandle = opendir(pDir);
    if ( !pDirHandle )
    {
        return;
    }

    struct dirent* pEntry;
    while ( (pEntry = readdir(pDirHandle)) != NULL )
    {
        if ( strcmp(pEntry->d_name, ".") == 0 || strcmp(pEntry->d_name, "..") == 0 )
        {
            continue;
        }

        char aPath[JONESINSTALL_MAX_PATH];
        snprintf(aPath, sizeof(aPath), "%s/%s", pDir, pEntry->d_name);
        if ( JonesInstallTest_IsDirectory(aPath) )
        {
            JonesInstallTest_CheckNoPartFiles(aPath);
        }
        else
        {
            TEST_ASSERT_NULL(strstr(pEntry->d_name, ".part"));
        }
    }

    closedir(pDirHandle);
#endif
}

static void* J3DAPI JonesInstallTest_Malloc(size_t size)
{
    return malloc(size);
}

static void J3DAPI JonesInstallTest_Free(void* pData)
{
    free(pData);
}

static void* J3DAPI JonesInstallTest_Realloc(void* pData, size_t size)
{
    return realloc(pData, size);
}

static int J3DAPI JonesInstallTest_Print(const char* pFormat, ...)
{
    va_list args;
    va_start(args, pFormat);
    int result = vfprintf(stderr, pFormat, args);
    va_end(args);
    return result;
}

static tFileHandle J3DAPI JonesInstallTest_FileOpen(const char* pFilename, const char* mode)
{
    FILE* pFile = NULL;
    if ( fopen_s(&pFile, pFilename, mode) != 0 )
    {
        return 0;
    }

    return (tFileHandle)(uintptr_t)pFile;
}

static int J3DAPI JonesInstallTest_FileClose(tFileHandle fh)
{
    return fclose((FILE*)(uintptr_t)fh);
}

static size_t J3DAPI JonesInstallTest_FileRead(tFileHandle fh, void* pDst, size_t size)
{
    return fread(pDst, 1, size, (FILE*)(uintptr_t)fh);
}

static char* J3DAPI JonesInstallTest_FileGets(tFileHandle fh, char* pStr, size_t size)
{
    return fgets(pStr, (int)size, (FILE*)(uintptr_t)fh);
}

static size_t J3DAPI JonesInstallTest_FileWrite(tFileHandle fh, const void* pData, size_t size)
{
    return fwrite(pData, 1, size, (FILE*)(uintptr_t)fh);
}

static int J3DAPI JonesInstallTest_FileEOF(tFileHandle fh)
{
    return feof((FILE*)(uintptr_t)fh);
}

static int J3DAPI JonesInstallTest_FileTell(tFileHandle fh)
{
    return (int)ftell((FILE*)(uintptr_t)fh);
}

static int J3DAPI JonesInstallTest_FileSeek(tFileHandle fh, int offset, int origin)
{
    return fseek((FILE*)(uintptr_t)fh, offset, origin);
}

static size_t J3DAPI JonesInstallTest_FileSize(const char* pFilename)
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

static int JonesInstallTest_FilePrintf(tFileHandle fh, const char* pFormat, ...)
{
    va_list args;
    va_start(args, pFormat);
    int result = vfprintf((FILE*)(uintptr_t)fh, pFormat, args);
    va_end(args);
    return result;
}

static void JonesInstallTest_StartHostServices(void)
{
    memset(&g_hostServices, 0, sizeof(g_hostServices));
    g_hostServices.pStatusPrint  = JonesInstallTest_Print;
    g_hostServices.pMessagePrint = JonesInstallTest_Print;
    g_hostServices.pWarningPrint = JonesInstallTest_Print;
    g_hostServices.pErrorPrint   = JonesInstallTest_Print;
    g_hostServices.pDebugPrint   = JonesInstallTest_Print;
    g_hostServices.pMalloc       = JonesInstallTest_Malloc;
    g_hostServices.pFree         = JonesInstallTest_Free;
    g_hostServices.pRealloc      = JonesInstallTest_Realloc;
    g_hostServices.pFileOpen     = JonesInstallTest_FileOpen;
    g_hostServices.pFileClose    = JonesInstallTest_FileClose;
    g_hostServices.pFileRead     = JonesInstallTest_FileRead;
    g_hostServices.pFileGets     = JonesInstallTest_FileGets;
    g_hostServices.pFileWrite    = JonesInstallTest_FileWrite;
    g_hostServices.pFileEOF      = JonesInstallTest_FileEOF;
    g_hostServices.pFileTell     = JonesInstallTest_FileTell;
    g_hostServices.pFileSeek     = JonesInstallTest_FileSeek;
    g_hostServices.pFileSize     = JonesInstallTest_FileSize;
    g_hostServices.pFilePrintf   = JonesInstallTest_FilePrintf;

    std_g_pHS     = &g_hostServices;
    g_bHostServices = true;
}

static void JonesInstallTest_StopHostServices(void)
{
    if ( g_bHostServices )
    {
        std_g_pHS     = NULL;
        g_bHostServices = false;
    }
}

static bool JonesInstallTest_GetFreeBytes(const char* pPath, int64_t* pOutFreeBytes,
    void* pUserData)
{
    (void)pPath;
    (void)pUserData;

    if ( !g_bFreeBytesKnown )
    {
        return false;
    }

    *pOutFreeBytes = g_freeBytes;
    return true;
}

static void JonesInstallTest_MakeHost(JonesInstallHost* pHost, JonesInstallPlatform platform,
    const char* pHome)
{
    memset(pHost, 0, sizeof(*pHost));
    pHost->platform        = platform;
    pHost->pHome           = pHome;
    pHost->pfGetFreeBytes  = JonesInstallTest_GetFreeBytes;
}

static void JonesInstallTest_Progress(int64_t bytesDone, int64_t bytesTotal,
    const char* pCurrentDestination, void* pUserData)
{
    (void)pUserData;

    ++g_numProgressCalls;
    g_lastDone  = bytesDone;
    g_lastTotal = bytesTotal;
    snprintf(g_lastDestination, sizeof(g_lastDestination), "%s",
        pCurrentDestination ? pCurrentDestination : "");

    if ( g_bCancelAtProgress && g_pbCancelTarget && bytesDone > 0 )
    {
        *g_pbCancelTarget = true;
    }
}

TEST_SETUP(JonesInstall)
{
    TEST_ASSERT_EQUAL_INT(0, JonesInstallTest_MakeTempDir());
    JonesInstallTest_StartHostServices();

    g_bFreeBytesKnown    = false;
    g_freeBytes          = 0;
    g_numProgressCalls   = 0;
    g_lastDone           = 0;
    g_lastTotal          = 0;
    g_lastDestination[0] = '\0';
    g_bCancelAtProgress  = false;
    g_pbCancelTarget     = NULL;
}

TEST_TEAR_DOWN(JonesInstall)
{
    JonesInstallTest_StopHostServices();
    if ( g_aTempDir[0] != '\0' )
    {
        JonesInstallTest_RemoveTree(g_aTempDir);
        g_aTempDir[0] = '\0';
    }
}

TEST(JonesInstall, TestFindSteamSourceLinux)
{
    char aHome[JONESINSTALL_MAX_PATH];
    char aSteam[JONESINSTALL_MAX_PATH];
    char aCommon[JONESINSTALL_MAX_PATH];
    char aGame[JONESINSTALL_MAX_PATH];
    char aResource[JONESINSTALL_MAX_PATH];

    JonesInstallTest_BuildPath(aHome, sizeof(aHome), g_aTempDir, "home");
    JonesInstallTest_BuildPath(aSteam, sizeof(aSteam), aHome, ".steam");
    JonesInstallTest_BuildPath(aCommon, sizeof(aCommon), aSteam, "steam");
    JonesInstallTest_BuildPath(aGame, sizeof(aGame), aCommon, "steamapps/common/" JONESINSTALL_TEST_STEAM_NAME);
    JonesInstallTest_BuildPath(aResource, sizeof(aResource), aGame, "Resource");
    JonesInstallTest_WriteRequiredSet(aResource);

    JonesInstallHost host;
    JonesInstallTest_MakeHost(&host, JONESINSTALL_PLATFORM_LINUX, aHome);

    JonesInstallSourceList sources;
    TEST_ASSERT_TRUE(JonesInstall_FindSources(&host, &sources));

    const JonesInstallSource* pSource = JonesInstallTest_FindSource(&sources,
        JONESINSTALL_SOURCE_STEAM, aGame);
    TEST_ASSERT_NOT_NULL(pSource);
    TEST_ASSERT_TRUE(pSource->bComplete);
    TEST_ASSERT_EQUAL_INT(0, pSource->discNumber);
}

TEST(JonesInstall, TestFindSteamLibraryFolders)
{
    char aHome[JONESINSTALL_MAX_PATH];
    char aRoot[JONESINSTALL_MAX_PATH];
    char aSteamApps[JONESINSTALL_MAX_PATH];
    char aLibrary1[JONESINSTALL_MAX_PATH];
    char aLibrary2[JONESINSTALL_MAX_PATH];
    char aGame1[JONESINSTALL_MAX_PATH];
    char aGame2[JONESINSTALL_MAX_PATH];
    char aResource1[JONESINSTALL_MAX_PATH];
    char aResource2[JONESINSTALL_MAX_PATH];
    char aVdf[JONESINSTALL_MAX_PATH * 2 + 128];

    JonesInstallTest_BuildPath(aHome, sizeof(aHome), g_aTempDir, "home");
    JonesInstallTest_BuildPath(aRoot, sizeof(aRoot), aHome, ".steam/steam");
    JonesInstallTest_BuildPath(aSteamApps, sizeof(aSteamApps), aRoot, "steamapps");
    JonesInstallTest_BuildPath(aLibrary1, sizeof(aLibrary1), g_aTempDir, "library1");
    JonesInstallTest_BuildPath(aLibrary2, sizeof(aLibrary2), g_aTempDir, "library2");
    JonesInstallTest_BuildPath(aGame1, sizeof(aGame1), aLibrary1,
        "steamapps/common/" JONESINSTALL_TEST_STEAM_NAME);
    JonesInstallTest_BuildPath(aGame2, sizeof(aGame2), aLibrary2,
        "steamapps/common/" JONESINSTALL_TEST_STEAM_NAME);
    JonesInstallTest_BuildPath(aResource1, sizeof(aResource1), aGame1, "Resource");
    JonesInstallTest_BuildPath(aResource2, sizeof(aResource2), aGame2, "Resource");
    JonesInstallTest_WriteRequiredSet(aResource1);
    JonesInstallTest_MakeDirs(aResource2);
    JonesInstallTest_WriteFile(aResource2, "Jones3D.GOB", "jones", 5);

    JonesInstallTest_MakeDirs(aSteamApps);
    snprintf(aVdf, sizeof(aVdf),
        "\"libraryfolders\"\n{\n"
        "  \"0\"\n  {\n    \"path\" \"%s\"\n  }\n"
        "  \"1\"\n  {\n    \"path\" \"%s\"\n  }\n"
        "}\n", aLibrary1, aLibrary2);
    JonesInstallTest_WriteText(aSteamApps, "libraryfolders.vdf", aVdf);

    JonesInstallHost host;
    JonesInstallTest_MakeHost(&host, JONESINSTALL_PLATFORM_LINUX, aHome);

    JonesInstallSourceList sources;
    TEST_ASSERT_TRUE(JonesInstall_FindSources(&host, &sources));

    const JonesInstallSource* pSource1 = JonesInstallTest_FindSource(&sources,
        JONESINSTALL_SOURCE_STEAM, aGame1);
    TEST_ASSERT_NOT_NULL(pSource1);
    TEST_ASSERT_TRUE(pSource1->bComplete);

    const JonesInstallSource* pSource2 = JonesInstallTest_FindSource(&sources,
        JONESINSTALL_SOURCE_STEAM, aGame2);
    TEST_ASSERT_NOT_NULL(pSource2);
    TEST_ASSERT_FALSE(pSource2->bComplete);
}

TEST(JonesInstall, TestFindGogInstalled)
{
    char aHome[JONESINSTALL_MAX_PATH];
    char aGog[JONESINSTALL_MAX_PATH];
    char aHeroic[JONESINSTALL_MAX_PATH];
    char aRegistry[JONESINSTALL_MAX_PATH];
    char aGogResource[JONESINSTALL_MAX_PATH];
    char aHeroicResource[JONESINSTALL_MAX_PATH];
    char aRegistryResource[JONESINSTALL_MAX_PATH];

    JonesInstallTest_BuildPath(aHome, sizeof(aHome), g_aTempDir, "home");
    JonesInstallTest_BuildPath(aGog, sizeof(aGog), aHome,
        "GOG Games/" JONESINSTALL_TEST_STEAM_NAME);
    JonesInstallTest_BuildPath(aHeroic, sizeof(aHeroic), aHome,
        "Games/Heroic/" JONESINSTALL_TEST_STEAM_NAME);
    JonesInstallTest_BuildPath(aRegistry, sizeof(aRegistry), g_aTempDir, "registry-game");
    JonesInstallTest_BuildPath(aGogResource, sizeof(aGogResource), aGog, "Resource");
    JonesInstallTest_BuildPath(aHeroicResource, sizeof(aHeroicResource), aHeroic, "Resource");
    JonesInstallTest_BuildPath(aRegistryResource, sizeof(aRegistryResource), aRegistry, "Resource");
    JonesInstallTest_WriteRequiredSet(aGogResource);
    JonesInstallTest_WriteRequiredSet(aRegistryResource);
    JonesInstallTest_MakeDirs(aHeroicResource);
    JonesInstallTest_WriteFile(aHeroicResource, "Jones3D.GOB", "jones", 5);

    JonesInstallPathList registry;
    memset(&registry, 0, sizeof(registry));
    snprintf(registry.aPaths[registry.numPaths++], JONESINSTALL_MAX_PATH, "%s", aRegistry);

    JonesInstallHost host;
    JonesInstallTest_MakeHost(&host, JONESINSTALL_PLATFORM_LINUX, aHome);
    host.pGogRegistryPaths = &registry;

    JonesInstallSourceList sources;
    TEST_ASSERT_TRUE(JonesInstall_FindSources(&host, &sources));

    const JonesInstallSource* pGog = JonesInstallTest_FindSource(&sources,
        JONESINSTALL_SOURCE_GOG_INSTALLED, aGog);
    TEST_ASSERT_NOT_NULL(pGog);
    TEST_ASSERT_TRUE(pGog->bComplete);

    const JonesInstallSource* pHeroic = JonesInstallTest_FindSource(&sources,
        JONESINSTALL_SOURCE_GOG_INSTALLED, aHeroic);
    TEST_ASSERT_NOT_NULL(pHeroic);
    TEST_ASSERT_FALSE(pHeroic->bComplete);

    const JonesInstallSource* pRegistry = JonesInstallTest_FindSource(&sources,
        JONESINSTALL_SOURCE_GOG_INSTALLED, aRegistry);
    TEST_ASSERT_NOT_NULL(pRegistry);
    TEST_ASSERT_TRUE(pRegistry->bComplete);
}

TEST(JonesInstall, TestFindGogInstaller)
{
    char aHome[JONESINSTALL_MAX_PATH];
    char aDownloads[JONESINSTALL_MAX_PATH];
    char aXdg[JONESINSTALL_MAX_PATH];
    char aInstaller1[JONESINSTALL_MAX_PATH];
    char aInstaller2[JONESINSTALL_MAX_PATH];
    char aInstaller3[JONESINSTALL_MAX_PATH];

    JonesInstallTest_BuildPath(aHome, sizeof(aHome), g_aTempDir, "home");
    JonesInstallTest_BuildPath(aDownloads, sizeof(aDownloads), aHome, "Downloads");
    JonesInstallTest_BuildPath(aXdg, sizeof(aXdg), g_aTempDir, "xdg-downloads");
    JonesInstallTest_MakeDirs(aDownloads);
    JonesInstallTest_MakeDirs(aXdg);
    JonesInstallTest_WriteText(aDownloads, JONESINSTALL_TEST_GOG_INSTALLER, "setup");
    JonesInstallTest_WriteText(aXdg, "setup_indiana_jones_and_the_infernal_machine_2.exe", "setup");
    JonesInstallTest_WriteText(aHome, "setup_indiana_jones_and_the_infernal_machine_3.exe", "setup");
    JonesInstallTest_WriteText(aDownloads, "other.exe", "other");
    JonesInstallTest_BuildPath(aInstaller1, sizeof(aInstaller1), aDownloads,
        JONESINSTALL_TEST_GOG_INSTALLER);
    JonesInstallTest_BuildPath(aInstaller2, sizeof(aInstaller2), aXdg,
        "setup_indiana_jones_and_the_infernal_machine_2.exe");
    JonesInstallTest_BuildPath(aInstaller3, sizeof(aInstaller3), aHome,
        "setup_indiana_jones_and_the_infernal_machine_3.exe");

    JonesInstallHost host;
    JonesInstallTest_MakeHost(&host, JONESINSTALL_PLATFORM_LINUX, aHome);
    host.pXdgDownloadDir = aXdg;

    JonesInstallSourceList sources;
    TEST_ASSERT_TRUE(JonesInstall_FindSources(&host, &sources));

    const JonesInstallSource* pInstaller1 = JonesInstallTest_FindSource(&sources,
        JONESINSTALL_SOURCE_GOG_INSTALLER, aInstaller1);
    TEST_ASSERT_NOT_NULL(pInstaller1);
    TEST_ASSERT_FALSE(pInstaller1->bComplete);

    const JonesInstallSource* pInstaller2 = JonesInstallTest_FindSource(&sources,
        JONESINSTALL_SOURCE_GOG_INSTALLER, aInstaller2);
    TEST_ASSERT_NOT_NULL(pInstaller2);

    const JonesInstallSource* pInstaller3 = JonesInstallTest_FindSource(&sources,
        JONESINSTALL_SOURCE_GOG_INSTALLER, aInstaller3);
    TEST_ASSERT_NOT_NULL(pInstaller3);

    TEST_ASSERT_EQUAL_size_t(3, sources.numSources);
}

TEST(JonesInstall, TestFindDiscsFromVolumeBase)
{
    char aBase[JONESINSTALL_MAX_PATH];
    char aDisc1[JONESINSTALL_MAX_PATH];
    char aDisc2[JONESINSTALL_MAX_PATH];

    JonesInstallTest_BuildPath(aBase, sizeof(aBase), g_aTempDir, "media");
    JonesInstallTest_BuildPath(aDisc1, sizeof(aDisc1), aBase, "disc1");
    JonesInstallTest_BuildPath(aDisc2, sizeof(aDisc2), aBase, "disc2");
    JonesInstallTest_WriteDisc1(aDisc1);
    JonesInstallTest_WriteDisc2(aDisc2);

    JonesInstallPathList volumes;
    memset(&volumes, 0, sizeof(volumes));
    snprintf(volumes.aPaths[volumes.numPaths++], JONESINSTALL_MAX_PATH, "%s", aBase);

    JonesInstallHost host;
    JonesInstallTest_MakeHost(&host, JONESINSTALL_PLATFORM_LINUX, NULL);
    host.pVolumeRoots = &volumes;

    JonesInstallSourceList sources;
    TEST_ASSERT_TRUE(JonesInstall_FindSources(&host, &sources));

    const JonesInstallSource* pDisc1 = JonesInstallTest_FindSource(&sources,
        JONESINSTALL_SOURCE_DISC, aDisc1);
    TEST_ASSERT_NOT_NULL(pDisc1);
    TEST_ASSERT_EQUAL_INT(1, pDisc1->discNumber);
    TEST_ASSERT_FALSE(pDisc1->bComplete);

    const JonesInstallSource* pDisc2 = JonesInstallTest_FindSource(&sources,
        JONESINSTALL_SOURCE_DISC, aDisc2);
    TEST_ASSERT_NOT_NULL(pDisc2);
    TEST_ASSERT_EQUAL_INT(2, pDisc2->discNumber);
    TEST_ASSERT_FALSE(pDisc2->bComplete);
}

TEST(JonesInstall, TestFindWindowsStylePaths)
{
    char aHome[JONESINSTALL_MAX_PATH];
    char aProgramFiles[JONESINSTALL_MAX_PATH];
    char aSteam[JONESINSTALL_MAX_PATH];
    char aCommon[JONESINSTALL_MAX_PATH];
    char aGame[JONESINSTALL_MAX_PATH];
    char aResource[JONESINSTALL_MAX_PATH];
    char aExpected[JONESINSTALL_MAX_PATH + 128];
    char aRegistry[JONESINSTALL_MAX_PATH];
    char aRegistryResource[JONESINSTALL_MAX_PATH];
    char aWindowsGame[JONESINSTALL_MAX_PATH + 64];

    JonesInstallTest_BuildPath(aHome, sizeof(aHome), g_aTempDir, "home");
    JonesInstallTest_BuildPath(aProgramFiles, sizeof(aProgramFiles), g_aTempDir, "pf-x86");
    JonesInstallTest_BuildPath(aSteam, sizeof(aSteam), aProgramFiles, "Steam");
    JonesInstallTest_BuildPath(aCommon, sizeof(aCommon), aSteam, "steamapps/common");
    JonesInstallTest_BuildPath(aGame, sizeof(aGame), aCommon, JONESINSTALL_TEST_STEAM_NAME);
    JonesInstallTest_BuildPath(aResource, sizeof(aResource), aGame, "Resource");
    JonesInstallTest_WriteRequiredSet(aResource);

    JonesInstallTest_BuildPath(aRegistry, sizeof(aRegistry), g_aTempDir, "registry-game");
    JonesInstallTest_BuildPath(aRegistryResource, sizeof(aRegistryResource), aRegistry, "Resource");
    JonesInstallTest_WriteRequiredSet(aRegistryResource);

    snprintf(aExpected, sizeof(aExpected), "%s\\Steam\\steamapps\\common\\%s",
        aProgramFiles, JONESINSTALL_TEST_STEAM_NAME);
    snprintf(aWindowsGame, sizeof(aWindowsGame), "%s\\registry-game", g_aTempDir);

    JonesInstallPathList registry;
    memset(&registry, 0, sizeof(registry));
    size_t windowsGameLen = strlen(aWindowsGame);
    TEST_ASSERT_TRUE(windowsGameLen + 1 <= sizeof(registry.aPaths[0]));
    memcpy(registry.aPaths[0], aWindowsGame, windowsGameLen + 1);
    registry.numPaths = 1;

    JonesInstallHost host;
    JonesInstallTest_MakeHost(&host, JONESINSTALL_PLATFORM_WINDOWS, aHome);
    host.pProgramFilesX86 = aProgramFiles;
    host.pGogRegistryPaths = &registry;

    JonesInstallSourceList sources;
    TEST_ASSERT_TRUE(JonesInstall_FindSources(&host, &sources));

    const JonesInstallSource* pSteam = JonesInstallTest_FindSource(&sources,
        JONESINSTALL_SOURCE_STEAM, aExpected);
    TEST_ASSERT_NOT_NULL(pSteam);
    TEST_ASSERT_TRUE(pSteam->bComplete);

    const JonesInstallSource* pRegistry = JonesInstallTest_FindSource(&sources,
        JONESINSTALL_SOURCE_GOG_INSTALLED, aWindowsGame);
    TEST_ASSERT_NOT_NULL(pRegistry);
    TEST_ASSERT_TRUE(pRegistry->bComplete);
}

TEST(JonesInstall, TestGetFolderSource)
{
    char aFolder[JONESINSTALL_MAX_PATH];
    char aResource[JONESINSTALL_MAX_PATH];
    char aMissing[JONESINSTALL_MAX_PATH];

    JonesInstallTest_BuildPath(aFolder, sizeof(aFolder), g_aTempDir, "folder");
    JonesInstallTest_BuildPath(aResource, sizeof(aResource), aFolder, "Resource");
    JonesInstallTest_WriteRequiredSet(aResource);
    JonesInstallTest_BuildPath(aMissing, sizeof(aMissing), g_aTempDir, "missing");

    JonesInstallHost host;
    JonesInstallTest_MakeHost(&host, JONESINSTALL_PLATFORM_LINUX, NULL);

    JonesInstallSource source;
    TEST_ASSERT_TRUE(JonesInstall_GetFolderSource(&host, aFolder, &source));
    TEST_ASSERT_EQUAL_INT(JONESINSTALL_SOURCE_FOLDER, source.kind);
    TEST_ASSERT_TRUE(source.bComplete);
    TEST_ASSERT_TRUE(JonesInstallTest_PathsEqual(aFolder, source.aPath));

    TEST_ASSERT_FALSE(JonesInstall_GetFolderSource(&host, aMissing, &source));
}

TEST(JonesInstall, TestBuildPlanContents)
{
    char aSource[JONESINSTALL_MAX_PATH];
    char aResource[JONESINSTALL_MAX_PATH];
    char aCog[JONESINSTALL_MAX_PATH];
    char aKeySets[JONESINSTALL_MAX_PATH];
    char aSaves[JONESINSTALL_MAX_PATH];
    char aTarget[JONESINSTALL_MAX_PATH];

    JonesInstallTest_BuildPath(aSource, sizeof(aSource), g_aTempDir, "source");
    JonesInstallTest_BuildPath(aResource, sizeof(aResource), aSource, "resource");
    JonesInstallTest_BuildPath(aCog, sizeof(aCog), aResource, "Cog");
    JonesInstallTest_BuildPath(aKeySets, sizeof(aKeySets), aSource, "keysets");
    JonesInstallTest_BuildPath(aSaves, sizeof(aSaves), aSource, "SaveGames");
    JonesInstallTest_BuildPath(aTarget, sizeof(aTarget), g_aTempDir, "target");

    JonesInstallTest_MakeDirs(aResource);
    JonesInstallTest_WriteFile(aResource, "jones3d.gob", "jones", 5);
    JonesInstallTest_WriteFile(aResource, "cd1.gob", "cd1", 3);
    JonesInstallTest_WriteFile(aResource, "cd2.gob", "cd2", 3);
    JonesInstallTest_WriteFile(aResource, "jonesopn.snm", "snm", 3);
    JonesInstallTest_MakeDirs(aCog);
    JonesInstallTest_WriteFile(aCog, "Fix.COG", "fix", 3);
    JonesInstallTest_MakeDirs(aKeySets);
    JonesInstallTest_WriteFile(aKeySets, "default.kfg", "keys", 4);
    JonesInstallTest_WriteFile(aKeySets, "readme.txt", "readme", 6);
    JonesInstallTest_WriteText(aSource, "Jones3D.chm", "help");
    JonesInstallTest_MakeDirs(aSaves);
    JonesInstallTest_WriteFile(aSaves, "save1.nds", "save", 4);

    JonesInstallPlan plan;
    TEST_ASSERT_TRUE(JonesInstall_BuildPlan(JONESINSTALL_PLATFORM_LINUX, aSource, aTarget, true,
        &plan));

    TEST_ASSERT_EQUAL_size_t(8, plan.numItems);
    TEST_ASSERT_TRUE(plan.bComplete);
    TEST_ASSERT_FALSE(plan.bNeedsDisc2);
    TEST_ASSERT_EQUAL_STRING("", plan.aMissing);
    TEST_ASSERT_EQUAL_INT64(5 + 3 + 3 + 3 + 3 + 4 + 4 + 4, plan.totalBytes);

    const JonesInstallPlanItem* pItem;

    char aExpectedSource[JONESINSTALL_MAX_PATH];
    JonesInstallTest_BuildPath(aExpectedSource, sizeof(aExpectedSource), aResource, "jones3d.gob");

    pItem = JonesInstallTest_FindItem(&plan, "Resource/Jones3D.GOB");
    TEST_ASSERT_NOT_NULL(pItem);
    TEST_ASSERT_TRUE(JonesInstallTest_PathsEqual(aExpectedSource, pItem->aSource));
    TEST_ASSERT_EQUAL_INT64(5, pItem->size);

    pItem = JonesInstallTest_FindItem(&plan, "Resource/jonesopn.snm");
    TEST_ASSERT_NOT_NULL(pItem);
    TEST_ASSERT_EQUAL_INT64(3, pItem->size);

    pItem = JonesInstallTest_FindItem(&plan, "Resource/Cog/Fix.COG");
    TEST_ASSERT_NOT_NULL(pItem);
    TEST_ASSERT_EQUAL_INT64(3, pItem->size);

    pItem = JonesInstallTest_FindItem(&plan, "KeySets/default.kfg");
    TEST_ASSERT_NOT_NULL(pItem);
    TEST_ASSERT_EQUAL_INT64(4, pItem->size);

    pItem = JonesInstallTest_FindItem(&plan, "Jones3D.chm");
    TEST_ASSERT_NOT_NULL(pItem);
    TEST_ASSERT_EQUAL_INT64(4, pItem->size);

    pItem = JonesInstallTest_FindItem(&plan, "SaveGames/save1.nds");
    TEST_ASSERT_NOT_NULL(pItem);
    TEST_ASSERT_EQUAL_INT64(4, pItem->size);

    TEST_ASSERT_NULL(JonesInstallTest_FindItem(&plan, "KeySets/readme.txt"));
}

TEST(JonesInstall, TestBuildPlanDoesNotOverwriteSaves)
{
    char aSource[JONESINSTALL_MAX_PATH];
    char aResource[JONESINSTALL_MAX_PATH];
    char aSaves[JONESINSTALL_MAX_PATH];
    char aTarget[JONESINSTALL_MAX_PATH];
    char aTargetSaves[JONESINSTALL_MAX_PATH];

    JonesInstallTest_BuildPath(aSource, sizeof(aSource), g_aTempDir, "source");
    JonesInstallTest_BuildPath(aResource, sizeof(aResource), aSource, "Resource");
    JonesInstallTest_BuildPath(aSaves, sizeof(aSaves), aSource, "SaveGames");
    JonesInstallTest_BuildPath(aTarget, sizeof(aTarget), g_aTempDir, "target");
    JonesInstallTest_BuildPath(aTargetSaves, sizeof(aTargetSaves), aTarget, "SaveGames");
    JonesInstallTest_WriteRequiredSet(aResource);
    JonesInstallTest_MakeDirs(aSaves);
    JonesInstallTest_WriteFile(aSaves, "save1.nds", "save1", 5);
    JonesInstallTest_WriteFile(aSaves, "save2.nds", "save2", 5);
    JonesInstallTest_MakeDirs(aTargetSaves);
    JonesInstallTest_WriteFile(aTargetSaves, "save1.nds", "keep", 4);

    JonesInstallPlan plan;
    TEST_ASSERT_TRUE(JonesInstall_BuildPlan(JONESINSTALL_PLATFORM_LINUX, aSource, aTarget, true,
        &plan));

    TEST_ASSERT_NULL(JonesInstallTest_FindItem(&plan, "SaveGames/save1.nds"));
    TEST_ASSERT_NOT_NULL(JonesInstallTest_FindItem(&plan, "SaveGames/save2.nds"));
}

TEST(JonesInstall, TestBuildPlanDisc1NeedsDisc2)
{
    char aSource[JONESINSTALL_MAX_PATH];
    char aTarget[JONESINSTALL_MAX_PATH];

    JonesInstallTest_BuildPath(aSource, sizeof(aSource), g_aTempDir, "disc1");
    JonesInstallTest_BuildPath(aTarget, sizeof(aTarget), g_aTempDir, "target");
    JonesInstallTest_WriteDisc1(aSource);

    JonesInstallPlan plan;
    TEST_ASSERT_TRUE(JonesInstall_BuildPlan(JONESINSTALL_PLATFORM_LINUX, aSource, aTarget, false,
        &plan));

    TEST_ASSERT_TRUE(plan.bNeedsDisc2);
    TEST_ASSERT_FALSE(plan.bComplete);
    TEST_ASSERT_NOT_NULL(strstr(plan.aMissing, "Jones3D.GOB"));
    TEST_ASSERT_NOT_NULL(strstr(plan.aMissing, "CD2.GOB"));
    TEST_ASSERT_NOT_NULL(JonesInstallTest_FindItem(&plan, "Resource/CD1.GOB"));
}

TEST(JonesInstall, TestRunCopiesFilesAndReportsProgress)
{
    char aSource[JONESINSTALL_MAX_PATH];
    char aResource[JONESINSTALL_MAX_PATH];
    char aCog[JONESINSTALL_MAX_PATH];
    char aKeySets[JONESINSTALL_MAX_PATH];
    char aTarget[JONESINSTALL_MAX_PATH];
    char aTargetResource[JONESINSTALL_MAX_PATH];
    char aPath[JONESINSTALL_MAX_PATH];
    char aText[32];

    JonesInstallTest_BuildPath(aSource, sizeof(aSource), g_aTempDir, "source");
    JonesInstallTest_BuildPath(aResource, sizeof(aResource), aSource, "Resource");
    JonesInstallTest_BuildPath(aCog, sizeof(aCog), aResource, "Cog");
    JonesInstallTest_BuildPath(aKeySets, sizeof(aKeySets), aSource, "KeySets");
    JonesInstallTest_BuildPath(aTarget, sizeof(aTarget), g_aTempDir, "target");
    JonesInstallTest_BuildPath(aTargetResource, sizeof(aTargetResource), aTarget, "Resource");
    JonesInstallTest_WriteRequiredSet(aResource);
    JonesInstallTest_MakeDirs(aCog);
    JonesInstallTest_WriteFile(aCog, "fix.cog", "fix", 3);
    JonesInstallTest_MakeDirs(aKeySets);
    JonesInstallTest_WriteFile(aKeySets, "default.kfg", "keys", 4);
    JonesInstallTest_WriteText(aSource, "Jones3D.chm", "help");

    JonesInstallPlan plan;
    TEST_ASSERT_TRUE(JonesInstall_BuildPlan(JONESINSTALL_PLATFORM_LINUX, aSource, aTarget, false,
        &plan));

    g_bFreeBytesKnown = true;
    g_freeBytes       = plan.totalBytes * 2;

    JonesInstallHost host;
    JonesInstallTest_MakeHost(&host, JONESINSTALL_PLATFORM_LINUX, NULL);

    JonesInstallRunOutcome outcome;
    TEST_ASSERT_EQUAL_INT(JONESINSTALL_RUN_OK,
        JonesInstall_Run(&plan, &host, JonesInstallTest_Progress, NULL, NULL, &outcome));
    TEST_ASSERT_EQUAL_STRING("", outcome.aMessage);
    TEST_ASSERT_TRUE(g_numProgressCalls >= 2);
    TEST_ASSERT_EQUAL_INT64(plan.totalBytes, g_lastDone);
    TEST_ASSERT_EQUAL_INT64(plan.totalBytes, g_lastTotal);

    JonesInstallTest_BuildPath(aPath, sizeof(aPath), aTargetResource, "Jones3D.GOB");
    TEST_ASSERT_EQUAL_size_t(5, JonesInstallTest_ReadWhole(aPath, aText, sizeof(aText)));
    TEST_ASSERT_EQUAL_STRING("jones", aText);

    JonesInstallTest_BuildPath(aPath, sizeof(aPath), aTargetResource, "Cog/fix.cog");
    TEST_ASSERT_EQUAL_size_t(3, JonesInstallTest_ReadWhole(aPath, aText, sizeof(aText)));
    TEST_ASSERT_EQUAL_STRING("fix", aText);

    JonesInstallTest_BuildPath(aPath, sizeof(aPath), aTarget, "KeySets/default.kfg");
    TEST_ASSERT_EQUAL_size_t(4, JonesInstallTest_ReadWhole(aPath, aText, sizeof(aText)));
    TEST_ASSERT_EQUAL_STRING("keys", aText);

    JonesInstallTest_BuildPath(aPath, sizeof(aPath), aTarget, "Jones3D.chm");
    TEST_ASSERT_EQUAL_size_t(4, JonesInstallTest_ReadWhole(aPath, aText, sizeof(aText)));
    TEST_ASSERT_EQUAL_STRING("help", aText);

    JonesInstallTest_CheckNoPartFiles(aTarget);
}

TEST(JonesInstall, TestRunSkipsSameSizeDestination)
{
    char aSource[JONESINSTALL_MAX_PATH];
    char aResource[JONESINSTALL_MAX_PATH];
    char aTarget[JONESINSTALL_MAX_PATH];
    char aTargetResource[JONESINSTALL_MAX_PATH];
    char aPath[JONESINSTALL_MAX_PATH];
    char aText[32];

    JonesInstallTest_BuildPath(aSource, sizeof(aSource), g_aTempDir, "source");
    JonesInstallTest_BuildPath(aResource, sizeof(aResource), aSource, "Resource");
    JonesInstallTest_BuildPath(aTarget, sizeof(aTarget), g_aTempDir, "target");
    JonesInstallTest_BuildPath(aTargetResource, sizeof(aTargetResource), aTarget, "Resource");
    JonesInstallTest_WriteRequiredSet(aResource);
    JonesInstallTest_MakeDirs(aTargetResource);
    JonesInstallTest_WriteFile(aTargetResource, "CD1.GOB", "old", 3);
    JonesInstallTest_WriteFile(aTargetResource, "CD2.GOB", "old", 3);
    JonesInstallTest_WriteFile(aTargetResource, "Jones3D.GOB", "old", 3);

    JonesInstallPlan plan;
    TEST_ASSERT_TRUE(JonesInstall_BuildPlan(JONESINSTALL_PLATFORM_LINUX, aSource, aTarget, false,
        &plan));

    JonesInstallHost host;
    JonesInstallTest_MakeHost(&host, JONESINSTALL_PLATFORM_LINUX, NULL);

    JonesInstallRunOutcome outcome;
    TEST_ASSERT_EQUAL_INT(JONESINSTALL_RUN_OK,
        JonesInstall_Run(&plan, &host, JonesInstallTest_Progress, NULL, NULL, &outcome));
    TEST_ASSERT_EQUAL_INT64(plan.totalBytes, g_lastDone);

    JonesInstallTest_BuildPath(aPath, sizeof(aPath), aTargetResource, "CD1.GOB");
    TEST_ASSERT_EQUAL_size_t(3, JonesInstallTest_ReadWhole(aPath, aText, sizeof(aText)));
    TEST_ASSERT_EQUAL_STRING("old", aText);
}

TEST(JonesInstall, TestRunCancelsMidway)
{
    char aSource[JONESINSTALL_MAX_PATH];
    char aResource[JONESINSTALL_MAX_PATH];
    char aTarget[JONESINSTALL_MAX_PATH];
    char aTargetResource[JONESINSTALL_MAX_PATH];
    char aPath[JONESINSTALL_MAX_PATH];
    char* pData = (char*)malloc(262144);

    TEST_ASSERT_NOT_NULL(pData);
    memset(pData, 'a', 262144);

    JonesInstallTest_BuildPath(aSource, sizeof(aSource), g_aTempDir, "source");
    JonesInstallTest_BuildPath(aResource, sizeof(aResource), aSource, "Resource");
    JonesInstallTest_BuildPath(aTarget, sizeof(aTarget), g_aTempDir, "target");
    JonesInstallTest_BuildPath(aTargetResource, sizeof(aTargetResource), aTarget, "Resource");
    JonesInstallTest_MakeDirs(aResource);
    JonesInstallTest_WriteFile(aResource, "CD1.GOB", pData, 262144);
    free(pData);

    JonesInstallPlan plan;
    TEST_ASSERT_TRUE(JonesInstall_BuildPlan(JONESINSTALL_PLATFORM_LINUX, aSource, aTarget, false,
        &plan));

    bool bCancel = false;
    g_bCancelAtProgress = true;
    g_pbCancelTarget    = &bCancel;

    JonesInstallHost host;
    JonesInstallTest_MakeHost(&host, JONESINSTALL_PLATFORM_LINUX, NULL);

    JonesInstallRunOutcome outcome;
    TEST_ASSERT_EQUAL_INT(JONESINSTALL_RUN_CANCELLED,
        JonesInstall_Run(&plan, &host, JonesInstallTest_Progress, NULL, &bCancel, &outcome));
    TEST_ASSERT_NOT_NULL(strstr(outcome.aMessage, "cancel"));

    JonesInstallTest_BuildPath(aPath, sizeof(aPath), aTargetResource, "CD1.GOB");
    TEST_ASSERT_FALSE(JonesInstallTest_FileExists(aPath));
    JonesInstallTest_CheckNoPartFiles(aTarget);
}

TEST(JonesInstall, TestRunErrorNamesFile)
{
    char aSource[JONESINSTALL_MAX_PATH];
    char aResource[JONESINSTALL_MAX_PATH];
    char aTarget[JONESINSTALL_MAX_PATH];
    char aTargetResource[JONESINSTALL_MAX_PATH];
    char aDestinationDir[JONESINSTALL_MAX_PATH];

    JonesInstallTest_BuildPath(aSource, sizeof(aSource), g_aTempDir, "source");
    JonesInstallTest_BuildPath(aResource, sizeof(aResource), aSource, "Resource");
    JonesInstallTest_BuildPath(aTarget, sizeof(aTarget), g_aTempDir, "target");
    JonesInstallTest_BuildPath(aTargetResource, sizeof(aTargetResource), aTarget, "Resource");
    JonesInstallTest_BuildPath(aDestinationDir, sizeof(aDestinationDir), aTargetResource,
        "CD1.GOB");
    JonesInstallTest_MakeDirs(aResource);
    JonesInstallTest_WriteFile(aResource, "CD1.GOB", "cd1", 3);
    JonesInstallTest_MakeDirs(aDestinationDir);

    JonesInstallPlan plan;
    TEST_ASSERT_TRUE(JonesInstall_BuildPlan(JONESINSTALL_PLATFORM_LINUX, aSource, aTarget, false,
        &plan));

    JonesInstallHost host;
    JonesInstallTest_MakeHost(&host, JONESINSTALL_PLATFORM_LINUX, NULL);

    JonesInstallRunOutcome outcome;
    TEST_ASSERT_EQUAL_INT(JONESINSTALL_RUN_ERROR,
        JonesInstall_Run(&plan, &host, NULL, NULL, NULL, &outcome));
    TEST_ASSERT_NOT_NULL(strstr(outcome.aMessage, "CD1.GOB"));
    JonesInstallTest_CheckNoPartFiles(aTarget);
}

TEST(JonesInstall, TestRunRefusesLowFreeSpace)
{
    char aSource[JONESINSTALL_MAX_PATH];
    char aResource[JONESINSTALL_MAX_PATH];
    char aTarget[JONESINSTALL_MAX_PATH];
    char aTargetResource[JONESINSTALL_MAX_PATH];

    JonesInstallTest_BuildPath(aSource, sizeof(aSource), g_aTempDir, "source");
    JonesInstallTest_BuildPath(aResource, sizeof(aResource), aSource, "Resource");
    JonesInstallTest_BuildPath(aTarget, sizeof(aTarget), g_aTempDir, "target");
    JonesInstallTest_BuildPath(aTargetResource, sizeof(aTargetResource), aTarget, "Resource");
    JonesInstallTest_WriteRequiredSet(aResource);

    JonesInstallPlan plan;
    TEST_ASSERT_TRUE(JonesInstall_BuildPlan(JONESINSTALL_PLATFORM_LINUX, aSource, aTarget, false,
        &plan));

    g_bFreeBytesKnown = true;
    g_freeBytes       = plan.totalBytes > 0 ? plan.totalBytes - 1 : 0;

    JonesInstallHost host;
    JonesInstallTest_MakeHost(&host, JONESINSTALL_PLATFORM_LINUX, NULL);

    JonesInstallRunOutcome outcome;
    TEST_ASSERT_EQUAL_INT(JONESINSTALL_RUN_ERROR,
        JonesInstall_Run(&plan, &host, NULL, NULL, NULL, &outcome));
    TEST_ASSERT_NOT_NULL(strstr(outcome.aMessage, "free disk space"));

    char aPath[JONESINSTALL_MAX_PATH];
    JonesInstallTest_BuildPath(aPath, sizeof(aPath), aTargetResource, "Jones3D.GOB");
    TEST_ASSERT_FALSE(JonesInstallTest_FileExists(aPath));
}

TEST(JonesInstall, TestCheckTargetReportsMissingFiles)
{
    char aTarget[JONESINSTALL_MAX_PATH];
    char aResource[JONESINSTALL_MAX_PATH];
    char aCd1[JONESINSTALL_MAX_PATH];

    JonesInstallTest_BuildPath(aTarget, sizeof(aTarget), g_aTempDir, "target");
    JonesInstallTest_BuildPath(aResource, sizeof(aResource), aTarget, "Resource");
    JonesInstallTest_WriteRequiredSet(aResource);

    JonesDataReport report;
    TEST_ASSERT_TRUE(JonesInstall_CheckTarget(aTarget, &report));
    TEST_ASSERT_TRUE(report.bComplete);

    JonesInstallTest_BuildPath(aCd1, sizeof(aCd1), aResource, "CD1.GOB");
    remove(aCd1);

    TEST_ASSERT_FALSE(JonesInstall_CheckTarget(aTarget, &report));
    TEST_ASSERT_FALSE(report.bComplete);
    TEST_ASSERT_EQUAL_size_t(1, report.numMissingRequired);
}

TEST(JonesInstall, TestInstallDataUsesEngine)
{
    char aSource[JONESINSTALL_MAX_PATH];
    char aResource[JONESINSTALL_MAX_PATH];
    char aCog[JONESINSTALL_MAX_PATH];
    char aKeySets[JONESINSTALL_MAX_PATH];
    char aTarget[JONESINSTALL_MAX_PATH];
    char aTargetResource[JONESINSTALL_MAX_PATH];
    char aPath[JONESINSTALL_MAX_PATH];
    char aText[32];

    JonesInstallTest_BuildPath(aSource, sizeof(aSource), g_aTempDir, "source");
    JonesInstallTest_BuildPath(aResource, sizeof(aResource), aSource, "Resource");
    JonesInstallTest_BuildPath(aCog, sizeof(aCog), aResource, "Cog");
    JonesInstallTest_BuildPath(aKeySets, sizeof(aKeySets), aSource, "KeySets");
    JonesInstallTest_BuildPath(aTarget, sizeof(aTarget), g_aTempDir, "target");
    JonesInstallTest_BuildPath(aTargetResource, sizeof(aTargetResource), aTarget, "Resource");
    JonesInstallTest_WriteRequiredSet(aResource);
    JonesInstallTest_MakeDirs(aCog);
    JonesInstallTest_WriteFile(aCog, "fix.cog", "fix", 3);
    JonesInstallTest_MakeDirs(aKeySets);
    JonesInstallTest_WriteFile(aKeySets, "default.kfg", "keys", 4);

    TEST_ASSERT_EQUAL_INT(0, JonesDataLocate_InstallDataTo(aSource, aTarget));

    JonesInstallTest_BuildPath(aPath, sizeof(aPath), aTargetResource, "Jones3D.GOB");
    TEST_ASSERT_EQUAL_size_t(5, JonesInstallTest_ReadWhole(aPath, aText, sizeof(aText)));
    TEST_ASSERT_EQUAL_STRING("jones", aText);

    JonesInstallTest_BuildPath(aPath, sizeof(aPath), aTargetResource, "Cog/fix.cog");
    TEST_ASSERT_EQUAL_size_t(3, JonesInstallTest_ReadWhole(aPath, aText, sizeof(aText)));
    TEST_ASSERT_EQUAL_STRING("fix", aText);

    JonesInstallTest_BuildPath(aPath, sizeof(aPath), aTarget, "KeySets/default.kfg");
    TEST_ASSERT_EQUAL_size_t(4, JonesInstallTest_ReadWhole(aPath, aText, sizeof(aText)));
    TEST_ASSERT_EQUAL_STRING("keys", aText);
}

TEST_GROUP_RUNNER(JonesInstall)
{
    RUN_TEST_CASE(JonesInstall, TestFindSteamSourceLinux);
    RUN_TEST_CASE(JonesInstall, TestFindSteamLibraryFolders);
    RUN_TEST_CASE(JonesInstall, TestFindGogInstalled);
    RUN_TEST_CASE(JonesInstall, TestFindGogInstaller);
    RUN_TEST_CASE(JonesInstall, TestFindDiscsFromVolumeBase);
    RUN_TEST_CASE(JonesInstall, TestFindWindowsStylePaths);
    RUN_TEST_CASE(JonesInstall, TestGetFolderSource);
    RUN_TEST_CASE(JonesInstall, TestBuildPlanContents);
    RUN_TEST_CASE(JonesInstall, TestBuildPlanDoesNotOverwriteSaves);
    RUN_TEST_CASE(JonesInstall, TestBuildPlanDisc1NeedsDisc2);
    RUN_TEST_CASE(JonesInstall, TestRunCopiesFilesAndReportsProgress);
    RUN_TEST_CASE(JonesInstall, TestRunSkipsSameSizeDestination);
    RUN_TEST_CASE(JonesInstall, TestRunCancelsMidway);
    RUN_TEST_CASE(JonesInstall, TestRunErrorNamesFile);
    RUN_TEST_CASE(JonesInstall, TestRunRefusesLowFreeSpace);
    RUN_TEST_CASE(JonesInstall, TestCheckTargetReportsMissingFiles);
    RUN_TEST_CASE(JonesInstall, TestInstallDataUsesEngine);
}
