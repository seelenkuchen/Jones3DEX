// Added: the interaction target query. The suite compiles the real sithPlayerActions.c,
// sithPlayerControls.c and sithCollision.c with synthetic geometry and records the COG messages a
// press sends, so a test proves the prompt query names exactly the surface, thing or item the press
// acts on and leaves the collision scratch untouched.
#include <unity_fixture.h>

#include <string.h>

#include <rdroid/Math/rdVector.h>
#include <sith/Cog/sithCog.h>
#include <sith/Devices/sithControl.h>
#include <sith/Engine/sithCollision.h>
#include <sith/Engine/sithIntersect.h>
#include <sith/Gameplay/sithInventory.h>
#include <sith/Gameplay/sithPlayer.h>
#include <sith/Gameplay/sithPlayerActions.h>
#include <sith/Gameplay/sithPlayerControls.h>
#include <sith/Gameplay/sithTime.h>
#include <sith/Gameplay/sithWhip.h>
#include <sith/Main/sithMain.h>
#include <sith/World/sithThing.h>
#include <sith/World/sithWeapon.h>
#include <sith/World/sithWorld.h>
#include <std/General/stdUtil.h>

static SithWorld sithInteractionTest_world;
static SithSector sithInteractionTest_sector;
static SithSurface sithInteractionTest_aSurfaces[2];
#define sithInteractionTest_surface sithInteractionTest_aSurfaces[0]
#define sithInteractionTest_floorSurface sithInteractionTest_aSurfaces[1]
static int sithInteractionTest_aFaceVertices[4];
static int sithInteractionTest_aFloorFaceVertices[4];
static rdVector3 sithInteractionTest_aVertices[8];
static SithThing sithInteractionTest_aThings[4];
static SithPlayer sithInteractionTest_player;
static SithCog sithInteractionTest_cog;
static SithCogScript sithInteractionTest_script;
static rdModel3 sithInteractionTest_model;
static tHostServices sithInteractionTest_hostServices;
static tHostServices* sithInteractionTest_pSavedHostServices;
static SithWorld* sithInteractionTest_pSavedWorld;
static SithThing* sithInteractionTest_pSavedLocalPlayer;
static int sithInteractionTest_numAsserts;
static int sithInteractionTest_numSurfaceMessages;
static int sithInteractionTest_numThingMessages;
static SithSurface* sithInteractionTest_pLastSurface;
static SithThing* sithInteractionTest_pLastThing;

#define PLAYER (&sithInteractionTest_aThings[0])
#define COG (&sithInteractionTest_aThings[1])
#define ITEM (&sithInteractionTest_aThings[2])
#define OTHERPLAYER (&sithInteractionTest_aThings[3])

TEST_GROUP(sithInteraction);

static int J3DAPI sithInteractionTest_Print(const char* pFormat, ...)
{
    (void)pFormat;
    return 0;
}

static void J3DAPI sithInteractionTest_RecordAssert(const char* pText, const char* pFile, int line)
{
    (void)pText;
    (void)pFile;
    (void)line;
    ++sithInteractionTest_numAsserts;
}

void J3DAPI sithInteractionTest_SurfaceSendMessage(const SithSurface* pSurf,
    const SithThing* pSrcThing, SithCogMsgType messageType)
{
    (void)pSrcThing;
    (void)messageType;
    ++sithInteractionTest_numSurfaceMessages;
    sithInteractionTest_pLastSurface = (SithSurface*)pSurf;
}

int J3DAPI sithInteractionTest_ThingSendMessage(const SithThing* pThing, const SithThing* pSrcThing,
    SithCogMsgType messageType)
{
    (void)pSrcThing;
    (void)messageType;
    ++sithInteractionTest_numThingMessages;
    sithInteractionTest_pLastThing = (SithThing*)pThing;
    return 0;
}

void J3DAPI sithInteractionTest_SendMessage(SithCog* pCog, SithCogMsgType messageType,
    SithCogSymbolRefType senderType, int senderIdx, SithCogSymbolRefType srcType, int srcIdx,
    int linkId)
{
    (void)pCog;
    (void)messageType;
    (void)senderType;
    (void)senderIdx;
    (void)srcType;
    (void)srcIdx;
    (void)linkId;
}

void J3DAPI sithInteractionTest_SectorSendMessage(const SithSector* pSector, const SithThing* pThing,
    SithCogMsgType messageType)
{
    (void)pSector;
    (void)pThing;
    (void)messageType;
}

int J3DAPI sithInteractionTest_ThingSendMessageEx(const SithThing* pThing,
    const SithThing* pSrcThing, SithCogMsgType messageType, int param0, int param1, int param2,
    int param3)
{
    (void)pThing;
    (void)pSrcThing;
    (void)messageType;
    (void)param0;
    (void)param1;
    (void)param2;
    (void)param3;
    return 0;
}

static void sithInteractionTest_InitThing(SithThing* pThing, int idx, SithThingType type,
    const rdVector3* pPos)
{
    STD_ZEROMEM(pThing, sizeof(*pThing));
    pThing->idx       = idx;
    pThing->type      = type;
    pThing->controlType = SITH_CT_PLAYER;
    pThing->moveType  = SITH_MT_PHYSICS;
    pThing->pos       = *pPos;
    pThing->pInSector = &sithInteractionTest_sector;
    pThing->collide.type = SITH_COLLIDE_SPHERE;
    pThing->collide.size = 0.05f;
    pThing->collide.movesize = 0.05f;
    pThing->orient.rvec = (rdVector3){ 1.0f, 0.0f, 0.0f };
    pThing->orient.lvec = (rdVector3){ 0.0f, 1.0f, 0.0f };
    pThing->orient.uvec = (rdVector3){ 0.0f, 0.0f, 1.0f };
    pThing->pNextThingInSector = NULL;
}

static void sithInteractionTest_ResetScene(void)
{
    STD_ZEROMEM(&sithInteractionTest_world, sizeof(sithInteractionTest_world));
    STD_ZEROMEM(&sithInteractionTest_sector, sizeof(sithInteractionTest_sector));
    STD_ZEROMEM(sithInteractionTest_aSurfaces, sizeof(sithInteractionTest_aSurfaces));
    STD_ZEROMEM(sithInteractionTest_aThings, sizeof(sithInteractionTest_aThings));
    STD_ZEROMEM(&sithInteractionTest_player, sizeof(sithInteractionTest_player));

    sithInteractionTest_aVertices[0] = (rdVector3){ -0.2f, 0.06f, -0.2f };
    sithInteractionTest_aVertices[1] = (rdVector3){  0.2f, 0.06f, -0.2f };
    sithInteractionTest_aVertices[2] = (rdVector3){  0.2f, 0.06f,  0.2f };
    sithInteractionTest_aVertices[3] = (rdVector3){ -0.2f, 0.06f,  0.2f };
    for ( int i = 0; i < 4; ++i )
    {
        sithInteractionTest_aFaceVertices[i] = i;
        sithInteractionTest_aFloorFaceVertices[i] = i + 4;
    }

    sithInteractionTest_world.aThings    = sithInteractionTest_aThings;
    sithInteractionTest_world.numThings  = STD_ARRAYLEN(sithInteractionTest_aThings);
    sithInteractionTest_world.aSectors   = &sithInteractionTest_sector;
    sithInteractionTest_world.numSectors = 1;
    sithInteractionTest_world.aVertices  = sithInteractionTest_aVertices;
    sithInteractionTest_world.numVertices = STD_ARRAYLEN(sithInteractionTest_aVertices);

    sithInteractionTest_sector.pFirstSurface = &sithInteractionTest_surface;
    sithInteractionTest_sector.numSurfaces   = 1;
    sithInteractionTest_sector.pFirstThingInSector = NULL;

    sithInteractionTest_surface.pSector = &sithInteractionTest_sector;
    sithInteractionTest_surface.flags   = SITH_SURFACE_COGLINKED | SITH_SURFACE_COLLISION;
    sithInteractionTest_surface.face.numVertices = 4;
    sithInteractionTest_surface.face.aVertices   = sithInteractionTest_aFaceVertices;
    sithInteractionTest_surface.face.normal      = (rdVector3){ 0.0f, -1.0f, 0.0f };

    const rdVector3 zeroPos = { 0.0f, 0.0f, 0.0f };
    const rdVector3 frontPos = { 0.0f, 0.06f, 0.0f };
    sithInteractionTest_InitThing(PLAYER, 0, SITH_THING_PLAYER, &zeroPos);
    sithInteractionTest_InitThing(COG, 1, SITH_THING_COG, &frontPos);
    sithInteractionTest_InitThing(ITEM, 2, SITH_THING_ITEM, &frontPos);
    sithInteractionTest_InitThing(OTHERPLAYER, 3, SITH_THING_PLAYER, &frontPos);

    // Added: a COG with an activated handler is linked to the surface and attached to the things.
    STD_ZEROMEM(&sithInteractionTest_cog, sizeof(sithInteractionTest_cog));
    STD_ZEROMEM(&sithInteractionTest_script, sizeof(sithInteractionTest_script));
    sithInteractionTest_cog.pScript = &sithInteractionTest_script;
    sithInteractionTest_script.numHandlers = 1;
    sithInteractionTest_script.aHandlers[0].type = SITHCOG_MSG_ACTIVATE;
    STD_STRCPY(sithInteractionTest_cog.aName, "test_cog");

    PLAYER->moveStatus = SITHPLAYERMOVE_STILL;
    PLAYER->thingInfo.actorInfo.pPlayer = &sithInteractionTest_player;
    PLAYER->thingInfo.actorInfo.weaponInfo.selectedWeaponID = SITHWEAPON_NOWEAPONSELECTED;
    PLAYER->thingInfo.actorInfo.weaponInfo.curWeaponID = SITHWEAPON_NO_WEAPON;
    sithInteractionTest_player.curWeaponID = SITHWEAPON_NO_WEAPON;
    sithInteractionTest_player.curItemID   = 0;

    COG->flags = SITH_TF_COGLINKED;

    sithWorld_g_pCurrentWorld      = &sithInteractionTest_world;
    sithPlayer_g_pLocalPlayerThing = PLAYER;
    sithPlayer_g_playerNum         = 0;

    sithTime_g_clockTime    = 1000u;
    sithTime_g_secGameTime  = 10.0;
    sithTime_g_bPaused      = 0;
    sithPlayerControls_g_bCutsceneMode = 0;
    sithPlayerActions_msecCurActivateTime = 0;

    sithInteractionTest_numSurfaceMessages = 0;
    sithInteractionTest_numThingMessages   = 0;
    sithInteractionTest_pLastSurface       = NULL;
    sithInteractionTest_pLastThing         = NULL;

    COG->pCog = &sithInteractionTest_cog;
    ITEM->pCog = &sithInteractionTest_cog;
    OTHERPLAYER->pCog = &sithInteractionTest_cog;
    sithCog_LinkCogToSurface(&sithInteractionTest_cog, &sithInteractionTest_surface, 1,
        SITHTHING_TYPEMASK(SITH_THING_PLAYER));
}

// Added: a vertical wall in front of the player at y; its top edge is the climbable ledge height.
static void sithInteractionTest_SetWall(float y, float bottomZ, float topZ, SithSurfaceFlag flags)
{
    sithInteractionTest_aVertices[0] = (rdVector3){ -0.2f, y, bottomZ };
    sithInteractionTest_aVertices[1] = (rdVector3){  0.2f, y, bottomZ };
    sithInteractionTest_aVertices[2] = (rdVector3){  0.2f, y, topZ };
    sithInteractionTest_aVertices[3] = (rdVector3){ -0.2f, y, topZ };
    sithInteractionTest_surface.flags = flags;
    sithInteractionTest_surface.face.normal = (rdVector3){ 0.0f, -1.0f, 0.0f };
}

// Added: a floor below the player for the push/pull move checks.
static void sithInteractionTest_AddFloor(float z)
{
    sithInteractionTest_aVertices[4] = (rdVector3){ -1.0f, -1.0f, z };
    sithInteractionTest_aVertices[5] = (rdVector3){  1.0f, -1.0f, z };
    sithInteractionTest_aVertices[6] = (rdVector3){  1.0f,  1.0f, z };
    sithInteractionTest_aVertices[7] = (rdVector3){ -1.0f,  1.0f, z };

    sithInteractionTest_floorSurface.pSector = &sithInteractionTest_sector;
    sithInteractionTest_floorSurface.flags   = SITH_SURFACE_COLLISION;
    sithInteractionTest_floorSurface.face.numVertices = 4;
    sithInteractionTest_floorSurface.face.aVertices   = sithInteractionTest_aFloorFaceVertices;
    sithInteractionTest_floorSurface.face.normal      = (rdVector3){ 0.0f, 0.0f, 1.0f };
    sithInteractionTest_sector.numSurfaces = 2;
}

TEST_SETUP(sithInteraction)
{
    STD_ZEROMEM(&sithInteractionTest_hostServices, sizeof(sithInteractionTest_hostServices));
    sithInteractionTest_hostServices.pStatusPrint  = sithInteractionTest_Print;
    sithInteractionTest_hostServices.pMessagePrint = sithInteractionTest_Print;
    sithInteractionTest_hostServices.pWarningPrint = sithInteractionTest_Print;
    sithInteractionTest_hostServices.pErrorPrint   = sithInteractionTest_Print;
    sithInteractionTest_hostServices.pDebugPrint   = sithInteractionTest_Print;
    sithInteractionTest_hostServices.pAssert       = sithInteractionTest_RecordAssert;

    sithInteractionTest_pSavedHostServices = sith_g_pHS;
    sithInteractionTest_pSavedWorld        = sithWorld_g_pCurrentWorld;
    sithInteractionTest_pSavedLocalPlayer  = sithPlayer_g_pLocalPlayerThing;
    sith_g_pHS = &sithInteractionTest_hostServices;
    sithInteractionTest_numAsserts = 0;

    sithCollision_ResetGlobals();
    sithCollision_Startup();
    sithCog_ResetGlobals();
    sithControl_ClearKeyOverrides();
    sithPlayerControls_Reset();
    sithWeapon_ResetGlobals();
    sithWeapon_SetQuickWhipEnabled(J3D_QOL_VALUE(1, 0));
    sithInteractionTest_ResetScene();
    sithWhip_SetTargetsForTest(NULL, NULL);
}

TEST_TEAR_DOWN(sithInteraction)
{
    sithCollision_Shutdown();
    sithWorld_g_pCurrentWorld      = sithInteractionTest_pSavedWorld;
    sithPlayer_g_pLocalPlayerThing = sithInteractionTest_pSavedLocalPlayer;
    sith_g_pHS                     = sithInteractionTest_pSavedHostServices;
    TEST_ASSERT_EQUAL_INT(0, sithInteractionTest_numAsserts);
}

static void sithInteractionTest_KeepOnlySurface(const SithSurfaceFlag flags)
{
    sithInteractionTest_surface.flags = flags;
    sithInteractionTest_sector.pFirstThingInSector = NULL;
}

static void sithInteractionTest_KeepOnlyThing(SithThing* pThing)
{
    sithInteractionTest_surface.flags = SITH_SURFACE_COLLISION; // not COGLINKED
    sithInteractionTest_sector.pFirstThingInSector = pThing;
    pThing->pNextThingInSector = NULL;
}

// Added: puts the whip in the player's hands and installs the whip-aim targets directly, so the
// interaction query tests read the same target choice the press would make.
static void sithInteractionTest_EquipWhip(SithThing* pSwing, SithThing* pClimb)
{
    sithWeapon_InitalizeActorWeapon(PLAYER);
    PLAYER->thingInfo.actorInfo.weaponInfo.curWeaponID = SITHWEAPON_WHIP;
    sithInteractionTest_player.curWeaponID = SITHWEAPON_WHIP;
    sithInventory_SetInventoryFlags(PLAYER, SITHWEAPON_WHIP, SITHINVENTORY_TYPE_UNKNOWN_200);
    sithWhip_SetTargetsForTest(pSwing, pClimb);
}

// Added: a real search target for the whip finders, found by the collision code rather than
// installed with sithWhip_SetTargetsForTest.
static void sithInteractionTest_SetUpWhipSearchThing(SithThing* pThing, const rdVector3* pPos,
    SithThingFlag flags)
{
    sithInteractionTest_KeepOnlyThing(pThing);
    pThing->flags = flags;
    pThing->pos = *pPos;
    pThing->collide.type = SITH_COLLIDE_SPHERE;
    pThing->collide.size = 0.05f;
    pThing->collide.movesize = 0.05f;
    pThing->pInSector = &sithInteractionTest_sector;
}

// Added: the quick whip availability scene: standing on a whip-aim surface with the whip owned.
static void sithInteractionTest_SetUpQuickWhipScene(void)
{
    const rdVector3 swingPos = { 0.0f, 0.1f, 0.0f };
    sithInteractionTest_SetUpWhipSearchThing(COG, &swingPos, SITH_TF_WHIPSWING);

    sithInteractionTest_surface.flags |= SITH_SURFACE_WHIPAIM;
    PLAYER->attach.flags = SITH_ATTACH_SURFACE;
    PLAYER->attach.attachedToStructure.pSurfaceAttached = &sithInteractionTest_surface;

    sithWeapon_InitalizeActorWeapon(PLAYER);
    PLAYER->thingInfo.actorInfo.weaponInfo.curWeaponID = SITHWEAPON_NO_WEAPON;
    sithInteractionTest_player.curWeaponID = SITHWEAPON_NO_WEAPON;
    sithInventory_SetInventoryFlags(PLAYER, SITHWEAPON_WHIP, SITHINVENTORY_TYPE_REGISTERED);
    sithInteractionTest_player.aItems[SITHWEAPON_WHIP].amount = 1.0f;
    sithInteractionTest_player.aItems[SITHWEAPON_WHIP].status = SITHINVENTORY_ITEM_AVAILABLE;
}

TEST(sithInteraction, QueryAndActivateAgreeOnCogSurface)
{
    sithInteractionTest_KeepOnlySurface(SITH_SURFACE_COGLINKED | SITH_SURFACE_COLLISION);

    SithPlayerActionsTarget target;
    TEST_ASSERT_TRUE(sithPlayerActions_QueryActivateTarget(PLAYER, &target));
    TEST_ASSERT_EQUAL_INT(SITHPLAYERACTIONS_TARGET_SURFACE, target.kind);
    TEST_ASSERT_EQUAL_PTR(&sithInteractionTest_surface, target.pSurface);
    TEST_ASSERT_NULL(target.pThing);
    TEST_ASSERT_EQUAL_INT(0, sithInteractionTest_numSurfaceMessages);

    TEST_ASSERT_EQUAL_INT(1, sithPlayerActions_Activate(PLAYER));
    TEST_ASSERT_EQUAL_INT(1, sithInteractionTest_numSurfaceMessages);
    TEST_ASSERT_EQUAL_PTR(&sithInteractionTest_surface, sithInteractionTest_pLastSurface);
}

TEST(sithInteraction, QueryAndActivateAgreeOnCogThing)
{
    sithInteractionTest_KeepOnlyThing(COG);

    SithPlayerActionsTarget target;
    TEST_ASSERT_TRUE(sithPlayerActions_QueryActivateTarget(PLAYER, &target));
    TEST_ASSERT_EQUAL_INT(SITHPLAYERACTIONS_TARGET_THING, target.kind);
    TEST_ASSERT_EQUAL_PTR(COG, target.pThing);
    TEST_ASSERT_NULL(target.pSurface);
    TEST_ASSERT_EQUAL_INT(0, sithInteractionTest_numThingMessages);

    sithPlayerActions_msecCurActivateTime = 0;
    TEST_ASSERT_EQUAL_INT(1, sithPlayerActions_Activate(PLAYER));
    TEST_ASSERT_EQUAL_INT(1, sithInteractionTest_numThingMessages);
    TEST_ASSERT_EQUAL_PTR(COG, sithInteractionTest_pLastThing);
}

TEST(sithInteraction, MovableThingHasNoPromptAndNoActivation)
{
    COG->flags |= SITH_TF_MOVABLE;
    sithInteractionTest_KeepOnlyThing(COG);

    SithPlayerActionsTarget target;
    TEST_ASSERT_FALSE(sithPlayerActions_QueryActivateTarget(PLAYER, &target));
    TEST_ASSERT_EQUAL_INT(SITHPLAYERACTIONS_TARGET_NONE, target.kind);

    TEST_ASSERT_EQUAL_INT(0, sithPlayerActions_Activate(PLAYER));
    TEST_ASSERT_EQUAL_INT(0, sithInteractionTest_numThingMessages);
}

TEST(sithInteraction, DrawnWeaponHasNoPromptAndNoActivation)
{
    sithInteractionTest_KeepOnlySurface(SITH_SURFACE_COGLINKED | SITH_SURFACE_COLLISION);
    sithInteractionTest_player.curWeaponID = SITHWEAPON_PISTOL;
    PLAYER->thingInfo.actorInfo.weaponInfo.curWeaponID = SITHWEAPON_PISTOL;

    SithPlayerActionsTarget target;
    TEST_ASSERT_FALSE(sithPlayerActions_QueryActivateTarget(PLAYER, &target));
    TEST_ASSERT_EQUAL_INT(0, sithPlayerActions_Activate(PLAYER));
    TEST_ASSERT_EQUAL_INT(0, sithInteractionTest_numSurfaceMessages);
}

TEST(sithInteraction, AiDoesNotActivatePlayerThings)
{
    OTHERPLAYER->flags = SITH_TF_COGLINKED;
    PLAYER->controlType = SITH_CT_AI;
    sithInteractionTest_KeepOnlyThing(OTHERPLAYER);

    SithPlayerActionsTarget target;
    TEST_ASSERT_FALSE(sithPlayerActions_QueryActivateTarget(PLAYER, &target));

    TEST_ASSERT_EQUAL_INT(0, sithPlayerActions_Activate(PLAYER));
    TEST_ASSERT_EQUAL_INT(0, sithInteractionTest_numThingMessages);
}

TEST(sithInteraction, RepeatGuardHidesThePromptAndThePress)
{
    sithInteractionTest_KeepOnlySurface(SITH_SURFACE_COGLINKED | SITH_SURFACE_COLLISION);

    TEST_ASSERT_EQUAL_INT(1, sithPlayerActions_Activate(PLAYER));
    TEST_ASSERT_EQUAL_INT(1, sithInteractionTest_numSurfaceMessages);

    SithPlayerActionsTarget target;
    sithTime_g_clockTime = 1100u;
    TEST_ASSERT_FALSE(sithPlayerActions_QueryActivateTarget(PLAYER, &target));
    TEST_ASSERT_EQUAL_INT(0, sithPlayerActions_Activate(PLAYER));
    TEST_ASSERT_EQUAL_INT(1, sithInteractionTest_numSurfaceMessages);

    sithTime_g_clockTime = 1600u;
    TEST_ASSERT_TRUE(sithPlayerActions_QueryActivateTarget(PLAYER, &target));
}

TEST(sithInteraction, StandingQueryPicksTheItemThePressWouldFind)
{
    COG->pInSector = &sithInteractionTest_sector;
    sithInteractionTest_sector.pFirstThingInSector = ITEM;
    ITEM->pNextThingInSector = NULL;
    sithInteractionTest_surface.flags = SITH_SURFACE_COLLISION; // not COGLINKED

    SithPlayerInteraction interaction;
    TEST_ASSERT_TRUE(sithPlayerControls_QueryInteraction(PLAYER, &interaction));
    TEST_ASSERT_EQUAL_INT(SITHPLAYERINTERACTION_PICKUP, interaction.kind);
    TEST_ASSERT_EQUAL_PTR(ITEM, interaction.pThing);

    int bFoundFloorItem = -1;
    SithThing* pItemThing = sithCollision_FindItemThing(PLAYER, &bFoundFloorItem);
    TEST_ASSERT_EQUAL_PTR(ITEM, pItemThing);
    TEST_ASSERT_EQUAL_INT(interaction.bFloorItem, bFoundFloorItem);
}

TEST(sithInteraction, StandingQueryPicksAFloorItem)
{
    ITEM->pos = (rdVector3){ 0.0f, 0.025f, -0.06f };
    sithInteractionTest_sector.pFirstThingInSector = ITEM;
    ITEM->pNextThingInSector = NULL;
    sithInteractionTest_surface.flags = SITH_SURFACE_COLLISION; // not COGLINKED

    SithPlayerInteraction interaction;
    TEST_ASSERT_TRUE(sithPlayerControls_QueryInteraction(PLAYER, &interaction));
    TEST_ASSERT_EQUAL_INT(SITHPLAYERINTERACTION_PICKUP, interaction.kind);
    TEST_ASSERT_EQUAL_PTR(ITEM, interaction.pThing);
    TEST_ASSERT_EQUAL_INT(1, interaction.bFloorItem);

    int bFoundFloorItem = -1;
    TEST_ASSERT_EQUAL_PTR(ITEM, sithCollision_FindItemThing(PLAYER, &bFoundFloorItem));
    TEST_ASSERT_EQUAL_INT(1, bFoundFloorItem);
}

TEST(sithInteraction, ReadOnlySearchLeavesTheThingOrientationTranslation)
{
    // A face-collision target makes the intersection park the thing's position in orient.dvec.
    // The read-only scope must put the original value back.
    STD_ZEROMEM(&sithInteractionTest_model, sizeof(sithInteractionTest_model));
    COG->collide.type = SITH_COLLIDE_FACE;
    COG->renderData.type = RD_THING_MODEL3;
    COG->renderData.data.pModel3 = &sithInteractionTest_model;
    COG->orient.dvec = (rdVector3){ 0.3f, 0.4f, 0.5f };
    const rdVector3 savedDvec = COG->orient.dvec;

    const rdVector3 startPos = { 0.0f, 0.0f, 0.0f };
    const rdVector3 moveNorm = { 0.0f, 1.0f, 0.0f };
    float hitDistance = 0.0f;
    rdModel3Mesh* pMesh = NULL;
    rdFace* pFace = NULL;
    rdVector3 hitNorm = { 0.0f, 0.0f, 0.0f };

    TEST_ASSERT_TRUE(sithCollision_BeginReadOnlySearch());
    TEST_ASSERT_TRUE(sithCollision_IsReadOnlySearchActive());
    sithIntersect_CheckSphereThingIntersection(PLAYER, &startPos, &moveNorm, 0.08f, 0.025f,
        COG, 0, &hitDistance, &pMesh, &pFace, &hitNorm);
    sithCollision_EndReadOnlySearch();
    TEST_ASSERT_FALSE(sithCollision_IsReadOnlySearchActive());

    TEST_ASSERT_EQUAL_FLOAT(savedDvec.x, COG->orient.dvec.x);
    TEST_ASSERT_EQUAL_FLOAT(savedDvec.y, COG->orient.dvec.y);
    TEST_ASSERT_EQUAL_FLOAT(savedDvec.z, COG->orient.dvec.z);
}

TEST(sithInteraction, CogWithoutActivateHandlerHidesUse)
{
    sithInteractionTest_script.numHandlers = 0;
    sithInteractionTest_KeepOnlySurface(SITH_SURFACE_COGLINKED | SITH_SURFACE_COLLISION);

    SithPlayerActionsTarget target;
    TEST_ASSERT_FALSE(sithPlayerActions_QueryActivateTarget(PLAYER, &target));
    TEST_ASSERT_EQUAL_INT(SITHPLAYERACTIONS_TARGET_NONE, target.kind);

    // Activate itself is unchanged: it still sends the message even without a handler.
    TEST_ASSERT_EQUAL_INT(1, sithPlayerActions_Activate(PLAYER));
    TEST_ASSERT_EQUAL_INT(1, sithInteractionTest_numSurfaceMessages);
}

TEST(sithInteraction, ThingWithoutActivateHandlerHidesUse)
{
    sithInteractionTest_script.numHandlers = 0;
    sithInteractionTest_KeepOnlyThing(COG);

    SithPlayerActionsTarget target;
    TEST_ASSERT_FALSE(sithPlayerActions_QueryActivateTarget(PLAYER, &target));

    TEST_ASSERT_EQUAL_INT(1, sithPlayerActions_Activate(PLAYER));
    TEST_ASSERT_EQUAL_INT(1, sithInteractionTest_numThingMessages);
}

TEST(sithInteraction, ThingLinkedCogWithActivateHandlerShowsUse)
{
    COG->pCog = NULL;
    sithCog_LinkCogToThing(&sithInteractionTest_world, &sithInteractionTest_cog, COG, 1,
        SITHTHING_TYPEMASK(SITH_THING_PLAYER));
    sithInteractionTest_KeepOnlyThing(COG);

    SithPlayerActionsTarget target;
    TEST_ASSERT_TRUE(sithPlayerActions_QueryActivateTarget(PLAYER, &target));
    TEST_ASSERT_EQUAL_INT(SITHPLAYERACTIONS_TARGET_THING, target.kind);
    TEST_ASSERT_EQUAL_PTR(COG, target.pThing);
}

TEST(sithInteraction, ItemWithoutActivateHandlerHidesPickUp)
{
    sithInteractionTest_script.numHandlers = 0;
    ITEM->pCog = &sithInteractionTest_cog;
    sithInteractionTest_sector.pFirstThingInSector = ITEM;
    ITEM->pNextThingInSector = NULL;
    sithInteractionTest_surface.flags = SITH_SURFACE_COLLISION; // not COGLINKED

    SithPlayerInteraction interaction;
    TEST_ASSERT_FALSE(sithPlayerControls_QueryInteraction(PLAYER, &interaction));

    // The press's finder still sees the item; only the prompt is withheld.
    int bFoundFloorItem = -1;
    TEST_ASSERT_EQUAL_PTR(ITEM, sithCollision_FindItemThing(PLAYER, &bFoundFloorItem));
}

TEST(sithInteraction, QueryLeavesTheCollisionScratchUnchanged)
{
    sithInteractionTest_KeepOnlySurface(SITH_SURFACE_COGLINKED | SITH_SURFACE_COLLISION);

    const uint64_t before = sithCollision_GetScratchChecksumForTest();
    for ( int i = 0; i < 16; ++i )
    {
        SithPlayerActionsTarget target;
        TEST_ASSERT_TRUE(sithPlayerActions_QueryActivateTarget(PLAYER, &target));
    }
    TEST_ASSERT_EQUAL_UINT64(before, sithCollision_GetScratchChecksumForTest());
    TEST_ASSERT_EQUAL_INT(0, sithInteractionTest_numSurfaceMessages);
}

TEST(sithInteraction, QueryDoesNotStartTheRepeatGuard)
{
    sithInteractionTest_KeepOnlySurface(SITH_SURFACE_COGLINKED | SITH_SURFACE_COLLISION);

    SithPlayerActionsTarget target;
    TEST_ASSERT_TRUE(sithPlayerActions_QueryActivateTarget(PLAYER, &target));
    TEST_ASSERT_EQUAL_INT(1, sithPlayerActions_Activate(PLAYER));
    TEST_ASSERT_EQUAL_INT(1, sithInteractionTest_numSurfaceMessages);
}

// Added: the movable-object scene the standing press uses for push/pull. The object must be in
// front and close enough for the grab search; the floor makes the press's move checks pass.
static void sithInteractionTest_SetUpMovableScene(float objectZ, float objectMovesize,
    SithSurfaceFlag wallFlags)
{
    sithInteractionTest_SetWall(0.06f, -0.2f, 0.2f, wallFlags);
    sithInteractionTest_AddFloor(-0.07f);

    PLAYER->moveStatus = SITHPLAYERMOVE_STILL;
    PLAYER->attach.flags = SITH_ATTACH_THING; // the grab search requires an attached player
    PLAYER->thingInfo.actorInfo.weaponInfo.curWeaponID = SITHWEAPON_NO_WEAPON;
    sithInteractionTest_player.curWeaponID = SITHWEAPON_NO_WEAPON;

    COG->flags = SITH_TF_MOVABLE;
    COG->pCog = NULL;
    COG->pos = (rdVector3){ 0.0f, 0.05f, objectZ };
    COG->collide.type = SITH_COLLIDE_SPHERE;
    COG->collide.size = 0.05f;
    COG->collide.movesize = objectMovesize;
    COG->attach.flags = 0;
    COG->pInSector = &sithInteractionTest_sector;
    COG->pNextThingInSector = NULL;
    sithInteractionTest_sector.pFirstThingInSector = COG;
}

TEST(sithInteraction, StandingQueryShowsPushAndPull)
{
    sithInteractionTest_SetUpMovableScene(0.0f, 0.05f, (SithSurfaceFlag)0);

    SithPlayerInteraction interaction;
    TEST_ASSERT_TRUE(sithPlayerControls_QueryInteraction(PLAYER, &interaction));
    TEST_ASSERT_EQUAL_INT(SITHPLAYERINTERACTION_PUSH_PULL, interaction.kind);
    TEST_ASSERT_EQUAL_INT(SITHCONTROL_ACT2, interaction.control);
    TEST_ASSERT_EQUAL_PTR(COG, interaction.pThing);
}

TEST(sithInteraction, StandingQueryShowsPushOnlyWhenPullIsTooHigh)
{
    // A large object below the player fails the press's pull height check but not the push checks.
    sithInteractionTest_SetUpMovableScene(-0.03f, 0.1f, (SithSurfaceFlag)0);

    SithPlayerInteraction interaction;
    TEST_ASSERT_TRUE(sithPlayerControls_QueryInteraction(PLAYER, &interaction));
    TEST_ASSERT_EQUAL_INT(SITHPLAYERINTERACTION_PUSH, interaction.kind);
    TEST_ASSERT_EQUAL_INT(SITHCONTROL_ACT2, interaction.control);
    TEST_ASSERT_EQUAL_PTR(COG, interaction.pThing);
}

TEST(sithInteraction, StandingQueryShowsPullOnlyWhenPushIsBlocked)
{
    // The wall in front blocks the press's push move but not the pull move away from it.
    sithInteractionTest_SetUpMovableScene(0.0f, 0.05f, SITH_SURFACE_COLLISION);

    SithPlayerInteraction interaction;
    TEST_ASSERT_TRUE(sithPlayerControls_QueryInteraction(PLAYER, &interaction));
    TEST_ASSERT_EQUAL_INT(SITHPLAYERINTERACTION_PULL, interaction.kind);
    TEST_ASSERT_EQUAL_INT(SITHCONTROL_ACT2, interaction.control);
    TEST_ASSERT_EQUAL_PTR(COG, interaction.pThing);
}

TEST(sithInteraction, JumpQueryShowsClimbOn1mLikeThePress)
{
    sithInteractionTest_SetWall(0.07f, -0.2f, 0.1f, SITH_SURFACE_COLLISION);
    PLAYER->moveStatus = SITHPLAYERMOVE_STILL;
    PLAYER->attach.flags = 0;

    SithPlayerInteraction interaction;
    TEST_ASSERT_TRUE(sithPlayerControls_QueryInteraction(PLAYER, &interaction));
    TEST_ASSERT_EQUAL_INT(SITHPLAYERINTERACTION_CLIMB_UP, interaction.kind);
    TEST_ASSERT_EQUAL_INT(SITHCONTROL_JUMP, interaction.control);

    // The press tries 2m first and then 1m; the query reports the same winner.
    TEST_ASSERT_EQUAL_INT(0, sithPlayerActions_CanClimbOn2m(PLAYER));
    TEST_ASSERT_EQUAL_INT(1, sithPlayerActions_CanClimbOn1m(PLAYER));
}

TEST(sithInteraction, JumpQueryShowsClimbWallMount)
{
    sithInteractionTest_SetWall(0.07f, -0.2f, 0.03f, SITH_SURFACE_CLIMBABLE | SITH_SURFACE_COLLISION);
    PLAYER->moveStatus = SITHPLAYERMOVE_STILL;
    PLAYER->attach.flags = 0;

    SithPlayerInteraction interaction;
    TEST_ASSERT_TRUE(sithPlayerControls_QueryInteraction(PLAYER, &interaction));
    TEST_ASSERT_EQUAL_INT(SITHPLAYERINTERACTION_CLIMB, interaction.kind);
    TEST_ASSERT_EQUAL_INT(SITHCONTROL_JUMP, interaction.control);
    TEST_ASSERT_EQUAL_PTR(&sithInteractionTest_surface, interaction.pSurface);

    TEST_ASSERT_EQUAL_INT(0, sithPlayerActions_CanClimbOn2m(PLAYER));
    TEST_ASSERT_EQUAL_INT(0, sithPlayerActions_CanClimbOn1m(PLAYER));
}

TEST(sithInteraction, ClimbIdleQueryShowsPullUpWithActivate)
{
    sithInteractionTest_SetWall(0.07f, -0.2f, 0.03f, SITH_SURFACE_CLIMBABLE | SITH_SURFACE_COLLISION);
    PLAYER->moveStatus = SITHPLAYERMOVE_CLIMBIDLE;
    PLAYER->attach.flags = 0;

    SithPlayerInteraction interaction;
    TEST_ASSERT_TRUE(sithPlayerControls_QueryInteraction(PLAYER, &interaction));
    TEST_ASSERT_EQUAL_INT(SITHPLAYERINTERACTION_CLIMB_UP, interaction.kind);
    TEST_ASSERT_EQUAL_INT(SITHCONTROL_ACT2, interaction.control);

    TEST_ASSERT_EQUAL_INT(1, sithPlayerActions_CanPullUp(PLAYER));
}

TEST(sithInteraction, HangingQueryShowsPullUpWithForward)
{
    sithInteractionTest_SetWall(0.07f, -0.2f, 0.03f, SITH_SURFACE_COLLISION);
    PLAYER->moveStatus = SITHPLAYERMOVE_HANGING;
    PLAYER->attach.flags = 0;

    SithPlayerInteraction interaction;
    TEST_ASSERT_TRUE(sithPlayerControls_QueryInteraction(PLAYER, &interaction));
    TEST_ASSERT_EQUAL_INT(SITHPLAYERINTERACTION_CLIMB_UP, interaction.kind);
    TEST_ASSERT_EQUAL_INT(SITHCONTROL_FORWARD, interaction.control);

    TEST_ASSERT_EQUAL_INT(1, sithPlayerActions_CanPullUp(PLAYER));
}

TEST(sithInteraction, WaterSurfaceQueryShowsLedgePullUpWithJump)
{
    sithInteractionTest_SetWall(0.06f, -0.2f, 0.1f,
        SITH_SURFACE_COLLISION | SITH_SURFACE_WATERLEDGE);
    PLAYER->moveStatus = SITHPLAYERMOVE_SWIMIDLE;
    PLAYER->attach.flags = 0;
    PLAYER->moveInfo.physics.flags = SITH_PF_ONWATERSURFACE;
    sithPlayerControls_SetSwimIdleTimeForTest(1.0f);

    SithPlayerInteraction interaction;
    TEST_ASSERT_TRUE(sithPlayerControls_QueryInteraction(PLAYER, &interaction));
    TEST_ASSERT_EQUAL_INT(SITHPLAYERINTERACTION_CLIMB_UP, interaction.kind);
    TEST_ASSERT_EQUAL_INT(SITHCONTROL_JUMP, interaction.control);
}

TEST(sithInteraction, PushPullClimbQueriesLeaveTheCollisionScratchUnchanged)
{
    sithInteractionTest_SetUpMovableScene(0.0f, 0.05f, (SithSurfaceFlag)0);

    SithPlayerInteraction interaction;
    const uint64_t before = sithCollision_GetScratchChecksumForTest();
    for ( int i = 0; i < 8; ++i )
    {
        TEST_ASSERT_TRUE(sithPlayerControls_QueryInteraction(PLAYER, &interaction));
    }
    TEST_ASSERT_EQUAL_INT(SITHPLAYERINTERACTION_PUSH_PULL, interaction.kind);
    TEST_ASSERT_EQUAL_UINT64(before, sithCollision_GetScratchChecksumForTest());

    sithInteractionTest_ResetScene();
    sithInteractionTest_SetWall(0.07f, -0.2f, 0.1f, SITH_SURFACE_COLLISION);
    PLAYER->moveStatus = SITHPLAYERMOVE_STILL;
    PLAYER->attach.flags = 0;

    const uint64_t climbBefore = sithCollision_GetScratchChecksumForTest();
    for ( int i = 0; i < 8; ++i )
    {
        TEST_ASSERT_TRUE(sithPlayerControls_QueryInteraction(PLAYER, &interaction));
    }
    TEST_ASSERT_EQUAL_INT(SITHPLAYERINTERACTION_CLIMB_UP, interaction.kind);
    TEST_ASSERT_EQUAL_UINT64(climbBefore, sithCollision_GetScratchChecksumForTest());
}

TEST(sithInteraction, WhipSwingQueryShowsSwingWithActivate)
{
    sithInteractionTest_EquipWhip(COG, NULL);

    SithPlayerInteraction interaction;
    TEST_ASSERT_TRUE(sithPlayerControls_QueryInteraction(PLAYER, &interaction));
    TEST_ASSERT_EQUAL_INT(SITHPLAYERINTERACTION_WHIP_SWING, interaction.kind);
    TEST_ASSERT_EQUAL_INT(SITHCONTROL_ACT2, interaction.control);
    TEST_ASSERT_EQUAL_PTR(COG, interaction.pThing);

    int bSwing = -1;
    TEST_ASSERT_EQUAL_INT(1, sithWeapon_QueryWhipPress(PLAYER, &bSwing));
    TEST_ASSERT_EQUAL_INT(1, bSwing);
}

TEST(sithInteraction, WhipClimbQueryShowsClimbWithActivate)
{
    sithInteractionTest_EquipWhip(NULL, COG);

    SithPlayerInteraction interaction;
    TEST_ASSERT_TRUE(sithPlayerControls_QueryInteraction(PLAYER, &interaction));
    TEST_ASSERT_EQUAL_INT(SITHPLAYERINTERACTION_WHIP_CLIMB, interaction.kind);
    TEST_ASSERT_EQUAL_INT(SITHCONTROL_ACT2, interaction.control);
    TEST_ASSERT_EQUAL_PTR(COG, interaction.pThing);

    int bSwing = -1;
    TEST_ASSERT_EQUAL_INT(1, sithWeapon_QueryWhipPress(PLAYER, &bSwing));
    TEST_ASSERT_EQUAL_INT(0, bSwing);
}

TEST(sithInteraction, WhipSwingTargetFlaggedClimbGetsNoPrompt)
{
    sithInteractionTest_EquipWhip(COG, NULL);
    COG->flags |= SITH_TF_WHIPCLIMB;
    sithInteractionTest_KeepOnlySurface(SITH_SURFACE_COLLISION);

    SithPlayerInteraction interaction;
    TEST_ASSERT_FALSE(sithPlayerControls_QueryInteraction(PLAYER, &interaction));
    TEST_ASSERT_EQUAL_INT(SITHPLAYERINTERACTION_NONE, interaction.kind);

    int bSwing = -1;
    TEST_ASSERT_EQUAL_INT(0, sithWeapon_QueryWhipPress(PLAYER, &bSwing));
}

TEST(sithInteraction, NoWhipDrawnGetsNoWhipPrompt)
{
    sithInteractionTest_KeepOnlySurface(SITH_SURFACE_COLLISION);
    sithWhip_SetTargetsForTest(COG, NULL);

    SithPlayerInteraction interaction;
    TEST_ASSERT_FALSE(sithPlayerControls_QueryInteraction(PLAYER, &interaction));
    TEST_ASSERT_EQUAL_INT(SITHPLAYERINTERACTION_NONE, interaction.kind);

    int bSwing = -1;
    TEST_ASSERT_EQUAL_INT(0, sithWeapon_QueryWhipPress(PLAYER, &bSwing));
}

TEST(sithInteraction, WhipQueryRejectsDisabledControlsAndMoveStatusAboveRunning)
{
    sithInteractionTest_EquipWhip(COG, NULL);
    sithInteractionTest_KeepOnlySurface(SITH_SURFACE_COLLISION);

    int bSwing = -1;
    TEST_ASSERT_EQUAL_INT(1, sithWeapon_QueryWhipPress(PLAYER, &bSwing));
    TEST_ASSERT_EQUAL_INT(1, bSwing);

    PLAYER->thingInfo.actorInfo.bControlsDisabled = 1;
    TEST_ASSERT_EQUAL_INT(0, sithWeapon_QueryWhipPress(PLAYER, &bSwing));

    SithPlayerInteraction interaction;
    TEST_ASSERT_FALSE(sithPlayerControls_QueryInteraction(PLAYER, &interaction));

    PLAYER->thingInfo.actorInfo.bControlsDisabled = 0;
    PLAYER->moveStatus = SITHPLAYERMOVE_FALLING;
    TEST_ASSERT_EQUAL_INT(0, sithWeapon_QueryWhipPress(PLAYER, &bSwing));
    TEST_ASSERT_FALSE(sithPlayerControls_QueryInteraction(PLAYER, &interaction));
}

TEST(sithInteraction, WhipQueryChangesNoState)
{
    sithInteractionTest_EquipWhip(COG, OTHERPLAYER);
    sithInteractionTest_KeepOnlySurface(SITH_SURFACE_COLLISION);

    SithThing* pSwingBefore = sithWhip_GetWhipSwingThing();
    SithThing* pClimbBefore = sithWhip_GetWhipClimbThing();
    const SithThingMoveStatus moveBefore = PLAYER->moveStatus;
    const int controlsBefore = PLAYER->thingInfo.actorInfo.bControlsDisabled;
    const rdVector3 posBefore = PLAYER->pos;
    const SithWeaponId weaponBefore = PLAYER->thingInfo.actorInfo.weaponInfo.curWeaponID;
    const float timeBefore = (float)sithTime_g_secGameTime;
    const uint64_t scratchBefore = sithCollision_GetScratchChecksumForTest();

    SithPlayerInteraction interaction;
    for ( int i = 0; i < 8; ++i )
    {
        TEST_ASSERT_TRUE(sithPlayerControls_QueryInteraction(PLAYER, &interaction));
        TEST_ASSERT_EQUAL_INT(SITHPLAYERINTERACTION_WHIP_SWING, interaction.kind);
    }

    TEST_ASSERT_EQUAL_PTR(pSwingBefore, sithWhip_GetWhipSwingThing());
    TEST_ASSERT_EQUAL_PTR(pClimbBefore, sithWhip_GetWhipClimbThing());
    TEST_ASSERT_EQUAL_INT(moveBefore, PLAYER->moveStatus);
    TEST_ASSERT_EQUAL_INT(controlsBefore, PLAYER->thingInfo.actorInfo.bControlsDisabled);
    TEST_ASSERT_EQUAL_FLOAT(posBefore.x, PLAYER->pos.x);
    TEST_ASSERT_EQUAL_FLOAT(posBefore.y, PLAYER->pos.y);
    TEST_ASSERT_EQUAL_FLOAT(posBefore.z, PLAYER->pos.z);
    TEST_ASSERT_EQUAL_INT(weaponBefore, PLAYER->thingInfo.actorInfo.weaponInfo.curWeaponID);
    TEST_ASSERT_EQUAL_FLOAT(timeBefore, (float)sithTime_g_secGameTime);
    TEST_ASSERT_EQUAL_UINT64(scratchBefore, sithCollision_GetScratchChecksumForTest());
}

TEST(sithInteraction, WhipSearchSetsTheSwingTarget)
{
    const rdVector3 pos = { 0.0f, 0.1f, 0.0f };
    sithInteractionTest_SetUpWhipSearchThing(COG, &pos, SITH_TF_WHIPSWING);

    TEST_ASSERT_EQUAL_INT(1, sithWhip_SearchWhipSwingThing(PLAYER));
    TEST_ASSERT_EQUAL_PTR(COG, sithWhip_GetWhipSwingThing());
    TEST_ASSERT_NULL(sithWhip_GetWhipClimbThing());
}

TEST(sithInteraction, WhipSearchSetsTheClimbTarget)
{
    const rdVector3 pos = { 0.0f, 0.0f, 0.4f };
    sithInteractionTest_SetUpWhipSearchThing(COG, &pos, SITH_TF_WHIPCLIMB);

    TEST_ASSERT_EQUAL_INT(1, sithWhip_SearchWhipClimbThing(PLAYER));
    TEST_ASSERT_EQUAL_PTR(COG, sithWhip_GetWhipClimbThing());
    TEST_ASSERT_NULL(sithWhip_GetWhipSwingThing());
}

TEST(sithInteraction, WhipClimbSearchRejectsATargetTooLow)
{
    const rdVector3 pos = { 0.0f, 0.0f, 0.1f };
    sithInteractionTest_SetUpWhipSearchThing(COG, &pos, SITH_TF_WHIPCLIMB);

    TEST_ASSERT_EQUAL_INT(0, sithWhip_SearchWhipClimbThing(PLAYER));
    TEST_ASSERT_NULL(sithWhip_GetWhipClimbThing());
}

TEST(sithInteraction, WhipSearchFindsNothingWithoutATarget)
{
    sithInteractionTest_KeepOnlySurface(SITH_SURFACE_COLLISION);

    TEST_ASSERT_EQUAL_INT(0, sithWhip_SearchWhipSwingThing(PLAYER));
    TEST_ASSERT_NULL(sithWhip_GetWhipSwingThing());
    TEST_ASSERT_EQUAL_INT(0, sithWhip_SearchWhipClimbThing(PLAYER));
    TEST_ASSERT_NULL(sithWhip_GetWhipClimbThing());
}

TEST(sithInteraction, QuickWhipQueryPicksTheSwingTargetAndChangesNoState)
{
    const rdVector3 swingPos = { 0.0f, 0.1f, 0.0f };
    const rdVector3 climbPos = { 0.0f, 0.0f, 0.4f };
    sithInteractionTest_SetUpWhipSearchThing(COG, &swingPos, SITH_TF_WHIPSWING);
    sithInteractionTest_InitThing(OTHERPLAYER, 3, SITH_THING_PLAYER, &climbPos);
    OTHERPLAYER->flags = SITH_TF_WHIPCLIMB;
    COG->pNextThingInSector = OTHERPLAYER;

    // The old globals point elsewhere; the query must not touch them.
    sithWhip_SetTargetsForTest(ITEM, ITEM);
    SithThing* pSwingBefore = sithWhip_GetWhipSwingThing();
    SithThing* pClimbBefore = sithWhip_GetWhipClimbThing();
    const SithWeaponId weaponBefore = PLAYER->thingInfo.actorInfo.weaponInfo.curWeaponID;
    const SithThingMoveStatus moveBefore = PLAYER->moveStatus;
    const uint64_t scratchBefore = sithCollision_GetScratchChecksumForTest();

    SithThing* pTarget = NULL;
    int bSwing = -1;
    TEST_ASSERT_EQUAL_INT(1, sithWhip_QueryQuickWhipTarget(PLAYER, &pTarget, &bSwing));
    TEST_ASSERT_EQUAL_PTR(COG, pTarget);
    TEST_ASSERT_EQUAL_INT(1, bSwing);

    TEST_ASSERT_EQUAL_PTR(pSwingBefore, sithWhip_GetWhipSwingThing());
    TEST_ASSERT_EQUAL_PTR(pClimbBefore, sithWhip_GetWhipClimbThing());
    TEST_ASSERT_EQUAL_INT(weaponBefore, PLAYER->thingInfo.actorInfo.weaponInfo.curWeaponID);
    TEST_ASSERT_EQUAL_INT(moveBefore, PLAYER->moveStatus);
    TEST_ASSERT_EQUAL_UINT64(scratchBefore, sithCollision_GetScratchChecksumForTest());
}

TEST(sithInteraction, QuickWhipQueryPicksAClimbTarget)
{
    const rdVector3 pos = { 0.0f, 0.0f, 0.4f };
    sithInteractionTest_SetUpWhipSearchThing(COG, &pos, SITH_TF_WHIPCLIMB);

    SithThing* pTarget = NULL;
    int bSwing = -1;
    TEST_ASSERT_EQUAL_INT(1, sithWhip_QueryQuickWhipTarget(PLAYER, &pTarget, &bSwing));
    TEST_ASSERT_EQUAL_PTR(COG, pTarget);
    TEST_ASSERT_EQUAL_INT(0, bSwing);
}

TEST(sithInteraction, QuickWhipQueryRejectsAClimbTargetTooLow)
{
    const rdVector3 pos = { 0.0f, 0.0f, 0.1f };
    sithInteractionTest_SetUpWhipSearchThing(COG, &pos, SITH_TF_WHIPCLIMB);

    SithThing* pTarget = COG;
    int bSwing = 1;
    TEST_ASSERT_EQUAL_INT(0, sithWhip_QueryQuickWhipTarget(PLAYER, &pTarget, &bSwing));
    TEST_ASSERT_NULL(pTarget);
    TEST_ASSERT_EQUAL_INT(0, bSwing);
}

TEST(sithInteraction, CanQuickWhipAcceptsAWhipAimScene)
{
#ifndef J3D_QOL_IMPROVEMENTS
    TEST_IGNORE_MESSAGE("quick whip is always off in a non-QOL build");
#endif

    sithInteractionTest_SetUpQuickWhipScene();

    SithThing* pTarget = NULL;
    int bSwing = -1;
    TEST_ASSERT_EQUAL_INT(1, sithWeapon_CanQuickWhip(PLAYER, &pTarget, &bSwing));
    TEST_ASSERT_EQUAL_PTR(COG, pTarget);
    TEST_ASSERT_EQUAL_INT(1, bSwing);
}

TEST(sithInteraction, CanQuickWhipRejectsEachUnavailableCondition)
{
#ifndef J3D_QOL_IMPROVEMENTS
    TEST_IGNORE_MESSAGE("quick whip is always off in a non-QOL build");
#endif

    sithInteractionTest_SetUpQuickWhipScene();

    SithThing* pTarget = NULL;
    int bSwing = -1;
    TEST_ASSERT_EQUAL_INT(1, sithWeapon_CanQuickWhip(PLAYER, &pTarget, &bSwing));

    sithWeapon_SetQuickWhipEnabled(0);
    TEST_ASSERT_EQUAL_INT(0, sithWeapon_CanQuickWhip(PLAYER, &pTarget, &bSwing));
    sithWeapon_SetQuickWhipEnabled(J3D_QOL_VALUE(1, 0));

    PLAYER->thingInfo.actorInfo.weaponInfo.curWeaponID = SITHWEAPON_WHIP;
    TEST_ASSERT_EQUAL_INT(0, sithWeapon_CanQuickWhip(PLAYER, &pTarget, &bSwing));
    PLAYER->thingInfo.actorInfo.weaponInfo.curWeaponID = SITHWEAPON_NO_WEAPON;

    PLAYER->attach.flags = 0;
    TEST_ASSERT_EQUAL_INT(0, sithWeapon_CanQuickWhip(PLAYER, &pTarget, &bSwing));
    PLAYER->attach.flags = SITH_ATTACH_SURFACE;
    PLAYER->attach.attachedToStructure.pSurfaceAttached = &sithInteractionTest_surface;

    PLAYER->thingInfo.actorInfo.bControlsDisabled = 1;
    TEST_ASSERT_EQUAL_INT(0, sithWeapon_CanQuickWhip(PLAYER, &pTarget, &bSwing));
    PLAYER->thingInfo.actorInfo.bControlsDisabled = 0;

    PLAYER->moveStatus = SITHPLAYERMOVE_FALLING;
    TEST_ASSERT_EQUAL_INT(0, sithWeapon_CanQuickWhip(PLAYER, &pTarget, &bSwing));
    PLAYER->moveStatus = SITHPLAYERMOVE_STILL;

    sithInteractionTest_player.aItems[SITHWEAPON_WHIP].amount = 0.0f;
    TEST_ASSERT_EQUAL_INT(0, sithWeapon_CanQuickWhip(PLAYER, &pTarget, &bSwing));
    sithInteractionTest_player.aItems[SITHWEAPON_WHIP].amount = 1.0f;

    sithInteractionTest_sector.pFirstThingInSector = NULL;
    TEST_ASSERT_EQUAL_INT(0, sithWeapon_CanQuickWhip(PLAYER, &pTarget, &bSwing));
}

TEST(sithInteraction, QuickWhipSwitchDefaultsAndRoundTrips)
{
    TEST_ASSERT_EQUAL_INT(J3D_QOL_VALUE(1, 0), sithWeapon_IsQuickWhipEnabled());

    sithWeapon_SetQuickWhipEnabled(0);
    TEST_ASSERT_EQUAL_INT(0, sithWeapon_IsQuickWhipEnabled());

    sithWeapon_SetQuickWhipEnabled(1);
    TEST_ASSERT_EQUAL_INT(J3D_QOL_VALUE(1, 0), sithWeapon_IsQuickWhipEnabled());
}

// Added: the fixture has no whip COG or actor, so the fire itself cannot run. This presses the
// quick whip binding and completes the whip draw by hand; it proves the press selects the whip
// without drawing it as a plain select, and that the deadline restores the empty hands.
TEST(sithInteraction, QuickWhipPressSelectsWhipThenDeadlineRestores)
{
#ifndef J3D_QOL_IMPROVEMENTS
    TEST_IGNORE_MESSAGE("quick whip is always off in a non-QOL build");
#endif

    sithInteractionTest_SetUpQuickWhipScene();
    sithInventory_SetInventoryFlags(PLAYER, SITHWEAPON_WHIP,
        (SithInventoryTypeFlag)(SITHINVENTORY_TYPE_WEAPON | SITHINVENTORY_TYPE_UNKNOWN_200));

    sithControl_RegisterKeyFunction(SITHCONTROL_QUICKWHIP);
    sithControl_SetKeyOverride(SITHCONTROL_QUICKWHIP, 0, 0.0f);
    sithControl_SetKeyOverride(SITHCONTROL_QUICKWHIP, 1, 1.0f);

    TEST_ASSERT_EQUAL_INT(0, sithWeapon_ProcessWeaponControls(PLAYER, 0.1f));
    TEST_ASSERT_EQUAL_INT(SITHWEAPON_WHIP, PLAYER->thingInfo.actorInfo.weaponInfo.selectedWeaponID);
    TEST_ASSERT_EQUAL_INT(SITHWEAPON_NO_WEAPON, PLAYER->thingInfo.actorInfo.weaponInfo.curWeaponID);

    // The draw completes; no target comes before the deadline.
    PLAYER->thingInfo.actorInfo.weaponInfo.selectedWeaponID   = SITHWEAPON_NOWEAPONSELECTED;
    PLAYER->thingInfo.actorInfo.weaponInfo.deselectedWeaponID = SITHWEAPON_NOWEAPONDESELECTED;
    PLAYER->thingInfo.actorInfo.weaponInfo.curWeaponID        = SITHWEAPON_WHIP;
    sithTime_g_secGameTime += 3.0;

    TEST_ASSERT_EQUAL_INT(0, sithWeapon_ProcessWeaponControls(PLAYER, 0.1f));
    TEST_ASSERT_EQUAL_INT(SITHWEAPON_WHIP, PLAYER->thingInfo.actorInfo.weaponInfo.deselectedWeaponID);
    TEST_ASSERT_EQUAL_INT(SITHWEAPON_NO_WEAPON, PLAYER->thingInfo.actorInfo.weaponInfo.selectedWeaponID);
}

// Added: the restore waits for the whole whip move. The fixture cannot start the real swing, so
// the move status is set by hand; this still proves the state machine does not restore until the
// whip move is over.
TEST(sithInteraction, QuickWhipRestoresAfterTheWhipMoveEnds)
{
#ifndef J3D_QOL_IMPROVEMENTS
    TEST_IGNORE_MESSAGE("quick whip is always off in a non-QOL build");
#endif

    sithInteractionTest_SetUpQuickWhipScene();
    sithInventory_SetInventoryFlags(PLAYER, SITHWEAPON_WHIP,
        (SithInventoryTypeFlag)(SITHINVENTORY_TYPE_WEAPON | SITHINVENTORY_TYPE_UNKNOWN_200));

    sithControl_RegisterKeyFunction(SITHCONTROL_QUICKWHIP);
    sithControl_SetKeyOverride(SITHCONTROL_QUICKWHIP, 0, 0.0f);
    sithControl_SetKeyOverride(SITHCONTROL_QUICKWHIP, 1, 1.0f);
    TEST_ASSERT_EQUAL_INT(0, sithWeapon_ProcessWeaponControls(PLAYER, 0.1f));

    PLAYER->thingInfo.actorInfo.weaponInfo.selectedWeaponID   = SITHWEAPON_NOWEAPONSELECTED;
    PLAYER->thingInfo.actorInfo.weaponInfo.deselectedWeaponID = SITHWEAPON_NOWEAPONDESELECTED;
    PLAYER->thingInfo.actorInfo.weaponInfo.curWeaponID        = SITHWEAPON_WHIP;
    PLAYER->moveStatus = SITHPLAYERMOVE_WHIPSWINGING;

    TEST_ASSERT_EQUAL_INT(0, sithWeapon_ProcessWeaponControls(PLAYER, 0.1f));
    TEST_ASSERT_EQUAL_INT(SITHWEAPON_NOWEAPONDESELECTED,
        PLAYER->thingInfo.actorInfo.weaponInfo.deselectedWeaponID);

    PLAYER->moveStatus = SITHPLAYERMOVE_STILL;
    TEST_ASSERT_EQUAL_INT(0, sithWeapon_ProcessWeaponControls(PLAYER, 0.1f));
    TEST_ASSERT_EQUAL_INT(SITHWEAPON_WHIP, PLAYER->thingInfo.actorInfo.weaponInfo.deselectedWeaponID);
}

TEST_GROUP_RUNNER(sithInteraction)
{
    RUN_TEST_CASE(sithInteraction, QueryAndActivateAgreeOnCogSurface);
    RUN_TEST_CASE(sithInteraction, QueryAndActivateAgreeOnCogThing);
    RUN_TEST_CASE(sithInteraction, MovableThingHasNoPromptAndNoActivation);
    RUN_TEST_CASE(sithInteraction, DrawnWeaponHasNoPromptAndNoActivation);
    RUN_TEST_CASE(sithInteraction, AiDoesNotActivatePlayerThings);
    RUN_TEST_CASE(sithInteraction, RepeatGuardHidesThePromptAndThePress);
    RUN_TEST_CASE(sithInteraction, StandingQueryPicksTheItemThePressWouldFind);
    RUN_TEST_CASE(sithInteraction, StandingQueryPicksAFloorItem);
    RUN_TEST_CASE(sithInteraction, CogWithoutActivateHandlerHidesUse);
    RUN_TEST_CASE(sithInteraction, ThingWithoutActivateHandlerHidesUse);
    RUN_TEST_CASE(sithInteraction, ThingLinkedCogWithActivateHandlerShowsUse);
    RUN_TEST_CASE(sithInteraction, ItemWithoutActivateHandlerHidesPickUp);
    RUN_TEST_CASE(sithInteraction, ReadOnlySearchLeavesTheThingOrientationTranslation);
    RUN_TEST_CASE(sithInteraction, QueryLeavesTheCollisionScratchUnchanged);
    RUN_TEST_CASE(sithInteraction, QueryDoesNotStartTheRepeatGuard);
    RUN_TEST_CASE(sithInteraction, StandingQueryShowsPushAndPull);
    RUN_TEST_CASE(sithInteraction, StandingQueryShowsPushOnlyWhenPullIsTooHigh);
    RUN_TEST_CASE(sithInteraction, StandingQueryShowsPullOnlyWhenPushIsBlocked);
    RUN_TEST_CASE(sithInteraction, JumpQueryShowsClimbOn1mLikeThePress);
    RUN_TEST_CASE(sithInteraction, JumpQueryShowsClimbWallMount);
    RUN_TEST_CASE(sithInteraction, ClimbIdleQueryShowsPullUpWithActivate);
    RUN_TEST_CASE(sithInteraction, HangingQueryShowsPullUpWithForward);
    RUN_TEST_CASE(sithInteraction, WaterSurfaceQueryShowsLedgePullUpWithJump);
    RUN_TEST_CASE(sithInteraction, PushPullClimbQueriesLeaveTheCollisionScratchUnchanged);
    RUN_TEST_CASE(sithInteraction, WhipSwingQueryShowsSwingWithActivate);
    RUN_TEST_CASE(sithInteraction, WhipClimbQueryShowsClimbWithActivate);
    RUN_TEST_CASE(sithInteraction, WhipSwingTargetFlaggedClimbGetsNoPrompt);
    RUN_TEST_CASE(sithInteraction, NoWhipDrawnGetsNoWhipPrompt);
    RUN_TEST_CASE(sithInteraction, WhipQueryRejectsDisabledControlsAndMoveStatusAboveRunning);
    RUN_TEST_CASE(sithInteraction, WhipQueryChangesNoState);
    RUN_TEST_CASE(sithInteraction, WhipSearchSetsTheSwingTarget);
    RUN_TEST_CASE(sithInteraction, WhipSearchSetsTheClimbTarget);
    RUN_TEST_CASE(sithInteraction, WhipClimbSearchRejectsATargetTooLow);
    RUN_TEST_CASE(sithInteraction, WhipSearchFindsNothingWithoutATarget);
    RUN_TEST_CASE(sithInteraction, QuickWhipQueryPicksTheSwingTargetAndChangesNoState);
    RUN_TEST_CASE(sithInteraction, QuickWhipQueryPicksAClimbTarget);
    RUN_TEST_CASE(sithInteraction, QuickWhipQueryRejectsAClimbTargetTooLow);
    RUN_TEST_CASE(sithInteraction, CanQuickWhipAcceptsAWhipAimScene);
    RUN_TEST_CASE(sithInteraction, CanQuickWhipRejectsEachUnavailableCondition);
    RUN_TEST_CASE(sithInteraction, QuickWhipSwitchDefaultsAndRoundTrips);
    RUN_TEST_CASE(sithInteraction, QuickWhipPressSelectsWhipThenDeadlineRestores);
    RUN_TEST_CASE(sithInteraction, QuickWhipRestoresAfterTheWhipMoveEnds);
}
