// Added: the logic tests of the Babylon column's Controls screen (ADR-0026). They use a temporary
// key-sets directory with hand-written .kfg files, a config, a started sithControl and a platform
// that delivers one scripted event per modal frame, so the screen runs exactly as in the game loop
// without a window or a GPU. The drawing sample lives in JonesUiRenderTest.cpp.
#include <doctest.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <babylon/cvars/Registry.h>
#include <babylon/files/FileSystem.h>
#include <babylon/files/SdlFileSystem.h>
#include <babylon/platform/Event.h>
#include <babylon/platform/Keys.h>
#include <babylon/platform/NullPlatform.h>
#include <babylon/platform/Platform.h>
#include <babylon/platform/Window.h>
#include <babylon/time/NullClock.h>
#include <babylon/ui/Binding.h>
#include <babylon/ui/NullPainter.h>
#include <babylon/ui/Ui.h>

#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Main/JonesMain.h>
#include <Jones3D/Play/JonesControl.h>
#include <Jones3D/Platform/Babylon/JonesControlSchemes.h>
#include <Jones3D/Platform/Babylon/JonesUiButtonLayout.h>
#include <Jones3D/Platform/Babylon/JonesUiControls.h>
#include <Jones3D/Platform/Babylon/JonesUiOptions.h>
#include <Jones3D/types.h>

#include <sith/Devices/sithControl.h>
#include <sith/Main/sithMain.h>

#include <std/Babylon/stdBabylon.h>
#include <std/Babylon/stdBabylonEvents.h>
#include <std/General/std.h>
#include <std/General/stdConfig.h>
#include <std/General/stdControl.h>
#include <std/General/stdPlatform.h>
#include <std/Null/stdControlNull.h>

namespace
{
    using namespace babylon;

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

    // Delivers one event per modal frame, like the save/load tests. If a screen never closes, a
    // quit request ends it with its back result instead of hanging the suite.
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
                if ( ++m_emptyPolls > 300 )
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
            ++m_messages;
            return false;
        }

        int MessageCount() const
        {
            return m_messages;
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
            m_events.push_back(platform::Event{ platform::KeyChanged{ 1, key, true, false } });
            m_events.push_back(platform::Event{ platform::KeyChanged{ 1, key, false, false } });
        }

        void PushText(const std::string& text)
        {
            m_events.push_back(platform::Event{ platform::TextInput{ 1, text } });
        }

        void PushMouseMove(float x, float y)
        {
            m_events.push_back(platform::Event{ platform::MouseMoved{ 1, x, y } });
        }

        void PushMouseButton(platform::MouseButton button)
        {
            m_events.push_back(platform::Event{ platform::MouseButtonChanged{ 1, button, true } });
            m_events.push_back(platform::Event{ platform::MouseButtonChanged{ 1, button, false } });
        }

        void PushGamepadConnected()
        {
            m_events.push_back(platform::Event{
                platform::GamepadConnected{ 1, platform::GamepadType::Xbox } });
        }

        void PushGamepadButton(platform::GamepadButton button)
        {
            m_events.push_back(platform::Event{
                platform::GamepadButtonChanged{ 1, button, true } });
            m_events.push_back(platform::Event{
                platform::GamepadButtonChanged{ 1, button, false } });
        }

        void PushGamepadButtonDown(platform::GamepadButton button)
        {
            m_events.push_back(platform::Event{
                platform::GamepadButtonChanged{ 1, button, true } });
        }

        void PushGamepadButtonUp(platform::GamepadButton button)
        {
            m_events.push_back(platform::Event{
                platform::GamepadButtonChanged{ 1, button, false } });
        }

        void PushGamepadAxis(platform::GamepadAxis axis, float value)
        {
            m_events.push_back(platform::Event{ platform::GamepadAxisMoved{ 1, axis, value } });
        }

        void PushQuit()
        {
            m_events.push_back(platform::Event{ platform::QuitRequested{} });
        }

        void PushKeyRepeated(platform::Key key, int count)
        {
            for ( int cur = 0; cur < count; ++cur )
            {
                PushKey(key);
            }
        }

    private:
        std::deque<platform::Event> m_events;
        bool m_delivering = false;
        int m_emptyPolls  = 0;
        int m_messages    = 0;
    };

    // One temp key-sets directory, config and Babylon context for a case.
    class ControlsTestContext
    {
    public:
        ScriptedPlatform platform;
        platform::NullPlatform scriptPlatform;
        time::NullClock clock;
        time::NullClock virtualClock;
        std::unique_ptr<files::FileSystem> pFileSystem;
        cvars::Registry cvars;
        StdBabylonContext context{};
        std::filesystem::path tempDir;
        std::filesystem::path configPath;
        std::string keysetsDir;
        std::string installPath;

        ControlsTestContext()
        {
            std::error_code error;
            tempDir = std::filesystem::temp_directory_path(error);
            REQUIRE_FALSE(error);
            tempDir /= "jones3d-ui-controls-" + std::to_string((unsigned)std::rand());
            std::filesystem::remove_all(tempDir, error);
            std::filesystem::create_directories(tempDir / "keysets", error);

            installPath = tempDir.string();
            keysetsDir  = (tempDir / "keysets").string();
            configPath  = tempDir / "Jones.cfg";

            pFileSystem = files::createSdlFileSystem();
            context.pClock          = &clock;
            context.pVirtualClock   = &virtualClock;
            context.pFileSystem     = pFileSystem.get();
            context.pPlatform       = &platform;
            context.pScriptPlatform = nullptr;
            context.pWindow         = nullptr;
            context.pCvars          = &cvars;
            context.pRenderDevice   = nullptr;
            context.pAudioDevice    = nullptr;

            stdBabylon_SetContext(&context);
            SetupHostServices();

            const StdConfigFallback fallback = {
                ConfigFallbackHasStarted, ConfigFallbackGetInt, ConfigFallbackGetFloat,
                ConfigFallbackGetStr,
            };
            stdConfig_SetFallback(&fallback);

            stdStartup(&g_hostServices);
            REQUIRE(stdConfig_Startup(configPath.string().c_str()));
            REQUIRE(stdConfig_SetString(SITH_CFG_INSTALLPATH, installPath.c_str()));

            jonesConfig_Shutdown();
        }

        ~ControlsTestContext()
        {
            if ( sithControl_IsOpen() )
            {
                static_cast<void>(sithControl_Close());
            }
            jonesConfig_Shutdown();
            sithControl_Shutdown();
            sithControl_ResetGlobals();

            if ( stdConfig_HasStarted() )
            {
                stdConfig_Shutdown();
            }
            stdShutdown();

            stdBabylon_SetContext(nullptr);
            std_g_pHS  = nullptr;
            sith_g_pHS = nullptr;
            pFileSystem.reset();

            std::error_code error;
            std::filesystem::remove_all(tempDir, error);
        }

        ControlsTestContext(const ControlsTestContext&) = delete;
        ControlsTestContext& operator=(const ControlsTestContext&) = delete;

        std::string SchemePath(const std::string& name) const
        {
            return (std::filesystem::path(keysetsDir) / (name + ".kfg")).string();
        }

        void WriteSchemeFile(const std::string& name, const std::string& text) const
        {
            std::ofstream file(SchemePath(name), std::ios::binary | std::ios::trunc);
            REQUIRE(file);
            file.write(text.data(), (std::streamsize)text.size());
        }

        std::string ReadSchemeFile(const std::string& name) const
        {
            std::ifstream file(SchemePath(name), std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(file),
                std::istreambuf_iterator<char>());
        }

        void StartControls()
        {
            REQUIRE(sithControl_Startup(0) == 0);
            REQUIRE(sithControl_Open() == 1);
        }
    };

    const char* const kActionKeys[] =
    {
        "JONES_STR_WLKFWD", "JONES_STR_WLKBK", "JONES_STR_TRNLFT", "JONES_STR_TRNRGHT",
        "JONES_STR_STPLFT", "JONES_STR_STPRGHT", "JONES_STR_CRAWL", "JONES_STR_RUN",
        "JONES_STR_ROLL", "JONES_STR_JUMP", "JONES_STR_LOOK", "JONES_STR_ACT",
        "JONES_STR_TGLWEAP", "JONES_STR_PREVWEAP", "JONES_STR_NEXTWEAP", "JONES_STR_TGLLGHT",
        "JONES_STR_FISTS", "JONES_STR_WHIP", "JONES_STR_MAUSER", "JONES_STR_PPSH41",
        "JONES_STR_PISTOL", "JONES_STR_SIMONOV", "JONES_STR_TOKEREV", "JONES_STR_TOZ34",
        "JONES_STR_BAZOOKA", "JONES_STR_MACHETE", "JONES_STR_SATCHEL", "JONES_STR_GRENADE",
        "JONES_STR_MAP", "JONES_STR_INTERFACE", "JONES_STR_HEALTH", "JONES_STR_IMP1",
        "JONES_STR_IMP2", "JONES_STR_IMP3", "JONES_STR_IMP4", "JONES_STR_IMP5",
        "JONES_STR_CHALK"
    };

    bool IsWeaponAction(std::size_t action)
    {
        return action >= JONESCONTROL_ACTION_FISTS && action <= JONESCONTROL_ACTION_BAZOOKA;
    }

    std::string QuotedPadded(const std::string& value, std::size_t width)
    {
        std::string text = "\"" + value + "\"";
        if ( text.size() < width )
        {
            text.append(width - text.size(), ' ');
        }
        return text;
    }

    // A hand-written scheme the parser accepts: every non-weapon action has one binding, so the
    // Edit OK check passes. Jump is distinct so the cases can find it.
    std::string HandWrittenScheme()
    {
        std::string text = "scheme heading\r\n1\r\n\r\nACTIONS KEYS\r\n\r\n";
        for ( std::size_t cur = 0; cur < std::size(kActionKeys); ++cur )
        {
            text += "\"";
            text += kActionKeys[cur];
            text += "\"";

            if ( cur == JONESCONTROL_ACTION_JUMP )
            {
                text += " \"B\"";
            }
            else if ( !IsWeaponAction(cur) )
            {
                text += " \"A\"";
            }

            text += "\r\n";
        }

        return text;
    }

    // A scheme for the reassign cases: Walk forward keeps two bindings so moving one away still
    // leaves the action valid, and Jump is bound to a different key.
    std::string ReassignScheme()
    {
        std::string text = "scheme heading\r\n1\r\n\r\nACTIONS KEYS\r\n\r\n";
        for ( std::size_t cur = 0; cur < std::size(kActionKeys); ++cur )
        {
            text += "\"";
            text += kActionKeys[cur];
            text += "\"";

            if ( cur == JONESCONTROL_ACTION_WALKFORWARD )
            {
                text += " \"J\" \"K\"";
            }
            else if ( cur == JONESCONTROL_ACTION_JUMP )
            {
                text += " \"M\"";
            }
            else if ( !IsWeaponAction(cur) )
            {
                text += " \"A\"";
            }

            text += "\r\n";
        }

        return text;
    }

    // The exact bytes the original writer produces for HandWrittenScheme after the UI added Z,
    // mouse button 1, pad button 1 and the left-stick X positive axis to Jump, then removed B.
    // The widths are the original padding: the cap action names are 24 characters and the longest
    // control is 22, both rounded up to the next multiple of five.
    std::string ExpectedEditedScheme()
    {
        constexpr std::size_t kActionWidth  = 25;
        constexpr std::size_t kControlWidth = 25;

        std::string text = "JONES_STR_HEADING\r\n";
        text += "1\r\n\r\n";
        std::string title = "JONES_STR_ACTIONS";
        title.append(kActionWidth - title.size(), ' ');
        text += title + "JONES_STR_KEYS\r\n\r\n";

        for ( std::size_t cur = 0; cur < std::size(kActionKeys); ++cur )
        {
            text += QuotedPadded(kActionKeys[cur], kActionWidth);
            if ( cur == JONESCONTROL_ACTION_JUMP )
            {
                text += QuotedPadded("Z", kControlWidth);
                text += QuotedPadded("BUTTON1", kControlWidth);
                text += QuotedPadded("RIGHT", kControlWidth);
            }
            else if ( !IsWeaponAction(cur) )
            {
                text += QuotedPadded("A", kControlWidth);
            }
            text += "\r\n";
        }

        text += "EXTRAS\r\n";
        text += "\"JONES_STR_JUMP\" \"MOUSE1\"\r\n";
        return text;
    }

    std::size_t FindScheme(const JonesControlsConfig& config, const char* pName)
    {
        for ( std::size_t cur = 0; cur < config.numSchemes; ++cur )
        {
            if ( std::strcmp(config.aSchemes[cur].aName, pName) == 0 )
            {
                return cur;
            }
        }

        return config.numSchemes;
    }

    bool LoadConfig(JonesControlsConfig& config)
    {
        return JonesControlSchemes_LoadAllSchemes(&config) == 1;
    }

    // Queues a key through the engine's script platform and reads it back. The modal loop needs
    // one event per frame, which the ScriptedPlatform gives, so the script platform is installed
    // only for this check and the context is put back afterwards.
    bool KeyBound(ControlsTestContext& testContext, SithControlFunction function, int keyId)
    {
        testContext.context.pPlatform       = &testContext.scriptPlatform;
        testContext.context.pScriptPlatform = &testContext.scriptPlatform;

        REQUIRE(stdControl_QueueKeyEvent(keyId, 1));
        stdControl_ReadControls();

        int pressed = 0;
        const bool bBound = sithControl_GetKey(function, &pressed) != 0 && pressed != 0;

        static_cast<void>(stdControl_QueueKeyEvent(keyId, 0));
        stdControl_ReadControls();

        testContext.context.pPlatform       = &testContext.platform;
        testContext.context.pScriptPlatform = nullptr;
        return bBound;
    }
}

// The windowed/headless test the scheme module and the screen wrappers use. These cases drive the
// runners directly, so only the linker needs the answer.
extern "C" bool JonesBabylon_HasWindow(void)
{
    return false;
}

// The game-layer hooks the scheme module calls when a scheme becomes active. They are no-ops here;
// the cases check the bindings the module writes through sithControl.
extern "C" void J3DAPI JonesMain_BindToggleMenuControlKeys(const size_t* aKeyIds, size_t numKeys)
{
    (void)aKeyIds;
    (void)numKeys;
}

extern "C" void J3DAPI JonesHud_BindActivateControlKeys(const size_t* aKeyIds, size_t numKeys)
{
    (void)aKeyIds;
    (void)numKeys;
}

extern "C" void JonesControl_EnableJoystickAxes(void)
{
}

TEST_CASE("the captured bindings map to the original control ids")
{
    ControlsTestContext testContext;

    struct
    {
        ui::Binding binding;
        std::size_t controlId;
    } aCases[] =
    {
        { ui::Binding{ ui::BindingKind::Key, platform::Key::A }, (std::size_t)STDKEY_A },
        { ui::Binding{ ui::BindingKind::Key, platform::Key::Escape }, (std::size_t)STDKEY_ESCAPE },
        { ui::Binding{ ui::BindingKind::MouseButton, platform::Key::Unknown,
            platform::MouseButton::Left }, (std::size_t)STDCONTROL_KID_MOUSE_LBUTTON },
        { ui::Binding{ ui::BindingKind::MouseButton, platform::Key::Unknown,
            platform::MouseButton::Right }, (std::size_t)STDCONTROL_KID_MOUSE_RBUTTON },
        { ui::Binding{ ui::BindingKind::MouseButton, platform::Key::Unknown,
            platform::MouseButton::Middle }, (std::size_t)STDCONTROL_KID_MOUSE_MBUTTON },
        { ui::Binding{ ui::BindingKind::MouseButton, platform::Key::Unknown,
            platform::MouseButton::X1 }, (std::size_t)STDCONTROL_KID_MOUSE_XBUTTON },
        { ui::Binding{ ui::BindingKind::GamepadButton, platform::Key::Unknown,
            platform::MouseButton::Left, platform::GamepadButton::South },
            (std::size_t)STDCONTROL_JOYSTICK_GETBUTTON(0, 0) },
        { ui::Binding{ ui::BindingKind::GamepadButton, platform::Key::Unknown,
            platform::MouseButton::Left, platform::GamepadButton::Guide },
            (std::size_t)STDCONTROL_JOYSTICK_GETBUTTON(0, 10) },
        { ui::Binding{ ui::BindingKind::GamepadButton, platform::Key::Unknown,
            platform::MouseButton::Left, platform::GamepadButton::DpadLeft },
            (std::size_t)STDCONTROL_JOYSTICK_GETPOVLEFT(0, 0) },
        { ui::Binding{ ui::BindingKind::GamepadButton, platform::Key::Unknown,
            platform::MouseButton::Left, platform::GamepadButton::DpadUp },
            (std::size_t)STDCONTROL_JOYSTICK_GETPOVUP(0, 0) },
        { ui::Binding{ ui::BindingKind::GamepadAxis, platform::Key::Unknown,
            platform::MouseButton::Left, platform::GamepadButton::South,
            platform::GamepadAxis::LeftX, 1.0f }, (std::size_t)JONESCONFIG_JOYAXISKID_RIGHT },
        { ui::Binding{ ui::BindingKind::GamepadAxis, platform::Key::Unknown,
            platform::MouseButton::Left, platform::GamepadButton::South,
            platform::GamepadAxis::LeftX, -1.0f }, (std::size_t)JONESCONFIG_JOYAXISKID_LEFT },
        { ui::Binding{ ui::BindingKind::GamepadAxis, platform::Key::Unknown,
            platform::MouseButton::Left, platform::GamepadButton::South,
            platform::GamepadAxis::LeftY, 1.0f }, (std::size_t)JONESCONFIG_JOYAXISKID_DOWN },
        { ui::Binding{ ui::BindingKind::GamepadAxis, platform::Key::Unknown,
            platform::MouseButton::Left, platform::GamepadButton::South,
            platform::GamepadAxis::LeftY, -1.0f }, (std::size_t)JONESCONFIG_JOYAXISKID_UP },
        { ui::Binding{ ui::BindingKind::GamepadAxis, platform::Key::Unknown,
            platform::MouseButton::Left, platform::GamepadButton::South,
            platform::GamepadAxis::LeftTrigger, 1.0f },
            (std::size_t)JONESCONFIG_JOYAXISKID_GROUND },
        { ui::Binding{ ui::BindingKind::GamepadAxis, platform::Key::Unknown,
            platform::MouseButton::Left, platform::GamepadButton::South,
            platform::GamepadAxis::LeftTrigger, -1.0f },
            (std::size_t)JONESCONFIG_JOYAXISKID_AIR },
        { ui::Binding{ ui::BindingKind::GamepadAxis, platform::Key::Unknown,
            platform::MouseButton::Left, platform::GamepadButton::South,
            platform::GamepadAxis::RightX, 1.0f },
            (std::size_t)JONESCONTROLSCHEMES_JOYAXIS_RX_POS },
        { ui::Binding{ ui::BindingKind::GamepadAxis, platform::Key::Unknown,
            platform::MouseButton::Left, platform::GamepadButton::South,
            platform::GamepadAxis::RightX, -1.0f },
            (std::size_t)JONESCONTROLSCHEMES_JOYAXIS_RX_NEG },
        { ui::Binding{ ui::BindingKind::GamepadAxis, platform::Key::Unknown,
            platform::MouseButton::Left, platform::GamepadButton::South,
            platform::GamepadAxis::RightY, 1.0f },
            (std::size_t)JONESCONTROLSCHEMES_JOYAXIS_RY_POS },
        { ui::Binding{ ui::BindingKind::GamepadAxis, platform::Key::Unknown,
            platform::MouseButton::Left, platform::GamepadButton::South,
            platform::GamepadAxis::RightY, -1.0f },
            (std::size_t)JONESCONTROLSCHEMES_JOYAXIS_RY_NEG },
        { ui::Binding{ ui::BindingKind::GamepadAxis, platform::Key::Unknown,
            platform::MouseButton::Left, platform::GamepadButton::South,
            platform::GamepadAxis::RightTrigger, 1.0f },
            (std::size_t)JONESCONFIG_JOYAXISKID_TWISTR },
        { ui::Binding{ ui::BindingKind::GamepadAxis, platform::Key::Unknown,
            platform::MouseButton::Left, platform::GamepadButton::South,
            platform::GamepadAxis::RightTrigger, -1.0f },
            (std::size_t)JONESCONFIG_JOYAXISKID_TWISTL },
    };

    for ( const auto& testCase : aCases )
    {
        std::size_t controlId = 0;
        CHECK(JonesUiControls_MapBinding(testCase.binding, controlId));
        CHECK(controlId == testCase.controlId);
    }

    std::size_t controlId = 0;
    CHECK_FALSE(JonesUiControls_MapBinding(ui::Binding{}, controlId));
    CHECK_FALSE(JonesUiControls_MapBinding(ui::Binding{ ui::BindingKind::Key,
        platform::Key::PrintScreen }, controlId));
    CHECK_FALSE(JonesUiControls_MapBinding(ui::Binding{ ui::BindingKind::MouseButton,
        platform::Key::Unknown, platform::MouseButton::X2 }, controlId));
    CHECK_FALSE(JonesUiControls_MapBinding(ui::Binding{ ui::BindingKind::GamepadAxis,
        platform::Key::Unknown, platform::MouseButton::Left, platform::GamepadButton::South,
        platform::GamepadAxis::LeftX, 0.0f }, controlId));
}

TEST_CASE("the scheme name rules are the original ones")
{
    JonesControlsScheme scheme;
    std::memset(&scheme, 0, sizeof(scheme));
    std::snprintf(scheme.aName, sizeof(scheme.aName), "%s", "taken");

    JonesControlsConfig config;
    std::memset(&config, 0, sizeof(config));
    config.numSchemes = 1;
    config.aSchemes   = &scheme;

    CHECK(JonesUiControls_ValidateSchemeName("", config) == JonesUiControlsNameStatus::Empty);
    CHECK(JonesUiControls_ValidateSchemeName("a/b", config) == JonesUiControlsNameStatus::Invalid);
    CHECK(JonesUiControls_ValidateSchemeName("a:b", config) == JonesUiControlsNameStatus::Invalid);
    CHECK(JonesUiControls_ValidateSchemeName("a\\b", config) == JonesUiControlsNameStatus::Invalid);
    CHECK(JonesUiControls_ValidateSchemeName("a?b", config) == JonesUiControlsNameStatus::Invalid);
    CHECK(JonesUiControls_ValidateSchemeName("a<b", config) == JonesUiControlsNameStatus::Invalid);
    CHECK(JonesUiControls_ValidateSchemeName("a>b", config) == JonesUiControlsNameStatus::Invalid);
    CHECK(JonesUiControls_ValidateSchemeName("a*b", config) == JonesUiControlsNameStatus::Invalid);
    CHECK(JonesUiControls_ValidateSchemeName("a|b", config) == JonesUiControlsNameStatus::Invalid);
    CHECK(JonesUiControls_ValidateSchemeName("a+b", config) == JonesUiControlsNameStatus::Invalid);
    CHECK(JonesUiControls_ValidateSchemeName("a\"b", config) == JonesUiControlsNameStatus::Invalid);
    CHECK(JonesUiControls_ValidateSchemeName("taken", config)
        == JonesUiControlsNameStatus::Duplicate);
    CHECK(JonesUiControls_ValidateSchemeName("fresh", config) == JonesUiControlsNameStatus::Ok);
}

TEST_CASE("editing an action writes the exact file and rebinds on OK")
{
    ControlsTestContext testContext;
    testContext.WriteSchemeFile("ui-seed", HandWrittenScheme());

    JonesControlsConfig config;
    REQUIRE(LoadConfig(config));
    const std::size_t seedIndex = FindScheme(config, "ui-seed");
    REQUIRE(seedIndex < config.numSchemes);
    config.selectedShemeIdx = (int)seedIndex;
    testContext.StartControls();

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    // Main: scheme choice -> Edit.
    testContext.platform.PushKeyRepeated(platform::Key::Down, 5);
    testContext.platform.PushKey(platform::Key::Enter);
    // Edit: pick the Jump row.
    testContext.platform.PushKeyRepeated(platform::Key::Down, JONESCONTROL_ACTION_JUMP);
    testContext.platform.PushKey(platform::Key::Enter);
    // Add a key, a mouse button, a pad button and the left-stick X positive axis; each capture
    // starts by accepting the Add row. BindingCapture captures only the navigation stick axes.
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Z);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushMouseButton(platform::MouseButton::Left);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushGamepadButton(platform::GamepadButton::South);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushGamepadAxis(platform::GamepadAxis::LeftX, 1.0f);
    testContext.platform.PushGamepadAxis(platform::GamepadAxis::LeftX, 0.0f);
    // Remove the seeded B from the selected row, then Tab out of the remove list to Back.
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Tab);
    testContext.platform.PushKey(platform::Key::Enter);
    // Edit OK, then main OK (focus is on Edit -> Reset -> OK).
    testContext.platform.PushKey(platform::Key::Tab);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKeyRepeated(platform::Key::Down, 2);
    testContext.platform.PushKey(platform::Key::Enter);

    REQUIRE(JonesUiControls_RunControlOptionsScreen(ui, &painter, config) == 1);
    jonesConfig_FreeControlConfigEntry(&config);

    CHECK(testContext.ReadSchemeFile("ui-seed") == ExpectedEditedScheme());

    char aActive[128] = { 0 };
    REQUIRE(stdConfig_GetString(JONESCONFIG_CFG_CONTROLS_CONFIGFILE, aActive,
        sizeof(aActive), ""));
    CHECK(std::string(aActive) == "ui-seed");

    CHECK(KeyBound(testContext, SITHCONTROL_JUMP, STDKEY_Z));
}

TEST_CASE("capturing the right stick and a trigger writes the EXTRAS controls")
{
    ControlsTestContext testContext;
    testContext.WriteSchemeFile("ui-extras", HandWrittenScheme());

    JonesControlsConfig config;
    REQUIRE(LoadConfig(config));
    const std::size_t seedIndex = FindScheme(config, "ui-extras");
    REQUIRE(seedIndex < config.numSchemes);
    config.selectedShemeIdx = (int)seedIndex;
    testContext.StartControls();

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    // Main -> Edit -> the Jump row.
    testContext.platform.PushKeyRepeated(platform::Key::Down, 5);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKeyRepeated(platform::Key::Down, JONESCONTROL_ACTION_JUMP);
    testContext.platform.PushKey(platform::Key::Enter);

    // Capture RightX positive (RX+, EXTRAS), the right trigger (RZ/TWISTR, the original action
    // line) and the left trigger (Z+, EXTRAS).
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushGamepadAxis(platform::GamepadAxis::RightX, 1.0f);
    testContext.platform.PushGamepadAxis(platform::GamepadAxis::RightX, 0.0f);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushGamepadAxis(platform::GamepadAxis::RightTrigger, 1.0f);
    testContext.platform.PushGamepadAxis(platform::GamepadAxis::RightTrigger, 0.0f);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushGamepadAxis(platform::GamepadAxis::LeftTrigger, 1.0f);
    testContext.platform.PushGamepadAxis(platform::GamepadAxis::LeftTrigger, 0.0f);

    // Down to the remove list, Tab to Back, then save the Edit list and the main screen.
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Tab);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Tab);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKeyRepeated(platform::Key::Down, 2);
    testContext.platform.PushKey(platform::Key::Enter);

    REQUIRE(JonesUiControls_RunControlOptionsScreen(ui, &painter, config) == 1);
    jonesConfig_FreeControlConfigEntry(&config);

    JonesControlsScheme written;
    REQUIRE(jonesConfig_LoadControlScheme(testContext.SchemePath("ui-extras").c_str(),
        &written) == 1);

    const std::size_t numBindings = JonesControlSchemes_GetNumBindings(&written,
        JONESCONTROL_ACTION_JUMP);
    REQUIRE(numBindings == 4);

    const std::size_t aExpected[] = {
        (std::size_t)STDKEY_B,
        (std::size_t)JONESCONFIG_JOYAXISKID_TWISTR,
        (std::size_t)JONESCONTROLSCHEMES_JOYAXIS_RX_POS,
        (std::size_t)JONESCONFIG_JOYAXISKID_GROUND,
    };
    for ( std::size_t expected : aExpected )
    {
        bool bFound = false;
        for ( std::size_t index = 0; index < numBindings; ++index )
        {
            std::size_t controlId = 0;
            REQUIRE(JonesControlSchemes_GetBinding(&written, JONESCONTROL_ACTION_JUMP, index,
                &controlId));
            bFound = bFound || controlId == expected;
        }
        CHECK(bFound);
    }

    const std::string text = testContext.ReadSchemeFile("ui-extras");
    CHECK(text.find("\"JONES_STR_JUMP\" \"RX+\"\r\n") != std::string::npos);
    CHECK(text.find("\"JONES_STR_JUMP\" \"Z+\"\r\n") != std::string::npos);
    CHECK(text.find("\"TWISTR\"") != std::string::npos);
}

TEST_CASE("Cancel keeps the scheme file and the active bindings")
{
    ControlsTestContext testContext;
    testContext.WriteSchemeFile("ui-seed", HandWrittenScheme());
    const std::string before = testContext.ReadSchemeFile("ui-seed");

    JonesControlsConfig config;
    REQUIRE(LoadConfig(config));
    const std::size_t seedIndex = FindScheme(config, "ui-seed");
    REQUIRE(seedIndex < config.numSchemes);
    config.selectedShemeIdx = (int)seedIndex;
    testContext.StartControls();

    // Make the seeded scheme active so its Jump = B can be checked after the cancel.
    REQUIRE(JonesControlSchemes_SetActiveScheme(&config.aSchemes[seedIndex]) == 1);
    CHECK(KeyBound(testContext, SITHCONTROL_JUMP, STDKEY_B));

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    testContext.platform.PushKeyRepeated(platform::Key::Down, 5);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKeyRepeated(platform::Key::Down, JONESCONTROL_ACTION_JUMP);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Z);
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Tab);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Tab);
    testContext.platform.PushKey(platform::Key::Enter);
    // Escape closes the main screen with its back result.
    testContext.platform.PushKey(platform::Key::Escape);

    REQUIRE(JonesUiControls_RunControlOptionsScreen(ui, &painter, config) == 2);
    jonesConfig_FreeControlConfigEntry(&config);

    CHECK(testContext.ReadSchemeFile("ui-seed") == before);
    CHECK(KeyBound(testContext, SITHCONTROL_JUMP, STDKEY_B));
    CHECK_FALSE(KeyBound(testContext, SITHCONTROL_JUMP, STDKEY_Z));
}

TEST_CASE("a control bound to another action is moved on Yes")
{
    ControlsTestContext testContext;
    testContext.WriteSchemeFile("ui-reassign", ReassignScheme());

    JonesControlsConfig config;
    REQUIRE(LoadConfig(config));
    const std::size_t seedIndex = FindScheme(config, "ui-reassign");
    REQUIRE(seedIndex < config.numSchemes);
    config.selectedShemeIdx = (int)seedIndex;
    testContext.StartControls();

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    testContext.platform.PushKeyRepeated(platform::Key::Down, 5);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKeyRepeated(platform::Key::Down, JONESCONTROL_ACTION_JUMP);
    testContext.platform.PushKey(platform::Key::Enter);
    // J is already bound to Walk forward: answer Yes to the reassign prompt.
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::J);
    testContext.platform.PushKey(platform::Key::Enter);
    // Back to the Edit list, save the scheme, then save the main screen.
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Tab);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Tab);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKeyRepeated(platform::Key::Down, 2);
    testContext.platform.PushKey(platform::Key::Enter);

    REQUIRE(JonesUiControls_RunControlOptionsScreen(ui, &painter, config) == 1);
    jonesConfig_FreeControlConfigEntry(&config);

    JonesControlsScheme written;
    REQUIRE(jonesConfig_LoadControlScheme(testContext.SchemePath("ui-reassign").c_str(),
        &written) == 1);

    CHECK(JonesControlSchemes_GetNumBindings(&written, JONESCONTROL_ACTION_JUMP) == 2);
    CHECK(JonesControlSchemes_GetNumBindings(&written, JONESCONTROL_ACTION_WALKFORWARD) == 1);

    std::size_t controlId = 0;
    REQUIRE(JonesControlSchemes_GetBinding(&written, JONESCONTROL_ACTION_JUMP, 0, &controlId));
    CHECK(controlId == (std::size_t)STDKEY_M);
    REQUIRE(JonesControlSchemes_GetBinding(&written, JONESCONTROL_ACTION_JUMP, 1, &controlId));
    CHECK(controlId == (std::size_t)STDKEY_J);
    REQUIRE(JonesControlSchemes_GetBinding(&written, JONESCONTROL_ACTION_WALKFORWARD, 0,
        &controlId));
    CHECK(controlId == (std::size_t)STDKEY_K);
}

TEST_CASE("answering No to the reassign prompt leaves the scheme unchanged")
{
    ControlsTestContext testContext;
    testContext.WriteSchemeFile("ui-reassign", ReassignScheme());
    const std::string before = testContext.ReadSchemeFile("ui-reassign");

    JonesControlsConfig config;
    REQUIRE(LoadConfig(config));
    const std::size_t seedIndex = FindScheme(config, "ui-reassign");
    REQUIRE(seedIndex < config.numSchemes);
    config.selectedShemeIdx = (int)seedIndex;
    testContext.StartControls();

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    testContext.platform.PushKeyRepeated(platform::Key::Down, 5);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKeyRepeated(platform::Key::Down, JONESCONTROL_ACTION_JUMP);
    testContext.platform.PushKey(platform::Key::Enter);
    // J is already bound: Down to No and accept.
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::J);
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter);
    // Back to the Edit list and save it unchanged, then cancel the main screen.
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Tab);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Tab);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Escape);

    REQUIRE(JonesUiControls_RunControlOptionsScreen(ui, &painter, config) == 2);
    jonesConfig_FreeControlConfigEntry(&config);

    CHECK(testContext.ReadSchemeFile("ui-reassign") == before);
}

TEST_CASE("the New flow creates a custom scheme and its name rules hold")
{
    ControlsTestContext testContext;

    JonesControlsConfig config;
    REQUIRE(LoadConfig(config));
    REQUIRE(config.numSchemes == 5);
    // Pin the choice so the first scripted Down reaches the screen.
    config.selectedShemeIdx = 0;

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    // New -> an empty name is refused, then Cancel the name screen.
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKeyRepeated(platform::Key::Down, 2);
    testContext.platform.PushKey(platform::Key::Enter);
    // New -> an invalid name is refused, then Cancel.
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushText("bad/name");
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter);
    // New -> a good name, accept the new scheme's Edit screen, then OK the main screen.
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushText("NewScheme");
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Tab);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKeyRepeated(platform::Key::Down, 6);
    testContext.platform.PushKey(platform::Key::Enter);

    REQUIRE(JonesUiControls_RunControlOptionsScreen(ui, &painter, config) == 1);
    jonesConfig_FreeControlConfigEntry(&config);

    CHECK(std::filesystem::exists(testContext.SchemePath("NewScheme")));

    char aActive[128] = { 0 };
    REQUIRE(stdConfig_GetString(JONESCONFIG_CFG_CONTROLS_CONFIGFILE, aActive,
        sizeof(aActive), ""));
    CHECK(std::string(aActive) == "NewScheme");
}

TEST_CASE("deleting a custom scheme removes its file only on OK")
{
    ControlsTestContext testContext;
    testContext.WriteSchemeFile("ui-delete", HandWrittenScheme());

    JonesControlsConfig config;
    REQUIRE(LoadConfig(config));
    const std::size_t seedIndex = FindScheme(config, "ui-delete");
    REQUIRE(seedIndex < config.numSchemes);
    config.selectedShemeIdx = (int)seedIndex;
    testContext.StartControls();

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    // Choice -> Delete -> the now-disabled Delete moves focus to Mouse. Reset is now enabled for
    // the built-in selection too, so it takes one focus slot and four Down presses reach OK.
    testContext.platform.PushKeyRepeated(platform::Key::Down, 2);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKeyRepeated(platform::Key::Down, 4);
    testContext.platform.PushKey(platform::Key::Enter);

    REQUIRE(JonesUiControls_RunControlOptionsScreen(ui, &painter, config) == 1);
    jonesConfig_FreeControlConfigEntry(&config);

    CHECK_FALSE(std::filesystem::exists(testContext.SchemePath("ui-delete")));
}

TEST_CASE("cancelling after a delete keeps the file")
{
    ControlsTestContext testContext;
    testContext.WriteSchemeFile("ui-delete", HandWrittenScheme());

    JonesControlsConfig config;
    REQUIRE(LoadConfig(config));
    const std::size_t seedIndex = FindScheme(config, "ui-delete");
    REQUIRE(seedIndex < config.numSchemes);
    config.selectedShemeIdx = (int)seedIndex;

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    // Delete, then Escape closes the main screen.
    testContext.platform.PushKeyRepeated(platform::Key::Down, 2);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Escape);

    REQUIRE(JonesUiControls_RunControlOptionsScreen(ui, &painter, config) == 2);
    jonesConfig_FreeControlConfigEntry(&config);

    CHECK(std::filesystem::exists(testContext.SchemePath("ui-delete")));
}

TEST_CASE("the mouse toggle writes its Jones.cfg key and the controller key is untouched")
{
    ControlsTestContext testContext;
    testContext.WriteSchemeFile("ui-seed", HandWrittenScheme());

    JonesControlsConfig config;
    REQUIRE(LoadConfig(config));
    const std::size_t seedIndex = FindScheme(config, "ui-seed");
    REQUIRE(seedIndex < config.numSchemes);
    config.selectedShemeIdx = (int)seedIndex;
    testContext.StartControls();

    // A stale controller key in the file must not be read or rewritten by the screen.
    REQUIRE(stdConfig_SetBool(JONESCONTROL_CFG_CONTROLS_CONTROLLER, true));
    // Pin the original off value so this case tests turning the mouse camera on.
    REQUIRE(stdConfig_SetBool(JONESCONTROL_CFG_CONTROLS_MOUSE, false));

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    // Choice -> Mouse, turn it on, then Mouse -> Rumble -> Edit -> Reset -> OK.
    testContext.platform.PushKeyRepeated(platform::Key::Down, 3);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKeyRepeated(platform::Key::Down, 4);
    testContext.platform.PushKey(platform::Key::Enter);

    REQUIRE(JonesUiControls_RunControlOptionsScreen(ui, &painter, config) == 1);
    jonesConfig_FreeControlConfigEntry(&config);

    CHECK(stdConfig_GetBool(JONESCONTROL_CFG_CONTROLS_MOUSE, false));
    CHECK(stdConfig_GetBool(JONESCONTROL_CFG_CONTROLS_CONTROLLER, false));
}

TEST_CASE("a missing mouse camera key follows the Enhanced default and an existing key wins")
{
    ControlsTestContext testContext;

    JonesControlsConfig config;
    REQUIRE(LoadConfig(config));

    REQUIRE_FALSE(stdConfig_Contains(JONESCONTROL_CFG_CONTROLS_MOUSE));

    JonesUiControlsModel model;
    REQUIRE(JonesUiControls_InitModel(model, config));
    CHECK(model.bMouse == (J3D_QOL_VALUE(true, false) != 0));
    JonesUiControls_FreeModel(model);

    // An existing key, even the original off value, is never replaced by the new default.
    REQUIRE(stdConfig_SetBool(JONESCONTROL_CFG_CONTROLS_MOUSE, false));
    REQUIRE(JonesUiControls_InitModel(model, config));
    CHECK_FALSE(model.bMouse);
    JonesUiControls_FreeModel(model);

    jonesConfig_FreeControlConfigEntry(&config);
}

TEST_CASE("the rumble row switches the setting and the adapter at once")
{
    ControlsTestContext testContext;
    testContext.WriteSchemeFile("ui-seed", HandWrittenScheme());

    JonesControlsConfig config;
    REQUIRE(LoadConfig(config));
    const std::size_t seedIndex = FindScheme(config, "ui-seed");
    REQUIRE(seedIndex < config.numSchemes);
    config.selectedShemeIdx = (int)seedIndex;
    testContext.StartControls();

    // Rumble is on by default.
    stdControl_EnableRumble(true);
    REQUIRE(stdConfig_SetBool(JONESCONTROL_CFG_ENGINE_INPUT_RUMBLE, true));

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    // Choice -> Rumble, turn it off. The row applies and stores it immediately.
    testContext.platform.PushKeyRepeated(platform::Key::Down, 4);
    testContext.platform.PushKey(platform::Key::Enter);

    // Rumble -> Edit -> Reset -> OK confirms the screen.
    testContext.platform.PushKeyRepeated(platform::Key::Down, 3);
    testContext.platform.PushKey(platform::Key::Enter);

    REQUIRE(JonesUiControls_RunControlOptionsScreen(ui, &painter, config) == 1);
    jonesConfig_FreeControlConfigEntry(&config);

    CHECK_FALSE(stdConfig_GetBool(JONESCONTROL_CFG_ENGINE_INPUT_RUMBLE, true));
    CHECK_FALSE(stdControl_IsRumbleEnabled());

    // Leave the shared flag at its default for any case that runs next.
    stdControl_EnableRumble(true);
}

TEST_CASE("the headless wrapper answers cancel without blocking")
{
    ControlsTestContext testContext;

    CHECK(jonesConfig_ShowControlOptions(nullptr) == 2);
}


namespace
{
    // One loaded custom scheme ready for a button-layout screen. The caller owns config.
    bool LoadSchemeForLayout(ControlsTestContext& testContext, JonesControlsConfig& config,
        std::size_t& index)
    {
        testContext.WriteSchemeFile("ui-layout", HandWrittenScheme());
        if ( !LoadConfig(config) )
        {
            return false;
        }
        index = FindScheme(config, "ui-layout");
        return index < config.numSchemes;
    }
}

TEST_CASE("binding on the controller screen replaces only the pad binding")
{
    sithControl_SetModernControlStyle(false);
    ControlsTestContext testContext;
    JonesControlsConfig config;
    std::size_t index = 0;
    REQUIRE(LoadSchemeForLayout(testContext, config, index));
    config.selectedShemeIdx = (int)index;
    testContext.StartControls();

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    JonesUiButtonLayoutModel model;
    model.pConfig = &config;
    model.schemeIndex = index;
    model.device = JonesUiButtonLayoutDevice::Controller;
    JonesUiButtonLayout_RefreshRows(model);

    // Walk Forward -> Bind -> gamepad South -> Back.
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushGamepadButton(platform::GamepadButton::South);
    testContext.platform.PushKey(platform::Key::Escape);

    REQUIRE(JonesUiButtonLayout_RunScreen(ui, &painter, model) == 1);

    std::size_t controller = 0;
    REQUIRE(JonesUiButtonLayout_GetDeviceBinding(config.aSchemes[index],
        JONESCONTROL_ACTION_WALKFORWARD, JonesUiButtonLayoutDevice::Controller, controller));
    CHECK(controller == STDCONTROL_JOYSTICK_GETBUTTON(0, 0));

    std::size_t keyboard = 0;
    REQUIRE(JonesUiButtonLayout_GetDeviceBinding(config.aSchemes[index],
        JONESCONTROL_ACTION_WALKFORWARD, JonesUiButtonLayoutDevice::KeyboardMouse, keyboard));
    CHECK(keyboard < STDCONTROL_MAX_KEYBOARD_BUTTONS);

    const std::string written = testContext.ReadSchemeFile("ui-layout");
    CHECK(written.find("BUTTON1") != std::string::npos);
    jonesConfig_FreeControlConfigEntry(&config);
    sithControl_SetModernControlStyle(true);
}

TEST_CASE("escape cancels a binding capture and leaves the scheme file alone")
{
    ControlsTestContext testContext;
    JonesControlsConfig config;
    std::size_t index = 0;
    REQUIRE(LoadSchemeForLayout(testContext, config, index));
    config.selectedShemeIdx = (int)index;
    testContext.StartControls();

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    JonesUiButtonLayoutModel model;
    model.pConfig = &config;
    model.schemeIndex = index;
    model.device = JonesUiButtonLayoutDevice::Controller;
    JonesUiButtonLayout_RefreshRows(model);

    // Bind, cancel the capture, then leave with Back.
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Escape);
    testContext.platform.PushKey(platform::Key::Escape);

    REQUIRE(JonesUiButtonLayout_RunScreen(ui, &painter, model) == 1);
    std::size_t controller = 0;
    CHECK_FALSE(JonesUiButtonLayout_GetDeviceBinding(config.aSchemes[index],
        JONESCONTROL_ACTION_WALKFORWARD, JonesUiButtonLayoutDevice::Controller, controller));
    jonesConfig_FreeControlConfigEntry(&config);
}

TEST_CASE("a pad control bound to another action asks before it moves")
{
    sithControl_SetModernControlStyle(false);
    ControlsTestContext testContext;
    JonesControlsConfig config;
    std::size_t index = 0;
    REQUIRE(LoadSchemeForLayout(testContext, config, index));
    config.selectedShemeIdx = (int)index;
    testContext.StartControls();

    const std::size_t padSouth = STDCONTROL_JOYSTICK_GETBUTTON(0, 0);
    REQUIRE(JonesControlSchemes_AddBinding(&config.aSchemes[index], JONESCONTROL_ACTION_JUMP,
        padSouth) == 1);

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    JonesUiButtonLayoutModel model;
    model.pConfig = &config;
    model.schemeIndex = index;
    model.device = JonesUiButtonLayoutDevice::Controller;
    JonesUiButtonLayout_RefreshRows(model);

    // Walk Forward -> Bind -> South asks to move it from Jump -> Yes -> Back.
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushGamepadButton(platform::GamepadButton::South);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Escape);

    REQUIRE(JonesUiButtonLayout_RunScreen(ui, &painter, model) == 1);

    std::size_t controller = 0;
    REQUIRE(JonesUiButtonLayout_GetDeviceBinding(config.aSchemes[index],
        JONESCONTROL_ACTION_WALKFORWARD, JonesUiButtonLayoutDevice::Controller, controller));
    CHECK(controller == padSouth);
    CHECK_FALSE(JonesUiButtonLayout_GetDeviceBinding(config.aSchemes[index],
        JONESCONTROL_ACTION_JUMP, JonesUiButtonLayoutDevice::Controller, controller));
    jonesConfig_FreeControlConfigEntry(&config);
    sithControl_SetModernControlStyle(true);
}

TEST_CASE("reset binds restores the device defaults after a yes")
{
    ControlsTestContext testContext;
    JonesControlsConfig config;
    std::size_t index = 0;
    REQUIRE(LoadSchemeForLayout(testContext, config, index));
    config.selectedShemeIdx = (int)index;
    testContext.StartControls();

    const std::size_t padEast = STDCONTROL_JOYSTICK_GETBUTTON(0, 1);
    REQUIRE(JonesControlSchemes_AddBinding(&config.aSchemes[index], JONESCONTROL_ACTION_JUMP,
        padEast) == 1);

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    JonesUiButtonLayoutModel model;
    model.pConfig = &config;
    model.schemeIndex = index;
    model.device = JonesUiButtonLayoutDevice::Controller;
    JonesUiButtonLayout_RefreshRows(model);

    // Reset, confirm with Yes, then Back.
    testContext.platform.PushGamepadButton(platform::GamepadButton::North);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Escape);

    REQUIRE(JonesUiButtonLayout_RunScreen(ui, &painter, model) == 1);

    // The hand-written scheme has no pad bindings, so resetting Jump puts its default pad
    // control back (and not East).
    std::size_t controller = 0;
    REQUIRE(JonesUiButtonLayout_GetDeviceBinding(config.aSchemes[index],
        JONESCONTROL_ACTION_JUMP, JonesUiButtonLayoutDevice::Controller, controller));

    JonesControlsScheme defaults{};
    REQUIRE(JonesControlSchemes_SetDefaultScheme(&defaults,
        JonesUiControls_DefaultSchemeIndex()) == 1);
    std::size_t expected = 0;
    REQUIRE(JonesUiButtonLayout_GetDeviceBinding(defaults,
        JONESCONTROL_ACTION_JUMP, JonesUiButtonLayoutDevice::Controller, expected));
    CHECK(controller == expected);
    jonesConfig_FreeControlConfigEntry(&config);
}

TEST_CASE("back refuses while a required action has no binding")
{
    ControlsTestContext testContext;
    JonesControlsConfig config;
    std::size_t index = 0;
    REQUIRE(LoadSchemeForLayout(testContext, config, index));

    // Remove Crawl's only binding: it is required in both control styles.
    JonesControlsScheme& scheme = config.aSchemes[index];
    REQUIRE(JonesControlSchemes_GetNumBindings(&scheme, JONESCONTROL_ACTION_CRAWL) == 1);
    REQUIRE(JonesControlSchemes_RemoveBinding(&scheme, JONESCONTROL_ACTION_CRAWL, 0) == 1);
    config.selectedShemeIdx = (int)index;
    testContext.StartControls();

    const std::string before = testContext.ReadSchemeFile("ui-layout");

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    JonesUiButtonLayoutModel model;
    model.pConfig = &config;
    model.schemeIndex = index;
    model.device = JonesUiButtonLayoutDevice::Controller;
    JonesUiButtonLayout_RefreshRows(model);
    // Simulate a change so Back runs the required-binding check.
    model.bChanged = true;

    // Back is refused; a quit then closes the loop as the game would after the dialog.
    testContext.platform.PushKey(platform::Key::Escape);
    testContext.platform.PushQuit();

    REQUIRE(JonesUiButtonLayout_RunScreen(ui, &painter, model) == 1);
    CHECK(testContext.ReadSchemeFile("ui-layout") == before);
    stdBabylonEvents_Reset();
    jonesConfig_FreeControlConfigEntry(&config);
}


TEST_CASE("the Options Controls tab opens the Button Layout screen and keeps Options open")
{
    ControlsTestContext testContext;
    JonesControlsConfig config;
    std::size_t index = 0;
    REQUIRE(LoadSchemeForLayout(testContext, config, index));
    config.selectedShemeIdx = (int)index;
    testContext.StartControls();

    JonesSoundSettings sound{};
    sound.maxSoundVolume = 1.0f;

    JonesUiOptionsData data;
    data.pControlsConfig = &config;
    data.pSoundSettings = &sound;

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    JonesUiButtonLayout_ResetLayoutFramesForTest();

    // Controls tab, down to Controller, open it, Back to Options, open it again to prove focus
    // returned to the same row, Back again, then close Options.
    testContext.platform.PushKey(platform::Key::E);
    testContext.platform.PushKeyRepeated(platform::Key::Down, 3);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Escape);
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Escape);
    testContext.platform.PushKey(platform::Key::Escape);

    REQUIRE(JonesUiOptions_RunOptionsScreen(ui, &painter, data) == 1);

    CHECK(JonesUiButtonLayout_GetLayoutRunsForTest() == 2u);
    CHECK(JonesUiButtonLayout_GetLayoutFramesForTest() > 1u);
    CHECK(JonesUiButtonLayout_GetLastDeviceForTest()
        == JonesUiButtonLayoutDevice::Controller);
    CHECK(ui.screenCount() == 0u);

    jonesConfig_FreeControlConfigEntry(&config);
}


#ifdef J3D_QOL_IMPROVEMENTS
TEST_CASE("Modern stick-owned rows do not capture a binding")
{
    sithControl_SetModernControlStyle(true);
    ControlsTestContext testContext;

    JonesControlsScheme scheme{};
    JonesControlsConfig config{};
    config.numSchemes = 1;
    config.aSchemes = &scheme;
    config.selectedShemeIdx = 0;
    testContext.StartControls();

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    JonesUiButtonLayoutModel model;
    model.pConfig = &config;
    model.schemeIndex = 0;
    model.device = JonesUiButtonLayoutDevice::Controller;
    JonesUiButtonLayout_RefreshRows(model);

    // Walk Forward is stick-owned: Bind shows a message, and the South press that follows must
    // not become a binding.
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushGamepadButton(platform::GamepadButton::South);
    testContext.platform.PushKey(platform::Key::Escape);

    REQUIRE(JonesUiButtonLayout_RunScreen(ui, &painter, model) == 1);

    std::size_t controller = 0;
    CHECK_FALSE(JonesUiButtonLayout_GetDeviceBinding(config.aSchemes[0],
        JONESCONTROL_ACTION_WALKFORWARD, JonesUiButtonLayoutDevice::Controller, controller));
    CHECK(model.rows[JONESCONTROL_ACTION_WALKFORWARD].stickOwnership
        == JonesUiStickOwnership::Left);

    sithControl_SetModernControlStyle(true);
}
#endif


TEST_CASE("Back on an unchanged built-in scheme closes without nagging")
{
    sithControl_SetModernControlStyle(true);
    ControlsTestContext testContext;

    JonesControlsConfig config;
    REQUIRE(LoadConfig(config));
    REQUIRE(config.numSchemes > 0);
    config.selectedShemeIdx = 0;
    testContext.StartControls();

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    JonesUiButtonLayoutModel model;
    model.pConfig = &config;
    model.schemeIndex = 0;
    model.device = JonesUiButtonLayoutDevice::Controller;
    JonesUiButtonLayout_RefreshRows(model);

    testContext.platform.PushKey(platform::Key::Escape);
    REQUIRE(JonesUiButtonLayout_RunScreen(ui, &painter, model) == 1);

    CHECK(testContext.platform.MessageCount() == 0);
    CHECK(model.bChanged == false);
    CHECK(model.schemeIndex == 0);
    jonesConfig_FreeControlConfigEntry(&config);
    sithControl_SetModernControlStyle(true);
}


TEST_CASE("the Controls tab keeps its row order after the Controller screen closes")
{
    ControlsTestContext testContext;
    JonesControlsConfig config;
    std::size_t index = 0;
    REQUIRE(LoadSchemeForLayout(testContext, config, index));
    config.selectedShemeIdx = (int)index;
    testContext.StartControls();

    JonesSoundSettings sound{};
    sound.maxSoundVolume = 1.0f;
    JonesUiOptionsData data;
    data.pControlsConfig = &config;
    data.pSoundSettings = &sound;

    JonesUiControlsModel model;
    REQUIRE(JonesUiControls_InitModel(model, config));

    JonesUiControlsLayout layout;
    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);
    REQUIRE(ui.setViewport(1280, 800));

    const babylon::ui::Screen screen =
        JonesUiOptions_BuildTabScreenForTest(1, data, &layout);
    const babylon::ui::ScreenId screenId = ui.push(screen);
    REQUIRE(screenId != babylon::ui::kInvalidScreen);

    const std::vector<std::string> expected = {
        "Scheme", "New", "Delete", "Controller", "Keyboard & Mouse", "Mouse camera",
        "Rumble", "Reset to defaults", "Apply",
    };
    const std::vector<std::string> known = {
        "Scheme", "New", "Delete", "Controller", "Keyboard & Mouse", "Mouse camera",
        "Rumble", "Apply", "Reset to defaults",
    };

    const auto labels = [&]()
    {
        painter.clearRecorded();
        ui.draw();
        std::vector<std::string> found;
        for ( const babylon::ui::RecordedCall& call : painter.recorded() )
        {
            if ( const auto* pText = std::get_if<babylon::ui::RecordedDrawText>(&call) )
            {
                if ( std::find(known.begin(), known.end(), pText->text) != known.end() )
                {
                    found.push_back(pText->text);
                }
            }
        }
        if ( found.size() > expected.size() )
        {
            found.resize(expected.size());
        }
        return found;
    };

    CHECK(labels() == expected);

    // Back from the nested Controller screen runs UpdateScreenWidgets on the same tab.
    testContext.platform.PushKey(platform::Key::Escape);
    REQUIRE(JonesUiControls_HandleAction(ui, screenId, 14, &painter, model, layout));

    CHECK(labels() == expected);
    const std::optional<babylon::ui::WidgetValue> first = ui.widgetValue(screenId,
        layout.scheme);
    REQUIRE(first.has_value());
    CHECK(std::holds_alternative<std::int64_t>(*first));

    JonesUiControls_FreeModel(model);
    jonesConfig_FreeControlConfigEntry(&config);
}


TEST_CASE("reset is enabled for a built-in scheme and removes its stale user copy on OK")
{
    sithControl_SetModernControlStyle(true);
    ControlsTestContext testContext;

    // The stale user copy predates the quick whip: it has no gamepad bindings at all.
    const char* pName = JonesControlSchemes_GetDefaultSchemeName(4);
    REQUIRE(pName != nullptr);
    testContext.WriteSchemeFile(pName, HandWrittenScheme());

    JonesControlsConfig config;
    REQUIRE(LoadConfig(config));
    const std::size_t builtIn = 4;
    REQUIRE(FindScheme(config, pName) == builtIn);
    config.selectedShemeIdx = (int)builtIn;
    testContext.StartControls();

    JonesUiControlsModel model;
    REQUIRE(JonesUiControls_InitModel(model, config));
    REQUIRE(model.selected == builtIn);

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);
    JonesUiControlsLayout layout;
    const babylon::ui::Screen screen = JonesUiControls_BuildOptionsTabScreen(model, layout);
    const babylon::ui::ScreenId screenId = ui.push(screen);
    REQUIRE(screenId != babylon::ui::kInvalidScreen);

    const auto* pReset = std::get_if<babylon::ui::Button>(&screen.widgets[layout.reset]);
    REQUIRE(pReset != nullptr);
    CHECK(pReset->enabled);
    CHECK(pReset->text == "Reset to defaults");

    // Reset, confirm Yes, then OK.
    testContext.platform.PushKey(platform::Key::Enter);
    REQUIRE(JonesUiControls_HandleAction(ui, screenId, 13, &painter, model, layout));
    REQUIRE(JonesUiControls_Apply(model, config) == 1);

    CHECK_FALSE(std::filesystem::exists(testContext.SchemePath(pName)));

    // The built-in now carries the Modern defaults again, including LB on the step-left row.
    std::size_t lb = STDCONTROL_JOYSTICK_GETBUTTON(0, 4);
    bool bFound = false;
    for ( std::size_t cur = 0; cur < JonesControlSchemes_GetNumBindings(
            &config.aSchemes[builtIn], JONESCONTROL_ACTION_STEPLEFT); ++cur )
    {
        std::size_t bound = 0;
        if ( JonesControlSchemes_GetBinding(&config.aSchemes[builtIn],
                JONESCONTROL_ACTION_STEPLEFT, cur, &bound) && bound == lb )
        {
            bFound = true;
        }
    }
    CHECK(bFound);

    // Reloading the active scheme now falls back to the built-in data; LB is quick whip in Modern.
    // The pad must be registered before the bind call, because the binder only binds present pads.
    testContext.platform.PushGamepadConnected();
    static_cast<void>(stdControl_GetNumJoysticks());
    JonesControlsScheme* pActive = jonesConfig_LoadActiveControlScheme();
    REQUIRE(pActive != nullptr);
    sithControl_SetModernGamepadStyle(true);
    REQUIRE(JonesControlSchemes_SetActiveScheme(pActive) == 1);
    jonesConfig_FreeControlScheme(pActive);

    testContext.platform.PushGamepadButtonDown(platform::GamepadButton::LeftShoulder);
    stdControl_ReadControls();
    int pressed = 0;
#ifdef J3D_QOL_IMPROVEMENTS
    // The Modern gamepad maps the step-left row's LB to the quick whip.
    CHECK(sithControl_GetKey(SITHCONTROL_QUICKWHIP, &pressed) != 0);
#else
    // Retail keeps LB as the original held sidestep.
    CHECK(sithControl_GetKey(SITHCONTROL_STPLEFT, &pressed) != 0);
#endif
    CHECK(pressed != 0);
    testContext.platform.PushGamepadButtonUp(platform::GamepadButton::LeftShoulder);
    stdControl_ReadControls();

    jonesConfig_FreeControlConfigEntry(&config);
    sithControl_SetModernControlStyle(true);
}


TEST_CASE("reset on a custom scheme still restores the style defaults")
{
    sithControl_SetModernControlStyle(true);
    ControlsTestContext testContext;
    JonesControlsConfig config;
    std::size_t index = 0;
    REQUIRE(LoadSchemeForLayout(testContext, config, index));
    config.selectedShemeIdx = (int)index;
    testContext.StartControls();

    JonesUiControlsModel model;
    REQUIRE(JonesUiControls_InitModel(model, config));
    model.selected = index;

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);
    JonesUiControlsLayout layout;
    const babylon::ui::Screen screen = JonesUiControls_BuildOptionsTabScreen(model, layout);
    const babylon::ui::ScreenId screenId = ui.push(screen);
    REQUIRE(screenId != babylon::ui::kInvalidScreen);

    testContext.platform.PushKey(platform::Key::Enter);
    REQUIRE(JonesUiControls_HandleAction(ui, screenId, 13, &painter, model, layout));
    REQUIRE(JonesUiControls_Apply(model, config) == 1);

    // Still a custom scheme, now saved with the style defaults.
    JonesControlsScheme defaults{};
    REQUIRE(JonesControlSchemes_SetDefaultScheme(&defaults,
        JonesUiControls_DefaultSchemeIndex()) == 1);
    CHECK(JonesUiControls_SchemeMatchesDefault(config.aSchemes[index],
        JonesUiControls_DefaultSchemeIndex()));
    CHECK(std::filesystem::exists(testContext.SchemePath("ui-layout")));
    jonesConfig_FreeControlConfigEntry(&config);
    sithControl_SetModernControlStyle(true);
}


TEST_CASE("OK on an unchanged built-in scheme writes no user copy")
{
    ControlsTestContext testContext;
    JonesControlsConfig config;
    REQUIRE(LoadConfig(config));
    const char* pName = JonesControlSchemes_GetDefaultSchemeName(4);
    REQUIRE(pName != nullptr);
    const std::size_t builtIn = FindScheme(config, pName);
    REQUIRE(builtIn == 4);
    config.selectedShemeIdx = (int)builtIn;
    testContext.StartControls();

    JonesUiControlsModel model;
    REQUIRE(JonesUiControls_InitModel(model, config));
    model.selected = builtIn;

    REQUIRE(JonesUiControls_Apply(model, config) == 1);
    CHECK_FALSE(std::filesystem::exists(testContext.SchemePath(pName)));
    jonesConfig_FreeControlConfigEntry(&config);
}


TEST_CASE("the Button Layout reset row triggers the existing device reset")
{
    sithControl_SetModernControlStyle(false);
    ControlsTestContext testContext;
    JonesControlsConfig config;
    std::size_t index = 0;
    REQUIRE(LoadSchemeForLayout(testContext, config, index));
    config.selectedShemeIdx = (int)index;
    testContext.StartControls();

    const std::size_t padEast = STDCONTROL_JOYSTICK_GETBUTTON(0, 1);
    REQUIRE(JonesControlSchemes_AddBinding(&config.aSchemes[index], JONESCONTROL_ACTION_JUMP,
        padEast) == 1);

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);
    REQUIRE(ui.setViewport(480, 480));

    JonesUiButtonLayoutModel model;
    model.pConfig = &config;
    model.schemeIndex = index;
    model.device = JonesUiButtonLayoutDevice::Controller;
    JonesUiButtonLayout_RefreshRows(model);

    const babylon::ui::Rect resetRow = JonesUiButtonLayout_ResetRowRectForTest(ui.canvasSize());
    testContext.platform.PushMouseMove(resetRow.x + resetRow.width * 0.5f,
        resetRow.y + resetRow.height * 0.5f);
    testContext.platform.PushMouseButton(platform::MouseButton::Left);
    testContext.platform.PushKey(platform::Key::Enter);   // Yes
    testContext.platform.PushKey(platform::Key::Escape);  // Back

    REQUIRE(JonesUiButtonLayout_RunScreen(ui, &painter, model) == 1);

    // The hand-written scheme had no pad bindings, so the reset row restored Jump's default pad
    // control (and not East).
    std::size_t controller = 0;
    REQUIRE(JonesUiButtonLayout_GetDeviceBinding(config.aSchemes[index],
        JONESCONTROL_ACTION_JUMP, JonesUiButtonLayoutDevice::Controller, controller));

    JonesControlsScheme defaults{};
    REQUIRE(JonesControlSchemes_SetDefaultScheme(&defaults,
        JonesUiControls_DefaultSchemeIndex()) == 1);
    std::size_t expected = 0;
    REQUIRE(JonesUiButtonLayout_GetDeviceBinding(defaults,
        JONESCONTROL_ACTION_JUMP, JonesUiButtonLayoutDevice::Controller, expected));
    CHECK(controller == expected);
    CHECK(controller != padEast);
    jonesConfig_FreeControlConfigEntry(&config);
    sithControl_SetModernControlStyle(true);
}
