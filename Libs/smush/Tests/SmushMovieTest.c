// Added: movie playback on the Babylon column (M6.8): the sound driver (DsndBabylon.cpp), the polled timer
// (TimerPolled.c) and SmushPlay end to end on a synthetic SMUSH stream, all on a fake millisecond clock injected through
// TimerPolled_SetClockForTest. The Null column keeps DsndNull.c and the tick thread, where movies are skipped
// (SmushCore.SmushPlay_PlayMovie_NoStartup), so this group is empty there.
#include <unity_fixture.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <j3dcore/j3d.h>

#include <smush/SmushPlay.h>
#include <smush/insane/File/Framestream.h>
#include <smush/insane/File/Stdio.h>
#include <smush/insane/Flobject/D_bloc16.h>
#include <smush/insane/Lec/LecShare.h>
#include <smush/insane/Memory/Memory.h>
#include <smush/insane/Sound/Dsnd.h>
#include <smush/insane/Timer/Timer.h>
#include <smush/insane/Timer/TimerSys.h>

TEST_GROUP(SmushMovie);

#if defined(J3D_PLATFORM_BABYLON)

void Engine_TestReset(void); // Engine.c, compiled into this executable with J3D_TEST

// ---- The fake clock: time moves only through the timer's idle sleep and the tests ----

static tStdTime SmushMovieTest_now;

static unsigned int SmushMovieTest_Now(void)
{
    return SmushMovieTest_now;
}

static void J3DAPI SmushMovieTest_Sleep(uint32_t msec)
{
    SmushMovieTest_now += msec;
}

static void SmushMovieTest_ErrorFn(const char* pFormat, ...)
{
    (void)pFormat;
}

static int SmushMovieTest_bShare;

#define SMUSHMOVIETEST_START_MSEC 5000u

TEST_SETUP(SmushMovie)
{
    Smush_ResetGlobals();
    Engine_TestReset();
    SmushMovieTest_now = SMUSHMOVIETEST_START_MSEC;
    TimerPolled_SetClockForTest(SmushMovieTest_Now, SmushMovieTest_Sleep);
    SmushMovieTest_bShare = 0;
}

TEST_TEAR_DOWN(SmushMovie)
{
    if ( SmushMovieTest_bShare )
    {
        LecShare_Shutdown();
        LecShare_UnregisterAll();
    }

    TimerPolled_SetClockForTest(NULL, NULL);
    Smush_ResetGlobals();
    Engine_TestReset();
}

// ---- Silent driver ----

static int SmushMovieTest_numFills;
static int SmushMovieTest_numFilledFrames;
static int SmushMovieTest_lastFillSize;

static void SmushMovieTest_Fill(int n, SoundFeed pfnFeed)
{
    TEST_ASSERT_NOT_NULL(pfnFeed);
    SmushMovieTest_numFills++;
    SmushMovieTest_numFilledFrames += n;
    SmushMovieTest_lastFillSize = n;
}

TEST(SmushMovie, SilentDriver_SubscribesAndConsumesAtTheStreamRate)
{
    LecModuleEntry aModules[] = { { memory_Register, NULL }, { timer_Register, NULL }, { dsnd_Register, NULL }, { NULL, NULL } };
    TEST_ASSERT_NOT_NULL(LecShare_Init(SmushMovieTest_ErrorFn, aModules, "MEMORY=insane", ""));
    SmushMovieTest_bShare = 1;
    TEST_ASSERT_EQUAL_INT(0, LecShare_Startup()); // dsnd_Startup subscribes TIMER=clock and MEMORY.mixertable

    // The engine finds it the way Engine.c subscribes it
    SndDriver* pDriver = (SndDriver*)LecShare_Subscribe("SNDDRIVER.driver", NULL);
    TEST_ASSERT_NOT_NULL(pDriver);
    TEST_ASSERT_TRUE(pDriver->Start == dsnd_Start);
    TEST_ASSERT_EQUAL_INT(22050, pDriver->GetRate());
    pDriver->SetVolume(0x85);
    TEST_ASSERT_EQUAL_INT(5, pDriver->GetVolume());

    SmushMovieTest_numFills = SmushMovieTest_numFilledFrames = SmushMovieTest_lastFillSize = 0;
    TEST_ASSERT_EQUAL_INT(0, pDriver->Start(11025, 1, SmushMovieTest_Fill)); // below 22050 is raised to it
    TEST_ASSERT_EQUAL_INT(0, SmushMovieTest_numFills);

    // Ten seconds of polling, as the movie loop does (each idle poll sleeps a millisecond on the fake clock)
    const tStdTime end = SMUSHMOVIETEST_START_MSEC + 10000u;
    while ( SmushMovieTest_now < end )
    {
        timer_Update();
    }

    // 600 ticks of 0x4DAE are 11931600 timer ticks, 220496 stream frames at 22050 Hz: 143 blocks of 0x600 frames played.
    // The block schedule keeps the ring filled one block short of the one playing, so it runs up to two blocks ahead.
    TEST_ASSERT_EQUAL_INT(600 * TIMER_TICKLEN, timer_GetTicks());
    TEST_ASSERT_EQUAL_INT(DSND_BLOCKFRAMES, SmushMovieTest_lastFillSize);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(143, SmushMovieTest_numFills);
    TEST_ASSERT_LESS_OR_EQUAL_INT(143 + 2, SmushMovieTest_numFills);
    TEST_ASSERT_EQUAL_INT(SmushMovieTest_numFills * DSND_BLOCKFRAMES, SmushMovieTest_numFilledFrames);

    // Stopped, it consumes nothing more
    pDriver->Stop();
    int numFills = SmushMovieTest_numFills;
    while ( SmushMovieTest_now < end + 1000u )
    {
        timer_Update();
    }

    TEST_ASSERT_EQUAL_INT(numFills, SmushMovieTest_numFills);
}

// ---- Polled timer ----

TEST(SmushMovie, PolledTimer_TicksAtTheThreadRateOnThePlatformClock)
{
    LecModuleEntry aModules[] = { { memory_Register, NULL }, { timer_Register, NULL }, { NULL, NULL } };
    TEST_ASSERT_NOT_NULL(LecShare_Init(SmushMovieTest_ErrorFn, aModules, "MEMORY=insane", ""));
    SmushMovieTest_bShare = 1;
    TEST_ASSERT_EQUAL_INT(0, LecShare_Startup());

    // The first tick is due at the start, like the Win32 thread's; then nothing until the next step, and the idle poll
    // yields one millisecond
    TEST_ASSERT_EQUAL_INT(1, timer_Update());
    TEST_ASSERT_EQUAL_UINT32(SMUSHMOVIETEST_START_MSEC, SmushMovieTest_now);
    TEST_ASSERT_EQUAL_INT(0, timer_Update());
    TEST_ASSERT_EQUAL_UINT32(SMUSHMOVIETEST_START_MSEC + 1u, SmushMovieTest_now);
    TEST_ASSERT_EQUAL_INT(TIMER_TICKLEN, timer_GetTicks());

    // 1000 / 60 ms per tick, the remainder carried: exactly 30 ticks per half second, 60 per second
    SmushMovieTest_now = SMUSHMOVIETEST_START_MSEC + 499u;
    TEST_ASSERT_EQUAL_INT(29, timer_Update());
    SmushMovieTest_now = SMUSHMOVIETEST_START_MSEC + 500u;
    TEST_ASSERT_EQUAL_INT(1, timer_Update());
    SmushMovieTest_now = SMUSHMOVIETEST_START_MSEC + 1000u;
    TEST_ASSERT_EQUAL_INT(30, timer_Update());
    TEST_ASSERT_EQUAL_INT(61 * TIMER_TICKLEN, timer_GetTicks());
    SmushMovieTest_now = SMUSHMOVIETEST_START_MSEC + 3000u;
    TEST_ASSERT_EQUAL_INT(60, timer_Update()); // a stall: one second of backlog, then the schedule resumes from now
    SmushMovieTest_now += 17u;
    TEST_ASSERT_EQUAL_INT(1, timer_Update());

    // Stopped, it no longer ticks or sleeps
    LecShare_Shutdown();
    LecShare_UnregisterAll();
    SmushMovieTest_bShare = 0;
    tStdTime now = SmushMovieTest_now += 100u;
    TEST_ASSERT_EQUAL_INT(0, timer_Update());
    TEST_ASSERT_EQUAL_UINT32(now, SmushMovieTest_now);
}

// ---- SmushPlay ----

TEST(SmushMovie, SysStartupSucceedsAndShutsDown)
{
    // Every INSANE module starts, the sound module included (it subscribes the silent SNDDRIVER), and the services the
    // player drives are subscribed.
    TEST_ASSERT_EQUAL_INT(1, SmushPlay_SysStartup(NULL, NULL));
    SmushPlay_SysShutdown();
}

// A synthetic SMUSH stream: "ANIM", an SHDR with the frame count, then numFrames FRME chunks of one raw (codec 0) Bl16
// object each, every pixel of frame i set to i.
#define SMUSHMOVIETEST_W       8
#define SMUSHMOVIETEST_H       8
#define SMUSHMOVIETEST_PATH    "smush_movie_test.snm"
#define SMUSHMOVIETEST_BL16    (offsetof(Bl16Header, data) + SMUSHMOVIETEST_W * SMUSHMOVIETEST_H * 2)
#define SMUSHMOVIETEST_FRME    (8 + SMUSHMOVIETEST_BL16)

static void SmushMovieTest_PutBE32(uint8_t* pOut, uint32_t value)
{
    pOut[0] = (uint8_t)(value >> 24);
    pOut[1] = (uint8_t)(value >> 16);
    pOut[2] = (uint8_t)(value >> 8);
    pOut[3] = (uint8_t)value;
}

static void SmushMovieTest_WriteChunk(int fh, const char* pTag, uint32_t size)
{
    uint8_t aHead[8];
    memcpy(aHead, pTag, 4);
    SmushMovieTest_PutBE32(aHead + 4, size);
    TEST_ASSERT_EQUAL_INT(8, stdio_write(fh, aHead, 8));
}

static void SmushMovieTest_WriteStream(int numFrames)
{
    int fh = stdio_open(SMUSHMOVIETEST_PATH, "wb");
    TEST_ASSERT_NOT_EQUAL(0, fh);

    SmushMovieTest_WriteChunk(fh, "ANIM", (uint32_t)(8 + sizeof(FrameHeader) + numFrames * (8 + SMUSHMOVIETEST_FRME)));

    FrameHeader header;
    memset(&header, 0, sizeof(header));
    header.count = (int16_t)numFrames;
    SmushMovieTest_WriteChunk(fh, "SHDR", sizeof(header));
    TEST_ASSERT_EQUAL_INT((int)sizeof(header), stdio_write(fh, &header, sizeof(header)));

    static uint8_t aObject[SMUSHMOVIETEST_BL16];
    for ( int i = 0; i < numFrames; ++i )
    {
        memset(aObject, 0, sizeof(aObject));
        Bl16Header* pHdr = (Bl16Header*)aObject;
        pHdr->width  = SMUSHMOVIETEST_W;
        pHdr->height = SMUSHMOVIETEST_H;
        pHdr->seq    = (int16_t)i;
        pHdr->codec  = BL16_CODEC_RAW;
        pHdr->depth  = 3; // LECBITMAP_TYPE_RGB565
        uint16_t* pPixels = (uint16_t*)(aObject + offsetof(Bl16Header, data));
        for ( int p = 0; p < SMUSHMOVIETEST_W * SMUSHMOVIETEST_H; ++p )
        {
            pPixels[p] = (uint16_t)i;
        }

        SmushMovieTest_WriteChunk(fh, "FRME", SMUSHMOVIETEST_FRME);
        SmushMovieTest_WriteChunk(fh, "Bl16", SMUSHMOVIETEST_BL16);
        TEST_ASSERT_EQUAL_INT((int)sizeof(aObject), stdio_write(fh, aObject, sizeof(aObject)));
    }

    stdio_close(fh);
}

#define SMUSHMOVIETEST_MAXBLITS 128

static int SmushMovieTest_numBlits;
static int SmushMovieTest_abortAt;
static tStdTime SmushMovieTest_aBlitTime[SMUSHMOVIETEST_MAXBLITS];
static int SmushMovieTest_aBlitPixel[SMUSHMOVIETEST_MAXBLITS];

static int J3DAPI SmushMovieTest_Blit(const SmushBitmap* pBitmap, int frame)
{
    J3D_UNUSED(frame);
    if ( SmushMovieTest_numBlits < SMUSHMOVIETEST_MAXBLITS )
    {
        SmushMovieTest_aBlitTime[SmushMovieTest_numBlits]  = SmushMovieTest_now;
        SmushMovieTest_aBlitPixel[SmushMovieTest_numBlits] = *(const uint16_t*)pBitmap->pPixels;
    }

    SmushMovieTest_numBlits++;
    return SmushMovieTest_numBlits == SmushMovieTest_abortAt; // the game's blit answers 1 for a skip or a quit
}

static int SmushMovieTest_Play(int numFrames, int abortAt)
{
    SmushMovieTest_WriteStream(numFrames);
    SmushMovieTest_numBlits = 0;
    SmushMovieTest_abortAt  = abortAt;

    TEST_ASSERT_EQUAL_INT(1, SmushPlay_SysStartup(NULL, NULL));
    int rc = SmushPlay_PlayMovie(SMUSHMOVIETEST_PATH, 15, 0, 0, 0, SMUSHMOVIETEST_W, SMUSHMOVIETEST_H, -1, 0, SmushMovieTest_Blit, 1,
        1000000, 1000000);
    SmushPlay_SysShutdown();
    remove(SMUSHMOVIETEST_PATH);
    return rc;
}

TEST(SmushMovie, PlayMoviePlaysASyntheticStreamToTheEndAt15Fps)
{
    const int numFrames = 60;
    TEST_ASSERT_EQUAL_INT(1, SmushMovieTest_Play(numFrames, 0));

    // Every frame is blitted once, in order
    TEST_ASSERT_EQUAL_INT(numFrames, SmushMovieTest_numBlits);
    for ( int i = 0; i < numFrames; ++i )
    {
        TEST_ASSERT_EQUAL_INT(i, SmushMovieTest_aBlitPixel[i]);
    }

    // At 15 fps on the fake clock: 59 frame intervals take 59 / 15 s = 3933 ms. A frame waits for whole timer ticks
    // (1000 / 60 ms): the frame interval of 79545 timer ticks is just over 4 ticks of 0x4DAE, so frames come after 5
    // ticks until the lead correction (interval2, clamped to 3/4 and 4/3 of the interval) shortens one to 3 or stretches
    // one to 6; the average stays within 2.5% (measured 4015 ms here, the same arithmetic as the original's 60 Hz tick
    // thread).
    tStdTime span = SmushMovieTest_aBlitTime[numFrames - 1] - SmushMovieTest_aBlitTime[0];
    TEST_ASSERT_UINT32_WITHIN(100u, 3933u, span);
    // Every interval is 3 to 6 ticks (50 to 100 ms)
    for ( int i = 1; i < numFrames; ++i )
    {
        tStdTime interval = SmushMovieTest_aBlitTime[i] - SmushMovieTest_aBlitTime[i - 1];
        TEST_ASSERT_UINT32_WITHIN(25u, 75u, interval);
    }
}

TEST(SmushMovie, PlayMovieStopsWhenTheBlitAsksToAbort)
{
    // smushplay_Blit stores the game blit's answer in smushplay_bAbort; the Pace loop ends the movie on it
    TEST_ASSERT_EQUAL_INT(1, SmushMovieTest_Play(60, 5));
    TEST_ASSERT_EQUAL_INT(5, SmushMovieTest_numBlits);
}

#else // J3D_PLATFORM_BABYLON

TEST_SETUP(SmushMovie)
{}

TEST_TEAR_DOWN(SmushMovie)
{}

#endif // J3D_PLATFORM_BABYLON

TEST_GROUP_RUNNER(SmushMovie)
{
#if defined(J3D_PLATFORM_BABYLON)
    RUN_TEST_CASE(SmushMovie, SilentDriver_SubscribesAndConsumesAtTheStreamRate);
    RUN_TEST_CASE(SmushMovie, PolledTimer_TicksAtTheThreadRateOnThePlatformClock);
    RUN_TEST_CASE(SmushMovie, SysStartupSucceedsAndShutsDown);
    RUN_TEST_CASE(SmushMovie, PlayMoviePlaysASyntheticStreamToTheEndAt15Fps);
    RUN_TEST_CASE(SmushMovie, PlayMovieStopsWhenTheBlitAsksToAbort);
#endif
}
