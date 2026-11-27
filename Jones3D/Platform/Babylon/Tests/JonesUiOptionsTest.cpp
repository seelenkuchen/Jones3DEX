// Added: the logic tests of the Babylon column's Options screen, its tabs and its back handling.
#include "JonesUiSettingsTestSupport.h"

namespace
{
    using namespace babylon;
    using namespace JonesUiSettingsTest;
}

TEST_CASE("the options tabs expose their rows and Display has no Performance row")
{
    SettingsTestContext testContext;
    ConfigTest config;

    StdDisplayEnvironment env;
    StdDisplayInfo info;
    std::vector<StdVideoMode> aModes;
    BuildDisplayEnv(env, info, aModes);

    JonesDisplaySettings display = DefaultDisplaySettings();
    JonesSoundSettings sound{};
    sound.maxSoundVolume = 1.0f;
    JonesControlsConfig controls{};

    JonesUiOptionsData data;
    data.bEnhancedProfile = true;
    data.pDisplayEnv      = &env;
    data.pDisplaySettings = &display;
    data.pSoundSettings   = &sound;
    data.pControlsConfig  = &controls;

    auto isInteractive = [](const babylon::ui::Widget& widget)
    {
        return std::holds_alternative<babylon::ui::Button>(widget)
            || std::holds_alternative<babylon::ui::Toggle>(widget)
            || std::holds_alternative<babylon::ui::Slider>(widget)
            || std::holds_alternative<babylon::ui::Choice>(widget)
            || std::holds_alternative<babylon::ui::List>(widget)
            || std::holds_alternative<babylon::ui::BindingCapture>(widget)
            || std::holds_alternative<babylon::ui::TextEntry>(widget);
    };

    const char* const aTabs[] = { "Gameplay", "Controls", "Display", "Sound", "Game data" };
    bool bHasPerformance = false;
    for ( std::size_t tab = 0; tab < 5; ++tab )
    {
        const babylon::ui::Screen screen = JonesUiOptions_BuildTabScreenForTest(tab, data);
        REQUIRE(screen.tabs.size() == 5u);
        CHECK(screen.activeTab == tab);
        CHECK(screen.title == "Options");
        for ( std::size_t i = 0; i < screen.tabs.size(); ++i )
        {
            CHECK(screen.tabs[i] == aTabs[i]);
        }

        REQUIRE(screen.help.size() == screen.widgets.size());
        bool bHasApply = false;
        bool bHasCancel = false;
        for ( std::size_t i = 0; i < screen.widgets.size(); ++i )
        {
            if ( isInteractive(screen.widgets[i]) )
            {
                CHECK_MESSAGE(!screen.help[i].empty(), screen.tabs[tab]);
            }
            if ( const babylon::ui::Choice* choice =
                    std::get_if<babylon::ui::Choice>(&screen.widgets[i]) )
            {
                bHasPerformance = bHasPerformance || choice->text == "Performance";
            }
            if ( const babylon::ui::Button* pButton =
                    std::get_if<babylon::ui::Button>(&screen.widgets[i]) )
            {
                if ( pButton->action == 2 )
                {
                    bHasCancel = true;
                }
                if ( pButton->action == 1 )
                {
                    bHasApply = true;
                    CHECK(pButton->text == "Apply");
                }
            }
        }

        // Leaving a tab keeps its changes, so the Cancel row is gone and the accept row is Apply.
        CHECK(bHasApply);
        CHECK_FALSE(bHasCancel);
        CHECK(screen.backResult == 1);
    }

    // Added: the Controls tab has the mouse camera row, which the player could not see before.
    const babylon::ui::Screen controlsTab = JonesUiOptions_BuildTabScreenForTest(1, data);
    bool bHasMouseCamera = false;
    bool bHasController = false;
    bool bHasKeyboard = false;
    for ( const babylon::ui::Widget& widget : controlsTab.widgets )
    {
        if ( const babylon::ui::Toggle* pToggle = std::get_if<babylon::ui::Toggle>(&widget) )
        {
            bHasMouseCamera = bHasMouseCamera || pToggle->text == "Mouse camera";
        }
        if ( const babylon::ui::Button* pButton = std::get_if<babylon::ui::Button>(&widget) )
        {
            bHasController = bHasController || pButton->text == "Controller";
            bHasKeyboard = bHasKeyboard || pButton->text == "Keyboard & Mouse";
        }
    }
    CHECK(bHasMouseCamera);
    CHECK(bHasController);
    CHECK(bHasKeyboard);

    // ADR-0028: the Display tab drops the row, while the stored level is still read and written.
    CHECK_FALSE(bHasPerformance);
    CHECK(bHasPerformance == false);

    // Added: the Control style rows moved to the Controls tab, next to Mouse camera.
    bool bHasKeyboardStyle = false;
    bool bHasGamepadStyle = false;
    for ( std::size_t i = 0; i < controlsTab.widgets.size(); ++i )
    {
        if ( const babylon::ui::Choice* choice =
                std::get_if<babylon::ui::Choice>(&controlsTab.widgets[i]) )
        {
            if ( choice->text == "Keyboard & mouse style" )
            {
                bHasKeyboardStyle = true;
                REQUIRE(choice->options.size() == 2u);
                CHECK(choice->options[0] == "Modern");
                CHECK(choice->options[1] == "Classic");
#ifdef J3D_QOL_IMPROVEMENTS
                CHECK(choice->enabled);
#else
                // The per-device styles are Enhanced-only; the rows are shown but locked.
                CHECK_FALSE(choice->enabled);
#endif
                REQUIRE(controlsTab.help.size() == controlsTab.widgets.size());
                CHECK(!controlsTab.help[i].empty());
            }
            if ( choice->text == "Gamepad style" )
            {
                bHasGamepadStyle = true;
                REQUIRE(choice->options.size() == 2u);
                CHECK(choice->options[0] == "Modern");
                CHECK(choice->options[1] == "Classic");
#ifdef J3D_QOL_IMPROVEMENTS
                CHECK(choice->enabled);
#else
                CHECK_FALSE(choice->enabled);
#endif
                REQUIRE(controlsTab.help.size() == controlsTab.widgets.size());
                CHECK(!controlsTab.help[i].empty());
            }
        }
    }
    CHECK(bHasKeyboardStyle);
    CHECK(bHasGamepadStyle);

    // The Gameplay tab no longer has a Control style row.
    const babylon::ui::Screen gameplay = JonesUiOptions_BuildTabScreenForTest(0, data);
    for ( const babylon::ui::Widget& widget : gameplay.widgets )
    {
        if ( const babylon::ui::Choice* choice = std::get_if<babylon::ui::Choice>(&widget) )
        {
            CHECK(choice->text != "Control style");
        }
    }
}

TEST_CASE("the Game data tab shows the release and folder and disables setup during a level")
{
    JonesUiOptionsData data;
    data.pRelease     = "GOG.com 1.0";
    data.pInstallPath = "/home/player/Jones3DEX/GameData";

    const babylon::ui::Screen screen = JonesUiOptions_BuildTabScreenForTest(4, data);
    REQUIRE(screen.tabs.size() == 5u);
    CHECK(screen.tabs[4] == "Game data");
    CHECK(screen.activeTab == 4u);

    bool bHasRelease = false;
    bool bHasFolder = false;
    bool bHasSetupButton = false;
    for ( std::size_t i = 0; i < screen.widgets.size(); ++i )
    {
        if ( const babylon::ui::Label* pLabel =
                std::get_if<babylon::ui::Label>(&screen.widgets[i]) )
        {
            if ( pLabel->text == "Release" )
            {
                bHasRelease = true;
                CHECK(pLabel->value == "GOG.com 1.0");
            }
            if ( pLabel->text == "Folder" )
            {
                bHasFolder = true;
                CHECK(pLabel->value.find("GameData") != std::string::npos);
            }
        }

        if ( const babylon::ui::Button* pButton =
                std::get_if<babylon::ui::Button>(&screen.widgets[i]) )
        {
            if ( pButton->text == "Run setup again..." )
            {
                bHasSetupButton = true;
                CHECK(pButton->enabled);
                REQUIRE(screen.help.size() == screen.widgets.size());
                CHECK_FALSE(screen.help[i].empty());
            }
        }
    }

    CHECK(bHasRelease);
    CHECK(bHasFolder);
    CHECK(bHasSetupButton);

    data.bLevelRunning = true;
    const babylon::ui::Screen paused = JonesUiOptions_BuildTabScreenForTest(4, data);
    for ( const babylon::ui::Widget& widget : paused.widgets )
    {
        const babylon::ui::Button* pButton = std::get_if<babylon::ui::Button>(&widget);
        if ( pButton && pButton->text == "Run setup again..." )
        {
            CHECK_FALSE(pButton->enabled);
        }
    }
}

TEST_CASE("Q and E switch options tabs and keep a changed value")
{
    SettingsTestContext testContext;
    ConfigTest config;
    SeedDisplayConfig();

    StdDisplayEnvironment env;
    StdDisplayInfo info;
    std::vector<StdVideoMode> aModes;
    BuildDisplayEnv(env, info, aModes);

    JonesDisplaySettings display = DefaultDisplaySettings();
    JonesSoundSettings sound{};
    sound.maxSoundVolume = 1.0f;
    JonesControlsConfig controls{};

    sithVoice_ShowText(0);

    JonesUiOptionsData data;
    data.bEnhancedProfile = true;
    data.pDisplayEnv      = &env;
    data.pDisplaySettings = &display;
    data.pSoundSettings   = &sound;
    data.pControlsConfig  = &controls;

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    // Toggle Show Text, leave the tab twice and come back, then accept from the Gameplay tab.
    testContext.platform.PushKey(platform::Key::Enter); // show text on
    testContext.platform.PushKey(platform::Key::E);     // Controls
    testContext.platform.PushKey(platform::Key::E);     // Display
    testContext.platform.PushKey(platform::Key::Q);     // Controls
    testContext.platform.PushKey(platform::Key::Q);     // Gameplay
    testContext.platform.PushKey(platform::Key::Up);    // wrap to Apply
    testContext.platform.PushKey(platform::Key::Enter);

    REQUIRE(JonesUiOptions_RunOptionsScreen(ui, &painter, data) == 1);
    CHECK(sithVoice_GetShowText() == 1);
    CHECK(stdConfig_GetBool(SITHVOICE_CFG_GAMEPLAY_SHOWTEXT, false));
    CHECK(ui.screenCount() == 0u);
}

#ifdef J3D_QOL_IMPROVEMENTS
TEST_CASE("the Controls tab reads and writes the per-device control styles")
{
    SettingsTestContext testContext;
    ConfigTest config;

    StdDisplayEnvironment env;
    StdDisplayInfo info;
    std::vector<StdVideoMode> aModes;
    BuildDisplayEnv(env, info, aModes);

    JonesDisplaySettings display = DefaultDisplaySettings();
    JonesSoundSettings sound{};
    sound.maxSoundVolume = 1.0f;
    JonesControlsConfig controls{};

    // Start the opposite way so both directions are exercised.
    sithControl_SetModernKeyboardStyle(true);
    sithControl_SetModernGamepadStyle(false);

    JonesUiOptionsData data;
    data.bEnhancedProfile = true;
    data.pDisplayEnv      = &env;
    data.pDisplaySettings = &display;
    data.pSoundSettings   = &sound;
    data.pControlsConfig  = &controls;

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    // Controls tab focus order (the scheme list is empty, so the Controller and Keyboard & Mouse
    // buttons are disabled): Scheme, New, Mouse camera, Keyboard & mouse style, Gamepad style,
    // Rumble, Apply.
    testContext.platform.PushKey(platform::Key::E);     // Controls
    testContext.platform.PushKey(platform::Key::Down);  // New
    testContext.platform.PushKey(platform::Key::Down);  // Mouse camera
    testContext.platform.PushKey(platform::Key::Down);  // Keyboard & mouse style
    testContext.platform.PushKey(platform::Key::Right); // Modern -> Classic
    testContext.platform.PushKey(platform::Key::Down);  // Gamepad style
    testContext.platform.PushKey(platform::Key::Left);  // Classic -> Modern
    testContext.platform.PushKey(platform::Key::Down);  // Rumble
    testContext.platform.PushKey(platform::Key::Down);  // Apply
    testContext.platform.PushKey(platform::Key::Enter);

    REQUIRE(JonesUiOptions_RunOptionsScreen(ui, &painter, data) == 1);
    CHECK_FALSE(sithControl_IsModernKeyboardStyle());
    CHECK(sithControl_IsModernGamepadStyle());

    char aStyle[16] = { 0 };
    CHECK(stdConfig_GetString(SITHCONTROL_CFG_ENGINE_INPUT_KEYBOARDCONTROLSTYLE, aStyle,
        sizeof(aStyle), ""));
    CHECK(std::string(aStyle) == "classic");
    CHECK(stdConfig_GetString(SITHCONTROL_CFG_ENGINE_INPUT_GAMEPADCONTROLSTYLE, aStyle,
        sizeof(aStyle), ""));
    CHECK(std::string(aStyle) == "modern");
    // The old single key is no longer written.
    CHECK_FALSE(stdConfig_Contains(SITHCONTROL_CFG_ENGINE_INPUT_CONTROLSTYLE));
}
#endif

TEST_CASE("the retail profile locks the Controls tab style rows")
{
    SettingsTestContext testContext;
    ConfigTest config;

    StdDisplayEnvironment env;
    StdDisplayInfo info;
    std::vector<StdVideoMode> aModes;
    BuildDisplayEnv(env, info, aModes);

    JonesDisplaySettings display = DefaultDisplaySettings();
    JonesSoundSettings sound{};
    sound.maxSoundVolume = 1.0f;
    JonesControlsConfig controls{};

    JonesUiOptionsData data;
    data.bEnhancedProfile = false;
    data.pDisplayEnv      = &env;
    data.pDisplaySettings = &display;
    data.pSoundSettings   = &sound;
    data.pControlsConfig  = &controls;

    const babylon::ui::Screen controlsTab = JonesUiOptions_BuildTabScreenForTest(1, data);
    bool bKeyboardLocked = false;
    bool bGamepadLocked  = false;
    for ( const babylon::ui::Widget& widget : controlsTab.widgets )
    {
        if ( const babylon::ui::Choice* choice = std::get_if<babylon::ui::Choice>(&widget) )
        {
            if ( choice->text == "Keyboard & mouse style" )
            {
                bKeyboardLocked = !choice->enabled;
            }
            if ( choice->text == "Gamepad style" )
            {
                bGamepadLocked = !choice->enabled;
            }
        }
    }

    CHECK(bKeyboardLocked);
    CHECK(bGamepadLocked);
}

TEST_CASE("the options Display tab still writes the stored performance level unchanged")
{
    SettingsTestContext testContext;
    ConfigTest config;
    SeedDisplayConfig();

    StdDisplayEnvironment env;
    StdDisplayInfo info;
    std::vector<StdVideoMode> aModes;
    BuildDisplayEnv(env, info, aModes);

    JonesDisplaySettings display = DefaultDisplaySettings();
    JonesSoundSettings sound{};
    sound.maxSoundVolume = 1.0f;
    JonesControlsConfig controls{};

    JonesUiOptionsData data;
    data.bEnhancedProfile = true;
    data.pDisplayEnv      = &env;
    data.pDisplaySettings = &display;
    data.pSoundSettings   = &sound;
    data.pControlsConfig  = &controls;

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    // Display tab: Bit depth, Resolution, Windowed, Filter, Fog, Fog density, Hi-poly, Lit faces,
    // OK, Back. Turn windowed on and accept.
    testContext.platform.PushKey(platform::Key::E);
    testContext.platform.PushKey(platform::Key::E);
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter); // windowed on
    testContext.platform.PushKey(platform::Key::Up);
    testContext.platform.PushKey(platform::Key::Up);
    testContext.platform.PushKey(platform::Key::Up);    // wrap to Apply
    testContext.platform.PushKey(platform::Key::Enter);

    REQUIRE(JonesUiOptions_RunOptionsScreen(ui, &painter, data) == 1);
    CHECK(display.bWindowMode == 1);
    CHECK(stdConfig_GetBool(JONESDISPLAY_CFG_GRAPHICS_WINDOW, false));
    CHECK(stdConfig_GetInt(JONESDISPLAY_CFG_GRAPHICS_PERFORMANCELEVEL, -1) == 4);
}

TEST_CASE("leaving the options screen with back applies the changes")
{
    SettingsTestContext testContext;
    ConfigTest config;
    SeedDisplayConfig();

    StdDisplayEnvironment env;
    StdDisplayInfo info;
    std::vector<StdVideoMode> aModes;
    BuildDisplayEnv(env, info, aModes);

    JonesDisplaySettings display = DefaultDisplaySettings();
    JonesSoundSettings sound{};
    sound.maxSoundVolume = 1.0f;
    JonesControlsConfig controls{};

    sithVoice_ShowText(0);

    JonesUiOptionsData data;
    data.bEnhancedProfile = true;
    data.pDisplayEnv      = &env;
    data.pDisplaySettings = &display;
    data.pSoundSettings   = &sound;
    data.pControlsConfig  = &controls;

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    // Toggle Show Text on the Gameplay tab, switch to Display and turn windowed on, then leave
    // with Escape instead of the Apply row.
    testContext.platform.PushKey(platform::Key::Enter); // show text on
    testContext.platform.PushKey(platform::Key::E);     // Controls
    testContext.platform.PushKey(platform::Key::E);     // Display
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter); // windowed on
    testContext.platform.PushKey(platform::Key::Escape);

    REQUIRE(JonesUiOptions_RunOptionsScreen(ui, &painter, data) == 1);
    CHECK(sithVoice_GetShowText() == 1);
    CHECK(stdConfig_GetBool(SITHVOICE_CFG_GAMEPLAY_SHOWTEXT, false));
    CHECK(display.bWindowMode == 1);
    CHECK(stdConfig_GetBool(JONESDISPLAY_CFG_GRAPHICS_WINDOW, false));
    CHECK(ui.screenCount() == 0u);
}
