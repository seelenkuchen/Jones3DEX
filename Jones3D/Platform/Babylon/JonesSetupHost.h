// The running host's values for the setup wizard's install engine. The wizard owns one of these
// structs for the whole window; every string and list the JonesInstallHost points into lives here.
#ifndef JONES3D_PLATFORM_BABYLON_JONESSETUPHOST_H
#define JONES3D_PLATFORM_BABYLON_JONESSETUPHOST_H
#ifndef __cplusplus
#error "JonesSetupHost.h is C++ only: it holds std::string members."
#endif

#include <Jones3D/Main/JonesInstall.h>

#include <string>

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

// Fills the host from the environment and the platform. GOG registry paths stay empty in this
// slice. The struct must not be copied after Init: installHost points into its own members.
void JonesSetupHost_Init(JonesSetupHost& host);

#endif // JONES3D_PLATFORM_BABYLON_JONESSETUPHOST_H
