// Added: regression test for JonesFile_OpenResource. It replaces the platform directory search
// with two GOB names, so the test controls exactly which files are opened and only needs tiny fake
// GOB headers. The test runs on the hosts that build the POSIX/Null file utilities.
#include <unity_fixture.h>

#include <Jones3D/Main/JonesFile.h>
#include <Jones3D/types.h>

#include <std/General/std.h>
#include <std/General/stdFileUtil.h>
#include <std/General/stdMemory.h>
#include <std/General/stdUtil.h>

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
#define JONESFILE_TEST_MKDIR(path) _mkdir(path)
#define JONESFILE_TEST_RMDIR(path) _rmdir(path)
#define JONESFILE_TEST_GETPID _getpid
#else
#include <dirent.h>
#include <unistd.h>
#define JONESFILE_TEST_MKDIR(path) mkdir((path), 0700)
#define JONESFILE_TEST_RMDIR(path) rmdir(path)
#define JONESFILE_TEST_GETPID getpid
#endif

#define JONESFILE_TEST_MAX_PATH 1024

extern JonesResource JonesFile_aResources[4];
extern bool JonesFile_bOpened;

static char g_aTempDir[JONESFILE_TEST_MAX_PATH];
static unsigned int g_tempDirCounter;
static tHostServices g_hostServices;

TEST_GROUP(JonesFile);

static const char* JonesFileTest_GetEnv(const char* pName)
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

static const char* JonesFileTest_TempBase(void)
{
#ifdef _WIN32
    static char aBase[JONESFILE_TEST_MAX_PATH];

    DWORD len = GetTempPathA((DWORD)sizeof(aBase), aBase);
    if ( len > 0 && (size_t)len < sizeof(aBase) )
    {
        while ( len > 1 && (aBase[len - 1] == '\\' || aBase[len - 1] == '/') )
        {
            aBase[--len] = '\0';
        }

        return aBase;
    }

    const char* pBase = JonesFileTest_GetEnv("TEMP");
    return pBase && pBase[0] ? pBase : "C:\\Windows\\Temp";
#else
    const char* pBase = JonesFileTest_GetEnv("TMPDIR");
    if ( !pBase || pBase[0] == '\0' )
    {
        pBase = JonesFileTest_GetEnv("TEMP");
    }
    if ( !pBase || pBase[0] == '\0' )
    {
        pBase = JonesFileTest_GetEnv("TMP");
    }
    if ( !pBase || pBase[0] == '\0' )
    {
        pBase = "/tmp";
    }

    return pBase;
#endif
}

static int JonesFileTest_MakeTempDir(void)
{
    const char* pBase = JonesFileTest_TempBase();

    for ( unsigned int attempt = 0; attempt < 1000; ++attempt )
    {
        int len = snprintf(g_aTempDir, sizeof(g_aTempDir), "%s/JonesFileTest-%ld-%u",
            pBase, (long)JONESFILE_TEST_GETPID(), ++g_tempDirCounter);
        if ( len > 0 && (size_t)len < sizeof(g_aTempDir)
          && JONESFILE_TEST_MKDIR(g_aTempDir) == 0 )
        {
            return 0;
        }
    }

    g_aTempDir[0] = '\0';
    return -1;
}

static bool JonesFileTest_IsDirectory(const char* pPath)
{
#ifdef _WIN32
    struct _stat st;
    return _stat(pPath, &st) == 0 && (st.st_mode & _S_IFDIR) != 0;
#else
    struct stat st;
    return stat(pPath, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static void JonesFileTest_RemoveTree(const char* pDir)
{
    char aEntries[64][512];
    size_t numEntries = 0;

#ifdef _WIN32
    char aPattern[JONESFILE_TEST_MAX_PATH];
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
        char aPath[JONESFILE_TEST_MAX_PATH];
        int len = snprintf(aPath, sizeof(aPath), "%s/%s", pDir, aEntries[i]);
        if ( len <= 0 || (size_t)len >= sizeof(aPath) )
        {
            continue;
        }

        if ( JonesFileTest_IsDirectory(aPath) )
        {
            JonesFileTest_RemoveTree(aPath);
        }
        else
        {
            remove(aPath);
        }
    }

    JONESFILE_TEST_RMDIR(pDir);
}

static void JonesFileTest_BuildPath(char* pOut, size_t outSize, const char* pDir,
    const char* pName)
{
    int len = snprintf(pOut, outSize, "%s/%s", pDir, pName);
    TEST_ASSERT_TRUE(len > 0 && (size_t)len < outSize);
}

static void JonesFileTest_MakeDirs(const char* pPath)
{
    char aPath[JONESFILE_TEST_MAX_PATH];
    size_t len = strlen(pPath);
    TEST_ASSERT_TRUE(len + 1 < sizeof(aPath));
    memcpy(aPath, pPath, len + 1);

    for ( size_t i = 1; aPath[i] != '\0'; ++i )
    {
        if ( aPath[i] != '/' && aPath[i] != '\\' )
        {
            continue;
        }

        char ch = aPath[i];
        aPath[i] = '\0';
        if ( aPath[0] != '\0' )
        {
            JONESFILE_TEST_MKDIR(aPath);
        }
        aPath[i] = ch;
    }

    JONESFILE_TEST_MKDIR(aPath);
}

static void JonesFileTest_WriteFakeGob(const char* pPath)
{
    GobFileHeader header;
    tGobFileEntry entry;
    uint32_t numEntries = 1;
    uint8_t payload     = 0x5A;
    uint8_t aData[sizeof(header) + sizeof(payload) + sizeof(numEntries) + sizeof(entry)];

    memset(&header, 0, sizeof(header));
    memset(&entry, 0, sizeof(entry));

    memcpy(header.signature, "GOB ", 4u);
    header.version   = 20;
    header.dirOffset = (int)(sizeof(header) + sizeof(payload));

    entry.offset = (int)sizeof(header);
    entry.size   = (int)sizeof(payload);
    snprintf(entry.aName, sizeof(entry.aName), "%s", "dummy");

    size_t offset = 0;
    memcpy(&aData[offset], &header, sizeof(header));
    offset += sizeof(header);
    memcpy(&aData[offset], &payload, sizeof(payload));
    offset += sizeof(payload);
    memcpy(&aData[offset], &numEntries, sizeof(numEntries));
    offset += sizeof(numEntries);
    memcpy(&aData[offset], &entry, sizeof(entry));
    offset += sizeof(entry);

    FILE* pFile = NULL;
    TEST_ASSERT_EQUAL_INT(0, fopen_s(&pFile, pPath, "wb"));
    TEST_ASSERT_NOT_NULL(pFile);
    TEST_ASSERT_EQUAL_size_t(offset, fwrite(aData, 1, offset, pFile));
    TEST_ASSERT_EQUAL_INT(0, fclose(pFile));
}

static void* J3DAPI JonesFileTest_Malloc(size_t size)
{
    return malloc(size);
}

static void J3DAPI JonesFileTest_Free(void* pData)
{
    free(pData);
}

static void* J3DAPI JonesFileTest_Realloc(void* pData, size_t size)
{
    return realloc(pData, size);
}

static int J3DAPI JonesFileTest_Print(const char* pFormat, ...)
{
    va_list args;
    va_start(args, pFormat);
    int result = vfprintf(stderr, pFormat, args);
    va_end(args);
    return result;
}

static tFileHandle J3DAPI JonesFileTest_FileOpen(const char* pFilename, const char* mode)
{
    FILE* pFile = NULL;
    if ( fopen_s(&pFile, pFilename, mode) != 0 )
    {
        return 0;
    }

    return (tFileHandle)(uintptr_t)pFile;
}

static int J3DAPI JonesFileTest_FileClose(tFileHandle fh)
{
    return fclose((FILE*)(uintptr_t)fh);
}

static size_t J3DAPI JonesFileTest_FileRead(tFileHandle fh, void* pDst, size_t size)
{
    return fread(pDst, 1, size, (FILE*)(uintptr_t)fh);
}

static char* J3DAPI JonesFileTest_FileGets(tFileHandle fh, char* pStr, size_t size)
{
    return fgets(pStr, (int)size, (FILE*)(uintptr_t)fh);
}

static size_t J3DAPI JonesFileTest_FileWrite(tFileHandle fh, const void* pData, size_t size)
{
    return fwrite(pData, 1, size, (FILE*)(uintptr_t)fh);
}

static int J3DAPI JonesFileTest_FileEOF(tFileHandle fh)
{
    return feof((FILE*)(uintptr_t)fh);
}

static int J3DAPI JonesFileTest_FileTell(tFileHandle fh)
{
    return (int)ftell((FILE*)(uintptr_t)fh);
}

static int J3DAPI JonesFileTest_FileSeek(tFileHandle fh, int offset, int origin)
{
    return fseek((FILE*)(uintptr_t)fh, offset, origin);
}

static size_t J3DAPI JonesFileTest_FileSize(const char* pFilename)
{
    FILE* pFile = NULL;
    TEST_ASSERT_EQUAL_INT(0, fopen_s(&pFile, pFilename, "rb"));
    if ( !pFile )
    {
        return 0;
    }

    if ( fseek(pFile, 0, SEEK_END) != 0 )
    {
        fclose(pFile);
        return 0;
    }

    long size = ftell(pFile);
    fclose(pFile);
    return size > 0 ? (size_t)size : 0;
}

static int JonesFileTest_FilePrintf(tFileHandle fh, const char* pFormat, ...)
{
    va_list args;
    va_start(args, pFormat);
    int result = vfprintf((FILE*)(uintptr_t)fh, pFormat, args);
    va_end(args);
    return result;
}

// Added: the directory search test double. It returns the two GOB names only for the Resource
// search, so the install path itself has no GOBs. JonesFile_OpenResource still builds every path
// and opens each file through stdGob, which is what the regression covers.
int J3DAPI stdFileUtil_FindNext(FindFileData* pData, tFoundFileInfo* pFileInfo)
{
    static const char* const aNames[] = { "CD1.GOB", "CD2.GOB" };

    if ( !pData || !pFileInfo || !strstr(pData->aSearchFilter, "Resource") )
    {
        return 0;
    }

    if ( pData->nFoundFiles >= (int)STD_ARRAYLEN(aNames) )
    {
        return 0;
    }

    snprintf(pFileInfo->aName, sizeof(pFileInfo->aName), "%s", aNames[pData->nFoundFiles]);
    pFileInfo->bIsDirectory = 0;
    pFileInfo->lastChanged  = 0;
    ++pData->nFoundFiles;
    return 1;
}

void J3DAPI stdFileUtil_DisposeFind(FindFileData* pData)
{
    if ( pData )
    {
        STDFREE(pData);
    }
}

TEST_SETUP(JonesFile)
{
    TEST_ASSERT_EQUAL_INT(0, JonesFileTest_MakeTempDir());
    memset(&g_hostServices, 0, sizeof(g_hostServices));
    g_hostServices.pStatusPrint  = JonesFileTest_Print;
    g_hostServices.pMessagePrint = JonesFileTest_Print;
    g_hostServices.pWarningPrint = JonesFileTest_Print;
    g_hostServices.pErrorPrint   = JonesFileTest_Print;
    g_hostServices.pDebugPrint   = JonesFileTest_Print;
    g_hostServices.pMalloc       = JonesFileTest_Malloc;
    g_hostServices.pFree         = JonesFileTest_Free;
    g_hostServices.pRealloc      = JonesFileTest_Realloc;
    g_hostServices.pFileOpen     = JonesFileTest_FileOpen;
    g_hostServices.pFileClose    = JonesFileTest_FileClose;
    g_hostServices.pFileRead     = JonesFileTest_FileRead;
    g_hostServices.pFileGets     = JonesFileTest_FileGets;
    g_hostServices.pFileWrite    = JonesFileTest_FileWrite;
    g_hostServices.pFileEOF      = JonesFileTest_FileEOF;
    g_hostServices.pFileTell     = JonesFileTest_FileTell;
    g_hostServices.pFileSeek     = JonesFileTest_FileSeek;
    g_hostServices.pFileSize     = JonesFileTest_FileSize;
    g_hostServices.pFilePrintf   = JonesFileTest_FilePrintf;
    std_g_pHS = &g_hostServices;
}

TEST_TEAR_DOWN(JonesFile)
{
    if ( JonesFile_bOpened )
    {
        JonesFile_Close();
    }

    JonesFile_Shutdown();
    std_g_pHS = NULL;

    if ( g_aTempDir[0] != '\0' )
    {
        JonesFileTest_RemoveTree(g_aTempDir);
        g_aTempDir[0] = '\0';
    }
}

TEST(JonesFile, TestOpenResourceKeepsEachGobPath)
{
    char aInstallDir[JONESFILE_TEST_MAX_PATH];
    char aResourceDir[JONESFILE_TEST_MAX_PATH];
    char aGobPath[JONESFILE_TEST_MAX_PATH];

    JonesFileTest_BuildPath(aInstallDir, sizeof(aInstallDir), g_aTempDir, "install");
    JonesFileTest_BuildPath(aResourceDir, sizeof(aResourceDir), aInstallDir, "Resource");
    JonesFileTest_MakeDirs(aResourceDir);

    JonesFileTest_BuildPath(aGobPath, sizeof(aGobPath), aResourceDir, "CD1.GOB");
    JonesFileTest_WriteFakeGob(aGobPath);
    JonesFileTest_BuildPath(aGobPath, sizeof(aGobPath), aResourceDir, "CD2.GOB");
    JonesFileTest_WriteFakeGob(aGobPath);

    JonesFile_Startup(&g_hostServices);
    TEST_ASSERT_EQUAL_INT(0, JonesFile_Open(&g_hostServices, aInstallDir, NULL));

    TEST_ASSERT_EQUAL_size_t(2, JonesFile_aResources[3].numGobFiles);
}

TEST_GROUP_RUNNER(JonesFile)
{
    RUN_TEST_CASE(JonesFile, TestOpenResourceKeepsEachGobPath);
}
