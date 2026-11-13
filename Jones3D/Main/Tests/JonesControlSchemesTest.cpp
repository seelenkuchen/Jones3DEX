// Added: tests of the Babylon column's control scheme files (M7.17a). They use a real temporary
// directory through the Babylon file system, so no game data or assets are needed. The module is
// C11; this test file is C++ only to construct that file-system context.
#include <unity_fixture.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include <babylon/cvars/Registry.h>
#include <babylon/files/SdlFileSystem.h>
#include <babylon/platform/NullPlatform.h>
#include <babylon/time/NullClock.h>

#include <j3dcore/j3d.h>

#include <Jones3D/Display/JonesHud.h>
#include <Jones3D/Main/JonesMain.h>
#include <Jones3D/Platform/Babylon/JonesControlSchemes.h>
#include <Jones3D/Play/JonesControl.h>
#include <Jones3D/Play/jonesCog.h>
#include <Jones3D/types.h>

#include <sith/Devices/sithControl.h>
#include <sith/Gameplay/sithPlayer.h>
#include <sith/Main/sithMain.h>

#include <std/Babylon/stdBabylon.h>
#include <std/General/std.h>
#include <std/General/stdConfig.h>
#include <std/General/stdControl.h>
#include <std/General/stdJSON.h>
#include <std/General/stdPlatform.h>
#include <std/Null/stdControlNull.h>

TEST_GROUP(JonesControlSchemes);

namespace
{
    bool g_bTestWindowed = false;
    tHostServices g_hostServices;
    babylon::platform::NullPlatform g_platform;
    babylon::time::NullClock g_clock;
    babylon::time::NullClock g_virtualClock;
    babylon::cvars::Registry g_cvars;
    StdBabylonContext g_context;
    std::unique_ptr<babylon::files::FileSystem> g_pFileSystem;

    std::filesystem::path g_tempDir;
    std::filesystem::path g_configPath;
    std::string g_keysetsDir;
    std::string g_installPath;

    int J3DAPI TestPrint(const char* pFormat, ...)
    {
        (void)pFormat;
        return 0;
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

    int J3DAPI ConfigFallbackGetStr(const char* pKey, char* pDstStr, size_t size, const char* pDefaultValue)
    {
        (void)pKey;
        if ( pDstStr && size )
        {
            snprintf(pDstStr, size, "%s", pDefaultValue ? pDefaultValue : "");
        }
        return 0;
    }

    void SetupHostServices()
    {
        std::memset(&g_hostServices, 0, sizeof(g_hostServices));
        stdPlatform_InitServices(&g_hostServices);
        g_hostServices.pStatusPrint  = TestPrint;
        g_hostServices.pMessagePrint = TestPrint;
        g_hostServices.pWarningPrint = TestPrint;
        g_hostServices.pErrorPrint   = TestPrint;
        g_hostServices.pDebugPrint   = TestPrint;

        std_g_pHS  = &g_hostServices;
        sith_g_pHS = &g_hostServices;
    }

    std::string KeySetsPath(const std::string& name)
    {
        return (std::filesystem::path(g_keysetsDir) / (name + ".kfg")).string();
    }

    void WriteFile(const std::string& path, const std::string& text)
    {
        std::filesystem::path parent = std::filesystem::path(path).parent_path();
        std::filesystem::create_directories(parent);
        std::ofstream file(path, std::ios::binary);
        file.write(text.data(), (std::streamsize)text.size());
    }

    std::string ReadFile(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

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

    std::string QuotedPadded(const std::string& value, size_t width)
    {
        std::string text = "\"" + value + "\"";
        if ( text.size() < width )
        {
            text.append(width - text.size(), ' ');
        }
        return text;
    }

    // The exact bytes the original wrote for a scheme whose only binding is Jump = M, with no game
    // string table (so JONES_STR_ keys fall back to the keys themselves). The two widths are the
    // original padding: the cap action names are 24 characters and the longest control is 22, both
    // rounded up to the next multiple of five.
    std::string ExpectedSingleJumpScheme()
    {
        constexpr size_t kActionWidth  = 25;
        constexpr size_t kControlWidth = 25;

        std::string text = "JONES_STR_HEADING\r\n";
        text += "1\r\n\r\n";
        std::string title = "JONES_STR_ACTIONS";
        title.append(kActionWidth - title.size(), ' ');
        text += title + "JONES_STR_KEYS\r\n\r\n";

        for ( size_t i = 0; i < std::size(kActionKeys); ++i )
        {
            text += QuotedPadded(kActionKeys[i], kActionWidth);
            if ( i == JONESCONTROL_ACTION_JUMP )
            {
                text += QuotedPadded("M", kControlWidth);
            }
            text += "\r\n";
        }

        return text;
    }

    // A hand-written scheme: heading, enabled, header, all 37 actions, then the added EXTRAS
    // section. Only the named action has bindings; the rest are empty.
    std::string HandWrittenScheme(const char* pActionKey, const std::string& controls,
        const std::string& extras = std::string())
    {
        std::string text = "scheme heading\r\n1\r\n\r\nACTIONS KEYS\r\n\r\n";
        for ( size_t i = 0; i < std::size(kActionKeys); ++i )
        {
            text += "\"";
            text += kActionKeys[i];
            text += "\"";
            if ( kActionKeys[i] == std::string(pActionKey) && !controls.empty() )
            {
                text += " ";
                text += controls;
            }
            text += "\r\n";
        }

        if ( !extras.empty() )
        {
            text += "EXTRAS\r\n";
            text += extras;
        }

        return text;
    }

    void ResetModule()
    {
        if ( sithControl_IsOpen() )
        {
            sithControl_Close();
        }
        jonesConfig_Shutdown();
        sithControl_Shutdown();
        sithControl_ResetGlobals();
        sithPlayer_g_pLocalPlayerThing = NULL;
        JonesHud_ResetGlobals();
        size_t aNoKeys[1] = { 0 };
        JonesMain_BindToggleMenuControlKeys(aNoKeys, 0);
    }

    void StartControls()
    {
        TEST_ASSERT_EQUAL_INT(0, sithControl_Startup(0));
        TEST_ASSERT_EQUAL_INT(1, sithControl_Open());
    }

    int ReadKeyState(SithControlFunction function)
    {
        int pressed = 0;
        return sithControl_GetKey(function, &pressed) != 0;
    }
}

extern "C" bool JonesBabylon_HasWindow(void)
{
    // The test controls the windowed/headless answer instead of building a real window.
    return g_bTestWindowed;
}

TEST_SETUP(JonesControlSchemes)
{
    g_bTestWindowed = false;

    std::error_code error;
    g_tempDir = std::filesystem::temp_directory_path(error);
    if ( error )
    {
        FAIL("no temporary directory");
    }

    g_tempDir /= "jones3d-control-schemes-" + std::to_string(std::rand());
    std::filesystem::remove_all(g_tempDir, error);
    std::filesystem::create_directories(g_tempDir, error);

    g_installPath = g_tempDir.string();
    g_keysetsDir  = (g_tempDir / "keysets").string();
    g_configPath  = g_tempDir / "Jones.cfg";

    g_pFileSystem = babylon::files::createSdlFileSystem();
    std::memset(&g_context, 0, sizeof(g_context));
    g_context.pClock          = &g_clock;
    g_context.pVirtualClock   = &g_virtualClock;
    g_context.pFileSystem     = g_pFileSystem.get();
    g_context.pPlatform       = &g_platform;
    g_context.pScriptPlatform = &g_platform;
    g_context.pWindow         = nullptr;
    g_context.pCvars          = &g_cvars;
    g_context.pRenderDevice   = nullptr;
    g_context.pAudioDevice    = nullptr;

    stdBabylon_SetContext(&g_context);
    SetupHostServices();

    const StdConfigFallback fallback = {
        ConfigFallbackHasStarted, ConfigFallbackGetInt, ConfigFallbackGetFloat, ConfigFallbackGetStr,
    };
    stdConfig_SetFallback(&fallback);

    stdStartup(&g_hostServices);
    TEST_ASSERT_TRUE(stdConfig_Startup(g_configPath.string().c_str()));
    TEST_ASSERT_TRUE(stdConfig_SetString(SITH_CFG_INSTALLPATH, g_installPath.c_str()));
    TEST_ASSERT_TRUE(stdConfig_SetBool(JONESCONTROL_CFG_CONTROLS_MOUSE, true));
    TEST_ASSERT_TRUE(stdConfig_SetBool(JONESCONTROL_CFG_CONTROLS_CONTROLLER, false));

    std::filesystem::create_directories(g_keysetsDir, error);
    ResetModule();
}

TEST_TEAR_DOWN(JonesControlSchemes)
{
    ResetModule();

    if ( stdConfig_HasStarted() )
    {
        stdConfig_Shutdown();
    }
    stdShutdown();

    stdBabylon_SetContext(nullptr);
    std_g_pHS  = nullptr;
    sith_g_pHS = nullptr;
    g_pFileSystem.reset();

    std::error_code error;
    std::filesystem::remove_all(g_tempDir, error);
}

TEST(JonesControlSchemes, TestLoadSchemeWithKeysMouseJoystick)
{
    WriteFile(KeySetsPath("mixed"), HandWrittenScheme("JONES_STR_JUMP",
        "\"M\" \"MOUSE2\" \"BUTTON3\" \"HAT1 UP\" \"TWISTR\" \"RX+\" \"Z-\""));

    JonesControlsScheme scheme;
    TEST_ASSERT_EQUAL_INT(1, jonesConfig_LoadControlScheme(KeySetsPath("mixed").c_str(), &scheme));
    TEST_ASSERT_EQUAL_STRING("mixed", scheme.aName);

    TEST_ASSERT_EQUAL_size_t(7, JONESCONTROL_ACTION_GETNUMBINDS(scheme.aActions[JONESCONTROL_ACTION_JUMP]));
    TEST_ASSERT_EQUAL_size_t(1, JONESCONTROL_ACTION_GETNUMKEYBOARDBINDS(scheme.aActions[JONESCONTROL_ACTION_JUMP]));
    TEST_ASSERT_EQUAL_size_t(6, JONESCONTROL_ACTION_GETNUMCONTROLLERBINDS(scheme.aActions[JONESCONTROL_ACTION_JUMP]));

    TEST_ASSERT_EQUAL_size_t(STDKEY_M, scheme.aActions[JONESCONTROL_ACTION_JUMP][1]);
    TEST_ASSERT_EQUAL_size_t(STDCONTROL_KID_MOUSE_RBUTTON, scheme.aActions[JONESCONTROL_ACTION_JUMP][2]);
    TEST_ASSERT_EQUAL_size_t(STDCONTROL_JOYSTICK_GETBUTTON(0, 2), scheme.aActions[JONESCONTROL_ACTION_JUMP][3]);
    TEST_ASSERT_EQUAL_size_t(STDCONTROL_JOYSTICK_GETPOVUP(0, 0), scheme.aActions[JONESCONTROL_ACTION_JUMP][4]);
    TEST_ASSERT_EQUAL_size_t(JONESCONFIG_JOYAXISKID_TWISTR, scheme.aActions[JONESCONTROL_ACTION_JUMP][5]);
    TEST_ASSERT_EQUAL_size_t(JONESCONTROLSCHEMES_JOYAXIS_RX_POS, scheme.aActions[JONESCONTROL_ACTION_JUMP][6]);
    TEST_ASSERT_EQUAL_size_t(JONESCONFIG_JOYAXISKID_AIR, scheme.aActions[JONESCONTROL_ACTION_JUMP][7]);

    TEST_ASSERT_EQUAL_size_t(0, JONESCONTROL_ACTION_GETNUMBINDS(scheme.aActions[JONESCONTROL_ACTION_WALKFORWARD]));
}

TEST(JonesControlSchemes, TestRejectMalformedSchemes)
{
    JonesControlsScheme scheme;

    WriteFile(KeySetsPath("disabled"), std::string("heading\r\n0\r\n\r\nACTIONS KEYS\r\n\r\n"));
    TEST_ASSERT_EQUAL_INT(0, jonesConfig_LoadControlScheme(KeySetsPath("disabled").c_str(), &scheme));

    std::string unknownAction = HandWrittenScheme("JONES_STR_JUMP", "\"M\"");
    size_t pos = unknownAction.find("JONES_STR_JUMP");
    unknownAction.replace(pos, strlen("JONES_STR_JUMP"), "JONES_STR_NOPE");
    WriteFile(KeySetsPath("unknown"), unknownAction);
    TEST_ASSERT_EQUAL_INT(0, jonesConfig_LoadControlScheme(KeySetsPath("unknown").c_str(), &scheme));

    WriteFile(KeySetsPath("overflow"), HandWrittenScheme("JONES_STR_JUMP",
        "\"M\" \"N\" \"B\" \"V\" \"C\" \"X\" \"Z\" \"Q\" \"W\""));
    TEST_ASSERT_EQUAL_INT(0, jonesConfig_LoadControlScheme(KeySetsPath("overflow").c_str(), &scheme));

    WriteFile(KeySetsPath("badcontrol"), HandWrittenScheme("JONES_STR_JUMP", "\"NOT-A-CONTROL\""));
    TEST_ASSERT_EQUAL_INT(0, jonesConfig_LoadControlScheme(KeySetsPath("badcontrol").c_str(), &scheme));

    // Fewer than the 37 action lines.
    WriteFile(KeySetsPath("short"), std::string("heading\r\n1\r\n\r\nACTIONS KEYS\r\n\r\n\"JONES_STR_WLKFWD\"\r\n"));
    TEST_ASSERT_EQUAL_INT(0, jonesConfig_LoadControlScheme(KeySetsPath("short").c_str(), &scheme));
}

TEST(JonesControlSchemes, TestWriteSchemeMatchesExpectedBytes)
{
    JonesControlsScheme scheme;
    TEST_ASSERT_EQUAL_INT(1, JonesControlSchemes_CreateScheme("expected", &scheme));
    memset(scheme.aActions, 0, sizeof(scheme.aActions));
    TEST_ASSERT_EQUAL_INT(1, JonesControlSchemes_AddBinding(&scheme, JONESCONTROL_ACTION_JUMP, STDKEY_M));

    TEST_ASSERT_EQUAL_INT(1, jonesConfig_ControlOptions_WriteScheme(&scheme));

    std::string expected = ExpectedSingleJumpScheme();
    std::string actual   = ReadFile(KeySetsPath("expected"));
    TEST_ASSERT_EQUAL_size_t(expected.size(), actual.size());
    TEST_ASSERT_EQUAL_STRING(expected.c_str(), actual.c_str());
}

TEST(JonesControlSchemes, TestRoundTripKeepsExtraBindings)
{
    JonesControlsScheme before;
    TEST_ASSERT_EQUAL_INT(1, JonesControlSchemes_CreateScheme("roundtrip", &before));
    memset(before.aActions, 0, sizeof(before.aActions));
    TEST_ASSERT_EQUAL_INT(1, JonesControlSchemes_AddBinding(&before, JONESCONTROL_ACTION_JUMP, STDKEY_M));
    TEST_ASSERT_EQUAL_INT(1, JonesControlSchemes_AddBinding(&before, JONESCONTROL_ACTION_JUMP, STDCONTROL_KID_MOUSE_LBUTTON));
    TEST_ASSERT_EQUAL_INT(1, JonesControlSchemes_AddBinding(&before, JONESCONTROL_ACTION_JUMP, JONESCONTROLSCHEMES_JOYAXIS_RY_NEG));
    TEST_ASSERT_EQUAL_INT(1, JonesControlSchemes_AddBinding(&before, JONESCONTROL_ACTION_JUMP, JONESCONFIG_JOYAXISKID_AIR));
    TEST_ASSERT_EQUAL_INT(1, JonesControlSchemes_AddBinding(&before, JONESCONTROL_ACTION_JUMP, STDCONTROL_JOYSTICK_GETBUTTON(0, 3)));

    TEST_ASSERT_EQUAL_INT(1, JonesControlSchemes_SaveScheme(&before));

    JonesControlsScheme after;
    TEST_ASSERT_EQUAL_INT(1, JonesControlSchemes_LoadScheme(KeySetsPath("roundtrip").c_str(), &after));

    TEST_ASSERT_EQUAL_size_t(JONESCONTROL_ACTION_GETNUMBINDS(before.aActions[JONESCONTROL_ACTION_JUMP]),
        JONESCONTROL_ACTION_GETNUMBINDS(after.aActions[JONESCONTROL_ACTION_JUMP]));

    TEST_ASSERT_EQUAL_size_t(5, JONESCONTROL_ACTION_GETNUMBINDS(after.aActions[JONESCONTROL_ACTION_JUMP]));

    // The original action lines hold the keyboard and joystick bindings; the added EXTRAS section
    // holds the mouse and the axes it cannot name, so their relative order may change.
    const size_t aExpected[] =
    {
        STDKEY_M,
        STDCONTROL_KID_MOUSE_LBUTTON,
        JONESCONTROLSCHEMES_JOYAXIS_RY_NEG,
        JONESCONFIG_JOYAXISKID_AIR,
        STDCONTROL_JOYSTICK_GETBUTTON(0, 3)
    };
    for ( size_t expectedIndex = 0; expectedIndex < std::size(aExpected); ++expectedIndex )
    {
        bool bFound = false;
        for ( size_t bind = 1; bind <= 5; ++bind )
        {
            bFound = bFound || after.aActions[JONESCONTROL_ACTION_JUMP][bind] == aExpected[expectedIndex];
        }
        TEST_ASSERT_TRUE(bFound);
    }
}

TEST(JonesControlSchemes, TestDefaultSchemes)
{
    TEST_ASSERT_EQUAL_size_t(3, JonesControlSchemes_GetNumDefaultSchemes());

    JonesControlsScheme scheme;
    TEST_ASSERT_EQUAL_INT(1, JonesControlSchemes_SetDefaultScheme(&scheme, 0));
    TEST_ASSERT_EQUAL_STRING("JONES_STR_DFLTKEY", scheme.aName);
    TEST_ASSERT_EQUAL_size_t(2, JONESCONTROL_ACTION_GETNUMBINDS(scheme.aActions[JONESCONTROL_ACTION_WALKFORWARD]));
    TEST_ASSERT_EQUAL_size_t(STDKEY_UP, scheme.aActions[JONESCONTROL_ACTION_WALKFORWARD][1]);
    TEST_ASSERT_EQUAL_size_t(JONESCONFIG_JOYAXISKID_UP, scheme.aActions[JONESCONTROL_ACTION_WALKFORWARD][2]);

    TEST_ASSERT_EQUAL_INT(1, JonesControlSchemes_SetDefaultScheme(&scheme, 1));
    TEST_ASSERT_EQUAL_STRING("JONES_STR_DFLT2A4B", scheme.aName);
    TEST_ASSERT_EQUAL_size_t(2, JONESCONTROL_ACTION_GETNUMBINDS(scheme.aActions[JONESCONTROL_ACTION_CRAWL]));

    TEST_ASSERT_EQUAL_INT(1, JonesControlSchemes_SetDefaultScheme(&scheme, 2));
    TEST_ASSERT_EQUAL_STRING("JONES_STR_DFLT2A8B", scheme.aName);
    TEST_ASSERT_EQUAL_size_t(2, JONESCONTROL_ACTION_GETNUMBINDS(scheme.aActions[JONESCONTROL_ACTION_ROLL]));
}

TEST(JonesControlSchemes, TestControlNamesRoundTrip)
{
    struct
    {
        size_t controlId;
        const char* pName;
    } aCases[] =
    {
        { STDKEY_M, "M" },
        { STDKEY_F1, "JONES_STR_F1" },
        { STDCONTROL_KID_MOUSE_RBUTTON, "MOUSE2" },
        { STDCONTROL_JOYSTICK_GETBUTTON(0, 2), "BUTTON3" },
        { STDCONTROL_JOYSTICK_GETPOVUP(0, 0), "HAT1 UP" },
        { JONESCONFIG_JOYAXISKID_UP, "FORWARD" },
        { JONESCONFIG_JOYAXISKID_TWISTR, "TWISTR" },
        { JONESCONFIG_JOYAXISKID_AIR, "Z-" },
        { JONESCONTROLSCHEMES_JOYAXIS_RX_POS, "RX+" },
        { JONESCONTROLSCHEMES_JOYAXIS_RY_NEG, "RY-" },
    };

    for ( size_t i = 0; i < std::size(aCases); ++i )
    {
        char aName[JONESCONFIG_CONTROLSTRING_MAXLEN] = { 0 };
        jonesConfig_ControlToString(aCases[i].controlId, aName);
        TEST_ASSERT_EQUAL_STRING(aCases[i].pName, aName);

        size_t parsed = 0;
        TEST_ASSERT_EQUAL_INT(1, JonesControlSchemes_ParseControlName(aName, &parsed));
        TEST_ASSERT_EQUAL_size_t(aCases[i].controlId, parsed);
    }
}

TEST(JonesControlSchemes, TestWindowedStartupLoadsAndBinds)
{
    g_bTestWindowed = true;
    WriteFile(KeySetsPath("startup"), HandWrittenScheme("JONES_STR_JUMP", "\"M\"",
        "\"JONES_STR_INTERFACE\" \"J\"\r\n"));

    TEST_ASSERT_TRUE(stdConfig_SetString(JONESCONFIG_CFG_CONTROLS_CONFIGFILE, "startup"));

    StartControls();
    TEST_ASSERT_EQUAL_INT(1, jonesConfig_Startup());
    JonesControlSchemes_BindActive();

    const JonesControlsScheme* pActive = JonesControlSchemes_GetActiveScheme();
    TEST_ASSERT_NOT_NULL(pActive);
    TEST_ASSERT_EQUAL_STRING("startup", pActive->aName);

    TEST_ASSERT_TRUE(stdControl_QueueKeyEvent(STDKEY_M, 1));
    stdControl_ReadControls();
    TEST_ASSERT_EQUAL_INT(1, ReadKeyState(SITHCONTROL_JUMP));

    size_t aToggleKeys[JONESCONTROL_ACTION_MAXBINDS] = { 0 };
    size_t numToggleKeys = JonesMain_GetToggleMenuControlKeys(aToggleKeys, std::size(aToggleKeys));
    TEST_ASSERT_EQUAL_size_t(1, numToggleKeys);
    TEST_ASSERT_EQUAL_size_t(STDKEY_J, aToggleKeys[0]);
}

TEST(JonesControlSchemes, TestWindowedFirstRunPicksDefaultAfterControlStartup)
{
    g_bTestWindowed = true;
    StartControls();

    // No controls.configFile: jonesConfig_Startup waits for the gamepad enumeration.
    TEST_ASSERT_EQUAL_INT(1, jonesConfig_Startup());
    TEST_ASSERT_NULL(JonesControlSchemes_GetActiveScheme());

    JonesControlSchemes_BindActive();
    TEST_ASSERT_NOT_NULL(JonesControlSchemes_GetActiveScheme());

    char aConfigured[128] = { 0 };
    TEST_ASSERT_TRUE(stdConfig_GetString(JONESCONFIG_CFG_CONTROLS_CONFIGFILE, aConfigured,
        sizeof(aConfigured), ""));
    TEST_ASSERT_EQUAL_STRING(JonesControlSchemes_GetDefaultSchemeName(0), aConfigured);
}

TEST(JonesControlSchemes, TestHeadlessStartupDoesNothing)
{
    g_bTestWindowed = false;
    WriteFile(KeySetsPath("headless"), HandWrittenScheme("JONES_STR_JUMP", "\"M\""));

    TEST_ASSERT_TRUE(stdConfig_SetString(JONESCONFIG_CFG_CONTROLS_CONFIGFILE, "headless"));

    TEST_ASSERT_NULL(JonesControlSchemes_GetActiveScheme());
    TEST_ASSERT_EQUAL_STRING("", JonesControlSchemes_GetDirectory());
    TEST_ASSERT_EQUAL_INT(1, jonesConfig_Startup());
    TEST_ASSERT_NULL(JonesControlSchemes_GetActiveScheme());
}

TEST(JonesControlSchemes, TestHudMenuFallbackWhenSchemeBindsNothing)
{
    StartControls();

    // An active scheme that deliberately binds no toggle-menu action.
    JonesControlsScheme scheme;
    TEST_ASSERT_EQUAL_INT(1, JonesControlSchemes_SetDefaultScheme(&scheme, 0));
    memset(scheme.aActions[JONESCONTROL_ACTION_INTERFACE], 0,
        sizeof(scheme.aActions[JONESCONTROL_ACTION_INTERFACE]));
    TEST_ASSERT_EQUAL_INT(1, JonesControlSchemes_SetActiveScheme(&scheme));

    size_t aNone[1] = { 0 };
    JonesMain_BindToggleMenuControlKeys(aNone, 0);
    JonesHud_ResetGlobals();
    JonesHud_EnableMenu(1);
    jonesCog_g_bMenuVisible = 0;

    TEST_ASSERT_TRUE(stdControl_QueueKeyEvent(STDKEY_TAB, 1));
    stdControl_ReadControls();
    JonesMain_ProcessMenu();

    TEST_ASSERT_EQUAL_INT(1, jonesCog_g_bMenuVisible);
}

TEST(JonesControlSchemes, TestHudMenuUsesSchemeBinds)
{
    StartControls();

    JonesControlsScheme scheme;
    TEST_ASSERT_EQUAL_INT(1, JonesControlSchemes_SetDefaultScheme(&scheme, 0));
    memset(scheme.aActions[JONESCONTROL_ACTION_INTERFACE], 0,
        sizeof(scheme.aActions[JONESCONTROL_ACTION_INTERFACE]));
    TEST_ASSERT_EQUAL_INT(1, JonesControlSchemes_AddBinding(&scheme,
        JONESCONTROL_ACTION_INTERFACE, STDKEY_J));
    TEST_ASSERT_EQUAL_INT(1, JonesControlSchemes_SetActiveScheme(&scheme));

    size_t aToggle[] = { STDKEY_J };
    JonesMain_BindToggleMenuControlKeys(aToggle, std::size(aToggle));
    JonesHud_ResetGlobals();
    JonesHud_EnableMenu(1);
    jonesCog_g_bMenuVisible = 0;

    // Tab is not a fallback while the scheme binds a toggle key.
    TEST_ASSERT_TRUE(stdControl_QueueKeyEvent(STDKEY_TAB, 1));
    stdControl_ReadControls();
    JonesMain_ProcessMenu();
    TEST_ASSERT_EQUAL_INT(0, jonesCog_g_bMenuVisible);

    TEST_ASSERT_TRUE(stdControl_QueueKeyEvent(STDKEY_TAB, 0));
    TEST_ASSERT_TRUE(stdControl_QueueKeyEvent(STDKEY_J, 1));
    stdControl_ReadControls();
    JonesMain_ProcessMenu();
    TEST_ASSERT_EQUAL_INT(1, jonesCog_g_bMenuVisible);
}

TEST(JonesControlSchemes, TestHudSystemIconsDefaultHidesThem)
{
    // A config without the key takes the default and writes it back as false.
    TEST_ASSERT_FALSE(stdConfig_Contains(JONESHUD_CFG_HUD_SYSTEMICONS));
    TEST_ASSERT_FALSE(JonesHud_LoadSystemIconsConfig());
    TEST_ASSERT_TRUE(stdConfig_Contains(JONESHUD_CFG_HUD_SYSTEMICONS));
    TEST_ASSERT_FALSE(stdConfig_GetBool(JONESHUD_CFG_HUD_SYSTEMICONS, true));
    TEST_ASSERT_EQUAL_INT(-1, JonesHud_SystemMenuItemID(JONESHUD_MENU_IQ));
}

TEST(JonesControlSchemes, TestHudSystemIconsTrueShowsThem)
{
    TEST_ASSERT_TRUE(stdConfig_SetBool(JONESHUD_CFG_HUD_SYSTEMICONS, true));
    TEST_ASSERT_TRUE(JonesHud_LoadSystemIconsConfig());
    TEST_ASSERT_TRUE(stdConfig_GetBool(JONESHUD_CFG_HUD_SYSTEMICONS, false));
    TEST_ASSERT_EQUAL_INT(JONESHUD_MENU_IQ, JonesHud_SystemMenuItemID(JONESHUD_MENU_IQ));
}

TEST(JonesControlSchemes, TestChangeBindingUpdatesCounts)
{
    JonesControlsScheme scheme;
    TEST_ASSERT_EQUAL_INT(1, JonesControlSchemes_CreateScheme("change", &scheme));
    memset(scheme.aActions, 0, sizeof(scheme.aActions));
    TEST_ASSERT_EQUAL_INT(1, JonesControlSchemes_AddBinding(&scheme, JONESCONTROL_ACTION_JUMP, STDKEY_M));
    TEST_ASSERT_EQUAL_INT(1, JonesControlSchemes_AddBinding(&scheme, JONESCONTROL_ACTION_JUMP, STDKEY_N));

    TEST_ASSERT_EQUAL_size_t(2, JonesControlSchemes_GetNumBindings(&scheme, JONESCONTROL_ACTION_JUMP));
    TEST_ASSERT_EQUAL_INT(1, JonesControlSchemes_SetBinding(&scheme, JONESCONTROL_ACTION_JUMP, 0, STDCONTROL_KID_MOUSE_LBUTTON));
    TEST_ASSERT_EQUAL_size_t(1, JONESCONTROL_ACTION_GETNUMKEYBOARDBINDS(scheme.aActions[JONESCONTROL_ACTION_JUMP]));
    TEST_ASSERT_EQUAL_size_t(1, JONESCONTROL_ACTION_GETNUMCONTROLLERBINDS(scheme.aActions[JONESCONTROL_ACTION_JUMP]));

    size_t controlId = 0;
    TEST_ASSERT_EQUAL_INT(1, JonesControlSchemes_GetBinding(&scheme, JONESCONTROL_ACTION_JUMP, 0, &controlId));
    TEST_ASSERT_EQUAL_size_t(STDCONTROL_KID_MOUSE_LBUTTON, controlId);

    TEST_ASSERT_EQUAL_INT(1, JonesControlSchemes_RemoveBinding(&scheme, JONESCONTROL_ACTION_JUMP, 0));
    TEST_ASSERT_EQUAL_size_t(1, JonesControlSchemes_GetNumBindings(&scheme, JONESCONTROL_ACTION_JUMP));
    TEST_ASSERT_EQUAL_INT(0, JonesControlSchemes_GetBinding(&scheme, JONESCONTROL_ACTION_JUMP, 1, &controlId));
}

TEST(JonesControlSchemes, TestCreateDeleteAndLoadAll)
{
    WriteFile(KeySetsPath("one"), HandWrittenScheme("JONES_STR_JUMP", "\"M\""));
    WriteFile(KeySetsPath("two"), HandWrittenScheme("JONES_STR_JUMP", "\"N\""));

    JonesControlsConfig config;
    TEST_ASSERT_EQUAL_INT(1, JonesControlSchemes_LoadAllSchemes(&config));
    TEST_ASSERT_EQUAL_size_t(5, config.numSchemes); // 3 defaults + 2 files

    bool bFoundOne = false;
    bool bFoundTwo = false;
    for ( size_t i = 0; i < config.numSchemes; ++i )
    {
        bFoundOne = bFoundOne || std::strcmp(config.aSchemes[i].aName, "one") == 0;
        bFoundTwo = bFoundTwo || std::strcmp(config.aSchemes[i].aName, "two") == 0;
    }
    TEST_ASSERT_TRUE(bFoundOne);
    TEST_ASSERT_TRUE(bFoundTwo);
    jonesConfig_FreeControlConfigEntry(&config);

    JonesControlsScheme scheme;
    TEST_ASSERT_EQUAL_INT(1, JonesControlSchemes_CreateScheme("fresh", &scheme));
    TEST_ASSERT_EQUAL_INT(1, JonesControlSchemes_SaveScheme(&scheme));
    TEST_ASSERT_TRUE(std::filesystem::exists(KeySetsPath("fresh")));

    TEST_ASSERT_EQUAL_INT(1, JonesControlSchemes_DeleteScheme("fresh"));
    TEST_ASSERT_FALSE(std::filesystem::exists(KeySetsPath("fresh")));
}

static void JonesControlSchemes_RunAllTests(void)
{
    RUN_TEST_CASE(JonesControlSchemes, TestLoadSchemeWithKeysMouseJoystick);
    RUN_TEST_CASE(JonesControlSchemes, TestRejectMalformedSchemes);
    RUN_TEST_CASE(JonesControlSchemes, TestWriteSchemeMatchesExpectedBytes);
    RUN_TEST_CASE(JonesControlSchemes, TestRoundTripKeepsExtraBindings);
    RUN_TEST_CASE(JonesControlSchemes, TestDefaultSchemes);
    RUN_TEST_CASE(JonesControlSchemes, TestControlNamesRoundTrip);
    RUN_TEST_CASE(JonesControlSchemes, TestWindowedStartupLoadsAndBinds);
    RUN_TEST_CASE(JonesControlSchemes, TestWindowedFirstRunPicksDefaultAfterControlStartup);
    RUN_TEST_CASE(JonesControlSchemes, TestHeadlessStartupDoesNothing);
    RUN_TEST_CASE(JonesControlSchemes, TestHudMenuFallbackWhenSchemeBindsNothing);
    RUN_TEST_CASE(JonesControlSchemes, TestHudMenuUsesSchemeBinds);
    RUN_TEST_CASE(JonesControlSchemes, TestHudSystemIconsDefaultHidesThem);
    RUN_TEST_CASE(JonesControlSchemes, TestHudSystemIconsTrueShowsThem);
    RUN_TEST_CASE(JonesControlSchemes, TestChangeBindingUpdatesCounts);
    RUN_TEST_CASE(JonesControlSchemes, TestCreateDeleteAndLoadAll);
}

extern "C" void TEST_JonesControlSchemes_GROUP_RUNNER(void)
{
    JonesControlSchemes_RunAllTests();
}
