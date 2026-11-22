#include <unity_fixture.h>

#include <math.h>
#include <string.h>

#include <Jones3D/Main/JonesFixedTick.h>
#include <sith/Gameplay/sithTime.h>

TEST_GROUP(JonesFixedTick);

TEST_SETUP(JonesFixedTick)
{
    JonesFixedTick_SetRateOverride(-1);
    JonesFixedTick_ResetGlobals();
}

TEST_TEAR_DOWN(JonesFixedTick)
{
    JonesFixedTick_SetRateOverride(-1);
    JonesFixedTick_ResetGlobals();
}

static rdMatrix34 JonesFixedTickTest_Identity(void)
{
    rdMatrix34 mat;
    memset(&mat, 0, sizeof(mat));
    mat.rvec.x = 1.0f;
    mat.lvec.y = 1.0f;
    mat.uvec.z = 1.0f;
    return mat;
}

static rdMatrix34 JonesFixedTickTest_RotateZ90(void)
{
    // rvec = +y, lvec = -x, uvec = +z: a +90 degree turn around z.
    rdMatrix34 mat;
    memset(&mat, 0, sizeof(mat));
    mat.rvec.y = 1.0f;
    mat.lvec.x = -1.0f;
    mat.uvec.z = 1.0f;
    return mat;
}

TEST(JonesFixedTick, RateClampsToTheAllowedSet)
{
    TEST_ASSERT_EQUAL_INT(0, JonesFixedTick_ClampRate(-5));
    TEST_ASSERT_EQUAL_INT(0, JonesFixedTick_ClampRate(0));
    TEST_ASSERT_EQUAL_INT(0, JonesFixedTick_ClampRate(1));
    TEST_ASSERT_EQUAL_INT(0, JonesFixedTick_ClampRate(15));  // tie between 0 and 30 snaps down
    TEST_ASSERT_EQUAL_INT(30, JonesFixedTick_ClampRate(29));
    TEST_ASSERT_EQUAL_INT(30, JonesFixedTick_ClampRate(30));
    TEST_ASSERT_EQUAL_INT(30, JonesFixedTick_ClampRate(40)); // tie between 30 and 50 snaps down
    TEST_ASSERT_EQUAL_INT(50, JonesFixedTick_ClampRate(45));
    TEST_ASSERT_EQUAL_INT(50, JonesFixedTick_ClampRate(50));
    TEST_ASSERT_EQUAL_INT(50, JonesFixedTick_ClampRate(55)); // tie between 50 and 60 snaps down
    TEST_ASSERT_EQUAL_INT(60, JonesFixedTick_ClampRate(60));
    TEST_ASSERT_EQUAL_INT(60, JonesFixedTick_ClampRate(90)); // tie between 60 and 120 snaps down
    TEST_ASSERT_EQUAL_INT(120, JonesFixedTick_ClampRate(100));
    TEST_ASSERT_EQUAL_INT(120, JonesFixedTick_ClampRate(120));
    TEST_ASSERT_EQUAL_INT(120, JonesFixedTick_ClampRate(1000));
}

TEST(JonesFixedTick, RateToStepUsesIntegerMilliseconds)
{
    TEST_ASSERT_EQUAL_UINT32(0, JonesFixedTick_RateToStepMsec(0));
    TEST_ASSERT_EQUAL_UINT32(33, JonesFixedTick_RateToStepMsec(30));
    TEST_ASSERT_EQUAL_UINT32(20, JonesFixedTick_RateToStepMsec(50));
    TEST_ASSERT_EQUAL_UINT32(16, JonesFixedTick_RateToStepMsec(60));
    TEST_ASSERT_EQUAL_UINT32(8, JonesFixedTick_RateToStepMsec(120));
}

TEST(JonesFixedTick, AccumulatorRunsATickPerElapsedStep)
{
    JonesFixedTickAccumulator accum;
    JonesFixedTick_AccumReset(&accum);

    TEST_ASSERT_EQUAL_INT(0, JonesFixedTick_AccumAdvance(&accum, 1000, false, 16, JONESFIXEDTICK_MAX_CATCHUP));
    TEST_ASSERT_EQUAL_INT(0, JonesFixedTick_AccumAdvance(&accum, 1008, false, 16, JONESFIXEDTICK_MAX_CATCHUP));
    TEST_ASSERT_EQUAL_FLOAT(0.5f, JonesFixedTick_AccumAlpha(&accum, 16));
    TEST_ASSERT_EQUAL_INT(1, JonesFixedTick_AccumAdvance(&accum, 1016, false, 16, JONESFIXEDTICK_MAX_CATCHUP));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, JonesFixedTick_AccumAlpha(&accum, 16));
    TEST_ASSERT_EQUAL_INT(3, JonesFixedTick_AccumAdvance(&accum, 1064, false, 16, JONESFIXEDTICK_MAX_CATCHUP));
}

TEST(JonesFixedTick, AccumulatorCapsCatchUpAndDropsTheBacklog)
{
    JonesFixedTickAccumulator accum;
    JonesFixedTick_AccumReset(&accum);

    TEST_ASSERT_EQUAL_INT(0, JonesFixedTick_AccumAdvance(&accum, 1000, false, 16, JONESFIXEDTICK_MAX_CATCHUP));
    // 500 ms is clamped to sithTime's 200 ms, which is 12 ticks; the cap gives 5 and drops the rest.
    TEST_ASSERT_EQUAL_INT(5, JonesFixedTick_AccumAdvance(&accum, 1500, false, 16, JONESFIXEDTICK_MAX_CATCHUP));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, JonesFixedTick_AccumAlpha(&accum, 16));
    TEST_ASSERT_EQUAL_INT(2, JonesFixedTick_AccumAdvance(&accum, 1536, false, 16, JONESFIXEDTICK_MAX_CATCHUP));
}

TEST(JonesFixedTick, AccumulatorHandlesAZeroDeltaAndPause)
{
    JonesFixedTickAccumulator accum;
    JonesFixedTick_AccumReset(&accum);

    TEST_ASSERT_EQUAL_INT(0, JonesFixedTick_AccumAdvance(&accum, 1000, false, 16, JONESFIXEDTICK_MAX_CATCHUP));

    // A zero delta still gets sithTime's 1 ms floor, which is below one step.
    TEST_ASSERT_EQUAL_INT(0, JonesFixedTick_AccumAdvance(&accum, 1000, false, 16, JONESFIXEDTICK_MAX_CATCHUP));
    TEST_ASSERT_EQUAL_FLOAT(1.0f / 16.0f, JonesFixedTick_AccumAlpha(&accum, 16));

    // Paused clears the accumulator and never runs a tick.
    TEST_ASSERT_EQUAL_INT(0, JonesFixedTick_AccumAdvance(&accum, 1200, true, 16, JONESFIXEDTICK_MAX_CATCHUP));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, JonesFixedTick_AccumAlpha(&accum, 16));
    TEST_ASSERT_EQUAL_INT(1, JonesFixedTick_AccumAdvance(&accum, 1216, false, 16, JONESFIXEDTICK_MAX_CATCHUP));
}

TEST(JonesFixedTick, LerpVectorBlendsEachComponent)
{
    const rdVector3 a = { 0.0f, 10.0f, -4.0f };
    const rdVector3 b = { 4.0f, 20.0f, 4.0f };
    rdVector3 out;

    JonesFixedTick_LerpVector(&out, &a, &b, 0.25f);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, out.x);
    TEST_ASSERT_EQUAL_FLOAT(12.5f, out.y);
    TEST_ASSERT_EQUAL_FLOAT(-2.0f, out.z);
}

TEST(JonesFixedTick, LerpOrientReOrthonormalisesAndKeepsPosition)
{
    rdMatrix34 a = JonesFixedTickTest_Identity();
    a.dvec.x = 1.0f;
    a.dvec.y = 2.0f;
    a.dvec.z = 3.0f;

    rdMatrix34 b = JonesFixedTickTest_RotateZ90();
    b.dvec.x = 5.0f;
    b.dvec.y = 6.0f;
    b.dvec.z = 7.0f;

    rdMatrix34 out;
    JonesFixedTick_LerpOrient(&out, &a, &b, 0.5f);

    TEST_ASSERT_EQUAL_FLOAT(3.0f, out.dvec.x);
    TEST_ASSERT_EQUAL_FLOAT(4.0f, out.dvec.y);
    TEST_ASSERT_EQUAL_FLOAT(5.0f, out.dvec.z);

    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, rdVector_Len3(&out.rvec));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, rdVector_Len3(&out.lvec));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, rdVector_Len3(&out.uvec));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, rdVector_Dot3(&out.rvec, &out.lvec));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, rdVector_Dot3(&out.rvec, &out.uvec));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, rdVector_Dot3(&out.lvec, &out.uvec));
}

TEST(JonesFixedTick, BlendThingInterpolatesAndRestoresExactPostTickValues)
{
    SithThing thing;
    memset(&thing, 0, sizeof(thing));
    thing.type      = SITH_THING_ACTOR;
    thing.signature = 7;
    thing.pos.x     = 2.0f;
    thing.pos.y     = 3.0f;
    thing.flags     = SITH_TF_SEEN;
    thing.renderFrame = 42;

    JonesFixedTickThingState cur;
    JonesFixedTick_CaptureThing(&cur, &thing);

    JonesFixedTickThingState prev;
    memset(&prev, 0, sizeof(prev));
    prev.valid     = true;
    prev.type      = SITH_THING_ACTOR;
    prev.signature = 7;
    prev.pos.x     = 1.0f;
    prev.pos.y     = 2.0f;
    prev.orient    = JonesFixedTickTest_Identity();
    cur.orient     = JonesFixedTickTest_Identity();

    TEST_ASSERT_TRUE(JonesFixedTick_BlendThing(&prev, &cur, &thing, 0.5f));
    TEST_ASSERT_EQUAL_FLOAT(1.5f, thing.pos.x);
    TEST_ASSERT_EQUAL_FLOAT(2.5f, thing.pos.y);

    JonesFixedTick_RestoreThing(&cur, &thing);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, thing.pos.x);
    TEST_ASSERT_EQUAL_FLOAT(3.0f, thing.pos.y);
    TEST_ASSERT_EQUAL_MEMORY(&cur.orient, &thing.orient, sizeof(thing.orient));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)cur.flags, (uint32_t)thing.flags);
    TEST_ASSERT_EQUAL_size_t(cur.renderFrame, thing.renderFrame);
}

TEST(JonesFixedTick, BlendThingNeverBlendsAcrossASignatureChange)
{
    SithThing thing;
    memset(&thing, 0, sizeof(thing));
    thing.type      = SITH_THING_ACTOR;
    thing.signature = 8;
    thing.pos.x     = 10.0f;

    JonesFixedTickThingState prev;
    memset(&prev, 0, sizeof(prev));
    prev.valid     = true;
    prev.type      = SITH_THING_ACTOR;
    prev.signature = 7;
    prev.pos.x     = 0.0f;

    JonesFixedTickThingState cur;
    memset(&cur, 0, sizeof(cur));
    cur.valid     = true;
    cur.type      = SITH_THING_ACTOR;
    cur.signature = 7;
    cur.pos.x     = 5.0f;

    TEST_ASSERT_FALSE(JonesFixedTick_BlendThing(&prev, &cur, &thing, 0.5f));
    TEST_ASSERT_EQUAL_FLOAT(10.0f, thing.pos.x);
}

TEST(JonesFixedTick, BlendThingSnapsACreatedThingAndATeleport)
{
    SithThing thing;
    memset(&thing, 0, sizeof(thing));
    thing.type      = SITH_THING_ACTOR;
    thing.signature = 7;
    thing.pos.x     = 10.0f;

    JonesFixedTickThingState cur;
    JonesFixedTick_CaptureThing(&cur, &thing);

    // No previous tick: the created thing is drawn at its current state.
    cur.orient = JonesFixedTickTest_Identity();
    TEST_ASSERT_TRUE(JonesFixedTick_BlendThing(NULL, &cur, &thing, 0.5f));
    TEST_ASSERT_EQUAL_FLOAT(10.0f, thing.pos.x);

    // A move beyond the teleport threshold is not blended.
    JonesFixedTickThingState prev;
    memset(&prev, 0, sizeof(prev));
    prev.valid     = true;
    prev.type      = SITH_THING_ACTOR;
    prev.signature = 7;
    prev.orient    = JonesFixedTickTest_Identity();
    prev.pos.x     = 0.0f;

    thing.pos.x = 5.0f;
    TEST_ASSERT_TRUE(JonesFixedTick_BlendThing(&prev, &cur, &thing, 0.5f));
    TEST_ASSERT_EQUAL_FLOAT(10.0f, thing.pos.x);
}

TEST(JonesFixedTick, BlendCameraInterpolatesAndRestoresFovAndAspect)
{
    SithCamera camera;
    memset(&camera, 0, sizeof(camera));
    camera.pos.x         = 1.0f;
    camera.orient        = JonesFixedTickTest_Identity();
    camera.orient.dvec.x = 1.0f;
    camera.fov           = 90.0f;
    camera.aspectRatio   = 1.3333333f;

    JonesFixedTickCameraState cur;
    JonesFixedTick_CaptureCamera(&cur, &camera);

    JonesFixedTickCameraState prev;
    memset(&prev, 0, sizeof(prev));
    prev.valid       = true;
    prev.pos.x       = -1.0f;
    prev.orient      = JonesFixedTickTest_Identity();
    prev.orient.dvec.x = -1.0f;
    prev.fov         = 70.0f;
    prev.aspectRatio = 1.0f;

    JonesFixedTick_BlendCamera(&prev, &cur, &camera, 0.5f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, camera.pos.x);
    TEST_ASSERT_EQUAL_FLOAT(80.0f, camera.fov);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.1666666f, camera.aspectRatio);

    JonesFixedTick_RestoreCamera(&cur, &camera);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, camera.pos.x);
    TEST_ASSERT_EQUAL_FLOAT(90.0f, camera.fov);
    TEST_ASSERT_EQUAL_FLOAT(1.3333333f, camera.aspectRatio);
    TEST_ASSERT_EQUAL_MEMORY(&cur.orient, &camera.orient, sizeof(camera.orient));
}

TEST(JonesFixedTick, ConfigureHonoursTheOptionOnlyInQOLBuilds)
{
    JonesFixedTick_SetRateOverride(45); // clamps to 50
    JonesFixedTick_Configure();

#ifdef J3D_QOL_IMPROVEMENTS
    TEST_ASSERT_TRUE(JonesFixedTick_IsEnabled());
    TEST_ASSERT_EQUAL_INT(50, JonesFixedTick_GetRateHz());
    TEST_ASSERT_EQUAL_UINT32(20, JonesFixedTick_GetStepMsec());
    TEST_ASSERT_EQUAL_INT(1, sithTime_g_bFixedStep);
#else
    TEST_ASSERT_FALSE(JonesFixedTick_IsEnabled());
    TEST_ASSERT_EQUAL_INT(0, JonesFixedTick_GetRateHz());
    TEST_ASSERT_EQUAL_UINT32(0, JonesFixedTick_GetStepMsec());
    TEST_ASSERT_EQUAL_INT(0, sithTime_g_bFixedStep);
#endif
}

TEST(JonesFixedTick, ConfigureAcceptsZeroAsOff)
{
    JonesFixedTick_SetRateOverride(0);
    JonesFixedTick_Configure();

    TEST_ASSERT_FALSE(JonesFixedTick_IsEnabled());
    TEST_ASSERT_EQUAL_INT(0, JonesFixedTick_GetRateHz());
    TEST_ASSERT_EQUAL_INT(0, sithTime_g_bFixedStep);
}

TEST_GROUP_RUNNER(JonesFixedTick)
{
    RUN_TEST_CASE(JonesFixedTick, RateClampsToTheAllowedSet);
    RUN_TEST_CASE(JonesFixedTick, RateToStepUsesIntegerMilliseconds);
    RUN_TEST_CASE(JonesFixedTick, AccumulatorRunsATickPerElapsedStep);
    RUN_TEST_CASE(JonesFixedTick, AccumulatorCapsCatchUpAndDropsTheBacklog);
    RUN_TEST_CASE(JonesFixedTick, AccumulatorHandlesAZeroDeltaAndPause);
    RUN_TEST_CASE(JonesFixedTick, LerpVectorBlendsEachComponent);
    RUN_TEST_CASE(JonesFixedTick, LerpOrientReOrthonormalisesAndKeepsPosition);
    RUN_TEST_CASE(JonesFixedTick, BlendThingInterpolatesAndRestoresExactPostTickValues);
    RUN_TEST_CASE(JonesFixedTick, BlendThingNeverBlendsAcrossASignatureChange);
    RUN_TEST_CASE(JonesFixedTick, BlendThingSnapsACreatedThingAndATeleport);
    RUN_TEST_CASE(JonesFixedTick, BlendCameraInterpolatesAndRestoresFovAndAspect);
    RUN_TEST_CASE(JonesFixedTick, ConfigureHonoursTheOptionOnlyInQOLBuilds);
    RUN_TEST_CASE(JonesFixedTick, ConfigureAcceptsZeroAsOff);
}
