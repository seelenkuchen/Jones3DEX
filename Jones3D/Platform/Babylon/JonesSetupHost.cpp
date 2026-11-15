// The running host's values for the setup wizard; see JonesSetupHost.h. Environment reads use
// _dupenv_s on Windows, as mainWindowed.cpp does, and std::filesystem with the error_code
// overloads for every path probe. The Windows registry read is the only direct Win32 call here.
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <Jones3D/Platform/Babylon/JonesSetupHost.h>

#include <Jones3D/Platform/Babylon/JonesUtf8Path.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace
{
    std::string ReadEnvironment(const char* pName)
    {
#ifdef _WIN32
        char* pValue = nullptr;
        size_t size = 0;
        if ( _dupenv_s(&pValue, &size, pName) != 0 || !pValue )
        {
            return {};
        }

        std::string value = pValue;
        std::free(pValue);
        return value;
#else
        const char* pValue = std::getenv(pName);
        return pValue ? pValue : "";
#endif
    }

    std::string HomeDirectory()
    {
        const std::string userProfile = ReadEnvironment("USERPROFILE");
        if ( !userProfile.empty() )
        {
            return userProfile;
        }

        const std::string drive = ReadEnvironment("HOMEDRIVE");
        const std::string path  = ReadEnvironment("HOMEPATH");
        if ( !drive.empty() && !path.empty() )
        {
            return drive + path;
        }

        return ReadEnvironment("HOME");
    }

    std::string JoinPath(const std::string& dir, const std::string& name)
    {
        if ( dir.empty() )
        {
            return name;
        }

        std::string path = dir;
        if ( path.back() != '/' && path.back() != '\\' )
        {
            path += '/';
        }

        return path + name;
    }

    bool Exists(const std::string& path)
    {
        std::error_code ec;
        return std::filesystem::exists(JonesUtf8Path_From(path), ec) && !ec;
    }

    bool IsDirectory(const std::string& path)
    {
        std::error_code ec;
        return std::filesystem::is_directory(JonesUtf8Path_From(path), ec) && !ec;
    }

    char FoldAsciiCase(char chr)
    {
        return chr >= 'A' && chr <= 'Z' ? static_cast<char>(chr - 'A' + 'a') : chr;
    }

    bool EqualsPathCaseInsensitive(const std::string& left, const std::string& right)
    {
        if ( left.size() != right.size() )
        {
            return false;
        }

        for ( size_t i = 0; i < left.size(); ++i )
        {
            if ( FoldAsciiCase(left[i]) != FoldAsciiCase(right[i]) )
            {
                return false;
            }
        }

        return true;
    }

    void AddRoot(JonesInstallPathList& roots, const std::string& path)
    {
        if ( path.empty() || roots.numPaths >= JONESINSTALL_MAX_HOST_PATHS
            || path.size() + 1 > JONESINSTALL_MAX_PATH || !IsDirectory(path) )
        {
            return;
        }

        for ( size_t i = 0; i < roots.numPaths; ++i )
        {
            if ( path == roots.aPaths[i] )
            {
                return;
            }
        }

        memcpy(roots.aPaths[roots.numPaths], path.c_str(), path.size() + 1);
        ++roots.numPaths;
    }

    void AddVolumeRoots(JonesInstallHost& installHost, JonesSetupHost& host)
    {
#ifdef _WIN32
        for ( char drive = 'A'; drive <= 'Z'; ++drive )
        {
            std::string root;
            root += drive;
            root += ":\\";
            AddRoot(host.volumeRoots, root);
        }
#else
        const std::string user = JonesUtf8Path_To(
            JonesUtf8Path_From(host.home).filename());
        if ( !user.empty() )
        {
            AddRoot(host.volumeRoots, "/media/" + user);
            AddRoot(host.volumeRoots, "/run/media/" + user);
        }

        AddRoot(host.volumeRoots, "/media");
        AddRoot(host.volumeRoots, "/mnt");
#endif
    }

#ifdef _WIN32
    // Reads the installed GOG game folders (M8 wizard, W4e). GOG writes one subkey per game
    // under the Games key and puts the install folder in the subkey's "path" value; both the
    // 32-bit and the 64-bit registry views are read, and duplicates fall out in the list helper.
    void AddGogRegistryPaths(JonesSetupHost& host)
    {
        const struct
        {
            const wchar_t* pSubKey;
            REGSAM samDesired;
        } locations[] =
        {
            { L"SOFTWARE\\WOW6432Node\\GOG.com\\Games", KEY_READ | KEY_WOW64_32KEY },
            { L"SOFTWARE\\GOG.com\\Games", KEY_READ },
        };

        std::vector<std::string> paths;
        for ( const auto& location : locations )
        {
            HKEY hGames = NULL;
            if ( RegOpenKeyExW(HKEY_LOCAL_MACHINE, location.pSubKey, 0, location.samDesired, &hGames)
                != ERROR_SUCCESS )
            {
                continue;
            }

            for ( DWORD index = 0; ; ++index )
            {
                wchar_t aGameName[256];
                DWORD nameLength = static_cast<DWORD>(sizeof(aGameName) / sizeof(aGameName[0]));
                const LONG enumResult = RegEnumKeyExW(hGames, index, aGameName, &nameLength, NULL,
                    NULL, NULL, NULL);
                if ( enumResult == ERROR_NO_MORE_ITEMS )
                {
                    break;
                }
                if ( enumResult != ERROR_SUCCESS )
                {
                    continue;
                }

                wchar_t aPath[JONESINSTALL_MAX_PATH];
                DWORD pathBytes = static_cast<DWORD>(sizeof(aPath));
                if ( RegGetValueW(hGames, aGameName, L"path", RRF_RT_REG_SZ, NULL, aPath,
                        &pathBytes) != ERROR_SUCCESS || pathBytes <= sizeof(wchar_t) )
                {
                    continue;
                }

                const int utf8Length = WideCharToMultiByte(CP_UTF8, 0, aPath, -1, NULL, 0, NULL,
                    NULL);
                if ( utf8Length <= 1 )
                {
                    continue;
                }

                std::string path(static_cast<size_t>(utf8Length - 1), '\0');
                if ( WideCharToMultiByte(CP_UTF8, 0, aPath, -1, path.data(), utf8Length, NULL,
                        NULL) != utf8Length )
                {
                    continue;
                }

                paths.push_back(path);
            }

            RegCloseKey(hGames);
        }

        JonesSetupHost_BuildPathList(&host.gogRegistryPaths, paths, true);
    }
#endif

    // Reports the free bytes of the volume holding the nearest existing parent of pPath.
    bool GetFreeBytes(const char* pPath, int64_t* pOutFreeBytes, void* pUserData)
    {
        (void)pUserData;

        if ( !pPath || !pOutFreeBytes || pPath[0] == '\0' )
        {
            return false;
        }

        std::filesystem::path path = JonesUtf8Path_From(pPath);
        std::error_code ec;
        while ( !path.empty() && !std::filesystem::exists(path, ec) )
        {
            const std::filesystem::path parent = path.parent_path();
            if ( parent == path )
            {
                break;
            }
            path = parent;
            ec.clear();
        }

        if ( path.empty() || !std::filesystem::exists(path, ec) || ec )
        {
            return false;
        }

        const std::filesystem::space_info space = std::filesystem::space(path, ec);
        if ( ec )
        {
            return false;
        }

        *pOutFreeBytes = static_cast<int64_t>(space.available);
        return true;
    }
}

void JonesSetupHost_BuildPathList(JonesInstallPathList* pOut,
    const std::vector<std::string>& paths, bool bCaseInsensitive)
{
    if ( !pOut )
    {
        return;
    }

    *pOut = JonesInstallPathList();
    for ( const std::string& path : paths )
    {
        if ( path.empty() || path.size() + 1 > JONESINSTALL_MAX_PATH
            || pOut->numPaths >= JONESINSTALL_MAX_HOST_PATHS )
        {
            continue;
        }

        bool bDuplicate = false;
        for ( size_t i = 0; i < pOut->numPaths; ++i )
        {
            if ( bCaseInsensitive ? EqualsPathCaseInsensitive(path, pOut->aPaths[i])
                                  : path == pOut->aPaths[i] )
            {
                bDuplicate = true;
                break;
            }
        }

        if ( bDuplicate )
        {
            continue;
        }

        memcpy(pOut->aPaths[pOut->numPaths], path.c_str(), path.size() + 1);
        ++pOut->numPaths;
    }
}

void JonesSetupHost_Init(JonesSetupHost& host)
{
    host = JonesSetupHost();

    host.bWindows = false;
#ifdef _WIN32
    host.bWindows = true;
#endif

    host.home = HomeDirectory();
    host.xdgDataHome = ReadEnvironment("XDG_DATA_HOME");
    host.xdgDownloadDir = ReadEnvironment("XDG_DOWNLOAD_DIR");
    if ( host.xdgDownloadDir.empty() && !host.home.empty() )
    {
        host.xdgDownloadDir = JoinPath(host.home, "Downloads");
    }

    host.programFiles = ReadEnvironment("ProgramFiles");
    host.programFilesX86 = ReadEnvironment("ProgramFiles(x86)");
    host.appData = ReadEnvironment("APPDATA");

    host.installHost = JonesInstallHost();
    host.installHost.platform = host.bWindows
        ? JONESINSTALL_PLATFORM_WINDOWS : JONESINSTALL_PLATFORM_LINUX;
    host.installHost.pfGetFreeBytes = GetFreeBytes;
    host.installHost.pFreeBytesUserData = nullptr;
    host.installHost.pHome = host.home.c_str();
    host.installHost.pXdgDataHome = host.xdgDataHome.c_str();
    host.installHost.pXdgDownloadDir = host.xdgDownloadDir.c_str();
    host.installHost.pProgramFiles = host.programFiles.c_str();
    host.installHost.pProgramFilesX86 = host.programFilesX86.c_str();
    host.installHost.pAppData = host.appData.c_str();

    AddVolumeRoots(host.installHost, host);
#ifdef _WIN32
    AddGogRegistryPaths(host);
#endif
    host.installHost.pVolumeRoots = &host.volumeRoots;
    host.installHost.pGogRegistryPaths = &host.gogRegistryPaths;
}
