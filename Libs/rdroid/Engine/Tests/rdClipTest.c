// Added: tests of rdClip_FaceToPlaneLit (option A L1 and L2, ADR-0023), the rdClip_FaceToPlane of the
// lit path that world surfaces and models will take (sithRender.c, rdModel3.c). Each face goes through
// the lit function into a lit entry and through rdClip_FaceToPlane into a classic one: the screen
// vertices and intensities must be the same bytes, and the view-space positions and normals must be
// the inputs, the normals renormalized, as worked out here by hand. rdClip_FaceToPlane culls faces
// that lie wholly outside one frustum plane and clips none (the GPU clips), so the cases are: all in,
// vertices on the planes, faces crossing planes (kept whole, nothing interpolated) and all out.
#include <unity_fixture.h>

#include <j3dcore/Tests/j3dTest.h>
#include <rdroid/Math/Tests/rdMathTestHost.h>

#include <rdroid/Engine/rdCamera.h>
#include <rdroid/Engine/rdClip.h>

#include <stddef.h>
#include <string.h>

#define RDCLIPTEST_MAXVERTS 8

// The camera rdClip_FaceToPlane projects with: canvas center (320, 240), focal length 100, aspect 1.
static rdCanvas rdClipTest_canvas;
static rdCamera rdClipTest_camera;
static rdCamera* rdClipTest_pSavedCamera;

// The frustum: near 1, far 100, the side planes at 45 degrees (x and z within +-y).
static rdClipFrustum rdClipTest_frustum;

// A world of six view-space vertices, their texture vertices and per-vertex values.
static const rdVector3 rdClipTest_aVerts[6] = {
    {  2.0f, 10.0f,  1.0f },
    { -4.0f, 20.0f,  2.0f },
    {  1.0f,  5.0f, -1.0f },
    {  0.0f,  8.0f,  4.0f },
    {  3.0f, 12.0f, -2.0f },
    { -1.0f,  4.0f,  0.5f },
};

static const rdVector2 rdClipTest_aTexVerts[4] = {
    { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f },
};

static const rdVector4 rdClipTest_aLightColors[6] = {
    { 0.1f, 0.2f, 0.3f, 1.0f }, { 0.4f, 0.5f, 0.6f, 1.0f }, { 0.7f, 0.8f, 0.9f, 1.0f },
    { 0.2f, 0.2f, 0.2f, 0.5f }, { 0.6f, 0.4f, 0.2f, 1.0f }, { 0.9f, 0.9f, 0.9f, 1.0f },
};

static const rdVector4 rdClipTest_aVertColors[3] = {
    { 0.5f, 0.5f, 0.5f, 0.0f }, { 0.25f, 0.25f, 0.25f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f },
};

// Per-vertex view-space normals, none of them unit length; vertex 5 has none (zero).
static const rdVector3 rdClipTest_aNormals[6] = {
    {  0.0f, -2.0f, 0.0f },
    {  3.0f,  0.0f, 4.0f },
    {  0.0f,  0.0f, 5.0f },
    {  0.0f, -3.0f, 4.0f },
    { -6.0f,  8.0f, 0.0f },
    {  0.0f,  0.0f, 0.0f },
};

// An entry with its own arrays: lit (with view arrays) or classic (without).
typedef struct sRdClipTestEntry
{
    rdCacheProcEntry entry;
    StdVertexTL aVertices[RDCLIPTEST_MAXVERTS];
    rdVector4 aIntensities[RDCLIPTEST_MAXVERTS];
    rdVector3 aViewVertices[RDCLIPTEST_MAXVERTS];
    rdVector3 aViewNormals[RDCLIPTEST_MAXVERTS];
} RdClipTestEntry;

static RdClipTestEntry rdClipTest_lit;
static RdClipTestEntry rdClipTest_classic;

static void rdClipTest_InitEntry(RdClipTestEntry* pEntry, bool bLit)
{
    // A pattern, so a test sees what was written and what was left.
    memset(pEntry, 0xA5, sizeof(*pEntry));
    memset(&pEntry->entry, 0, sizeof(pEntry->entry));
    pEntry->entry.aVertices        = pEntry->aVertices;
    pEntry->entry.aVertIntensities = pEntry->aIntensities;
    pEntry->entry.aViewVertices    = bLit ? pEntry->aViewVertices : NULL;
    pEntry->entry.aViewNormals     = bLit ? pEntry->aViewNormals : NULL;
}

static rdFace rdClipTest_face;
static int rdClipTest_aFaceVerts[RDCLIPTEST_MAXVERTS];
static int rdClipTest_aFaceTexVerts[RDCLIPTEST_MAXVERTS];

static const rdFace* rdClipTest_Face(const int* aVertIdxs, size_t numVertices)
{
    memset(&rdClipTest_face, 0, sizeof(rdClipTest_face));
    for ( size_t i = 0; i < numVertices; ++i )
    {
        rdClipTest_aFaceVerts[i]    = aVertIdxs[i];
        rdClipTest_aFaceTexVerts[i] = (int)(i % 4u);
    }

    rdClipTest_face.numVertices     = numVertices;
    rdClipTest_face.aVertices       = rdClipTest_aFaceVerts;
    rdClipTest_face.aTexVertices    = rdClipTest_aFaceTexVerts;
    rdClipTest_face.texVertOffset.x = 0.5f;
    rdClipTest_face.texVertOffset.y = -0.25f;
    return &rdClipTest_face;
}

// Both functions on the same face; the screen vertices and intensities are the same bytes and the
// results agree. Returns the result.
static int rdClipTest_Run(const rdVector3* aVerts, const int* aVertIdxs, size_t numVertices, const rdVector4* aLightColors,
    const rdVector4* aVertColors, const rdVector3* aNormals, const rdVector3* pFaceNormal)
{
    rdClipTest_InitEntry(&rdClipTest_lit, true);
    rdClipTest_InitEntry(&rdClipTest_classic, false);

    const rdFace* pFace = rdClipTest_Face(aVertIdxs, numVertices);
    int litResult       = rdClip_FaceToPlaneLit(&rdClipTest_frustum, &rdClipTest_lit.entry, pFace, aVerts, rdClipTest_aTexVerts, aLightColors, aVertColors, aNormals, pFaceNormal);
    int classicResult   = rdClip_FaceToPlane(&rdClipTest_frustum, &rdClipTest_classic.entry, pFace, aVerts, rdClipTest_aTexVerts, aLightColors, aVertColors);

    TEST_ASSERT_EQUAL_INT(classicResult, litResult);
    TEST_ASSERT_EQUAL_MEMORY(rdClipTest_classic.aVertices, rdClipTest_lit.aVertices, sizeof(rdClipTest_lit.aVertices));
    TEST_ASSERT_EQUAL_MEMORY(rdClipTest_classic.aIntensities, rdClipTest_lit.aIntensities, sizeof(rdClipTest_lit.aIntensities));
    return litResult;
}

// Vertex i of the lit entry has the view position *pPos and the normal (x, y, z), and the entries
// after the face's last vertex are untouched.
static void rdClipTest_AssertLitVertex(size_t i, const rdVector3* pPos, float x, float y, float z)
{
    TEST_ASSERT_EQUAL_MEMORY(pPos, &rdClipTest_lit.aViewVertices[i], sizeof(rdVector3));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, x, rdClipTest_lit.aViewNormals[i].x);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, y, rdClipTest_lit.aViewNormals[i].y);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, z, rdClipTest_lit.aViewNormals[i].z);
}

static void rdClipTest_AssertUntouchedFrom(size_t first)
{
    RdClipTestEntry pattern;
    memset(&pattern, 0xA5, sizeof(pattern));
    for ( size_t i = first; i < RDCLIPTEST_MAXVERTS; ++i )
    {
        TEST_ASSERT_EQUAL_MEMORY(&pattern.aViewVertices[i], &rdClipTest_lit.aViewVertices[i], sizeof(rdVector3));
        TEST_ASSERT_EQUAL_MEMORY(&pattern.aViewNormals[i], &rdClipTest_lit.aViewNormals[i], sizeof(rdVector3));
    }
}

TEST_GROUP(rdClip);

TEST_SETUP(rdClip)
{
    J3DTest_Startup();
    J3DTest_BindHostServicesGlobal(rdroid_g_pHS_ADDR);

    memset(&rdClipTest_canvas, 0, sizeof(rdClipTest_canvas));
    rdClipTest_canvas.center.x = 320.0f;
    rdClipTest_canvas.center.y = 240.0f;

    memset(&rdClipTest_camera, 0, sizeof(rdClipTest_camera));
    rdClipTest_camera.pCanvas          = &rdClipTest_canvas;
    rdClipTest_camera.focalLength      = 100.0f;
    rdClipTest_camera.aspectRatio      = 1.0f;
    rdClipTest_camera.invNearClipPlane = 1.0f;
    rdClipTest_camera.invFarClipPlane  = 0.01f;
    rdClipTest_camera.pFrustum         = &rdClipTest_frustum;

    memset(&rdClipTest_frustum, 0, sizeof(rdClipTest_frustum));
    rdClipTest_frustum.bClipFar    = 1;
    rdClipTest_frustum.nearPlane   = 1.0f;
    rdClipTest_frustum.farPlane    = 100.0f;
    rdClipTest_frustum.leftPlane   = -1.0f;
    rdClipTest_frustum.rightPlane  = 1.0f;
    rdClipTest_frustum.topPlane    = 1.0f;
    rdClipTest_frustum.bottomPlane = -1.0f;

    rdClipTest_pSavedCamera = rdCamera_g_pCurCamera;
    rdCamera_g_pCurCamera   = &rdClipTest_camera;
}

TEST_TEAR_DOWN(rdClip)
{
    rdCamera_g_pCurCamera = rdClipTest_pSavedCamera;
    J3DTest_Shutdown();
}

TEST(rdClip, AFaceInsideKeepsItsPositionsAndRenormalizedVertexNormals)
{
    const int aIdxs[3] = { 0, 1, 2 };
    TEST_ASSERT_EQUAL_INT(1, rdClipTest_Run(rdClipTest_aVerts, aIdxs, 3u, rdClipTest_aLightColors, rdClipTest_aVertColors, rdClipTest_aNormals, NULL));

    // The projection, worked out: vertex 0 (2, 10, 1) is at 320 + 2 * 100 / 10 and 240 - 1 * 100 / 10.
    TEST_ASSERT_EQUAL_FLOAT(340.0f, rdClipTest_lit.aVertices[0].sx);
    TEST_ASSERT_EQUAL_FLOAT(230.0f, rdClipTest_lit.aVertices[0].sy);
    TEST_ASSERT_EQUAL_FLOAT(0.1f / 32.0f, rdClipTest_lit.aVertices[0].rhw);

    // (0, -2, 0), (3, 0, 4) and (0, 0, 5) as unit vectors.
    rdClipTest_AssertLitVertex(0, &rdClipTest_aVerts[0], 0.0f, -1.0f, 0.0f);
    rdClipTest_AssertLitVertex(1, &rdClipTest_aVerts[1], 0.6f, 0.0f, 0.8f);
    rdClipTest_AssertLitVertex(2, &rdClipTest_aVerts[2], 0.0f, 0.0f, 1.0f);
    rdClipTest_AssertUntouchedFrom(3);
}

TEST(rdClip, VertexNormalsFollowTheFaceVertexIndices)
{
    // A quad of vertices 4, 3, 5, 0 in that order, without light colors.
    const int aIdxs[4] = { 4, 3, 5, 0 };
    TEST_ASSERT_EQUAL_INT(1, rdClipTest_Run(rdClipTest_aVerts, aIdxs, 4u, NULL, NULL, rdClipTest_aNormals, NULL));

    rdClipTest_AssertLitVertex(0, &rdClipTest_aVerts[4], -0.6f, 0.8f, 0.0f);
    rdClipTest_AssertLitVertex(1, &rdClipTest_aVerts[3], 0.0f, -0.6f, 0.8f);
    rdClipTest_AssertLitVertex(2, &rdClipTest_aVerts[5], 0.0f, 0.0f, 0.0f); // none known: stays zero
    rdClipTest_AssertLitVertex(3, &rdClipTest_aVerts[0], 0.0f, -1.0f, 0.0f);
    rdClipTest_AssertUntouchedFrom(4);
}

TEST(rdClip, TheFaceNormalGoesToEveryVertexWithoutVertexNormals)
{
    const int aIdxs[3] = { 2, 0, 1 };
    const rdVector3 faceNormal = { 0.0f, -3.0f, 4.0f };
    TEST_ASSERT_EQUAL_INT(1, rdClipTest_Run(rdClipTest_aVerts, aIdxs, 3u, rdClipTest_aLightColors, NULL, NULL, &faceNormal));

    rdClipTest_AssertLitVertex(0, &rdClipTest_aVerts[2], 0.0f, -0.6f, 0.8f);
    rdClipTest_AssertLitVertex(1, &rdClipTest_aVerts[0], 0.0f, -0.6f, 0.8f);
    rdClipTest_AssertLitVertex(2, &rdClipTest_aVerts[1], 0.0f, -0.6f, 0.8f);

    // Vertex normals win over the face normal; neither gives zero normals.
    TEST_ASSERT_EQUAL_INT(1, rdClipTest_Run(rdClipTest_aVerts, aIdxs, 3u, NULL, NULL, rdClipTest_aNormals, &faceNormal));
    rdClipTest_AssertLitVertex(0, &rdClipTest_aVerts[2], 0.0f, 0.0f, 1.0f);
    TEST_ASSERT_EQUAL_INT(1, rdClipTest_Run(rdClipTest_aVerts, aIdxs, 3u, NULL, NULL, NULL, NULL));
    rdClipTest_AssertLitVertex(0, &rdClipTest_aVerts[2], 0.0f, 0.0f, 0.0f);
    rdClipTest_AssertLitVertex(2, &rdClipTest_aVerts[1], 0.0f, 0.0f, 0.0f);
}

TEST(rdClip, VerticesOnThePlanesAreKept)
{
    // On the right, top and near planes: x = y, z = y, y = near.
    const rdVector3 aVerts[3] = { { 10.0f, 10.0f, 0.0f }, { 0.0f, 10.0f, 10.0f }, { 0.0f, 1.0f, 0.0f } };
    const rdVector3 aNormals[3] = { { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -2.0f }, { 0.0f, -0.5f, 0.0f } };
    const int aIdxs[3] = { 0, 1, 2 };
    TEST_ASSERT_EQUAL_INT(1, rdClipTest_Run(aVerts, aIdxs, 3u, NULL, NULL, aNormals, NULL));

    rdClipTest_AssertLitVertex(0, &aVerts[0], -1.0f, 0.0f, 0.0f);
    rdClipTest_AssertLitVertex(1, &aVerts[1], 0.0f, 0.0f, -1.0f);
    rdClipTest_AssertLitVertex(2, &aVerts[2], 0.0f, -1.0f, 0.0f);
    rdClipTest_AssertUntouchedFrom(3);
}

TEST(rdClip, AFaceCrossingPlanesIsKeptWholeWithoutInterpolation)
{
    // Vertex 0 behind the near plane, vertex 1 left of the left plane, vertex 2 inside; a second face
    // whose first vertex is outside the far and right planes. Nothing is clipped: each output vertex
    // is its input vertex, with its own normal, renormalized.
    const rdVector3 aVerts[4] = { { 0.0f, 0.5f, 0.0f }, { -20.0f, 10.0f, 0.0f }, { 5.0f, 10.0f, 0.0f }, { 300.0f, 200.0f, 0.0f } };
    const rdVector3 aNormals[4] = { { 0.0f, 0.0f, 3.0f }, { 4.0f, 3.0f, 0.0f }, { 0.0f, -7.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } };
    const int aIdxs[3] = { 0, 1, 2 };
    TEST_ASSERT_EQUAL_INT(1, rdClipTest_Run(aVerts, aIdxs, 3u, NULL, NULL, aNormals, NULL));

    rdClipTest_AssertLitVertex(0, &aVerts[0], 0.0f, 0.0f, 1.0f);
    rdClipTest_AssertLitVertex(1, &aVerts[1], 0.8f, 0.6f, 0.0f);
    rdClipTest_AssertLitVertex(2, &aVerts[2], 0.0f, -1.0f, 0.0f);
    rdClipTest_AssertUntouchedFrom(3);

    const int aFarIdxs[3] = { 3, 2, 1 };
    TEST_ASSERT_EQUAL_INT(1, rdClipTest_Run(aVerts, aFarIdxs, 3u, NULL, NULL, aNormals, NULL));
    const float invSqrt3 = 0.57735026f;
    rdClipTest_AssertLitVertex(0, &aVerts[3], invSqrt3, invSqrt3, invSqrt3);
    rdClipTest_AssertLitVertex(1, &aVerts[2], 0.0f, -1.0f, 0.0f);
    rdClipTest_AssertLitVertex(2, &aVerts[1], 0.8f, 0.6f, 0.0f);
    rdClipTest_AssertUntouchedFrom(3);
}

TEST(rdClip, AFaceOutsideIsCulledAndWritesNothing)
{
    // All right of the right plane; then all behind the near plane.
    const rdVector3 aVerts[6] = { { 20.0f, 10.0f, 0.0f }, { 30.0f, 10.0f, 0.0f }, { 25.0f, 10.0f, 5.0f },
        { 0.0f, 0.5f, 0.0f }, { 0.1f, 0.25f, 0.0f }, { 0.0f, -2.0f, 0.1f } };
    const int aRight[3] = { 0, 1, 2 };
    const int aBehind[3] = { 3, 4, 5 };
    TEST_ASSERT_EQUAL_INT(0, rdClipTest_Run(aVerts, aRight, 3u, NULL, NULL, rdClipTest_aNormals, NULL));
    rdClipTest_AssertUntouchedFrom(0);
    TEST_ASSERT_EQUAL_INT(0, rdClipTest_Run(aVerts, aBehind, 3u, NULL, NULL, rdClipTest_aNormals, NULL));
    rdClipTest_AssertUntouchedFrom(0);
}

TEST(rdClip, AClassicEntryGetsWhatFaceToPlaneWrites)
{
    // An entry of rdCache_GetProcEntry has no view arrays: the lit function writes it as the classic one.
    const int aIdxs[3] = { 0, 1, 2 };
    rdClipTest_InitEntry(&rdClipTest_lit, false);
    rdClipTest_InitEntry(&rdClipTest_classic, false);
    const rdFace* pFace = rdClipTest_Face(aIdxs, 3u);
    TEST_ASSERT_EQUAL_INT(1, rdClip_FaceToPlaneLit(&rdClipTest_frustum, &rdClipTest_lit.entry, pFace, rdClipTest_aVerts, rdClipTest_aTexVerts,
        rdClipTest_aLightColors, NULL, rdClipTest_aNormals, NULL));
    TEST_ASSERT_EQUAL_INT(1, rdClip_FaceToPlane(&rdClipTest_frustum, &rdClipTest_classic.entry, pFace, rdClipTest_aVerts, rdClipTest_aTexVerts,
        rdClipTest_aLightColors, NULL));
    const size_t arraysOffset = offsetof(RdClipTestEntry, aVertices);
    TEST_ASSERT_EQUAL_MEMORY((const uint8_t*)&rdClipTest_classic + arraysOffset, (const uint8_t*)&rdClipTest_lit + arraysOffset,
        sizeof(rdClipTest_lit) - arraysOffset);
}

TEST_GROUP_RUNNER(rdClip)
{
    RUN_TEST_CASE(rdClip, AFaceInsideKeepsItsPositionsAndRenormalizedVertexNormals);
    RUN_TEST_CASE(rdClip, VertexNormalsFollowTheFaceVertexIndices);
    RUN_TEST_CASE(rdClip, TheFaceNormalGoesToEveryVertexWithoutVertexNormals);
    RUN_TEST_CASE(rdClip, VerticesOnThePlanesAreKept);
    RUN_TEST_CASE(rdClip, AFaceCrossingPlanesIsKeptWholeWithoutInterpolation);
    RUN_TEST_CASE(rdClip, AFaceOutsideIsCulledAndWritesNothing);
    RUN_TEST_CASE(rdClip, AClassicEntryGetsWhatFaceToPlaneWrites);
}
