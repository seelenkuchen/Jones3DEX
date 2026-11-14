// Added: the game data install engine (M8 wizard, W2). Source finding, the copy plan and the
// copy run are one module so the wizard, the disc-2 step and --install-data share one path.
// Nothing here calls a platform API: the host supplies the mount and registry lists and the
// free-space count, and files go through the j3dcrt-mapped stdio and stdFileUtil search.
#include "JonesInstall.h"

#include <Jones3D/Main/JonesData.h>
#include <Jones3D/Main/JonesDataLocate.h>

#ifndef _WIN32
#include <j3dcore/j3dcrt.h>
#endif

#include <std/General/stdFileUtil.h>
#include <std/General/stdMemory.h>
#include <std/General/stdUtil.h>
#include <std/types.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <direct.h>
#define JONESINSTALL_MKDIR(path) _mkdir(path)
#define JONESINSTALL_STAT _stat
#define JONESINSTALL_STAT_ISDIR(mode) (((mode) & _S_IFDIR) != 0)
#define JONESINSTALL_STAT_ISREG(mode) (((mode) & _S_IFREG) != 0)
#else
#define JONESINSTALL_MKDIR(path) mkdir((path), 0700)
#define JONESINSTALL_STAT stat
#define JONESINSTALL_STAT_ISDIR(mode) S_ISDIR(mode)
#define JONESINSTALL_STAT_ISREG(mode) S_ISREG(mode)
#endif

// TODO: [COMPAT] confirm the Steam install folder name
#define JONESINSTALL_STEAM_NAME "Indiana Jones and the Infernal Machine"
#define JONESINSTALL_GOG_INSTALLER_PREFIX "setup_indiana_jones_and_the_infernal_machine"
#define JONESINSTALL_GOG_INSTALLER_EXTENSION ".exe"

#define JONESINSTALL_COPY_SIZE 65536u
#define JONESINSTALL_VDF_MAX_SIZE (1024u * 1024u)

typedef struct JonesInstallKnownFile
{
    const char* pName;
    bool bRequired;
} JonesInstallKnownFile;

// Added: the same known files JonesData reports, in report order. Only the GOBs are required.
static const JonesInstallKnownFile JonesInstall_aKnownFiles[JONESDATA_MAX_FILES] =
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

// Added: the help files a retail or GOG copy may carry at its root.
static const char* const JonesInstall_aHelpFiles[] =
{
    "Jones3D.chm",
    "Jones3D.hlp",
    "Jones3D.cnt",
};

static const char* JonesInstall_Resolve(const char* pPath, char* pOut, size_t size)
{
    if ( !pOut || size == 0 )
    {
        return pPath;
    }

    pOut[0] = '\0';
    if ( !pPath )
    {
        return pOut;
    }

    if ( strlen(pPath) >= size )
    {
        return pPath;
    }

#ifndef _WIN32
    // Added: the engine's resolver also handles backslashes and letter case on POSIX hosts.
    return j3dcrt_ResolvePath(pPath, pOut, size);
#else
    // Windows resolves separators and letter case itself.
    memcpy(pOut, pPath, strlen(pPath) + 1);
    return pOut;
#endif
}

static bool JonesInstall_CopyPath(char* pOut, size_t outSize, const char* pPath)
{
    if ( !pPath )
    {
        if ( pOut && outSize > 0 )
        {
            pOut[0] = '\0';
        }

        return false;
    }

    if ( !pOut )
    {
        return true;
    }

    if ( outSize == 0 )
    {
        return false;
    }

    size_t len = strlen(pPath);
    if ( len + 1 > outSize )
    {
        pOut[0] = '\0';
        return false;
    }

    memcpy(pOut, pPath, len + 1);
    return true;
}

static char JonesInstall_Separator(JonesInstallPlatform platform)
{
    return platform == JONESINSTALL_PLATFORM_WINDOWS ? '\\' : '/';
}

static bool JonesInstall_JoinPath(char* pOut, size_t outSize, JonesInstallPlatform platform,
    const char* pDir, const char* pName)
{
    if ( !pOut || outSize == 0 || !pName )
    {
        return false;
    }

    char chSeparator = JonesInstall_Separator(platform);
    size_t dirLen = pDir ? strlen(pDir) : 0;
    bool bSlash   = dirLen > 0 && (pDir[dirLen - 1] == '/' || pDir[dirLen - 1] == '\\');
    size_t needed = (dirLen > 0 ? dirLen + (bSlash ? 0 : 1) : 0) + strlen(pName) + 1;
    if ( needed > outSize )
    {
        return false;
    }

    size_t pos = 0;
    if ( dirLen > 0 )
    {
        memcpy(pOut, pDir, dirLen);
        pos = dirLen;
        if ( !bSlash )
        {
            pOut[pos++] = chSeparator;
        }
    }

    memcpy(&pOut[pos], pName, strlen(pName) + 1);
    return true;
}

// Joins a '/'-separated plan destination below an absolute target directory.
static bool JonesInstall_JoinRelative(char* pOut, size_t outSize, JonesInstallPlatform platform,
    const char* pDir, const char* pRelative)
{
    if ( !pOut || outSize == 0 || !pRelative )
    {
        return false;
    }

    char chSeparator = JonesInstall_Separator(platform);
    size_t dirLen = pDir ? strlen(pDir) : 0;
    bool bSlash   = dirLen > 0 && (pDir[dirLen - 1] == '/' || pDir[dirLen - 1] == '\\');
    size_t needed = (dirLen > 0 ? dirLen + (bSlash ? 0 : 1) : 0) + strlen(pRelative) + 1;
    if ( needed > outSize )
    {
        return false;
    }

    size_t pos = 0;
    if ( dirLen > 0 )
    {
        memcpy(pOut, pDir, dirLen);
        pos = dirLen;
        if ( !bSlash )
        {
            pOut[pos++] = chSeparator;
        }
    }

    memcpy(&pOut[pos], pRelative, strlen(pRelative) + 1);
    return true;
}

static void JonesInstall_UsePlatformSeparators(char* pPath, JonesInstallPlatform platform)
{
    if ( !pPath )
    {
        return;
    }

    char chSeparator = JonesInstall_Separator(platform);
    for ( size_t i = 0; pPath[i] != '\0'; ++i )
    {
        if ( pPath[i] == '/' || pPath[i] == '\\' )
        {
            pPath[i] = chSeparator;
        }
    }
}

static void JonesInstall_NormalizePath(char* pOut, size_t outSize, const char* pPath,
    JonesInstallPlatform platform)
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

            if ( platform == JONESINSTALL_PLATFORM_WINDOWS && ch >= 'A' && ch <= 'Z' )
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

static bool JonesInstall_PathsEqual(const char* pPath1, const char* pPath2,
    JonesInstallPlatform platform)
{
    char aPath1[JONESINSTALL_MAX_PATH];
    char aPath2[JONESINSTALL_MAX_PATH];

    JonesInstall_NormalizePath(aPath1, sizeof(aPath1), pPath1, platform);
    JonesInstall_NormalizePath(aPath2, sizeof(aPath2), pPath2, platform);
    return strcmp(aPath1, aPath2) == 0;
}

static bool JonesInstall_Stat(const char* pPath, bool* pbDirectory, bool* pbRegular,
    int64_t* pSize)
{
    if ( !pPath || pPath[0] == '\0' )
    {
        return false;
    }

    char aResolved[JONESINSTALL_MAX_PATH];
    const char* pUse = JonesInstall_Resolve(pPath, aResolved, sizeof(aResolved));

    struct JONESINSTALL_STAT st;
    if ( !pUse || JONESINSTALL_STAT(pUse, &st) != 0 )
    {
        return false;
    }

    if ( pbDirectory )
    {
        *pbDirectory = JONESINSTALL_STAT_ISDIR(st.st_mode);
    }

    if ( pbRegular )
    {
        *pbRegular = JONESINSTALL_STAT_ISREG(st.st_mode);
    }

    if ( pSize )
    {
        *pSize = (int64_t)st.st_size;
    }

    return true;
}

static bool JonesInstall_IsDirectory(const char* pPath)
{
    bool bDirectory = false;
    return JonesInstall_Stat(pPath, &bDirectory, NULL, NULL) && bDirectory;
}

static bool JonesInstall_IsFile(const char* pPath)
{
    bool bRegular = false;
    return JonesInstall_Stat(pPath, NULL, &bRegular, NULL) && bRegular;
}

static bool JonesInstall_GetFileSize(const char* pPath, int64_t* pSize)
{
    bool bRegular = false;
    return JonesInstall_Stat(pPath, NULL, &bRegular, pSize) && bRegular;
}

static bool JonesInstall_MakeDirs(const char* pPath)
{
    if ( !pPath || pPath[0] == '\0' )
    {
        return false;
    }

    char aResolved[JONESINSTALL_MAX_PATH];
    const char* pUse = JonesInstall_Resolve(pPath, aResolved, sizeof(aResolved));
    if ( !pUse )
    {
        return false;
    }

    char aPath[JONESINSTALL_MAX_PATH];
    size_t len = strlen(pUse);
    if ( len + 1 > sizeof(aPath) )
    {
        return false;
    }

    memcpy(aPath, pUse, len + 1);
    while ( len > 1 && (aPath[len - 1] == '/' || aPath[len - 1] == '\\') )
    {
        aPath[--len] = '\0';
    }

    if ( JonesInstall_IsDirectory(aPath) )
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
        if ( aPath[0] != '\0' && !JonesInstall_IsDirectory(aPath) )
        {
            JONESINSTALL_MKDIR(aPath);
        }
        aPath[i] = chSeparator;
    }

    if ( !JonesInstall_IsDirectory(aPath) )
    {
        JONESINSTALL_MKDIR(aPath);
    }

    return JonesInstall_IsDirectory(aPath);
}

static void JonesInstall_MakeCaseVariant(char* pOut, size_t outSize, const char* pName,
    bool bUpper)
{
    if ( !pOut || outSize == 0 )
    {
        return;
    }

    size_t i = 0;
    for ( ; pName && pName[i] && i + 1 < outSize; ++i )
    {
        char ch = pName[i];
        if ( bUpper )
        {
            if ( ch >= 'a' && ch <= 'z' )
            {
                ch = (char)(ch - 'a' + 'A');
            }
        }
        else if ( ch >= 'A' && ch <= 'Z' )
        {
            ch = (char)(ch - 'A' + 'a');
        }

        pOut[i] = ch;
    }

    pOut[i] = '\0';
}

// Finds one directory entry by exact name, then all lower, all upper, then any spelling. The
// last step uses the engine's directory search, which is the only way to match mixed case.
static bool JonesInstall_FindEntry(JonesInstallPlatform platform, const char* pDir,
    const char* pName, bool bDirectory, char* pOut, size_t outSize)
{
    if ( !pDir || !pName )
    {
        return false;
    }

    char aPath[JONESINSTALL_MAX_PATH];
    if ( JonesInstall_JoinPath(aPath, sizeof(aPath), platform, pDir, pName) )
    {
        if ( bDirectory ? JonesInstall_IsDirectory(aPath) : JonesInstall_IsFile(aPath) )
        {
            return JonesInstall_CopyPath(pOut, outSize, aPath);
        }
    }

    char aVariant[JONESINSTALL_MAX_PATH];
    JonesInstall_MakeCaseVariant(aVariant, sizeof(aVariant), pName, false);
    if ( strcmp(aVariant, pName) != 0
      && JonesInstall_JoinPath(aPath, sizeof(aPath), platform, pDir, aVariant)
      && (bDirectory ? JonesInstall_IsDirectory(aPath) : JonesInstall_IsFile(aPath)) )
    {
        return JonesInstall_CopyPath(pOut, outSize, aPath);
    }

    JonesInstall_MakeCaseVariant(aVariant, sizeof(aVariant), pName, true);
    if ( strcmp(aVariant, pName) != 0
      && JonesInstall_JoinPath(aPath, sizeof(aPath), platform, pDir, aVariant)
      && (bDirectory ? JonesInstall_IsDirectory(aPath) : JonesInstall_IsFile(aPath)) )
    {
        return JonesInstall_CopyPath(pOut, outSize, aPath);
    }

    FindFileData* pFind = stdFileUtil_NewFind(pDir, 0, NULL);
    if ( !pFind )
    {
        return false;
    }

    bool bFound = false;
    tFoundFileInfo info;
    while ( stdFileUtil_FindNext(pFind, &info) )
    {
        if ( (info.bIsDirectory != 0) != bDirectory )
        {
            continue;
        }

        if ( streqi(info.aName, pName)
          && JonesInstall_JoinPath(aPath, sizeof(aPath), platform, pDir, info.aName) )
        {
            bFound = JonesInstall_CopyPath(pOut, outSize, aPath);
            break;
        }
    }

    stdFileUtil_DisposeFind(pFind);
    return bFound;
}

static bool JonesInstall_ResolveRelative(JonesInstallPlatform platform, const char* pRoot,
    const char* pRelative, bool bDirectory, char* pOut, size_t outSize)
{
    if ( !pRoot || !pRelative )
    {
        return false;
    }

    char aCur[JONESINSTALL_MAX_PATH];
    if ( !JonesInstall_CopyPath(aCur, sizeof(aCur), pRoot) )
    {
        return false;
    }

    const char* pCur = pRelative;
    while ( *pCur )
    {
        while ( *pCur == '/' || *pCur == '\\' )
        {
            ++pCur;
        }

        if ( *pCur == '\0' )
        {
            break;
        }

        const char* pStart = pCur;
        while ( *pCur && *pCur != '/' && *pCur != '\\' )
        {
            ++pCur;
        }

        size_t len = (size_t)(pCur - pStart);
        if ( len == 0 || len >= JONESINSTALL_MAX_PATH )
        {
            return false;
        }

        char aComponent[JONESINSTALL_MAX_PATH];
        memcpy(aComponent, pStart, len);
        aComponent[len] = '\0';

        const char* pPeek = pCur;
        while ( *pPeek == '/' || *pPeek == '\\' )
        {
            ++pPeek;
        }

        bool bLast = *pPeek == '\0';
        char aNext[JONESINSTALL_MAX_PATH];
        if ( !JonesInstall_FindEntry(platform, aCur, aComponent, bLast ? bDirectory : true,
                aNext, sizeof(aNext)) )
        {
            return false;
        }

        if ( !JonesInstall_CopyPath(aCur, sizeof(aCur), aNext) )
        {
            return false;
        }
    }

    return JonesInstall_CopyPath(pOut, outSize, aCur);
}

static bool JonesInstall_FindResourceDir(JonesInstallPlatform platform, const char* pRoot,
    char* pOut, size_t outSize)
{
    char aDir[JONESINSTALL_MAX_PATH];
    if ( JonesData_FindResourceDir(pRoot, aDir, sizeof(aDir)) )
    {
        return JonesInstall_CopyPath(pOut, outSize, aDir);
    }

    char aFile[JONESINSTALL_MAX_PATH];
    if ( JonesInstall_FindEntry(platform, pRoot, "Jones3D.GOB", false, aFile, sizeof(aFile)) )
    {
        return JonesInstall_CopyPath(pOut, outSize, pRoot);
    }

    if ( JonesInstall_FindEntry(platform, pRoot, "Resource", true, aDir, sizeof(aDir))
      && JonesInstall_FindEntry(platform, aDir, "Jones3D.GOB", false, aFile, sizeof(aFile)) )
    {
        return JonesInstall_CopyPath(pOut, outSize, aDir);
    }

    return false;
}

static bool JonesInstall_FindResourceFile(JonesInstallPlatform platform, const char* pRoot,
    const char* pName, char* pOut, size_t outSize)
{
    char aResourceDir[JONESINSTALL_MAX_PATH];
    if ( JonesInstall_ResolveRelative(platform, pRoot, "Resource", true, aResourceDir,
            sizeof(aResourceDir)) )
    {
        if ( JonesInstall_FindEntry(platform, aResourceDir, pName, false, pOut, outSize) )
        {
            return true;
        }
    }

    return JonesInstall_FindEntry(platform, pRoot, pName, false, pOut, outSize);
}

static bool JonesInstall_IsCompleteRoot(JonesInstallPlatform platform, const char* pRoot)
{
    char aResourceDir[JONESINSTALL_MAX_PATH];
    if ( !JonesInstall_FindResourceDir(platform, pRoot, aResourceDir, sizeof(aResourceDir)) )
    {
        return false;
    }

    for ( size_t i = 0; i < STD_ARRAYLEN(JonesInstall_aKnownFiles); ++i )
    {
        if ( !JonesInstall_aKnownFiles[i].bRequired )
        {
            continue;
        }

        if ( !JonesInstall_FindEntry(platform, aResourceDir, JonesInstall_aKnownFiles[i].pName,
                false, NULL, 0) )
        {
            return false;
        }
    }

    return true;
}

static char* JonesInstall_ReadTextFile(const char* pPath)
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
    if ( size < 0 || (unsigned long)size > JONESINSTALL_VDF_MAX_SIZE )
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

static bool JonesInstall_PathListContains(const JonesInstallPathList* pList, const char* pPath,
    JonesInstallPlatform platform)
{
    if ( !pList )
    {
        return false;
    }

    for ( size_t i = 0; i < pList->numPaths; ++i )
    {
        if ( JonesInstall_PathsEqual(pList->aPaths[i], pPath, platform) )
        {
            return true;
        }
    }

    return false;
}

static void JonesInstall_PathListAdd(JonesInstallPathList* pList, const char* pPath,
    JonesInstallPlatform platform)
{
    if ( !pList || !pPath || pPath[0] == '\0'
      || pList->numPaths >= JONESINSTALL_MAX_HOST_PATHS
      || JonesInstall_PathListContains(pList, pPath, platform) )
    {
        return;
    }

    if ( JonesInstall_CopyPath(pList->aPaths[pList->numPaths], JONESINSTALL_MAX_PATH, pPath) )
    {
        ++pList->numPaths;
    }
}

static void JonesInstall_AddSource(JonesInstallSourceList* pList, JonesInstallPlatform platform,
    JonesInstallSourceKind kind, const char* pPath, bool bComplete, int discNumber)
{
    if ( !pList || !pPath || pPath[0] == '\0'
      || pList->numSources >= JONESINSTALL_MAX_SOURCES )
    {
        return;
    }

    for ( size_t i = 0; i < pList->numSources; ++i )
    {
        if ( JonesInstall_PathsEqual(pList->aSources[i].aPath, pPath, platform) )
        {
            if ( bComplete )
            {
                pList->aSources[i].bComplete = true;
            }

            return;
        }
    }

    JonesInstallSource* pSource = &pList->aSources[pList->numSources];
    memset(pSource, 0, sizeof(*pSource));
    pSource->kind       = kind;
    pSource->bComplete  = bComplete;
    pSource->discNumber = discNumber;
    if ( JonesInstall_CopyPath(pSource->aPath, sizeof(pSource->aPath), pPath) )
    {
        ++pList->numSources;
    }
}

static void JonesInstall_AddSteamRoot(JonesInstallPathList* pOut, JonesInstallPlatform platform,
    const char* pRoot)
{
    if ( !pRoot || pRoot[0] == '\0' || !JonesInstall_IsDirectory(pRoot) )
    {
        return;
    }

    JonesInstall_PathListAdd(pOut, pRoot, platform);

    char aSteamApps[JONESINSTALL_MAX_PATH];
    char aVdfPath[JONESINSTALL_MAX_PATH];
    if ( !JonesInstall_JoinPath(aSteamApps, sizeof(aSteamApps), platform, pRoot, "steamapps")
      || !JonesInstall_JoinPath(aVdfPath, sizeof(aVdfPath), platform, aSteamApps,
            "libraryfolders.vdf") )
    {
        return;
    }

    char* pText = JonesInstall_ReadTextFile(aVdfPath);
    if ( !pText )
    {
        return;
    }

    JonesDataLocatePathList parsed;
    JonesDataLocate_ParseLibraryFolders(pText, &parsed);
    STDFREE(pText);

    for ( size_t i = 0; i < parsed.numPaths; ++i )
    {
        if ( JonesInstall_IsDirectory(parsed.aPaths[i]) )
        {
            JonesInstall_PathListAdd(pOut, parsed.aPaths[i], platform);
        }
    }
}

static void JonesInstall_AddDefaultSteamRoots(JonesInstallPathList* pOut,
    JonesInstallPlatform platform, const JonesInstallHost* pHost)
{
    char aRoot[JONESINSTALL_MAX_PATH];
    char aSub[JONESINSTALL_MAX_PATH];

    if ( platform == JONESINSTALL_PLATFORM_WINDOWS )
    {
        if ( pHost->pProgramFilesX86 && pHost->pProgramFilesX86[0] != '\0'
          && JonesInstall_JoinPath(aRoot, sizeof(aRoot), platform, pHost->pProgramFilesX86,
                "Steam") )
        {
            JonesInstall_AddSteamRoot(pOut, platform, aRoot);
        }

        if ( pHost->pProgramFiles && pHost->pProgramFiles[0] != '\0'
          && JonesInstall_JoinPath(aRoot, sizeof(aRoot), platform, pHost->pProgramFiles, "Steam") )
        {
            JonesInstall_AddSteamRoot(pOut, platform, aRoot);
        }

        return;
    }

    if ( !pHost->pHome || pHost->pHome[0] == '\0' )
    {
        return;
    }

    if ( JonesInstall_JoinPath(aRoot, sizeof(aRoot), platform, pHost->pHome, ".steam")
      && JonesInstall_JoinPath(aSub, sizeof(aSub), platform, aRoot, "steam") )
    {
        JonesInstall_AddSteamRoot(pOut, platform, aSub);
    }

    if ( JonesInstall_JoinPath(aRoot, sizeof(aRoot), platform, pHost->pHome, ".local")
      && JonesInstall_JoinPath(aSub, sizeof(aSub), platform, aRoot, "share")
      && JonesInstall_JoinPath(aRoot, sizeof(aRoot), platform, aSub, "Steam") )
    {
        JonesInstall_AddSteamRoot(pOut, platform, aRoot);
    }

    if ( JonesInstall_JoinPath(aRoot, sizeof(aRoot), platform, pHost->pHome, ".var")
      && JonesInstall_JoinPath(aSub, sizeof(aSub), platform, aRoot, "app")
      && JonesInstall_JoinPath(aRoot, sizeof(aRoot), platform, aSub,
            "com.valvesoftware.Steam")
      && JonesInstall_JoinPath(aSub, sizeof(aSub), platform, aRoot, ".local")
      && JonesInstall_JoinPath(aRoot, sizeof(aRoot), platform, aSub, "share")
      && JonesInstall_JoinPath(aSub, sizeof(aSub), platform, aRoot, "Steam") )
    {
        JonesInstall_AddSteamRoot(pOut, platform, aSub);
    }

    if ( JonesInstall_JoinPath(aRoot, sizeof(aRoot), platform, pHost->pHome, "snap")
      && JonesInstall_JoinPath(aSub, sizeof(aSub), platform, aRoot, "steam")
      && JonesInstall_JoinPath(aRoot, sizeof(aRoot), platform, aSub, "common")
      && JonesInstall_JoinPath(aSub, sizeof(aSub), platform, aRoot, ".local")
      && JonesInstall_JoinPath(aRoot, sizeof(aRoot), platform, aSub, "share")
      && JonesInstall_JoinPath(aSub, sizeof(aSub), platform, aRoot, "Steam") )
    {
        JonesInstall_AddSteamRoot(pOut, platform, aSub);
    }
}

static void JonesInstall_AddSteamSources(JonesInstallSourceList* pOut,
    JonesInstallPlatform platform, const JonesInstallHost* pHost)
{
    JonesInstallPathList libraries;
    memset(&libraries, 0, sizeof(libraries));
    JonesInstall_AddDefaultSteamRoots(&libraries, platform, pHost);

    for ( size_t i = 0; i < libraries.numPaths; ++i )
    {
        char aSteamApps[JONESINSTALL_MAX_PATH];
        char aCommon[JONESINSTALL_MAX_PATH];
        char aGame[JONESINSTALL_MAX_PATH];
        if ( !JonesInstall_JoinPath(aSteamApps, sizeof(aSteamApps), platform, libraries.aPaths[i],
                "steamapps")
          || !JonesInstall_JoinPath(aCommon, sizeof(aCommon), platform, aSteamApps, "common")
          || !JonesInstall_JoinPath(aGame, sizeof(aGame), platform, aCommon,
                JONESINSTALL_STEAM_NAME)
          || !JonesInstall_IsDirectory(aGame) )
        {
            continue;
        }

        JonesInstall_AddSource(pOut, platform, JONESINSTALL_SOURCE_STEAM, aGame,
            JonesInstall_IsCompleteRoot(platform, aGame), 0);
    }
}

static void JonesInstall_AddInstalledSource(JonesInstallSourceList* pOut,
    JonesInstallPlatform platform, const char* pPath)
{
    if ( !pPath || pPath[0] == '\0' || !JonesInstall_IsDirectory(pPath) )
    {
        return;
    }

    JonesInstall_AddSource(pOut, platform, JONESINSTALL_SOURCE_GOG_INSTALLED, pPath,
        JonesInstall_IsCompleteRoot(platform, pPath), 0);
}

static void JonesInstall_AddDefaultGogFolders(JonesInstallSourceList* pOut,
    JonesInstallPlatform platform, const JonesInstallHost* pHost)
{
    char aDir[JONESINSTALL_MAX_PATH];
    char aSub[JONESINSTALL_MAX_PATH];

    if ( platform == JONESINSTALL_PLATFORM_WINDOWS )
    {
        if ( JonesInstall_JoinPath(aDir, sizeof(aDir), platform, "C:\\GOG Games",
                JONESINSTALL_STEAM_NAME) )
        {
            JonesInstall_AddInstalledSource(pOut, platform, aDir);
        }

        if ( pHost->pProgramFilesX86 && pHost->pProgramFilesX86[0] != '\0'
          && JonesInstall_JoinPath(aDir, sizeof(aDir), platform, pHost->pProgramFilesX86,
                "GOG Galaxy")
          && JonesInstall_JoinPath(aSub, sizeof(aSub), platform, aDir, "Games")
          && JonesInstall_JoinPath(aDir, sizeof(aDir), platform, aSub, JONESINSTALL_STEAM_NAME) )
        {
            JonesInstall_AddInstalledSource(pOut, platform, aDir);
        }

        return;
    }

    if ( !pHost->pHome || pHost->pHome[0] == '\0' )
    {
        return;
    }

    if ( JonesInstall_JoinPath(aDir, sizeof(aDir), platform, pHost->pHome, "GOG Games")
      && JonesInstall_JoinPath(aSub, sizeof(aSub), platform, aDir, JONESINSTALL_STEAM_NAME) )
    {
        JonesInstall_AddInstalledSource(pOut, platform, aSub);
    }

    if ( JonesInstall_JoinPath(aDir, sizeof(aDir), platform, pHost->pHome, "Games")
      && JonesInstall_JoinPath(aSub, sizeof(aSub), platform, aDir, JONESINSTALL_STEAM_NAME) )
    {
        JonesInstall_AddInstalledSource(pOut, platform, aSub);
    }

    if ( JonesInstall_JoinPath(aDir, sizeof(aDir), platform, pHost->pHome, "Games")
      && JonesInstall_JoinPath(aSub, sizeof(aSub), platform, aDir, "Heroic")
      && JonesInstall_JoinPath(aDir, sizeof(aDir), platform, aSub, JONESINSTALL_STEAM_NAME) )
    {
        JonesInstall_AddInstalledSource(pOut, platform, aDir);
    }

    if ( JonesInstall_JoinPath(aDir, sizeof(aDir), platform, pHost->pHome, ".wine")
      && JonesInstall_JoinPath(aSub, sizeof(aSub), platform, aDir, "drive_c")
      && JonesInstall_JoinPath(aDir, sizeof(aDir), platform, aSub, "GOG Games")
      && JonesInstall_JoinPath(aSub, sizeof(aSub), platform, aDir, JONESINSTALL_STEAM_NAME) )
    {
        JonesInstall_AddInstalledSource(pOut, platform, aSub);
    }
}

static void JonesInstall_FindGogInstallersInDir(JonesInstallSourceList* pOut,
    JonesInstallPlatform platform, const char* pDir)
{
    if ( !pDir || pDir[0] == '\0' || !JonesInstall_IsDirectory(pDir) )
    {
        return;
    }

    FindFileData* pFind = stdFileUtil_NewFind(pDir, 3, "exe");
    if ( !pFind )
    {
        return;
    }

    const size_t prefixLen = strlen(JONESINSTALL_GOG_INSTALLER_PREFIX);
    const size_t extLen    = strlen(JONESINSTALL_GOG_INSTALLER_EXTENSION);

    tFoundFileInfo info;
    while ( stdFileUtil_FindNext(pFind, &info) )
    {
        if ( info.bIsDirectory )
        {
            continue;
        }

        size_t nameLen = strlen(info.aName);
        if ( nameLen < prefixLen + extLen
          || !strneqi(info.aName, JONESINSTALL_GOG_INSTALLER_PREFIX, prefixLen)
          || strcmpi(&info.aName[nameLen - extLen], JONESINSTALL_GOG_INSTALLER_EXTENSION) != 0 )
        {
            continue;
        }

        char aPath[JONESINSTALL_MAX_PATH];
        if ( JonesInstall_JoinPath(aPath, sizeof(aPath), platform, pDir, info.aName) )
        {
            JonesInstall_AddSource(pOut, platform, JONESINSTALL_SOURCE_GOG_INSTALLER, aPath,
                false, 0);
        }
    }

    stdFileUtil_DisposeFind(pFind);
}

static bool JonesInstall_IsDiscRoot(JonesInstallPlatform platform, const char* pRoot,
    int discNumber)
{
    if ( !pRoot || !JonesInstall_IsDirectory(pRoot) )
    {
        return false;
    }

    if ( discNumber == 1 )
    {
        char aLicense[JONESINSTALL_MAX_PATH];
        if ( !JonesInstall_ResolveRelative(platform, pRoot, "install/license.txt", false, aLicense,
                sizeof(aLicense)) )
        {
            return false;
        }

        return JonesInstall_FindResourceFile(platform, pRoot, "CD1.GOB", NULL, 0);
    }

    return JonesInstall_FindResourceFile(platform, pRoot, "CD2.GOB", NULL, 0);
}

static void JonesInstall_AddDiscAt(JonesInstallSourceList* pOut, JonesInstallPlatform platform,
    const char* pRoot)
{
    bool bComplete = JonesInstall_IsCompleteRoot(platform, pRoot);
    if ( JonesInstall_IsDiscRoot(platform, pRoot, 1) )
    {
        JonesInstall_AddSource(pOut, platform, JONESINSTALL_SOURCE_DISC, pRoot, bComplete, 1);
    }
    else if ( JonesInstall_IsDiscRoot(platform, pRoot, 2) )
    {
        JonesInstall_AddSource(pOut, platform, JONESINSTALL_SOURCE_DISC, pRoot, bComplete, 2);
    }
}

static void JonesInstall_ScanVolumeRoot(JonesInstallSourceList* pOut,
    JonesInstallPlatform platform, const char* pRoot)
{
    if ( !pRoot || pRoot[0] == '\0' || !JonesInstall_IsDirectory(pRoot) )
    {
        return;
    }

    JonesInstall_AddDiscAt(pOut, platform, pRoot);
    if ( platform == JONESINSTALL_PLATFORM_WINDOWS )
    {
        return;
    }

    // The host may list a mount base such as /run/media/<user> or /mnt; a disc is one of its
    // immediate subdirectories.
    FindFileData* pFind = stdFileUtil_NewFind(pRoot, 0, NULL);
    if ( !pFind )
    {
        return;
    }

    tFoundFileInfo info;
    while ( stdFileUtil_FindNext(pFind, &info) )
    {
        if ( !info.bIsDirectory || strcmp(info.aName, ".") == 0 || strcmp(info.aName, "..") == 0 )
        {
            continue;
        }

        char aChild[JONESINSTALL_MAX_PATH];
        if ( JonesInstall_JoinPath(aChild, sizeof(aChild), platform, pRoot, info.aName) )
        {
            JonesInstall_AddDiscAt(pOut, platform, aChild);
        }
    }

    stdFileUtil_DisposeFind(pFind);
}

const char* JonesInstall_SourceKindName(JonesInstallSourceKind kind)
{
    switch ( kind )
    {
        case JONESINSTALL_SOURCE_STEAM:
            return "Steam";

        case JONESINSTALL_SOURCE_GOG_INSTALLED:
            return "GOG";

        case JONESINSTALL_SOURCE_GOG_INSTALLER:
            return "GOG installer";

        case JONESINSTALL_SOURCE_DISC:
            return "disc";

        case JONESINSTALL_SOURCE_FOLDER:
            return "folder";

        default:
            return "unknown source";
    }
}

bool JonesInstall_FindSources(const JonesInstallHost* pHost, JonesInstallSourceList* pOut)
{
    if ( !pOut )
    {
        return false;
    }

    memset(pOut, 0, sizeof(*pOut));
    if ( !pHost )
    {
        return false;
    }

    JonesInstallPlatform platform = pHost->platform;

    JonesInstall_AddSteamSources(pOut, platform, pHost);

    if ( pHost->pGogRegistryPaths )
    {
        for ( size_t i = 0; i < pHost->pGogRegistryPaths->numPaths; ++i )
        {
            JonesInstall_AddInstalledSource(pOut, platform, pHost->pGogRegistryPaths->aPaths[i]);
        }
    }

    JonesInstall_AddDefaultGogFolders(pOut, platform, pHost);

    char aDownloads[JONESINSTALL_MAX_PATH];
    if ( pHost->pHome && pHost->pHome[0] != '\0'
      && JonesInstall_JoinPath(aDownloads, sizeof(aDownloads), platform, pHost->pHome, "Downloads") )
    {
        JonesInstall_FindGogInstallersInDir(pOut, platform, aDownloads);
    }

    if ( pHost->pXdgDownloadDir && pHost->pXdgDownloadDir[0] != '\0' )
    {
        JonesInstall_FindGogInstallersInDir(pOut, platform, pHost->pXdgDownloadDir);
    }

    if ( pHost->pHome && pHost->pHome[0] != '\0' )
    {
        JonesInstall_FindGogInstallersInDir(pOut, platform, pHost->pHome);
    }

    if ( pHost->pVolumeRoots )
    {
        for ( size_t i = 0; i < pHost->pVolumeRoots->numPaths; ++i )
        {
            JonesInstall_ScanVolumeRoot(pOut, platform, pHost->pVolumeRoots->aPaths[i]);
        }
    }

    return pOut->numSources > 0;
}

bool JonesInstall_GetFolderSource(const JonesInstallHost* pHost, const char* pFolder,
    JonesInstallSource* pOut)
{
    if ( !pOut )
    {
        return false;
    }

    memset(pOut, 0, sizeof(*pOut));
    if ( !pHost || !pFolder || pFolder[0] == '\0' || !JonesInstall_IsDirectory(pFolder) )
    {
        return false;
    }

    pOut->kind       = JONESINSTALL_SOURCE_FOLDER;
    pOut->discNumber = 0;
    pOut->bComplete  = JonesInstall_IsCompleteRoot(pHost->platform, pFolder);
    if ( !JonesInstall_CopyPath(pOut->aPath, sizeof(pOut->aPath), pFolder) )
    {
        return false;
    }

    JonesInstall_UsePlatformSeparators(pOut->aPath, pHost->platform);
    return true;
}

static bool JonesInstall_PlanHasDestination(const JonesInstallPlan* pPlan,
    const char* pDestination)
{
    for ( size_t i = 0; i < pPlan->numItems; ++i )
    {
        if ( strcmpi(pPlan->aItems[i].aDestination, pDestination) == 0 )
        {
            return true;
        }
    }

    return false;
}

static bool JonesInstall_AddPlanItem(JonesInstallPlan* pPlan, const char* pSource,
    const char* pDestination, int64_t size)
{
    if ( !pPlan || !pSource || !pDestination || size < 0
      || pPlan->numItems >= JONESINSTALL_MAX_PLAN_ITEMS
      || JonesInstall_PlanHasDestination(pPlan, pDestination)
      || pPlan->totalBytes > INT64_MAX - size )
    {
        return false;
    }

    JonesInstallPlanItem* pItem = &pPlan->aItems[pPlan->numItems];
    if ( !JonesInstall_CopyPath(pItem->aSource, sizeof(pItem->aSource), pSource)
      || !JonesInstall_CopyPath(pItem->aDestination, sizeof(pItem->aDestination), pDestination) )
    {
        return false;
    }

    pItem->size = size;
    pPlan->totalBytes += size;
    ++pPlan->numItems;
    return true;
}

static bool JonesInstall_HasExtension(const char* pName, const char* pExtension)
{
    size_t nameLen = strlen(pName);
    size_t extLen  = strlen(pExtension);
    return nameLen > extLen
        && pName[nameLen - extLen - 1] == '.'
        && strcmpi(&pName[nameLen - extLen], pExtension) == 0;
}

static void JonesInstall_AddPlanDir(JonesInstallPlan* pPlan, JonesInstallPlatform platform,
    const char* pDir, const char* pDestinationDir, const char* pExtension, bool bSkipExisting)
{
    FindFileData* pFind = stdFileUtil_NewFind(pDir, pExtension ? 3 : 0, pExtension);
    if ( !pFind )
    {
        return;
    }

    tFoundFileInfo info;
    while ( stdFileUtil_FindNext(pFind, &info) )
    {
        if ( info.bIsDirectory || (pExtension && !JonesInstall_HasExtension(info.aName, pExtension)) )
        {
            continue;
        }

        char aSource[JONESINSTALL_MAX_PATH];
        char aDestination[JONESINSTALL_MAX_RELATIVE];
        if ( !JonesInstall_JoinPath(aSource, sizeof(aSource), platform, pDir, info.aName) )
        {
            continue;
        }

        int len = snprintf(aDestination, sizeof(aDestination), "%s/%s", pDestinationDir,
            info.aName);
        if ( len <= 0 || (size_t)len >= sizeof(aDestination) )
        {
            continue;
        }

        if ( bSkipExisting )
        {
            char aTargetPath[JONESINSTALL_MAX_PATH];
            if ( JonesInstall_JoinRelative(aTargetPath, sizeof(aTargetPath), platform,
                    pPlan->aTargetDir, aDestination)
              && JonesInstall_Stat(aTargetPath, NULL, NULL, NULL) )
            {
                continue;
            }
        }

        int64_t size;
        if ( !JonesInstall_GetFileSize(aSource, &size) )
        {
            continue;
        }

        JonesInstall_AddPlanItem(pPlan, aSource, aDestination, size);
    }

    stdFileUtil_DisposeFind(pFind);
}

static int JonesInstall_ComparePlanItems(const void* pLeft, const void* pRight)
{
    const JonesInstallPlanItem* pItemLeft  = (const JonesInstallPlanItem*)pLeft;
    const JonesInstallPlanItem* pItemRight = (const JonesInstallPlanItem*)pRight;

    int result = strcmpi(pItemLeft->aDestination, pItemRight->aDestination);
    if ( result != 0 )
    {
        return result;
    }

    return strcmpi(pItemLeft->aSource, pItemRight->aSource);
}

static void JonesInstall_AppendMissing(char* pOut, size_t outSize, const char* pName,
    size_t* pLen)
{
    if ( !pOut || outSize == 0 || !pName )
    {
        return;
    }

    const char* pSeparator = *pLen == 0 ? "" : ", ";
    size_t needed = *pLen + strlen(pSeparator) + strlen(pName) + 1;
    if ( needed > outSize )
    {
        return;
    }

    memcpy(&pOut[*pLen], pSeparator, strlen(pSeparator) + 1);
    *pLen += strlen(pSeparator);
    memcpy(&pOut[*pLen], pName, strlen(pName) + 1);
    *pLen += strlen(pName);
}

bool JonesInstall_BuildPlan(JonesInstallPlatform platform, const char* pSourceRoot,
    const char* pTargetDataDir, bool bCopySaves, JonesInstallPlan* pOut)
{
    if ( !pOut )
    {
        return false;
    }

    memset(pOut, 0, sizeof(*pOut));
    if ( !pSourceRoot || pSourceRoot[0] == '\0'
      || !pTargetDataDir || pTargetDataDir[0] == '\0'
      || !JonesInstall_CopyPath(pOut->aTargetDir, sizeof(pOut->aTargetDir), pTargetDataDir) )
    {
        return false;
    }

    pOut->platform = platform;
    JonesInstall_UsePlatformSeparators(pOut->aTargetDir, platform);

    for ( size_t i = 0; i < STD_ARRAYLEN(JonesInstall_aKnownFiles); ++i )
    {
        char aSource[JONESINSTALL_MAX_PATH];
        int64_t size;
        if ( !JonesInstall_FindResourceFile(platform, pSourceRoot, JonesInstall_aKnownFiles[i].pName,
                aSource, sizeof(aSource))
          || !JonesInstall_GetFileSize(aSource, &size) )
        {
            continue;
        }

        char aDestination[JONESINSTALL_MAX_RELATIVE];
        int len = snprintf(aDestination, sizeof(aDestination), "Resource/%s",
            JonesInstall_aKnownFiles[i].pName);
        if ( len > 0 && (size_t)len < sizeof(aDestination) )
        {
            JonesInstall_AddPlanItem(pOut, aSource, aDestination, size);
        }
    }

    char aDir[JONESINSTALL_MAX_PATH];
    if ( JonesInstall_ResolveRelative(platform, pSourceRoot, "Resource/Cog", true, aDir,
            sizeof(aDir))
      || JonesInstall_ResolveRelative(platform, pSourceRoot, "Cog", true, aDir, sizeof(aDir)) )
    {
        JonesInstall_AddPlanDir(pOut, platform, aDir, "Resource/Cog", NULL, false);
    }

    if ( JonesInstall_ResolveRelative(platform, pSourceRoot, "KeySets", true, aDir, sizeof(aDir)) )
    {
        JonesInstall_AddPlanDir(pOut, platform, aDir, "KeySets", "kfg", false);
    }

    for ( size_t i = 0; i < STD_ARRAYLEN(JonesInstall_aHelpFiles); ++i )
    {
        char aSource[JONESINSTALL_MAX_PATH];
        int64_t size;
        if ( !JonesInstall_FindResourceFile(platform, pSourceRoot, JonesInstall_aHelpFiles[i],
                aSource, sizeof(aSource))
          || !JonesInstall_GetFileSize(aSource, &size) )
        {
            continue;
        }

        JonesInstall_AddPlanItem(pOut, aSource, JonesInstall_aHelpFiles[i], size);
    }

    if ( bCopySaves
      && JonesInstall_ResolveRelative(platform, pSourceRoot, "SaveGames", true, aDir,
            sizeof(aDir)) )
    {
        JonesInstall_AddPlanDir(pOut, platform, aDir, "SaveGames", "nds", true);
    }

    size_t numMissingLen = 0;
    pOut->bComplete = true;
    for ( size_t i = 0; i < STD_ARRAYLEN(JonesInstall_aKnownFiles); ++i )
    {
        if ( !JonesInstall_aKnownFiles[i].bRequired )
        {
            continue;
        }

        char aDestination[64];
        int len = snprintf(aDestination, sizeof(aDestination), "Resource/%s",
            JonesInstall_aKnownFiles[i].pName);
        if ( len <= 0 || (size_t)len >= sizeof(aDestination)
          || !JonesInstall_PlanHasDestination(pOut, aDestination) )
        {
            pOut->bComplete = false;
            JonesInstall_AppendMissing(pOut->aMissing, sizeof(pOut->aMissing),
                JonesInstall_aKnownFiles[i].pName, &numMissingLen);
        }
    }

    pOut->bNeedsDisc2 = JonesInstall_IsDiscRoot(platform, pSourceRoot, 1)
        && !JonesInstall_PlanHasDestination(pOut, "Resource/CD2.GOB");

    if ( pOut->numItems == 0 )
    {
        return false;
    }

    qsort(pOut->aItems, pOut->numItems, sizeof(pOut->aItems[0]),
        JonesInstall_ComparePlanItems);
    return true;
}

static bool JonesInstall_DirName(char* pOut, size_t outSize, const char* pPath)
{
    if ( !pOut || outSize == 0 || !pPath )
    {
        return false;
    }

    const char* pLast = NULL;
    for ( const char* pCur = pPath; *pCur; ++pCur )
    {
        if ( *pCur == '/' || *pCur == '\\' )
        {
            pLast = pCur;
        }
    }

    if ( !pLast )
    {
        pOut[0] = '\0';
        return true;
    }

    size_t len = (size_t)(pLast - pPath);
    if ( len == 0 )
    {
        len = 1;
    }

    if ( len + 1 > outSize )
    {
        return false;
    }

    memcpy(pOut, pPath, len);
    pOut[len] = '\0';
    return true;
}

static JonesInstallRunResult JonesInstall_SetError(JonesInstallRunOutcome* pOut,
    const char* pFormat, ...)
{
    va_list args;
    va_start(args, pFormat);
    vsnprintf(pOut->aMessage, sizeof(pOut->aMessage), pFormat, args);
    va_end(args);
    pOut->result = JONESINSTALL_RUN_ERROR;
    return pOut->result;
}

static JonesInstallRunResult JonesInstall_SetCancelled(JonesInstallRunOutcome* pOut)
{
    snprintf(pOut->aMessage, sizeof(pOut->aMessage), "Installation cancelled.");
    pOut->result = JONESINSTALL_RUN_CANCELLED;
    return pOut->result;
}

JonesInstallRunResult JonesInstall_Run(const JonesInstallPlan* pPlan, const JonesInstallHost* pHost,
    JonesInstallProgressFunc pfProgress, void* pProgressUserData, const bool* pbCancel,
    JonesInstallRunOutcome* pOut)
{
    if ( !pOut )
    {
        return JONESINSTALL_RUN_ERROR;
    }

    memset(pOut, 0, sizeof(*pOut));
    pOut->result = JONESINSTALL_RUN_OK;

    if ( !pPlan || pPlan->numItems == 0 || pPlan->aTargetDir[0] == '\0' )
    {
        return JonesInstall_SetError(pOut, "The install plan is empty.");
    }

    pOut->bNeedsDisc2 = pPlan->bNeedsDisc2;

    if ( pbCancel && *pbCancel )
    {
        return JonesInstall_SetCancelled(pOut);
    }

    int64_t total = pPlan->totalBytes;
    if ( total < 0 )
    {
        return JonesInstall_SetError(pOut, "The install plan has an invalid size.");
    }

    int64_t required = total;
    if ( total > INT64_MAX - total / 20 )
    {
        required = INT64_MAX;
    }
    else
    {
        required = total + total / 20;
    }

    if ( pHost && pHost->pfGetFreeBytes )
    {
        int64_t freeBytes = 0;
        if ( pHost->pfGetFreeBytes(pPlan->aTargetDir, &freeBytes, pHost->pFreeBytesUserData)
          && freeBytes < required )
        {
            return JonesInstall_SetError(pOut,
                "Not enough free disk space in '%s': %lld bytes are needed, only %lld are available.",
                pPlan->aTargetDir, (long long)required, (long long)freeBytes);
        }
    }

    const char* pCurrent = pPlan->aItems[0].aDestination;
    if ( pfProgress )
    {
        pfProgress(0, total, pCurrent, pProgressUserData);
    }

    int64_t done = 0;
    for ( size_t i = 0; i < pPlan->numItems; ++i )
    {
        const JonesInstallPlanItem* pItem = &pPlan->aItems[i];

        if ( pbCancel && *pbCancel )
        {
            return JonesInstall_SetCancelled(pOut);
        }

        char aDestPath[JONESINSTALL_MAX_PATH];
        if ( !JonesInstall_JoinRelative(aDestPath, sizeof(aDestPath), pPlan->platform,
                pPlan->aTargetDir, pItem->aDestination) )
        {
            return JonesInstall_SetError(pOut,
                "The destination path for '%s' is too long.", pItem->aDestination);
        }

        int64_t destSize;
        if ( JonesInstall_GetFileSize(aDestPath, &destSize) && destSize == pItem->size )
        {
            done += pItem->size;
            pCurrent = pItem->aDestination;
            if ( pfProgress )
            {
                pfProgress(done, total, pCurrent, pProgressUserData);
            }

            continue;
        }

        char aDir[JONESINSTALL_MAX_PATH];
        if ( !JonesInstall_DirName(aDir, sizeof(aDir), aDestPath) || !JonesInstall_MakeDirs(aDir) )
        {
            return JonesInstall_SetError(pOut, "Could not create the folder for '%s'.",
                pItem->aDestination);
        }

        char aTempPath[JONESINSTALL_MAX_PATH + 8];
        int tempLen = snprintf(aTempPath, sizeof(aTempPath), "%s.part", aDestPath);
        if ( tempLen <= 0 || (size_t)tempLen >= sizeof(aTempPath) )
        {
            return JonesInstall_SetError(pOut,
                "The temporary path for '%s' is too long.", pItem->aDestination);
        }

        FILE* pIn = NULL;
        if ( fopen_s(&pIn, pItem->aSource, "rb") != 0 || !pIn )
        {
            return JonesInstall_SetError(pOut, "Could not open the source file '%s'.",
                pItem->aSource);
        }

        FILE* pCopy = NULL;
        if ( fopen_s(&pCopy, aTempPath, "wb") != 0 || !pCopy )
        {
            fclose(pIn);
            return JonesInstall_SetError(pOut, "Could not create the temporary file '%s'.",
                aTempPath);
        }

        bool bFailed = false;
        const char* pFailedStep = NULL;
        char aBuffer[JONESINSTALL_COPY_SIZE];

        for ( ;; )
        {
            if ( pbCancel && *pbCancel )
            {
                fclose(pCopy);
                fclose(pIn);
                remove(aTempPath);
                return JonesInstall_SetCancelled(pOut);
            }

            size_t numRead = fread(aBuffer, 1, sizeof(aBuffer), pIn);
            if ( numRead > 0 )
            {
                if ( fwrite(aBuffer, 1, numRead, pCopy) != numRead )
                {
                    bFailed = true;
                    pFailedStep = "write";
                    break;
                }

                done += (int64_t)numRead;
                pCurrent = pItem->aDestination;
                if ( pfProgress )
                {
                    pfProgress(done, total, pCurrent, pProgressUserData);
                }
            }

            if ( numRead < sizeof(aBuffer) )
            {
                if ( ferror(pIn) )
                {
                    bFailed = true;
                    pFailedStep = "read";
                }

                break;
            }
        }

        bool bCloseFailed = fclose(pCopy) != 0;
        fclose(pIn);

        if ( bFailed || bCloseFailed )
        {
            remove(aTempPath);
            if ( bFailed && pFailedStep && strcmp(pFailedStep, "read") == 0 )
            {
                return JonesInstall_SetError(pOut, "Could not read the source file '%s'.",
                    pItem->aSource);
            }

            return JonesInstall_SetError(pOut,
                "Could not write the file '%s'. Is the disc readable and is there enough disk space?",
                pItem->aDestination);
        }

#ifdef _WIN32
        remove(aDestPath);
#endif
        if ( rename(aTempPath, aDestPath) != 0 )
        {
            remove(aTempPath);
            return JonesInstall_SetError(pOut, "Could not replace the file '%s'.",
                pItem->aDestination);
        }
    }

    if ( pfProgress )
    {
        pfProgress(total, total, pCurrent, pProgressUserData);
    }

    return JONESINSTALL_RUN_OK;
}

bool JonesInstall_CheckTarget(const char* pTargetDataDir, JonesDataReport* pOut)
{
    if ( !pOut )
    {
        return false;
    }

    char aResourceDir[JONESINSTALL_MAX_PATH];
    bool bFound = pTargetDataDir && pTargetDataDir[0] != '\0'
        && JonesData_FindResourceDir(pTargetDataDir, aResourceDir, sizeof(aResourceDir));

    return JonesData_CheckDir(bFound ? aResourceDir : NULL, NULL, pOut);
}
