// Added: tests of the smoothed level surface normals (option A L3), built once per world and used by
// every lit draw. The meshes are synthetic: a flat quad split in two surfaces, a hinge at 30
// degrees (under the 45 degree threshold) and one at 90 degrees (hard), plus a vertex index outside
// the world's vertex count. Classic and per-pixel both replace the face normal with the smoothed
// corner normal through sithRender_ApplySmoothNormals.
#include <unity_fixture.h>

#include <rdroid/Main/rdroid.h>
#include <rdroid/Math/rdMatrix.h>
#include <rdroid/Math/rdVector.h>
#include <sith/Engine/sithRender.h>
#include <sith/World/sithWorld.h>
#include <std/General/stdMemory.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SITHRENDERTEST_MAXSURFACES 6
#define SITHRENDERTEST_NUMVERTS    8

static SithWorld sithRenderTest_world;
static SithSurface sithRenderTest_aSurfaces[SITHRENDERTEST_MAXSURFACES];
static int sithRenderTest_aFaceVerts[SITHRENDERTEST_MAXSURFACES][4];
static rdVector3 sithRenderTest_aWorldVertices[SITHRENDERTEST_NUMVERTS];
static rdVector3 sithRenderTest_aViewNormals[4];
static SithSurfaceAdjoin sithRenderTest_adjoin;

// Added: M11.3. The renamed std3D_SetStaticShadowCasters recorder (sithRenderTestSeams.h): the production
// upload helper is called unchanged and its argument list is copied before it frees the array.
#define SITHRENDERTEST_MAXSTATICVERTICES 64
static float sithRenderTest_aStaticShadowCasters[SITHRENDERTEST_MAXSTATICVERTICES * 3];
static size_t sithRenderTest_numStaticShadowCasters;
static size_t sithRenderTest_numStaticShadowCasterCalls;

void sithRenderTest_SetStaticShadowCasters(const float* aPositions, size_t numVertices)
{
    ++sithRenderTest_numStaticShadowCasterCalls;
    sithRenderTest_numStaticShadowCasters = numVertices;
    if ( aPositions && numVertices <= SITHRENDERTEST_MAXSTATICVERTICES )
    {
        memcpy(sithRenderTest_aStaticShadowCasters, aPositions,
            numVertices * 3u * sizeof(float));
    }
}

static tHostServices sithRenderTest_hostServices;
static tHostServices* sithRenderTest_pSavedSithHS;
static tHostServices* sithRenderTest_pSavedStdHS;
static SithWorld* sithRenderTest_pSavedWorld;
static bool sithRenderTest_bSavedPerPixelLighting;

static void* J3DAPI sithRenderTest_Malloc(size_t size)
{
    return malloc(size);
}

static void J3DAPI sithRenderTest_Free(void* pData)
{
    free(pData);
}

static void* J3DAPI sithRenderTest_Realloc(void* pData, size_t size)
{
    return realloc(pData, size);
}

static int J3DAPI sithRenderTest_Print(const char* pFormat, ...)
{
    (void)pFormat;
    return 0;
}

static const double SITHRENDERTEST_PI = 3.14159265358979323846;

static void sithRenderTest_AddSurface(const int* aVerts, size_t numVerts, float nx, float ny, float nz)
{
    TEST_ASSERT_TRUE(sithRenderTest_world.numSurfaces < SITHRENDERTEST_MAXSURFACES);
    TEST_ASSERT_TRUE(numVerts <= 4u);

    const size_t surfNum = sithRenderTest_world.numSurfaces++;
    SithSurface* pSurf   = &sithRenderTest_aSurfaces[surfNum];
    memset(pSurf, 0, sizeof(*pSurf));

    for ( size_t i = 0; i < numVerts; ++i )
    {
        sithRenderTest_aFaceVerts[surfNum][i] = aVerts[i];
    }

    pSurf->face.geometryMode   = RD_GEOMETRY_FULL;
    pSurf->face.numVertices    = numVerts;
    pSurf->face.aVertices      = sithRenderTest_aFaceVerts[surfNum];
    pSurf->face.normal.x       = nx;
    pSurf->face.normal.y       = ny;
    pSurf->face.normal.z       = nz;
}

static void sithRenderTest_Build(void)
{
    sithWorld_g_pCurrentWorld = &sithRenderTest_world;
    sithRender_BuildSmoothNormalsForTest();
}

// Fills rdCacheProcEntry.aViewNormals as the classic clip call would (the face normal at every
// corner), then lets the production helper decide whether per-pixel replaces them.
static void sithRenderTest_Apply(size_t surfNum)
{
    TEST_ASSERT_TRUE(surfNum < sithRenderTest_world.numSurfaces);
    const SithSurface* pSurf = &sithRenderTest_aSurfaces[surfNum];

    rdCacheProcEntry entry;
    memset(&entry, 0, sizeof(entry));
    entry.aViewNormals = sithRenderTest_aViewNormals;

    rdVector3 faceNormal;
    rdVector_Normalize3(&faceNormal, &pSurf->face.normal);
    for ( size_t i = 0; i < pSurf->face.numVertices; ++i )
    {
        sithRenderTest_aViewNormals[i] = faceNormal;
    }

    rdMatrix34 identity;
    rdMatrix_Identity34(&identity);
    sithRender_ApplySmoothNormalsForTest(&entry, &sithRenderTest_world, pSurf, &identity);
}

static void sithRenderTest_AssertCorner(size_t i, double x, double y, double z)
{
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, (float)x, sithRenderTest_aViewNormals[i].x);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, (float)y, sithRenderTest_aViewNormals[i].y);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, (float)z, sithRenderTest_aViewNormals[i].z);
}

TEST_GROUP(sithRender);

TEST_SETUP(sithRender)
{
    memset(&sithRenderTest_hostServices, 0, sizeof(sithRenderTest_hostServices));
    sithRenderTest_hostServices.pStatusPrint  = sithRenderTest_Print;
    sithRenderTest_hostServices.pMessagePrint = sithRenderTest_Print;
    sithRenderTest_hostServices.pWarningPrint = sithRenderTest_Print;
    sithRenderTest_hostServices.pErrorPrint   = sithRenderTest_Print;
    sithRenderTest_hostServices.pDebugPrint   = sithRenderTest_Print;
    sithRenderTest_hostServices.pMalloc       = sithRenderTest_Malloc;
    sithRenderTest_hostServices.pFree         = sithRenderTest_Free;
    sithRenderTest_hostServices.pRealloc      = sithRenderTest_Realloc;

    sithRenderTest_pSavedSithHS           = sith_g_pHS;
    sithRenderTest_pSavedStdHS            = std_g_pHS;
    sithRenderTest_pSavedWorld            = sithWorld_g_pCurrentWorld;
    sithRenderTest_bSavedPerPixelLighting = rdGetPerPixelLighting();

    sith_g_pHS = &sithRenderTest_hostServices;
    std_g_pHS  = &sithRenderTest_hostServices;

    memset(&sithRenderTest_world, 0, sizeof(sithRenderTest_world));
    memset(sithRenderTest_aSurfaces, 0, sizeof(sithRenderTest_aSurfaces));
    memset(sithRenderTest_aFaceVerts, 0, sizeof(sithRenderTest_aFaceVerts));
    memset(sithRenderTest_aWorldVertices, 0, sizeof(sithRenderTest_aWorldVertices));
    memset(&sithRenderTest_adjoin, 0, sizeof(sithRenderTest_adjoin));
    memset(sithRenderTest_aStaticShadowCasters, 0, sizeof(sithRenderTest_aStaticShadowCasters));
    sithRenderTest_numStaticShadowCasters     = 0;
    sithRenderTest_numStaticShadowCasterCalls = 0;
    sithRenderTest_world.aSurfaces   = sithRenderTest_aSurfaces;
    sithRenderTest_world.numVertices = SITHRENDERTEST_NUMVERTS;

    rdSetPerPixelLighting(true);
}

TEST_TEAR_DOWN(sithRender)
{
    sithRender_FreeSmoothNormalsForTest();

    rdSetPerPixelLighting(sithRenderTest_bSavedPerPixelLighting);
    sithWorld_g_pCurrentWorld = sithRenderTest_pSavedWorld;
    sith_g_pHS                = sithRenderTest_pSavedSithHS;
    std_g_pHS                 = sithRenderTest_pSavedStdHS;
}

TEST(sithRender, AFlatQuadSplitInTwoSurfacesKeepsTheFaceNormal)
{
    const int aLeft[4]  = { 0, 1, 2, 3 };
    const int aRight[4] = { 2, 1, 4, 5 };
    sithRenderTest_AddSurface(aLeft, 4u, 0.0f, 0.0f, 1.0f);
    sithRenderTest_AddSurface(aRight, 4u, 0.0f, 0.0f, 1.0f);
    sithRenderTest_Build();

    for ( size_t surfNum = 0; surfNum < 2u; ++surfNum )
    {
        sithRenderTest_Apply(surfNum);
        for ( size_t i = 0; i < 4u; ++i )
        {
            sithRenderTest_AssertCorner(i, 0.0, 0.0, 1.0);
        }
    }
}

TEST(sithRender, AThirtyDegreeHingeSmoothsOnlyTheSharedVertices)
{
    const double tilt     = 30.0 * SITHRENDERTEST_PI / 180.0;
    const double ny       = -sin(tilt);
    const double nz       = cos(tilt);
    const double sumLen   = sqrt(1.0 + ny * ny + nz * nz + 2.0 * nz);
    const double smoothY  = ny / sumLen;
    const double smoothZ  = (1.0 + nz) / sumLen;

    const int aLeft[4]  = { 0, 1, 2, 3 };
    const int aRight[4] = { 2, 1, 4, 5 };
    sithRenderTest_AddSurface(aLeft, 4u, 0.0f, 0.0f, 1.0f);
    sithRenderTest_AddSurface(aRight, 4u, 0.0f, (float)ny, (float)nz);
    sithRenderTest_Build();

    sithRenderTest_Apply(0u);
    sithRenderTest_AssertCorner(0, 0.0, 0.0, 1.0);
    sithRenderTest_AssertCorner(1, 0.0, smoothY, smoothZ);
    sithRenderTest_AssertCorner(2, 0.0, smoothY, smoothZ);
    sithRenderTest_AssertCorner(3, 0.0, 0.0, 1.0);

    sithRenderTest_Apply(1u);
    sithRenderTest_AssertCorner(0, 0.0, smoothY, smoothZ);
    sithRenderTest_AssertCorner(1, 0.0, smoothY, smoothZ);
    sithRenderTest_AssertCorner(2, 0.0, ny, nz);
    sithRenderTest_AssertCorner(3, 0.0, ny, nz);
}

TEST(sithRender, ANinetyDegreeHingeKeepsBothFaceNormals)
{
    const int aLeft[4]  = { 0, 1, 2, 3 };
    const int aRight[4] = { 2, 1, 4, 5 };
    sithRenderTest_AddSurface(aLeft, 4u, 0.0f, 0.0f, 1.0f);
    sithRenderTest_AddSurface(aRight, 4u, 0.0f, -1.0f, 0.0f);
    sithRenderTest_Build();

    sithRenderTest_Apply(0u);
    for ( size_t i = 0; i < 4u; ++i )
    {
        sithRenderTest_AssertCorner(i, 0.0, 0.0, 1.0);
    }

    sithRenderTest_Apply(1u);
    for ( size_t i = 0; i < 4u; ++i )
    {
        sithRenderTest_AssertCorner(i, 0.0, -1.0, 0.0);
    }
}

TEST(sithRender, ClassicAndPerPixelBothUseTheSmoothedNormal)
{
    const double tilt    = 30.0 * SITHRENDERTEST_PI / 180.0;
    const double ny      = -sin(tilt);
    const double nz      = cos(tilt);
    const double sumLen  = sqrt(1.0 + ny * ny + nz * nz + 2.0 * nz);
    const double smoothY = ny / sumLen;
    const double smoothZ = (1.0 + nz) / sumLen;

    const int aLeft[4]  = { 0, 1, 2, 3 };
    const int aRight[4] = { 2, 1, 4, 5 };
    sithRenderTest_AddSurface(aLeft, 4u, 0.0f, 0.0f, 1.0f);
    sithRenderTest_AddSurface(aRight, 4u, 0.0f, (float)ny, (float)nz);
    sithRenderTest_Build();

    // Classic: the shared corner takes the smoothed normal (the normal target has used it since M6).
    rdSetPerPixelLighting(false);
    sithRenderTest_Apply(0u);
    sithRenderTest_AssertCorner(1, 0.0, smoothY, smoothZ);

    // Per-pixel: the same.
    rdSetPerPixelLighting(true);
    sithRenderTest_Apply(0u);
    sithRenderTest_AssertCorner(1, 0.0, smoothY, smoothZ);
}

TEST(sithRender, AVertexOutOfRangeJoinsNothingAndDoesNotCrash)
{
    const double tilt = 30.0 * SITHRENDERTEST_PI / 180.0;
    const double ny   = -sin(tilt);
    const double nz   = cos(tilt);

    const int aLeft[4]   = { 0, 1, 2, 3 };
    const int aBroken[4] = { 6, 1, 4, 5 };
    sithRenderTest_AddSurface(aLeft, 4u, 0.0f, 0.0f, 1.0f);
    sithRenderTest_AddSurface(aBroken, 4u, 0.0f, (float)ny, (float)nz);
    sithRenderTest_Build();

    // Surface 1's first corner at vertex 6 (out of range for 6 vertices) keeps the face normal.
    sithRenderTest_Apply(1u);
    sithRenderTest_AssertCorner(0, 0.0, ny, nz);
    sithRenderTest_AssertCorner(2, 0.0, ny, nz);
    sithRenderTest_AssertCorner(3, 0.0, ny, nz);
}

// Added: M11.3. The level upload keeps solid geometry and drops sky, water, translucent adjoins and
// surfaces without geometry, fan-triangulating the kept surfaces from their first vertex. The std3D call
// is renamed to this suite's recorder by CMake, so the production helper runs exactly as in the game.
TEST(sithRender, StaticShadowCastersKeepSolidSurfacesAndDropTheRest)
{
    for ( size_t i = 0; i < SITHRENDERTEST_NUMVERTS; ++i )
    {
        sithRenderTest_aWorldVertices[i].x = (float)i;
        sithRenderTest_aWorldVertices[i].y = (float)i * 0.5f;
        sithRenderTest_aWorldVertices[i].z = (float)i * 0.25f;
    }
    sithRenderTest_world.aVertices          = sithRenderTest_aWorldVertices;
    sithWorld_g_pCurrentWorld               = &sithRenderTest_world;

    const int aSolid[4]    = { 0, 1, 2, 3 };
    const int aSky[4]      = { 1, 2, 3, 4 };
    const int aWater[4]    = { 2, 3, 4, 5 };
    const int aAdjoin[4]   = { 3, 4, 5, 6 };
    const int aNoGeom[4]   = { 4, 5, 6, 7 };
    const int aTriangle[3] = { 5, 6, 7 };

    sithRenderTest_AddSurface(aSolid, 4u, 0.0f, 0.0f, 1.0f);

    sithRenderTest_AddSurface(aSky, 4u, 0.0f, 0.0f, 1.0f);
    sithRenderTest_aSurfaces[sithRenderTest_world.numSurfaces - 1u].flags = SITH_SURFACE_HORIZONSKY;

    sithRenderTest_AddSurface(aWater, 4u, 0.0f, 0.0f, 1.0f);
    sithRenderTest_aSurfaces[sithRenderTest_world.numSurfaces - 1u].flags = SITH_SURFACE_WATER;

    sithRenderTest_AddSurface(aAdjoin, 4u, 0.0f, 0.0f, 1.0f);
    sithRenderTest_aSurfaces[sithRenderTest_world.numSurfaces - 1u].pAdjoin = &sithRenderTest_adjoin;
    sithRenderTest_aSurfaces[sithRenderTest_world.numSurfaces - 1u].face.flags = RD_FF_TEX_TRANSLUCENT;

    sithRenderTest_AddSurface(aNoGeom, 4u, 0.0f, 0.0f, 1.0f);
    sithRenderTest_aSurfaces[sithRenderTest_world.numSurfaces - 1u].face.geometryMode = RD_GEOMETRY_NONE;

    sithRenderTest_AddSurface(aTriangle, 3u, 0.0f, 0.0f, 1.0f);

    TEST_ASSERT_EQUAL_INT(0, sithRender_Open());
    TEST_ASSERT_EQUAL_size_t(1u, sithRenderTest_numStaticShadowCasterCalls);

    // The solid quad fans into two triangles, the solid triangle stays one: nine vertices.
    TEST_ASSERT_EQUAL_size_t(9u, sithRenderTest_numStaticShadowCasters);
    const int aExpected[9] = { 0, 1, 2, 0, 2, 3, 5, 6, 7 };
    for ( size_t i = 0; i < 9u; ++i )
    {
        const rdVector3* pVertex = &sithRenderTest_aWorldVertices[aExpected[i]];
        TEST_ASSERT_EQUAL_FLOAT(pVertex->x, sithRenderTest_aStaticShadowCasters[i * 3u + 0u]);
        TEST_ASSERT_EQUAL_FLOAT(pVertex->y, sithRenderTest_aStaticShadowCasters[i * 3u + 1u]);
        TEST_ASSERT_EQUAL_FLOAT(pVertex->z, sithRenderTest_aStaticShadowCasters[i * 3u + 2u]);
    }

    sithRender_Close();
    TEST_ASSERT_EQUAL_size_t(2u, sithRenderTest_numStaticShadowCasterCalls);
    TEST_ASSERT_EQUAL_size_t(0u, sithRenderTest_numStaticShadowCasters);
}

TEST_GROUP_RUNNER(sithRender)
{
    RUN_TEST_CASE(sithRender, AFlatQuadSplitInTwoSurfacesKeepsTheFaceNormal);
    RUN_TEST_CASE(sithRender, AThirtyDegreeHingeSmoothsOnlyTheSharedVertices);
    RUN_TEST_CASE(sithRender, ANinetyDegreeHingeKeepsBothFaceNormals);
    RUN_TEST_CASE(sithRender, ClassicAndPerPixelBothUseTheSmoothedNormal);
    RUN_TEST_CASE(sithRender, AVertexOutOfRangeJoinsNothingAndDoesNotCrash);
    RUN_TEST_CASE(sithRender, StaticShadowCastersKeepSolidSurfacesAndDropTheRest);
}
