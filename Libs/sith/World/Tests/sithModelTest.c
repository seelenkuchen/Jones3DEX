#include <unity_fixture.h>

#include <string.h>

#include <sith/World/sithModel.h>
#include <sith/World/sithWorld.h>

#include <std/General/stdUtil.h>

TEST_GROUP(sithModel);

static SithWorld  sithModelTest_world;
static SithWorld  sithModelTest_staticWorld;
static rdModel3   sithModelTest_aModels[2];
static rdModel3   sithModelTest_aStaticModels[3];
static SithWorld* sithModelTest_pSavedWorld;
static SithWorld* sithModelTest_pSavedStaticWorld;

TEST_SETUP(sithModel)
{
    memset(&sithModelTest_world, 0, sizeof(sithModelTest_world));
    memset(&sithModelTest_staticWorld, 0, sizeof(sithModelTest_staticWorld));
    sithModelTest_world.aModels           = sithModelTest_aModels;
    sithModelTest_world.numModels         = STD_ARRAYLEN(sithModelTest_aModels);
    sithModelTest_staticWorld.aModels     = sithModelTest_aStaticModels;
    sithModelTest_staticWorld.numModels   = STD_ARRAYLEN(sithModelTest_aStaticModels);

    sithModelTest_pSavedWorld       = sithWorld_g_pCurrentWorld;
    sithModelTest_pSavedStaticWorld = sithWorld_g_pStaticWorld;
    sithWorld_g_pCurrentWorld       = &sithModelTest_world;
    sithWorld_g_pStaticWorld        = &sithModelTest_staticWorld;
}

TEST_TEAR_DOWN(sithModel)
{
    sithWorld_g_pCurrentWorld = sithModelTest_pSavedWorld;
    sithWorld_g_pStaticWorld  = sithModelTest_pSavedStaticWorld;
}

TEST(sithModel, IndexRoundTripsThroughTheSavegameInt16)
{
    // Savegames store the index as int16 and read it back sign-extended (sithDSS holster models,
    // hit ledge model). The original lookup takes 16 bits, so a static index must still resolve.
    const int staticIdx = sithModel_GetModelIndex(&sithModelTest_aStaticModels[2]);
    TEST_ASSERT_EQUAL_INT(SITHWORLD_STATICINDEX(2), staticIdx);

    const int16_t saved = (int16_t)staticIdx;
    TEST_ASSERT_TRUE(saved < 0);
    TEST_ASSERT_EQUAL_PTR(&sithModelTest_aStaticModels[2], sithModel_GetModelByIndex(saved));
    TEST_ASSERT_EQUAL_PTR(&sithModelTest_aStaticModels[2], sithModel_GetModelByIndex((uint16_t)saved));

    // Level models and out-of-range indices are unchanged.
    TEST_ASSERT_EQUAL_PTR(&sithModelTest_aModels[1], sithModel_GetModelByIndex(1));
    TEST_ASSERT_NULL(sithModel_GetModelByIndex(2));
    TEST_ASSERT_NULL(sithModel_GetModelByIndex(SITHWORLD_STATICINDEX(3)));
}

TEST_GROUP_RUNNER(sithModel)
{
    RUN_TEST_CASE(sithModel, IndexRoundTripsThroughTheSavegameInt16);
}
