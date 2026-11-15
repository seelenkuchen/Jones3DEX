// The running host's values for the setup wizard's install engine. The wizard owns one of these
// structs for the whole window; every string and list the JonesInstallHost points into lives here.
#ifndef JONES3D_PLATFORM_BABYLON_JONESSETUPHOST_H
#define JONES3D_PLATFORM_BABYLON_JONESSETUPHOST_H
#ifndef __cplusplus
#error "JonesSetupHost.h is C++ only: it holds std::string members."
#endif

#include <Jones3D/Main/JonesInstall.h>

#include <string>
#include <vector>

struct JonesSetupHost
{
    JonesInstallHost installHost = {};
    JonesInstallPathList volumeRoots = {};
    JonesInstallPathList gogRegistryPaths = {};

    std::string home;
    std::string xdgDataHome;
    std::string xdgDownloadDir;
    std::string programFiles;
    std::string programFilesX86;
    std::string appData;

    bool bWindows = false;
};

// Fills the host from the environment and the platform. GOG registry paths are read on Windows.
// The struct must not be copied after Init: installHost points into its own members.
void JonesSetupHost_Init(JonesSetupHost& host);

/**
 * @brief Fills pOut with pPaths in order, dropping paths that are empty, too long or duplicates.
 *
 * A path fits when it plus its terminating NUL is at most JONESINSTALL_MAX_PATH, and the list
 * holds at most JONESINSTALL_MAX_HOST_PATHS entries. Duplicates compare case-insensitively when
 * bCaseInsensitive is true (the Windows path style) and byte-exactly otherwise. pOut is fully
 * overwritten. Exposed for the pure part of the Windows registry read, which the tests cover.
 */
void JonesSetupHost_BuildPathList(JonesInstallPathList* pOut,
    const std::vector<std::string>& paths, bool bCaseInsensitive);

#endif // JONES3D_PLATFORM_BABYLON_JONESSETUPHOST_H
