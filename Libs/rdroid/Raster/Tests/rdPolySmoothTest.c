// Added: tests of rdPolySmooth, the ring tessellation that gives a world polygon smooth Gouraud
// light instead of the old zig-zag's diagonal. The cases work out the ring count from the
// view-space edge length, the mean value weights (partition of unity and linear precision), the
// projected attributes of the extra vertices, and that the indices keep the polygon's winding,
// cover its area and never split a boundary edge.
#include <unity_fixture.h>

#include <j3dcore/Tests/j3dTest.h>
#include <rdroid/Math/Tests/rdMathTestHost.h>

#include <rdroid/Math/rdVector.h>
#include <rdroid/Raster/rdCache.h>
#include <rdroid/Raster/rdPolySmooth.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define RDPOLYSMOOTHTEST_MAXVERTS 64

static void* J3DAPI rdPolySmoothTest_Malloc(size_t size)
{
    return malloc(size);
}

static void J3DAPI rdPolySmoothTest_Free(void* pData)
{
    free(pData);
}

static void* J3DAPI rdPolySmoothTest_Realloc(void* pData, size_t size)
{
    return realloc(pData, size);
}

static int J3DAPI rdPolySmoothTest_Print(const char* pFormat, ...)
{
    (void)pFormat;
    return 0;
}

TEST_GROUP(rdPolySmooth);

TEST_SETUP(rdPolySmooth)
{
    J3DTest_Startup();
    J3DTest_BindHostServicesGlobal(rdroid_g_pHS_ADDR);
    J3DTest_BindHostServicesGlobal((uintptr_t)&std_g_pHS);

    tHostServices* pHS = J3DTest_GetHostServices();
    pHS->pStatusPrint  = rdPolySmoothTest_Print;
    pHS->pMessagePrint = rdPolySmoothTest_Print;
    pHS->pWarningPrint = rdPolySmoothTest_Print;
    pHS->pErrorPrint   = rdPolySmoothTest_Print;
    pHS->pDebugPrint   = rdPolySmoothTest_Print;
    pHS->pMalloc       = rdPolySmoothTest_Malloc;
    pHS->pFree         = rdPolySmoothTest_Free;
    pHS->pRealloc      = rdPolySmoothTest_Realloc;
}

TEST_TEAR_DOWN(rdPolySmooth)
{
    J3DTest_Shutdown();
}

// A square in the y = 2 plane, corners in the order (x, z) = (-h, -h), (h, -h), (h, h), (-h, h).
static void rdPolySmoothTest_MakeSquare(rdVector3* aVerts, float side)
{
    float h = 0.5f * side;
    aVerts[0] = (rdVector3){ -h, 2.0f, -h };
    aVerts[1] = (rdVector3){  h, 2.0f, -h };
    aVerts[2] = (rdVector3){  h, 2.0f,  h };
    aVerts[3] = (rdVector3){ -h, 2.0f,  h };
}

// Centred ring point of ring r, corner i, as the tessellation promises it.
static rdVector3 rdPolySmoothTest_RingPoint(const rdVector3* aVerts, size_t numVerts, size_t rings, size_t r, size_t i)
{
    rdVector3 centre = { 0.0f, 0.0f, 0.0f };
    for ( size_t k = 0; k < numVerts; ++k )
    {
        rdVector_Add3Acc(&centre, &aVerts[k]);
    }
    rdVector_InvScale3Acc(&centre, (float)numVerts);

    float t = (float)r / (float)rings;
    rdVector3 point;
    rdVector_Sub3(&point, &aVerts[i], &centre);
    rdVector_Scale3Acc(&point, t);
    rdVector_Add3Acc(&point, &centre);
    return point;
}

static void rdPolySmoothTest_AssertRingCount(const rdVector3* aVerts, size_t numVerts, size_t expected)
{
    TEST_ASSERT_EQUAL_size_t(expected, rdPolySmooth_GetRingCount(aVerts, numVerts));
}

TEST(rdPolySmooth, RingCountFollowsTheViewSpaceSize)
{
    rdVector3 aVerts[4];

    rdPolySmoothTest_MakeSquare(aVerts, 0.05f);
    rdPolySmoothTest_AssertRingCount(aVerts, 4u, 0u);

    rdPolySmoothTest_MakeSquare(aVerts, 0.25f);
    rdPolySmoothTest_AssertRingCount(aVerts, 4u, 3u);

    rdPolySmoothTest_MakeSquare(aVerts, 5.0f);
    rdPolySmoothTest_AssertRingCount(aVerts, 4u, 8u);

    const rdVector3 aTriangle[3] = { { 0.0f, 2.0f, 0.0f }, { 1.0f, 2.0f, 0.0f }, { 0.0f, 2.0f, 1.0f } };
    rdPolySmoothTest_AssertRingCount(aTriangle, 3u, 0u);

    // V2 is reflex: the turn at V1 and at V2 disagree.
    const rdVector3 aNonConvex[4] = {
        { 0.0f, 2.0f, 0.0f }, { 2.0f, 2.0f, 0.0f }, { 0.5f, 2.0f, 0.5f }, { 0.0f, 2.0f, 2.0f },
    };
    rdPolySmoothTest_AssertRingCount(aNonConvex, 4u, 0u);

    // A square with an extra corner in the middle of its top edge: the straight turn is allowed.
    const rdVector3 aStraightEdgePentagon[5] = {
        { -0.5f, 2.0f, -0.5f }, { 0.5f, 2.0f, -0.5f }, { 0.5f, 2.0f, 0.5f },
        { 0.0f, 2.0f, 0.5f }, { -0.5f, 2.0f, 0.5f },
    };
    TEST_ASSERT_TRUE(rdPolySmooth_GetRingCount(aStraightEdgePentagon, 5u) > 0u);

    // A pentagon whose first three corners are collinear: Newell's normal does not depend on them.
    const rdVector3 aCollinearStartPentagon[5] = {
        { 0.0f, 2.0f, 0.0f }, { 1.0f, 2.0f, 0.0f }, { 2.0f, 2.0f, 0.0f },
        { 2.0f, 2.0f, 1.0f }, { 0.0f, 2.0f, 1.0f },
    };
    TEST_ASSERT_TRUE(rdPolySmooth_GetRingCount(aCollinearStartPentagon, 5u) > 0u);

    // A degenerate polygon has no normal.
    const rdVector3 aCollinear[4] = {
        { 0.0f, 2.0f, 0.0f }, { 1.0f, 2.0f, 0.0f }, { 2.0f, 2.0f, 0.0f }, { 3.0f, 2.0f, 0.0f },
    };
    rdPolySmoothTest_AssertRingCount(aCollinear, 4u, 0u);

    TEST_ASSERT_EQUAL_size_t(0u, rdPolySmooth_GetRingCount(NULL, 4u));
}

TEST(rdPolySmooth, ExtraVertexAndIndexCounts)
{
    TEST_ASSERT_EQUAL_size_t(9u, rdPolySmooth_GetExtraVertexCount(4u, 3u));
    TEST_ASSERT_EQUAL_size_t(1u, rdPolySmooth_GetExtraVertexCount(4u, 1u));
    TEST_ASSERT_EQUAL_size_t(0u, rdPolySmooth_GetExtraVertexCount(4u, 0u));
    TEST_ASSERT_EQUAL_size_t(43u, rdPolySmooth_GetExtraVertexCount(6u, 8u));

    TEST_ASSERT_EQUAL_size_t(60u, rdPolySmooth_GetIndexCount(4u, 3u));
    TEST_ASSERT_EQUAL_size_t(12u, rdPolySmooth_GetIndexCount(4u, 1u));
    TEST_ASSERT_EQUAL_size_t(0u, rdPolySmooth_GetIndexCount(4u, 0u));
    TEST_ASSERT_EQUAL_size_t(270u, rdPolySmooth_GetIndexCount(6u, 8u));
}

TEST(rdPolySmooth, MeanValueWeightsArePartitionOfUnityAndReproduceLinearFunctions)
{
    // A convex hexagon of radius 1 in the y = 2 plane.
    rdVector3 aHexagon[6];
    for ( size_t i = 0; i < 6u; ++i )
    {
        float angle = (float)(2.0 * 3.14159265358979323846 * (double)i / 6.0);
        aHexagon[i] = (rdVector3){ cosf(angle), 2.0f, sinf(angle) };
    }

    const rdVector3 aInterior[4] = {
        { 0.0f, 2.0f, 0.0f }, { 0.2f, 2.0f, 0.1f }, { -0.3f, 2.0f, 0.25f }, { 0.1f, 2.0f, -0.4f },
    };
    const float a = 0.75f;
    const rdVector3 b = { 0.3f, -0.2f, 0.6f };

    for ( size_t p = 0; p < STD_ARRAYLEN(aInterior); ++p )
    {
        float aWeights[6];
        TEST_ASSERT_TRUE(rdPolySmooth_MeanValueWeights(aHexagon, 6u, &aInterior[p], aWeights));

        float sum = 0.0f;
        float linear = 0.0f;
        for ( size_t k = 0; k < 6u; ++k )
        {
            sum += aWeights[k];
            float f = a + b.x * aHexagon[k].x + b.y * aHexagon[k].y + b.z * aHexagon[k].z;
            linear += aWeights[k] * f;
        }

        float expected = a + b.x * aInterior[p].x + b.y * aInterior[p].y + b.z * aInterior[p].z;
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, sum);
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, expected, linear);
    }

    // A point on a corner returns that corner.
    float aWeights[6];
    TEST_ASSERT_TRUE(rdPolySmooth_MeanValueWeights(aHexagon, 6u, &aHexagon[2], aWeights));
    for ( size_t k = 0; k < 6u; ++k )
    {
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, k == 2u ? 1.0f : 0.0f, aWeights[k]);
    }

    TEST_ASSERT_FALSE(rdPolySmooth_MeanValueWeights(NULL, 6u, &aInterior[0], aWeights));
    TEST_ASSERT_FALSE(rdPolySmooth_MeanValueWeights(aHexagon, 6u, NULL, aWeights));
    TEST_ASSERT_FALSE(rdPolySmooth_MeanValueWeights(aHexagon, 6u, &aInterior[0], NULL));
    TEST_ASSERT_FALSE(rdPolySmooth_MeanValueWeights(aHexagon, 2u, &aInterior[0], aWeights));
}

// The projected attributes and the interpolated light of a face's extra vertices.
static void rdPolySmoothTest_SetupFace(rdCacheProcEntry* pFace, const rdVector3* aViewVerts, const rdVector3* aNormals,
    StdVertexTL* aScreenVerts, rdVector4* aIntensities, const rdVector3* aTexCoords, size_t numVerts)
{
    memset(pFace, 0, sizeof(*pFace));
    pFace->numVertices      = numVerts;
    pFace->aViewVertices    = (rdVector3*)aViewVerts;
    pFace->aViewNormals     = (rdVector3*)aNormals;
    pFace->aVertices        = aScreenVerts;
    pFace->aVertIntensities = aIntensities;

    for ( size_t k = 0; k < numVerts; ++k )
    {
        memset(&aScreenVerts[k], 0, sizeof(aScreenVerts[k]));
        aScreenVerts[k].color    = 0x11223344u;
        aScreenVerts[k].specular = 0x55667788u;
        aScreenVerts[k].tu       = aTexCoords[k].x;
        aScreenVerts[k].tv       = aTexCoords[k].y;
    }
}

static void rdPolySmoothTest_Project(const rdVector3* pViewVert, const rdPolySmoothProjection* pProj,
    float* pSx, float* pSy, float* pSz, float* pRhw)
{
    float invY  = 1.0f / pViewVert->y;
    float scale = pProj->focalLength * invY;
    *pSx  = pViewVert->x * scale + pProj->centerX;
    *pSy  = pProj->centerY - pViewVert->z * scale;
    *pSz  = (invY - pProj->invNearPlane) * pProj->invFarPlane;
    *pRhw = invY / 32.0f;
}

TEST(rdPolySmooth, BuildVerticesProjectsInterpolatesAndSmoothsTheCorners)
{
    rdVector3 aView[4];
    rdPolySmoothTest_MakeSquare(aView, 0.5f); // five rings: 17 extra vertices

    const rdVector3 aNormals[4] = {
        { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f },
    };
    const rdVector3 aTexCoords[4] = {
        { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f },
    };

    StdVertexTL aScreen[4];
    rdVector4 aCornerIntensities[4];
    for ( size_t k = 0; k < 4u; ++k )
    {
        aCornerIntensities[k] = (rdVector4){
            0.1f + 0.2f * aView[k].x + 0.3f * aView[k].z,
            0.4f - 0.1f * aView[k].x + 0.05f * aView[k].z,
            0.2f + 0.25f * aView[k].x - 0.15f * aView[k].z,
            1.0f,
        };
    }

    rdCacheProcEntry face;
    rdPolySmoothTest_SetupFace(&face, aView, aNormals, aScreen, aCornerIntensities, aTexCoords, 4u);

    const rdPolySmoothProjection proj = { 200.0f, 320.0f, 240.0f, 0.5f, 0.01f };
    size_t rings = rdPolySmooth_GetRingCount(aView, 4u);
    TEST_ASSERT_EQUAL_size_t(5u, rings);

    size_t numExtra = rdPolySmooth_GetExtraVertexCount(4u, rings);
    TEST_ASSERT_EQUAL_size_t(17u, numExtra);

    StdVertexTL aOut[RDPOLYSMOOTH_MAXEXTRAVERTS];
    rdVector4 aIntensities[RDPOLYSMOOTH_MAXEXTRAVERTS];
    rdVector3 aViewVerts[RDPOLYSMOOTH_MAXEXTRAVERTS];
    rdVector3 aViewNormals[RDPOLYSMOOTH_MAXEXTRAVERTS];
    rdPolySmooth_BuildVertices(&face, rings, &proj, aCornerIntensities, aOut, aIntensities, aViewVerts, aViewNormals);

    size_t outIdx = 0;
    for ( size_t r = 1; r < rings; ++r )
    {
        for ( size_t i = 0; i < 4u; ++i, ++outIdx )
        {
            rdVector3 expected = rdPolySmoothTest_RingPoint(aView, 4u, rings, r, i);
            TEST_ASSERT_FLOAT_WITHIN(1e-6f, expected.x, aViewVerts[outIdx].x);
            TEST_ASSERT_FLOAT_WITHIN(1e-6f, expected.y, aViewVerts[outIdx].y);
            TEST_ASSERT_FLOAT_WITHIN(1e-6f, expected.z, aViewVerts[outIdx].z);

            float sx, sy, sz, rhw;
            rdPolySmoothTest_Project(&expected, &proj, &sx, &sy, &sz, &rhw);
            TEST_ASSERT_FLOAT_WITHIN(1e-5f, sx, aOut[outIdx].sx);
            TEST_ASSERT_FLOAT_WITHIN(1e-5f, sy, aOut[outIdx].sy);
            TEST_ASSERT_FLOAT_WITHIN(1e-5f, sz, aOut[outIdx].sz);
            TEST_ASSERT_FLOAT_WITHIN(1e-5f, rhw, aOut[outIdx].rhw);

            // Texture coordinates are affine over the planar face.
            float t = (float)r / (float)rings;
            float tu = 0.0f;
            float tv = 0.0f;
            for ( size_t k = 0; k < 4u; ++k )
            {
                float beta = (1.0f - t) / 4.0f + (k == i ? t : 0.0f);
                tu += beta * aTexCoords[k].x;
                tv += beta * aTexCoords[k].y;
            }
            TEST_ASSERT_FLOAT_WITHIN(1e-6f, tu, aOut[outIdx].tu);
            TEST_ASSERT_FLOAT_WITHIN(1e-6f, tv, aOut[outIdx].tv);

            TEST_ASSERT_EQUAL_UINT32(0x11223344u, aOut[outIdx].color);
            TEST_ASSERT_EQUAL_UINT32(0x55667788u, aOut[outIdx].specular);

            // A linear function of the corner values is reproduced at the smooth interior point.
            TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.1f + 0.2f * expected.x + 0.3f * expected.z, aIntensities[outIdx].red);
            TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.4f - 0.1f * expected.x + 0.05f * expected.z, aIntensities[outIdx].green);
            TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.2f + 0.25f * expected.x - 0.15f * expected.z, aIntensities[outIdx].blue);
            TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.0f, aIntensities[outIdx].alpha);

            TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, aViewNormals[outIdx].x);
            TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, aViewNormals[outIdx].y);
            TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, aViewNormals[outIdx].z);
        }
    }

    // The centre, the last extra vertex.
    const rdVector3 centre = { 0.0f, 2.0f, 0.0f };
    float sx, sy, sz, rhw;
    rdPolySmoothTest_Project(&centre, &proj, &sx, &sy, &sz, &rhw);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, sx, aOut[outIdx].sx);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, sy, aOut[outIdx].sy);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, sz, aOut[outIdx].sz);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, rhw, aOut[outIdx].rhw);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.5f, aOut[outIdx].tu);
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 0.5f, aOut[outIdx].tv);
    TEST_ASSERT_EQUAL_size_t(numExtra - 1u, outIdx);
}

TEST(rdPolySmooth, BuildVerticesProjectsFacesThatCrossTheCameraPlane)
{
    // A planar square in the y = z plane, from behind the camera to in front of it.
    const rdVector3 aView[4] = {
        { -0.5f, -0.5f, -0.5f }, { 0.5f, -0.5f, -0.5f }, { 0.5f, 0.5f, 0.5f }, { -0.5f, 0.5f, 0.5f },
    };
    const rdVector3 aNormals[4] = {
        { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f },
    };
    const rdVector3 aTexCoords[4] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };
    rdVector4 aCornerValues[4] = { { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } };

    size_t rings = rdPolySmooth_GetRingCount(aView, 4u);
    TEST_ASSERT_TRUE(rings > 0u);

    StdVertexTL aScreen[4];
    rdCacheProcEntry face;
    rdPolySmoothTest_SetupFace(&face, aView, aNormals, aScreen, aCornerValues, aTexCoords, 4u);

    const rdPolySmoothProjection proj = { 200.0f, 320.0f, 240.0f, 0.5f, 0.01f };
    StdVertexTL aOut[RDPOLYSMOOTH_MAXEXTRAVERTS];
    rdVector4 aOutValues[RDPOLYSMOOTH_MAXEXTRAVERTS];
    rdVector3 aViewVerts[RDPOLYSMOOTH_MAXEXTRAVERTS];
    rdVector3 aViewNormals[RDPOLYSMOOTH_MAXEXTRAVERTS];
    rdPolySmooth_BuildVertices(&face, rings, &proj, aCornerValues, aOut, aOutValues, aViewVerts, aViewNormals);

    size_t numExtra = rdPolySmooth_GetExtraVertexCount(4u, rings);
    for ( size_t i = 0; i < numExtra; ++i )
    {
        TEST_ASSERT_TRUE(isfinite(aOut[i].sx));
        TEST_ASSERT_TRUE(isfinite(aOut[i].sy));
        TEST_ASSERT_TRUE(isfinite(aOut[i].sz));
        TEST_ASSERT_TRUE(isfinite(aOut[i].rhw));

        // rhw has the sign of the view depth; a point on the plane uses the positive nudge.
        float y = aViewVerts[i].y;
        if ( fabsf(y) < 1e-5f )
        {
            y = y < 0.0f ? -1e-5f : 1e-5f;
        }
        TEST_ASSERT_EQUAL(y > 0.0f, aOut[i].rhw > 0.0f);
    }
}

TEST(rdPolySmooth, BuildVerticesIsSymmetricForBilinearIncompatibleCorners)
{
    // Values 1, 0, 1, 0: bilinear interpolation is symmetric across the x = z diagonal, and so is
    // this tessellation. The old zig-zag triangulation is not, because its diagonal picks a side.
    rdVector3 aView[4];
    rdPolySmoothTest_MakeSquare(aView, 0.5f);

    const rdVector3 aNormals[4] = {
        { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f },
    };
    const rdVector3 aTexCoords[4] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };
    const rdVector4 aCornerIntensities[4] = {
        { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 1.0f },
        { 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f, 1.0f },
    };

    StdVertexTL aScreen[4];
    rdCacheProcEntry face;
    rdPolySmoothTest_SetupFace(&face, aView, aNormals, aScreen, (rdVector4*)aCornerIntensities, aTexCoords, 4u);

    const rdPolySmoothProjection proj = { 200.0f, 320.0f, 240.0f, 0.5f, 0.01f };
    size_t rings = rdPolySmooth_GetRingCount(aView, 4u);

    StdVertexTL aOut[RDPOLYSMOOTH_MAXEXTRAVERTS];
    rdVector4 aIntensities[RDPOLYSMOOTH_MAXEXTRAVERTS];
    rdVector3 aViewVerts[RDPOLYSMOOTH_MAXEXTRAVERTS];
    rdVector3 aViewNormals[RDPOLYSMOOTH_MAXEXTRAVERTS];
    rdPolySmooth_BuildVertices(&face, rings, &proj, aCornerIntensities, aOut, aIntensities, aViewVerts, aViewNormals);

    // Corner 1 and corner 3 are mirror images across x = z; so are their ring points.
    for ( size_t r = 1; r < rings; ++r )
    {
        size_t idx1 = (r - 1u) * 4u + 1u;
        size_t idx3 = (r - 1u) * 4u + 3u;
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, aViewVerts[idx1].z, aViewVerts[idx3].x);
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, aViewVerts[idx1].x, aViewVerts[idx3].z);
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, aIntensities[idx1].red, aIntensities[idx3].red);
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, aIntensities[idx1].green, aIntensities[idx3].green);
        TEST_ASSERT_FLOAT_WITHIN(1e-5f, aIntensities[idx1].blue, aIntensities[idx3].blue);
    }
}

TEST(rdPolySmooth, BuildIndicesKeepWindingCoverThePolygonAndLeaveTheBoundaryWhole)
{
    rdVector3 aView[4];
    rdPolySmoothTest_MakeSquare(aView, 0.25f); // three rings
    size_t rings = rdPolySmooth_GetRingCount(aView, 4u);
    TEST_ASSERT_EQUAL_size_t(3u, rings);

    size_t numExtra = rdPolySmooth_GetExtraVertexCount(4u, rings);
    size_t numIndices = rdPolySmooth_GetIndexCount(4u, rings);

    const rdVector3 aNormals[4] = {
        { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f },
    };
    const rdVector3 aTexCoords[4] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };
    rdVector4 aCornerIntensities[4] = { { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } };

    const rdPolySmoothProjection proj = { 200.0f, 320.0f, 240.0f, 0.5f, 0.01f };
    StdVertexTL aScreen[4];
    rdCacheProcEntry face;
    rdPolySmoothTest_SetupFace(&face, aView, aNormals, aScreen, aCornerIntensities, aTexCoords, 4u);

    StdVertexTL aOut[RDPOLYSMOOTH_MAXEXTRAVERTS];
    rdVector4 aIntensities[RDPOLYSMOOTH_MAXEXTRAVERTS];
    rdVector3 aViewVerts[RDPOLYSMOOTH_MAXEXTRAVERTS];
    rdVector3 aViewNormals[RDPOLYSMOOTH_MAXEXTRAVERTS];
    rdPolySmooth_BuildVertices(&face, rings, &proj, aCornerIntensities, aOut, aIntensities, aViewVerts, aViewNormals);

    // The corner screen vertices are the projected view corners, so the areas are in screen space.
    for ( size_t k = 0; k < 4u; ++k )
    {
        rdPolySmoothTest_Project(&aView[k], &proj, &aScreen[k].sx, &aScreen[k].sy, &aScreen[k].sz, &aScreen[k].rhw);
    }

    const uint16_t baseIndex = 10u;
    uint16_t aIndices[3u * RDPOLYSMOOTH_MAXEXTRAVERTS];
    memset(aIndices, 0, sizeof(aIndices));
    rdPolySmooth_BuildIndices(4u, rings, baseIndex, aIndices);
    TEST_ASSERT_EQUAL_size_t(numIndices, rdPolySmooth_GetIndexCount(4u, rings));

    // All screen positions: the corners, then the extra vertices in the promised order.
    StdVertexTL aVerts[4u + RDPOLYSMOOTH_MAXEXTRAVERTS];
    for ( size_t k = 0; k < 4u; ++k )
    {
        aVerts[k] = aScreen[k];
    }
    for ( size_t i = 0; i < numExtra; ++i )
    {
        aVerts[4u + i] = aOut[i];
    }

    float polygonArea = 0.0f;
    for ( size_t k = 0; k < 4u; ++k )
    {
        size_t k1 = (k + 1u) % 4u;
        polygonArea += aVerts[k].sx * aVerts[k1].sy - aVerts[k1].sx * aVerts[k].sy;
    }
    TEST_ASSERT_TRUE(fabsf(polygonArea) > 1e-6f);
    float polygonSign = polygonArea > 0.0f ? 1.0f : -1.0f;

    float triangleArea = 0.0f;
    for ( size_t i = 0; i < numIndices; i += 3u )
    {
        for ( size_t j = 0; j < 3u; ++j )
        {
            TEST_ASSERT_TRUE(aIndices[i + j] >= baseIndex);
            TEST_ASSERT_TRUE(aIndices[i + j] < baseIndex + 4u + numExtra);
        }

        const StdVertexTL* p0 = &aVerts[aIndices[i] - baseIndex];
        const StdVertexTL* p1 = &aVerts[aIndices[i + 1u] - baseIndex];
        const StdVertexTL* p2 = &aVerts[aIndices[i + 2u] - baseIndex];
        float area = (p1->sx - p0->sx) * (p2->sy - p0->sy) - (p2->sx - p0->sx) * (p1->sy - p0->sy);
        TEST_ASSERT_TRUE(area * polygonSign > 0.0f);
        triangleArea += area;
    }

    TEST_ASSERT_FLOAT_WITHIN(1e-3f * fabsf(polygonArea), polygonArea, triangleArea);

    // Every boundary edge is exactly one triangle edge: no extra vertex splits it.
    for ( size_t k = 0; k < 4u; ++k )
    {
        uint16_t a = (uint16_t)(baseIndex + k);
        uint16_t b = (uint16_t)(baseIndex + (k + 1u) % 4u);
        size_t count = 0;
        for ( size_t i = 0; i < numIndices; i += 3u )
        {
            for ( size_t e = 0; e < 3u; ++e )
            {
                uint16_t v0 = aIndices[i + e];
                uint16_t v1 = aIndices[i + (e + 1u) % 3u];
                if ( (v0 == a && v1 == b) || (v0 == b && v1 == a) )
                {
                    ++count;
                }
            }
        }
        TEST_ASSERT_EQUAL_size_t(1u, count);
    }
}

TEST_GROUP_RUNNER(rdPolySmooth)
{
    RUN_TEST_CASE(rdPolySmooth, RingCountFollowsTheViewSpaceSize);
    RUN_TEST_CASE(rdPolySmooth, ExtraVertexAndIndexCounts);
    RUN_TEST_CASE(rdPolySmooth, MeanValueWeightsArePartitionOfUnityAndReproduceLinearFunctions);
    RUN_TEST_CASE(rdPolySmooth, BuildVerticesProjectsInterpolatesAndSmoothsTheCorners);
    RUN_TEST_CASE(rdPolySmooth, BuildVerticesProjectsFacesThatCrossTheCameraPlane);
    RUN_TEST_CASE(rdPolySmooth, BuildVerticesIsSymmetricForBilinearIncompatibleCorners);
    RUN_TEST_CASE(rdPolySmooth, BuildIndicesKeepWindingCoverThePolygonAndLeaveTheBoundaryWhole);
}
