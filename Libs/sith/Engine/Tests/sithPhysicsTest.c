// Original 50 Hz player-physics regressions.
// Test vectors come from upstream commit 85ac23e1 (sithPhysicsTest.c).
#include <unity_fixture.h>
#include <string.h>

#include <rdroid/Math/rdVector.h>
#include <sith/Devices/sithControl.h>
#include <sith/Engine/sithPhysics.h>
#include <sith/Gameplay/sithModernControls.h>
#include <sith/Gameplay/sithPlayer.h>
#include <sith/Main/sithMain.h>
#include <sith/World/sithThing.h>
#include <sith/World/sithWorld.h>

static SithThing sithPhysicsTest_thing;
static SithWorld sithPhysicsTest_world;
static SithSector sithPhysicsTest_sector;
static bool sithPhysicsTest_bHasConfigRate;
static float sithPhysicsTest_configRate;
static size_t sithPhysicsTest_numDragCalls;
static size_t sithPhysicsTest_numSectorCalls;
static size_t sithPhysicsTest_numSteps;
static size_t sithPhysicsTest_maxSteps;
static tHostServices sithPhysicsTest_hostServices;
static tHostServices* sithPhysicsTest_pSavedHostServices;
static SithWorld* sithPhysicsTest_pSavedWorld;
static int sithPhysicsTest_numAsserts;

TEST_GROUP(sithPhysics);

static int J3DAPI sithPhysicsTest_Print(const char* pFormat, ...)
{
    (void)pFormat;
    return 0;
}

static void J3DAPI sithPhysicsTest_RecordAssert(const char* pText, const char* pFile, int line)
{
    (void)pText;
    (void)pFile;
    (void)line;
    ++sithPhysicsTest_numAsserts;
}

void J3DAPI sithPhysicsTest_MultAcc3(rdVector3* pAcc, const rdVector3* pVector, float scale)
{
    SithPhysicsInfo* pPhysics = &sithPhysicsTest_thing.moveInfo.physics;
    if ( pAcc == &pPhysics->deltaVelocity )
    {
        TEST_ASSERT_EQUAL_PTR(&pPhysics->velocity, pVector);
        ++sithPhysicsTest_numSteps;
        TEST_ASSERT_TRUE_MESSAGE(sithPhysicsTest_numSteps <= sithPhysicsTest_maxSteps,
            "Unexpected 50 Hz integration step/count");
        TEST_ASSERT_EQUAL_FLOAT(0.02f, scale);
    }
    else if ( pAcc == &pPhysics->velocity )
    {
        TEST_ASSERT_EQUAL_PTR(pAcc, pVector);
        TEST_ASSERT_EQUAL_FLOAT(-0.1f, scale); // airDrag 5 over the original 20 ms step.
        ++sithPhysicsTest_numDragCalls;
    }
    else
    {
        TEST_ASSERT_EQUAL_PTR(&sithPhysicsTest_sector.thrust, pVector);
        TEST_ASSERT_EQUAL_FLOAT(0.02f, scale);
        ++sithPhysicsTest_numSectorCalls;
    }
    rdVector_MultAcc3(pAcc, pVector, scale);
}

static void sithPhysicsTest_InitThing(void)
{
    memset(&sithPhysicsTest_thing, 0, sizeof(sithPhysicsTest_thing));
    sithPhysicsTest_thing.type = SITH_THING_PLAYER;
    sithPhysicsTest_thing.moveType = SITH_MT_PHYSICS;
    sithPhysicsTest_thing.orient.rvec.x = 1.0f;
    sithPhysicsTest_thing.orient.lvec.y = 1.0f;
    sithPhysicsTest_thing.orient.uvec.z = 1.0f;
    // No raft, angular thrust, drag, mass or sector forces: observe the actual
    // fixed-step integrator without loading world/device globals.
    sithPhysicsTest_thing.moveInfo.physics.flags = SITH_PF_FLY;
    sithPhysicsTest_thing.moveInfo.physics.velocity = (rdVector3){ 1.0f, 2.0f, 3.0f };
    sithPhysicsTest_numSteps = 0;
    sithPhysicsTest_maxSteps = 0;
    sithPhysicsTest_numDragCalls = sithPhysicsTest_numSectorCalls = 0;
    memset(&sithPhysicsTest_world, 0, sizeof(sithPhysicsTest_world));
    memset(&sithPhysicsTest_sector, 0, sizeof(sithPhysicsTest_sector));
    sithWorld_g_pCurrentWorld = &sithPhysicsTest_world;
}

static void sithPhysicsTest_AssertVector(const rdVector3* pExpected, const rdVector3* pActual)
{
    TEST_ASSERT_FLOAT_WITHIN(0.000001f, pExpected->x, pActual->x);
    TEST_ASSERT_FLOAT_WITHIN(0.000001f, pExpected->y, pActual->y);
    TEST_ASSERT_FLOAT_WITHIN(0.000001f, pExpected->z, pActual->z);
}

TEST_SETUP(sithPhysics)
{
    memset(&sithPhysicsTest_hostServices, 0, sizeof(sithPhysicsTest_hostServices));
    sithPhysicsTest_hostServices.pStatusPrint  = sithPhysicsTest_Print;
    sithPhysicsTest_hostServices.pMessagePrint = sithPhysicsTest_Print;
    sithPhysicsTest_hostServices.pWarningPrint = sithPhysicsTest_Print;
    sithPhysicsTest_hostServices.pErrorPrint   = sithPhysicsTest_Print;
    sithPhysicsTest_hostServices.pDebugPrint   = sithPhysicsTest_Print;
    sithPhysicsTest_hostServices.pAssert       = sithPhysicsTest_RecordAssert;

    sithPhysicsTest_pSavedHostServices = sith_g_pHS;
    sithPhysicsTest_pSavedWorld        = sithWorld_g_pCurrentWorld;
    sith_g_pHS                         = &sithPhysicsTest_hostServices;
    sithPhysicsTest_numAsserts         = 0;
    sithPhysicsTest_bHasConfigRate     = false;
    sithPhysicsTest_configRate         = 0.0f;
    sithPhysicsTest_InitThing();
}

TEST_TEAR_DOWN(sithPhysics)
{
    sithWorld_g_pCurrentWorld = sithPhysicsTest_pSavedWorld;
    sith_g_pHS                = sithPhysicsTest_pSavedHostServices;
    TEST_ASSERT_EQUAL_INT(0, sithPhysicsTest_numAsserts);
}

float J3DAPI sithPhysicsTest_ConfigGetFloat(const char* pKey, float defaultValue)
{
    return strcmp(pKey, "engine.physics.fixedTimestep") == 0 && sithPhysicsTest_bHasConfigRate
        ? sithPhysicsTest_configRate : defaultValue;
}

bool sithPhysicsTest_ConfigContains(const char* pKey)
{
    return strcmp(pKey, "engine.physics.fixedTimestep") == 0 && sithPhysicsTest_bHasConfigRate;
}

bool J3DAPI sithPhysicsTest_ConfigSetFloat(const char* pKey, float value)
{
    if ( strcmp(pKey, "engine.physics.fixedTimestep") == 0 )
    {
        sithPhysicsTest_bHasConfigRate = true;
        sithPhysicsTest_configRate = value;
    }
    return true;
}

static SithPhysicsInfo* sithPhysicsTest_InitFall(void)
{
    sithPhysicsTest_InitThing();
    sithPhysicsTest_thing.moveStatus = SITHPLAYERMOVE_FALLING;
    sithPhysicsTest_thing.pInSector = &sithPhysicsTest_sector;
    sithPhysicsTest_world.gravity = 10.0f;
    SithPhysicsInfo* pPhysics = &sithPhysicsTest_thing.moveInfo.physics;
    pPhysics->mass = 1.0f;
    pPhysics->flags = SITH_PF_USEGRAVITY;
    pPhysics->velocity = (rdVector3){ 1.0f, 2.0f, 0.0f };
    return pPhysics;
}

TEST(sithPhysics, TestStartupIgnoresStoredNonOriginalRates)
{
    static const float aRates[] = { 150.0f, 60.0f, 0.0f, -50.0f };
    for ( size_t i = 0; i < sizeof(aRates) / sizeof(aRates[0]); ++i )
    {
        sithPhysicsTest_bHasConfigRate = true;
        sithPhysicsTest_configRate = aRates[i];
        sithPhysics_Startup();
        TEST_ASSERT_TRUE(sithPhysicsTest_bHasConfigRate);
        TEST_ASSERT_EQUAL_FLOAT(aRates[i], sithPhysicsTest_configRate);
        sithPhysicsTest_InitThing();
        SithPhysicsInfo* pPhysics = &sithPhysicsTest_thing.moveInfo.physics;
        sithPhysics_UpdateDetachedPlayerPhysics(&sithPhysicsTest_thing, 0.01f);
        TEST_ASSERT_EQUAL_size_t(0, sithPhysicsTest_numSteps);
        TEST_ASSERT_EQUAL_FLOAT(0.01f, pPhysics->physicsRolloverFrames);
        const rdVector3 zero = { 0 };
        sithPhysicsTest_AssertVector(&zero, &pPhysics->deltaVelocity);
        sithPhysicsTest_maxSteps = 1;
        sithPhysics_UpdateDetachedPlayerPhysics(&sithPhysicsTest_thing, 0.01f);
        const rdVector3 delta = { 0.02f, 0.04f, 0.06f };
        const rdVector3 velocity = { 1.0f, 2.0f, 3.0f };
        TEST_ASSERT_EQUAL_size_t(1, sithPhysicsTest_numSteps);
        TEST_ASSERT_EQUAL_FLOAT(0.0f, pPhysics->physicsRolloverFrames);
        sithPhysicsTest_AssertVector(&velocity, &pPhysics->velocity);
        sithPhysicsTest_AssertVector(&delta, &pPhysics->deltaVelocity);
        sithPhysicsTest_AssertVector(&zero, &pPhysics->gravityForce);
    }
}

TEST(sithPhysics, TestStartupDoesNotCreateFixedRateSetting)
{
    TEST_ASSERT_FALSE(sithPhysicsTest_bHasConfigRate);
    sithPhysics_Startup();
    sithPhysics_Startup();
    TEST_ASSERT_FALSE(sithPhysicsTest_bHasConfigRate);
    sithPhysicsTest_maxSteps = 1;
    sithPhysics_UpdateDetachedPlayerPhysics(&sithPhysicsTest_thing, 0.02f);
    const rdVector3 delta = { 0.02f, 0.04f, 0.06f };
    TEST_ASSERT_EQUAL_size_t(1, sithPhysicsTest_numSteps);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, sithPhysicsTest_thing.moveInfo.physics.physicsRolloverFrames);
    sithPhysicsTest_AssertVector(&delta, &sithPhysicsTest_thing.moveInfo.physics.deltaVelocity);
}

TEST(sithPhysics, TestNegativeAccumulatedTimeDoesNotIntegrate)
{
    // Fixed: A negative accumulated time takes no steps; the original wrapped the
    // floored count through an unsigned conversion and ran about 4 billion steps.
    static const struct { float timestep; float rollover; } aCases[] = {
        { -0.02f, 0.0f }, { -0.005f, 0.0f }, { 0.01f, -0.03f }
    };
    for ( size_t i = 0; i < sizeof(aCases) / sizeof(aCases[0]); ++i )
    {
        sithPhysicsTest_InitThing();
        SithPhysicsInfo* pPhysics = &sithPhysicsTest_thing.moveInfo.physics;
        pPhysics->physicsRolloverFrames = aCases[i].rollover;
        pPhysics->deltaVelocity = (rdVector3){ 4.0f, 5.0f, 6.0f };
        pPhysics->gravityForce = (rdVector3){ 7.0f, 8.0f, 9.0f };
        const rdVector3 velocity = pPhysics->velocity;
        const rdVector3 delta = pPhysics->deltaVelocity;
        const rdVector3 zero = { 0 };

        sithPhysics_UpdateDetachedPlayerPhysics(&sithPhysicsTest_thing, aCases[i].timestep);

        TEST_ASSERT_EQUAL_size_t(0, sithPhysicsTest_numSteps);
        TEST_ASSERT_EQUAL_FLOAT(aCases[i].timestep + aCases[i].rollover, pPhysics->physicsRolloverFrames);
        TEST_ASSERT_EQUAL_MEMORY(&velocity, &pPhysics->velocity, sizeof(velocity));
        TEST_ASSERT_EQUAL_MEMORY(&delta, &pPhysics->deltaVelocity, sizeof(delta));
        TEST_ASSERT_EQUAL_MEMORY(&zero, &pPhysics->gravityForce, sizeof(zero));
    }
}

TEST(sithPhysics, TestFallingGravityUsesCompleteStepsAndCarriesRemainder)
{
    SithPhysicsInfo* pPhysics = sithPhysicsTest_InitFall();
    sithPhysicsTest_maxSteps = 2;
    sithPhysics_UpdateDetachedPlayerPhysics(&sithPhysicsTest_thing, 0.05f);
    // Two authored Euler steps: vz = -0.2, -0.4; dz = -0.004 - 0.008.
    const rdVector3 velocity = { 1.0f, 2.0f, -0.4f };
    const rdVector3 delta = { 0.04f, 0.08f, -0.012f };
    const rdVector3 gravity = { 0.0f, 0.0f, -0.2f };
    TEST_ASSERT_EQUAL_size_t(2, sithPhysicsTest_numSteps);
    TEST_ASSERT_FLOAT_WITHIN(0.000001f, 0.01f, pPhysics->physicsRolloverFrames);
    sithPhysicsTest_AssertVector(&velocity, &pPhysics->velocity);
    sithPhysicsTest_AssertVector(&delta, &pPhysics->deltaVelocity);
    sithPhysicsTest_AssertVector(&gravity, &pPhysics->gravityForce);

    // The outer movement owner clears displacement before the next frame.
    pPhysics->deltaVelocity = (rdVector3){ 0 };
    sithPhysicsTest_maxSteps = 3;
    sithPhysics_UpdateDetachedPlayerPhysics(&sithPhysicsTest_thing, 0.01f);
    const rdVector3 nextVelocity = { 1.0f, 2.0f, -0.6f };
    const rdVector3 nextDelta = { 0.02f, 0.04f, -0.012f };
    TEST_ASSERT_EQUAL_size_t(3, sithPhysicsTest_numSteps);
    TEST_ASSERT_FLOAT_WITHIN(0.000001f, 0.0f, pPhysics->physicsRolloverFrames);
    sithPhysicsTest_AssertVector(&nextVelocity, &pPhysics->velocity);
    sithPhysicsTest_AssertVector(&nextDelta, &pPhysics->deltaVelocity);
    sithPhysicsTest_AssertVector(&gravity, &pPhysics->gravityForce);
}

TEST(sithPhysics, TestDragAndGravityKeepTheOriginalTrajectoryAcrossFrames)
{
    for ( int bSplit = 0; bSplit < 2; ++bSplit )
    {
        SithPhysicsInfo* pPhysics = sithPhysicsTest_InitFall();
        pPhysics->airDrag = 5.0f;
        pPhysics->velocity = (rdVector3){ 1.0f, -2.0f, -0.95f };
        sithPhysicsTest_maxSteps = 2;
        rdVector3 totalDelta = { 0 };
        for ( int frame = 0; frame < (bSplit ? 2 : 1); ++frame )
        {
            pPhysics->deltaVelocity = (rdVector3){ 0 };
            sithPhysics_UpdateDetachedPlayerPhysics(&sithPhysicsTest_thing, bSplit ? 0.02f : 0.04f);
            rdVector_Add3Acc(&totalDelta, &pPhysics->deltaVelocity);
        }
        // Per-step drag multiplier is 0.9. Gravity follows drag, giving vz -1.055 then -1.1495.
        const rdVector3 velocity = { 0.81f, -1.62f, -1.1495f };
        const rdVector3 delta = { 0.0342f, -0.0684f, -0.04409f };
        const rdVector3 gravity = { 0.0f, 0.0f, -0.2f };
        TEST_ASSERT_EQUAL_size_t(2, sithPhysicsTest_numSteps);
        TEST_ASSERT_EQUAL_size_t(2, sithPhysicsTest_numDragCalls);
        TEST_ASSERT_FLOAT_WITHIN(0.000001f, 0.0f, pPhysics->physicsRolloverFrames);
        sithPhysicsTest_AssertVector(&velocity, &pPhysics->velocity);
        sithPhysicsTest_AssertVector(&delta, &totalDelta);
        sithPhysicsTest_AssertVector(&gravity, &pPhysics->gravityForce);
    }
}

TEST(sithPhysics, TestGravityFlagsAndMassPreserveTwentyMillisecondSteps)
{
    static const struct { float mass; SithPhysicsFlags flags; uint32_t sectorFlags; float vz; float dz; float gravity; } aCases[] = {
        { 1.0f, SITH_PF_USEGRAVITY, 0, -0.2f, -0.004f, -0.2f },
        { 1.0f, SITH_PF_USEGRAVITY | SITH_PF_PARTIALGRAVITY, 0, -0.1f, -0.002f, -0.1f },
        { 1.0f, SITH_PF_USEGRAVITY, SITH_SECTOR_NOGRAVITY, 0.0f, 0.0f, 0.0f },
        { 0.0f, SITH_PF_USEGRAVITY, 0, 0.0f, 0.0f, 0.0f },
        { 1.0f, 0, 0, 0.0f, 0.0f, 0.0f }
    };
    for ( size_t i = 0; i < sizeof(aCases) / sizeof(aCases[0]); ++i )
    {
        SithPhysicsInfo* pPhysics = sithPhysicsTest_InitFall();
        pPhysics->mass = aCases[i].mass;
        pPhysics->flags = aCases[i].flags;
        sithPhysicsTest_sector.flags = aCases[i].sectorFlags;
        sithPhysicsTest_maxSteps = 1;
        sithPhysics_UpdateDetachedPlayerPhysics(&sithPhysicsTest_thing, 0.02f);
        const rdVector3 velocity = { 1.0f, 2.0f, aCases[i].vz };
        const rdVector3 delta = { 0.02f, 0.04f, aCases[i].dz };
        const rdVector3 gravity = { 0.0f, 0.0f, aCases[i].gravity };
        TEST_ASSERT_EQUAL_size_t(1, sithPhysicsTest_numSteps);
        TEST_ASSERT_EQUAL_FLOAT(0.0f, pPhysics->physicsRolloverFrames);
        sithPhysicsTest_AssertVector(&velocity, &pPhysics->velocity);
        sithPhysicsTest_AssertVector(&delta, &pPhysics->deltaVelocity);
        sithPhysicsTest_AssertVector(&gravity, &pPhysics->gravityForce);
    }
}

TEST(sithPhysics, TestLocalAndSectorThrustUseTheSameFixedStepAsGravity)
{
    static const struct { SithPhysicsFlags flags; rdVector3 velocity; rdVector3 delta; size_t sectorCalls; } aCases[] = {
        { SITH_PF_USETHRUST | SITH_PF_USEGRAVITY, { 0.48f, 0.96f, 1.04f }, { 0.0144f, 0.0288f, 0.0312f }, 2 },
        { SITH_PF_USETHRUST | SITH_PF_USEGRAVITY | SITH_PF_NOTHRUST, { 0.4f, 0.8f, 0.8f }, { 0.012f, 0.024f, 0.024f }, 0 }
    };
    for ( size_t i = 0; i < sizeof(aCases) / sizeof(aCases[0]); ++i )
    {
        SithPhysicsInfo* pPhysics = sithPhysicsTest_InitFall();
        pPhysics->flags = aCases[i].flags;
        pPhysics->velocity = (rdVector3){ 0 };
        pPhysics->thrust = (rdVector3){ 10.0f, 20.0f, 30.0f };
        sithPhysicsTest_sector.flags = SITH_SECTOR_USETHRUST;
        sithPhysicsTest_sector.thrust = (rdVector3){ 2.0f, 4.0f, 6.0f };
        sithPhysicsTest_maxSteps = 2;
        sithPhysics_UpdateDetachedPlayerPhysics(&sithPhysicsTest_thing, 0.05f);
        const rdVector3 gravity = { 0.0f, 0.0f, -0.2f };
        TEST_ASSERT_EQUAL_size_t(2, sithPhysicsTest_numSteps);
        TEST_ASSERT_EQUAL_size_t(aCases[i].sectorCalls, sithPhysicsTest_numSectorCalls);
        TEST_ASSERT_FLOAT_WITHIN(0.000001f, 0.01f, pPhysics->physicsRolloverFrames);
        sithPhysicsTest_AssertVector(&aCases[i].velocity, &pPhysics->velocity);
        sithPhysicsTest_AssertVector(&aCases[i].delta, &pPhysics->deltaVelocity);
        sithPhysicsTest_AssertVector(&gravity, &pPhysics->gravityForce);
    }
}

TEST(sithPhysics, TestFallingVelocityCrossesTheLedgeSpeedGateAtOriginalSteps)
{
    SithPhysicsInfo* pPhysics = sithPhysicsTest_InitFall();
    pPhysics->airDrag = 5.0f;
    pPhysics->velocity = (rdVector3){ 0.0f, 0.0f, -0.95f };
    sithPhysicsTest_maxSteps = 2;
    sithPhysics_UpdateDetachedPlayerPhysics(&sithPhysicsTest_thing, 0.02f);
    const rdVector3 firstVelocity = { 0.0f, 0.0f, -1.055f };
    sithPhysicsTest_AssertVector(&firstVelocity, &pPhysics->velocity);
    // This is only the velocity input to ProcessFallingMove's < 1.1 gate, not a ledge query.
    TEST_ASSERT_TRUE(rdVector_Len3(&pPhysics->velocity) < 1.1f);
    pPhysics->deltaVelocity = (rdVector3){ 0 };
    sithPhysics_UpdateDetachedPlayerPhysics(&sithPhysicsTest_thing, 0.02f);
    const rdVector3 secondVelocity = { 0.0f, 0.0f, -1.1495f };
    const rdVector3 secondDelta = { 0.0f, 0.0f, -0.02299f };
    sithPhysicsTest_AssertVector(&secondVelocity, &pPhysics->velocity);
    sithPhysicsTest_AssertVector(&secondDelta, &pPhysics->deltaVelocity);
    TEST_ASSERT_TRUE(rdVector_Len3(&pPhysics->velocity) >= 1.1f);
    TEST_ASSERT_EQUAL_size_t(2, sithPhysicsTest_numSteps);
    TEST_ASSERT_EQUAL_size_t(2, sithPhysicsTest_numDragCalls);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pPhysics->physicsRolloverFrames);
}

TEST(sithPhysics, TestModernGroundAccelerationReachesTargetWithoutPassingIt)
{
    // Added: the Modern start helper adds velocity before the original player physics steps.
    SithPhysicsInfo* pPhysics = &sithPhysicsTest_thing.moveInfo.physics;
    pPhysics->flags      = SITH_PF_USETHRUST;
    pPhysics->airDrag    = 5.0f;
    pPhysics->velocity   = (rdVector3){ 0 };
    pPhysics->thrust     = (rdVector3){ 0 };

    sithPhysicsTest_thing.moveStatus = SITHPLAYERMOVE_WALKING;

    SithThing* pSavedPlayerThing = sithPlayer_g_pLocalPlayerThing;
    bool bSavedModernStyle       = sithControl_IsModernControlStyle();
    sithPlayer_g_pLocalPlayerThing = &sithPhysicsTest_thing;
    sithControl_SetModernControlStyle(true);

    // Walk thrust 0.9 reaches 0.3 * 0.9 / 5 = 0.054 under the player physics.
    const float targetSpeed = 0.3f * 0.9f / 5.0f;
    float timeTo95 = -1.0f;
    bool bPassed   = true;
    sithPhysicsTest_maxSteps = 10;
    for ( size_t i = 0; i < 10; ++i )
    {
        // The original movement code sets the frame's thrust before the helper call.
        pPhysics->thrust.y = 0.9f;

        sithModernControls_ApplyGroundAcceleration(&sithPhysicsTest_thing, 0.02f);
        sithPhysics_UpdateDetachedPlayerPhysics(&sithPhysicsTest_thing, 0.02f);

        float speed = rdVector_Len2((const rdVector2*)&pPhysics->velocity);
        if ( speed > targetSpeed + 0.000001f )
        {
            bPassed = false;
        }

        if ( timeTo95 < 0.0f && speed >= 0.95f * targetSpeed )
        {
            timeTo95 = (float)(i + 1) * 0.02f;
        }
    }

    sithPlayer_g_pLocalPlayerThing = pSavedPlayerThing;
    sithControl_SetModernControlStyle(bSavedModernStyle);

    TEST_ASSERT_TRUE(bPassed);
    TEST_ASSERT_TRUE(timeTo95 > 0.0f);
    TEST_ASSERT_TRUE(timeTo95 <= 0.2f);
}

TEST_GROUP_RUNNER(sithPhysics)
{
    RUN_TEST_CASE(sithPhysics, TestStartupIgnoresStoredNonOriginalRates);
    RUN_TEST_CASE(sithPhysics, TestStartupDoesNotCreateFixedRateSetting);
    RUN_TEST_CASE(sithPhysics, TestNegativeAccumulatedTimeDoesNotIntegrate);
    RUN_TEST_CASE(sithPhysics, TestFallingGravityUsesCompleteStepsAndCarriesRemainder);
    RUN_TEST_CASE(sithPhysics, TestDragAndGravityKeepTheOriginalTrajectoryAcrossFrames);
    RUN_TEST_CASE(sithPhysics, TestGravityFlagsAndMassPreserveTwentyMillisecondSteps);
    RUN_TEST_CASE(sithPhysics, TestLocalAndSectorThrustUseTheSameFixedStepAsGravity);
    RUN_TEST_CASE(sithPhysics, TestFallingVelocityCrossesTheLedgeSpeedGateAtOriginalSteps);
    RUN_TEST_CASE(sithPhysics, TestModernGroundAccelerationReachesTargetWithoutPassingIt);
}
