// Added: the Babylon column's engine-path resolver, adapter-internal. It turns an engine path
// (Windows style: backslashes, any case, relative to the working directory) into the absolute
// '/' path of the real file system entry, the way j3dcrt_ResolvePath does for the Null column.
// Unlike a lexical OS resolution, ".." is applied to the segment list before any directory is
// opened: the result differs from the operating system only across symbolic links to directories.
// The result's root is '/' for a '/' path and, on Windows, the drive ("C:/") for a drive path
// ("C:\..." or "C:/..."); off Windows a Wine "Z:\" or "z:/" path is the Unix root, so it resolves
// from '/'. Relative input takes the root of fs.currentPath() ("/" on POSIX hosts,
// "C:/" on Windows). ".." never climbs above the root. Relative input is prefixed with
// fs.currentPath(), whose segments are trusted real directory names: the resolver never probes
// them, and each ".." that pops into them untrusts one.
#ifndef STD_STDBABYLONPATH_H
#define STD_STDBABYLONPATH_H
#ifndef __cplusplus
#error "stdBabylonPath.h is C++ only: it names babylon::files types and std::string."
#endif

#include <babylon/files/FileSystem.h>

#include <string>

// Resolves pPath through fs into out. Returns false (out unchanged) for a null or empty path,
// or when no valid path results.
bool stdBabylonPath_Resolve(babylon::files::FileSystem& fs, const char* pPath, std::string& out);

#endif // STD_STDBABYLONPATH_H
