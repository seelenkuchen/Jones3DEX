#include <unity_fixture.h>

#include <Jones3D/Main/JonesData.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#include <windows.h>
#define JONESDATA_TEST_MKDIR(path) _mkdir(path)
#define JONESDATA_TEST_RMDIR(path) _rmdir(path)
#define JONESDATA_TEST_GETPID _getpid
#else
#include <dirent.h>
#include <unistd.h>
#define JONESDATA_TEST_MKDIR(path) mkdir((path), 0700)
#define JONESDATA_TEST_RMDIR(path) rmdir(path)
#define JONESDATA_TEST_GETPID getpid
#endif

// Added: NIST/FIPS 180-2 test vectors. The first three are the FIPS examples; the last is the
// "one million a" vector from NIST's SHA-256 examples.
#define JONESDATA_TEST_HASH_EMPTY   "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
#define JONESDATA_TEST_HASH_ABC     "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
#define JONESDATA_TEST_HASH_448     "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"
#define JONESDATA_TEST_HASH_MILLION "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"

#define JONESDATA_TEST_448_MESSAGE "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"

#define JONESDATA_TEST_MAX_NAME    512
#define JONESDATA_TEST_MAX_ENTRIES 64

static const JonesDataRelease JonesDataTest_aReleases[] =
{
    {
        "Test release",
        {
            { "Jones3D.GOB",  0,                                                 JONESDATA_TEST_HASH_EMPTY },
            { "CD1.GOB",      3,                                                 JONESDATA_TEST_HASH_ABC },
            { "CD2.GOB",      (int64_t)(sizeof(JONESDATA_TEST_448_MESSAGE) - 1), JONESDATA_TEST_HASH_448 },
            { "jonesopn.snm", 3,                                                 JONESDATA_TEST_HASH_ABC },
        },
        4
    },
};

// Each row belongs to one of the known releases, but neither release has all three required files.
static const JonesDataRelease JonesDataTest_aMixedReleases[] =
{
    {
        "Release A",
        {
            { "Jones3D.GOB", 0, JONESDATA_TEST_HASH_EMPTY },
            { "CD2.GOB",     (int64_t)(sizeof(JONESDATA_TEST_448_MESSAGE) - 1), JONESDATA_TEST_HASH_448 },
        },
        2
    },
    {
        "Release B",
        {
            { "CD1.GOB", 3, JONESDATA_TEST_HASH_ABC },
            { "CD2.GOB", (int64_t)(sizeof(JONESDATA_TEST_448_MESSAGE) - 1), JONESDATA_TEST_HASH_448 },
        },
        2
    },
};

static char g_aTempDir[JONESDATA_MAX_PATH];
static unsigned int g_tempDirCounter;

TEST_GROUP(JonesData);

static const char* JonesDataTest_GetEnv(const char* pName)
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

#ifdef _WIN32
static const char* JonesDataTest_TempBase(void)
{
    static char aBase[JONESDATA_MAX_PATH];

    DWORD len = GetTempPathA((DWORD)sizeof(aBase), aBase);
    if ( len > 0 && (size_t)len < sizeof(aBase) )
    {
        while ( len > 1 && (aBase[len - 1] == '\\' || aBase[len - 1] == '/') )
        {
            aBase[--len] = '\0';
        }
        return aBase;
    }

    const char* pBase = JonesDataTest_GetEnv("TEMP");
    if ( pBase && pBase[0] != '\0' )
    {
        return pBase;
    }

    return "C:\\Windows\\Temp";
}
#else
static const char* JonesDataTest_TempBase(void)
{
    const char* pBase = JonesDataTest_GetEnv("TMPDIR");
    if ( !pBase || pBase[0] == '\0' )
    {
        pBase = JonesDataTest_GetEnv("TEMP");
    }
    if ( !pBase || pBase[0] == '\0' )
    {
        pBase = JonesDataTest_GetEnv("TMP");
    }
    if ( !pBase || pBase[0] == '\0' )
    {
        pBase = "/tmp";
    }

    return pBase;
}
#endif

static int JonesDataTest_MakeTempDir(void)
{
    const char* pBase = JonesDataTest_TempBase();

    for ( unsigned int attempt = 0; attempt < 1000; ++attempt )
    {
        int len = snprintf(g_aTempDir, sizeof(g_aTempDir), "%s/JonesDataTest-%ld-%u",
            pBase, (long)JONESDATA_TEST_GETPID(), ++g_tempDirCounter);
        if ( len > 0 && (size_t)len < sizeof(g_aTempDir) && JONESDATA_TEST_MKDIR(g_aTempDir) == 0 )
        {
            return 0;
        }
    }

    g_aTempDir[0] = '\0';
    return -1;
}

static void JonesDataTest_BuildPath(char* pOut, size_t outSize, const char* pDir, const char* pName)
{
    int len = snprintf(pOut, outSize, "%s/%s", pDir, pName);
    TEST_ASSERT_TRUE(len > 0 && (size_t)len < outSize);
}

static void JonesDataTest_RemoveDirContents(const char* pDir)
{
    char aEntries[JONESDATA_TEST_MAX_ENTRIES][JONESDATA_TEST_MAX_NAME];
    size_t numEntries = 0;

#ifdef _WIN32
    char aPattern[JONESDATA_MAX_PATH];
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
    } while ( numEntries < JONESDATA_TEST_MAX_ENTRIES && FindNextFileA(hFind, &data) );

    FindClose(hFind);
#else
    DIR* pDirHandle = opendir(pDir);
    if ( !pDirHandle )
    {
        return;
    }

    struct dirent* pEntry;
    while ( numEntries < JONESDATA_TEST_MAX_ENTRIES && (pEntry = readdir(pDirHandle)) != NULL )
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
        char aPath[JONESDATA_MAX_PATH];
        int len = snprintf(aPath, sizeof(aPath), "%s/%s", pDir, aEntries[i]);
        if ( len > 0 && (size_t)len < sizeof(aPath) )
        {
            remove(aPath);
        }
    }
}

static void JonesDataTest_RemoveTree(void)
{
    if ( g_aTempDir[0] == '\0' )
    {
        return;
    }

    JonesDataTest_RemoveDirContents(g_aTempDir);

    static const char* const aSubDirs[] = { "Resource", "resource" };
    for ( size_t i = 0; i < sizeof(aSubDirs) / sizeof(aSubDirs[0]); ++i )
    {
        char aSub[JONESDATA_MAX_PATH];
        int len = snprintf(aSub, sizeof(aSub), "%s/%s", g_aTempDir, aSubDirs[i]);
        if ( len > 0 && (size_t)len < sizeof(aSub) )
        {
            JonesDataTest_RemoveDirContents(aSub);
            JONESDATA_TEST_RMDIR(aSub);
        }
    }

    JONESDATA_TEST_RMDIR(g_aTempDir);
    g_aTempDir[0] = '\0';
}

static void JonesDataTest_MakeSubdir(const char* pName)
{
    char aPath[JONESDATA_MAX_PATH];
    JonesDataTest_BuildPath(aPath, sizeof(aPath), g_aTempDir, pName);
    TEST_ASSERT_EQUAL_INT(0, JONESDATA_TEST_MKDIR(aPath));
}

static void JonesDataTest_WriteFile(const char* pDir, const char* pName, const void* pData, size_t size)
{
    char aPath[JONESDATA_MAX_PATH];
    JonesDataTest_BuildPath(aPath, sizeof(aPath), pDir, pName);

    FILE* pFile = NULL;
    TEST_ASSERT_EQUAL_INT(0, fopen_s(&pFile, aPath, "wb"));
    TEST_ASSERT_NOT_NULL(pFile);

    if ( size > 0 )
    {
        TEST_ASSERT_EQUAL_size_t(size, fwrite(pData, 1, size, pFile));
    }

    TEST_ASSERT_EQUAL_INT(0, fclose(pFile));
}

static void JonesDataTest_WriteRepeated(const char* pDir, const char* pName, char value, size_t count)
{
    char aPath[JONESDATA_MAX_PATH];
    JonesDataTest_BuildPath(aPath, sizeof(aPath), pDir, pName);

    FILE* pFile = NULL;
    TEST_ASSERT_EQUAL_INT(0, fopen_s(&pFile, aPath, "wb"));
    TEST_ASSERT_NOT_NULL(pFile);

    char aBuffer[4096];
    memset(aBuffer, value, sizeof(aBuffer));

    size_t remaining = count;
    while ( remaining > 0 )
    {
        size_t chunk = remaining < sizeof(aBuffer) ? remaining : sizeof(aBuffer);
        TEST_ASSERT_EQUAL_size_t(chunk, fwrite(aBuffer, 1, chunk, pFile));
        remaining -= chunk;
    }

    TEST_ASSERT_EQUAL_INT(0, fclose(pFile));
}

static size_t JonesDataTest_ReadWhole(const char* pPath, char* pOut, size_t outSize)
{
    FILE* pFile = NULL;
    TEST_ASSERT_EQUAL_INT(0, fopen_s(&pFile, pPath, "rb"));
    TEST_ASSERT_NOT_NULL(pFile);

    size_t numRead = fread(pOut, 1, outSize - 1, pFile);
    pOut[numRead] = '\0';
    TEST_ASSERT_EQUAL_INT(0, fclose(pFile));
    return numRead;
}

static void JonesDataTest_StatPath(const char* pPath, int64_t* pSize, int64_t* pMtime)
{
#ifdef _WIN32
    struct _stat64 st;
    TEST_ASSERT_EQUAL_INT(0, _stat64(pPath, &st));
#else
    struct stat st;
    TEST_ASSERT_EQUAL_INT(0, stat(pPath, &st));
#endif

    *pSize  = (int64_t)st.st_size;
    *pMtime = (int64_t)st.st_mtime;
}

// The caller has created "jones3d.gob" in the temporary directory: when the other spelling does
// not exist, the volume is case-sensitive.
static bool JonesDataTest_IsCaseSensitive(void)
{
    char aPath[JONESDATA_MAX_PATH];
    JonesDataTest_BuildPath(aPath, sizeof(aPath), g_aTempDir, "JONES3D.GOB");

#ifdef _WIN32
    struct _stat64 st;
    return _stat64(aPath, &st) != 0;
#else
    struct stat st;
    return stat(aPath, &st) != 0;
#endif
}

static bool JonesDataTest_EndsWith(const char* pText, const char* pSuffix)
{
    size_t textLen   = strlen(pText);
    size_t suffixLen = strlen(pSuffix);
    return textLen >= suffixLen && strcmp(&pText[textLen - suffixLen], pSuffix) == 0;
}

static void JonesDataTest_WriteRequiredSet(void)
{
    JonesDataTest_WriteFile(g_aTempDir, "Jones3D.GOB", "", 0);
    JonesDataTest_WriteFile(g_aTempDir, "CD1.GOB", "abc", 3);
    JonesDataTest_WriteFile(g_aTempDir, "CD2.GOB", JONESDATA_TEST_448_MESSAGE, sizeof(JONESDATA_TEST_448_MESSAGE) - 1);
}

TEST_SETUP(JonesData)
{
    TEST_ASSERT_EQUAL_INT(0, JonesDataTest_MakeTempDir());
    JonesData_SetReleaseTable(JonesDataTest_aReleases, 1);
}

TEST_TEAR_DOWN(JonesData)
{
    JonesDataTest_RemoveTree();
    JonesData_SetReleaseTable(NULL, 0);
}

TEST(JonesData, TestSha256KnownAnswers)
{
    char aHash[JONESDATA_HASH_SIZE];
    char aPath[JONESDATA_MAX_PATH];

    JonesDataTest_WriteFile(g_aTempDir, "empty.bin", "", 0);
    JonesDataTest_BuildPath(aPath, sizeof(aPath), g_aTempDir, "empty.bin");
    TEST_ASSERT_TRUE(JonesData_Sha256File(aPath, aHash, sizeof(aHash)));
    TEST_ASSERT_EQUAL_STRING(JONESDATA_TEST_HASH_EMPTY, aHash);

    JonesDataTest_WriteFile(g_aTempDir, "abc.bin", "abc", 3);
    JonesDataTest_BuildPath(aPath, sizeof(aPath), g_aTempDir, "abc.bin");
    TEST_ASSERT_TRUE(JonesData_Sha256File(aPath, aHash, sizeof(aHash)));
    TEST_ASSERT_EQUAL_STRING(JONESDATA_TEST_HASH_ABC, aHash);

    JonesDataTest_WriteFile(g_aTempDir, "fips448.bin", JONESDATA_TEST_448_MESSAGE, sizeof(JONESDATA_TEST_448_MESSAGE) - 1);
    JonesDataTest_BuildPath(aPath, sizeof(aPath), g_aTempDir, "fips448.bin");
    TEST_ASSERT_TRUE(JonesData_Sha256File(aPath, aHash, sizeof(aHash)));
    TEST_ASSERT_EQUAL_STRING(JONESDATA_TEST_HASH_448, aHash);

    JonesDataTest_WriteRepeated(g_aTempDir, "million.bin", 'a', 1000000);
    JonesDataTest_BuildPath(aPath, sizeof(aPath), g_aTempDir, "million.bin");
    TEST_ASSERT_TRUE(JonesData_Sha256File(aPath, aHash, sizeof(aHash)));
    TEST_ASSERT_EQUAL_STRING(JONESDATA_TEST_HASH_MILLION, aHash);

    char aSmall[8] = "old";
    TEST_ASSERT_FALSE(JonesData_Sha256File(aPath, aSmall, sizeof(aSmall)));
    TEST_ASSERT_EQUAL_STRING("", aSmall);
}

TEST(JonesData, TestFindResourceDirDirect)
{
    char aDir[JONESDATA_MAX_PATH];

    JonesDataTest_WriteFile(g_aTempDir, "Jones3D.GOB", "", 0);
    TEST_ASSERT_TRUE(JonesData_FindResourceDir(g_aTempDir, aDir, sizeof(aDir)));
    TEST_ASSERT_EQUAL_STRING(g_aTempDir, aDir);
}

TEST(JonesData, TestFindResourceDirSubdirResource)
{
    char aDir[JONESDATA_MAX_PATH];
    char aExpected[JONESDATA_MAX_PATH];

    JonesDataTest_MakeSubdir("Resource");
    JonesDataTest_WriteFile(g_aTempDir, "Resource/Jones3D.GOB", "", 0);
    TEST_ASSERT_TRUE(JonesData_FindResourceDir(g_aTempDir, aDir, sizeof(aDir)));
    JonesDataTest_BuildPath(aExpected, sizeof(aExpected), g_aTempDir, "Resource");
    TEST_ASSERT_EQUAL_STRING(aExpected, aDir);
}

TEST(JonesData, TestFindResourceDirSubdirLower)
{
    char aDir[JONESDATA_MAX_PATH];
    char aExpected[JONESDATA_MAX_PATH];

    JonesDataTest_MakeSubdir("resource");
    JonesDataTest_WriteFile(g_aTempDir, "resource/Jones3D.GOB", "", 0);
    TEST_ASSERT_TRUE(JonesData_FindResourceDir(g_aTempDir, aDir, sizeof(aDir)));
    JonesDataTest_BuildPath(aExpected, sizeof(aExpected), g_aTempDir, "resource");
    if ( JonesDataTest_IsCaseSensitive() )
    {
        TEST_ASSERT_EQUAL_STRING(aExpected, aDir);
    }
    else
    {
        // Added: a case-insensitive file system finds the same folder through "Resource".
        TEST_ASSERT_EQUAL_INT(0, strcmpi(aExpected, aDir));
    }
}

TEST(JonesData, TestFindResourceDirNotFound)
{
    char aDir[JONESDATA_MAX_PATH] = "unchanged";

    TEST_ASSERT_FALSE(JonesData_FindResourceDir(g_aTempDir, aDir, sizeof(aDir)));
    TEST_ASSERT_EQUAL_STRING("", aDir);
}

TEST(JonesData, TestFindResourceDirTooSmall)
{
    char aDir[8] = "unset";

    JonesDataTest_WriteFile(g_aTempDir, "Jones3D.GOB", "", 0);
    TEST_ASSERT_FALSE(JonesData_FindResourceDir(g_aTempDir, aDir, sizeof(aDir)));
    TEST_ASSERT_EQUAL_STRING("", aDir);
}

TEST(JonesData, TestCheckDirKnownRelease)
{
    JonesDataReport report;

    JonesDataTest_WriteRequiredSet();
    TEST_ASSERT_TRUE(JonesData_CheckDir(g_aTempDir, NULL, &report));

    TEST_ASSERT_TRUE(report.bComplete);
    TEST_ASSERT_EQUAL_size_t(0, report.numMissingRequired);
    TEST_ASSERT_EQUAL_size_t(JONESDATA_MAX_FILES, report.numFiles);
    TEST_ASSERT_EQUAL_STRING("Test release", report.pRelease);

    TEST_ASSERT_EQUAL_STRING("Jones3D.GOB", report.aFiles[0].pName);
    TEST_ASSERT_TRUE(report.aFiles[0].bRequired);
    TEST_ASSERT_TRUE(report.aFiles[0].bPresent);
    TEST_ASSERT_EQUAL_INT64(0, report.aFiles[0].size);
    TEST_ASSERT_EQUAL_STRING(JONESDATA_TEST_HASH_EMPTY, report.aFiles[0].aSha256);
    TEST_ASSERT_TRUE(report.aFiles[0].bMatchesRelease);

    TEST_ASSERT_EQUAL_STRING("CD1.GOB", report.aFiles[1].pName);
    TEST_ASSERT_TRUE(report.aFiles[1].bPresent);
    TEST_ASSERT_EQUAL_INT64(3, report.aFiles[1].size);
    TEST_ASSERT_EQUAL_STRING(JONESDATA_TEST_HASH_ABC, report.aFiles[1].aSha256);
    TEST_ASSERT_TRUE(report.aFiles[1].bMatchesRelease);

    TEST_ASSERT_EQUAL_STRING("CD2.GOB", report.aFiles[2].pName);
    TEST_ASSERT_TRUE(report.aFiles[2].bPresent);
    TEST_ASSERT_EQUAL_STRING(JONESDATA_TEST_HASH_448, report.aFiles[2].aSha256);
    TEST_ASSERT_TRUE(report.aFiles[2].bMatchesRelease);

    TEST_ASSERT_EQUAL_STRING("jonesopn.snm", report.aFiles[3].pName);
    TEST_ASSERT_FALSE(report.aFiles[3].bRequired);
    TEST_ASSERT_FALSE(report.aFiles[3].bPresent);
    TEST_ASSERT_EQUAL_STRING("", report.aFiles[3].aSha256);
}

TEST(JonesData, TestCheckDirMissingRequired)
{
    JonesDataReport report;

    JonesDataTest_WriteFile(g_aTempDir, "Jones3D.GOB", "", 0);
    JonesDataTest_WriteFile(g_aTempDir, "CD1.GOB", "abc", 3);

    TEST_ASSERT_FALSE(JonesData_CheckDir(g_aTempDir, NULL, &report));
    TEST_ASSERT_FALSE(report.bComplete);
    TEST_ASSERT_EQUAL_size_t(1, report.numMissingRequired);
    TEST_ASSERT_NULL(report.pRelease);

    TEST_ASSERT_FALSE(report.aFiles[2].bPresent);
    TEST_ASSERT_EQUAL_STRING("", report.aFiles[2].aPath);
    TEST_ASSERT_EQUAL_STRING("", report.aFiles[2].aSha256);
    TEST_ASSERT_FALSE(report.aFiles[2].bMatchesRelease);
}

TEST(JonesData, TestCheckDirWrongContent)
{
    JonesDataReport report;

    JonesDataTest_WriteRequiredSet();
    JonesDataTest_WriteFile(g_aTempDir, "CD1.GOB", "abd", 3);

    TEST_ASSERT_TRUE(JonesData_CheckDir(g_aTempDir, NULL, &report));
    TEST_ASSERT_TRUE(report.bComplete);
    TEST_ASSERT_NULL(report.pRelease);

    TEST_ASSERT_TRUE(report.aFiles[1].bPresent);
    TEST_ASSERT_EQUAL_INT64(3, report.aFiles[1].size);
    TEST_ASSERT_EQUAL_size_t(JONESDATA_HASH_LENGTH, strlen(report.aFiles[1].aSha256));
    TEST_ASSERT_TRUE(strcmp(JONESDATA_TEST_HASH_ABC, report.aFiles[1].aSha256) != 0);
    TEST_ASSERT_FALSE(report.aFiles[1].bMatchesRelease);
}

TEST(JonesData, TestCheckDirCaseInsensitiveNames)
{
    JonesDataReport report;

    JonesDataTest_WriteFile(g_aTempDir, "jones3d.gob", "", 0);
    JonesDataTest_WriteFile(g_aTempDir, "CD1.GOB", "abc", 3);
    JonesDataTest_WriteFile(g_aTempDir, "cd2.gob", JONESDATA_TEST_448_MESSAGE, sizeof(JONESDATA_TEST_448_MESSAGE) - 1);

    TEST_ASSERT_TRUE(JonesData_CheckDir(g_aTempDir, NULL, &report));
    TEST_ASSERT_TRUE(report.bComplete);
    TEST_ASSERT_EQUAL_STRING("Test release", report.pRelease);
    TEST_ASSERT_TRUE(report.aFiles[0].bPresent);
    TEST_ASSERT_TRUE(report.aFiles[1].bPresent);
    TEST_ASSERT_TRUE(report.aFiles[2].bPresent);

    if ( JonesDataTest_IsCaseSensitive() )
    {
        TEST_ASSERT_TRUE(JonesDataTest_EndsWith(report.aFiles[0].aPath, "jones3d.gob"));
        TEST_ASSERT_TRUE(JonesDataTest_EndsWith(report.aFiles[2].aPath, "cd2.gob"));
    }
}

TEST(JonesData, TestCheckDirOptionalWrong)
{
    JonesDataReport report;

    JonesDataTest_WriteRequiredSet();
    JonesDataTest_WriteFile(g_aTempDir, "jonesopn.snm", "abd", 3);

    TEST_ASSERT_TRUE(JonesData_CheckDir(g_aTempDir, NULL, &report));
    TEST_ASSERT_TRUE(report.bComplete);
    TEST_ASSERT_NULL(report.pRelease);

    TEST_ASSERT_TRUE(report.aFiles[3].bPresent);
    TEST_ASSERT_EQUAL_INT64(3, report.aFiles[3].size);
    TEST_ASSERT_TRUE(strcmp(JONESDATA_TEST_HASH_ABC, report.aFiles[3].aSha256) != 0);
    TEST_ASSERT_FALSE(report.aFiles[3].bMatchesRelease);
}

TEST(JonesData, TestCheckDirMixedReleases)
{
    JonesDataReport report;

    JonesDataTest_WriteRequiredSet();
    JonesData_SetReleaseTable(JonesDataTest_aMixedReleases, 2);

    TEST_ASSERT_TRUE(JonesData_CheckDir(g_aTempDir, NULL, &report));
    TEST_ASSERT_TRUE(report.bComplete);
    TEST_ASSERT_NULL(report.pRelease);

    TEST_ASSERT_TRUE(report.aFiles[0].bMatchesRelease);
    TEST_ASSERT_TRUE(report.aFiles[1].bMatchesRelease);
    TEST_ASSERT_TRUE(report.aFiles[2].bMatchesRelease);
}

TEST(JonesData, TestCheckDirSizeMismatchNotHashed)
{
    JonesDataReport report;

    JonesDataTest_WriteRequiredSet();
    JonesDataTest_WriteFile(g_aTempDir, "CD1.GOB", "abcd", 4);

    TEST_ASSERT_TRUE(JonesData_CheckDir(g_aTempDir, NULL, &report));
    TEST_ASSERT_TRUE(report.bComplete);
    TEST_ASSERT_NULL(report.pRelease);

    TEST_ASSERT_TRUE(report.aFiles[1].bPresent);
    TEST_ASSERT_EQUAL_INT64(4, report.aFiles[1].size);
    TEST_ASSERT_EQUAL_STRING("", report.aFiles[1].aSha256);
    TEST_ASSERT_FALSE(report.aFiles[1].bMatchesRelease);
}

TEST(JonesData, TestCheckDirCacheReusesHash)
{
    JonesDataReport report;
    char aCachePath[JONESDATA_MAX_PATH];
    char aCd1Path[JONESDATA_MAX_PATH];
    char aCacheText[4096];
    char aWrongHash[JONESDATA_HASH_SIZE];
    int64_t size;
    int64_t mtime;

    JonesDataTest_WriteRequiredSet();
    JonesDataTest_BuildPath(aCachePath, sizeof(aCachePath), g_aTempDir, "cache.txt");
    JonesDataTest_BuildPath(aCd1Path, sizeof(aCd1Path), g_aTempDir, "CD1.GOB");
    JonesDataTest_StatPath(aCd1Path, &size, &mtime);
    TEST_ASSERT_EQUAL_INT64(3, size);

    memset(aWrongHash, '0', JONESDATA_HASH_LENGTH);
    aWrongHash[JONESDATA_HASH_LENGTH] = '\0';

    FILE* pCache = NULL;
    TEST_ASSERT_EQUAL_INT(0, fopen_s(&pCache, aCachePath, "wb"));
    TEST_ASSERT_NOT_NULL(pCache);
    fprintf(pCache, "%lld %lld %s %s\n", (long long)size, (long long)mtime, aWrongHash, aCd1Path);
    fprintf(pCache, "%lld %lld %s %s\n", 3LL, 1LL, aWrongHash, "/nonexistent/foo");
    TEST_ASSERT_EQUAL_INT(0, fclose(pCache));

    TEST_ASSERT_TRUE(JonesData_CheckDir(g_aTempDir, aCachePath, &report));
    TEST_ASSERT_NULL(report.pRelease);
    TEST_ASSERT_EQUAL_STRING(aWrongHash, report.aFiles[1].aSha256);
    TEST_ASSERT_FALSE(report.aFiles[1].bMatchesRelease);

    JonesDataTest_ReadWhole(aCachePath, aCacheText, sizeof(aCacheText));
    TEST_ASSERT_NOT_NULL(strstr(aCacheText, aCd1Path));
    TEST_ASSERT_NOT_NULL(strstr(aCacheText, aWrongHash));
    TEST_ASSERT_NOT_NULL(strstr(aCacheText, "CD2.GOB"));
    TEST_ASSERT_NOT_NULL(strstr(aCacheText, JONESDATA_TEST_HASH_448));
    TEST_ASSERT_NULL(strstr(aCacheText, "/nonexistent/foo"));
}

TEST(JonesData, TestCheckDirCacheIgnoresMalformed)
{
    JonesDataReport report;
    char aCachePath[JONESDATA_MAX_PATH];
    char aCd1Path[JONESDATA_MAX_PATH];
    char aWrongHash[JONESDATA_HASH_SIZE];
    int64_t size;
    int64_t mtime;

    JonesDataTest_WriteRequiredSet();
    JonesDataTest_BuildPath(aCachePath, sizeof(aCachePath), g_aTempDir, "cache.txt");
    JonesDataTest_BuildPath(aCd1Path, sizeof(aCd1Path), g_aTempDir, "CD1.GOB");
    JonesDataTest_StatPath(aCd1Path, &size, &mtime);

    memset(aWrongHash, '1', JONESDATA_HASH_LENGTH);
    aWrongHash[JONESDATA_HASH_LENGTH] = '\0';

    FILE* pCache = NULL;
    TEST_ASSERT_EQUAL_INT(0, fopen_s(&pCache, aCachePath, "wb"));
    TEST_ASSERT_NOT_NULL(pCache);
    fprintf(pCache, "garbage\n");
    for ( size_t i = 0; i < 3000; ++i )
    {
        fputc('x', pCache);
    }
    fputc('\n', pCache);
    fprintf(pCache, "3 not-a-number %s %s\n", aWrongHash, aCd1Path);
    fprintf(pCache, "%lld %lld %s %s\n", (long long)size, (long long)mtime, aWrongHash, aCd1Path);
    TEST_ASSERT_EQUAL_INT(0, fclose(pCache));

    TEST_ASSERT_TRUE(JonesData_CheckDir(g_aTempDir, aCachePath, &report));
    TEST_ASSERT_NULL(report.pRelease);
    TEST_ASSERT_EQUAL_STRING(aWrongHash, report.aFiles[1].aSha256);
}

TEST(JonesData, TestPrintHashRows)
{
    char aRowsPath[JONESDATA_MAX_PATH];
    char aRows[2048];
    char aExpected[2048];

    JonesDataTest_WriteFile(g_aTempDir, "Jones3D.GOB", "", 0);
    JonesDataTest_WriteFile(g_aTempDir, "CD1.GOB", "abc", 3);
    JonesDataTest_WriteFile(g_aTempDir, "jonesopn.snm", "abc", 3);

    JonesDataTest_BuildPath(aRowsPath, sizeof(aRowsPath), g_aTempDir, "rows.txt");
    FILE* pFile = NULL;
    TEST_ASSERT_EQUAL_INT(0, fopen_s(&pFile, aRowsPath, "wb"));
    TEST_ASSERT_NOT_NULL(pFile);
    TEST_ASSERT_TRUE(JonesData_PrintHashRows(g_aTempDir, pFile));
    TEST_ASSERT_EQUAL_INT(0, fclose(pFile));

    snprintf(aExpected, sizeof(aExpected),
        "{ \"Jones3D.GOB\", 0, \"%s\" },\n"
        "{ \"CD1.GOB\", 3, \"%s\" },\n"
        "{ \"jonesopn.snm\", 3, \"%s\" },\n",
        JONESDATA_TEST_HASH_EMPTY, JONESDATA_TEST_HASH_ABC, JONESDATA_TEST_HASH_ABC);

    JonesDataTest_ReadWhole(aRowsPath, aRows, sizeof(aRows));
    TEST_ASSERT_EQUAL_STRING(aExpected, aRows);
}

TEST(JonesData, TestPrintHashRowsNoKnownFile)
{
    char aRowsPath[JONESDATA_MAX_PATH];
    char aRows[64];

    JonesDataTest_BuildPath(aRowsPath, sizeof(aRowsPath), g_aTempDir, "rows.txt");
    FILE* pFile = NULL;
    TEST_ASSERT_EQUAL_INT(0, fopen_s(&pFile, aRowsPath, "wb"));
    TEST_ASSERT_NOT_NULL(pFile);
    TEST_ASSERT_FALSE(JonesData_PrintHashRows(g_aTempDir, pFile));
    TEST_ASSERT_EQUAL_INT(0, fclose(pFile));

    JonesDataTest_ReadWhole(aRowsPath, aRows, sizeof(aRows));
    TEST_ASSERT_EQUAL_STRING("", aRows);
}

TEST_GROUP_RUNNER(JonesData)
{
    RUN_TEST_CASE(JonesData, TestSha256KnownAnswers);
    RUN_TEST_CASE(JonesData, TestFindResourceDirDirect);
    RUN_TEST_CASE(JonesData, TestFindResourceDirSubdirResource);
    RUN_TEST_CASE(JonesData, TestFindResourceDirSubdirLower);
    RUN_TEST_CASE(JonesData, TestFindResourceDirNotFound);
    RUN_TEST_CASE(JonesData, TestFindResourceDirTooSmall);
    RUN_TEST_CASE(JonesData, TestCheckDirKnownRelease);
    RUN_TEST_CASE(JonesData, TestCheckDirMissingRequired);
    RUN_TEST_CASE(JonesData, TestCheckDirWrongContent);
    RUN_TEST_CASE(JonesData, TestCheckDirCaseInsensitiveNames);
    RUN_TEST_CASE(JonesData, TestCheckDirOptionalWrong);
    RUN_TEST_CASE(JonesData, TestCheckDirMixedReleases);
    RUN_TEST_CASE(JonesData, TestCheckDirSizeMismatchNotHashed);
    RUN_TEST_CASE(JonesData, TestCheckDirCacheReusesHash);
    RUN_TEST_CASE(JonesData, TestCheckDirCacheIgnoresMalformed);
    RUN_TEST_CASE(JonesData, TestPrintHashRows);
    RUN_TEST_CASE(JonesData, TestPrintHashRowsNoKnownFile);
}
