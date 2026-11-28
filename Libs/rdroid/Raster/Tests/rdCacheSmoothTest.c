// Added: tests of rdCache's smooth Gouraud light, the ring tessellation that replaces a lit world
// face's zig-zag triangulation with interior vertices. The extra vertices blend the corners'
// final clamped colors, so a bright corner stays bright instead of being washed out by a clamp
// after the interpolation. The draws are read back from the same in-memory render capture the
// other rdCache tests use.
#include <unity_fixture.h>

#include "rdCacheTestSupport.h"

#include <j3dcore/Tests/j3dTest.h>
#include <rdroid/Math/Tests/rdMathTestHost.h>

#include <rdroid/Main/rdroid.h>
#include <rdroid/Raster/rdCache.h>

#include <std/General/std3D.h>
#include <std/General/stdRenderCapture.h>
#include <std/General/stdUtil.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

static rdGeometryMode rdCacheTest_savedGeometryMode;
static rdLightMode rdCacheTest_savedLightingMode;
static size_t rdCacheTest_savedMaxVertices;

TEST_GROUP(rdCacheSmooth);

TEST_SETUP(rdCacheSmooth)
{
    rdCacheTest_InitHost();

    rdCacheTest_savedGeometryMode = rdroid_g_curGeometryMode;
    rdCacheTest_savedLightingMode = rdroid_g_curLightingMode;
    rdroid_g_curGeometryMode      = RD_GEOMETRY_FULL;
    rdroid_g_curLightingMode      = RD_LIGHTING_GOURAUD;

    // An open std3D's vertex limit, so faces are batched as in the game (std3D isn't opened here).
    rdCacheTest_savedMaxVertices = std3D_g_maxVertices;
    std3D_g_maxVertices          = 65535u;

    rdCacheTest_ResetRender();
}

TEST_TEAR_DOWN(rdCacheSmooth)
{
    rdCacheTest_RestoreModelCamera();
    rdSetSmoothVertexLight(false);
    stdRenderCapture_ResetGlobals();
    rdCache_AdvanceFrame();
    rdroid_g_curGeometryMode = rdCacheTest_savedGeometryMode;
    rdroid_g_curLightingMode = rdCacheTest_savedLightingMode;
    std3D_g_maxVertices      = rdCacheTest_savedMaxVertices;
    J3DTest_Shutdown();
}

// Added: a lit quad of side 0.5 in view space, in front of the camera; five rings and 17 extra
// vertices. The corner colors follow rdCache's usual Gouraud rules.
static const rdVector3 rdCacheTest_aSmoothViewVerts[4] = {
    { -0.25f, 2.0f, -0.25f }, { 0.25f, 2.0f, -0.25f }, { 0.25f, 2.0f, 0.25f }, { -0.25f, 2.0f, 0.25f },
};

static void rdCacheTest_FillSmoothQuad(rdCacheProcEntry* pEntry, rdLightMode lightingMode)
{
    pEntry->flags        = RD_FF_TEX_FILTER_NEAREST;
    pEntry->lightingMode = lightingMode;
    pEntry->pMaterial    = NULL;
    pEntry->matCelNum    = -1;
    pEntry->distance     = 0.0f;
    pEntry->extraLight   = (rdVector4){ 0.25f, 0.0f, 0.5f, 1.0f };
    pEntry->numVertices  = 4u;
    for ( size_t i = 0; i < 4u; ++i )
    {
        pEntry->aVertices[i]        = rdCacheTest_ScreenVertex(0, i);
        pEntry->aVertIntensities[i] = rdCacheTest_aIntensities[i % 3u];
        if ( pEntry->aViewVertices )
        {
            pEntry->aViewVertices[i] = rdCacheTest_aSmoothViewVerts[i];
            pEntry->aViewNormals[i]  = (rdVector3){ 0.0f, 0.0f, 1.0f };
        }
    }
}

static void rdCacheTest_FlushSmoothQuad(bool bSmooth, bool bLit, rdLightMode lightingMode)
{
    rdSetSmoothVertexLight(bSmooth);
    rdCacheProcEntry* pEntry = bLit ? rdCache_GetLitProcEntry() : rdCache_GetProcEntry();
    TEST_ASSERT_NOT_NULL(pEntry);
    rdCacheTest_FillSmoothQuad(pEntry, lightingMode);
    rdCache_AddProcFace(4u);

    TEST_ASSERT_TRUE(stdRenderCapture_Open("rdCacheTest.rcap", "null"));
    rdCache_Flush();
    stdRenderCapture_Close();
    rdCacheTest_ParseDraws();
    TEST_ASSERT_EQUAL_size_t(1u, rdCacheTest_numDraws);
}

TEST(rdCacheSmooth, SmoothLightExpandsLitGouraudFaces)
{
    rdCacheTest_SetupModelCamera();

    // Off: the old four vertices and zig-zag, the reference for the corners.
    rdCacheTest_FlushSmoothQuad(false, true, RD_LIGHTING_GOURAUD);
    const RdCacheTestDraw classic = rdCacheTest_aDraws[0];
    TEST_ASSERT_EQUAL_size_t(4u, classic.numVerts);
    TEST_ASSERT_EQUAL_size_t(6u, classic.numIndices);

    // On: five rings add 17 vertices and 3 * 4 * (2 * 5 - 1) indices.
    rdCacheTest_FlushSmoothQuad(true, true, RD_LIGHTING_GOURAUD);
    const RdCacheTestDraw* pDraw = &rdCacheTest_aDraws[0];
    TEST_ASSERT_EQUAL_UINT16(RDCACHETEST_ID_DRAWLIT, pDraw->id);
    TEST_ASSERT_EQUAL_size_t(21u, pDraw->numVerts);
    TEST_ASSERT_EQUAL_size_t(108u, pDraw->numIndices);
    for ( size_t i = 0; i < pDraw->numIndices; ++i )
    {
        TEST_ASSERT_TRUE(pDraw->aIndices[i] < pDraw->numVerts);
    }

    // The corners keep their old colors and lit attributes.
    for ( size_t i = 0; i < 4u; ++i )
    {
        TEST_ASSERT_EQUAL_MEMORY(&classic.aVerts[i], &pDraw->aVerts[i], sizeof(StdVertexLit));
    }

    rdSetSmoothVertexLight(false);
}

TEST(rdCacheSmooth, SmoothLightLeavesClassicAndLitModeFacesUnchanged)
{
    rdCacheTest_SetupModelCamera();

    const uint16_t aQuadIndices[6] = { 0u, 1u, 3u, 1u, 2u, 3u };

    // A classic face is not smoothed even with the option on.
    rdCacheTest_FlushSmoothQuad(true, false, RD_LIGHTING_GOURAUD);
    TEST_ASSERT_EQUAL_UINT16(RDCACHETEST_ID_DRAW, rdCacheTest_aDraws[0].id);
    TEST_ASSERT_EQUAL_size_t(4u, rdCacheTest_aDraws[0].numVerts);
    TEST_ASSERT_EQUAL_size_t(6u, rdCacheTest_aDraws[0].numIndices);
    TEST_ASSERT_EQUAL_UINT16_ARRAY(aQuadIndices, rdCacheTest_aDraws[0].aIndices, 6u);

    // A lit face whose resolved mode is LIT is not Gouraud, so it keeps its old fan.
    rdCacheTest_FlushSmoothQuad(true, true, RD_LIGHTING_LIT);
    TEST_ASSERT_EQUAL_UINT16(RDCACHETEST_ID_DRAWLIT, rdCacheTest_aDraws[0].id);
    TEST_ASSERT_EQUAL_size_t(4u, rdCacheTest_aDraws[0].numVerts);
    TEST_ASSERT_EQUAL_size_t(6u, rdCacheTest_aDraws[0].numIndices);
    TEST_ASSERT_EQUAL_UINT16_ARRAY(aQuadIndices, rdCacheTest_aDraws[0].aIndices, 6u);

    rdSetSmoothVertexLight(false);
}

TEST(rdCacheSmooth, SmoothLightBlendsClampedCornerColors)
{
    rdCacheTest_SetupModelCamera();
    rdSetSmoothVertexLight(true);

    rdCacheProcEntry* pEntry = rdCache_GetLitProcEntry();
    TEST_ASSERT_NOT_NULL(pEntry);
    rdCacheTest_FillSmoothQuad(pEntry, RD_LIGHTING_GOURAUD);
    pEntry->extraLight = (rdVector4){ 0.9f, 0.9f, 0.9f, 1.0f };
    for ( size_t i = 0; i < 4u; ++i )
    {
        // Two corners clamp to 1 (0.9 + 0.9), two stay at 0.9.
        pEntry->aVertIntensities[i] = (i % 2u) == 0u
            ? (rdVector4){ 0.9f, 0.9f, 0.9f, 1.0f }
            : (rdVector4){ 0.0f, 0.0f, 0.0f, 1.0f };
    }
    rdCache_AddProcFace(4u);

    TEST_ASSERT_TRUE(stdRenderCapture_Open("rdCacheTest.rcap", "null"));
    rdCache_Flush();
    stdRenderCapture_Close();
    rdCacheTest_ParseDraws();
    TEST_ASSERT_EQUAL_size_t(1u, rdCacheTest_numDraws);

    const RdCacheTestDraw* pDraw = &rdCacheTest_aDraws[0];
    TEST_ASSERT_EQUAL_size_t(21u, pDraw->numVerts);

    // The two odd corners are 0.9 (229 after truncation), the two even ones clamp to 1 (255).
    uint32_t brightest = 0u;
    for ( size_t i = 0; i < 4u; ++i )
    {
        const uint32_t color = pDraw->aVerts[i].tl.color;
        const uint32_t red   = (color >> 16) & 0xFFu;
        const uint32_t green = (color >> 8) & 0xFFu;
        const uint32_t blue  = color & 0xFFu;
        const uint32_t expected = (i % 2u) == 0u ? 255u : 229u;
        TEST_ASSERT_EQUAL_UINT32(expected, red);
        TEST_ASSERT_EQUAL_UINT32(expected, green);
        TEST_ASSERT_EQUAL_UINT32(expected, blue);
        brightest = J3DMAX(brightest, J3DMAX(red, J3DMAX(green, blue)));
    }

    // No extra vertex is brighter than the brightest corner.
    for ( size_t i = 4u; i < pDraw->numVerts; ++i )
    {
        const uint32_t color = pDraw->aVerts[i].tl.color;
        TEST_ASSERT_TRUE(((color >> 16) & 0xFFu) <= brightest);
        TEST_ASSERT_TRUE(((color >> 8) & 0xFFu) <= brightest);
        TEST_ASSERT_TRUE((color & 0xFFu) <= brightest);
    }

    // The centre is the mean value blend of the CLAMPED corner colors: (1 + 0.9 + 1 + 0.9) / 4.
    const StdVertexTL* pCentre = &pDraw->aVerts[pDraw->numVerts - 1u].tl;
    const uint32_t expected = (uint32_t)(0.95f * 255.0f);
    TEST_ASSERT_UINT32_WITHIN(1u, expected, (pCentre->color >> 16) & 0xFFu);
    TEST_ASSERT_UINT32_WITHIN(1u, expected, (pCentre->color >> 8) & 0xFFu);
    TEST_ASSERT_UINT32_WITHIN(1u, expected, pCentre->color & 0xFFu);

    rdSetSmoothVertexLight(false);
}

TEST_GROUP_RUNNER(rdCacheSmooth)
{
    RUN_TEST_CASE(rdCacheSmooth, SmoothLightExpandsLitGouraudFaces);
    RUN_TEST_CASE(rdCacheSmooth, SmoothLightLeavesClassicAndLitModeFacesUnchanged);
    RUN_TEST_CASE(rdCacheSmooth, SmoothLightBlendsClampedCornerColors);
}
