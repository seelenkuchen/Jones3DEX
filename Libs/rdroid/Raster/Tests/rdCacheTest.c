// Added: tests of rdCache's lit path (option A L1, ADR-0023): faces of rdCache_GetLitProcEntry are
// drawn with std3D_DrawRenderListLit, each vertex the StdVertexTL rdCache makes for the classic path
// plus its view-space position and normal; classic faces still go to std3D_DrawRenderList, and a draw
// never mixes the two. What rdCache hands to std3D is read from a render capture (RCAP.md), written to
// memory through the test's host file services; the expected vertices are worked out here from
// rdCache's color rules (Gouraud: extra light plus the vertex intensity, clamped, alpha 255 unless
// translucent) and its fan triangulation.
#include <unity_fixture.h>

#include <j3dcore/Tests/j3dTest.h>
#include <rdroid/Math/Tests/rdMathTestHost.h>

#include <rdroid/Engine/rdCamera.h>
#include <rdroid/Main/rdroid.h>
#include <rdroid/Primitives/rdModel3.h>
#include <rdroid/Raster/rdCache.h>

#include <std/General/std.h>
#include <std/General/std3D.h>
#include <std/General/stdRenderCapture.h>
#include <std/General/stdUtil.h>

#include <stdlib.h>
#include <string.h>

// RCAP record ids (stdRenderCaptureIds.h): DrawRenderList, DrawRenderListLit.
#define RDCACHETEST_ID_DRAW    14u
#define RDCACHETEST_ID_DRAWLIT 35u

// What every face here draws with: nearest filtering keeps the QOL filter choice out of the state.
#define RDCACHETEST_RDFLAGS (STD3D_RS_SUBPIXEL_CORRECTION | STD3D_RS_UNKNOWN_2 | STD3D_RS_UNKNOWN_1)

// The capture file, in memory.
static uint8_t rdCacheTest_aFile[16384];
static size_t rdCacheTest_fileSize;
static bool rdCacheTest_bFileOverflow;

static int J3DAPI rdCacheTest_Print(const char* pFormat, ...)
{
    (void)pFormat;
    return 0;
}

static void* J3DAPI rdCacheTest_Malloc(size_t size)
{
    return malloc(size);
}

static void J3DAPI rdCacheTest_Free(void* pData)
{
    free(pData);
}

static void* J3DAPI rdCacheTest_Realloc(void* pData, size_t size)
{
    return realloc(pData, size);
}

static tFileHandle J3DAPI rdCacheTest_FileOpen(const char* pFilename, const char* pMode)
{
    (void)pFilename;
    (void)pMode;
    rdCacheTest_fileSize      = 0;
    rdCacheTest_bFileOverflow = false;
    return (tFileHandle)1;
}

static int J3DAPI rdCacheTest_FileClose(tFileHandle fh)
{
    (void)fh;
    return 0;
}

static size_t J3DAPI rdCacheTest_FileWrite(tFileHandle fh, const void* pData, size_t size)
{
    (void)fh;
    if ( size > sizeof(rdCacheTest_aFile) - rdCacheTest_fileSize )
    {
        rdCacheTest_bFileOverflow = true;
        return 0;
    }

    memcpy(&rdCacheTest_aFile[rdCacheTest_fileSize], pData, size);
    rdCacheTest_fileSize += size;
    return size;
}

// One recorded draw: its record id, texture handle, render state, vertices (StdVertexTL records
// leave the position and normal zero) and indices.
typedef struct sRdCacheTestDraw
{
    uint16_t id;
    uint32_t texHandle;
    int32_t rdflags;
    size_t numVerts;
    StdVertexLit aVerts[16];
    size_t numIndices;
    uint16_t aIndices[48];
} RdCacheTestDraw;

static RdCacheTestDraw rdCacheTest_aDraws[8];
static size_t rdCacheTest_numDraws;

static uint32_t rdCacheTest_U32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static float rdCacheTest_F32(const uint8_t* p)
{
    uint32_t bits = rdCacheTest_U32(p);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

// Reads the draw records of the capture in memory into rdCacheTest_aDraws, checking each payload's
// size; every other record is skipped by its size.
static void rdCacheTest_ParseDraws(void)
{
    TEST_ASSERT_FALSE(rdCacheTest_bFileOverflow);
    TEST_ASSERT_TRUE(rdCacheTest_fileSize >= 20u);
    TEST_ASSERT_EQUAL_MEMORY("J3DRCAP", rdCacheTest_aFile, 8u);

    rdCacheTest_numDraws = 0;
    size_t pos = 16u + rdCacheTest_U32(&rdCacheTest_aFile[12]);
    while ( pos < rdCacheTest_fileSize )
    {
        TEST_ASSERT_TRUE(pos + 8u <= rdCacheTest_fileSize);
        const uint16_t id    = (uint16_t)(rdCacheTest_aFile[pos] | (rdCacheTest_aFile[pos + 1u] << 8));
        const uint32_t size  = rdCacheTest_U32(&rdCacheTest_aFile[pos + 4u]);
        const uint8_t* pData = &rdCacheTest_aFile[pos + 8u];
        TEST_ASSERT_TRUE(pos + 8u + size <= rdCacheTest_fileSize);
        pos += 8u + size;
        if ( id != RDCACHETEST_ID_DRAW && id != RDCACHETEST_ID_DRAWLIT )
        {
            continue;
        }

        TEST_ASSERT_TRUE(rdCacheTest_numDraws < STD_ARRAYLEN(rdCacheTest_aDraws));
        RdCacheTestDraw* pDraw = &rdCacheTest_aDraws[rdCacheTest_numDraws++];
        memset(pDraw, 0, sizeof(*pDraw));
        pDraw->id        = id;
        pDraw->texHandle = rdCacheTest_U32(pData);
        pDraw->rdflags   = (int32_t)rdCacheTest_U32(pData + 4u);
        pDraw->numVerts  = rdCacheTest_U32(pData + 8u);
        TEST_ASSERT_TRUE(pDraw->numVerts <= STD_ARRAYLEN(pDraw->aVerts));

        const size_t vertSize = id == RDCACHETEST_ID_DRAWLIT ? 56u : 32u;
        const uint8_t* pVert  = pData + 12u;
        for ( size_t i = 0; i < pDraw->numVerts; ++i, pVert += vertSize )
        {
            StdVertexLit* pOut = &pDraw->aVerts[i];
            pOut->tl.sx       = rdCacheTest_F32(pVert);
            pOut->tl.sy       = rdCacheTest_F32(pVert + 4u);
            pOut->tl.sz       = rdCacheTest_F32(pVert + 8u);
            pOut->tl.rhw      = rdCacheTest_F32(pVert + 12u);
            pOut->tl.color    = rdCacheTest_U32(pVert + 16u);
            pOut->tl.specular = rdCacheTest_U32(pVert + 20u);
            pOut->tl.tu       = rdCacheTest_F32(pVert + 24u);
            pOut->tl.tv       = rdCacheTest_F32(pVert + 28u);
            if ( id == RDCACHETEST_ID_DRAWLIT )
            {
                pOut->vx = rdCacheTest_F32(pVert + 32u);
                pOut->vy = rdCacheTest_F32(pVert + 36u);
                pOut->vz = rdCacheTest_F32(pVert + 40u);
                pOut->nx = rdCacheTest_F32(pVert + 44u);
                pOut->ny = rdCacheTest_F32(pVert + 48u);
                pOut->nz = rdCacheTest_F32(pVert + 52u);
            }
        }

        pDraw->numIndices = rdCacheTest_U32(pVert);
        TEST_ASSERT_TRUE(pDraw->numIndices <= STD_ARRAYLEN(pDraw->aIndices));
        for ( size_t i = 0; i < pDraw->numIndices; ++i )
        {
            pDraw->aIndices[i] = (uint16_t)(pVert[4u + 2u * i] | (pVert[5u + 2u * i] << 8));
        }

        TEST_ASSERT_EQUAL_size_t(12u + pDraw->numVerts * vertSize + 4u + 2u * pDraw->numIndices, size);
    }
}

// The faces the tests add: screen vertices i of face f, intensities, view positions and normals.
static StdVertexTL rdCacheTest_ScreenVertex(size_t face, size_t i)
{
    StdVertexTL vertex;
    vertex.sx       = 10.0f * (float)face + (float)i;
    vertex.sy       = 20.0f + (float)i;
    vertex.sz       = 0.5f;
    vertex.rhw      = 0.25f;
    vertex.color    = 0x12345678u; // replaced by rdCache
    vertex.specular = 0xFF00FF00u + (uint32_t)i;
    vertex.tu       = 0.5f * (float)i;
    vertex.tv       = 1.0f - 0.5f * (float)i;
    return vertex;
}

// Gouraud intensities over the extra light (0.25, 0, 0.5): the colors 0xFF7F7FBF, 0xFFFFFFFF and
// 0xFF3F007F (0.5, 0.5, 0.75; 1, 1, 1.25 clamped to 1; 0.25, 0, 0.5, each * 255 truncated).
static const rdVector4 rdCacheTest_extraLight = { 0.25f, 0.0f, 0.5f, 1.0f };
static const rdVector4 rdCacheTest_aIntensities[3] = {
    { 0.25f, 0.5f, 0.25f, 1.0f }, { 0.75f, 1.0f, 0.75f, 1.0f }, { 0.0f, 0.0f, 0.0f, 1.0f },
};
static const uint32_t rdCacheTest_aColors[3] = { 0xFF7F7FBFu, 0xFFFFFFFFu, 0xFF3F007Fu };

static rdVector3 rdCacheTest_ViewVertex(size_t face, size_t i)
{
    rdVector3 v = { (float)face - 1.5f, 10.0f + (float)i, 0.25f * (float)i };
    return v;
}

static rdVector3 rdCacheTest_ViewNormal(size_t face, size_t i)
{
    rdVector3 aNormals[3] = { { 0.0f, -1.0f, 0.0f }, { 0.6f, 0.0f, 0.8f }, { 0.0f, 0.0f, 1.0f } };
    rdVector3 n = aNormals[(face + i) % 3u];
    return n;
}

static void rdCacheTest_FillFace(rdCacheProcEntry* pEntry, size_t face, size_t numVerts)
{
    TEST_ASSERT_NOT_NULL(pEntry);
    pEntry->flags        = RD_FF_TEX_FILTER_NEAREST;
    pEntry->lightingMode = RD_LIGHTING_GOURAUD;
    pEntry->pMaterial    = NULL;
    pEntry->matCelNum    = -1;
    pEntry->extraLight   = rdCacheTest_extraLight;
    for ( size_t i = 0; i < numVerts; ++i )
    {
        pEntry->aVertices[i]        = rdCacheTest_ScreenVertex(face, i);
        pEntry->aVertIntensities[i] = rdCacheTest_aIntensities[i % 3u];
        if ( pEntry->aViewNormals )
        {
            pEntry->aViewVertices[i] = rdCacheTest_ViewVertex(face, i);
            pEntry->aViewNormals[i]  = rdCacheTest_ViewNormal(face, i);
        }
    }
}

// Draw d is a draw of face f with numVerts vertices and the indices, lit or classic, untextured.
static void rdCacheTest_AssertDraw(size_t d, bool bLit, size_t face, size_t numVerts, const uint16_t* aIndices, size_t numIndices)
{
    TEST_ASSERT_TRUE(d < rdCacheTest_numDraws);
    const RdCacheTestDraw* pDraw = &rdCacheTest_aDraws[d];
    TEST_ASSERT_EQUAL_UINT16(bLit ? RDCACHETEST_ID_DRAWLIT : RDCACHETEST_ID_DRAW, pDraw->id);
    TEST_ASSERT_EQUAL_UINT32(0u, pDraw->texHandle);
    TEST_ASSERT_EQUAL_INT32(RDCACHETEST_RDFLAGS, pDraw->rdflags);
    TEST_ASSERT_EQUAL_size_t(numVerts, pDraw->numVerts);
    for ( size_t i = 0; i < numVerts; ++i )
    {
        StdVertexLit expected;
        memset(&expected, 0, sizeof(expected));
        expected.tl       = rdCacheTest_ScreenVertex(face, i);
        expected.tl.color = rdCacheTest_aColors[i % 3u];
        if ( bLit )
        {
            const rdVector3 pos    = rdCacheTest_ViewVertex(face, i);
            const rdVector3 normal = rdCacheTest_ViewNormal(face, i);
            expected.vx = pos.x;
            expected.vy = pos.y;
            expected.vz = pos.z;
            expected.nx = normal.x;
            expected.ny = normal.y;
            expected.nz = normal.z;
        }
        TEST_ASSERT_EQUAL_MEMORY(&expected, &pDraw->aVerts[i], sizeof(expected));
    }

    TEST_ASSERT_EQUAL_size_t(numIndices, pDraw->numIndices);
    TEST_ASSERT_EQUAL_UINT16_ARRAY(aIndices, pDraw->aIndices, numIndices);
}

static rdGeometryMode rdCacheTest_savedGeometryMode;
static rdLightMode rdCacheTest_savedLightingMode;
static size_t rdCacheTest_savedMaxVertices;

TEST_GROUP(rdCache);

TEST_SETUP(rdCache)
{
    J3DTest_Startup();
    J3DTest_BindHostServicesGlobal(rdroid_g_pHS_ADDR);
    J3DTest_BindHostServicesGlobal((uintptr_t)&std_g_pHS);

    tHostServices* pHS = J3DTest_GetHostServices();
    pHS->pStatusPrint  = rdCacheTest_Print;
    pHS->pMessagePrint = rdCacheTest_Print;
    pHS->pWarningPrint = rdCacheTest_Print;
    pHS->pErrorPrint   = rdCacheTest_Print;
    pHS->pDebugPrint   = rdCacheTest_Print;
    pHS->pMalloc       = rdCacheTest_Malloc;
    pHS->pFree         = rdCacheTest_Free;
    pHS->pRealloc      = rdCacheTest_Realloc;
    pHS->pFileOpen     = rdCacheTest_FileOpen;
    pHS->pFileClose    = rdCacheTest_FileClose;
    pHS->pFileWrite    = rdCacheTest_FileWrite;

    rdCacheTest_savedGeometryMode = rdroid_g_curGeometryMode;
    rdCacheTest_savedLightingMode = rdroid_g_curLightingMode;
    rdroid_g_curGeometryMode      = RD_GEOMETRY_FULL;
    rdroid_g_curLightingMode      = RD_LIGHTING_GOURAUD;

    // An open std3D's vertex limit, so faces are batched as in the game (std3D isn't opened here).
    rdCacheTest_savedMaxVertices = std3D_g_maxVertices;
    std3D_g_maxVertices          = 65535u;

    stdRenderCapture_ResetGlobals();
    rdCache_Startup();
    rdCache_AdvanceFrame();
    rdCacheTest_fileSize = 0;
    rdCacheTest_numDraws = 0;
}

static void rdCacheTest_RestoreModelCamera(void);

TEST_TEAR_DOWN(rdCache)
{
    rdCacheTest_RestoreModelCamera();
    stdRenderCapture_ResetGlobals();
    rdCache_AdvanceFrame();
    rdroid_g_curGeometryMode = rdCacheTest_savedGeometryMode;
    rdroid_g_curLightingMode = rdCacheTest_savedLightingMode;
    std3D_g_maxVertices      = rdCacheTest_savedMaxVertices;
    J3DTest_Shutdown();
}

TEST(rdCache, LitFacesAreDrawnLitAndApartFromClassicOnes)
{
    // Lit, lit, classic quad, lit: the lit pair is one draw, the quad a classic one, the last lit one
    // a draw of its own.
    rdCacheProcEntry* pEntry = rdCache_GetLitProcEntry();
    TEST_ASSERT_NOT_NULL(pEntry->aViewVertices);
    TEST_ASSERT_NOT_NULL(pEntry->aViewNormals);
    rdCacheTest_FillFace(pEntry, 0, 3u);
    rdCache_AddProcFace(3u);
    rdCacheTest_FillFace(rdCache_GetLitProcEntry(), 1, 3u);
    rdCache_AddProcFace(3u);

    pEntry = rdCache_GetProcEntry();
    TEST_ASSERT_NULL(pEntry->aViewVertices);
    TEST_ASSERT_NULL(pEntry->aViewNormals);
    rdCacheTest_FillFace(pEntry, 2, 4u);
    rdCache_AddProcFace(4u);

    rdCacheTest_FillFace(rdCache_GetLitProcEntry(), 3, 3u);
    rdCache_AddProcFace(3u);

    TEST_ASSERT_TRUE(stdRenderCapture_Open("rdCacheTest.rcap", "null"));
    rdCache_Flush();
    stdRenderCapture_Close();
    rdCacheTest_ParseDraws();

    TEST_ASSERT_EQUAL_size_t(3u, rdCacheTest_numDraws);

    // The pair: six vertices, face 1's after face 0's.
    const RdCacheTestDraw* pPair = &rdCacheTest_aDraws[0];
    TEST_ASSERT_EQUAL_UINT16(RDCACHETEST_ID_DRAWLIT, pPair->id);
    TEST_ASSERT_EQUAL_size_t(6u, pPair->numVerts);
    const uint16_t aPairIndices[6] = { 0u, 1u, 2u, 3u, 4u, 5u };
    TEST_ASSERT_EQUAL_size_t(6u, pPair->numIndices);
    TEST_ASSERT_EQUAL_UINT16_ARRAY(aPairIndices, pPair->aIndices, 6u);
    for ( size_t face = 0; face < 2u; ++face )
    {
        for ( size_t i = 0; i < 3u; ++i )
        {
            const StdVertexLit* pVert = &pPair->aVerts[face * 3u + i];
            const StdVertexTL tl      = rdCacheTest_ScreenVertex(face, i);
            const rdVector3 pos       = rdCacheTest_ViewVertex(face, i);
            const rdVector3 normal    = rdCacheTest_ViewNormal(face, i);
            TEST_ASSERT_EQUAL_HEX32(rdCacheTest_aColors[i], pVert->tl.color);
            TEST_ASSERT_EQUAL_FLOAT(tl.sx, pVert->tl.sx);
            TEST_ASSERT_EQUAL_HEX32(tl.specular, pVert->tl.specular);
            TEST_ASSERT_EQUAL_FLOAT(tl.tv, pVert->tl.tv);
            TEST_ASSERT_EQUAL_FLOAT(pos.x, pVert->vx);
            TEST_ASSERT_EQUAL_FLOAT(pos.y, pVert->vy);
            TEST_ASSERT_EQUAL_FLOAT(pos.z, pVert->vz);
            TEST_ASSERT_EQUAL_FLOAT(normal.x, pVert->nx);
            TEST_ASSERT_EQUAL_FLOAT(normal.y, pVert->ny);
            TEST_ASSERT_EQUAL_FLOAT(normal.z, pVert->nz);
        }
    }

    // The quad, classic and fanned as rdCache does: (0, 1, 3), (1, 2, 3).
    const uint16_t aQuadIndices[6] = { 0u, 1u, 3u, 1u, 2u, 3u };
    rdCacheTest_AssertDraw(1, false, 2, 4u, aQuadIndices, 6u);

    const uint16_t aTriangleIndices[3] = { 0u, 1u, 2u };
    rdCacheTest_AssertDraw(2, true, 3, 3u, aTriangleIndices, 3u);
}

TEST(rdCache, ClassicFacesStayOnTheClassicDraw)
{
    // Only classic faces: one DrawRenderList with both, as before the lit path.
    rdCacheTest_FillFace(rdCache_GetProcEntry(), 0, 3u);
    rdCache_AddProcFace(3u);
    rdCacheTest_FillFace(rdCache_GetProcEntry(), 1, 3u);
    rdCache_AddProcFace(3u);

    TEST_ASSERT_TRUE(stdRenderCapture_Open("rdCacheTest.rcap", "null"));
    rdCache_Flush();
    stdRenderCapture_Close();
    rdCacheTest_ParseDraws();

    TEST_ASSERT_EQUAL_size_t(1u, rdCacheTest_numDraws);
    const RdCacheTestDraw* pDraw = &rdCacheTest_aDraws[0];
    TEST_ASSERT_EQUAL_UINT16(RDCACHETEST_ID_DRAW, pDraw->id);
    TEST_ASSERT_EQUAL_size_t(6u, pDraw->numVerts);
    TEST_ASSERT_EQUAL_HEX32(rdCacheTest_aColors[2], pDraw->aVerts[5].tl.color);
    TEST_ASSERT_EQUAL_FLOAT(rdCacheTest_ScreenVertex(1, 2).sx, pDraw->aVerts[5].tl.sx);
}

TEST(rdCache, AnEntryLeftUnaddedDoesNotMakeTheNextOneLit)
{
    // A lit entry that is not added (its face was culled) gives its slot to the next entry, which is
    // classic when it comes from rdCache_GetProcEntry.
    rdCacheProcEntry* pLit = rdCache_GetLitProcEntry();
    rdCacheTest_FillFace(pLit, 0, 3u);
    rdCacheProcEntry* pClassic = rdCache_GetProcEntry();
    TEST_ASSERT_EQUAL_PTR(pLit, pClassic);
    TEST_ASSERT_NULL(pClassic->aViewVertices);
    TEST_ASSERT_NULL(pClassic->aViewNormals);
    rdCacheTest_FillFace(pClassic, 0, 3u);
    rdCache_AddProcFace(3u);

    TEST_ASSERT_TRUE(stdRenderCapture_Open("rdCacheTest.rcap", "null"));
    rdCache_Flush();
    stdRenderCapture_Close();
    rdCacheTest_ParseDraws();

    const uint16_t aIndices[3] = { 0u, 1u, 2u };
    TEST_ASSERT_EQUAL_size_t(1u, rdCacheTest_numDraws);
    rdCacheTest_AssertDraw(0, false, 0, 3u, aIndices, 3u);
}

TEST(rdCache, LitAlphaFacesAreDrawnLit)
{
    rdCacheProcEntry* pEntry = rdCache_GetLitAlphaProcEntry();
    TEST_ASSERT_NOT_NULL(pEntry->aViewNormals);
    rdCacheTest_FillFace(pEntry, 0, 3u);
    rdCache_AddAlphaProcFace(3u);

    pEntry = rdCache_GetAlphaProcEntry();
    TEST_ASSERT_NULL(pEntry->aViewVertices);
    TEST_ASSERT_NULL(pEntry->aViewNormals);

    TEST_ASSERT_TRUE(stdRenderCapture_Open("rdCacheTest.rcap", "null"));
    rdCache_FlushAlpha();
    stdRenderCapture_Close();
    rdCacheTest_ParseDraws();

    const uint16_t aIndices[3] = { 0u, 1u, 2u };
    TEST_ASSERT_EQUAL_size_t(1u, rdCacheTest_numDraws);
    rdCacheTest_AssertDraw(0, true, 0, 3u, aIndices, 3u);
}

// Added: models through rdCache (option A L3). A one-face mesh is drawn by rdModel3_Draw under a
// camera whose view matrix rotates (x, y, z) to (-z, y, x) and moves it 10 forward, with lit faces on
// and off. With them on, the face is a lit draw whose normals are the mesh's vertex normals (Gouraud)
// or its face normal (flat shaded) rotated the same way, negated for a back face; its StdVertexTL part
// is the classic draw's, byte for byte. With them off, every draw is classic.
// rdModel3.c links rdPuppet and rdThing, whose sprite, particle and polyline drawing reads sith's
// current world (a layering break of the recovered code). rdroidTests doesn't link sith, so the
// symbol is defined here; nothing these tests run reads it.
struct sSithWorld* sithWorld_g_pCurrentWorld = NULL;

static rdCanvas rdCacheTest_canvas;
static tVBuffer rdCacheTest_vbuffer;
static rdCamera rdCacheTest_camera;
static rdClipFrustum rdCacheTest_frustum;

// Model space. Under the camera the vertices are at (0, 10, 0), (0, 10, 1), (-1, 10, 0), (-1, 10, 1).
static const rdVector3 rdCacheTest_aModelVerts[4] = {
    { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 1.0f },
};

// Rotated: (0, -1, 0), (0, -0.8, 0.6), (-0.6, -0.8, 0), (0, -1, 0).
static const rdVector3 rdCacheTest_aModelNormals[4] = {
    { 0.0f, -1.0f, 0.0f }, { 0.6f, -0.8f, 0.0f }, { 0.0f, -0.8f, 0.6f }, { 0.0f, -1.0f, 0.0f },
};

static const rdVector2 rdCacheTest_aModelTexVerts[4] = {
    { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f },
};

static const rdVector4 rdCacheTest_aModelColors[4] = {
    { 0.25f, 0.5f, 0.75f, 1.0f }, { 0.5f, 0.5f, 0.5f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f, 1.0f },
};

static rdVector4 rdCacheTest_aModelIntensities[4];
static int rdCacheTest_aModelFaceVerts[3];
static int rdCacheTest_aModelFaceTexVerts[3] = { 0, 1, 2 };
static rdFace rdCacheTest_modelFace;
static rdModel3Mesh rdCacheTest_mesh;
static rdModel3HNode rdCacheTest_node;
static rdModel3 rdCacheTest_model;
static rdMatrix34 rdCacheTest_jointMatrix;
static uint32_t rdCacheTest_jointAmputation;
static rdThing rdCacheTest_thing;

static bool rdCacheTest_bModelCamera; // set until the tear down restores what the setup replaced
static rdCamera* rdCacheTest_pSavedCamera;
static rdMatrix34 rdCacheTest_savedCamMatrix;
static rdRoidFlags rdCacheTest_savedRenderOptions;
static bool rdCacheTest_bSavedLitFaces;

static void rdCacheTest_SetupModelCamera(void)
{
    memset(&rdCacheTest_vbuffer, 0, sizeof(rdCacheTest_vbuffer));
    rdCacheTest_vbuffer.rasterInfo.colorInfo.bpp = 32;

    memset(&rdCacheTest_canvas, 0, sizeof(rdCacheTest_canvas));
    rdCacheTest_canvas.pVBuffer = &rdCacheTest_vbuffer;
    rdCacheTest_canvas.center.x = 320.0f;
    rdCacheTest_canvas.center.y = 240.0f;

    memset(&rdCacheTest_frustum, 0, sizeof(rdCacheTest_frustum));
    rdCacheTest_frustum.bClipFar    = 1;
    rdCacheTest_frustum.nearPlane   = 1.0f;
    rdCacheTest_frustum.farPlane    = 100.0f;
    rdCacheTest_frustum.leftPlane   = -1.0f;
    rdCacheTest_frustum.rightPlane  = 1.0f;
    rdCacheTest_frustum.topPlane    = 1.0f;
    rdCacheTest_frustum.bottomPlane = -1.0f;

    memset(&rdCacheTest_camera, 0, sizeof(rdCacheTest_camera));
    rdCacheTest_camera.pCanvas          = &rdCacheTest_canvas;
    rdCacheTest_camera.focalLength      = 100.0f;
    rdCacheTest_camera.aspectRatio      = 1.0f;
    rdCacheTest_camera.invNearClipPlane = 1.0f;
    rdCacheTest_camera.invFarClipPlane  = 0.01f;
    rdCacheTest_camera.pFrustum         = &rdCacheTest_frustum;

    rdMatrix34 view = { { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 10.0f, 0.0f } };
    rdCacheTest_camera.viewMatrix = view;

    rdCacheTest_pSavedCamera       = rdCamera_g_pCurCamera;
    rdCacheTest_savedCamMatrix     = rdCamera_g_camMatrix;
    rdCacheTest_savedRenderOptions = rdroid_g_curRenderOptions;
    rdCacheTest_bSavedLitFaces     = rdGetLitFaces();
    rdCamera_g_pCurCamera          = &rdCacheTest_camera;
    rdCacheTest_bModelCamera       = true;

    // The camera in front of the faces whose normal points to -y: they are front faces.
    memset(&rdCamera_g_camMatrix, 0, sizeof(rdCamera_g_camMatrix));
    rdCamera_g_camMatrix.dvec.y = -10.0f;
    rdroid_g_curRenderOptions   = RDROID_BACKFACE_CULLING_ENABLED;
}

static void rdCacheTest_RestoreModelCamera(void)
{
    if ( !rdCacheTest_bModelCamera )
    {
        return;
    }

    rdCacheTest_bModelCamera  = false;
    rdCamera_g_pCurCamera     = rdCacheTest_pSavedCamera;
    rdCamera_g_camMatrix      = rdCacheTest_savedCamMatrix;
    rdroid_g_curRenderOptions = rdCacheTest_savedRenderOptions;
    rdSetLitFaces(rdCacheTest_bSavedLitFaces);
}

// Draws the one-face mesh (vertices v0, v1, v2) with lit faces on or off and reads its draws.
static void rdCacheTest_DrawModelFace(bool bLitFaces, const int* aVerts, rdLightMode lightMode, rdFaceFlags flags, const rdVector3* pNormal)
{
    for ( size_t i = 0; i < 3u; ++i )
    {
        rdCacheTest_aModelFaceVerts[i] = aVerts[i];
    }

    memset(&rdCacheTest_modelFace, 0, sizeof(rdCacheTest_modelFace));
    rdCacheTest_modelFace.flags        = flags | RD_FF_TEX_FILTER_NEAREST;
    rdCacheTest_modelFace.geometryMode = RD_GEOMETRY_FULL;
    rdCacheTest_modelFace.lightingMode = lightMode;
    rdCacheTest_modelFace.numVertices  = 3u;
    rdCacheTest_modelFace.aVertices    = rdCacheTest_aModelFaceVerts;
    rdCacheTest_modelFace.aTexVertices = rdCacheTest_aModelFaceTexVerts;
    rdCacheTest_modelFace.matCelNum    = -1;
    rdCacheTest_modelFace.normal       = *pNormal;

    memset(&rdCacheTest_mesh, 0, sizeof(rdCacheTest_mesh));
    rdCacheTest_mesh.geoMode           = RD_GEOMETRY_FULL;
    rdCacheTest_mesh.lightMode         = RD_LIGHTING_GOURAUD;
    rdCacheTest_mesh.apVertices        = (rdVector3*)rdCacheTest_aModelVerts;
    rdCacheTest_mesh.apTexVertices     = (rdVector2*)rdCacheTest_aModelTexVerts;
    rdCacheTest_mesh.aVertColors       = (rdVector4*)rdCacheTest_aModelColors;
    rdCacheTest_mesh.aLightIntensities = rdCacheTest_aModelIntensities;
    rdCacheTest_mesh.meshColor.red     = 1.0f;
    rdCacheTest_mesh.meshColor.green   = 1.0f;
    rdCacheTest_mesh.meshColor.blue    = 1.0f;
    rdCacheTest_mesh.meshColor.alpha   = 1.0f;
    rdCacheTest_mesh.aFaces            = &rdCacheTest_modelFace;
    rdCacheTest_mesh.aVertNormals      = (rdVector3*)rdCacheTest_aModelNormals;
    rdCacheTest_mesh.numVertices       = 4u;
    rdCacheTest_mesh.numTexVertices    = 4u;
    rdCacheTest_mesh.numFaces          = 1u;

    memset(&rdCacheTest_node, 0, sizeof(rdCacheTest_node));
    rdCacheTest_node.meshIdx = 0;

    memset(&rdCacheTest_model, 0, sizeof(rdCacheTest_model));
    rdCacheTest_model.aGeos[0].numMeshes = 1u;
    rdCacheTest_model.aGeos[0].aMeshes   = &rdCacheTest_mesh;
    rdCacheTest_model.numGeos            = 1u;
    rdCacheTest_model.numHNodes          = 1u;
    rdCacheTest_model.aHierarchyNodes    = &rdCacheTest_node;

    // The joint matrices are this frame's, so rdModel3_Draw uses them as they are: identity.
    memset(&rdCacheTest_jointMatrix, 0, sizeof(rdCacheTest_jointMatrix));
    rdCacheTest_jointMatrix.rvec.x = 1.0f;
    rdCacheTest_jointMatrix.lvec.y = 1.0f;
    rdCacheTest_jointMatrix.uvec.z = 1.0f;
    rdCacheTest_jointAmputation    = 0;

    memset(&rdCacheTest_thing, 0, sizeof(rdCacheTest_thing));
    rdCacheTest_thing.type                   = RD_THING_MODEL3;
    rdCacheTest_thing.data.pModel3           = &rdCacheTest_model;
    rdCacheTest_thing.rdFrameNum             = (int)rdCache_GetFrameNum();
    rdCacheTest_thing.paJointMatrices        = &rdCacheTest_jointMatrix;
    rdCacheTest_thing.paJointAmputationFlags = &rdCacheTest_jointAmputation;
    rdCacheTest_thing.geosetNum              = 0;
    rdCacheTest_thing.frustumCull            = RDFRUSTUMCULL_INSIDE;

    rdSetLitFaces(bLitFaces);
    rdMatrix34 placement = rdCacheTest_jointMatrix;
    TEST_ASSERT_EQUAL_INT(1, rdModel3_Draw(&rdCacheTest_thing, &placement));

    TEST_ASSERT_TRUE(stdRenderCapture_Open("rdCacheTest.rcap", "null"));
    rdCache_Flush();
    rdCache_FlushAlpha();
    stdRenderCapture_Close();
    rdCacheTest_ParseDraws();
}

// The face drawn with lit faces on and then off: one lit draw, then one classic draw with the same
// StdVertexTL bytes. aNormals are the lit draw's expected normals.
static void rdCacheTest_AssertModelFace(const int* aVerts, rdLightMode lightMode, rdFaceFlags flags, const rdVector3* pNormal, const rdVector3* aNormals)
{
    rdCacheTest_DrawModelFace(true, aVerts, lightMode, flags, pNormal);
    TEST_ASSERT_EQUAL_size_t(1u, rdCacheTest_numDraws);
    RdCacheTestDraw lit = rdCacheTest_aDraws[0];
    TEST_ASSERT_EQUAL_UINT16(RDCACHETEST_ID_DRAWLIT, lit.id);
    TEST_ASSERT_EQUAL_size_t(3u, lit.numVerts);

    rdCacheTest_DrawModelFace(false, aVerts, lightMode, flags, pNormal);
    TEST_ASSERT_EQUAL_size_t(1u, rdCacheTest_numDraws);
    const RdCacheTestDraw* pClassic = &rdCacheTest_aDraws[0];
    TEST_ASSERT_EQUAL_UINT16(RDCACHETEST_ID_DRAW, pClassic->id);
    TEST_ASSERT_EQUAL_size_t(3u, pClassic->numVerts);
    TEST_ASSERT_EQUAL_INT32(pClassic->rdflags, lit.rdflags);
    TEST_ASSERT_EQUAL_size_t(pClassic->numIndices, lit.numIndices);
    TEST_ASSERT_EQUAL_UINT16_ARRAY(pClassic->aIndices, lit.aIndices, lit.numIndices);

    for ( size_t i = 0; i < 3u; ++i )
    {
        TEST_ASSERT_EQUAL_MEMORY(&pClassic->aVerts[i].tl, &lit.aVerts[i].tl, sizeof(StdVertexTL));

        // The view position: (x, y, z) of the model is (-z, y + 10, x) under the camera
        const rdVector3* pModel = &rdCacheTest_aModelVerts[aVerts[i]];
        TEST_ASSERT_EQUAL_FLOAT(-pModel->z, lit.aVerts[i].vx);
        TEST_ASSERT_EQUAL_FLOAT(pModel->y + 10.0f, lit.aVerts[i].vy);
        TEST_ASSERT_EQUAL_FLOAT(pModel->x, lit.aVerts[i].vz);

        TEST_ASSERT_FLOAT_WITHIN(1e-6f, aNormals[i].x, lit.aVerts[i].nx);
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, aNormals[i].y, lit.aVerts[i].ny);
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, aNormals[i].z, lit.aVerts[i].nz);
    }
}

TEST(rdCache, GouraudModelFacesCarryTheirVertexNormalsInViewSpace)
{
    rdCacheTest_SetupModelCamera();
    const int aVerts[3]          = { 0, 1, 2 };
    const rdVector3 faceNormal   = { 0.0f, -1.0f, 0.0f };
    const rdVector3 aExpected[3] = { { 0.0f, -1.0f, 0.0f }, { 0.0f, -0.8f, 0.6f }, { -0.6f, -0.8f, 0.0f } };
    rdCacheTest_AssertModelFace(aVerts, RD_LIGHTING_GOURAUD, 0, &faceNormal, aExpected);
}

TEST(rdCache, FlatModelFacesCarryTheirFaceNormalInViewSpace)
{
    // A diffuse face: the face normal (0, -0.6, -0.8), rotated to (0.8, -0.6, 0), at every vertex.
    rdCacheTest_SetupModelCamera();
    const int aVerts[3]          = { 1, 3, 2 };
    const rdVector3 faceNormal   = { 0.0f, -0.6f, -0.8f };
    const rdVector3 aExpected[3] = { { 0.8f, -0.6f, 0.0f }, { 0.8f, -0.6f, 0.0f }, { 0.8f, -0.6f, 0.0f } };
    rdCacheTest_AssertModelFace(aVerts, RD_LIGHTING_DIFFUSE, 0, &faceNormal, aExpected);
}

TEST(rdCache, BackFacesOfModelsCarryNegatedNormals)
{
    // A double-sided face seen from behind (its normal points away from the camera): drawn, with
    // the rotated vertex normals negated.
    rdCacheTest_SetupModelCamera();
    const int aVerts[3]          = { 0, 2, 3 };
    const rdVector3 faceNormal   = { 0.0f, 1.0f, 0.0f };
    const rdVector3 aExpected[3] = { { 0.0f, 1.0f, 0.0f }, { 0.6f, 0.8f, 0.0f }, { 0.0f, 1.0f, 0.0f } };
    rdCacheTest_AssertModelFace(aVerts, RD_LIGHTING_GOURAUD, RD_FF_DOUBLE_SIDED, &faceNormal, aExpected);
}

TEST(rdCache, TranslucentModelFacesAreDrawnLitFromTheAlphaList)
{
    rdCacheTest_SetupModelCamera();
    const int aVerts[3]          = { 0, 1, 3 };
    const rdVector3 faceNormal   = { 0.0f, -1.0f, 0.0f };
    const rdVector3 aExpected[3] = { { 0.0f, -1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f } };
    rdCacheTest_AssertModelFace(aVerts, RD_LIGHTING_DIFFUSE, RD_FF_TEX_TRANSLUCENT, &faceNormal, aExpected);
}

TEST(rdCache, ModelFacesStayClassicWithLitFacesOff)
{
    // Lit faces off: whatever the face, rdCache makes no lit draw (record 35), only classic ones.
    rdCacheTest_SetupModelCamera();
    const int aVerts[3]        = { 0, 1, 2 };
    const rdVector3 faceNormal = { 0.0f, -1.0f, 0.0f };
    const rdFaceFlags aFlags[3] = { 0, RD_FF_DOUBLE_SIDED, RD_FF_TEX_TRANSLUCENT };
    for ( size_t i = 0; i < STD_ARRAYLEN(aFlags); ++i )
    {
        rdCacheTest_DrawModelFace(false, aVerts, RD_LIGHTING_GOURAUD, aFlags[i], &faceNormal);
        TEST_ASSERT_EQUAL_size_t(1u, rdCacheTest_numDraws);
        TEST_ASSERT_EQUAL_UINT16(RDCACHETEST_ID_DRAW, rdCacheTest_aDraws[0].id);
    }

    TEST_ASSERT_FALSE(rdGetLitFaces());
}

TEST_GROUP_RUNNER(rdCache)
{
    RUN_TEST_CASE(rdCache, LitFacesAreDrawnLitAndApartFromClassicOnes);
    RUN_TEST_CASE(rdCache, ClassicFacesStayOnTheClassicDraw);
    RUN_TEST_CASE(rdCache, AnEntryLeftUnaddedDoesNotMakeTheNextOneLit);
    RUN_TEST_CASE(rdCache, LitAlphaFacesAreDrawnLit);
    RUN_TEST_CASE(rdCache, GouraudModelFacesCarryTheirVertexNormalsInViewSpace);
    RUN_TEST_CASE(rdCache, FlatModelFacesCarryTheirFaceNormalInViewSpace);
    RUN_TEST_CASE(rdCache, BackFacesOfModelsCarryNegatedNormals);
    RUN_TEST_CASE(rdCache, TranslucentModelFacesAreDrawnLitFromTheAlphaList);
    RUN_TEST_CASE(rdCache, ModelFacesStayClassicWithLitFacesOff);
}
