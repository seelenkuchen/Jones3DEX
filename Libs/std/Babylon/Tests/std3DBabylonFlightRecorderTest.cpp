// Added: Unity Fixture suites of the Babylon column's flight recorder
// (stdDisplayBabylonFlightRecorder.cpp), run through the display adapter's present and screenshot
// paths. The Null group checks that the option off records nothing and the option on fills the ring
// with the requested presents and writes them oldest to newest; the GPU group presents three clear
// colours and reads the centre pixel of each written BMP back. The failure case passes a regular
// file as the output directory. The shared main turns the recorder off for the other display
// suites; each case here sets what it needs.
#include "std3DBabylonTestSupport.h"

#include <std/Babylon/stdBabylonEvents.h>
#include <std/Babylon/stdDisplayBabylonInternal.h>

#include <babylon/platform/Event.h>
#include <babylon/platform/Keys.h>

#include <std/General/stdConfig.h>
#include <std/General/stdJSON.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace std3DBabylonTest;

namespace
{
    constexpr const char* kRecorderDir     = "std3DBabylonFlightRecorder_shots";
    constexpr const char* kBadDir          = "std3DBabylonFlightRecorder_bad";
    constexpr const char* kShotPath        = "std3DBabylonFlightRecorder_shot.bmp";
    constexpr const char* kShotRecorderDir = "std3DBabylonFlightRecorder_shot-last2s";
    constexpr const char* kConfigPath      = "std3DBabylonFlightRecorderTest.json";
    constexpr const char* kConfigKey       = "engine.debug.flightRecorder";

    void RemoveRecorderFiles()
    {
        std::error_code error;
        std::filesystem::remove_all(kRecorderDir, error);
        std::filesystem::remove_all(kShotRecorderDir, error);
        std::remove(kBadDir);
        std::remove(kConfigPath);
        std::remove(kShotPath);
    }

    void OpenHeadless(uint32_t width, uint32_t height)
    {
        TEST_ASSERT_EQUAL_INT(1, stdDisplay_Startup());
        TEST_ASSERT_EQUAL_INT(1, stdDisplay_Open(0u));
        stdDisplay_SetDefaultResolution(width, height);
        TEST_ASSERT_EQUAL_INT(0, stdDisplay_SetMode(0u, 0, 1u));
    }

    size_t CountSubmitted(render::NullDevice& rDevice)
    {
        size_t count = 0;
        for ( const render::RecordedCommand& command : rDevice.recorded() )
        {
            if ( std::holds_alternative<render::FrameSubmitted>(command) )
            {
                ++count;
            }
        }
        return count;
    }

    uint32_t BmpWidth(const std::vector<uint8_t>& bmp)
    {
        TEST_ASSERT_TRUE(bmp.size() >= 54u);
        return (uint32_t)bmp[18] | ((uint32_t)bmp[19] << 8) | ((uint32_t)bmp[20] << 16)
            | ((uint32_t)bmp[21] << 24);
    }

    uint32_t BmpHeight(const std::vector<uint8_t>& bmp)
    {
        TEST_ASSERT_TRUE(bmp.size() >= 54u);
        return (uint32_t)bmp[22] | ((uint32_t)bmp[23] << 8) | ((uint32_t)bmp[24] << 16)
            | ((uint32_t)bmp[25] << 24);
    }

    // The B, G and R bytes of a 24-bit bottom-up BMP's (x, y) pixel, y from the top.
    void BmpPixel(const std::vector<uint8_t>& bmp, uint32_t x, uint32_t y,
        uint8_t* pBlue, uint8_t* pGreen, uint8_t* pRed)
    {
        const uint32_t width  = BmpWidth(bmp);
        const uint32_t height = BmpHeight(bmp);
        TEST_ASSERT_TRUE(x < width && y < height);
        TEST_ASSERT_EQUAL_HEX16(24u, (uint16_t)bmp[28]);

        const size_t stride = ((size_t)width * 3u + 3u) & ~size_t(3u);
        const size_t row    = height - 1u - y;
        const size_t at     = 54u + row * stride + (size_t)x * 3u;
        TEST_ASSERT_TRUE(at + 3u <= bmp.size());
        *pBlue  = bmp[at + 0u];
        *pGreen = bmp[at + 1u];
        *pRed   = bmp[at + 2u];
    }

    std::vector<uint8_t> ReadBmp(const char* pPath)
    {
        std::ifstream file(pPath, std::ios::binary);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)),
            std::istreambuf_iterator<char>());
    }

    bool FlightRecorderFallbackHasStarted()
    {
        return true;
    }

    int FlightRecorderFallbackGetInt(const char*, int defaultValue)
    {
        return defaultValue;
    }

    float FlightRecorderFallbackGetFloat(const char*, float defaultValue)
    {
        return defaultValue;
    }

    int FlightRecorderFallbackGetStr(const char*, char* pDstStr, size_t size,
        const char* pDefaultValue)
    {
        if ( pDstStr && size > 0 )
        {
            pDstStr[0] = '\0';
            if ( pDefaultValue )
            {
                std::snprintf(pDstStr, size, "%s", pDefaultValue);
            }
        }
        return 0;
    }

    void DisableRenderEffects()
    {
        stdRenderBabylon_SetAntiAliasing(false);
        stdRenderBabylon_SetSharpening(0.0f);
        stdRenderBabylon_SetBloom(0.0f);
        stdRenderBabylon_SetLightShafts(0);
        stdRenderBabylon_SetLensFlare(false);
        stdRenderBabylon_SetVignette(false);
        stdRenderBabylon_SetFilmGrain(false);
        stdRenderBabylon_SetWaterRipples(false);
        stdRenderBabylon_SetSsao(0);
        stdRenderBabylon_SetContactShadows(false);
        stdRenderBabylon_SetShadows(false);
        stdRenderBabylon_SetMotionBlur(0);
        stdRenderBabylon_SetDepthOfField(false, false, 1.0f);
        stdRenderBabylon_SetToneMap(StdRenderBabylonToneMap{ 1.0f, 0u, 1.0f, 0u });
    }
}

TEST_GROUP(std3DBabylonFlightRecorder);

TEST_SETUP(std3DBabylonFlightRecorder)
{
    StdGeneralTest_Startup();
    stdRenderCapture_ResetGlobals();
    std3D_ResetGlobals();
    stdDisplay_ResetGlobals();
    UseNullDevice();
    RemoveRecorderFiles();
    stdDisplayBabylonInternal::stdDisplay_FlightRecorderSetEnabledForTest(false);
}

TEST_TEAR_DOWN(std3DBabylonFlightRecorder)
{
    std3D_Shutdown();
    stdDisplay_Shutdown();
    std3D_ResetGlobals();
    stdDisplay_ResetGlobals();
    DropDevice();
    RemoveRecorderFiles();
    stdDisplayBabylonInternal::stdDisplay_FlightRecorderSetEnabledForTest(false);
    StdGeneralTest_Shutdown();
}

TEST(std3DBabylonFlightRecorder, OffAllocatesNothingAndRecordsNoFrame)
{
    OpenHeadless(4u, 2u);
    TEST_ASSERT_EQUAL_size_t(0u,
        stdDisplayBabylonInternal::stdDisplay_FlightRecorderGetTextureCountForTest());

    Null().clearRecorded();
    for ( int i = 0; i < 3; ++i )
    {
        TEST_ASSERT_EQUAL_INT(0, stdDisplay_Update());
    }

    // One submit per present: the flight recorder added no capture frame.
    TEST_ASSERT_EQUAL_size_t(3u, CountSubmitted(Null()));
    TEST_ASSERT_EQUAL_size_t(0u,
        stdDisplayBabylonInternal::stdDisplay_FlightRecorderGetTextureCountForTest());
    TEST_ASSERT_FALSE(stdDisplay_SaveFlightRecorder(kRecorderDir));
    TEST_ASSERT_FALSE(std::filesystem::exists(kRecorderDir));
}

TEST(std3DBabylonFlightRecorder, OnKeepsThePresentsOldestToNewest)
{
    OpenHeadless(4u, 2u);
    stdDisplayBabylonInternal::stdDisplay_FlightRecorderSetEnabledForTest(true);
    TEST_ASSERT_EQUAL_size_t(0u,
        stdDisplayBabylonInternal::stdDisplay_FlightRecorderGetTextureCountForTest());

    Null().clearRecorded();
    for ( int i = 0; i < 3; ++i )
    {
        TEST_ASSERT_EQUAL_INT(0, stdDisplay_Update());
    }

    // A normal present and a capture present per frame, and the ring made on first use.
    TEST_ASSERT_EQUAL_size_t(6u, CountSubmitted(Null()));
    TEST_ASSERT_EQUAL_size_t(stdDisplayBabylonInternal::stdDisplay_kFlightRecorderFrames,
        stdDisplayBabylonInternal::stdDisplay_FlightRecorderGetTextureCountForTest());

    TEST_ASSERT_TRUE(stdDisplay_SaveFlightRecorder(kRecorderDir));

    const std::vector<char> index = ReadFile((std::string(kRecorderDir) + "/index.txt").c_str());
    const std::string indexText(index.begin(), index.end());
    TEST_ASSERT_EQUAL_STRING(
        "frame-0000.bmp, 0\nframe-0001.bmp, 1\nframe-0002.bmp, 2\n", indexText.c_str());

    for ( size_t i = 0; i < 3u; ++i )
    {
        char aName[64];
        std::snprintf(aName, sizeof(aName), "%s/frame-%04zu.bmp", kRecorderDir, i);
        const std::vector<uint8_t> bmp = ReadBmp(aName);
        TEST_ASSERT_EQUAL_UINT32(640u, BmpWidth(bmp));
        TEST_ASSERT_EQUAL_UINT32(320u, BmpHeight(bmp));
        const size_t stride = ((size_t)640u * 3u + 3u) & ~size_t(3u);
        TEST_ASSERT_EQUAL_size_t(54u + stride * 320u, bmp.size());
    }

    TEST_ASSERT_FALSE(std::filesystem::exists((std::string(kRecorderDir) + "/frame-0003.bmp").c_str()));
}

TEST(std3DBabylonFlightRecorder, ConfigReadsTheKeyWhenTheDisplayOpens)
{
    if ( stdConfig_HasStarted() )
    {
        stdConfig_Shutdown();
    }

    const StdConfigFallback fallback = { FlightRecorderFallbackHasStarted,
        FlightRecorderFallbackGetInt, FlightRecorderFallbackGetFloat,
        FlightRecorderFallbackGetStr };
    stdConfig_SetFallback(&fallback);
    TEST_ASSERT_TRUE(stdJSON_Startup());
    TEST_ASSERT_TRUE(stdConfig_Startup(kConfigPath));
    TEST_ASSERT_FALSE(stdConfig_Contains(kConfigKey));

    stdDisplayBabylonInternal::stdDisplay_FlightRecorderClearEnabledForTest();

    // The default is on.
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_Startup());
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_Open(0u));
    TEST_ASSERT_TRUE(stdDisplayBabylonInternal::stdDisplay_FlightRecorderIsEnabledForTest());
    stdDisplay_Close();

    // An explicit false turns it off for the next open.
    TEST_ASSERT_TRUE(stdConfig_SetBool(kConfigKey, false));
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_Open(0u));
    TEST_ASSERT_FALSE(stdDisplayBabylonInternal::stdDisplay_FlightRecorderIsEnabledForTest());
    stdDisplay_Close();

    stdConfig_Shutdown();
    stdJSON_Shutdown();
    stdDisplayBabylonInternal::stdDisplay_FlightRecorderSetEnabledForTest(false);
}

TEST(std3DBabylonFlightRecorder, SaveFailsAndLogsForAnUnwritableDirectory)
{
    OpenHeadless(4u, 2u);
    stdDisplayBabylonInternal::stdDisplay_FlightRecorderSetEnabledForTest(true);
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_Update());

    std::ofstream blockingFile(kBadDir, std::ios::binary);
    TEST_ASSERT_TRUE(blockingFile.is_open());
    blockingFile << "not a directory";
    blockingFile.close();

    std::vector<std::string> aMessages;
    bool bSaved = false;
    {
        babylon::log::CaptureSink sink;
        bSaved = stdDisplay_SaveFlightRecorder(kBadDir);
        for ( const babylon::log::CapturedRecord& record : sink.records() )
        {
            aMessages.push_back(record.message);
        }
    }

    TEST_ASSERT_FALSE(bSaved);
    TEST_ASSERT_FALSE(aMessages.empty());
}

TEST(std3DBabylonFlightRecorder, ShiftF12WritesTheRecorderBesideTheScreenshot)
{
    OpenHeadless(4u, 2u);
    stdDisplayBabylonInternal::stdDisplay_FlightRecorderSetEnabledForTest(true);
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_Update());

    babylon::input::InputState& rInputState = stdBabylonEvents_InputState();
    rInputState.apply(babylon::platform::KeyChanged{ 0,
        babylon::platform::Key::LeftShift, true, false });
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_SaveScreen(kShotPath));
    TEST_ASSERT_TRUE(std::filesystem::exists(kShotPath));
    TEST_ASSERT_TRUE(std::filesystem::exists(
        (std::string(kShotRecorderDir) + "/index.txt").c_str()));

    // Without Shift the next screenshot writes no recorder directory.
    std::error_code error;
    std::filesystem::remove_all(kShotRecorderDir, error);
    rInputState.apply(babylon::platform::KeyChanged{ 0,
        babylon::platform::Key::LeftShift, false, false });
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_SaveScreen(kShotPath));
    TEST_ASSERT_FALSE(std::filesystem::exists(kShotRecorderDir));
}

TEST(std3DBabylonFlightRecorder, GpuWritesThePresentedColoursInOrder)
{
    std::unique_ptr<render::Device> pDevice = render::createSdlGpuDevice(nullptr);
    if ( !pDevice )
    {
        if ( GpuRequired() )
        {
            TEST_FAIL_MESSAGE("no SDL GPU device although BABYLON_REQUIRE_GPU=1");
        }
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }
    UseDevice(std::move(pDevice));

    OpenHeadless(4u, 2u);
    DisableRenderEffects();
    stdDisplayBabylonInternal::stdDisplay_FlightRecorderSetEnabledForTest(true);

    // Three distinct clear colours, each presented once.
    const uint32_t aColors[3] = { 0x00FF0000u, 0x0000FF00u, 0x000000FFu };
    for ( uint32_t color : aColors )
    {
        TEST_ASSERT_EQUAL_INT(0, stdDisplay_BackBufferFill(color, nullptr));
        TEST_ASSERT_EQUAL_INT(0, stdDisplay_Update());
    }

    TEST_ASSERT_TRUE(stdDisplay_SaveFlightRecorder(kRecorderDir));

    const uint8_t aExpectedBgr[3][3] = {
        { 0u, 0u, 255u },
        { 0u, 255u, 0u },
        { 255u, 0u, 0u },
    };
    for ( size_t i = 0; i < 3u; ++i )
    {
        char aName[64];
        std::snprintf(aName, sizeof(aName), "%s/frame-%04zu.bmp", kRecorderDir, i);
        const std::vector<uint8_t> bmp = ReadBmp(aName);
        TEST_ASSERT_EQUAL_UINT32(640u, BmpWidth(bmp));
        TEST_ASSERT_EQUAL_UINT32(320u, BmpHeight(bmp));

        uint8_t blue  = 0;
        uint8_t green = 0;
        uint8_t red   = 0;
        BmpPixel(bmp, BmpWidth(bmp) / 2u, BmpHeight(bmp) / 2u, &blue, &green, &red);
        char aMessage[96];
        std::snprintf(aMessage, sizeof(aMessage), "frame %zu centre is %u %u %u", i,
            (unsigned)red, (unsigned)green, (unsigned)blue);
        TEST_ASSERT_INT_WITHIN_MESSAGE(1, aExpectedBgr[i][0], blue, aMessage);
        TEST_ASSERT_INT_WITHIN_MESSAGE(1, aExpectedBgr[i][1], green, aMessage);
        TEST_ASSERT_INT_WITHIN_MESSAGE(1, aExpectedBgr[i][2], red, aMessage);
    }
}

TEST_GROUP_RUNNER(std3DBabylonFlightRecorder)
{
    RUN_TEST_CASE(std3DBabylonFlightRecorder, OffAllocatesNothingAndRecordsNoFrame);
    RUN_TEST_CASE(std3DBabylonFlightRecorder, OnKeepsThePresentsOldestToNewest);
    RUN_TEST_CASE(std3DBabylonFlightRecorder, ConfigReadsTheKeyWhenTheDisplayOpens);
    RUN_TEST_CASE(std3DBabylonFlightRecorder, SaveFailsAndLogsForAnUnwritableDirectory);
    RUN_TEST_CASE(std3DBabylonFlightRecorder, ShiftF12WritesTheRecorderBesideTheScreenshot);
    RUN_TEST_CASE(std3DBabylonFlightRecorder, GpuWritesThePresentedColoursInOrder);
}
