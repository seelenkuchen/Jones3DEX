// Added: the logic tests of the Babylon column's Display and Sound screens (ADR-0026). They use a
// platform that delivers one scripted event per frame and a Null painter, so the screens run
// exactly as in the game loop without a window or a GPU. The resolution list is built from a
// synthetic display environment and the output-device list from a Null audio device with several
// outputs. The drawing sample lives in JonesUiPainterTest.cpp.
#include <doctest.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <babylon/audio/NullDevice.h>
#include <babylon/cvars/Registry.h>
#include <babylon/files/FileSystem.h>
#include <babylon/files/SdlFileSystem.h>
#include <babylon/platform/Event.h>
#include <babylon/platform/Keys.h>
#include <babylon/platform/Platform.h>
#include <babylon/platform/Window.h>
#include <babylon/time/NullClock.h>
#include <babylon/ui/NullPainter.h>
#include <babylon/ui/Ui.h>

#include <Jones3D/Display/JonesDisplay.h>
#include <Jones3D/Display/JonesReticle.h>
#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Platform/Babylon/JonesUiDisplaySettings.h>
#include <Jones3D/Platform/Babylon/JonesUiGameplaySettings.h>
#include <Jones3D/Platform/Babylon/JonesUiSoundSettings.h>

#include <sound/Driver.h>
#include <sound/DriverOutput.h>

#include <sith/Devices/sithControl.h>
#include <sith/Engine/sithPuppet.h>
#include <sith/Engine/sithRender.h>
#include <sith/Gameplay/sithOverlayMap.h>
#include <sith/Main/sithMain.h>
#include <sith/World/sithMaterial.h>
#include <sith/World/sithModel.h>
#include <sith/World/sithVoice.h>
#include <sith/World/sithWeapon.h>

#include <std/Babylon/stdBabylon.h>
#include <std/General/std.h>
#include <std/General/stdConfig.h>
#include <std/General/stdJSON.h>
#include <std/General/stdPlatform.h>
#include <std/types.h>

namespace
{
    using namespace babylon;

    constexpr const char* kConfigPath = "ui-settings-test.json";

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

    // A fresh Jones.cfg for one case.
    class ConfigTest
    {
    public:
        ConfigTest()
        {
            const StdConfigFallback fallback = {
                ConfigFallbackHasStarted, ConfigFallbackGetInt, ConfigFallbackGetFloat,
                ConfigFallbackGetStr,
            };
            stdConfig_SetFallback(&fallback);

            std::remove(kConfigPath);
            REQUIRE(stdJSON_Startup());
            REQUIRE(stdConfig_Startup(kConfigPath));
        }

        ~ConfigTest()
        {
            if ( stdConfig_HasStarted() )
            {
                stdConfig_Shutdown();
            }
            if ( stdJSON_HasStarted() )
            {
                stdJSON_Shutdown();
            }

            std::remove(kConfigPath);
        }

        ConfigTest(const ConfigTest&) = delete;
        ConfigTest& operator=(const ConfigTest&) = delete;
    };

    // Delivers one event per frame, as the save/load tests do.
    class ScriptedPlatform final : public platform::Platform
    {
    public:
        std::unique_ptr<platform::Window> createWindow(const platform::WindowDesc&) override
        {
            return nullptr;
        }

        std::optional<platform::Event> pollEvent() override
        {
            if ( m_delivering )
            {
                m_delivering = false;
                return std::nullopt;
            }

            if ( m_events.empty() )
            {
                if ( ++m_emptyPolls > 200 )
                {
                    m_emptyPolls = 0;
                    return platform::Event{ platform::QuitRequested{} };
                }

                return std::nullopt;
            }

            m_emptyPolls = 0;
            m_delivering = true;
            platform::Event event = m_events.front();
            m_events.pop_front();
            return event;
        }

        bool showMessageBox(std::string_view, std::string_view) override
        {
            return false;
        }

        void PushKey(platform::Key key)
        {
            m_events.push_back(platform::Event{
                platform::KeyChanged{ 1, key, true, false } });
            m_events.push_back(platform::Event{
                platform::KeyChanged{ 1, key, false, false } });
        }

    private:
        std::deque<platform::Event> m_events;
        bool m_delivering = false;
        int m_emptyPolls  = 0;
    };

    struct SettingsTestContext
    {
        ScriptedPlatform platform;
        time::NullClock clock;
        time::NullClock virtualClock;
        std::unique_ptr<files::FileSystem> pFileSystem;
        std::unique_ptr<audio::NullDevice> pAudioDevice;
        cvars::Registry cvars;
        StdBabylonContext context{};

        SettingsTestContext()
        {
            pFileSystem  = files::createSdlFileSystem();
            pAudioDevice = std::make_unique<audio::NullDevice>();

            context.pClock          = &clock;
            context.pVirtualClock   = &virtualClock;
            context.pFileSystem     = pFileSystem.get();
            context.pPlatform       = &platform;
            context.pScriptPlatform = nullptr;
            context.pWindow         = nullptr;
            context.pCvars          = &cvars;
            context.pRenderDevice   = nullptr;
            context.pAudioDevice    = pAudioDevice.get();

            stdBabylon_SetContext(&context);
            SetupHostServices();
        }

        ~SettingsTestContext()
        {
            stdBabylon_SetContext(nullptr);
        }
    };

    // Opens the sound driver on the context's Null device for one case.
    class OpenSoundDriver
    {
    public:
        OpenSoundDriver()
        {
            SoundDriver_SetConfiguredOutputDevice(nullptr);
            REQUIRE(SoundDriver_Open(0, 0, nullptr, nullptr, nullptr, nullptr, TestPrint) == 1);
        }

        ~OpenSoundDriver()
        {
            SoundDriver_Close();
        }

        OpenSoundDriver(const OpenSoundDriver&) = delete;
        OpenSoundDriver& operator=(const OpenSoundDriver&) = delete;
    };

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

TEST_CASE("the display resolution list is deduplicated by size")
{
    StdDisplayEnvironment env;
    StdDisplayInfo info;
    std::vector<StdVideoMode> aModes;
    BuildDisplayEnv(env, info, aModes);

    std::vector<JonesUiDisplayVideoMode> modes;
    REQUIRE(JonesUiDisplay_CollectVideoModes(&env, 0, modes));
    REQUIRE(modes.size() == 3u);

    CHECK(modes[0].width == 512u);
    CHECK(modes[0].height == 384u);
    CHECK(modes[0].text == "512x384 (60 Hz)");
    CHECK(modes[0].aBitDepths == std::vector<int>{ 16 });
    CHECK(modes[0].aModeNums == std::vector<std::size_t>{ 0 });

    CHECK(modes[1].width == 640u);
    CHECK(modes[1].height == 480u);
    CHECK(modes[1].text == "640x480 (60 Hz)");
    CHECK(modes[1].aBitDepths == std::vector<int>({ 16, 32 }));
    CHECK(modes[1].aModeNums == std::vector<std::size_t>({ 1, 2 }));

    CHECK(modes[2].width == 800u);
    CHECK(modes[2].height == 600u);
    CHECK(modes[2].aBitDepths == std::vector<int>{ 32 });
    CHECK(modes[2].aModeNums == std::vector<std::size_t>{ 3 });

    // A missing environment or device has no list.
    CHECK_FALSE(JonesUiDisplay_CollectVideoModes(nullptr, 0, modes));
    CHECK_FALSE(JonesUiDisplay_CollectVideoModes(&env, 1, modes));
}

TEST_CASE("the display screen OK writes the chosen settings and config")
{
    SettingsTestContext testContext;
    ConfigTest config;
    SeedDisplayConfig();

    StdDisplayEnvironment env;
    StdDisplayInfo info;
    std::vector<StdVideoMode> aModes;
    BuildDisplayEnv(env, info, aModes);

    JonesDisplaySettings settings = DefaultDisplaySettings();
    sithModel_EnableHiPoly(1);
    sithRender_g_fogDensity = 100.0f;

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    // 0 bit depth, 1 resolution, 2 windowed, 3 filter, 4 fog, 5 fog density, 6 performance,
    // 7 hi-poly, 8 lit faces, 9 OK, 10 back.
    testContext.platform.PushKey(platform::Key::Right); // 16 -> 32 bit
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Right); // 640x480 -> 800x600
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter); // windowed on
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Left); // trilinear -> bilinear
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter); // fog off
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Left); // density 1.00 -> 0.99
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Left); // very fast -> fast
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter); // hi-poly off
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter); // lit faces off
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter); // OK

    REQUIRE(JonesUiDisplay_RunDisplayScreen(ui, &painter, &env, &settings) == 1);

    CHECK(settings.videoModeNum == 3u);
    CHECK(settings.width == 800u);
    CHECK(settings.height == 600u);
    CHECK(settings.bWindowMode == 1);
    CHECK(settings.filter == STD3D_MIPMAPFILTER_BILINEAR);
    CHECK(settings.bFog == 0);
    CHECK(settings.fogDensity == doctest::Approx(0.99f));
    CHECK(settings.bBuffering == 0);

    CHECK(stdConfig_GetInt(JONESDISPLAY_CFG_GRAPHICS_WIDTH, 0) == 800);
    CHECK(stdConfig_GetInt(JONESDISPLAY_CFG_GRAPHICS_HEIGHT, 0) == 600);
    CHECK(stdConfig_GetInt(JONESDISPLAY_CFG_GRAPHICS_BPP, 0) == 32);
    CHECK(stdConfig_GetInt(JONESDISPLAY_CFG_GRAPHICS_REFRESHRATE, 0) == 60);
    CHECK(stdConfig_GetBool(JONESDISPLAY_CFG_GRAPHICS_WINDOW, false));
    CHECK(stdConfig_GetInt(JONESDISPLAY_CFG_GRAPHICS_MIPMAPFILTER, -1)
        == STD3D_MIPMAPFILTER_BILINEAR);
    CHECK_FALSE(stdConfig_GetBool(JONESDISPLAY_CFG_ENGINE_RENDER_FOGENABLED, true));
    CHECK(stdConfig_GetFloat(JONESDISPLAY_CFG_ENGINE_RENDER_FOGDENSITY, 0.0f)
        == doctest::Approx(0.99f));
    CHECK(stdConfig_GetInt(JONESDISPLAY_CFG_GRAPHICS_PERFORMANCELEVEL, -1) == 2);
    CHECK_FALSE(stdConfig_GetBool(JONESDISPLAY_CFG_ENGINE_RENDER_HIPOLY, true));
    CHECK_FALSE(stdConfig_GetBool(JONESDISPLAY_CFG_ENGINE_RENDER_LITFACESENABLED, true));
    char aDisplay[128] = { 0 };
    CHECK(stdConfig_GetString(JONESDISPLAY_CFG_GRAPHICS_DISPLAY, aDisplay,
        sizeof(aDisplay), ""));
    CHECK(std::string(aDisplay) == "TestDisplay");
    char aDevice[128] = { 0 };
    CHECK(stdConfig_GetString(JONESDISPLAY_CFG_GRAPHICS_DEVICE, aDevice, sizeof(aDevice), ""));
    CHECK(std::string(aDevice) == "TestDevice");
    CHECK_FALSE(stdConfig_GetBool(JONESDISPLAY_CFG_GRAPHICS_BUFFERING, true));
    CHECK_FALSE(sithModel_IsHiPolyEnabled());
    CHECK(sithRender_g_fogDensity == doctest::Approx(99.0f));
}

TEST_CASE("the display screen back writes nothing")
{
    SettingsTestContext testContext;
    ConfigTest config;
    SeedDisplayConfig();

    StdDisplayEnvironment env;
    StdDisplayInfo info;
    std::vector<StdVideoMode> aModes;
    BuildDisplayEnv(env, info, aModes);

    const JonesDisplaySettings before = DefaultDisplaySettings();
    JonesDisplaySettings settings = before;
    sithModel_EnableHiPoly(1);
    sithRender_g_fogDensity = 100.0f;

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    testContext.platform.PushKey(platform::Key::Right);
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Escape);

    REQUIRE(JonesUiDisplay_RunDisplayScreen(ui, &painter, &env, &settings) == 2);

    CHECK(settings.videoModeNum == before.videoModeNum);
    CHECK(settings.width == before.width);
    CHECK(settings.height == before.height);
    CHECK(settings.bWindowMode == before.bWindowMode);
    CHECK(settings.filter == before.filter);
    CHECK(settings.bFog == before.bFog);
    CHECK(settings.fogDensity == doctest::Approx(before.fogDensity));

    CHECK(stdConfig_GetInt(JONESDISPLAY_CFG_GRAPHICS_WIDTH, 0) == 640);
    CHECK(stdConfig_GetInt(JONESDISPLAY_CFG_GRAPHICS_HEIGHT, 0) == 480);
    CHECK_FALSE(stdConfig_GetBool(JONESDISPLAY_CFG_GRAPHICS_WINDOW, true));
    CHECK(stdConfig_GetInt(JONESDISPLAY_CFG_GRAPHICS_PERFORMANCELEVEL, -1) == 4);
    CHECK(stdConfig_GetBool(JONESDISPLAY_CFG_ENGINE_RENDER_HIPOLY, false));
    CHECK(sithModel_IsHiPolyEnabled());
    CHECK(sithRender_g_fogDensity == doctest::Approx(100.0f));
}

TEST_CASE("the sound device list starts with the system default")
{
    SettingsTestContext testContext;
    OpenSoundDriver driver;

    testContext.pAudioDevice->setOutputDevices({
        audio::OutputDevice{ "device-a", "Device A" },
        audio::OutputDevice{ "device-b", "Device B" },
    });

    std::vector<JonesUiSoundOutputDevice> devices;
    JonesUiSound_CollectOutputDevices(devices);
    REQUIRE(devices.size() == 3u);
    CHECK(devices[0].id.empty());
    CHECK(devices[0].name == "Default");
    CHECK(devices[1].id == "device-a");
    CHECK(devices[1].name == "Device A");
    CHECK(devices[2].id == "device-b");
    CHECK(devices[2].name == "Device B");
}

TEST_CASE("the sound screen OK writes volume, reverse and the output device")
{
    SettingsTestContext testContext;
    ConfigTest config;
    OpenSoundDriver driver;

    testContext.pAudioDevice->setOutputDevices({
        audio::OutputDevice{ "device-a", "Device A" },
        audio::OutputDevice{ "device-b", "Device B" },
    });
    REQUIRE(SoundDriver_SetOutputDevice(nullptr) == 1);

    REQUIRE(stdConfig_SetFloat(JONESCONFIG_CFG_SOUND_VOLUME, 0.3f));
    REQUIRE(stdConfig_SetBool(JONESCONFIG_CFG_SOUND_REVERSE, false));

    JonesSoundSettings settings;
    std::memset(&settings, 0, sizeof(settings));
    settings.maxSoundVolume = 0.5f;
    settings.b3DHWSupport   = 0;
    settings.bReverseSound  = 0;

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    // 0 volume, 1 reverse, 2 output device, 3 OK, 4 back.
    testContext.platform.PushKey(platform::Key::Right); // 0.50 -> 0.51
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter); // reverse on
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Right); // default -> device-a
    testContext.platform.PushKey(platform::Key::Right); // device-a -> device-b
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter); // OK

    REQUIRE(JonesUiSound_RunSoundScreen(ui, &painter, &settings) == 1);

    CHECK(settings.maxSoundVolume == doctest::Approx(0.51f));
    CHECK(settings.bReverseSound == 1);
    CHECK(settings.b3DHWSupport == 0);
    CHECK(stdConfig_GetFloat(JONESCONFIG_CFG_SOUND_VOLUME, 0.0f) == doctest::Approx(0.51f));
    CHECK(stdConfig_GetBool(JONESCONFIG_CFG_SOUND_REVERSE, false));
    CHECK_FALSE(stdConfig_GetBool(JONESCONFIG_CFG_SOUND_HW, true));
    CHECK(std::string(SoundDriver_GetCurrentOutputDevice()) == "device-b");
    char aOutput[SOUNDDRIVER_OUTPUTDEVICE_ID_MAX + 1] = { 0 };
    CHECK(stdConfig_GetString(JONESCONFIG_CFG_ENGINE_AUDIO_OUTPUTDEVICE, aOutput,
        sizeof(aOutput), ""));
    CHECK(std::string(aOutput) == "device-b");
}

TEST_CASE("the sound screen back writes nothing")
{
    SettingsTestContext testContext;
    ConfigTest config;
    OpenSoundDriver driver;

    testContext.pAudioDevice->setOutputDevices({
        audio::OutputDevice{ "device-a", "Device A" },
        audio::OutputDevice{ "device-b", "Device B" },
    });
    REQUIRE(SoundDriver_SetOutputDevice("device-a") == 1);

    REQUIRE(stdConfig_SetFloat(JONESCONFIG_CFG_SOUND_VOLUME, 0.3f));
    REQUIRE(stdConfig_SetBool(JONESCONFIG_CFG_SOUND_REVERSE, false));
    REQUIRE(stdConfig_SetString(JONESCONFIG_CFG_ENGINE_AUDIO_OUTPUTDEVICE, "device-a"));

    const JonesSoundSettings before = {
        0.5f, 0, 0,
    };
    JonesSoundSettings settings = before;

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    testContext.platform.PushKey(platform::Key::Right);
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Escape);

    REQUIRE(JonesUiSound_RunSoundScreen(ui, &painter, &settings) == 2);

    CHECK(settings.maxSoundVolume == before.maxSoundVolume);
    CHECK(settings.b3DHWSupport == before.b3DHWSupport);
    CHECK(settings.bReverseSound == before.bReverseSound);
    CHECK(stdConfig_GetFloat(JONESCONFIG_CFG_SOUND_VOLUME, 0.0f) == doctest::Approx(0.3f));
    CHECK_FALSE(stdConfig_GetBool(JONESCONFIG_CFG_SOUND_REVERSE, true));
    CHECK(std::string(SoundDriver_GetCurrentOutputDevice()) == "device-a");
    char aOutput[SOUNDDRIVER_OUTPUTDEVICE_ID_MAX + 1] = { 0 };
    CHECK(stdConfig_GetString(JONESCONFIG_CFG_ENGINE_AUDIO_OUTPUTDEVICE, aOutput,
        sizeof(aOutput), ""));
    CHECK(std::string(aOutput) == "device-a");
}

TEST_CASE("the gameplay screen OK writes the original options and the enhancements")
{
    SettingsTestContext testContext;
    ConfigTest config;

    sithVoice_ShowText(0);
    sithOverlayMap_SetShowHints(0);
    sithOverlayMap_EnableMapRotation(0);
    sithSetGameDifficulty(0);
    sithControl_g_controlOptions = 0x04;
    JonesReticle_Enable(false);
    sithWeapon_EnableBloodSplatter(false);

    REQUIRE(stdConfig_SetBool(SITHVOICE_CFG_GAMEPLAY_SHOWTEXT, false));
    REQUIRE(stdConfig_SetBool(SITHOVERLAYMAP_CFG_GAMEPLAY_SHOWHINTS, false));
    REQUIRE(stdConfig_SetBool(SITHOVERLAYMAP_CFG_GAMEPLAY_MAPROTATION, false));
    REQUIRE(stdConfig_SetInt(SITH_CFG_GAMEPLAY_DIFFICULTY, 0));
    REQUIRE(stdConfig_SetBool(JONESCONFIG_CFG_GAMEPLAY_DEFAULTRUN, false));
    REQUIRE(stdConfig_SetBool(SITHWEAPON_CFG_ENGINERENDERER_VFX_WEAPONFIREFLASH_ENABLED, true));
    REQUIRE(stdConfig_SetBool(SITHMATERIAL_CFG_CNDWORLDMATERIALS_LOADEXTERNAL, true));
    REQUIRE(stdConfig_SetBool(SITHPUPPET_CFG_CNDWORLDKEYFRAMES_LOADEXTERNAL, true));

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    // 0 show text, 1 show hints, 2 rotate map, 3 difficulty, 4 default to run, 5 reticle,
    // 6 blood, 7 profile (disabled), 8 profile note, 9 enhancements heading, 10 fire flash,
    // 11 external materials, 12 external keyframes, 13 OK, 14 back.
    testContext.platform.PushKey(platform::Key::Enter); // show text on
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter); // show hints on
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter); // rotate map on
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Right); // difficulty 0 -> 1
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter); // default to run on
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter); // reticle on
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter); // blood on
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter); // fire flash off
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter); // external materials off
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter); // external keyframes off
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter); // OK

    REQUIRE(JonesUiGameplay_RunGameplayScreen(ui, &painter, true) == 1);

    CHECK(sithVoice_GetShowText() == 1);
    CHECK(stdConfig_GetBool(SITHVOICE_CFG_GAMEPLAY_SHOWTEXT, false));
    CHECK(sithOverlayMap_GetShowHints() == 1);
    CHECK(stdConfig_GetBool(SITHOVERLAYMAP_CFG_GAMEPLAY_SHOWHINTS, false));
    CHECK(sithOverlayMap_GetMapRotation() == 1);
    CHECK(stdConfig_GetBool(SITHOVERLAYMAP_CFG_GAMEPLAY_MAPROTATION, false));
    CHECK(sithGetGameDifficulty() == 1);
    CHECK(stdConfig_GetInt(SITH_CFG_GAMEPLAY_DIFFICULTY, -1) == 1);
    CHECK((sithControl_g_controlOptions & 0x02) != 0);
    CHECK(stdConfig_GetBool(JONESCONFIG_CFG_GAMEPLAY_DEFAULTRUN, false));
    CHECK(JonesReticle_IsEnabled());
    CHECK(sithWeapon_IsBloodSplatterEnabled());
    CHECK_FALSE(stdConfig_GetBool(SITHWEAPON_CFG_ENGINERENDERER_VFX_WEAPONFIREFLASH_ENABLED, true));
    CHECK_FALSE(stdConfig_GetBool(SITHMATERIAL_CFG_CNDWORLDMATERIALS_LOADEXTERNAL, true));
    CHECK_FALSE(stdConfig_GetBool(SITHPUPPET_CFG_CNDWORLDKEYFRAMES_LOADEXTERNAL, true));
    // The profile is fixed by the build and written nowhere.
    CHECK_FALSE(stdConfig_Contains("game.profile"));
}

TEST_CASE("the gameplay screen back writes nothing")
{
    SettingsTestContext testContext;
    ConfigTest config;

    sithVoice_ShowText(0);
    sithOverlayMap_SetShowHints(0);
    sithOverlayMap_EnableMapRotation(0);
    sithSetGameDifficulty(0);
    sithControl_g_controlOptions = 0x04;
    JonesReticle_Enable(false);
    sithWeapon_EnableBloodSplatter(false);

    REQUIRE(stdConfig_SetBool(SITHVOICE_CFG_GAMEPLAY_SHOWTEXT, false));
    REQUIRE(stdConfig_SetBool(SITHOVERLAYMAP_CFG_GAMEPLAY_SHOWHINTS, false));
    REQUIRE(stdConfig_SetBool(SITHOVERLAYMAP_CFG_GAMEPLAY_MAPROTATION, false));
    REQUIRE(stdConfig_SetInt(SITH_CFG_GAMEPLAY_DIFFICULTY, 0));
    REQUIRE(stdConfig_SetBool(JONESCONFIG_CFG_GAMEPLAY_DEFAULTRUN, false));
    REQUIRE(stdConfig_SetBool(SITHWEAPON_CFG_ENGINERENDERER_VFX_WEAPONFIREFLASH_ENABLED, true));
    REQUIRE(stdConfig_SetBool(SITHMATERIAL_CFG_CNDWORLDMATERIALS_LOADEXTERNAL, true));
    REQUIRE(stdConfig_SetBool(SITHPUPPET_CFG_CNDWORLDKEYFRAMES_LOADEXTERNAL, true));

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    testContext.platform.PushKey(platform::Key::Enter); // show text on in the screen only
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Right); // difficulty 0 -> 1 in the screen only
    testContext.platform.PushKey(platform::Key::Escape);

    REQUIRE(JonesUiGameplay_RunGameplayScreen(ui, &painter, true) == 2);

    CHECK(sithVoice_GetShowText() == 0);
    CHECK_FALSE(stdConfig_GetBool(SITHVOICE_CFG_GAMEPLAY_SHOWTEXT, true));
    CHECK(sithOverlayMap_GetShowHints() == 0);
    CHECK_FALSE(stdConfig_GetBool(SITHOVERLAYMAP_CFG_GAMEPLAY_SHOWHINTS, true));
    CHECK(sithOverlayMap_GetMapRotation() == 0);
    CHECK_FALSE(stdConfig_GetBool(SITHOVERLAYMAP_CFG_GAMEPLAY_MAPROTATION, true));
    CHECK(sithGetGameDifficulty() == 0);
    CHECK(stdConfig_GetInt(SITH_CFG_GAMEPLAY_DIFFICULTY, -1) == 0);
    CHECK((sithControl_g_controlOptions & 0x02) == 0);
    CHECK_FALSE(stdConfig_GetBool(JONESCONFIG_CFG_GAMEPLAY_DEFAULTRUN, true));
    CHECK_FALSE(JonesReticle_IsEnabled());
    CHECK_FALSE(sithWeapon_IsBloodSplatterEnabled());
    CHECK(stdConfig_GetBool(SITHWEAPON_CFG_ENGINERENDERER_VFX_WEAPONFIREFLASH_ENABLED, false));
    CHECK(stdConfig_GetBool(SITHMATERIAL_CFG_CNDWORLDMATERIALS_LOADEXTERNAL, false));
    CHECK(stdConfig_GetBool(SITHPUPPET_CFG_CNDWORLDKEYFRAMES_LOADEXTERNAL, false));
}

TEST_CASE("the retail profile disables the enhancements and writes them off")
{
    SettingsTestContext testContext;
    ConfigTest config;

    sithVoice_ShowText(0);
    sithOverlayMap_SetShowHints(0);
    sithOverlayMap_EnableMapRotation(0);
    sithSetGameDifficulty(0);
    sithControl_g_controlOptions = 0x04;

    REQUIRE(stdConfig_SetBool(SITHWEAPON_CFG_ENGINERENDERER_VFX_WEAPONFIREFLASH_ENABLED, true));
    REQUIRE(stdConfig_SetBool(SITHMATERIAL_CFG_CNDWORLDMATERIALS_LOADEXTERNAL, true));
    REQUIRE(stdConfig_SetBool(SITHPUPPET_CFG_CNDWORLDKEYFRAMES_LOADEXTERNAL, true));

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    // Up from the first row wraps to back, then OK. The disabled enhancement rows are skipped.
    testContext.platform.PushKey(platform::Key::Up);
    testContext.platform.PushKey(platform::Key::Up);
    testContext.platform.PushKey(platform::Key::Enter);

    REQUIRE(JonesUiGameplay_RunGameplayScreen(ui, &painter, false) == 1);

    CHECK_FALSE(stdConfig_GetBool(SITHWEAPON_CFG_ENGINERENDERER_VFX_WEAPONFIREFLASH_ENABLED, true));
    CHECK_FALSE(stdConfig_GetBool(SITHMATERIAL_CFG_CNDWORLDMATERIALS_LOADEXTERNAL, true));
    CHECK_FALSE(stdConfig_GetBool(SITHPUPPET_CFG_CNDWORLDKEYFRAMES_LOADEXTERNAL, true));
    // The original options still work under Retail.
    CHECK(stdConfig_Contains(SITHVOICE_CFG_GAMEPLAY_SHOWTEXT));
    // The profile row is fixed by the build and writes no key in either direction.
    CHECK_FALSE(stdConfig_Contains("game.profile"));
}

TEST_CASE("the headless settings wrappers answer the Null stub values")
{
    SettingsTestContext testContext;

    StdDisplayEnvironment env;
    StdDisplayInfo info;
    std::vector<StdVideoMode> aModes;
    BuildDisplayEnv(env, info, aModes);

    const JonesDisplaySettings displayBefore = DefaultDisplaySettings();
    JonesDisplaySettings displaySettings = displayBefore;
    CHECK(jonesConfig_ShowDisplaySettingsDialog(nullptr, &env, &displaySettings) == 2);
    CHECK(displaySettings.videoModeNum == displayBefore.videoModeNum);
    CHECK(displaySettings.width == displayBefore.width);
    CHECK(displaySettings.height == displayBefore.height);

    const JonesSoundSettings soundBefore = { 0.5f, 1, 1 };
    JonesSoundSettings soundSettings = soundBefore;
    CHECK(jonesConfig_ShowSoundSettingsDialog(nullptr, &soundSettings) == 2);
    CHECK(soundSettings.maxSoundVolume == soundBefore.maxSoundVolume);
    CHECK(soundSettings.b3DHWSupport == soundBefore.b3DHWSupport);
    CHECK(soundSettings.bReverseSound == soundBefore.bReverseSound);

    CHECK(jonesConfig_ShowGamePlayOptions(nullptr) == 2);
}
