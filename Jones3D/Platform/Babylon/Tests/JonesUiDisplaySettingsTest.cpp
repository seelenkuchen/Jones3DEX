// Added: the logic tests of the Babylon column's Display settings screen and its resolution list.
#include "JonesUiSettingsTestSupport.h"

namespace
{
    using namespace babylon;
    using namespace JonesUiSettingsTest;
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

TEST_CASE("the display screen reads the ambient occlusion default per build")
{
    SettingsTestContext testContext;
    ConfigTest config;
    SeedDisplayConfig();

    StdDisplayEnvironment env;
    StdDisplayInfo info;
    std::vector<StdVideoMode> aModes;
    BuildDisplayEnv(env, info, aModes);

    JonesDisplaySettings settings = DefaultDisplaySettings();
    JonesUiDisplayValues values;
    JonesUiDisplayLayout layout;
    const JonesUiDisplayModel model = JonesUiDisplay_LoadModel(&env, &settings, values);
    const babylon::ui::Screen screen = JonesUiDisplay_BuildScreen(model, values, layout, true);

    REQUIRE(layout.ssao < screen.widgets.size());
    const babylon::ui::Choice* pChoice =
        std::get_if<babylon::ui::Choice>(&screen.widgets[layout.ssao]);
    REQUIRE(pChoice != nullptr);
    CHECK(pChoice->text == "Ambient occlusion");
    CHECK(pChoice->options == std::vector<std::string>{ "Off", "Low", "Medium", "High" });
#ifdef J3D_QOL_IMPROVEMENTS
    CHECK(pChoice->selected == 1u);
#else
    CHECK(pChoice->selected == 0u);
#endif

    // The row sits after Light shafts and before Motion blur and Water ripples.
    const babylon::ui::Choice* pShafts =
        std::get_if<babylon::ui::Choice>(&screen.widgets[layout.lightShafts]);
    const babylon::ui::Choice* pMotionBlur =
        std::get_if<babylon::ui::Choice>(&screen.widgets[layout.motionBlur]);
    const babylon::ui::Toggle* pWater =
        std::get_if<babylon::ui::Toggle>(&screen.widgets[layout.waterRipples]);
    REQUIRE(pShafts != nullptr);
    REQUIRE(pMotionBlur != nullptr);
    REQUIRE(pWater != nullptr);
    CHECK(layout.lightShafts < layout.ssao);
    CHECK(layout.ssao < layout.motionBlur);
    CHECK(layout.motionBlur < layout.waterRipples);
}

TEST_CASE("the display screen reads the shadow defaults per build")
{
    SettingsTestContext testContext;
    ConfigTest config;
    SeedDisplayConfig();

    StdDisplayEnvironment env;
    StdDisplayInfo info;
    std::vector<StdVideoMode> aModes;
    BuildDisplayEnv(env, info, aModes);

    JonesDisplaySettings settings = DefaultDisplaySettings();
    JonesUiDisplayValues values;
    JonesUiDisplayLayout layout;
    JonesUiDisplayModel model = JonesUiDisplay_LoadModel(&env, &settings, values);
    babylon::ui::Screen screen = JonesUiDisplay_BuildScreen(model, values, layout, true);

    // The two rows sit together directly after Ambient occlusion.
    REQUIRE(layout.shadows < screen.widgets.size());
    REQUIRE(layout.contactShadows < screen.widgets.size());
    const babylon::ui::Toggle* pShadows =
        std::get_if<babylon::ui::Toggle>(&screen.widgets[layout.shadows]);
    const babylon::ui::Toggle* pContactShadows =
        std::get_if<babylon::ui::Toggle>(&screen.widgets[layout.contactShadows]);
    REQUIRE(pShadows != nullptr);
    REQUIRE(pContactShadows != nullptr);
    CHECK(pShadows->text == "Shadows");
    CHECK(pContactShadows->text == "Contact shadows");
    CHECK(layout.ssao < layout.shadows);
    CHECK(layout.shadows < layout.contactShadows);
    CHECK(layout.contactShadows < layout.motionBlur);
#ifdef J3D_QOL_IMPROVEMENTS
    CHECK(pShadows->value);
    CHECK(pContactShadows->value);
#else
    CHECK_FALSE(pShadows->value);
    CHECK_FALSE(pContactShadows->value);
#endif

    // A stored on still reports off in Retail/QOL-off, as the Lighting row does.
    REQUIRE(stdConfig_SetBool(JONESDISPLAY_CFG_ENGINE_RENDER_SHADOWS, true));
    REQUIRE(stdConfig_SetBool(JONESDISPLAY_CFG_ENGINE_RENDER_CONTACTSHADOWS, true));
    model = JonesUiDisplay_LoadModel(&env, &settings, values);
    screen = JonesUiDisplay_BuildScreen(model, values, layout, true);
    pShadows = std::get_if<babylon::ui::Toggle>(&screen.widgets[layout.shadows]);
    pContactShadows = std::get_if<babylon::ui::Toggle>(&screen.widgets[layout.contactShadows]);
    REQUIRE(pShadows != nullptr);
    REQUIRE(pContactShadows != nullptr);
#ifdef J3D_QOL_IMPROVEMENTS
    CHECK(pShadows->value);
    CHECK(pContactShadows->value);
#else
    CHECK_FALSE(pShadows->value);
    CHECK_FALSE(pContactShadows->value);
#endif

    // A stored off reaches the rows in Enhanced.
    REQUIRE(stdConfig_SetBool(JONESDISPLAY_CFG_ENGINE_RENDER_SHADOWS, false));
    REQUIRE(stdConfig_SetBool(JONESDISPLAY_CFG_ENGINE_RENDER_CONTACTSHADOWS, false));
    model = JonesUiDisplay_LoadModel(&env, &settings, values);
    screen = JonesUiDisplay_BuildScreen(model, values, layout, true);
    pShadows = std::get_if<babylon::ui::Toggle>(&screen.widgets[layout.shadows]);
    pContactShadows = std::get_if<babylon::ui::Toggle>(&screen.widgets[layout.contactShadows]);
    REQUIRE(pShadows != nullptr);
    REQUIRE(pContactShadows != nullptr);
    CHECK_FALSE(pShadows->value);
    CHECK_FALSE(pContactShadows->value);
}

TEST_CASE("the display screen reads the motion blur default per build")
{
    SettingsTestContext testContext;
    ConfigTest config;
    SeedDisplayConfig();

    StdDisplayEnvironment env;
    StdDisplayInfo info;
    std::vector<StdVideoMode> aModes;
    BuildDisplayEnv(env, info, aModes);

    JonesDisplaySettings settings = DefaultDisplaySettings();
    JonesUiDisplayValues values;
    JonesUiDisplayLayout layout;
    const JonesUiDisplayModel model = JonesUiDisplay_LoadModel(&env, &settings, values);
    const babylon::ui::Screen screen = JonesUiDisplay_BuildScreen(model, values, layout, true);

    REQUIRE(layout.motionBlur < screen.widgets.size());
    const babylon::ui::Choice* pChoice =
        std::get_if<babylon::ui::Choice>(&screen.widgets[layout.motionBlur]);
    REQUIRE(pChoice != nullptr);
    CHECK(pChoice->text == "Motion blur");
    CHECK(pChoice->options == std::vector<std::string>{ "Off", "Low", "Medium", "High" });
#ifdef J3D_QOL_IMPROVEMENTS
    CHECK(pChoice->selected == 1u);
#else
    CHECK(pChoice->selected == 0u);
#endif
}

TEST_CASE("the display frame rate row defaults to V-Sync")
{
    SettingsTestContext testContext;
    ConfigTest config;
    SeedDisplayConfig();

    StdDisplayEnvironment env;
    StdDisplayInfo info;
    std::vector<StdVideoMode> aModes;
    BuildDisplayEnv(env, info, aModes);

    JonesDisplaySettings settings = DefaultDisplaySettings();
    JonesUiDisplayValues values;
    JonesUiDisplayLayout layout;
    const JonesUiDisplayModel model = JonesUiDisplay_LoadModel(&env, &settings, values);
    CHECK(values.frameLimit == 0u);

    const babylon::ui::Screen screen = JonesUiDisplay_BuildScreen(model, values, layout, true);
    REQUIRE(layout.frameLimit < screen.widgets.size());
    const babylon::ui::Choice* pChoice =
        std::get_if<babylon::ui::Choice>(&screen.widgets[layout.frameLimit]);
    REQUIRE(pChoice != nullptr);
    CHECK(pChoice->text == "Frame rate limit");
    CHECK(pChoice->options == std::vector<std::string>{
        "V-Sync", "30", "60", "120", "144", "240", "Unlimited" });
    CHECK(pChoice->selected == 0u);
}

TEST_CASE("the display lighting row defaults to Per-pixel in Enhanced and reads the config")
{
    SettingsTestContext testContext;
    ConfigTest config;
    SeedDisplayConfig();

    StdDisplayEnvironment env;
    StdDisplayInfo info;
    std::vector<StdVideoMode> aModes;
    BuildDisplayEnv(env, info, aModes);

    JonesDisplaySettings settings = DefaultDisplaySettings();
    JonesUiDisplayValues values;
    JonesUiDisplayLayout layout;
    JonesUiDisplayModel model = JonesUiDisplay_LoadModel(&env, &settings, values);
    babylon::ui::Screen screen = JonesUiDisplay_BuildScreen(model, values, layout, true);

    REQUIRE(layout.lighting < screen.widgets.size());
    const babylon::ui::Choice* pChoice =
        std::get_if<babylon::ui::Choice>(&screen.widgets[layout.lighting]);
    REQUIRE(pChoice != nullptr);
    CHECK(pChoice->text == "Lighting");
    CHECK(pChoice->options == std::vector<std::string>{ "Classic", "Per-pixel" });
#ifdef J3D_QOL_IMPROVEMENTS
    // Enhanced defaults to Per-pixel (owner, 2026-11-24).
    CHECK(pChoice->selected == 1u);

    // A stored Classic reaches the row in Enhanced, but Retail/QOL-off always report Classic.
    REQUIRE(stdConfig_SetBool(JONESDISPLAY_CFG_ENGINE_RENDER_PERPIXELLIGHTING, false));
    model = JonesUiDisplay_LoadModel(&env, &settings, values);
    screen = JonesUiDisplay_BuildScreen(model, values, layout, true);
    pChoice = std::get_if<babylon::ui::Choice>(&screen.widgets[layout.lighting]);
    REQUIRE(pChoice != nullptr);
    CHECK(pChoice->selected == 0u);
#else
    CHECK(pChoice->selected == 0u);
    REQUIRE(stdConfig_SetBool(JONESDISPLAY_CFG_ENGINE_RENDER_PERPIXELLIGHTING, true));
    model = JonesUiDisplay_LoadModel(&env, &settings, values);
    screen = JonesUiDisplay_BuildScreen(model, values, layout, true);
    pChoice = std::get_if<babylon::ui::Choice>(&screen.widgets[layout.lighting]);
    REQUIRE(pChoice != nullptr);
    CHECK(pChoice->selected == 0u);
#endif
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
    sithCamera_SetGameplayFOV(0.0f);

    // The frame-rate row reads the stored 144 Hz (index 4); Right selects 240 (index 5).
    REQUIRE(stdConfig_SetInt(JONESDISPLAY_CFG_ENGINE_RENDER_FRAMELIMIT, 144));

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    // 0 bit depth, 1 resolution, 2 windowed, 3 filter, 4 fog, 5 fog density, 6 performance,
    // 7 hi-poly, 8 lit faces, 9 lighting, 10 field of view, 11 render scale, 12 anti-aliasing,
    // 13 bloom, 14 light shafts, 15 ambient occlusion, 16 shadows, 17 contact shadows,
    // 18 motion blur, 19 lens flare, 20 vignette, 21 film grain, 22 water ripples,
    // 23 depth of field (cutscenes), 24 depth of field (gameplay), 25 sharpening, 26 tone mapping,
    // 27 exposure, 28 colour grading, 29 anisotropic filtering, 30 crisp pixels,
    // 31 frame rate limit, 32 Brightness..., 33 OK, 34 back.
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
    testContext.platform.PushKey(platform::Key::Right); // lighting Classic -> Per-pixel
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Left); // field of view 90 -> 89
    testContext.platform.PushKey(platform::Key::Down);  // render scale
    testContext.platform.PushKey(platform::Key::Right); // 100% -> 125%
    testContext.platform.PushKey(platform::Key::Down);  // anti-aliasing
    testContext.platform.PushKey(platform::Key::Left);  // SMAA -> Off
    testContext.platform.PushKey(platform::Key::Down);  // bloom
    testContext.platform.PushKey(platform::Key::Left);  // Low/Off -> Off
    testContext.platform.PushKey(platform::Key::Down);  // light shafts
    testContext.platform.PushKey(platform::Key::Left);  // Low/Off -> Off
    testContext.platform.PushKey(platform::Key::Down);  // ambient occlusion
    testContext.platform.PushKey(platform::Key::Left);  // Low/Off -> Off
    testContext.platform.PushKey(platform::Key::Down);  // shadows
    testContext.platform.PushKey(platform::Key::Enter); // on -> off in Enhanced, off -> on without QOL
    testContext.platform.PushKey(platform::Key::Down);  // contact shadows
    testContext.platform.PushKey(platform::Key::Enter); // on -> off in Enhanced, off -> on without QOL
    testContext.platform.PushKey(platform::Key::Down);  // motion blur
    testContext.platform.PushKey(platform::Key::Left);  // Low/Off -> Off
    testContext.platform.PushKey(platform::Key::Down);  // lens flare
    testContext.platform.PushKey(platform::Key::Enter); // on -> off in Enhanced, off -> on without QOL
    testContext.platform.PushKey(platform::Key::Down);  // vignette
    testContext.platform.PushKey(platform::Key::Enter); // off -> on
    testContext.platform.PushKey(platform::Key::Down);  // film grain
    testContext.platform.PushKey(platform::Key::Enter); // off -> on
    testContext.platform.PushKey(platform::Key::Down);  // water ripples
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Down);  // depth of field (cutscenes)
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Down);  // depth of field (gameplay)
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Down);  // sharpening
    testContext.platform.PushKey(platform::Key::Left);  // Low -> Off
    testContext.platform.PushKey(platform::Key::Down);  // tone mapping
    testContext.platform.PushKey(platform::Key::Right); // Neutral -> Filmic
    testContext.platform.PushKey(platform::Key::Down);  // exposure
    testContext.platform.PushKey(platform::Key::Left);  // 1.0 -> 0.9
    testContext.platform.PushKey(platform::Key::Down);  // colour grading
    testContext.platform.PushKey(platform::Key::Right); // Neutral -> Warm
    testContext.platform.PushKey(platform::Key::Down);  // anisotropic filtering
    testContext.platform.PushKey(platform::Key::Left);  // 8x -> 4x
    testContext.platform.PushKey(platform::Key::Down);  // crisp pixels
    testContext.platform.PushKey(platform::Key::Enter); // crisp pixels on
    testContext.platform.PushKey(platform::Key::Down);  // frame rate limit
    testContext.platform.PushKey(platform::Key::Right); // 144 -> 240
    testContext.platform.PushKey(platform::Key::Down);  // Brightness...
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
#ifdef J3D_QOL_IMPROVEMENTS
    // The Lighting row moved to Per-pixel; the Retail profile and QOL-off builds write Classic.
    CHECK(stdConfig_GetBool(JONESDISPLAY_CFG_ENGINE_RENDER_PERPIXELLIGHTING, false));
#else
    CHECK_FALSE(stdConfig_GetBool(JONESDISPLAY_CFG_ENGINE_RENDER_PERPIXELLIGHTING, true));
#endif
    CHECK(stdConfig_GetFloat(JONESDISPLAY_CFG_ENGINE_RENDER_FOV, 0.0f) == doctest::Approx(89.0f));
    CHECK(sithCamera_GetGameplayFOV() == doctest::Approx(89.0f));
    CHECK(stdConfig_GetFloat(JONESDISPLAY_CFG_ENGINE_RENDER_RENDERSCALE, 0.0f)
        == doctest::Approx(1.25f));
    CHECK(stdConfig_GetInt(JONESDISPLAY_CFG_ENGINE_RENDER_ANTIALIASING, -1) == 0);
    CHECK(stdConfig_GetInt(JONESDISPLAY_CFG_ENGINE_RENDER_BLOOM, -1) == 0);
    CHECK(stdConfig_GetInt(JONESDISPLAY_CFG_ENGINE_RENDER_LIGHTSHAFTS, -1) == 0);
    CHECK(stdConfig_GetInt(JONESDISPLAY_CFG_ENGINE_RENDER_SSAO, -1) == 0);
    // Both rows start on in Enhanced and off in Retail/QOL-off; Enter flips the row either way,
    // and Retail/QOL-off still writes them off.
    CHECK_FALSE(stdConfig_GetBool(JONESDISPLAY_CFG_ENGINE_RENDER_SHADOWS, true));
    CHECK_FALSE(stdConfig_GetBool(JONESDISPLAY_CFG_ENGINE_RENDER_CONTACTSHADOWS, true));
    CHECK(stdConfig_GetInt(JONESDISPLAY_CFG_ENGINE_RENDER_MOTIONBLUR, -1) == 0);
#ifdef J3D_QOL_IMPROVEMENTS
    // Enhanced starts lens flare on, so Enter turns it off.
    CHECK_FALSE(stdConfig_GetBool(JONESDISPLAY_CFG_ENGINE_RENDER_LENSFLARE, true));
#else
    // Without QOL it starts off, so Enter turns it on.
    CHECK(stdConfig_GetBool(JONESDISPLAY_CFG_ENGINE_RENDER_LENSFLARE, false));
#endif
    // The vignette and film grain start off in every build, so Enter turns them on.
    CHECK(stdConfig_GetBool(JONESDISPLAY_CFG_ENGINE_RENDER_VIGNETTE, false));
    CHECK(stdConfig_GetBool(JONESDISPLAY_CFG_ENGINE_RENDER_FILMGRAIN, false));
#ifdef J3D_QOL_IMPROVEMENTS
    // Enhanced starts water ripples on, so Enter turns them off.
    CHECK_FALSE(stdConfig_GetBool(JONESDISPLAY_CFG_ENGINE_RENDER_WATER_RIPPLES, true));
#else
    // Without QOL they start off, so Enter turns them on.
    CHECK(stdConfig_GetBool(JONESDISPLAY_CFG_ENGINE_RENDER_WATER_RIPPLES, false));
#endif
#ifdef J3D_QOL_IMPROVEMENTS
    // Enhanced starts both on, so Enter turns them off.
    CHECK_FALSE(stdConfig_GetBool(JONESDISPLAY_CFG_ENGINE_RENDER_DOF_CUTSCENES, true));
    CHECK_FALSE(stdConfig_GetBool(JONESDISPLAY_CFG_ENGINE_RENDER_DOF_GAMEPLAY, true));
#else
    // Without QOL both start off, so Enter turns them on.
    CHECK(stdConfig_GetBool(JONESDISPLAY_CFG_ENGINE_RENDER_DOF_CUTSCENES, false));
    CHECK(stdConfig_GetBool(JONESDISPLAY_CFG_ENGINE_RENDER_DOF_GAMEPLAY, false));
#endif
    CHECK(stdConfig_GetFloat(JONESDISPLAY_CFG_ENGINE_RENDER_SHARPENING, -1.0f)
        == doctest::Approx(0.0f));
#ifdef J3D_QOL_IMPROVEMENTS
    CHECK(stdConfig_GetInt(JONESDISPLAY_CFG_ENGINE_RENDER_TONEMAPPING, -1) == 2);
#else
    // Without QOL the row starts at Clamp (0); stepping right from the first choice selects
    // Neutral (1), not Filmic.
    CHECK(stdConfig_GetInt(JONESDISPLAY_CFG_ENGINE_RENDER_TONEMAPPING, -1) == 1);
#endif
    CHECK(stdConfig_GetFloat(JONESDISPLAY_CFG_ENGINE_RENDER_EXPOSURE, -1.0f)
        == doctest::Approx(0.9f));
    CHECK(stdConfig_GetInt(JONESDISPLAY_CFG_ENGINE_RENDER_COLORGRADING, -1) == 1);
#ifdef J3D_QOL_IMPROVEMENTS
    CHECK(stdConfig_GetInt(STD3D_CFG_ANISOTROPICFILTER, 0) == 4);
#else
    // Without QOL the row starts at Off (1); stepping left from the first choice keeps it there.
    CHECK(stdConfig_GetInt(STD3D_CFG_ANISOTROPICFILTER, 0) == 1);
#endif
    CHECK(stdConfig_GetBool(STD3D_CFG_TEXTUREPIXELATED, false));
    CHECK(stdConfig_GetInt(JONESDISPLAY_CFG_ENGINE_RENDER_FRAMELIMIT, -1) == 240);
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

    // Leave the shared option at its default for the following cases.
    sithCamera_SetGameplayFOV(0.0f);
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

TEST_CASE("the anisotropic filtering key accepts the old boolean form")
{
    SettingsTestContext testContext;
    ConfigTest config;

    CHECK(std3DBabylon_ReadAnisotropicFilterLevel() == J3D_QOL_VALUE(8, 1));

    // An existing Jones.cfg from before the level existed holds true or false.
    REQUIRE(stdConfig_SetBool(STD3D_CFG_ANISOTROPICFILTER, true));
    CHECK(std3DBabylon_ReadAnisotropicFilterLevel() == 8);
    REQUIRE(stdConfig_SetBool(STD3D_CFG_ANISOTROPICFILTER, false));
    CHECK(std3DBabylon_ReadAnisotropicFilterLevel() == 1);

    REQUIRE(stdConfig_SetInt(STD3D_CFG_ANISOTROPICFILTER, 4));
    CHECK(std3DBabylon_ReadAnisotropicFilterLevel() == 4);
    // A hand-edited value snaps to the next lower offered step.
    REQUIRE(stdConfig_SetInt(STD3D_CFG_ANISOTROPICFILTER, 3));
    CHECK(std3DBabylon_ReadAnisotropicFilterLevel() == 2);
    REQUIRE(stdConfig_SetInt(STD3D_CFG_ANISOTROPICFILTER, 100));
    CHECK(std3DBabylon_ReadAnisotropicFilterLevel() == 16);

    CHECK_FALSE(std3DBabylon_ReadTexturePixelated());
    REQUIRE(stdConfig_SetBool(STD3D_CFG_TEXTUREPIXELATED, true));
    CHECK(std3DBabylon_ReadTexturePixelated());
}

TEST_CASE("Alt+Enter toggles fullscreen and writes graphics.window")
{
    SettingsTestContext testContext;
    ConfigTest config;

    g_windowModeSettings = JonesDisplaySettings{};
    platform::NullPlatform nullPlatform;
    std::unique_ptr<platform::Window> pWindow = nullPlatform.createWindow(
        platform::WindowDesc{ "window mode", platform::Size{ 640, 480 }, false });
    REQUIRE(pWindow != nullptr);

    // Point the shared pump at a Null window for this case, so the focus/quit recording and the
    // input state are the same as in the windowed run.
    testContext.context.pPlatform = &nullPlatform;
    testContext.context.pWindow   = pWindow.get();

    // Clear the keys and the hotkey's Enter edge left by earlier cases.
    nullPlatform.pushEvent(platform::KeyChanged{ pWindow->id(), platform::Key::Enter, false, false });
    nullPlatform.pushEvent(platform::KeyChanged{ pWindow->id(), platform::Key::KeypadEnter, false, false });
    nullPlatform.pushEvent(platform::KeyChanged{ pWindow->id(), platform::Key::LeftAlt, false, false });
    nullPlatform.pushEvent(platform::KeyChanged{ pWindow->id(), platform::Key::RightAlt, false, false });
    REQUIRE(stdBabylonEvents_Pump() == 4u);
    CHECK_FALSE(JonesWindowMode_HandleHotkey());
    REQUIRE_FALSE(pWindow->fullscreen());

    // Alt then Enter goes fullscreen and the persisted key says not windowed.
    nullPlatform.pushEvent(platform::KeyChanged{ pWindow->id(), platform::Key::LeftAlt, true, false });
    nullPlatform.pushEvent(platform::KeyChanged{ pWindow->id(), platform::Key::Enter, true, false });
    REQUIRE(stdBabylonEvents_Pump() == 2u);
    CHECK(JonesWindowMode_HandleHotkey());
    CHECK(pWindow->fullscreen());
    CHECK(g_windowModeSettings.bWindowMode == 0);
    CHECK_FALSE(stdConfig_GetBool(JONESDISPLAY_CFG_GRAPHICS_WINDOW, true));

    // A held Enter toggles once; releasing and pressing it again returns to windowed.
    CHECK_FALSE(JonesWindowMode_HandleHotkey());
    nullPlatform.pushEvent(platform::KeyChanged{ pWindow->id(), platform::Key::Enter, false, false });
    REQUIRE(stdBabylonEvents_Pump() == 1u);
    CHECK_FALSE(JonesWindowMode_HandleHotkey());
    nullPlatform.pushEvent(platform::KeyChanged{ pWindow->id(), platform::Key::Enter, true, false });
    REQUIRE(stdBabylonEvents_Pump() == 1u);
    CHECK(JonesWindowMode_HandleHotkey());
    CHECK_FALSE(pWindow->fullscreen());
    CHECK(g_windowModeSettings.bWindowMode == 1);
    CHECK(stdConfig_GetBool(JONESDISPLAY_CFG_GRAPHICS_WINDOW, false));

    // Release the chord, so a later case does not see Alt or Enter held.
    nullPlatform.pushEvent(platform::KeyChanged{ pWindow->id(), platform::Key::Enter, false, false });
    nullPlatform.pushEvent(platform::KeyChanged{ pWindow->id(), platform::Key::LeftAlt, false, false });
    REQUIRE(stdBabylonEvents_Pump() == 2u);
    CHECK_FALSE(JonesWindowMode_HandleHotkey());

    testContext.context.pPlatform = &testContext.platform;
    testContext.context.pWindow   = nullptr;
}
