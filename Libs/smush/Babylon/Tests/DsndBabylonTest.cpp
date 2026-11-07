// Added: Unity Fixture suite for the Babylon column's SMUSH sound driver. It installs the adapter
// context in the shared test main (a NullDevice for audio), so the stream queue moves only when a
// test says so, and drives the timer from a fake millisecond clock through TimerPolled. The number
// of blocks and the timer values are the ones DsndSilent.c's schedule implies; the gains are the
// DX9 volume curve Dsnd.c defines (hundredths of a dB, gain = 10^(dB / 2000)).
#include <unity_fixture.h>

#include <cmath>
#include <cstdint>
#include <optional>

#include <babylon/audio/Handle.h>
#include <babylon/audio/NullDevice.h>
#include <babylon/audio/Stream.h>

#include <smush/insane/Lec/LecShare.h>
#include <smush/insane/Memory/Memory.h>
#include <smush/insane/Sound/Dsnd.h>
#include <smush/insane/Timer/Timer.h>
#include <smush/insane/Timer/TimerSys.h>

#include <std/Babylon/stdBabylon.h>

TEST_GROUP(SmushDsndBabylon);

extern "C" void DsndBabylon_TestGetVoice(uint32_t* pIndex, uint32_t* pGeneration);
extern "C" int DsndBabylon_TestGetVoicesCreated(void);
extern "C" int DsndBabylon_TestGetPushes(void);
extern "C" int DsndBabylon_TestGetSkipped(void);
extern "C" int DsndBabylon_TestGetRecreates(void);
extern "C" uint64_t DsndBabylon_TestGetBytesPushed(void);

namespace
{
    const unsigned int kStartMsec = 5000;

    unsigned int nowMsec   = kStartMsec;
    int      bShare        = 0;
    int      bStarted      = 0;
    int      numFills      = 0;
    int      numFillFrames = 0;
    SndDriver* pDriver     = nullptr;
    babylon::audio::NullDevice* pDevice = nullptr;

    unsigned int TestNow(void)
    {
        return nowMsec;
    }

    void J3DAPI TestSleep(uint32_t msec)
    {
        nowMsec += msec;
    }

    void TestError(const char* pFormat, ...)
    {
        (void)pFormat;
    }

    void J3DAPI TestFill(int n, SoundFeed pfnFeed)
    {
        (void)pfnFeed;
        ++numFills;
        numFillFrames += n;
    }

    babylon::audio::VoiceHandle Voice(void)
    {
        uint32_t index      = 0;
        uint32_t generation = 0;
        DsndBabylon_TestGetVoice(&index, &generation);
        return babylon::audio::VoiceHandle{ index, generation };
    }

    // The DX9 curve Dsnd.c fills in: gain = 10^(dB / 2000), dB in hundredths.
    float ExpectedGain(int db)
    {
        return (float)std::pow(10.0, (double)db / 2000.0);
    }

    void StartDriver(void)
    {
        TEST_ASSERT_EQUAL_INT(0, pDriver->Start(22050, 1, TestFill));
        bStarted = 1;
    }

    void AdvanceUntilFills(int target)
    {
        int guard = 0;
        while ( numFills < target && guard++ < 1000000 )
        {
            timer_Update();
        }

        TEST_ASSERT_GREATER_OR_EQUAL_INT(target, numFills);
    }
}

TEST_SETUP(SmushDsndBabylon)
{
    StdBabylonContext& rContext = stdBabylon_RequireContext("SmushDsndBabylon");
    pDevice = dynamic_cast<babylon::audio::NullDevice*>(rContext.pAudioDevice);
    TEST_ASSERT_NOT_NULL(pDevice);

    TEST_ASSERT_TRUE(pDevice->open());

    Dsnd_ResetGlobals();
    Timer_ResetGlobals();
    nowMsec       = kStartMsec;
    bShare        = 0;
    bStarted      = 0;
    numFills      = 0;
    numFillFrames = 0;
    pDriver       = nullptr;
    TimerPolled_SetClockForTest(TestNow, TestSleep);

    LecModuleEntry aModules[] = {
        { memory_Register, nullptr },
        { timer_Register, nullptr },
        { dsnd_Register, nullptr },
        { nullptr, nullptr },
    };
    TEST_ASSERT_NOT_NULL(LecShare_Init(TestError, aModules, "MEMORY=insane", ""));
    bShare = 1;
    TEST_ASSERT_EQUAL_INT(0, LecShare_Startup());
    pDriver = (SndDriver*)LecShare_Subscribe("SNDDRIVER.driver", nullptr);
    TEST_ASSERT_NOT_NULL(pDriver);
}

TEST_TEAR_DOWN(SmushDsndBabylon)
{
    if ( bStarted )
    {
        pDriver->Stop();
        bStarted = 0;
    }

    if ( bShare )
    {
        LecShare_Shutdown();
        LecShare_UnregisterAll();
        bShare = 0;
    }

    TimerPolled_SetClockForTest(nullptr, nullptr);
    Dsnd_ResetGlobals();
    Timer_ResetGlobals();
}

TEST(SmushDsndBabylon, StartPushesOneBlockPerDueFillWithTheDx9Volume)
{
    StartDriver();
    AdvanceUntilFills(4);

    TEST_ASSERT_EQUAL_INT(4, numFills);
    TEST_ASSERT_EQUAL_INT(4 * DSND_BLOCKFRAMES, numFillFrames);
    TEST_ASSERT_EQUAL_INT(1, DsndBabylon_TestGetVoicesCreated());
    TEST_ASSERT_EQUAL_INT(4, DsndBabylon_TestGetPushes());
    TEST_ASSERT_EQUAL_INT(0, DsndBabylon_TestGetRecreates());
    TEST_ASSERT_EQUAL_UINT64((uint64_t)4 * DSND_BLOCKSIZE, DsndBabylon_TestGetBytesPushed());

    const babylon::audio::VoiceHandle voice = Voice();
    TEST_ASSERT_FALSE(voice.isNull());

    const std::optional<babylon::audio::StreamState> state = pDevice->streamState(voice);
    TEST_ASSERT_TRUE(state.has_value());
    TEST_ASSERT_EQUAL_UINT64((uint64_t)4 * DSND_BLOCKFRAMES, state->queuedFrames);
    TEST_ASSERT_EQUAL_UINT64(0, state->playedFrames);

    std::optional<babylon::audio::VoiceMix> mix = pDevice->voiceMix(voice);
    TEST_ASSERT_TRUE(mix.has_value());
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, mix->gain); // default volume 127 is 0 dB

    pDriver->SetVolume(64); // -1000 hundredths of a dB
    mix = pDevice->voiceMix(voice);
    TEST_ASSERT_TRUE(mix.has_value());
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, ExpectedGain(-1000), mix->gain);

    pDriver->SetVolume(0x85); // masked to 5, -4500 hundredths of a dB
    mix = pDevice->voiceMix(voice);
    TEST_ASSERT_TRUE(mix.has_value());
    TEST_ASSERT_FLOAT_WITHIN(1e-7f, ExpectedGain(-4500), mix->gain);
}

TEST(SmushDsndBabylon, QueueSkipsOneBlockPerPushAboveTheHighMark)
{
    StartDriver();
    for ( int i = 0; i < 18; ++i )
    {
        AdvanceUntilFills(numFills + 1);

        const std::optional<babylon::audio::StreamState> state = pDevice->streamState(Voice());
        TEST_ASSERT_TRUE(state.has_value());
        TEST_ASSERT_LESS_OR_EQUAL_UINT64((uint64_t)6 * DSND_BLOCKFRAMES, state->queuedFrames);

        // The queue reaches six blocks after six pushes; every later fill skips exactly one push.
        const int numFilled = i + 1;
        const int expectedSkipped = numFilled > 6 ? numFilled - 6 : 0;
        TEST_ASSERT_EQUAL_INT(expectedSkipped, DsndBabylon_TestGetSkipped());
    }

    TEST_ASSERT_EQUAL_INT(6, DsndBabylon_TestGetPushes());
    TEST_ASSERT_EQUAL_INT(12, DsndBabylon_TestGetSkipped());
    TEST_ASSERT_EQUAL_INT(0, DsndBabylon_TestGetRecreates());
    TEST_ASSERT_EQUAL_UINT64((uint64_t)6 * DSND_BLOCKSIZE, DsndBabylon_TestGetBytesPushed());
}

TEST(SmushDsndBabylon, QueueUnderrunsWhenTheDeviceConsumesFaster)
{
    StartDriver();
    for ( int i = 0; i < 6; ++i )
    {
        AdvanceUntilFills(numFills + 1);
        pDevice->advanceFrames(6000); // 2756 source frames at 22050 Hz: more than one block
    }

    TEST_ASSERT_EQUAL_INT(6, DsndBabylon_TestGetPushes());
    TEST_ASSERT_EQUAL_INT(0, DsndBabylon_TestGetRecreates());

    const std::optional<babylon::audio::StreamState> state = pDevice->streamState(Voice());
    TEST_ASSERT_TRUE(state.has_value());
    TEST_ASSERT_EQUAL_UINT64(0, state->queuedFrames);
    TEST_ASSERT_EQUAL_UINT64((uint64_t)6 * DSND_BLOCKFRAMES, state->playedFrames);
}

TEST(SmushDsndBabylon, QueueGrowsButStaysUnderTheHighMarkWhenTheDeviceLags)
{
    StartDriver();
    for ( int i = 0; i < 10; ++i )
    {
        AdvanceUntilFills(numFills + 1);
        pDevice->advanceFrames(3000); // 1378 source frames, slower than the pushed block

        const std::optional<babylon::audio::StreamState> state = pDevice->streamState(Voice());
        TEST_ASSERT_TRUE(state.has_value());
        TEST_ASSERT_LESS_OR_EQUAL_UINT64((uint64_t)6 * DSND_BLOCKFRAMES, state->queuedFrames);
    }

    TEST_ASSERT_EQUAL_INT(10, DsndBabylon_TestGetPushes());
    TEST_ASSERT_EQUAL_INT(0, DsndBabylon_TestGetRecreates());
}

TEST(SmushDsndBabylon, StopDestroysTheVoiceAndStartStopStartWorks)
{
    StartDriver();
    AdvanceUntilFills(2);

    const babylon::audio::VoiceHandle first = Voice();
    TEST_ASSERT_FALSE(first.isNull());
    TEST_ASSERT_EQUAL_INT(1, DsndBabylon_TestGetVoicesCreated());

    pDriver->Stop();
    bStarted = 0;
    TEST_ASSERT_TRUE(Voice().isNull());

    pDriver->SetVolume(64);
    StartDriver();
    AdvanceUntilFills(4); // two more after the restart

    const babylon::audio::VoiceHandle second = Voice();
    TEST_ASSERT_FALSE(second.isNull());
    TEST_ASSERT_TRUE(!(second == first));
    TEST_ASSERT_EQUAL_INT(2, DsndBabylon_TestGetVoicesCreated());
    TEST_ASSERT_EQUAL_INT(4, DsndBabylon_TestGetPushes());

    const std::optional<babylon::audio::StreamState> state = pDevice->streamState(second);
    TEST_ASSERT_TRUE(state.has_value());
    TEST_ASSERT_EQUAL_UINT64((uint64_t)2 * DSND_BLOCKFRAMES, state->queuedFrames);

    const std::optional<babylon::audio::VoiceMix> mix = pDevice->voiceMix(second);
    TEST_ASSERT_TRUE(mix.has_value());
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, ExpectedGain(-1000), mix->gain);

    pDriver->Stop();
    bStarted = 0;
    TEST_ASSERT_TRUE(Voice().isNull());
}

TEST(SmushDsndBabylon, PacingMatchesTheSilentDriverSchedule)
{
    StartDriver();

    const unsigned int end = kStartMsec + 10000u;
    while ( nowMsec < end )
    {
        timer_Update();
    }

    // The same numbers DsndSilent.c's schedule produced: 600 ticks of 0x4DAE are 143 blocks of 0x600
    // frames, and the ring runs at most two blocks ahead of the one playing.
    TEST_ASSERT_EQUAL_INT(600 * TIMER_TICKLEN, timer_GetTicks());
    TEST_ASSERT_GREATER_OR_EQUAL_INT(143, numFills);
    TEST_ASSERT_LESS_OR_EQUAL_INT(145, numFills);
    TEST_ASSERT_EQUAL_INT(numFills * DSND_BLOCKFRAMES, numFillFrames);

    // Nothing consumes, so the first six blocks are queued and every later fill skips its push.
    TEST_ASSERT_EQUAL_INT(6, DsndBabylon_TestGetPushes());
    TEST_ASSERT_EQUAL_INT(numFills - 6, DsndBabylon_TestGetSkipped());
    TEST_ASSERT_EQUAL_INT(0, DsndBabylon_TestGetRecreates());
    TEST_ASSERT_EQUAL_UINT64((uint64_t)6 * DSND_BLOCKSIZE, DsndBabylon_TestGetBytesPushed());
}

TEST_GROUP_RUNNER(SmushDsndBabylon)
{
    RUN_TEST_CASE(SmushDsndBabylon, StartPushesOneBlockPerDueFillWithTheDx9Volume);
    RUN_TEST_CASE(SmushDsndBabylon, QueueSkipsOneBlockPerPushAboveTheHighMark);
    RUN_TEST_CASE(SmushDsndBabylon, QueueUnderrunsWhenTheDeviceConsumesFaster);
    RUN_TEST_CASE(SmushDsndBabylon, QueueGrowsButStaysUnderTheHighMarkWhenTheDeviceLags);
    RUN_TEST_CASE(SmushDsndBabylon, StopDestroysTheVoiceAndStartStopStartWorks);
    RUN_TEST_CASE(SmushDsndBabylon, PacingMatchesTheSilentDriverSchedule);
}
