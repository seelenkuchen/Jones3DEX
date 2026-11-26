// Added: the logic tests of the Babylon column's Display and Sound screens (ADR-0026). They use a
// platform that delivers one scripted event per frame and a Null painter, so the screens run
// exactly as in the game loop without a window or a GPU. The resolution list is built from a
// synthetic display environment and the output-device list from a Null audio device with several
// outputs. The drawing sample lives in JonesUiRenderTest.cpp.
// Added: the shared helpers, fixtures and constants of the Babylon column's settings menu logic tests.
#pragma once

#include <doctest.h>

#include <cmath>
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
#include <babylon/platform/NullPlatform.h>
#include <babylon/platform/Platform.h>
#include <babylon/platform/Window.h>
#include <babylon/time/NullClock.h>
#include <babylon/ui/NullPainter.h>
#include <babylon/ui/Ui.h>

#include <Jones3D/Display/JonesDisplay.h>
#include <Jones3D/Display/JonesHud.h>
#include <Jones3D/Display/JonesReticle.h>
#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Main/JonesFixedTick.h>
#include <Jones3D/Platform/Babylon/JonesUiBrightnessCalibration.h>
#include <Jones3D/Platform/Babylon/JonesUiDisplaySettings.h>
#include <Jones3D/Platform/Babylon/JonesUiControls.h>
#include <Jones3D/Platform/Babylon/JonesUiGameplaySettings.h>
#include <Jones3D/Platform/Babylon/JonesUiOptions.h>
#include <Jones3D/Platform/Babylon/JonesUiSoundSettings.h>
#include <Jones3D/Platform/Babylon/JonesWindowMode.h>

#include <sound/Driver.h>
#include <sound/DriverOutput.h>

#include <sith/Devices/sithControl.h>
#include <sith/Engine/sithCamera.h>
#include <sith/Engine/sithPuppet.h>
#include <sith/Engine/sithRender.h>
#include <sith/Gameplay/sithOverlayMap.h>
#include <sith/Main/sithMain.h>
#include <sith/World/sithMaterial.h>
#include <sith/World/sithModel.h>
#include <sith/World/sithVoice.h>
#include <sith/World/sithWeapon.h>

#include <std/Babylon/stdBabylon.h>
#include <std/Babylon/stdBabylonEvents.h>
#include <std/Babylon/std3DBabylon.h>
#include <std/General/std.h>
#include <std/General/std3D.h>
#include <std/General/stdConfig.h>
#include <std/General/stdJSON.h>
#include <std/General/stdPlatform.h>
#include <std/types.h>

#include <cstddef>
#include <span>
#include <string_view>

namespace JonesUiSettingsTest
{
    using namespace babylon;

    constexpr const char* kConfigPath = "ui-settings-test.json";

    int J3DAPI TestPrint(const char* pFormat, ...);
    extern tHostServices g_hostServices;
    void SetupHostServices();

    // Added: JonesWindowMode.cpp updates this struct through the real JonesMain; this executable
    // does not link the game layer, so the case reads the same storage through the stub.
    extern JonesDisplaySettings g_windowModeSettings;

    bool J3DAPI ConfigFallbackHasStarted(void);
    int J3DAPI ConfigFallbackGetInt(const char* pKey, int defaultValue);
    float J3DAPI ConfigFallbackGetFloat(const char* pKey, float defaultValue);
    int J3DAPI ConfigFallbackGetStr(const char* pKey, char* pDstStr, size_t size,
        const char* pDefaultValue);

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

        std::optional<std::size_t> askChoice(std::string_view, std::string_view,
            std::span<const std::string_view>) override
        {
            return std::nullopt;   // these tests never ask for a dialog
        }

        platform::PathChoice chooseFolder(std::string_view, std::string_view) override
        {
            return {};   // these tests never ask for a dialog
        }

        platform::PathChoice chooseFile(std::string_view, std::string_view,
            std::span<const platform::FileFilter>) override
        {
            return {};   // these tests never ask for a dialog
        }

        bool rumbleGamepad(platform::GamepadId, float, float, std::uint32_t) override
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

    void AddMode(std::vector<StdVideoMode>& modes, std::uint32_t width, std::uint32_t height,
        std::uint32_t bpp, std::uint32_t refreshRate);
    void BuildDisplayEnv(StdDisplayEnvironment& env, StdDisplayInfo& info,
        std::vector<StdVideoMode>& modes);
    JonesDisplaySettings DefaultDisplaySettings();
    void SeedDisplayConfig();
}
