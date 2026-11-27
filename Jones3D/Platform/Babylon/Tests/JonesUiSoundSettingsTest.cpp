// Added: the logic tests of the Babylon column's Sound settings screen and its output-device list.
#include "JonesUiSettingsTestSupport.h"

namespace
{
    using namespace babylon;
    using namespace JonesUiSettingsTest;

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
