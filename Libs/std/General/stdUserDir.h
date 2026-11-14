#ifndef STD_STDUSERDIR_H
#define STD_STDUSERDIR_H
#include <j3dcore/j3d.h>
#include <std/types.h>

#include <stdbool.h>
#include <stddef.h>

J3D_EXTERN_C_START

// Added: the per-user writable directory (ADR-0019). Writes that used to land next to the game
// data (Jones.cfg, savegames, screenshots, key sets, logs) go here when it is set. The module is
// game-agnostic: it only stores the directory, joins paths and copies files into it.
//
// The longest directory kept, including its terminator. A caller with a smaller output buffer
// passes it to stdUserDir_MakePath, which refuses a path that does not fit instead of truncating.
#define STDUSERDIR_MAXPATH 512

// Stores pPath as the user directory. NULL and "" mean the working directory, which is also the
// starting value. A non-empty path is created, including missing parent directories; a failure to
// create it is left to the first file operation. Trailing '/' and '\' separators are dropped,
// except for a path that is only a root. Returns false and keeps the previous value when pPath is
// too long for STDUSERDIR_MAXPATH.
bool J3DAPI stdUserDir_Set(const char* pPath);

// The stored directory, never NULL. "" means the working directory.
const char* J3DAPI stdUserDir_GetPath(void);

// Joins the user directory and pRelative the way stdFnames_MakePath does and writes the result to
// pOut. With no user directory pRelative is copied unchanged. Returns false, with pOut unchanged,
// when pOut is NULL, outSize is 0, or the joined path does not fit; it never truncates.
bool J3DAPI stdUserDir_MakePath(char* pOut, size_t outSize, const char* pRelative);

// Copies pSourcePath into the user directory as pRelativeName when that destination does not exist
// yet. Never replaces, moves or deletes the source. Returns true when the copy happened; false
// when no user directory is set, the source is missing, the destination already exists, a path
// does not fit, or the copy failed.
bool J3DAPI stdUserDir_CopyFileIfMissing(const char* pSourcePath, const char* pRelativeName);

// Copies the files with extension pExtension (without the dot, case-insensitive) from pLegacyDir
// into <user directory>/pRelativeDir, but only when that destination directory holds no such
// file yet. Files that already exist are kept. The destination directory is created when missing.
// Returns the number of files copied. Does nothing and returns 0 when no user directory is set or
// a path does not fit.
size_t J3DAPI stdUserDir_MigrateDirectory(const char* pLegacyDir, const char* pRelativeDir,
    const char* pExtension);

// Restores the starting value, the working directory. For tests and for the global reset.
void stdUserDir_Reset(void);

J3D_EXTERN_C_END
#endif // STD_STDUSERDIR_H
