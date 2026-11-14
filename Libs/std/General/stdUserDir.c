#include "stdUserDir.h"

#include <std/General/std.h>
#include <std/General/stdFileUtil.h>
#include <std/General/stdFnames.h>
#include <std/General/stdUtil.h>

#include <limits.h>
#include <string.h>

// Added: the writable directory for user output (ADR-0019); "" is the working directory.
static char stdUserDir_aPath[STDUSERDIR_MAXPATH];

#define STDUSERDIR_COPYBUFFER 4096u
#define STDUSERDIR_JOINMAX (STDUSERDIR_MAXPATH * 2u)

void stdUserDir_Reset(void)
{
    stdUserDir_aPath[0] = '\0';
}

const char* J3DAPI stdUserDir_GetPath(void)
{
    return stdUserDir_aPath;
}

// Drops trailing separators, but keeps a root ("/", "\\", "C:\", "C:/") intact.
static void stdUserDir_TrimTrailingSeparators(char* pPath)
{
    size_t len = strlen(pPath);
    while ( len > 1 && (pPath[len - 1] == '/' || pPath[len - 1] == '\\') )
    {
        if ( len == 3 && pPath[1] == ':' )
        {
            break;
        }

        pPath[--len] = '\0';
    }
}

// Creates pPath and any missing parent directories, each ignored when it already exists.
static void stdUserDir_CreatePath(const char* pPath)
{
    char aPath[STDUSERDIR_MAXPATH];
    stdUtil_StringCopy(aPath, sizeof(aPath), pPath);

    if ( stdFileUtil_FileExists(aPath) )
    {
        return;
    }

    for ( size_t i = 1; aPath[i] != '\0'; ++i )
    {
        if ( aPath[i] != '/' && aPath[i] != '\\' )
        {
            continue;
        }

        const char chSeparator = aPath[i];
        aPath[i] = '\0';
        if ( aPath[0] != '\0' && !(aPath[1] == ':' && aPath[2] == '\0') )
        {
            if ( !stdFileUtil_FileExists(aPath) )
            {
                stdFileUtil_MkDir(aPath);
            }
        }

        aPath[i] = chSeparator;
    }

    if ( !stdFileUtil_FileExists(aPath) )
    {
        stdFileUtil_MkDir(aPath);
    }
}

bool J3DAPI stdUserDir_Set(const char* pPath)
{
    if ( !pPath )
    {
        pPath = "";
    }

    size_t len = strlen(pPath);
    if ( len >= sizeof(stdUserDir_aPath) )
    {
        return false;
    }

    char aPath[STDUSERDIR_MAXPATH];
    stdUtil_StringCopy(aPath, sizeof(aPath), pPath);
    stdUserDir_TrimTrailingSeparators(aPath);

    stdUtil_StringCopy(stdUserDir_aPath, sizeof(stdUserDir_aPath), aPath);

    if ( stdUserDir_aPath[0] != '\0' )
    {
        stdUserDir_CreatePath(stdUserDir_aPath);
    }

    return true;
}

// Joins pDir and pName with the stdFnames_MakePath conventions. Returns false without touching
// pOut when the result does not fit.
static bool stdUserDir_Join(char* pOut, size_t outSize, const char* pDir, const char* pName)
{
    if ( !pOut || outSize == 0 )
    {
        return false;
    }

    const char* pRelative = pName ? pName : "";
    if ( *pRelative == '\\' )
    {
        ++pRelative;
    }

    const size_t dirLen  = strlen(pDir);
    const size_t sepLen  = ( dirLen > 0 && pDir[dirLen - 1] != '\\' ) ? 1u : 0u;
    const size_t nameLen = strlen(pRelative);
    if ( dirLen + sepLen + nameLen + 1u > outSize )
    {
        return false;
    }

    const int size = ( outSize > (size_t)INT_MAX ) ? INT_MAX : (int)outSize;
    stdFnames_MakePath(pOut, size, pDir, pRelative);
    return true;
}

bool J3DAPI stdUserDir_MakePath(char* pOut, size_t outSize, const char* pRelative)
{
    if ( !pOut || outSize == 0 )
    {
        return false;
    }

    if ( stdUserDir_aPath[0] == '\0' )
    {
        const char* pName = pRelative ? pRelative : "";
        const size_t nameLen = strlen(pName);
        if ( nameLen + 1u > outSize )
        {
            return false;
        }

        stdUtil_StringCopy(pOut, outSize, pName);
        return true;
    }

    return stdUserDir_Join(pOut, outSize, stdUserDir_aPath, pRelative);
}

// Copies pSource to pDest unless pDest exists. Returns true only when the copy happened.
static bool stdUserDir_CopyFile(const char* pSource, const char* pDest)
{
    if ( stdFileUtil_FileExists(pDest) )
    {
        return false;
    }

    tFileHandle hSource = stdFileOpen(pSource, "rb");
    if ( !hSource )
    {
        return false;
    }

    tFileHandle hDest = stdFileOpen(pDest, "wb");
    if ( !hDest )
    {
        stdFileClose(hSource);
        return false;
    }

    bool bOk = true;
    char aBuffer[STDUSERDIR_COPYBUFFER];
    while ( 1 )
    {
        const size_t numRead = stdFileRead(hSource, aBuffer, sizeof(aBuffer));
        if ( numRead == 0 )
        {
            break;
        }

        if ( stdFileWrite(hDest, aBuffer, numRead) != numRead )
        {
            bOk = false;
            break;
        }
    }

    stdFileClose(hSource);
    stdFileClose(hDest);

    if ( !bOk )
    {
        stdFileUtil_DelFile(pDest);
    }

    return bOk;
}

bool J3DAPI stdUserDir_CopyFileIfMissing(const char* pSourcePath, const char* pRelativeName)
{
    if ( stdUserDir_aPath[0] == '\0' || !pSourcePath || !pRelativeName )
    {
        return false;
    }

    char aDest[STDUSERDIR_JOINMAX];
    if ( !stdUserDir_Join(aDest, sizeof(aDest), stdUserDir_aPath, pRelativeName) )
    {
        return false;
    }

    return stdUserDir_CopyFile(pSourcePath, aDest);
}

// True when pName ends with ".pExtension", ignoring ASCII case.
static bool stdUserDir_NameHasExtension(const char* pName, const char* pExtension)
{
    const size_t nameLen = strlen(pName);
    const size_t extLen  = strlen(pExtension);
    if ( nameLen <= extLen || pName[nameLen - extLen - 1] != '.' )
    {
        return false;
    }

    return strneqi(pName + nameLen - extLen, pExtension, extLen) != 0;
}

size_t J3DAPI stdUserDir_MigrateDirectory(const char* pLegacyDir, const char* pRelativeDir,
    const char* pExtension)
{
    if ( stdUserDir_aPath[0] == '\0' || !pLegacyDir || !pLegacyDir[0] || !pRelativeDir
        || !pExtension || !pExtension[0] )
    {
        return 0;
    }

    if ( *pExtension == '.' )
    {
        ++pExtension;
    }

    char aDestDir[STDUSERDIR_JOINMAX];
    if ( !stdUserDir_Join(aDestDir, sizeof(aDestDir), stdUserDir_aPath, pRelativeDir) )
    {
        return 0;
    }

    if ( !stdFileUtil_FileExists(aDestDir) )
    {
        stdUserDir_CreatePath(aDestDir);
    }

    // The destination already holds a file of this kind: the user has saves of their own, so the
    // legacy ones stay where they are.
    FindFileData* pFind = stdFileUtil_NewFind(aDestDir, 3, pExtension);
    if ( !pFind )
    {
        return 0;
    }

    tFoundFileInfo fileInfo;
    while ( stdFileUtil_FindNext(pFind, &fileInfo) )
    {
        if ( !fileInfo.bIsDirectory && stdUserDir_NameHasExtension(fileInfo.aName, pExtension) )
        {
            stdFileUtil_DisposeFind(pFind);
            return 0;
        }
    }

    stdFileUtil_DisposeFind(pFind);

    pFind = stdFileUtil_NewFind(pLegacyDir, 3, pExtension);
    if ( !pFind )
    {
        return 0;
    }

    size_t numCopied = 0;
    while ( stdFileUtil_FindNext(pFind, &fileInfo) )
    {
        if ( fileInfo.bIsDirectory || !stdUserDir_NameHasExtension(fileInfo.aName, pExtension) )
        {
            continue;
        }

        char aSource[STDUSERDIR_JOINMAX];
        char aDest[STDUSERDIR_JOINMAX];
        if ( !stdUserDir_Join(aSource, sizeof(aSource), pLegacyDir, fileInfo.aName)
            || !stdUserDir_Join(aDest, sizeof(aDest), aDestDir, fileInfo.aName) )
        {
            continue;
        }

        if ( stdUserDir_CopyFile(aSource, aDest) )
        {
            ++numCopied;
            STDLOG_STATUS("user dir: copied legacy file '%s' to '%s'\n", aSource, aDest);
        }
    }

    stdFileUtil_DisposeFind(pFind);
    return numCopied;
}
