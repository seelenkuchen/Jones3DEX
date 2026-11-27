// Added: the logic tests of the Babylon column's Gameplay settings screen and the headless settings wrappers.
#include "JonesUiSettingsTestSupport.h"

namespace
{
    using namespace babylon;
    using namespace JonesUiSettingsTest;
}

TEST_CASE("loading a config without the interaction prompt key leaves it absent")
{
    SettingsTestContext testContext;
    ConfigTest config;

    CHECK_FALSE(stdConfig_Contains(JONESHUD_CFG_HUD_INTERACTIONPROMPTS));
    CHECK(JonesHud_LoadInteractionPromptsConfig() == J3D_QOL_VALUE(true, false));
    CHECK_FALSE(stdConfig_Contains(JONESHUD_CFG_HUD_INTERACTIONPROMPTS));

    // A normal run shutdown writes the config; a missing key must stay missing.
    stdConfig_Shutdown();
    stdJSON_Shutdown();
    REQUIRE(stdJSON_Startup());
    REQUIRE(stdConfig_Startup(kConfigPath));
    CHECK_FALSE(stdConfig_Contains(JONESHUD_CFG_HUD_INTERACTIONPROMPTS));
}

TEST_CASE("quick whip reads its config key")
{
    SettingsTestContext testContext;
    ConfigTest config;

    CHECK_FALSE(stdConfig_Contains(SITHWEAPON_CFG_GAMEPLAY_QUICKWHIP));
    sithWeapon_Open();
    CHECK(sithWeapon_IsQuickWhipEnabled() == J3D_QOL_VALUE(true, false));
    CHECK(stdConfig_Contains(SITHWEAPON_CFG_GAMEPLAY_QUICKWHIP));
    CHECK(stdConfig_GetBool(SITHWEAPON_CFG_GAMEPLAY_QUICKWHIP, false)
        == (J3D_QOL_VALUE(true, false) != 0));

    REQUIRE(stdConfig_SetBool(SITHWEAPON_CFG_GAMEPLAY_QUICKWHIP, false));
    sithWeapon_Open();
    CHECK_FALSE(sithWeapon_IsQuickWhipEnabled());

    REQUIRE(stdConfig_SetBool(SITHWEAPON_CFG_GAMEPLAY_QUICKWHIP, true));
    sithWeapon_Open();
    CHECK(sithWeapon_IsQuickWhipEnabled() == (J3D_QOL_VALUE(true, false) != 0));
}

TEST_CASE("the simulation tick row is an Enhanced toggle that starts on")
{
    SettingsTestContext testContext;
    ConfigTest config;

    // A missing key is on in an Enhanced build (50 Hz) and always off in the Retail profile.
    // A QOL-off build has no Enhanced default, so the key missing stays Off there too.
#ifdef J3D_QOL_IMPROVEMENTS
    CHECK(JonesUiGameplay_LoadValues(true).bFixedTick);
#else
    CHECK_FALSE(JonesUiGameplay_LoadValues(true).bFixedTick);
#endif
    CHECK_FALSE(JonesUiGameplay_LoadValues(false).bFixedTick);

    REQUIRE(stdConfig_SetInt(JONESFIXEDTICK_CFG_ENGINE_SIM_FIXEDTICK, 0));
    CHECK_FALSE(JonesUiGameplay_LoadValues(true).bFixedTick);

    REQUIRE(stdConfig_SetInt(JONESFIXEDTICK_CFG_ENGINE_SIM_FIXEDTICK, 50));
    CHECK(JonesUiGameplay_LoadValues(true).bFixedTick);

    // An old 30/60/120 from an earlier build reads as on.
    REQUIRE(stdConfig_SetInt(JONESFIXEDTICK_CFG_ENGINE_SIM_FIXEDTICK, 60));
    CHECK(JonesUiGameplay_LoadValues(true).bFixedTick);

    const JonesUiGameplayValues values;
    JonesUiGameplayLayout layout;
    const babylon::ui::Screen screen = JonesUiGameplay_BuildScreen(true, values, layout);
    REQUIRE(layout.fixedTick < screen.widgets.size());
    const babylon::ui::Toggle* pToggle =
        std::get_if<babylon::ui::Toggle>(&screen.widgets[layout.fixedTick]);
    REQUIRE(pToggle != nullptr);
    CHECK(pToggle->text == "Simulation tick");
    CHECK(pToggle->enabled);

    JonesUiGameplayLayout retailLayout;
    const babylon::ui::Screen retail =
        JonesUiGameplay_BuildScreen(false, values, retailLayout);
    REQUIRE(retailLayout.fixedTick < retail.widgets.size());
    const babylon::ui::Toggle* pRetailToggle =
        std::get_if<babylon::ui::Toggle>(&retail.widgets[retailLayout.fixedTick]);
    REQUIRE(pRetailToggle != nullptr);
    CHECK_FALSE(pRetailToggle->enabled);
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
    sithControl_SetModernControlStyle(true);
    JonesReticle_Enable(false);
    sithWeapon_EnableBloodSplatter(false);
    sithWeapon_SetQuickWhipEnabled(true);

    // The simulation tick row reads the stored Off; Enter turns it on, which writes 50 Hz.
    REQUIRE(stdConfig_SetInt(JONESFIXEDTICK_CFG_ENGINE_SIM_FIXEDTICK, 0));
    CHECK_FALSE(JonesUiGameplay_LoadValues(true).bFixedTick);

    REQUIRE(stdConfig_SetBool(SITHVOICE_CFG_GAMEPLAY_SHOWTEXT, false));
    REQUIRE(stdConfig_SetBool(SITHOVERLAYMAP_CFG_GAMEPLAY_SHOWHINTS, false));
    REQUIRE(stdConfig_SetBool(SITHOVERLAYMAP_CFG_GAMEPLAY_MAPROTATION, false));
    REQUIRE(stdConfig_SetInt(SITH_CFG_GAMEPLAY_DIFFICULTY, 0));
    REQUIRE(stdConfig_SetBool(JONESCONFIG_CFG_GAMEPLAY_DEFAULTRUN, false));
    REQUIRE(stdConfig_SetBool(SITHWEAPON_CFG_ENGINERENDERER_VFX_WEAPONFIREFLASH_ENABLED, true));
    REQUIRE(stdConfig_SetBool(SITHMATERIAL_CFG_CNDWORLDMATERIALS_LOADEXTERNAL, true));
    REQUIRE(stdConfig_SetBool(SITHPUPPET_CFG_CNDWORLDKEYFRAMES_LOADEXTERNAL, true));
    REQUIRE(stdConfig_SetBool(SITHWEAPON_CFG_GAMEPLAY_QUICKWHIP, true));

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    // 0 show text, 1 show hints, 2 rotate map, 3 difficulty, 4 default to run, 5 reticle,
    // 6 blood, 7 profile (disabled), 8 profile note, 9 enhancements heading,
    // 10 interaction prompts, 11 quick whip, 12 fire flash, 13 external materials,
    // 14 external keyframes, 15 simulation tick, 16 OK, 17 back.
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
    testContext.platform.PushKey(platform::Key::Enter); // interaction prompts off
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter); // quick whip off
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter); // fire flash off
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter); // external materials off
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter); // external keyframes off
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter); // simulation tick off -> on
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
    CHECK_FALSE(JonesHud_IsInteractionPromptsEnabled());
    CHECK_FALSE(sithWeapon_IsQuickWhipEnabled());
#ifdef J3D_QOL_IMPROVEMENTS
    CHECK_FALSE(stdConfig_GetBool(SITHWEAPON_CFG_GAMEPLAY_QUICKWHIP, true));
#else
    // The non-QOL weapon ignores the toggle, so the row was loaded off and Enter turned the
    // stored value on; this build only exercises the write path.
    CHECK(stdConfig_GetBool(SITHWEAPON_CFG_GAMEPLAY_QUICKWHIP, false));
#endif
    CHECK_FALSE(stdConfig_GetBool(SITHWEAPON_CFG_ENGINERENDERER_VFX_WEAPONFIREFLASH_ENABLED, true));
    CHECK_FALSE(stdConfig_GetBool(SITHMATERIAL_CFG_CNDWORLDMATERIALS_LOADEXTERNAL, true));
    CHECK_FALSE(stdConfig_GetBool(SITHPUPPET_CFG_CNDWORLDKEYFRAMES_LOADEXTERNAL, true));
    CHECK(stdConfig_GetInt(JONESFIXEDTICK_CFG_ENGINE_SIM_FIXEDTICK, -1) == 50);
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
    sithControl_SetModernControlStyle(true);
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
    REQUIRE(stdConfig_SetBool(SITHWEAPON_CFG_GAMEPLAY_QUICKWHIP, true));
    sithWeapon_SetQuickWhipEnabled(true);

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    // Up from the first row wraps to back, then OK. The disabled enhancement rows are skipped.
    testContext.platform.PushKey(platform::Key::Up);
    testContext.platform.PushKey(platform::Key::Up);
    testContext.platform.PushKey(platform::Key::Enter);

    REQUIRE(JonesUiGameplay_RunGameplayScreen(ui, &painter, false) == 1);

    CHECK_FALSE(JonesHud_IsInteractionPromptsEnabled());
    CHECK_FALSE(sithWeapon_IsQuickWhipEnabled());
    CHECK_FALSE(stdConfig_GetBool(SITHWEAPON_CFG_GAMEPLAY_QUICKWHIP, true));
    CHECK_FALSE(stdConfig_GetBool(SITHWEAPON_CFG_ENGINERENDERER_VFX_WEAPONFIREFLASH_ENABLED, true));
    CHECK_FALSE(stdConfig_GetBool(SITHMATERIAL_CFG_CNDWORLDMATERIALS_LOADEXTERNAL, true));
    CHECK_FALSE(stdConfig_GetBool(SITHPUPPET_CFG_CNDWORLDKEYFRAMES_LOADEXTERNAL, true));
    CHECK(stdConfig_GetInt(JONESFIXEDTICK_CFG_ENGINE_SIM_FIXEDTICK, -1) == 0);
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
