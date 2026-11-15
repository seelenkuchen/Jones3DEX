// The running host's values for the setup wizard; see JonesSetupHost.h. Environment reads use
// _dupenv_s on Windows, as mainWindowed.cpp does, and std::filesystem with the error_code
// overloads for every path probe, so no POSIX or Windows API call is needed here.
#include <Jones3D/Platform/Babylon/JonesSetupHost.h>

#include <Jones3D/Platform/Babylon/JonesUtf8Path.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

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

    std::string AppImageDir()
    {
        const std::string image = ReadEnvironment("APPIMAGE");
        if ( image.empty() )
        {
            return {};
        }

        const std::filesystem::path parent = JonesUtf8Path_From(image).parent_path();
        return parent.empty() ? std::string() : JonesUtf8Path_To(parent);
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

std::string JonesSetupHost_AppImageDir(void)
{
    return AppImageDir();
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
    host.installHost.pVolumeRoots = &host.volumeRoots;
    host.installHost.pGogRegistryPaths = &host.gogRegistryPaths;
}
