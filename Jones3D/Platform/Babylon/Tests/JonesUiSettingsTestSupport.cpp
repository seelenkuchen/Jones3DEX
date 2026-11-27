// Added: definitions of the shared helpers, fixtures and stubs of the Babylon column's settings menu logic tests.
#include "JonesUiSettingsTestSupport.h"

namespace JonesUiSettingsTest
{
    int J3DAPI TestPrint(const char* pFormat, ...)
    {
        (void)pFormat;
        return 0;
    }

    tHostServices g_hostServices;

    void SetupHostServices()
    {
        std::memset(&g_hostServices, 0, sizeof(g_hostServices));
        g_hostServices.pStatusPrint  = TestPrint;
        g_hostServices.pMessagePrint = TestPrint;
        g_hostServices.pWarningPrint = TestPrint;
        g_hostServices.pErrorPrint   = TestPrint;
        g_hostServices.pDebugPrint   = TestPrint;
        g_hostServices.pMalloc       = malloc;
        g_hostServices.pFree         = free;
        g_hostServices.pRealloc      = realloc;
        g_hostServices.pFileOpen     = stdFileOpen;
        g_hostServices.pFileClose    = stdFileClose;
        g_hostServices.pFileRead     = stdFileRead;
        g_hostServices.pFileGets     = stdFileGets;
        g_hostServices.pFileWrite    = stdFileWrite;
        g_hostServices.pFileEOF      = stdFileEof;
        g_hostServices.pFileTell     = stdFileTell;
        g_hostServices.pFileSeek     = stdFileSeek;
        g_hostServices.pFileSize     = stdFileSize;
        g_hostServices.pFilePrintf   = stdFilePrintf;
        g_hostServices.pFileGetws    = stdFileGetws;

        std_g_pHS  = &g_hostServices;
        sith_g_pHS = &g_hostServices;
    }

    JonesDisplaySettings g_windowModeSettings;

    bool J3DAPI ConfigFallbackHasStarted(void)
    {
        return true;
    }

    int J3DAPI ConfigFallbackGetInt(const char* pKey, int defaultValue)
    {
        (void)pKey;
        return defaultValue;
    }

    float J3DAPI ConfigFallbackGetFloat(const char* pKey, float defaultValue)
    {
        (void)pKey;
        return defaultValue;
    }

    int J3DAPI ConfigFallbackGetStr(const char* pKey, char* pDstStr, size_t size,
        const char* pDefaultValue)
    {
        (void)pKey;
        if ( pDstStr && size )
        {
            std::snprintf(pDstStr, size, "%s", pDefaultValue ? pDefaultValue : "");
        }

        return 0;
    }

    void AddMode(std::vector<StdVideoMode>& modes, std::uint32_t width, std::uint32_t height,
        std::uint32_t bpp, std::uint32_t refreshRate)
    {
        StdVideoMode mode;
        std::memset(&mode, 0, sizeof(mode));
        mode.rasterInfo.width                = width;
        mode.rasterInfo.height               = height;
        mode.rasterInfo.colorInfo.bpp        = bpp;
        mode.rasterInfo.colorInfo.colorMode  = STDCOLOR_RGB;
        mode.refreshRate                     = refreshRate;
        modes.push_back(mode);
    }

    void BuildDisplayEnv(StdDisplayEnvironment& env, StdDisplayInfo& info,
        std::vector<StdVideoMode>& modes)
    {
        modes.clear();
        AddMode(modes, 512, 384, 16, 60);
        AddMode(modes, 640, 480, 16, 60);
        AddMode(modes, 640, 480, 32, 60);
        AddMode(modes, 800, 600, 32, 60);

        // One fake 3D device so the screen writes graphics.device too. Only its description is
        // read.
        static Device3D device;
        std::memset(&device, 0, sizeof(device));
        std::snprintf(device.deviceDescription, sizeof(device.deviceDescription), "%s",
            "TestDevice");

        std::memset(&info, 0, sizeof(info));
        std::snprintf(info.displayDevice.aDriverName, sizeof(info.displayDevice.aDriverName),
            "%s", "TestDisplay");
        info.displayDevice.bHAL = 1;
        info.aModes             = modes.data();
        info.numModes           = modes.size();
        info.numDevices         = 1;
        info.aDevices           = &device;

        std::memset(&env, 0, sizeof(env));
        env.numInfos      = 1;
        env.aDisplayInfos = &info;
    }

    JonesDisplaySettings DefaultDisplaySettings()
    {
        JonesDisplaySettings settings;
        std::memset(&settings, 0, sizeof(settings));
        settings.bWindowMode    = 0;
        settings.bDualMonitor   = 0;
        settings.displayDeviceNum = 0;
        settings.videoModeNum   = 1; // 640x480 16-bit
        settings.device3DNum    = 0;
        settings.filter         = STD3D_MIPMAPFILTER_TRILINEAR;
        settings.width          = 640;
        settings.height         = 480;
        settings.bBuffering     = 0;
        settings.bFog           = 1;
        settings.geoMode        = RD_GEOMETRY_FULL;
        settings.lightMode      = RD_LIGHTING_GOURAUD;
        settings.bClearBackBuffer = 0;
        settings.fogDensity     = 1.0f;
        return settings;
    }

    void SeedDisplayConfig()
    {
        REQUIRE(stdConfig_SetInt(JONESDISPLAY_CFG_GRAPHICS_PERFORMANCELEVEL, 4));
        REQUIRE(stdConfig_SetBool(JONESDISPLAY_CFG_ENGINE_RENDER_HIPOLY, true));
        REQUIRE(stdConfig_SetBool(JONESDISPLAY_CFG_ENGINE_RENDER_LITFACESENABLED, true));
        REQUIRE(stdConfig_SetInt(JONESDISPLAY_CFG_GRAPHICS_WIDTH, 640));
        REQUIRE(stdConfig_SetInt(JONESDISPLAY_CFG_GRAPHICS_HEIGHT, 480));
        REQUIRE(stdConfig_SetBool(JONESDISPLAY_CFG_GRAPHICS_WINDOW, false));
        REQUIRE(stdConfig_SetInt(JONESDISPLAY_CFG_GRAPHICS_MIPMAPFILTER,
            STD3D_MIPMAPFILTER_TRILINEAR));
    }
}

using namespace JonesUiSettingsTest;

// Added: the real getter lives in Jones3D/Main/JonesMain.c, which this executable does not link.
extern "C" JonesDisplaySettings* JonesMain_GetDisplaySettings(void)
{
    return &g_windowModeSettings;
}

// Added: the Options wrapper refreshes the display after applying; this executable does not link
// the game layer.
extern "C" void JonesMain_RefreshDisplayDevice(void)
{
}

// Added: the real getters live in Jones3D/Main/JonesMain.c, which this executable does not link.
extern "C" const char* JonesMain_GetDataRelease(void)
{
    return nullptr;
}

extern "C" const char* JonesMain_GetInstallPath(void)
{
    return "";
}
