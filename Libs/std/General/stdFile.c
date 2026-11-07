// Added: the stdFile* host file functions on the C runtime, split out of std.c so a platform column can
// replace them as a whole (the Babylon column does).
//
// Text mode: the original game ran on the Microsoft C runtime, where a read open without 'b' ("r",
// "rt") is a text-mode stream whose reads hand out "\r\n" as "\n" and end the data at the first
// Ctrl-Z (0x1A); a lone '\r' stays. Other C runtimes (glibc, the BSD libc of macOS) read every
// byte as it is, so on them the text-mode read handles translate here, the way the Babylon column's
// stdFileBabylon.cpp does, and the parsers see what they saw on retail Windows. On Windows the
// runtime already does it. Positions stay byte offsets in the file. Not reproduced, as no engine
// caller can observe them: the Microsoft ftell of a text read stream (off for LF-only files) and
// the "\n" to "\r\n" translation of text-mode writes, which stay byte-exact. stdFileGetws reads
// through the runtime's fgetws unchanged; no engine code calls it.

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "std.h"

// The file-private data behind a tFileHandle, created by stdFileOpen and freed by stdFileClose.
// 0 means "no file". It is allocated with the C runtime like the FILE it holds, not through
// stdMemory, so opening a file does not count as an engine allocation.
typedef struct sStdFileHandle
{
    FILE* pFile;  // the open stream, never NULL
    bool  bText;  // a read handle that translates text mode here
    bool  bCtrlZ; // a text read reached a Ctrl-Z: the data ends there until the next seek
} sStdFileHandle;

static sStdFileHandle* stdFile_AsHandle(tFileHandle fh)
{
    return (sStdFileHandle*)fh;
}

// The next byte a text-mode read hands out, or EOF at the end of the data or after a failed read:
// "\r\n" is one '\n', a lone '\r' stays, and a Ctrl-Z ends the data (the byte is pushed back, so
// the position does not move past it).
static int stdFile_GetTextChar(sStdFileHandle* pHandle)
{
    if ( pHandle->bCtrlZ )
    {
        return EOF;
    }

    int ch = getc(pHandle->pFile);
    if ( ch == 0x1A )
    {
        ungetc(ch, pHandle->pFile);
        pHandle->bCtrlZ = true;
        return EOF;
    }

    if ( ch != '\r' )
    {
        return ch;
    }

    int chNext = getc(pHandle->pFile);
    if ( chNext == '\n' )
    {
        return '\n';
    }

    if ( chNext == EOF )
    {
        // The Microsoft runtime hands out a '\r' before the end without reporting the end yet;
        // the next read does. A failed read keeps its error flag.
        if ( !ferror(pHandle->pFile) )
        {
            clearerr(pHandle->pFile);
        }
    }
    else
    {
        ungetc(chNext, pHandle->pFile);
    }

    return '\r';
}

tFileHandle J3DAPI stdFileOpen(const char* pFilename, const char* mode)
{
    FILE* fh;
    if ( fopen_s(&fh, pFilename, mode) != 0 )
    {
        return 0;
    }

    sStdFileHandle* pHandle = (sStdFileHandle*)malloc(sizeof(sStdFileHandle));
    if ( !pHandle )
    {
        fclose(fh);
        return 0;
    }

    pHandle->pFile  = fh;
#ifdef _WIN32
    pHandle->bText  = false; // the Microsoft runtime translates text mode itself
#else
    pHandle->bText  = mode[0] == 'r' && !strchr(mode, 'b');
#endif
    pHandle->bCtrlZ = false;
    return (tFileHandle)pHandle;
}

int J3DAPI stdFileClose(tFileHandle fh)
{
    sStdFileHandle* pHandle = stdFile_AsHandle(fh);
    if ( pHandle )
    {
        fclose(pHandle->pFile);
        free(pHandle);
    }

    return 0;
}

size_t J3DAPI stdFileRead(tFileHandle fh, void* pOutData, size_t size)
{
    sStdFileHandle* pHandle = stdFile_AsHandle(fh);
    if ( !pHandle->bText )
    {
        return fread(pOutData, 1u, size, pHandle->pFile);
    }

    unsigned char* pDst = (unsigned char*)pOutData;
    size_t numRead      = 0;
    while ( numRead < size )
    {
        int ch = stdFile_GetTextChar(pHandle);
        if ( ch == EOF )
        {
            break;
        }

        pDst[numRead++] = (unsigned char)ch;
    }

    return numRead;
}

size_t J3DAPI stdFileWrite(tFileHandle fh, const void* pData, size_t size)
{
    return fwrite(pData, 1u, size, stdFile_AsHandle(fh)->pFile);
}

int J3DAPI stdFileFlush(tFileHandle fh)
{
    return fflush(stdFile_AsHandle(fh)->pFile);
}

char* J3DAPI stdFileGets(tFileHandle fh, char* pStr, size_t size)
{
    sStdFileHandle* pHandle = stdFile_AsHandle(fh);
    if ( !pHandle->bText )
    {
        return fgets(pStr, (int)size, pHandle->pFile);
    }

    // fgets on the translated bytes: up to size - 1 characters, through the first '\n'.
    if ( size == 0 )
    {
        return NULL;
    }

    size_t curChar = 0;
    while ( curChar + 1 < size )
    {
        int ch = stdFile_GetTextChar(pHandle);
        if ( ch == EOF )
        {
            break;
        }

        pStr[curChar++] = (char)ch;
        if ( ch == '\n' )
        {
            break;
        }
    }

    if ( curChar == 0 && size > 1 )
    {
        return NULL;
    }

    pStr[curChar] = '\0';
    return pStr;
}

wchar_t* J3DAPI stdFileGetws(tFileHandle fh, wchar_t* pOutStr, size_t size)
{
    return fgetws(pOutStr, (int)size, stdFile_AsHandle(fh)->pFile);
}

int J3DAPI stdFileEof(tFileHandle fh)
{
    sStdFileHandle* pHandle = stdFile_AsHandle(fh);
    return pHandle->bCtrlZ || feof(pHandle->pFile);
}

int J3DAPI stdFileTell(tFileHandle fh)
{
    return ftell(stdFile_AsHandle(fh)->pFile);
}

size_t J3DAPI stdFileSize(const char* pFilePath)
{
    tFileHandle fh = stdFileOpen(pFilePath, "rb");
    if ( !fh )
    {
        return 0;
    }

    stdFileSeek(fh, 0, 2);
    int size = stdFileTell(fh);
    stdFileClose(fh);
    return size;
}

int J3DAPI stdFileSeek(tFileHandle fh, int offset, int origin)
{
    sStdFileHandle* pHandle = stdFile_AsHandle(fh);
    int result              = fseek(pHandle->pFile, offset, origin);
    if ( result == 0 )
    {
        pHandle->bCtrlZ = false;
    }

    return result;
}
