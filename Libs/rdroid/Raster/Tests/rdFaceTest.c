// Added: tests of rdFace_CalcSmoothNormals (option A L3, ADR-0023), the smoothed corner normals the
// lit path gives level surfaces, which store a face normal only. The cases are worked out by hand: a
// cube keeps its hard corners, a 12-sided prism (sides 30 degrees apart) smooths each vertical edge
// to the bisector of its two sides, two faces 44 and 46 degrees apart are smoothed and kept hard under
// the 45 degree threshold, and faces that are excluded, have no geometry, no normal or a vertex out of
// range give nothing to their neighbours.
#include <unity_fixture.h>

#include <j3dcore/Tests/j3dTest.h>
#include <rdroid/Math/Tests/rdMathTestHost.h>

#include <rdroid/Raster/rdFace.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define RDFACETEST_MAXFACES   16
#define RDFACETEST_MAXCORNERS 64
#define RDFACETEST_PI         3.14159265358979323846

static void* J3DAPI rdFaceTest_Malloc(size_t size)
{
    return malloc(size);
}

static void J3DAPI rdFaceTest_Free(void* pData)
{
    free(pData);
}

static void* J3DAPI rdFaceTest_Realloc(void* pData, size_t size)
{
    return realloc(pData, size);
}

static int J3DAPI rdFaceTest_Print(const char* pFormat, ...)
{
    (void)pFormat;
    return 0;
}

// Faces of up to 4 vertices, built by the tests.
static rdFace rdFaceTest_aFaces[RDFACETEST_MAXFACES];
static int rdFaceTest_aFaceVerts[RDFACETEST_MAXFACES][4];
static const rdFace* rdFaceTest_apFaces[RDFACETEST_MAXFACES];
static size_t rdFaceTest_numFaces;

static size_t rdFaceTest_aFirstCorner[RDFACETEST_MAXFACES];
static rdVector3 rdFaceTest_aCorners[RDFACETEST_MAXCORNERS];

static void rdFaceTest_AddFace(const int* aVerts, size_t numVerts, float nx, float ny, float nz)
{
    TEST_ASSERT_TRUE(rdFaceTest_numFaces < RDFACETEST_MAXFACES);
    TEST_ASSERT_TRUE(numVerts <= 4u);

    size_t faceNum = rdFaceTest_numFaces++;
    rdFace* pFace  = &rdFaceTest_aFaces[faceNum];
    memset(pFace, 0, sizeof(*pFace));
    for ( size_t i = 0; i < numVerts; ++i )
    {
        rdFaceTest_aFaceVerts[faceNum][i] = aVerts[i];
    }

    pFace->geometryMode = RD_GEOMETRY_FULL;
    pFace->numVertices  = numVerts;
    pFace->aVertices    = rdFaceTest_aFaceVerts[faceNum];
    pFace->normal.x     = nx;
    pFace->normal.y     = ny;
    pFace->normal.z     = nz;
    rdFaceTest_apFaces[faceNum] = pFace;
}

static bool rdFaceTest_Calc(const bool* abExcluded, size_t numVertices, float maxAngle)
{
    return rdFace_CalcSmoothNormals(rdFaceTest_apFaces, abExcluded, rdFaceTest_numFaces, numVertices, maxAngle, rdFaceTest_aFirstCorner, rdFaceTest_aCorners);
}

static void rdFaceTest_AssertCorner(size_t faceNum, size_t i, double x, double y, double z)
{
    const rdVector3* pCorner = &rdFaceTest_aCorners[rdFaceTest_aFirstCorner[faceNum] + i];
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, (float)x, pCorner->x);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, (float)y, pCorner->y);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, (float)z, pCorner->z);
}

TEST_GROUP(rdFace);

TEST_SETUP(rdFace)
{
    J3DTest_Startup();
    J3DTest_BindHostServicesGlobal(rdroid_g_pHS_ADDR);
    J3DTest_BindHostServicesGlobal((uintptr_t)&std_g_pHS);

    tHostServices* pHS = J3DTest_GetHostServices();
    pHS->pStatusPrint  = rdFaceTest_Print;
    pHS->pMessagePrint = rdFaceTest_Print;
    pHS->pWarningPrint = rdFaceTest_Print;
    pHS->pErrorPrint   = rdFaceTest_Print;
    pHS->pDebugPrint   = rdFaceTest_Print;
    pHS->pMalloc       = rdFaceTest_Malloc;
    pHS->pFree         = rdFaceTest_Free;
    pHS->pRealloc      = rdFaceTest_Realloc;

    memset(rdFaceTest_aFaces, 0, sizeof(rdFaceTest_aFaces));
    memset(rdFaceTest_apFaces, 0, sizeof(rdFaceTest_apFaces));
    rdFaceTest_numFaces = 0;
    memset(rdFaceTest_aFirstCorner, 0xA5, sizeof(rdFaceTest_aFirstCorner));
    memset(rdFaceTest_aCorners, 0xA5, sizeof(rdFaceTest_aCorners));
}

TEST_TEAR_DOWN(rdFace)
{
    J3DTest_Shutdown();
}

TEST(rdFace, ACubeKeepsItsHardCorners)
{
    // Vertex (x, y, z) in {0, 1}^3 is x + 2y + 4z; every corner joins three faces 90 degrees apart.
    const int aBottom[4] = { 0, 2, 3, 1 };
    const int aTop[4]    = { 4, 5, 7, 6 };
    const int aFront[4]  = { 0, 1, 5, 4 };
    const int aBack[4]   = { 2, 6, 7, 3 };
    const int aLeft[4]   = { 0, 4, 6, 2 };
    const int aRight[4]  = { 1, 3, 7, 5 };
    rdFaceTest_AddFace(aBottom, 4u, 0.0f, 0.0f, -1.0f);
    rdFaceTest_AddFace(aTop, 4u, 0.0f, 0.0f, 1.0f);
    rdFaceTest_AddFace(aFront, 4u, 0.0f, -1.0f, 0.0f);
    rdFaceTest_AddFace(aBack, 4u, 0.0f, 1.0f, 0.0f);
    rdFaceTest_AddFace(aLeft, 4u, -1.0f, 0.0f, 0.0f);
    rdFaceTest_AddFace(aRight, 4u, 1.0f, 0.0f, 0.0f);

    TEST_ASSERT_TRUE(rdFaceTest_Calc(NULL, 8u, 45.0f));

    for ( size_t f = 0; f < 6u; ++f )
    {
        TEST_ASSERT_EQUAL_size_t(4u * f, rdFaceTest_aFirstCorner[f]);
        for ( size_t i = 0; i < 4u; ++i )
        {
            const rdVector3* pNormal = &rdFaceTest_aFaces[f].normal;
            rdFaceTest_AssertCorner(f, i, pNormal->x, pNormal->y, pNormal->z);
        }
    }

    // Nothing past the 24 corners is written.
    rdVector3 pattern;
    memset(&pattern, 0xA5, sizeof(pattern));
    TEST_ASSERT_EQUAL_MEMORY(&pattern, &rdFaceTest_aCorners[24], sizeof(pattern));
}

TEST(rdFace, APrismsSidesAverageAcrossTheirEdges)
{
    // A 12-sided prism around z: side k spans the bottom vertices k, k + 1 and the top ones 12 + k,
    // 12 + k + 1 (mod 12); its normal points at 30k + 15 degrees. The caps are 90 degrees off the
    // sides and stay hard. So every side corner on the edge at vertex j points at 30j degrees, the
    // bisector of the two sides that meet there, and the cap corners keep (0, 0, -1) and (0, 0, 1).
    const size_t numSides = 12u;
    for ( size_t k = 0; k < numSides; ++k )
    {
        int next     = (int)((k + 1u) % numSides);
        int aSide[4] = { (int)k, next, 12 + next, 12 + (int)k };
        double angle = (30.0 * (double)k + 15.0) * RDFACETEST_PI / 180.0;
        rdFaceTest_AddFace(aSide, 4u, (float)cos(angle), (float)sin(angle), 0.0f);
    }

    // The caps as two fans of quads would be too many faces here: each cap is one 4-vertex face over
    // vertices 0, 3, 6, 9 (bottom) and 12, 15, 18, 21 (top), enough to show caps stay hard.
    const int aBottom[4] = { 0, 9, 6, 3 };
    const int aTop[4]    = { 12, 15, 18, 21 };
    rdFaceTest_AddFace(aBottom, 4u, 0.0f, 0.0f, -1.0f);
    rdFaceTest_AddFace(aTop, 4u, 0.0f, 0.0f, 1.0f);

    TEST_ASSERT_TRUE(rdFaceTest_Calc(NULL, 24u, 45.0f));

    for ( size_t k = 0; k < numSides; ++k )
    {
        for ( size_t i = 0; i < 4u; ++i )
        {
            size_t vertex = (size_t)rdFaceTest_aFaceVerts[k][i] % 12u;
            double angle  = 30.0 * (double)vertex * RDFACETEST_PI / 180.0;
            rdFaceTest_AssertCorner(k, i, cos(angle), sin(angle), 0.0);
        }
    }

    for ( size_t i = 0; i < 4u; ++i )
    {
        rdFaceTest_AssertCorner(12u, i, 0.0, 0.0, -1.0);
        rdFaceTest_AssertCorner(13u, i, 0.0, 0.0, 1.0);
    }
}

// Two quads sharing the edge (1, 2), the first in the plane z = 0 (normal (0, 0, 1)), the second
// tilted by angle degrees about the x axis (normal (0, -sin, cos)).
static void rdFaceTest_AddHinge(double angleDeg)
{
    const int aFlat[4]   = { 0, 1, 2, 3 };
    const int aTilted[4] = { 2, 1, 4, 5 };
    double angle         = angleDeg * RDFACETEST_PI / 180.0;
    rdFaceTest_AddFace(aFlat, 4u, 0.0f, 0.0f, 1.0f);
    rdFaceTest_AddFace(aTilted, 4u, 0.0f, (float)-sin(angle), (float)cos(angle));
}

TEST(rdFace, EdgesUnderTheThresholdAreSmoothed)
{
    // 44 degrees apart, under 45: the shared corners take the bisector, at 22 degrees; the others
    // keep their face normal.
    rdFaceTest_AddHinge(44.0);
    TEST_ASSERT_TRUE(rdFaceTest_Calc(NULL, 6u, 45.0f));

    double half = 22.0 * RDFACETEST_PI / 180.0;
    double tilt = 44.0 * RDFACETEST_PI / 180.0;
    rdFaceTest_AssertCorner(0, 0, 0.0, 0.0, 1.0);
    rdFaceTest_AssertCorner(0, 1, 0.0, -sin(half), cos(half));
    rdFaceTest_AssertCorner(0, 2, 0.0, -sin(half), cos(half));
    rdFaceTest_AssertCorner(0, 3, 0.0, 0.0, 1.0);
    rdFaceTest_AssertCorner(1, 0, 0.0, -sin(half), cos(half));
    rdFaceTest_AssertCorner(1, 1, 0.0, -sin(half), cos(half));
    rdFaceTest_AssertCorner(1, 2, 0.0, -sin(tilt), cos(tilt));
    rdFaceTest_AssertCorner(1, 3, 0.0, -sin(tilt), cos(tilt));
}

TEST(rdFace, EdgesOverTheThresholdStayHard)
{
    // 46 degrees apart, over 45: every corner keeps its face normal. The same hinge smooths under a
    // threshold of 50.
    rdFaceTest_AddHinge(46.0);
    TEST_ASSERT_TRUE(rdFaceTest_Calc(NULL, 6u, 45.0f));

    double tilt = 46.0 * RDFACETEST_PI / 180.0;
    for ( size_t i = 0; i < 4u; ++i )
    {
        rdFaceTest_AssertCorner(0, i, 0.0, 0.0, 1.0);
        rdFaceTest_AssertCorner(1, i, 0.0, -sin(tilt), cos(tilt));
    }

    TEST_ASSERT_TRUE(rdFaceTest_Calc(NULL, 6u, 50.0f));
    double half = 23.0 * RDFACETEST_PI / 180.0;
    rdFaceTest_AssertCorner(0, 1, 0.0, -sin(half), cos(half));
    rdFaceTest_AssertCorner(1, 0, 0.0, -sin(half), cos(half));
}

TEST(rdFace, CoplanarFacesAndUnnormalizedNormalsGiveUnitFaceNormals)
{
    // Two coplanar quads with normals of length 2 and 3: every corner is the unit normal.
    const int aFirst[4]  = { 0, 1, 2, 3 };
    const int aSecond[4] = { 1, 4, 5, 2 };
    rdFaceTest_AddFace(aFirst, 4u, 0.0f, 2.0f, 0.0f);
    rdFaceTest_AddFace(aSecond, 4u, 0.0f, 3.0f, 0.0f);
    TEST_ASSERT_TRUE(rdFaceTest_Calc(NULL, 6u, 45.0f));

    for ( size_t i = 0; i < 4u; ++i )
    {
        rdFaceTest_AssertCorner(0, i, 0.0, 1.0, 0.0);
        rdFaceTest_AssertCorner(1, i, 0.0, 1.0, 0.0);
    }
}

TEST(rdFace, ExcludedFacesAndFacesWithoutGeometryOrNormalAreNoPartners)
{
    // Four hinges at 30 degrees on separate vertices; the tilted face of the first is excluded, of the
    // second has no geometry, of the third has no normal; the fourth is the control and smooths.
    double half = 15.0 * RDFACETEST_PI / 180.0;
    double tilt = 30.0 * RDFACETEST_PI / 180.0;
    for ( int h = 0; h < 4; ++h )
    {
        const int aFlat[4]   = { 6 * h + 0, 6 * h + 1, 6 * h + 2, 6 * h + 3 };
        const int aTilted[4] = { 6 * h + 2, 6 * h + 1, 6 * h + 4, 6 * h + 5 };
        rdFaceTest_AddFace(aFlat, 4u, 0.0f, 0.0f, 1.0f);
        rdFaceTest_AddFace(aTilted, 4u, 0.0f, (float)-sin(tilt), (float)cos(tilt));
    }

    bool abExcluded[8] = { false, true, false, false, false, false, false, false };
    rdFaceTest_aFaces[3].geometryMode = RD_GEOMETRY_NONE;
    rdFaceTest_aFaces[5].normal.x     = 0.0f;
    rdFaceTest_aFaces[5].normal.y     = 0.0f;
    rdFaceTest_aFaces[5].normal.z     = 0.0f;

    TEST_ASSERT_TRUE(rdFaceTest_Calc(abExcluded, 24u, 45.0f));

    // The flat faces of the first three hinges have no partner: hard.
    for ( size_t f = 0; f < 6u; f += 2u )
    {
        for ( size_t i = 0; i < 4u; ++i )
        {
            rdFaceTest_AssertCorner(f, i, 0.0, 0.0, 1.0);
        }
    }

    // The excluded face and the one without geometry still get their corners smoothed by the flat
    // face; the one without a normal gets zero corners.
    rdFaceTest_AssertCorner(1, 0, 0.0, -sin(half), cos(half));
    rdFaceTest_AssertCorner(1, 2, 0.0, -sin(tilt), cos(tilt));
    rdFaceTest_AssertCorner(3, 1, 0.0, -sin(half), cos(half));
    for ( size_t i = 0; i < 4u; ++i )
    {
        rdFaceTest_AssertCorner(5, i, 0.0, 0.0, 0.0);
    }

    // The control.
    rdFaceTest_AssertCorner(6, 1, 0.0, -sin(half), cos(half));
    rdFaceTest_AssertCorner(7, 0, 0.0, -sin(half), cos(half));
}

TEST(rdFace, AVertexOutOfRangeJoinsNothing)
{
    // The hinge at 30 degrees, but the second face's copy of vertex 1 is -1 and vertex 2 is 6, out of
    // range for 6 vertices: no corner is shared, so all keep their face normals.
    const int aFlat[4]   = { 0, 1, 2, 3 };
    const int aTilted[4] = { 6, -1, 4, 5 };
    double tilt          = 30.0 * RDFACETEST_PI / 180.0;
    rdFaceTest_AddFace(aFlat, 4u, 0.0f, 0.0f, 1.0f);
    rdFaceTest_AddFace(aTilted, 4u, 0.0f, (float)-sin(tilt), (float)cos(tilt));
    TEST_ASSERT_TRUE(rdFaceTest_Calc(NULL, 6u, 45.0f));

    for ( size_t i = 0; i < 4u; ++i )
    {
        rdFaceTest_AssertCorner(0, i, 0.0, 0.0, 1.0);
        rdFaceTest_AssertCorner(1, i, 0.0, -sin(tilt), cos(tilt));
    }
}

TEST(rdFace, BadArgumentsWriteNothing)
{
    const int aFlat[4] = { 0, 1, 2, 3 };
    rdFaceTest_AddFace(aFlat, 4u, 0.0f, 0.0f, 1.0f);
    rdFaceTest_AddFace(aFlat, 4u, 0.0f, 0.0f, 1.0f);

    TEST_ASSERT_FALSE(rdFace_CalcSmoothNormals(NULL, NULL, 2u, 4u, 45.0f, rdFaceTest_aFirstCorner, rdFaceTest_aCorners));
    TEST_ASSERT_FALSE(rdFace_CalcSmoothNormals(rdFaceTest_apFaces, NULL, 2u, 4u, 45.0f, NULL, rdFaceTest_aCorners));
    TEST_ASSERT_FALSE(rdFace_CalcSmoothNormals(rdFaceTest_apFaces, NULL, 2u, 4u, 45.0f, rdFaceTest_aFirstCorner, NULL));

    rdFaceTest_apFaces[1] = NULL;
    TEST_ASSERT_FALSE(rdFace_CalcSmoothNormals(rdFaceTest_apFaces, NULL, 2u, 4u, 45.0f, rdFaceTest_aFirstCorner, rdFaceTest_aCorners));

    uint8_t aPattern[sizeof(rdFaceTest_aCorners)];
    memset(aPattern, 0xA5, sizeof(aPattern));
    TEST_ASSERT_EQUAL_MEMORY(aPattern, rdFaceTest_aCorners, sizeof(rdFaceTest_aCorners));
    TEST_ASSERT_EQUAL_MEMORY(aPattern, rdFaceTest_aFirstCorner, sizeof(rdFaceTest_aFirstCorner));

    // No faces: nothing to do, success.
    TEST_ASSERT_TRUE(rdFace_CalcSmoothNormals(NULL, NULL, 0u, 0u, 45.0f, NULL, NULL));
}

TEST_GROUP_RUNNER(rdFace)
{
    RUN_TEST_CASE(rdFace, ACubeKeepsItsHardCorners);
    RUN_TEST_CASE(rdFace, APrismsSidesAverageAcrossTheirEdges);
    RUN_TEST_CASE(rdFace, EdgesUnderTheThresholdAreSmoothed);
    RUN_TEST_CASE(rdFace, EdgesOverTheThresholdStayHard);
    RUN_TEST_CASE(rdFace, CoplanarFacesAndUnnormalizedNormalsGiveUnitFaceNormals);
    RUN_TEST_CASE(rdFace, ExcludedFacesAndFacesWithoutGeometryOrNormalAreNoPartners);
    RUN_TEST_CASE(rdFace, AVertexOutOfRangeJoinsNothing);
    RUN_TEST_CASE(rdFace, BadArgumentsWriteNothing);
}
