#include <unity_fixture.h>

#include <math.h>
#include <string.h>

#include <Jones3D/Main/JonesFixedTick.h>
#include <rdroid/Engine/rdPuppet.h>
#include <rdroid/Raster/rdCache.h>
#include <sith/Engine/sithCamera.h>
#include <sith/Gameplay/sithTime.h>
#include <sith/World/sithWorld.h>

#include <std/General/std.h>
#include <std/General/stdPlatform.h>

TEST_GROUP(JonesFixedTick);

// Added: the draw tests build a world snapshot, which allocates through the engine allocator.
static tHostServices jonesFixedTickTest_hostServices;

TEST_SETUP(JonesFixedTick)
{
    JonesFixedTick_SetRateOverride(-1);
    JonesFixedTick_ResetGlobals();

    memset(&jonesFixedTickTest_hostServices, 0, sizeof(jonesFixedTickTest_hostServices));
    stdPlatform_InitServices(&jonesFixedTickTest_hostServices);
    stdStartup(&jonesFixedTickTest_hostServices);
}

TEST_TEAR_DOWN(JonesFixedTick)
{
    JonesFixedTick_SetRateOverride(-1);
    JonesFixedTick_ResetGlobals();

    stdShutdown();
    std_g_pHS = NULL;
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

TEST(JonesFixedTick, RateMapsToOffOrFifty)
{
    // The option is Off (0) or On at the game's 50 Hz physics rate. Any positive value, including
    // a rate an earlier test build wrote, means On at 50.
    TEST_ASSERT_EQUAL_INT(0, JonesFixedTick_ClampRate(0));
    TEST_ASSERT_EQUAL_INT(50, JonesFixedTick_ClampRate(1));
    TEST_ASSERT_EQUAL_INT(50, JonesFixedTick_ClampRate(30));
    TEST_ASSERT_EQUAL_INT(50, JonesFixedTick_ClampRate(50));
    TEST_ASSERT_EQUAL_INT(50, JonesFixedTick_ClampRate(60));
    TEST_ASSERT_EQUAL_INT(50, JonesFixedTick_ClampRate(120));
    TEST_ASSERT_EQUAL_INT(50, JonesFixedTick_ClampRate(1000));
}

TEST(JonesFixedTick, ConfigRateKeepsTheBuildDefaultForGarbage)
{
    TEST_ASSERT_EQUAL_INT(0, JonesFixedTick_ResolveConfigRate(0));
    TEST_ASSERT_EQUAL_INT(50, JonesFixedTick_ResolveConfigRate(50));
    TEST_ASSERT_EQUAL_INT(50, JonesFixedTick_ResolveConfigRate(30));
    TEST_ASSERT_EQUAL_INT(50, JonesFixedTick_ResolveConfigRate(60));
    TEST_ASSERT_EQUAL_INT(50, JonesFixedTick_ResolveConfigRate(120));

    // A missing key and a hand-edited negative value both use the build's default: on in Enhanced,
    // off in Retail and QOL-off builds.
    TEST_ASSERT_EQUAL_INT(J3D_QOL_VALUE(50, 0), JonesFixedTick_DefaultRate());
    TEST_ASSERT_EQUAL_INT(J3D_QOL_VALUE(50, 0), JonesFixedTick_ResolveConfigRate(-1));
    TEST_ASSERT_EQUAL_INT(J3D_QOL_VALUE(50, 0), JonesFixedTick_ResolveConfigRate(-999));
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

TEST(JonesFixedTick, BlendCameraInterpolatesAndRestoresFovAspectAndLook)
{
    SithCamera camera;
    memset(&camera, 0, sizeof(camera));

    // The previous tick.
    camera.pos.x         = -1.0f;
    camera.orient        = JonesFixedTickTest_Identity();
    camera.orient.dvec.x = -1.0f;
    camera.fov           = 70.0f;
    camera.aspectRatio   = 1.0f;
    camera.lookPos       = (rdVector3){ -1.0f, -2.0f, -3.0f };
    camera.focusPos      = (rdVector3){ -4.0f, -5.0f, -6.0f };
    sithCamera_g_camSpot     = (rdVector3){ -7.0f, -8.0f, -9.0f };
    sithCamera_g_camLookSpot = (rdVector3){ -10.0f, -11.0f, -12.0f };

    JonesFixedTickCameraState prev;
    JonesFixedTick_CaptureCamera(&prev, &camera);

    // The current tick.
    camera.pos.x         = 1.0f;
    camera.orient.dvec.x = 1.0f;
    camera.fov           = 90.0f;
    camera.aspectRatio   = 1.3333333f;
    camera.lookPos       = (rdVector3){ 1.0f, 2.0f, 3.0f };
    camera.focusPos      = (rdVector3){ 4.0f, 5.0f, 6.0f };
    sithCamera_g_camSpot     = (rdVector3){ 7.0f, 8.0f, 9.0f };
    sithCamera_g_camLookSpot = (rdVector3){ 10.0f, 11.0f, 12.0f };

    JonesFixedTickCameraState cur;
    JonesFixedTick_CaptureCamera(&cur, &camera);

    JonesFixedTick_BlendCamera(&prev, &cur, &camera, 0.5f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, camera.pos.x);
    TEST_ASSERT_EQUAL_FLOAT(80.0f, camera.fov);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.1666666f, camera.aspectRatio);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, camera.lookPos.x);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, camera.lookPos.y);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, camera.lookPos.z);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, camera.focusPos.x);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, camera.focusPos.y);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, camera.focusPos.z);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, sithCamera_g_camSpot.x);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, sithCamera_g_camLookSpot.z);

    JonesFixedTick_RestoreCamera(&cur, &camera);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, camera.pos.x);
    TEST_ASSERT_EQUAL_FLOAT(90.0f, camera.fov);
    TEST_ASSERT_EQUAL_FLOAT(1.3333333f, camera.aspectRatio);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, camera.lookPos.x);
    TEST_ASSERT_EQUAL_FLOAT(6.0f, camera.focusPos.z);
    TEST_ASSERT_EQUAL_FLOAT(7.0f, sithCamera_g_camSpot.x);
    TEST_ASSERT_EQUAL_FLOAT(12.0f, sithCamera_g_camLookSpot.z);
    TEST_ASSERT_EQUAL_MEMORY(&cur.orient, &camera.orient, sizeof(camera.orient));
}

TEST(JonesFixedTick, RestoreCameraRestoresTickSector)
{
    SithSector sectorA;
    SithSector sectorB;
    memset(&sectorA, 0, sizeof(sectorA));
    memset(&sectorB, 0, sizeof(sectorB));

    SithCamera camera;
    memset(&camera, 0, sizeof(camera));
    camera.pSector = &sectorA;

    JonesFixedTickCameraState cur;
    JonesFixedTick_CaptureCamera(&cur, &camera);
    TEST_ASSERT_EQUAL_PTR(&sectorA, cur.pSector);

    // No previous tick: the draw keeps the tick sector.
    JonesFixedTick_BlendCamera(NULL, &cur, &camera, 0.5f);
    TEST_ASSERT_EQUAL_PTR(&sectorA, camera.pSector);

    // A drawn frame may have moved the camera to another sector; the restore puts the tick's back.
    camera.pSector = &sectorB;
    JonesFixedTick_RestoreCamera(&cur, &camera);
    TEST_ASSERT_EQUAL_PTR(&sectorA, camera.pSector);
}

TEST(JonesFixedTick, BlendCameraTeleportKeepsTickSector)
{
    SithSector sectorA;
    SithSector sectorB;
    memset(&sectorA, 0, sizeof(sectorA));
    memset(&sectorB, 0, sizeof(sectorB));

    SithCamera camera;
    memset(&camera, 0, sizeof(camera));
    camera.pos.x         = 10.0f;
    camera.orient        = JonesFixedTickTest_Identity();
    camera.orient.dvec.x = 10.0f;
    camera.pSector       = &sectorA;

    JonesFixedTickCameraState cur;
    JonesFixedTick_CaptureCamera(&cur, &camera);

    JonesFixedTickCameraState prev;
    memset(&prev, 0, sizeof(prev));
    prev.valid     = true;
    prev.orient    = JonesFixedTickTest_Identity();
    prev.pSector   = &sectorB;

    // The pose moved beyond the teleport threshold, so the draw snaps to the current tick sector.
    camera.pSector = &sectorB;
    JonesFixedTick_BlendCamera(&prev, &cur, &camera, 0.5f);
    TEST_ASSERT_EQUAL_PTR(&sectorA, camera.pSector);

    // An invalid previous tick takes the same path.
    prev.valid     = false;
    camera.pSector = &sectorB;
    JonesFixedTick_BlendCamera(&prev, &cur, &camera, 0.5f);
    TEST_ASSERT_EQUAL_PTR(&sectorA, camera.pSector);
}

TEST(JonesFixedTick, ResolveCameraSectorNullArgsReturnFromSector)
{
    // With no world there is nothing to walk, so every call returns the sector it started from.
    SithWorld* pSavedWorld = sithWorld_g_pCurrentWorld;
    sithWorld_g_pCurrentWorld = NULL;

    SithSector sector;
    memset(&sector, 0, sizeof(sector));
    const rdVector3 from = { 0.0f, 0.0f, 0.0f };
    const rdVector3 to   = { 1.0f, 1.0f, 1.0f };

    SithSector* pNullSector   = JonesFixedTick_ResolveCameraSector(NULL, &from, &to);
    SithSector* pNullFrom     = JonesFixedTick_ResolveCameraSector(&sector, NULL, &to);
    SithSector* pNullPos      = JonesFixedTick_ResolveCameraSector(&sector, &from, NULL);
    SithSector* pNoWorldSector = JonesFixedTick_ResolveCameraSector(&sector, &from, &to);

    sithWorld_g_pCurrentWorld = pSavedWorld;

    TEST_ASSERT_EQUAL_PTR(NULL, pNullSector);
    TEST_ASSERT_EQUAL_PTR(&sector, pNullFrom);
    TEST_ASSERT_EQUAL_PTR(&sector, pNullPos);
    TEST_ASSERT_EQUAL_PTR(&sector, pNoWorldSector);
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

TEST(JonesFixedTick, ConfigureDevOverrideKeepsTheComparisonRates)
{
    // Added: the headless dev override is not part of the cfg mapping; 60 Hz stays 60 Hz so two
    // builds can still be compared at a rate other than the option's 50 Hz.
    JonesFixedTick_SetRateOverride(60);
    JonesFixedTick_Configure();

#ifdef J3D_QOL_IMPROVEMENTS
    TEST_ASSERT_TRUE(JonesFixedTick_IsEnabled());
    TEST_ASSERT_EQUAL_INT(60, JonesFixedTick_GetRateHz());
    TEST_ASSERT_EQUAL_UINT32(16, JonesFixedTick_GetStepMsec());
#else
    TEST_ASSERT_FALSE(JonesFixedTick_IsEnabled());
    TEST_ASSERT_EQUAL_INT(0, JonesFixedTick_GetRateHz());
#endif
}


TEST(JonesFixedTick, PuppetPoseSamplesBetweenTicksAndRestores)
{
    rdKeyframe keyframe;
    memset(&keyframe, 0, sizeof(keyframe));
    keyframe.numFrames = 100;

    rdThing renderData;
    memset(&renderData, 0, sizeof(renderData));

    rdPuppet puppet;
    memset(&puppet, 0, sizeof(puppet));
    puppet.pThing = &renderData;
    renderData.pPuppet = &puppet;

    puppet.aTracks[0].pKFTrack    = &keyframe;
    puppet.aTracks[0].playbackSpeed = 1.0f;

    SithThing thing;
    memset(&thing, 0, sizeof(thing));
    thing.type               = SITH_THING_ACTOR;
    thing.signature          = 9;
    thing.renderData.pPuppet = &puppet;

    puppet.aTracks[0].curFrame    = 1.0f;
    puppet.aTracks[0].blendWeight = 0.25f;
    JonesFixedTickThingState prev;
    JonesFixedTick_CaptureThing(&prev, &thing);

    puppet.aTracks[0].curFrame    = 5.0f;
    puppet.aTracks[0].blendWeight = 0.75f;
    JonesFixedTickThingState cur;
    JonesFixedTick_CaptureThing(&cur, &thing);

    TEST_ASSERT_TRUE(JonesFixedTick_BlendThing(&prev, &cur, &thing, 0.0f));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, puppet.aTracks[0].curFrame);
    TEST_ASSERT_EQUAL_FLOAT(0.25f, puppet.aTracks[0].blendWeight);
    JonesFixedTick_RestoreThing(&cur, &thing);
    TEST_ASSERT_EQUAL_FLOAT(5.0f, puppet.aTracks[0].curFrame);
    TEST_ASSERT_EQUAL_FLOAT(0.75f, puppet.aTracks[0].blendWeight);

    TEST_ASSERT_TRUE(JonesFixedTick_BlendThing(&prev, &cur, &thing, 1.0f));
    TEST_ASSERT_EQUAL_FLOAT(5.0f, puppet.aTracks[0].curFrame);
    TEST_ASSERT_EQUAL_FLOAT(0.75f, puppet.aTracks[0].blendWeight);
    JonesFixedTick_RestoreThing(&cur, &thing);

    TEST_ASSERT_TRUE(JonesFixedTick_BlendThing(&prev, &cur, &thing, 0.5f));
    TEST_ASSERT_EQUAL_FLOAT(3.0f, puppet.aTracks[0].curFrame);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, puppet.aTracks[0].blendWeight);
    JonesFixedTick_RestoreThing(&cur, &thing);
    TEST_ASSERT_EQUAL_FLOAT(5.0f, puppet.aTracks[0].curFrame);
    TEST_ASSERT_EQUAL_FLOAT(0.75f, puppet.aTracks[0].blendWeight);
}

static JonesFixedTickPuppetState JonesFixedTickTest_MakePuppetState(rdPuppet* pPuppet,
    rdKeyframe* pTrack0, float frame0, float blend0)
{
    JonesFixedTickPuppetState state;
    memset(&state, 0, sizeof(state));
    state.valid   = true;
    state.pPuppet = pPuppet;
    state.aTracks[0].valid       = pTrack0 != NULL;
    state.aTracks[0].pKFTrack    = pTrack0;
    state.aTracks[0].curFrame    = frame0;
    state.aTracks[0].blendWeight = blend0;
    return state;
}

TEST(JonesFixedTick, PuppetSameTrackContinuesWithoutPoseCrossfade)
{
    rdKeyframe keyframe;
    memset(&keyframe, 0, sizeof(keyframe));
    keyframe.numFrames = 100;

    rdPuppet puppet;
    memset(&puppet, 0, sizeof(puppet));

    JonesFixedTickPuppetState prev = JonesFixedTickTest_MakePuppetState(&puppet, &keyframe, 10.0f, 1.0f);
    JonesFixedTickPuppetState cur  = JonesFixedTickTest_MakePuppetState(&puppet, &keyframe, 14.0f, 1.0f);

    TEST_ASSERT_FALSE(JonesFixedTick_PuppetNeedsPoseCrossfade(&prev, &cur));
    TEST_ASSERT_TRUE(JonesFixedTick_PuppetCanPoseCrossfade(&prev, &cur));
}

TEST(JonesFixedTick, PuppetReplacedTrackNeedsPoseCrossfade)
{
    // A replaced keyframe has no relationship between its node poses and the old one, so the old
    // keyframe must never be sampled at the new track's frame. The complete tick poses crossfade.
    rdKeyframe keyframeA;
    rdKeyframe keyframeB;
    memset(&keyframeA, 0, sizeof(keyframeA));
    memset(&keyframeB, 0, sizeof(keyframeB));
    keyframeA.numFrames = 100;
    keyframeB.numFrames = 100;

    rdPuppet puppet;
    memset(&puppet, 0, sizeof(puppet));

    JonesFixedTickPuppetState prev = JonesFixedTickTest_MakePuppetState(&puppet, &keyframeA, 1.0f, 1.0f);
    JonesFixedTickPuppetState cur  = JonesFixedTickTest_MakePuppetState(&puppet, &keyframeB, 9.0f, 1.0f);

    TEST_ASSERT_TRUE(JonesFixedTick_PuppetNeedsPoseCrossfade(&prev, &cur));
    TEST_ASSERT_TRUE(JonesFixedTick_PuppetCanPoseCrossfade(&prev, &cur));

    // The normal per-track sampler only ever uses the new tick's own frame.
    puppet.aTracks[0].pKFTrack = &keyframeB;
    JonesFixedTick_BlendPuppet(&prev, &cur, &puppet, 0.5f);
    TEST_ASSERT_EQUAL_FLOAT(9.0f, puppet.aTracks[0].curFrame);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, puppet.aTracks[0].blendWeight);
}

TEST(JonesFixedTick, PuppetLoopNeedsPoseCrossfade)
{
    // A loop reset must blend the end pose to the start pose, never run the whole track backwards.
    rdKeyframe keyframe;
    memset(&keyframe, 0, sizeof(keyframe));
    keyframe.numFrames = 100;

    rdPuppet puppet;
    memset(&puppet, 0, sizeof(puppet));

    JonesFixedTickPuppetState prev = JonesFixedTickTest_MakePuppetState(&puppet, &keyframe, 90.0f, 1.0f);
    JonesFixedTickPuppetState cur  = JonesFixedTickTest_MakePuppetState(&puppet, &keyframe, 5.0f, 1.0f);

    TEST_ASSERT_TRUE(JonesFixedTick_PuppetNeedsPoseCrossfade(&prev, &cur));
    TEST_ASSERT_TRUE(JonesFixedTick_PuppetCanPoseCrossfade(&prev, &cur));
}

TEST(JonesFixedTick, PuppetAppearedOrRemovedTrackNeedsPoseCrossfade)
{
    rdKeyframe keyframe;
    memset(&keyframe, 0, sizeof(keyframe));
    keyframe.numFrames = 100;

    rdPuppet puppet;
    memset(&puppet, 0, sizeof(puppet));

    JonesFixedTickPuppetState none = JonesFixedTickTest_MakePuppetState(&puppet, NULL, 0.0f, 0.0f);
    JonesFixedTickPuppetState one  = JonesFixedTickTest_MakePuppetState(&puppet, &keyframe, 5.0f, 1.0f);

    TEST_ASSERT_TRUE(JonesFixedTick_PuppetNeedsPoseCrossfade(&none, &one));
    TEST_ASSERT_TRUE(JonesFixedTick_PuppetNeedsPoseCrossfade(&one, &none));
    TEST_ASSERT_TRUE(JonesFixedTick_PuppetCanPoseCrossfade(&none, &one));
    TEST_ASSERT_TRUE(JonesFixedTick_PuppetCanPoseCrossfade(&one, &none));
}

TEST(JonesFixedTick, PuppetUnknownOldFrameSnaps)
{
    // An out-of-range old frame cannot be sampled; the caller must snap to the current tick.
    rdKeyframe keyframe;
    memset(&keyframe, 0, sizeof(keyframe));
    keyframe.numFrames = 100;

    rdPuppet puppet;
    memset(&puppet, 0, sizeof(puppet));

    JonesFixedTickPuppetState prev = JonesFixedTickTest_MakePuppetState(&puppet, &keyframe, 101.0f, 1.0f);
    JonesFixedTickPuppetState cur  = JonesFixedTickTest_MakePuppetState(&puppet, &keyframe, 5.0f, 1.0f);

    TEST_ASSERT_TRUE(JonesFixedTick_PuppetNeedsPoseCrossfade(&prev, &cur));
    TEST_ASSERT_FALSE(JonesFixedTick_PuppetCanPoseCrossfade(&prev, &cur));

    prev.aTracks[0].curFrame = 100.0f;
    TEST_ASSERT_TRUE(JonesFixedTick_PuppetCanPoseCrossfade(&prev, &cur)); // the last frame is valid
}

TEST(JonesFixedTick, PuppetModelSwapSnaps)
{
    rdKeyframe keyframe;
    memset(&keyframe, 0, sizeof(keyframe));
    keyframe.numFrames = 100;

    rdPuppet puppet;
    memset(&puppet, 0, sizeof(puppet));

    SithThing thing;
    memset(&thing, 0, sizeof(thing));
    thing.type               = SITH_THING_ACTOR;
    thing.signature          = 11;
    thing.renderData.pPuppet = &puppet;

    JonesFixedTickThingState prev;
    JonesFixedTickThingState cur;
    memset(&prev, 0, sizeof(prev));
    memset(&cur, 0, sizeof(cur));
    prev.valid = true;
    cur.valid  = true;
    prev.type  = cur.type      = SITH_THING_ACTOR;
    prev.signature = cur.signature = 11;
    prev.pos = cur.pos;
    prev.orient = cur.orient = JonesFixedTickTest_Identity();
    prev.pModel3 = (const rdModel3*)0x1000;
    cur.pModel3  = (const rdModel3*)0x2000;
    JonesFixedTick_CapturePuppet(&prev.puppet, &puppet);
    cur.puppet = prev.puppet;
    cur.puppet.aTracks[0].valid       = true;
    cur.puppet.aTracks[0].pKFTrack    = &keyframe;
    cur.puppet.aTracks[0].curFrame    = 5.0f;
    cur.puppet.aTracks[0].blendWeight = 1.0f;
    prev.puppet.aTracks[0] = cur.puppet.aTracks[0];

    puppet.aTracks[0].pKFTrack    = &keyframe;
    puppet.aTracks[0].curFrame    = 5.0f;
    puppet.aTracks[0].blendWeight = 1.0f;

    // A different model cannot be pose-blended; the current tick's pose is drawn unchanged.
    TEST_ASSERT_TRUE(JonesFixedTick_BlendThing(&prev, &cur, &thing, 0.5f));
    TEST_ASSERT_EQUAL_FLOAT(5.0f, puppet.aTracks[0].curFrame);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, puppet.aTracks[0].blendWeight);
    JonesFixedTick_RestoreThing(&cur, &thing);
}

TEST(JonesFixedTick, JointMatrixBlendStaysOnTheTranslationSegment)
{
    rdMatrix34 prev = JonesFixedTickTest_Identity();
    rdMatrix34 cur  = JonesFixedTickTest_Identity();
    prev.dvec.x = 1.0f;
    prev.dvec.y = 2.0f;
    prev.dvec.z = 3.0f;
    cur.dvec.x = 5.0f;
    cur.dvec.y = 6.0f;
    cur.dvec.z = 7.0f;

    rdMatrix34 out;
    JonesFixedTick_BlendJointMatrix(&out, &prev, &cur, 0.25f);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, out.dvec.x);
    TEST_ASSERT_EQUAL_FLOAT(3.0f, out.dvec.y);
    TEST_ASSERT_EQUAL_FLOAT(4.0f, out.dvec.z);

    JonesFixedTick_BlendJointMatrix(&out, &prev, &cur, 0.0f);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, out.dvec.x);
    JonesFixedTick_BlendJointMatrix(&out, &prev, &cur, 1.0f);
    TEST_ASSERT_EQUAL_FLOAT(5.0f, out.dvec.x);
}

TEST(JonesFixedTick, SurfaceOffsetsAndLightBlendAndRestore)
{
    SithSurface surface;
    memset(&surface, 0, sizeof(surface));
    surface.flags = SITH_SURFACE_SCROLLING;

    JonesFixedTickSurfaceState prev;
    memset(&prev, 0, sizeof(prev));
    prev.valid            = true;
    prev.scrolling        = true;
    prev.texVertOffset.x  = 10.0f;
    prev.texVertOffset.y  = -20.0f;
    prev.extraLight.red   = 1.0f;
    prev.extraLight.green = 2.0f;
    prev.extraLight.blue  = 3.0f;
    prev.extraLight.alpha = 4.0f;

    JonesFixedTickSurfaceState cur;
    memset(&cur, 0, sizeof(cur));
    cur.valid            = true;
    cur.scrolling        = true;
    cur.texVertOffset.x  = 14.0f;
    cur.texVertOffset.y  = -24.0f;
    cur.extraLight.red   = 3.0f;
    cur.extraLight.green = 4.0f;
    cur.extraLight.blue  = 5.0f;
    cur.extraLight.alpha = 6.0f;

    JonesFixedTick_BlendSurface(&prev, &cur, &surface, 0.5f);
    TEST_ASSERT_EQUAL_FLOAT(12.0f, surface.face.texVertOffset.x);
    TEST_ASSERT_EQUAL_FLOAT(-22.0f, surface.face.texVertOffset.y);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, surface.face.extraLight.red);
    TEST_ASSERT_EQUAL_FLOAT(3.0f, surface.face.extraLight.green);
    TEST_ASSERT_EQUAL_FLOAT(4.0f, surface.face.extraLight.blue);
    TEST_ASSERT_EQUAL_FLOAT(5.0f, surface.face.extraLight.alpha);

    JonesFixedTick_RestoreSurface(&cur, &surface);
    TEST_ASSERT_EQUAL_FLOAT(14.0f, surface.face.texVertOffset.x);
    TEST_ASSERT_EQUAL_FLOAT(-24.0f, surface.face.texVertOffset.y);
    TEST_ASSERT_EQUAL_FLOAT(3.0f, surface.face.extraLight.red);
    TEST_ASSERT_EQUAL_FLOAT(4.0f, surface.face.extraLight.green);
    TEST_ASSERT_EQUAL_FLOAT(5.0f, surface.face.extraLight.blue);
    TEST_ASSERT_EQUAL_FLOAT(6.0f, surface.face.extraLight.alpha);
}

TEST(JonesFixedTick, ScrollWrapBlendsTheShortWay)
{
    // 1020 -> 20 wrapped forward; the long way would pass through the middle at 520.
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 8.0f,
        JonesFixedTick_LerpWrapped(1020.0f, 20.0f, 0.5f, JONESFIXEDTICK_SCROLL_PERIOD));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 8.0f,
        JonesFixedTick_LerpWrapped(20.0f, 1020.0f, 0.5f, JONESFIXEDTICK_SCROLL_PERIOD));
}

TEST(JonesFixedTick, SkyOffsetsBlendAndRestore)
{
    SithWorld world;
    memset(&world, 0, sizeof(world));

    JonesFixedTickSkyState prev;
    memset(&prev, 0, sizeof(prev));
    prev.valid              = true;
    prev.horizonOffset.x    = 10.0f;
    prev.horizonOffset.y    = 20.0f;
    prev.ceilingOffset.x    = 30.0f;
    prev.ceilingOffset.y    = 40.0f;

    JonesFixedTickSkyState cur;
    memset(&cur, 0, sizeof(cur));
    cur.valid              = true;
    cur.horizonOffset.x    = 14.0f;
    cur.horizonOffset.y    = 24.0f;
    cur.ceilingOffset.x    = 34.0f;
    cur.ceilingOffset.y    = 44.0f;

    JonesFixedTick_BlendSky(&prev, &cur, &world, 0.5f);
    TEST_ASSERT_EQUAL_FLOAT(12.0f, world.horizonSkyOffset.x);
    TEST_ASSERT_EQUAL_FLOAT(22.0f, world.horizonSkyOffset.y);
    TEST_ASSERT_EQUAL_FLOAT(32.0f, world.ceilingSkyOffset.x);
    TEST_ASSERT_EQUAL_FLOAT(42.0f, world.ceilingSkyOffset.y);

    JonesFixedTick_RestoreSky(&cur, &world);
    TEST_ASSERT_EQUAL_FLOAT(14.0f, world.horizonSkyOffset.x);
    TEST_ASSERT_EQUAL_FLOAT(24.0f, world.horizonSkyOffset.y);
    TEST_ASSERT_EQUAL_FLOAT(34.0f, world.ceilingSkyOffset.x);
    TEST_ASSERT_EQUAL_FLOAT(44.0f, world.ceilingSkyOffset.y);
}

TEST(JonesFixedTick, SectorLightBlendsAndRestores)
{
    SithSector sector;
    memset(&sector, 0, sizeof(sector));

    JonesFixedTickSectorState prev;
    memset(&prev, 0, sizeof(prev));
    prev.valid            = true;
    prev.extraLight.red   = 1.0f;
    prev.extraLight.green = 2.0f;
    prev.extraLight.blue  = 3.0f;
    prev.extraLight.alpha = 4.0f;

    JonesFixedTickSectorState cur;
    memset(&cur, 0, sizeof(cur));
    cur.valid            = true;
    cur.extraLight.red   = 3.0f;
    cur.extraLight.green = 4.0f;
    cur.extraLight.blue  = 5.0f;
    cur.extraLight.alpha = 6.0f;

    JonesFixedTick_BlendSector(&prev, &cur, &sector, 0.5f);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, sector.extraLight.red);
    TEST_ASSERT_EQUAL_FLOAT(3.0f, sector.extraLight.green);
    TEST_ASSERT_EQUAL_FLOAT(4.0f, sector.extraLight.blue);
    TEST_ASSERT_EQUAL_FLOAT(5.0f, sector.extraLight.alpha);

    JonesFixedTick_RestoreSector(&cur, &sector);
    TEST_ASSERT_EQUAL_FLOAT(3.0f, sector.extraLight.red);
    TEST_ASSERT_EQUAL_FLOAT(4.0f, sector.extraLight.green);
    TEST_ASSERT_EQUAL_FLOAT(5.0f, sector.extraLight.blue);
    TEST_ASSERT_EQUAL_FLOAT(6.0f, sector.extraLight.alpha);
}

TEST(JonesFixedTick, ParticleSizeBlendsAndRestores)
{
    rdParticle particle;
    memset(&particle, 0, sizeof(particle));

    SithThing thing;
    memset(&thing, 0, sizeof(thing));
    thing.type                        = SITH_THING_PARTICLE;
    thing.signature                   = 5;
    thing.renderData.data.pParticle   = &particle;

    particle.radius = 10.0f;
    JonesFixedTickThingState prev;
    JonesFixedTick_CaptureThing(&prev, &thing);

    particle.radius = 14.0f;
    JonesFixedTickThingState cur;
    JonesFixedTick_CaptureThing(&cur, &thing);

    TEST_ASSERT_TRUE(JonesFixedTick_BlendThing(&prev, &cur, &thing, 0.5f));
    TEST_ASSERT_EQUAL_FLOAT(12.0f, particle.radius);

    JonesFixedTick_RestoreThing(&cur, &thing);
    TEST_ASSERT_EQUAL_FLOAT(14.0f, particle.radius);
}

TEST(JonesFixedTick, ParticleCreatedBetweenTicksIsNotBlended)
{
    rdParticle particle;
    memset(&particle, 0, sizeof(particle));
    particle.radius = 14.0f;

    SithThing thing;
    memset(&thing, 0, sizeof(thing));
    thing.type                      = SITH_THING_PARTICLE;
    thing.signature                 = 5;
    thing.renderData.data.pParticle = &particle;

    JonesFixedTickThingState prev;
    memset(&prev, 0, sizeof(prev));
    prev.valid     = true;
    prev.type      = SITH_THING_PARTICLE;
    prev.signature = 5;
    prev.bParticle = false;

    JonesFixedTickThingState cur;
    JonesFixedTick_CaptureThing(&cur, &thing);

    TEST_ASSERT_TRUE(JonesFixedTick_BlendThing(&prev, &cur, &thing, 0.5f));
    TEST_ASSERT_EQUAL_FLOAT(14.0f, particle.radius);
}

// Added: the simulation builds a path/force-move thing's joints with the placement at the origin
// and then shifts them so the root joint sits on the thing position, consuming the animation's
// root motion. Mirror that here.
static void JonesFixedTickTest_BuildSkippedJoints(SithThing* pThing, rdMatrix34* pJoints,
    size_t numJoints, float tickPosX)
{
    pThing->pos.x = tickPosX;

    rdMatrix34 placement = pThing->orient;
    placement.dvec       = rdroid_g_zeroVector3;
    pThing->renderData.bSkipBuildingJoints = 0;
    rdPuppet_BuildJointMatrices(&pThing->renderData, &placement);
    pThing->renderData.bSkipBuildingJoints = 1;

    rdVector3 shift;
    rdVector_Sub3(&shift, &pThing->pos, &pJoints[0].dvec);
    for ( size_t jointNum = 0; jointNum < numJoints; ++jointNum )
    {
        rdVector_Add3Acc(&pJoints[jointNum].dvec, &shift);
    }
}

TEST(JonesFixedTick, DrawRebuildsJointsBuiltAtTheTickPlacement)
{
#ifdef J3D_QOL_IMPROVEMENTS
    // A tick that builds a thing's joints stamps the cache frame it saw. The drawn frame must still
    // rebuild them from the interpolated placement, not draw the tick pose while everything else
    // moves; EndDraw must then give the tick's matrices and stamp back to the simulation.
    rdModel3     model;
    rdModel3HNode node;
    rdMatrix34   joints[1];
    SithWorld    world;
    SithThing    thing;
    memset(&model, 0, sizeof(model));
    memset(&node, 0, sizeof(node));
    memset(&joints, 0, sizeof(joints));
    memset(&world, 0, sizeof(world));
    memset(&thing, 0, sizeof(thing));

    model.numHNodes          = 1;
    model.aHierarchyNodes    = &node;
    node.num                 = 0;
    node.meshIdx             = -1;
    node.meshOrient          = JonesFixedTickTest_Identity();
    joints[0]                = JonesFixedTickTest_Identity();

    thing.type                            = SITH_THING_ACTOR;
    thing.signature                       = 3;
    thing.pos                             = (rdVector3){ 0.0f, 0.0f, 0.0f };
    thing.orient                          = JonesFixedTickTest_Identity();
    thing.renderData.type                 = RD_THING_MODEL3;
    thing.renderData.data.pModel3         = &model;
    thing.renderData.paJointMatrices      = joints;
    thing.renderData.bSkipBuildingJoints  = 0;

    world.aThings  = &thing;
    world.numThings = 1;

    SithWorld*  pSavedWorld  = sithWorld_g_pCurrentWorld;
    SithCamera* pSavedCamera = sithCamera_g_pCurCamera;
    sithWorld_g_pCurrentWorld = &world;
    sithCamera_g_pCurCamera   = NULL;

    JonesFixedTick_SetRateOverride(50);
    JonesFixedTick_Configure();
    TEST_ASSERT_TRUE(JonesFixedTick_IsEnabled());
    JonesFixedTick_SetDrawAlpha(0.5f);

    JonesFixedTick_BeforeTick();

    // The tick moves the thing and builds its joints at the tick placement in this cache frame.
    thing.pos.x = 2.0f;
    rdMatrix34 placement = thing.orient;
    placement.dvec       = thing.pos;
    thing.renderData.bSkipBuildingJoints = 0;
    rdPuppet_BuildJointMatrices(&thing.renderData, &placement);
    const int tickFrameNum = (int)rdCache_GetFrameNum();
    TEST_ASSERT_EQUAL_INT(tickFrameNum, thing.renderData.rdFrameNum);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.0f, joints[0].dvec.x);

    JonesFixedTick_AfterTick();

    JonesFixedTick_BeginDraw();
    TEST_ASSERT_TRUE(thing.renderData.rdFrameNum != (int)rdCache_GetFrameNum());

    const rdMatrix34 drawPlacement = thing.orient;
    rdMatrix34 placed = drawPlacement;
    placed.dvec = thing.pos;
    rdPuppet_BuildJointMatrices(&thing.renderData, &placed);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, joints[0].dvec.x);

    JonesFixedTick_EndDraw();

    // The simulation sees exactly what the tick left.
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.0f, joints[0].dvec.x);
    TEST_ASSERT_EQUAL_INT(tickFrameNum, thing.renderData.rdFrameNum);
    TEST_ASSERT_EQUAL_INT(0, thing.renderData.bSkipBuildingJoints);

    sithWorld_g_pCurrentWorld = pSavedWorld;
    sithCamera_g_pCurCamera   = pSavedCamera;
#endif
}

TEST(JonesFixedTick, SkippedJointThingKeepsItsPoseAndFollowsTheInterpolatedPlacement)
{
#ifdef J3D_QOL_IMPROVEMENTS
    // A path/force-move tick builds the joints at the origin and shifts the root onto the thing
    // position, then leaves bSkipBuildingJoints set. Clearing the flag and rebuilding would add the
    // root animation offset again. The stored pose must move from the tick placement to the
    // interpolated one, keep the skip flag during the draw, and be restored exactly afterwards.
    rdModel3     model;
    rdModel3HNode node;
    rdMatrix34   joints[1];
    SithWorld    world;
    SithThing    thing;
    memset(&model, 0, sizeof(model));
    memset(&node, 0, sizeof(node));
    memset(&joints, 0, sizeof(joints));
    memset(&world, 0, sizeof(world));
    memset(&thing, 0, sizeof(thing));

    // A root node with a 5-unit animation offset, as a force-move root translation would be.
    node.meshOrient           = JonesFixedTickTest_Identity();
    node.meshOrient.dvec.y    = 5.0f;
    model.numHNodes           = 1;
    model.aHierarchyNodes     = &node;
    node.num                  = 0;
    node.meshIdx              = -1;
    joints[0]                 = JonesFixedTickTest_Identity();

    thing.type                            = SITH_THING_ACTOR;
    thing.signature                       = 4;
    thing.pos                             = (rdVector3){ 0.0f, 0.0f, 0.0f };
    thing.orient                          = JonesFixedTickTest_Identity();
    thing.renderData.type                 = RD_THING_MODEL3;
    thing.renderData.data.pModel3         = &model;
    thing.renderData.paJointMatrices      = joints;
    thing.renderData.bSkipBuildingJoints  = 0;

    world.aThings   = &thing;
    world.numThings = 1;

    SithWorld*  pSavedWorld  = sithWorld_g_pCurrentWorld;
    SithCamera* pSavedCamera = sithCamera_g_pCurCamera;
    sithWorld_g_pCurrentWorld = &world;
    sithCamera_g_pCurCamera   = NULL;

    JonesFixedTick_SetRateOverride(50);
    JonesFixedTick_Configure();
    TEST_ASSERT_TRUE(JonesFixedTick_IsEnabled());
    JonesFixedTick_SetDrawAlpha(0.5f);

    // The previous tick is already inside the force move: its root sits on its position.
    JonesFixedTickTest_BuildSkippedJoints(&thing, joints, STD_ARRAYLEN(joints), 0.0f);

    JonesFixedTick_BeforeTick();

    JonesFixedTickTest_BuildSkippedJoints(&thing, joints, STD_ARRAYLEN(joints), 2.0f);
    const int tickFrameNum = (int)rdCache_GetFrameNum();
    TEST_ASSERT_EQUAL_INT(tickFrameNum, thing.renderData.rdFrameNum);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.0f, joints[0].dvec.x);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, joints[0].dvec.y);

    JonesFixedTick_AfterTick();

    JonesFixedTick_BeginDraw();

    // The draw keeps the stored pose but moves the root to the interpolated position (2 -> 1),
    // without adding the 5-unit root animation offset again.
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, joints[0].dvec.x);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, joints[0].dvec.y);
    TEST_ASSERT_EQUAL_INT(1, thing.renderData.bSkipBuildingJoints);

    // The scene draw's joint build is a no-op for a skipped model, so the pose stays as transformed.
    rdMatrix34 placed = thing.orient;
    placed.dvec = thing.pos;
    rdPuppet_BuildJointMatrices(&thing.renderData, &placed);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, joints[0].dvec.x);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, joints[0].dvec.y);

    JonesFixedTick_EndDraw();

    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.0f, joints[0].dvec.x);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, joints[0].dvec.y);
    TEST_ASSERT_EQUAL_INT(tickFrameNum, thing.renderData.rdFrameNum);
    TEST_ASSERT_EQUAL_INT(1, thing.renderData.bSkipBuildingJoints);

    sithWorld_g_pCurrentWorld = pSavedWorld;
    sithCamera_g_pCurCamera   = pSavedCamera;
#endif
}

TEST(JonesFixedTick, PoseCrossfadeFromAForceMovePoseKeepsItsRootOnThePosition)
{
#ifdef J3D_QOL_IMPROVEMENTS
    // A force-move tick (a ledge hang or climb) builds the joints at the origin and shifts the root
    // onto the thing position, consuming the animation's root offset. When the next tick ends the
    // force move and changes the tracks, the drawn frame crossfades the two tick skeletons. The old
    // skeleton must be rebuilt the same way; building it at the position adds the root offset
    // again and Indy pops up for a frame (owner video, 2026-11-24).
    rdKeyframeNode kfNode;
    rdKeyframe     keyframe;
    memset(&kfNode, 0, sizeof(kfNode));
    memset(&keyframe, 0, sizeof(keyframe));
    keyframe.numFrames = 100;
    keyframe.numJoints = 1;
    keyframe.aNodes    = &kfNode;
    keyframe.fps       = 30.0f;

    rdModel3      model;
    rdModel3HNode node;
    rdMatrix34    joints[1];
    rdVector3     tweaks[1];
    SithWorld     world;
    SithThing     thing;
    rdPuppet      puppet;
    memset(&model, 0, sizeof(model));
    memset(&node, 0, sizeof(node));
    memset(&joints, 0, sizeof(joints));
    memset(&tweaks, 0, sizeof(tweaks));
    memset(&world, 0, sizeof(world));
    memset(&thing, 0, sizeof(thing));
    memset(&puppet, 0, sizeof(puppet));

    // The root node sits 5 units up, like a hang pose's root above the start position.
    node.pos.y                = 5.0f;
    node.meshOrient           = JonesFixedTickTest_Identity();
    node.meshOrient.dvec.y    = 5.0f;
    node.num                  = 0;
    node.meshIdx              = -1;
    model.numHNodes           = 1;
    model.aHierarchyNodes     = &node;
    joints[0]                 = JonesFixedTickTest_Identity();

    thing.type                            = SITH_THING_ACTOR;
    thing.signature                       = 6;
    thing.orient                          = JonesFixedTickTest_Identity();
    thing.renderData.type                 = RD_THING_MODEL3;
    thing.renderData.data.pModel3         = &model;
    thing.renderData.paJointMatrices      = joints;
    thing.renderData.apTweakedAngles      = tweaks;
    thing.renderData.pPuppet              = &puppet;
    puppet.pThing                         = &thing.renderData;

    world.aThings   = &thing;
    world.numThings = 1;

    SithWorld*  pSavedWorld  = sithWorld_g_pCurrentWorld;
    SithCamera* pSavedCamera = sithCamera_g_pCurCamera;
    sithWorld_g_pCurrentWorld = &world;
    sithCamera_g_pCurCamera   = NULL;

    JonesFixedTick_SetRateOverride(50);
    JonesFixedTick_Configure();
    TEST_ASSERT_TRUE(JonesFixedTick_IsEnabled());

    // Force-move tick: a playing track, root shifted onto the position at the origin.
    puppet.aTracks[0].status        = RDPUPPET_TRACK_PLAYING;
    puppet.aTracks[0].pKFTrack      = &keyframe;
    puppet.aTracks[0].curFrame      = 1.0f;
    puppet.aTracks[0].blendWeight   = 1.0f;
    puppet.aTracks[0].playbackSpeed = 1.0f;
    JonesFixedTickTest_BuildSkippedJoints(&thing, joints, STD_ARRAYLEN(joints), 0.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, joints[0].dvec.y);

    JonesFixedTick_BeforeTick();

    // Next tick: the force move ends, its track is gone and the joints are built normally.
    memset(&puppet.aTracks[0], 0, sizeof(puppet.aTracks[0]));
    thing.pos.x = 2.0f;
    rdMatrix34 placement = thing.orient;
    placement.dvec       = thing.pos;
    thing.renderData.bSkipBuildingJoints = 0;
    rdPuppet_BuildJointMatrices(&thing.renderData, &placement);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 5.0f, joints[0].dvec.y);

    JonesFixedTick_AfterTick();

    // Halfway: the root lies between the old root (0, 0) and the new one (2, 5), not at y = 5.
    JonesFixedTick_SetDrawAlpha(0.5f);
    JonesFixedTick_BeginDraw();
    TEST_ASSERT_EQUAL_INT((int)rdCache_GetFrameNum(), thing.renderData.rdFrameNum);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, joints[0].dvec.x);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.5f, joints[0].dvec.y);
    JonesFixedTick_EndDraw();

    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.0f, joints[0].dvec.x);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 5.0f, joints[0].dvec.y);
    TEST_ASSERT_EQUAL_INT(0, thing.renderData.bSkipBuildingJoints);

    sithWorld_g_pCurrentWorld = pSavedWorld;
    sithCamera_g_pCurCamera   = pSavedCamera;
#endif
}

TEST(JonesFixedTick, ForceMoveStartDrawsTheTickJointsNotTheInterpolatedPosition)
{
#ifdef J3D_QOL_IMPROVEMENTS
    // The tick that starts a force move (a jump) moves the thing position by the animation's root
    // offset while the root joint stays where it was. Pinning the root to the interpolated position
    // would lift the body for that tick (owner video, 2026-11-24: Indy pops up as a jump starts).
    // The drawn frame must keep the tick's joints, then restore everything exactly.
    rdModel3      model;
    rdModel3HNode node;
    rdMatrix34    joints[1];
    SithWorld     world;
    SithThing     thing;
    memset(&model, 0, sizeof(model));
    memset(&node, 0, sizeof(node));
    memset(&joints, 0, sizeof(joints));
    memset(&world, 0, sizeof(world));
    memset(&thing, 0, sizeof(thing));

    // The standing pose puts the root 1 unit above the position.
    node.meshOrient           = JonesFixedTickTest_Identity();
    node.meshOrient.dvec.z    = 1.0f;
    model.numHNodes           = 1;
    model.aHierarchyNodes     = &node;
    node.num                  = 0;
    node.meshIdx              = -1;
    joints[0]                 = JonesFixedTickTest_Identity();

    thing.type                            = SITH_THING_ACTOR;
    thing.signature                       = 7;
    thing.orient                          = JonesFixedTickTest_Identity();
    thing.renderData.type                 = RD_THING_MODEL3;
    thing.renderData.data.pModel3         = &model;
    thing.renderData.paJointMatrices      = joints;
    thing.renderData.bSkipBuildingJoints  = 0;

    world.aThings   = &thing;
    world.numThings = 1;

    SithWorld*  pSavedWorld  = sithWorld_g_pCurrentWorld;
    SithCamera* pSavedCamera = sithCamera_g_pCurCamera;
    sithWorld_g_pCurrentWorld = &world;
    sithCamera_g_pCurCamera   = NULL;

    JonesFixedTick_SetRateOverride(50);
    JonesFixedTick_Configure();
    TEST_ASSERT_TRUE(JonesFixedTick_IsEnabled());

    // Standing tick: position 0, root at z = 1, built normally.
    JonesFixedTick_BeforeTick();

    // Force-move start: the root stays at z = 1 and the position moves onto it.
    thing.pos.z = 1.0f;
    joints[0]   = JonesFixedTickTest_Identity();
    joints[0].dvec.z = 1.0f;
    thing.renderData.bSkipBuildingJoints = 1;
    const int tickFrameNum = (int)rdCache_GetFrameNum();
    thing.renderData.rdFrameNum = tickFrameNum;

    JonesFixedTick_AfterTick();

    JonesFixedTick_SetDrawAlpha(0.25f);
    JonesFixedTick_BeginDraw();
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.25f, thing.pos.z); // the placement still interpolates
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, joints[0].dvec.z); // the root does not drop with it
    TEST_ASSERT_EQUAL_INT(1, thing.renderData.bSkipBuildingJoints);
    JonesFixedTick_EndDraw();

    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, thing.pos.z);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, joints[0].dvec.z);
    TEST_ASSERT_EQUAL_INT(tickFrameNum, thing.renderData.rdFrameNum);

    sithWorld_g_pCurrentWorld = pSavedWorld;
    sithCamera_g_pCurCamera   = pSavedCamera;
#endif
}

TEST(JonesFixedTick, ForceMovePoseBlendsBetweenTicks)
{
#ifdef J3D_QOL_IMPROVEMENTS
    // A force-move skeleton only changes when a tick rebuilds it. The drawn frame must blend the
    // previous and current tick skeletons, so the limbs move every frame instead of stepping at the
    // tick rate (owner trace, 2026-11-24: jitter while pulling up onto a block).
    rdModel3      model;
    rdModel3HNode nodes[2];
    rdMatrix34    joints[2];
    SithWorld     world;
    SithThing     thing;
    memset(&model, 0, sizeof(model));
    memset(&nodes, 0, sizeof(nodes));
    memset(&joints, 0, sizeof(joints));
    memset(&world, 0, sizeof(world));
    memset(&thing, 0, sizeof(thing));

    model.numHNodes       = 2;
    model.aHierarchyNodes = nodes;
    nodes[0].meshIdx      = -1;
    nodes[1].meshIdx      = -1;
    nodes[1].num          = 1;

    thing.type                           = SITH_THING_ACTOR;
    thing.signature                      = 8;
    thing.orient                         = JonesFixedTickTest_Identity();
    thing.renderData.type                = RD_THING_MODEL3;
    thing.renderData.data.pModel3        = &model;
    thing.renderData.paJointMatrices     = joints;
    thing.renderData.bSkipBuildingJoints = 1;

    world.aThings   = &thing;
    world.numThings = 1;

    SithWorld*  pSavedWorld  = sithWorld_g_pCurrentWorld;
    SithCamera* pSavedCamera = sithCamera_g_pCurCamera;
    sithWorld_g_pCurrentWorld = &world;
    sithCamera_g_pCurCamera   = NULL;

    JonesFixedTick_SetRateOverride(50);
    JonesFixedTick_Configure();
    TEST_ASSERT_TRUE(JonesFixedTick_IsEnabled());

    // Previous force-move tick: the root on the position, the hand 1 unit up.
    joints[0] = JonesFixedTickTest_Identity();
    joints[1] = JonesFixedTickTest_Identity();
    joints[1].dvec.z = 1.0f;

    JonesFixedTick_BeforeTick();

    // Current tick: the thing moved 2 units along x and the hand rose to 3 units.
    thing.pos.x = 2.0f;
    joints[0].dvec.x = 2.0f;
    joints[1].dvec.x = 2.0f;
    joints[1].dvec.z = 3.0f;
    const int tickFrameNum = (int)rdCache_GetFrameNum();
    thing.renderData.rdFrameNum = tickFrameNum;

    JonesFixedTick_AfterTick();

    JonesFixedTick_SetDrawAlpha(0.5f);
    JonesFixedTick_BeginDraw();
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, joints[0].dvec.x);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, joints[1].dvec.x);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.0f, joints[1].dvec.z); // halfway, not the current 3
    TEST_ASSERT_EQUAL_INT(1, thing.renderData.bSkipBuildingJoints);
    JonesFixedTick_EndDraw();

    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.0f, joints[1].dvec.x);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 3.0f, joints[1].dvec.z);
    TEST_ASSERT_EQUAL_INT(tickFrameNum, thing.renderData.rdFrameNum);

    sithWorld_g_pCurrentWorld = pSavedWorld;
    sithCamera_g_pCurCamera   = pSavedCamera;
#endif
}

TEST(JonesFixedTick, DrawLeavesNormalJointsAtTheTickPlacement)
{
#ifdef J3D_QOL_IMPROVEMENTS
    // A normal model's joints are built only by the draw; the simulation reads the last built ones
    // (the whip attached to Indy's hand). After a drawn frame they must be the tick placement's,
    // not the copy saved before the draw, which can be many frames old (owner, 2026-11-24: the
    // whip came loose from Indy's hand).
    rdModel3      model;
    rdModel3HNode node;
    rdMatrix34    joints[1];
    SithWorld     world;
    SithThing     thing;
    memset(&model, 0, sizeof(model));
    memset(&node, 0, sizeof(node));
    memset(&joints, 0, sizeof(joints));
    memset(&world, 0, sizeof(world));
    memset(&thing, 0, sizeof(thing));

    model.numHNodes       = 1;
    model.aHierarchyNodes = &node;
    node.meshIdx          = -1;
    node.meshOrient       = JonesFixedTickTest_Identity();

    thing.type                           = SITH_THING_ACTOR;
    thing.signature                      = 9;
    thing.orient                         = JonesFixedTickTest_Identity();
    thing.renderData.type                = RD_THING_MODEL3;
    thing.renderData.data.pModel3        = &model;
    thing.renderData.paJointMatrices     = joints;
    thing.renderData.bSkipBuildingJoints = 0;

    // Joints a draw built long ago, far away.
    joints[0]        = JonesFixedTickTest_Identity();
    joints[0].dvec.x = -5.0f;
    thing.renderData.rdFrameNum = -1;

    world.aThings   = &thing;
    world.numThings = 1;

    SithWorld*  pSavedWorld  = sithWorld_g_pCurrentWorld;
    SithCamera* pSavedCamera = sithCamera_g_pCurCamera;
    sithWorld_g_pCurrentWorld = &world;
    sithCamera_g_pCurCamera   = NULL;

    JonesFixedTick_SetRateOverride(50);
    JonesFixedTick_Configure();
    TEST_ASSERT_TRUE(JonesFixedTick_IsEnabled());

    JonesFixedTick_BeforeTick();
    thing.pos.x = 2.0f; // the tick moves the thing without building joints
    JonesFixedTick_AfterTick();

    JonesFixedTick_SetDrawAlpha(0.5f);
    JonesFixedTick_BeginDraw();

    // The scene draw rebuilds the joints from the interpolated placement.
    rdMatrix34 placed = thing.orient;
    placed.dvec = thing.pos;
    rdPuppet_BuildJointMatrices(&thing.renderData, &placed);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, joints[0].dvec.x);

    JonesFixedTick_EndDraw();

    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.0f, thing.pos.x);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.0f, joints[0].dvec.x); // the tick placement, not the stale -5
    TEST_ASSERT_EQUAL_INT(0, thing.renderData.bSkipBuildingJoints);

    sithWorld_g_pCurrentWorld = pSavedWorld;
    sithCamera_g_pCurCamera   = pSavedCamera;
#endif
}

TEST(JonesFixedTick, ForceMoveStartBlendsFromTheDrawnNormalPose)
{
#ifdef J3D_QOL_IMPROVEMENTS
    // When a drawn normal pose precedes a force move (a hang before a pull-up, standing before a
    // climb), the first force-move tick must blend from that pose instead of snapping to the new
    // one (owner trace, 2026-11-24: a foot jump at the start of climbs and pull-ups).
    rdModel3      model;
    rdModel3HNode nodes[2];
    rdMatrix34    joints[2];
    SithWorld     world;
    SithThing     thing;
    memset(&model, 0, sizeof(model));
    memset(&nodes, 0, sizeof(nodes));
    memset(&joints, 0, sizeof(joints));
    memset(&world, 0, sizeof(world));
    memset(&thing, 0, sizeof(thing));

    // The root 1 unit above the position, the hand 1 unit to the side of it.
    model.numHNodes          = 2;
    model.aHierarchyNodes    = nodes;
    nodes[0].meshIdx         = -1;
    nodes[0].meshOrient      = JonesFixedTickTest_Identity();
    nodes[0].meshOrient.dvec.z = 1.0f;
    nodes[1].num             = 1;
    nodes[1].meshIdx         = -1;
    nodes[1].meshOrient      = JonesFixedTickTest_Identity();
    nodes[1].meshOrient.dvec.x = 1.0f;
    nodes[1].meshOrient.dvec.z = 1.0f;

    thing.type                           = SITH_THING_ACTOR;
    thing.signature                      = 10;
    thing.orient                         = JonesFixedTickTest_Identity();
    thing.renderData.type                = RD_THING_MODEL3;
    thing.renderData.data.pModel3        = &model;
    thing.renderData.paJointMatrices     = joints;
    thing.renderData.bSkipBuildingJoints = 0;

    world.aThings   = &thing;
    world.numThings = 1;

    SithWorld*  pSavedWorld  = sithWorld_g_pCurrentWorld;
    SithCamera* pSavedCamera = sithCamera_g_pCurCamera;
    sithWorld_g_pCurrentWorld = &world;
    sithCamera_g_pCurCamera   = NULL;

    JonesFixedTick_SetRateOverride(50);
    JonesFixedTick_Configure();
    TEST_ASSERT_TRUE(JonesFixedTick_IsEnabled());

    // A normal tick, then a drawn frame that builds the normal pose.
    JonesFixedTick_BeforeTick();
    JonesFixedTick_AfterTick();
    JonesFixedTick_SetDrawAlpha(1.0f);
    JonesFixedTick_BeginDraw();
    rdMatrix34 placed = thing.orient;
    placed.dvec = thing.pos;
    rdPuppet_BuildJointMatrices(&thing.renderData, &placed);
    JonesFixedTick_EndDraw();
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, joints[1].dvec.x);

    // The force move starts: the position moves onto the root, the hand goes up above it.
    JonesFixedTick_BeforeTick();
    thing.pos.z      = 1.0f;
    joints[0]        = JonesFixedTickTest_Identity();
    joints[0].dvec.z = 1.0f;
    joints[1]        = JonesFixedTickTest_Identity();
    joints[1].dvec.z = 2.0f;
    thing.renderData.bSkipBuildingJoints = 1;
    const int tickFrameNum = (int)rdCache_GetFrameNum();
    thing.renderData.rdFrameNum = tickFrameNum;
    JonesFixedTick_AfterTick();

    JonesFixedTick_SetDrawAlpha(0.5f);
    JonesFixedTick_BeginDraw();
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.0f, joints[0].dvec.z); // the root stays put
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.5f, joints[1].dvec.x); // halfway between the poses
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 1.5f, joints[1].dvec.z);
    JonesFixedTick_EndDraw();

    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, joints[1].dvec.x);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 2.0f, joints[1].dvec.z);
    TEST_ASSERT_EQUAL_INT(1, thing.renderData.bSkipBuildingJoints);
    TEST_ASSERT_EQUAL_INT(tickFrameNum, thing.renderData.rdFrameNum);

    sithWorld_g_pCurrentWorld = pSavedWorld;
    sithCamera_g_pCurCamera   = pSavedCamera;
#endif
}

TEST_GROUP_RUNNER(JonesFixedTick)
{
    RUN_TEST_CASE(JonesFixedTick, RateMapsToOffOrFifty);
    RUN_TEST_CASE(JonesFixedTick, ConfigRateKeepsTheBuildDefaultForGarbage);
    RUN_TEST_CASE(JonesFixedTick, RateToStepUsesIntegerMilliseconds);
    RUN_TEST_CASE(JonesFixedTick, AccumulatorRunsATickPerElapsedStep);
    RUN_TEST_CASE(JonesFixedTick, AccumulatorCapsCatchUpAndDropsTheBacklog);
    RUN_TEST_CASE(JonesFixedTick, AccumulatorHandlesAZeroDeltaAndPause);
    RUN_TEST_CASE(JonesFixedTick, LerpVectorBlendsEachComponent);
    RUN_TEST_CASE(JonesFixedTick, LerpOrientReOrthonormalisesAndKeepsPosition);
    RUN_TEST_CASE(JonesFixedTick, BlendThingInterpolatesAndRestoresExactPostTickValues);
    RUN_TEST_CASE(JonesFixedTick, BlendThingNeverBlendsAcrossASignatureChange);
    RUN_TEST_CASE(JonesFixedTick, BlendThingSnapsACreatedThingAndATeleport);
    RUN_TEST_CASE(JonesFixedTick, BlendCameraInterpolatesAndRestoresFovAspectAndLook);
    RUN_TEST_CASE(JonesFixedTick, RestoreCameraRestoresTickSector);
    RUN_TEST_CASE(JonesFixedTick, BlendCameraTeleportKeepsTickSector);
    RUN_TEST_CASE(JonesFixedTick, ResolveCameraSectorNullArgsReturnFromSector);
    RUN_TEST_CASE(JonesFixedTick, ConfigureHonoursTheOptionOnlyInQOLBuilds);
    RUN_TEST_CASE(JonesFixedTick, ConfigureAcceptsZeroAsOff);
    RUN_TEST_CASE(JonesFixedTick, ConfigureDevOverrideKeepsTheComparisonRates);
    RUN_TEST_CASE(JonesFixedTick, PuppetPoseSamplesBetweenTicksAndRestores);
    RUN_TEST_CASE(JonesFixedTick, PuppetSameTrackContinuesWithoutPoseCrossfade);
    RUN_TEST_CASE(JonesFixedTick, PuppetReplacedTrackNeedsPoseCrossfade);
    RUN_TEST_CASE(JonesFixedTick, PuppetLoopNeedsPoseCrossfade);
    RUN_TEST_CASE(JonesFixedTick, PuppetAppearedOrRemovedTrackNeedsPoseCrossfade);
    RUN_TEST_CASE(JonesFixedTick, PuppetUnknownOldFrameSnaps);
    RUN_TEST_CASE(JonesFixedTick, PuppetModelSwapSnaps);
    RUN_TEST_CASE(JonesFixedTick, JointMatrixBlendStaysOnTheTranslationSegment);
    RUN_TEST_CASE(JonesFixedTick, SurfaceOffsetsAndLightBlendAndRestore);
    RUN_TEST_CASE(JonesFixedTick, ScrollWrapBlendsTheShortWay);
    RUN_TEST_CASE(JonesFixedTick, SkyOffsetsBlendAndRestore);
    RUN_TEST_CASE(JonesFixedTick, SectorLightBlendsAndRestores);
    RUN_TEST_CASE(JonesFixedTick, ParticleSizeBlendsAndRestores);
    RUN_TEST_CASE(JonesFixedTick, ParticleCreatedBetweenTicksIsNotBlended);
    RUN_TEST_CASE(JonesFixedTick, DrawRebuildsJointsBuiltAtTheTickPlacement);
    RUN_TEST_CASE(JonesFixedTick, SkippedJointThingKeepsItsPoseAndFollowsTheInterpolatedPlacement);
    RUN_TEST_CASE(JonesFixedTick, PoseCrossfadeFromAForceMovePoseKeepsItsRootOnThePosition);
    RUN_TEST_CASE(JonesFixedTick, ForceMoveStartDrawsTheTickJointsNotTheInterpolatedPosition);
    RUN_TEST_CASE(JonesFixedTick, ForceMovePoseBlendsBetweenTicks);
    RUN_TEST_CASE(JonesFixedTick, DrawLeavesNormalJointsAtTheTickPlacement);
    RUN_TEST_CASE(JonesFixedTick, ForceMoveStartBlendsFromTheDrawnNormalPose);
}
