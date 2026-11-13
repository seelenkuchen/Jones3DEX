// Added: the game-side rumble triggers of ADR-0028. The three game functions run for real against
// the Null column's stdControl, whose test seam records every request the adapter forwards, so a
// case checks both the trigger condition and the exact intensities and duration.
#include <unity_fixture.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <rdroid/Math/rdVector.h>

#include <sith/Gameplay/sithPlayer.h>
#include <sith/Main/sithMain.h>
#include <sith/World/sithExplosion.h>
#include <sith/World/sithThing.h>
#include <sith/World/sithWeapon.h>

#include <std/General/stdControl.h>
#include <std/Null/stdControlNull.h>

TEST_GROUP(sithRumble);

static tHostServices sithRumbleTest_hostServices;
static SithThing sithRumbleTest_player;
static SithThing sithRumbleTest_other;
static SithThing sithRumbleTest_weaponTemplate;
static SithThing sithRumbleTest_explosion;

static int J3DAPI sithRumbleTest_Print(const char* pFormat, ...)
{
    (void)pFormat;
    return 0;
}

static void J3DAPI sithRumbleTest_Assert(const char* pCondition, const char* pFile, int line)
{
    (void)pCondition;
    (void)pFile;
    (void)line;
    abort();
}

static void* J3DAPI sithRumbleTest_Malloc(size_t size)
{
    return malloc(size ? size : 1u);
}

static void J3DAPI sithRumbleTest_Free(void* pMemory)
{
    free(pMemory);
}

static void sithRumbleTest_ResetThings(void)
{
    memset(&sithRumbleTest_player, 0, sizeof(sithRumbleTest_player));
    memset(&sithRumbleTest_other, 0, sizeof(sithRumbleTest_other));
    memset(&sithRumbleTest_weaponTemplate, 0, sizeof(sithRumbleTest_weaponTemplate));
    memset(&sithRumbleTest_explosion, 0, sizeof(sithRumbleTest_explosion));

    sithRumbleTest_player.type = SITH_THING_PLAYER;
    sithRumbleTest_other.type  = SITH_THING_ACTOR;
    sithRumbleTest_weaponTemplate.type = SITH_THING_WEAPON;
    sithRumbleTest_explosion.type      = SITH_THING_EXPLOSION;

    // The damage cases must stop inside sithActor_DamageActor, before it needs sound, inventory or
    // AI state; an invulnerable target returns from that function without touching them.
    sithRumbleTest_player.thingInfo.actorInfo.flags |= SITH_AF_INVULNERABLE;
    sithRumbleTest_other.thingInfo.actorInfo.flags  |= SITH_AF_INVULNERABLE;

    sithPlayer_g_pLocalPlayerThing = &sithRumbleTest_player;
}

TEST_SETUP(sithRumble)
{
    memset(&sithRumbleTest_hostServices, 0, sizeof(sithRumbleTest_hostServices));
    sithRumbleTest_hostServices.pAssert       = sithRumbleTest_Assert;
    sithRumbleTest_hostServices.pStatusPrint  = sithRumbleTest_Print;
    sithRumbleTest_hostServices.pMessagePrint = sithRumbleTest_Print;
    sithRumbleTest_hostServices.pWarningPrint = sithRumbleTest_Print;
    sithRumbleTest_hostServices.pErrorPrint   = sithRumbleTest_Print;
    sithRumbleTest_hostServices.pDebugPrint   = sithRumbleTest_Print;
    sithRumbleTest_hostServices.pMalloc       = sithRumbleTest_Malloc;
    sithRumbleTest_hostServices.pFree         = sithRumbleTest_Free;
    sith_g_pHS  = &sithRumbleTest_hostServices;
    std_g_pHS   = &sithRumbleTest_hostServices;
    rdroid_g_pHS = &sithRumbleTest_hostServices;

    sithRumbleTest_ResetThings();
    stdControl_EnableRumble(true);
    stdControl_ResetRumbleForTest();
}

TEST_TEAR_DOWN(sithRumble)
{
    sithPlayer_g_pLocalPlayerThing = NULL;
    stdControl_EnableRumble(true);
    stdControl_ResetRumbleForTest();
    sith_g_pHS   = NULL;
    std_g_pHS    = NULL;
    rdroid_g_pHS = NULL;
}

TEST(sithRumble, DamageToThePlayerRumblesOnce)
{
    sithThing_DamageThing(&sithRumbleTest_player, &sithRumbleTest_player, 20.0f,
        SITH_DAMAGE_IMPACT);

    TEST_ASSERT_EQUAL_size_t(1, stdControl_GetRumbleCountForTest());

    float low = 0.0f;
    float high = 0.0f;
    uint32_t duration = 0;
    stdControl_GetRumbleForTest(0, &low, &high, &duration);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, low);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, high);
    TEST_ASSERT_EQUAL_UINT32(180u, duration);
}

TEST(sithRumble, DamageToAnotherThingDoesNotRumble)
{
    sithThing_DamageThing(&sithRumbleTest_other, &sithRumbleTest_other, 20.0f,
        SITH_DAMAGE_IMPACT);

    TEST_ASSERT_EQUAL_size_t(0, stdControl_GetRumbleCountForTest());
}

TEST(sithRumble, PlayerFireRumblesOnce)
{
    sithWeapon_WeaponFire(&sithRumbleTest_player, &sithRumbleTest_weaponTemplate, NULL, NULL,
        0, 0, 0.0f, 0, 0.0f);

    TEST_ASSERT_EQUAL_size_t(1, stdControl_GetRumbleCountForTest());

    float low = 0.0f;
    float high = 0.0f;
    uint32_t duration = 0;
    stdControl_GetRumbleForTest(0, &low, &high, &duration);
    TEST_ASSERT_EQUAL_FLOAT(0.15f, low);
    TEST_ASSERT_EQUAL_FLOAT(0.45f, high);
    TEST_ASSERT_EQUAL_UINT32(90u, duration);
}

TEST(sithRumble, BlastInsideItsRangeRumblesOnce)
{
    sithRumbleTest_explosion.thingInfo.explosionInfo.range = 10.0f;
    rdVector_Set3(&sithRumbleTest_explosion.pos, 6.0f, 0.0f, 0.0f);

    sithExplosion_MakeBlast(&sithRumbleTest_explosion);

    TEST_ASSERT_EQUAL_size_t(1, stdControl_GetRumbleCountForTest());

    float low = 0.0f;
    float high = 0.0f;
    uint32_t duration = 0;
    stdControl_GetRumbleForTest(0, &low, &high, &duration);
    TEST_ASSERT_EQUAL_FLOAT(0.4f, low);
    TEST_ASSERT_EQUAL_FLOAT(0.24f, high);
    TEST_ASSERT_EQUAL_UINT32(350u, duration);
}

TEST(sithRumble, BlastOutsideItsRangeDoesNotRumble)
{
    sithRumbleTest_explosion.thingInfo.explosionInfo.range = 10.0f;
    rdVector_Set3(&sithRumbleTest_explosion.pos, 11.0f, 0.0f, 0.0f);

    sithExplosion_MakeBlast(&sithRumbleTest_explosion);

    TEST_ASSERT_EQUAL_size_t(0, stdControl_GetRumbleCountForTest());
}

TEST(sithRumble, DisabledRumbleForwardsNothing)
{
    stdControl_EnableRumble(false);

    sithThing_DamageThing(&sithRumbleTest_player, &sithRumbleTest_player, 20.0f,
        SITH_DAMAGE_IMPACT);

    TEST_ASSERT_EQUAL_size_t(0, stdControl_GetRumbleCountForTest());
}

TEST_GROUP_RUNNER(sithRumble)
{
    RUN_TEST_CASE(sithRumble, DamageToThePlayerRumblesOnce);
    RUN_TEST_CASE(sithRumble, DamageToAnotherThingDoesNotRumble);
    RUN_TEST_CASE(sithRumble, PlayerFireRumblesOnce);
    RUN_TEST_CASE(sithRumble, BlastInsideItsRangeRumblesOnce);
    RUN_TEST_CASE(sithRumble, BlastOutsideItsRangeDoesNotRumble);
    RUN_TEST_CASE(sithRumble, DisabledRumbleForwardsNothing);
}
