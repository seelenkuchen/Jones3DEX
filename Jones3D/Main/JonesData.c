#include "JonesData.h"

#include <std/General/std.h>
#include <std/General/stdUtil.h>

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#define JonesData_Strtoll _strtoi64
#else
#define JonesData_Strtoll strtoll
#endif

#define JONESDATA_MAX_NAME       64
#define JONESDATA_READ_SIZE      65536
#define JONESDATA_CACHE_LINE_MAX 2048

// Added: the known files, in the order the report lists them. The three GOB archives are required;
// the intro movies are optional.
typedef struct JonesDataKnownFile
{
    const char* pName;
    bool bRequired;
} JonesDataKnownFile;

static const JonesDataKnownFile JonesData_aKnownFiles[JONESDATA_MAX_FILES] =
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

// Added: the known releases. Hashed 2026-11-14 from the owner's GOG.com copy: gameId
// 1106707958, build 51587695718826606, English.
//
//   File            Size        SHA-256
//   Jones3D.GOB     49305685    27699241fb22611d516b1a1833572742a0b655c5199d39d584b9ef77c1671c26
//   CD1.GOB         353666116   ac9717b422e44cc4152e71df77bb8ebf1cc2d8105a032a20d9c19df4b1815db3
//   CD2.GOB         438468652   2f482f9154da1226c7d19fc42824ced4f3c6f03a3ea68a85d9144355a22a06d0
//   jonesopn.snm    14451896    e341b4c08a8a1e48cca6a48c5f59e06269196baef47893e8994beae9f1ae74f2
//   jonesopn_3.snm  7550476     514b80c473c96ba8aa9c67d256591830a9483db45c9dea9c230ee7be4771d63a
//   jonesopn_4.snm  9112480     a06d0010fe2797bb0b0bfd6374093061202b4fe27aa9f9a616a74088c0d8c302
//   jonesopn_5.snm  11410752    8102925a3f9552944e463d5edfa88f3e1e292136e20f96c642de4ea39d816af9
//   jonesopn_8.snm  19349746    2e4ccdbe361c182059627e4694b1ac3c3077276c66682eac7391b95352ffa7e4
static const JonesDataRelease JonesData_aBuiltInReleases[] =
{
    {
        "GOG.com, English",
        {
            { "Jones3D.GOB",   49305685LL,  "27699241fb22611d516b1a1833572742a0b655c5199d39d584b9ef77c1671c26" },
            { "CD1.GOB",       353666116LL, "ac9717b422e44cc4152e71df77bb8ebf1cc2d8105a032a20d9c19df4b1815db3" },
            { "CD2.GOB",       438468652LL, "2f482f9154da1226c7d19fc42824ced4f3c6f03a3ea68a85d9144355a22a06d0" },
            { "jonesopn.snm",  14451896LL,  "e341b4c08a8a1e48cca6a48c5f59e06269196baef47893e8994beae9f1ae74f2" },
            { "jonesopn_3.snm", 7550476LL,  "514b80c473c96ba8aa9c67d256591830a9483db45c9dea9c230ee7be4771d63a" },
            { "jonesopn_4.snm", 9112480LL,  "a06d0010fe2797bb0b0bfd6374093061202b4fe27aa9f9a616a74088c0d8c302" },
            { "jonesopn_5.snm", 11410752LL, "8102925a3f9552944e463d5edfa88f3e1e292136e20f96c642de4ea39d816af9" },
            { "jonesopn_8.snm", 19349746LL, "2e4ccdbe361c182059627e4694b1ac3c3077276c66682eac7391b95352ffa7e4" },
        },
        8
    },
};

static const JonesDataRelease* JonesData_pReleases    = JonesData_aBuiltInReleases;
static size_t JonesData_numReleases                   = STD_ARRAYLEN(JonesData_aBuiltInReleases);

typedef struct JonesDataCacheEntry
{
    char aPath[JONESDATA_MAX_PATH];
    int64_t size;
    int64_t mtime;
    char aSha256[JONESDATA_HASH_SIZE];
} JonesDataCacheEntry;

typedef struct JonesDataSha256
{
    uint32_t aState[8];
    uint64_t numBytes;
    uint8_t aBlock[64];
    size_t numBlockBytes;
} JonesDataSha256;

static void JonesData_LogWarning(const char* pFormat, ...);
static bool JonesData_JoinPath(char* pOut, size_t outSize, const char* pDir, const char* pName);
static bool JonesData_CopyPath(char* pOut, size_t outSize, const char* pPath);
static bool JonesData_StatFile(const char* pPath, int64_t* pSize, int64_t* pMtime);
static bool JonesData_FindFileInDir(const char* pDir, const char* pName, char* pOutPath, size_t outSize);
static bool JonesData_IsKnownBaseName(const char* pPath);
static void JonesData_ReadCache(const char* pCachePath, JonesDataCacheEntry* aEntries, size_t* pNumEntries);
static void JonesData_WriteCache(const char* pCachePath, const JonesDataCacheEntry* aEntries, size_t numEntries);
static bool JonesData_ReleaseHasRow(const JonesDataRelease* pRelease, const char* pName, int64_t size, const char* pHash);
static bool JonesData_SizeMatchesAnyRelease(const char* pName, int64_t size);
static const char* JonesData_FindMatchingRelease(const JonesDataReport* pReport);

static void JonesData_LogWarning(const char* pFormat, ...)
{
    va_list args;
    char aText[512];

    if ( !std_g_pHS )
    {
        return;
    }

    va_start(args, pFormat);
    vsnprintf(aText, sizeof(aText), pFormat, args);
    va_end(args);

    STDLOG_WARNING("%s", aText);
}

static bool JonesData_JoinPath(char* pOut, size_t outSize, const char* pDir, const char* pName)
{
    if ( !pOut || outSize == 0 || !pDir || !pName )
    {
        return false;
    }

    size_t dirLen = strlen(pDir);
    bool bSlash   = dirLen > 0 && (pDir[dirLen - 1] == '/' || pDir[dirLen - 1] == '\\');
    size_t needed = dirLen + (bSlash ? 0 : 1) + strlen(pName) + 1;
    if ( dirLen == 0 )
    {
        needed = strlen(pName) + 1;
    }

    if ( needed > outSize )
    {
        return false;
    }

    if ( dirLen > 0 )
    {
        memcpy(pOut, pDir, dirLen);
        if ( !bSlash )
        {
            pOut[dirLen++] = '/';
        }
    }

    strcpy(&pOut[dirLen], pName);
    return true;
}

static bool JonesData_CopyPath(char* pOut, size_t outSize, const char* pPath)
{
    size_t len = strlen(pPath);
    if ( len + 1 > outSize )
    {
        return false;
    }

    memcpy(pOut, pPath, len + 1);
    return true;
}

static bool JonesData_StatFile(const char* pPath, int64_t* pSize, int64_t* pMtime)
{
#ifdef _WIN32
    struct _stat64 st;
    if ( _stat64(pPath, &st) != 0 || (st.st_mode & _S_IFREG) == 0 )
    {
        return false;
    }
#else
    struct stat st;
    if ( stat(pPath, &st) != 0 || !S_ISREG(st.st_mode) )
    {
        return false;
    }
#endif

    if ( pSize )
    {
        *pSize = (int64_t)st.st_size;
    }

    if ( pMtime )
    {
        *pMtime = (int64_t)st.st_mtime;
    }

    return true;
}

static void JonesData_MakeCaseVariant(char* pOut, const char* pName, bool bUpper)
{
    size_t i = 0;
    for ( ; pName[i] && i + 1 < JONESDATA_MAX_NAME; ++i )
    {
        pOut[i] = bUpper
            ? (char)toupper((unsigned char)pName[i])
            : (char)tolower((unsigned char)pName[i]);
    }

    pOut[i] = '\0';
}

static bool JonesData_TryFile(const char* pDir, const char* pName, char* pOutPath, size_t outSize)
{
    char aPath[JONESDATA_MAX_PATH];

    if ( !JonesData_JoinPath(aPath, sizeof(aPath), pDir, pName) )
    {
        return false;
    }

    if ( !JonesData_StatFile(aPath, NULL, NULL) )
    {
        return false;
    }

    return JonesData_CopyPath(pOutPath, outSize, aPath);
}

static bool JonesData_FindFileInDir(const char* pDir, const char* pName, char* pOutPath, size_t outSize)
{
    char aVariant[JONESDATA_MAX_NAME];

    // Added: Linux file systems are case-sensitive, while copies from CDs are often all upper or
    // all lower case. Try the exact name first, then the two all-case variants.
    if ( JonesData_TryFile(pDir, pName, pOutPath, outSize) )
    {
        return true;
    }

    JonesData_MakeCaseVariant(aVariant, pName, false);
    if ( strcmp(aVariant, pName) != 0 && JonesData_TryFile(pDir, aVariant, pOutPath, outSize) )
    {
        return true;
    }

    JonesData_MakeCaseVariant(aVariant, pName, true);
    if ( strcmp(aVariant, pName) != 0 && JonesData_TryFile(pDir, aVariant, pOutPath, outSize) )
    {
        return true;
    }

    return false;
}

bool JonesData_FindResourceDir(const char* pDir, char* pOutDir, size_t outSize)
{
    if ( pOutDir && outSize > 0 )
    {
        pOutDir[0] = '\0';
    }

    if ( !pDir || pDir[0] == '\0' || !pOutDir || outSize == 0 )
    {
        return false;
    }

    char aFound[JONESDATA_MAX_PATH];
    if ( JonesData_FindFileInDir(pDir, "Jones3D.GOB", aFound, sizeof(aFound)) )
    {
        return JonesData_CopyPath(pOutDir, outSize, pDir);
    }

    static const char* aSubDirs[] = { "Resource", "resource" };
    for ( size_t i = 0; i < STD_ARRAYLEN(aSubDirs); ++i )
    {
        char aSubDir[JONESDATA_MAX_PATH];
        if ( !JonesData_JoinPath(aSubDir, sizeof(aSubDir), pDir, aSubDirs[i]) )
        {
            continue;
        }

        if ( JonesData_FindFileInDir(aSubDir, "Jones3D.GOB", aFound, sizeof(aFound)) )
        {
            return JonesData_CopyPath(pOutDir, outSize, aSubDir);
        }
    }

    return false;
}

static const char* JonesData_BaseName(const char* pPath)
{
    const char* pBase = pPath;
    for ( const char* pCur = pPath; *pCur; ++pCur )
    {
        if ( *pCur == '/' || *pCur == '\\' )
        {
            pBase = pCur + 1;
        }
    }

    return pBase;
}

static bool JonesData_IsKnownBaseName(const char* pPath)
{
    const char* pBase = JonesData_BaseName(pPath);
    for ( size_t i = 0; i < STD_ARRAYLEN(JonesData_aKnownFiles); ++i )
    {
        if ( strcmpi(pBase, JonesData_aKnownFiles[i].pName) == 0 )
        {
            return true;
        }
    }

    return false;
}

static bool JonesData_ParseCacheLine(const char* pLine, JonesDataCacheEntry* pEntry)
{
    char* pEnd;

    errno = 0;
    long long size = JonesData_Strtoll(pLine, &pEnd, 10);
    if ( pEnd == pLine || *pEnd != ' ' || errno == ERANGE || size < 0 )
    {
        return false;
    }

    const char* pCur = pEnd + 1;
    errno = 0;
    long long mtime = JonesData_Strtoll(pCur, &pEnd, 10);
    if ( pEnd == pCur || *pEnd != ' ' || errno == ERANGE )
    {
        return false;
    }

    pCur = pEnd + 1;
    if ( strlen(pCur) < JONESDATA_HASH_LENGTH + 2 )
    {
        return false;
    }

    for ( size_t i = 0; i < JONESDATA_HASH_LENGTH; ++i )
    {
        if ( !isxdigit((unsigned char)pCur[i]) )
        {
            return false;
        }

        pEntry->aSha256[i] = (char)tolower((unsigned char)pCur[i]);
    }
    pEntry->aSha256[JONESDATA_HASH_LENGTH] = '\0';

    if ( pCur[JONESDATA_HASH_LENGTH] != ' ' )
    {
        return false;
    }

    const char* pPath = pCur + JONESDATA_HASH_LENGTH + 1;
    size_t pathLen    = strlen(pPath);
    if ( pathLen == 0 || pathLen >= sizeof(pEntry->aPath) )
    {
        return false;
    }

    memcpy(pEntry->aPath, pPath, pathLen + 1);
    pEntry->size  = (int64_t)size;
    pEntry->mtime = (int64_t)mtime;
    return true;
}

static void JonesData_StoreCacheEntry(JonesDataCacheEntry* aEntries, size_t* pNumEntries, const JonesDataCacheEntry* pEntry)
{
    const char* pBase = JonesData_BaseName(pEntry->aPath);

    for ( size_t i = 0; i < *pNumEntries; ++i )
    {
        if ( strcmpi(JonesData_BaseName(aEntries[i].aPath), pBase) == 0 )
        {
            aEntries[i] = *pEntry;
            return;
        }
    }

    if ( *pNumEntries < JONESDATA_MAX_FILES )
    {
        aEntries[*pNumEntries] = *pEntry;
        ++*pNumEntries;
    }
}

static void JonesData_ReadCache(const char* pCachePath, JonesDataCacheEntry* aEntries, size_t* pNumEntries)
{
    *pNumEntries = 0;

    FILE* pFile = fopen(pCachePath, "rb");
    if ( !pFile )
    {
        return;
    }

    char aLine[JONESDATA_CACHE_LINE_MAX];
    while ( fgets(aLine, sizeof(aLine), pFile) )
    {
        size_t len = strlen(aLine);
        if ( len == sizeof(aLine) - 1 && aLine[len - 1] != '\n' && !feof(pFile) )
        {
            int ch;
            while ( (ch = fgetc(pFile)) != EOF && ch != '\n' )
            {
                ;
            }
            continue;
        }

        while ( len > 0 && (aLine[len - 1] == '\n' || aLine[len - 1] == '\r') )
        {
            aLine[--len] = '\0';
        }

        JonesDataCacheEntry entry;
        if ( !JonesData_ParseCacheLine(aLine, &entry) )
        {
            continue;
        }

        if ( JonesData_IsKnownBaseName(entry.aPath) )
        {
            JonesData_StoreCacheEntry(aEntries, pNumEntries, &entry);
        }
    }

    fclose(pFile);
}

static bool JonesData_FindCacheEntry(const JonesDataCacheEntry* aEntries, size_t numEntries, const char* pPath, int64_t size, int64_t mtime, char* pHashOut)
{
    for ( size_t i = 0; i < numEntries; ++i )
    {
        if ( aEntries[i].size == size
          && aEntries[i].mtime == mtime
          && strcmp(aEntries[i].aPath, pPath) == 0 )
        {
            strcpy(pHashOut, aEntries[i].aSha256);
            return true;
        }
    }

    return false;
}

static void JonesData_WriteCache(const char* pCachePath, const JonesDataCacheEntry* aEntries, size_t numEntries)
{
    FILE* pFile = fopen(pCachePath, "wb");
    if ( !pFile )
    {
        JonesData_LogWarning("Game data cache: cannot write '%s'.\n", pCachePath);
        return;
    }

    for ( size_t i = 0; i < numEntries; ++i )
    {
        if ( strpbrk(aEntries[i].aPath, "\r\n") )
        {
            continue;
        }

        fprintf(pFile, "%lld %lld %s %s\n",
            (long long)aEntries[i].size,
            (long long)aEntries[i].mtime,
            aEntries[i].aSha256,
            aEntries[i].aPath
        );
    }

    if ( ferror(pFile) )
    {
        JonesData_LogWarning("Game data cache: write error on '%s'.\n", pCachePath);
    }

    fclose(pFile);
}

static bool JonesData_ReleaseHasRow(const JonesDataRelease* pRelease, const char* pName, int64_t size, const char* pHash)
{
    if ( !pHash || pHash[0] == '\0' )
    {
        return false;
    }

    for ( size_t i = 0; i < pRelease->numFiles; ++i )
    {
        const JonesDataReleaseFile* pRow = &pRelease->aFiles[i];
        if ( strcmp(pRow->pName, pName) == 0
          && pRow->size == size
          && strcmp(pRow->aSha256, pHash) == 0 )
        {
            return true;
        }
    }

    return false;
}

static bool JonesData_SizeMatchesAnyRelease(const char* pName, int64_t size)
{
    for ( size_t i = 0; i < JonesData_numReleases; ++i )
    {
        const JonesDataRelease* pRelease = &JonesData_pReleases[i];
        for ( size_t j = 0; j < pRelease->numFiles; ++j )
        {
            const JonesDataReleaseFile* pRow = &pRelease->aFiles[j];
            if ( strcmp(pRow->pName, pName) == 0 && pRow->size == size )
            {
                return true;
            }
        }
    }

    return false;
}

static const char* JonesData_FindMatchingRelease(const JonesDataReport* pReport)
{
    for ( size_t i = 0; i < JonesData_numReleases; ++i )
    {
        const JonesDataRelease* pRelease = &JonesData_pReleases[i];
        bool bMatches = true;

        for ( size_t j = 0; j < pReport->numFiles; ++j )
        {
            const JonesDataFile* pFile = &pReport->aFiles[j];
            if ( pFile->bPresent )
            {
                if ( !JonesData_ReleaseHasRow(pRelease, pFile->pName, pFile->size, pFile->aSha256) )
                {
                    bMatches = false;
                    break;
                }
            }
            else if ( pFile->bRequired )
            {
                bMatches = false;
                break;
            }
        }

        if ( bMatches )
        {
            return pRelease->pName;
        }
    }

    return NULL;
}

void JonesData_SetReleaseTable(const JonesDataRelease* aReleases, size_t numReleases)
{
    if ( !aReleases )
    {
        JonesData_pReleases  = JonesData_aBuiltInReleases;
        JonesData_numReleases = STD_ARRAYLEN(JonesData_aBuiltInReleases);
        return;
    }

    JonesData_pReleases  = aReleases;
    JonesData_numReleases = numReleases;
}

// Added: plain C SHA-256 (FIPS 180-4), so the check does not depend on the C++ header sha256.h.
static uint32_t JonesData_Rotr(uint32_t value, unsigned int count)
{
    return (value >> count) | (value << (32 - count));
}

static void JonesData_Sha256Transform(JonesDataSha256* pCtx, const uint8_t* pBlock)
{
    static const uint32_t aConstants[64] =
    {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
    };

    uint32_t aWords[64];

    for ( size_t i = 0; i < 16; ++i )
    {
        aWords[i] = ((uint32_t)pBlock[i * 4 + 0] << 24)
                  | ((uint32_t)pBlock[i * 4 + 1] << 16)
                  | ((uint32_t)pBlock[i * 4 + 2] << 8)
                  | ((uint32_t)pBlock[i * 4 + 3]);
    }

    for ( size_t i = 16; i < 64; ++i )
    {
        uint32_t s0 = JonesData_Rotr(aWords[i - 15], 7) ^ JonesData_Rotr(aWords[i - 15], 18) ^ (aWords[i - 15] >> 3);
        uint32_t s1 = JonesData_Rotr(aWords[i - 2], 17) ^ JonesData_Rotr(aWords[i - 2], 19) ^ (aWords[i - 2] >> 10);
        aWords[i] = aWords[i - 16] + s0 + aWords[i - 7] + s1;
    }

    uint32_t a = pCtx->aState[0];
    uint32_t b = pCtx->aState[1];
    uint32_t c = pCtx->aState[2];
    uint32_t d = pCtx->aState[3];
    uint32_t e = pCtx->aState[4];
    uint32_t f = pCtx->aState[5];
    uint32_t g = pCtx->aState[6];
    uint32_t h = pCtx->aState[7];

    for ( size_t i = 0; i < 64; ++i )
    {
        uint32_t sigma1 = JonesData_Rotr(e, 6) ^ JonesData_Rotr(e, 11) ^ JonesData_Rotr(e, 25);
        uint32_t choice = (e & f) ^ (~e & g);
        uint32_t temp1  = h + sigma1 + choice + aConstants[i] + aWords[i];
        uint32_t sigma0 = JonesData_Rotr(a, 2) ^ JonesData_Rotr(a, 13) ^ JonesData_Rotr(a, 22);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2    = sigma0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    pCtx->aState[0] += a;
    pCtx->aState[1] += b;
    pCtx->aState[2] += c;
    pCtx->aState[3] += d;
    pCtx->aState[4] += e;
    pCtx->aState[5] += f;
    pCtx->aState[6] += g;
    pCtx->aState[7] += h;
}

static void JonesData_Sha256Init(JonesDataSha256* pCtx)
{
    pCtx->aState[0] = 0x6a09e667u;
    pCtx->aState[1] = 0xbb67ae85u;
    pCtx->aState[2] = 0x3c6ef372u;
    pCtx->aState[3] = 0xa54ff53au;
    pCtx->aState[4] = 0x510e527fu;
    pCtx->aState[5] = 0x9b05688cu;
    pCtx->aState[6] = 0x1f83d9abu;
    pCtx->aState[7] = 0x5be0cd19u;
    pCtx->numBytes      = 0;
    pCtx->numBlockBytes = 0;
}

static void JonesData_Sha256Update(JonesDataSha256* pCtx, const uint8_t* pData, size_t size)
{
    pCtx->numBytes += (uint64_t)size;

    if ( pCtx->numBlockBytes > 0 )
    {
        size_t needed = sizeof(pCtx->aBlock) - pCtx->numBlockBytes;
        size_t take   = size < needed ? size : needed;
        memcpy(&pCtx->aBlock[pCtx->numBlockBytes], pData, take);
        pCtx->numBlockBytes += take;
        pData += take;
        size  -= take;

        if ( pCtx->numBlockBytes == sizeof(pCtx->aBlock) )
        {
            JonesData_Sha256Transform(pCtx, pCtx->aBlock);
            pCtx->numBlockBytes = 0;
        }
    }

    while ( size >= sizeof(pCtx->aBlock) )
    {
        JonesData_Sha256Transform(pCtx, pData);
        pData += sizeof(pCtx->aBlock);
        size  -= sizeof(pCtx->aBlock);
    }

    if ( size > 0 )
    {
        memcpy(pCtx->aBlock, pData, size);
        pCtx->numBlockBytes = size;
    }
}

static void JonesData_Sha256Finalize(JonesDataSha256* pCtx, uint8_t aDigest[32])
{
    uint64_t numBits = pCtx->numBytes * 8;

    pCtx->aBlock[pCtx->numBlockBytes++] = 0x80;
    if ( pCtx->numBlockBytes > 56 )
    {
        while ( pCtx->numBlockBytes < 64 )
        {
            pCtx->aBlock[pCtx->numBlockBytes++] = 0;
        }

        JonesData_Sha256Transform(pCtx, pCtx->aBlock);
        pCtx->numBlockBytes = 0;
    }

    while ( pCtx->numBlockBytes < 56 )
    {
        pCtx->aBlock[pCtx->numBlockBytes++] = 0;
    }

    for ( size_t i = 0; i < 8; ++i )
    {
        pCtx->aBlock[56 + i] = (uint8_t)(numBits >> (56 - i * 8));
    }

    JonesData_Sha256Transform(pCtx, pCtx->aBlock);

    for ( size_t i = 0; i < 8; ++i )
    {
        aDigest[i * 4 + 0] = (uint8_t)(pCtx->aState[i] >> 24);
        aDigest[i * 4 + 1] = (uint8_t)(pCtx->aState[i] >> 16);
        aDigest[i * 4 + 2] = (uint8_t)(pCtx->aState[i] >> 8);
        aDigest[i * 4 + 3] = (uint8_t)(pCtx->aState[i]);
    }
}

static void JonesData_Sha256Hex(const uint8_t aDigest[32], char* pHex)
{
    static const char aDigits[] = "0123456789abcdef";

    for ( size_t i = 0; i < 32; ++i )
    {
        pHex[i * 2 + 0] = aDigits[aDigest[i] >> 4];
        pHex[i * 2 + 1] = aDigits[aDigest[i] & 0x0F];
    }

    pHex[JONESDATA_HASH_LENGTH] = '\0';
}

bool JonesData_Sha256File(const char* pPath, char* pHashOut, size_t outSize)
{
    if ( pHashOut && outSize > 0 )
    {
        pHashOut[0] = '\0';
    }

    if ( !pPath || !pHashOut || outSize < JONESDATA_HASH_SIZE )
    {
        return false;
    }

    FILE* pFile = fopen(pPath, "rb");
    if ( !pFile )
    {
        JonesData_LogWarning("Game data: cannot open '%s' for hashing.\n", pPath);
        return false;
    }

    JonesDataSha256 ctx;
    JonesData_Sha256Init(&ctx);

    uint8_t aBuffer[JONESDATA_READ_SIZE];
    size_t numRead;
    bool bOk = true;

    while ( (numRead = fread(aBuffer, 1, sizeof(aBuffer), pFile)) > 0 )
    {
        JonesData_Sha256Update(&ctx, aBuffer, numRead);
    }

    if ( ferror(pFile) )
    {
        bOk = false;
    }

    fclose(pFile);

    if ( !bOk )
    {
        JonesData_LogWarning("Game data: read error on '%s'.\n", pPath);
        return false;
    }

    uint8_t aDigest[32];
    JonesData_Sha256Finalize(&ctx, aDigest);
    JonesData_Sha256Hex(aDigest, pHashOut);
    return true;
}

bool JonesData_CheckDir(const char* pResourceDir, const char* pCachePath, JonesDataReport* pOut)
{
    if ( !pOut )
    {
        return false;
    }

    memset(pOut, 0, sizeof(*pOut));
    pOut->numFiles = JONESDATA_MAX_FILES;

    for ( size_t i = 0; i < JONESDATA_MAX_FILES; ++i )
    {
        pOut->aFiles[i].pName     = JonesData_aKnownFiles[i].pName;
        pOut->aFiles[i].bRequired = JonesData_aKnownFiles[i].bRequired;
    }

    JonesDataCacheEntry aCache[JONESDATA_MAX_FILES];
    size_t numCacheEntries = 0;
    if ( pCachePath )
    {
        JonesData_ReadCache(pCachePath, aCache, &numCacheEntries);
    }

    JonesDataCacheEntry aWriteEntries[JONESDATA_MAX_FILES];
    size_t numWriteEntries = 0;

    for ( size_t i = 0; i < JONESDATA_MAX_FILES; ++i )
    {
        JonesDataFile* pFile = &pOut->aFiles[i];
        if ( !pResourceDir || !JonesData_FindFileInDir(pResourceDir, pFile->pName, pFile->aPath, sizeof(pFile->aPath)) )
        {
            if ( pFile->bRequired )
            {
                ++pOut->numMissingRequired;
            }
            continue;
        }

        int64_t mtime;
        if ( !JonesData_StatFile(pFile->aPath, &pFile->size, &mtime) )
        {
            pFile->aPath[0] = '\0';
            if ( pFile->bRequired )
            {
                ++pOut->numMissingRequired;
            }
            continue;
        }

        pFile->bPresent = true;

        if ( JonesData_SizeMatchesAnyRelease(pFile->pName, pFile->size) )
        {
            bool bHashed = false;
            if ( pCachePath && JonesData_FindCacheEntry(aCache, numCacheEntries, pFile->aPath, pFile->size, mtime, pFile->aSha256) )
            {
                bHashed = true;
            }
            else if ( JonesData_Sha256File(pFile->aPath, pFile->aSha256, sizeof(pFile->aSha256)) )
            {
                bHashed = true;
            }

            if ( bHashed && numWriteEntries < JONESDATA_MAX_FILES )
            {
                JonesDataCacheEntry* pWrite = &aWriteEntries[numWriteEntries++];
                JonesData_CopyPath(pWrite->aPath, sizeof(pWrite->aPath), pFile->aPath);
                pWrite->size  = pFile->size;
                pWrite->mtime = mtime;
                strcpy(pWrite->aSha256, pFile->aSha256);
            }
        }

        for ( size_t j = 0; j < JonesData_numReleases && !pFile->bMatchesRelease; ++j )
        {
            pFile->bMatchesRelease = JonesData_ReleaseHasRow(&JonesData_pReleases[j], pFile->pName, pFile->size, pFile->aSha256);
        }
    }

    pOut->bComplete = pOut->numMissingRequired == 0;
    pOut->pRelease  = JonesData_FindMatchingRelease(pOut);

    if ( pCachePath )
    {
        JonesData_WriteCache(pCachePath, aWriteEntries, numWriteEntries);
    }

    return pOut->bComplete;
}

void JonesData_LogReport(const JonesDataReport* pReport)
{
    if ( !pReport || !std_g_pHS )
    {
        return;
    }

    if ( pReport->pRelease )
    {
        STDLOG_STATUS("Game data: known release '%s'.\n", pReport->pRelease);
        return;
    }

    if ( !pReport->bComplete )
    {
        for ( size_t i = 0; i < pReport->numFiles; ++i )
        {
            const JonesDataFile* pFile = &pReport->aFiles[i];
            if ( pFile->bRequired && !pFile->bPresent )
            {
                STDLOG_ERROR("Game data: missing required file '%s'.\n", pFile->pName);
            }
        }
        return;
    }

    bool bAnyMismatch = false;
    for ( size_t i = 0; i < pReport->numFiles; ++i )
    {
        const JonesDataFile* pFile = &pReport->aFiles[i];
        if ( pFile->bPresent && !pFile->bMatchesRelease )
        {
            bAnyMismatch = true;
            break;
        }
    }

    // Added: every present file belongs to a known release, but no single release contains them all.
    if ( !bAnyMismatch )
    {
        STDLOG_WARNING("Game data: files come from different known releases.\n");
        return;
    }

    for ( size_t i = 0; i < pReport->numFiles; ++i )
    {
        const JonesDataFile* pFile = &pReport->aFiles[i];
        if ( pFile->bPresent && !pFile->bMatchesRelease )
        {
            STDLOG_WARNING("Game data: '%s' does not match a known release.\n", pFile->pName);
        }
    }
}

bool JonesData_PrintHashRows(const char* pResourceDir, FILE* pOut)
{
    if ( !pResourceDir || !pOut )
    {
        return false;
    }

    bool bAnyFound = false;

    for ( size_t i = 0; i < JONESDATA_MAX_FILES; ++i )
    {
        char aPath[JONESDATA_MAX_PATH];
        if ( !JonesData_FindFileInDir(pResourceDir, JonesData_aKnownFiles[i].pName, aPath, sizeof(aPath)) )
        {
            continue;
        }

        bAnyFound = true;

        int64_t size;
        char aHash[JONESDATA_HASH_SIZE];
        if ( !JonesData_StatFile(aPath, &size, NULL) || !JonesData_Sha256File(aPath, aHash, sizeof(aHash)) )
        {
            continue;
        }

        fprintf(pOut, "{ \"%s\", %lld, \"%s\" },\n", JonesData_aKnownFiles[i].pName, (long long)size, aHash);
    }

    return bAnyFound;
}
