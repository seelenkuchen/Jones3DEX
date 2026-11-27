// Added: the logic tests of the Babylon column's Brightness calibration screen and its Options row.
#include "JonesUiSettingsTestSupport.h"

namespace
{
    using namespace babylon;
    using namespace JonesUiSettingsTest;
}

TEST_CASE("the brightness calibration is wanted only while its key is missing")
{
    SettingsTestContext testContext;
    ConfigTest config;

    CHECK(JonesUiBrightness_NeedsCalibration());

    REQUIRE(stdConfig_SetFloat(JONESDISPLAY_CFG_ENGINE_RENDER_BRIGHTNESS, 1.2f));
    CHECK_FALSE(JonesUiBrightness_NeedsCalibration());
}

TEST_CASE("the brightness swatch uses the tone-map gamma formula")
{
    const ui::Color identity = JonesUiBrightness_MapSceneColor(0.25f, 1.0f, 0u, 1.0f);
    CHECK(identity.r == doctest::Approx(0.25f));
    CHECK(identity.g == doctest::Approx(0.25f));
    CHECK(identity.b == doctest::Approx(0.25f));

    const ui::Color bright = JonesUiBrightness_MapSceneColor(0.25f, 1.0f, 0u, 1.5f);
    const float expected = std::pow(0.25f, 1.0f / 1.5f);
    CHECK(bright.r == doctest::Approx(expected).epsilon(1e-6));
    CHECK(bright.g == doctest::Approx(expected).epsilon(1e-6));

    // An invalid brightness falls back to the identity, and the operator stays ahead of it.
    CHECK(JonesUiBrightness_MapSceneColor(0.25f, 1.0f, 0u, 0.0f).r
        == doctest::Approx(0.25f));
    const ui::Color neutral = JonesUiBrightness_MapSceneColor(0.25f, 1.0f, 1u, 1.0f);
    CHECK(neutral.r == doctest::Approx(0.25f));
}

TEST_CASE("the brightness screen steps by 0.05, clamps and writes the key on OK")
{
    SettingsTestContext testContext;
    ConfigTest config;

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    REQUIRE(stdConfig_SetFloat(JONESDISPLAY_CFG_ENGINE_RENDER_BRIGHTNESS, 1.0f));
    testContext.platform.PushKey(platform::Key::Right); // 1.00 -> 1.05
    testContext.platform.PushKey(platform::Key::Down);  // Default
    testContext.platform.PushKey(platform::Key::Down);  // OK
    testContext.platform.PushKey(platform::Key::Enter);
    REQUIRE(JonesUiBrightness_Run(ui, &painter) == 1);
    CHECK(stdConfig_GetFloat(JONESDISPLAY_CFG_ENGINE_RENDER_BRIGHTNESS, -1.0f)
        == doctest::Approx(1.05f));

    // Left at the lower end and Right at the upper end keep the clamp.
    REQUIRE(stdConfig_SetFloat(JONESDISPLAY_CFG_ENGINE_RENDER_BRIGHTNESS, 0.6f));
    testContext.platform.PushKey(platform::Key::Left);
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter);
    REQUIRE(JonesUiBrightness_Run(ui, &painter) == 1);
    CHECK(stdConfig_GetFloat(JONESDISPLAY_CFG_ENGINE_RENDER_BRIGHTNESS, -1.0f)
        == doctest::Approx(0.6f));

    REQUIRE(stdConfig_SetFloat(JONESDISPLAY_CFG_ENGINE_RENDER_BRIGHTNESS, 1.8f));
    testContext.platform.PushKey(platform::Key::Right);
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Down);
    testContext.platform.PushKey(platform::Key::Enter);
    REQUIRE(JonesUiBrightness_Run(ui, &painter) == 1);
    CHECK(stdConfig_GetFloat(JONESDISPLAY_CFG_ENGINE_RENDER_BRIGHTNESS, -1.0f)
        == doctest::Approx(1.8f));
}

TEST_CASE("the brightness screen Default returns to 1.0")
{
    SettingsTestContext testContext;
    ConfigTest config;

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    REQUIRE(stdConfig_SetFloat(JONESDISPLAY_CFG_ENGINE_RENDER_BRIGHTNESS, 1.3f));
    testContext.platform.PushKey(platform::Key::Down);  // Default
    testContext.platform.PushKey(platform::Key::Enter); // reopen at 1.0
    testContext.platform.PushKey(platform::Key::Down);  // Default
    testContext.platform.PushKey(platform::Key::Down);  // OK
    testContext.platform.PushKey(platform::Key::Enter);

    REQUIRE(JonesUiBrightness_Run(ui, &painter) == 1);
    CHECK(stdConfig_GetFloat(JONESDISPLAY_CFG_ENGINE_RENDER_BRIGHTNESS, -1.0f)
        == doctest::Approx(1.0f));
}

TEST_CASE("the brightness screen back keeps the value")
{
    SettingsTestContext testContext;
    ConfigTest config;

    ui::NullPainter painter;
    ui::Ui ui(painter, testContext.cvars);

    REQUIRE(stdConfig_SetFloat(JONESDISPLAY_CFG_ENGINE_RENDER_BRIGHTNESS, 1.0f));
    testContext.platform.PushKey(platform::Key::Right); // 1.05
    testContext.platform.PushKey(platform::Key::Escape);

    REQUIRE(JonesUiBrightness_Run(ui, &painter) == 2);
    CHECK(stdConfig_GetFloat(JONESDISPLAY_CFG_ENGINE_RENDER_BRIGHTNESS, -1.0f)
        == doctest::Approx(1.05f));
}

TEST_CASE("the Options Display brightness row opens the calibration")
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

    // Gameplay -> Controls -> Display, then down from Bit depth to Brightness... (row 31).
    testContext.platform.PushKey(platform::Key::E);
    testContext.platform.PushKey(platform::Key::E);
    for ( int row = 0; row < 31; ++row )
    {
        testContext.platform.PushKey(platform::Key::Down);
    }

    testContext.platform.PushKey(platform::Key::Enter); // open the calibration
    testContext.platform.PushKey(platform::Key::Right); // 1.00 -> 1.05
    testContext.platform.PushKey(platform::Key::Down);  // Default
    testContext.platform.PushKey(platform::Key::Down);  // OK
    testContext.platform.PushKey(platform::Key::Enter);
    testContext.platform.PushKey(platform::Key::Escape); // leave Options, keeping changes

    REQUIRE(JonesUiOptions_RunOptionsScreen(ui, &painter, data) == 1);
    CHECK(stdConfig_GetFloat(JONESDISPLAY_CFG_ENGINE_RENDER_BRIGHTNESS, -1.0f)
        == doctest::Approx(1.05f));
}
