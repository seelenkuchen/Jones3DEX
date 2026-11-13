#ifndef STD_STDCONFFILE_H
#define STD_STDCONFFILE_H
#include <j3dcore/j3d.h>

#include <std/types.h>

#include <stdarg.h>
#include <stdio.h>

J3D_EXTERN_C_START

#define STDCONFFILE_LINESIZE 4096u
#define STDCONFILE_MAXARGS   512u

typedef struct sStdConffileArg
{
    const char* argName;
    const char* argValue;
} StdConffileArg;
J3D_ASSERT_SIZE32(sizeof(StdConffileArg) == 8, "sizeof(StdConffileArg) == 8");

typedef struct sStdConffileEntry
{
    size_t numArgs;
    StdConffileArg aArgs[STDCONFILE_MAXARGS];
} StdConffileEntry;
J3D_ASSERT_SIZE32(sizeof(StdConffileEntry) == 4100, "sizeof(StdConffileEntry) == 4100");

extern StdConffileEntry stdConffile_g_entry;
extern char* stdConffile_g_aLine;

int J3DAPI stdConffile_Open(const char* pFilename);
int J3DAPI stdConffile_OpenWrite(const char* pFilename);
int J3DAPI stdConffile_OpenMode(const char* pFilename, const char* openMode);

void stdConffile_Close(void);
void stdConffile_CloseWrite(void);

int J3DAPI stdConffile_WriteLine(const char* pLine); // Added
int J3DAPI stdConffile_Write(const void* pData, size_t size);
int stdConffile_Printf(const char* pFormat, ...); // Added

int J3DAPI stdConffile_Read(void* pData, size_t size);
int J3DAPI stdConffile_ReadArgsFromStr(char* pStr);
int stdConffile_ReadArgs(void);
int stdConffile_ReadLine(void);
int stdConffile_ScanLine(const char* pFormat, ...); // Added

tFileHandle stdConffile_GetFileHandle(void);
tFileHandle stdConffile_GetWriteFileHandle(void); // Added
size_t stdConffile_GetLineNumber(void);

const char* stdConffile_GetFilename(void);
const char* stdConffile_GetWriteFilename(void); // Added

void stdConffile_ResetGlobals(void);

J3D_EXTERN_C_END
#endif // STD_STDCONFFILE_H
