// UTF-8 conversions for std::filesystem paths. Every path string the wizard handles is UTF-8
// (file pickers and SDL paths), while Windows builds std::filesystem::path from the ANSI code
// page; these helpers go through UTF-8/char8_t on Windows and are plain on other hosts.
#ifndef JONES3D_PLATFORM_BABYLON_JONESUTF8PATH_H
#define JONES3D_PLATFORM_BABYLON_JONESUTF8PATH_H
#ifndef __cplusplus
#error "JonesUtf8Path.h is C++ only: it names std::filesystem and std::u8string."
#endif

#include <filesystem>
#include <string>

inline std::filesystem::path JonesUtf8Path_From(const std::string& text)
{
#ifdef _WIN32
    const std::u8string utf8(reinterpret_cast<const char8_t*>(text.data()), text.size());
    return std::filesystem::path(utf8);
#else
    return std::filesystem::path(text);
#endif
}

inline std::string JonesUtf8Path_To(const std::filesystem::path& path)
{
#ifdef _WIN32
    const std::u8string utf8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
#else
    return path.string();
#endif
}

#endif // JONES3D_PLATFORM_BABYLON_JONESUTF8PATH_H
