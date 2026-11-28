#include "rdCacheTestSupport.h"

#include <unity_fixture.h>

#include <j3dcore/Tests/j3dTest.h>
#include <rdroid/Math/Tests/rdMathTestHost.h>

#include <rdroid/Engine/rdCamera.h>
#include <rdroid/Main/rdroid.h>
#include <rdroid/Raster/rdCache.h>

#include <std/General/std.h>
#include <std/General/std3D.h>
#include <std/General/stdRenderCapture.h>
#include <std/General/stdUtil.h>

#include <stdlib.h>
#include <string.h>

// The capture file, in memory. The batch tests below feed up to 65,535 vertices in one draw.
#define RDCACHETEST_FILESIZE (8u * 1024u * 1024u)
static uint8_t rdCacheTest_aFile[RDCACHETEST_FILESIZE];
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

RdCacheTestDraw rdCacheTest_aDraws[8];
size_t rdCacheTest_numDraws;

uint32_t rdCacheTest_U32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

float rdCacheTest_F32(const uint8_t* p)
{
    uint32_t bits = rdCacheTest_U32(p);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

const uint8_t* rdCacheTest_GetCaptureFile(size_t* pSize, bool* pOverflow)
{
    *pSize     = rdCacheTest_fileSize;
    *pOverflow = rdCacheTest_bFileOverflow;
    return rdCacheTest_aFile;
}

void rdCacheTest_InitHost(void)
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
}

void rdCacheTest_ResetRender(void)
{
    stdRenderCapture_ResetGlobals();
    rdCache_Startup();
    rdCache_AdvanceFrame();
    rdCacheTest_fileSize = 0;
    rdCacheTest_numDraws = 0;
}

// Reads the draw records of the capture in memory into rdCacheTest_aDraws, checking each payload's
// size; every other record is skipped by its size.
void rdCacheTest_ParseDraws(void)
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

StdVertexTL rdCacheTest_ScreenVertex(size_t face, size_t i)
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

const rdVector4 rdCacheTest_extraLight = { 0.25f, 0.0f, 0.5f, 1.0f };
const rdVector4 rdCacheTest_aIntensities[3] = {
    { 0.25f, 0.5f, 0.25f, 1.0f }, { 0.75f, 1.0f, 0.75f, 1.0f }, { 0.0f, 0.0f, 0.0f, 1.0f },
};
const uint32_t rdCacheTest_aColors[3] = { 0xFF7F7FBFu, 0xFFFFFFFFu, 0xFF3F007Fu };

static rdCanvas rdCacheTest_canvas;
static tVBuffer rdCacheTest_vbuffer;
static rdCamera rdCacheTest_camera;
static rdClipFrustum rdCacheTest_frustum;

static bool rdCacheTest_bModelCamera; // set until the tear down restores what the setup replaced
static rdCamera* rdCacheTest_pSavedCamera;
static rdMatrix34 rdCacheTest_savedCamMatrix;
static rdRoidFlags rdCacheTest_savedRenderOptions;
static bool rdCacheTest_bSavedLitFaces;
static bool rdCacheTest_bSavedPerPixelLighting;

void rdCacheTest_SetupModelCamera(void)
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
    rdCacheTest_bSavedPerPixelLighting = rdGetPerPixelLighting();
    rdCamera_g_pCurCamera          = &rdCacheTest_camera;
    rdCacheTest_bModelCamera       = true;

    // The camera in front of the faces whose normal points to -y: they are front faces.
    memset(&rdCamera_g_camMatrix, 0, sizeof(rdCamera_g_camMatrix));
    rdCamera_g_camMatrix.dvec.y = -10.0f;
    rdroid_g_curRenderOptions   = RDROID_BACKFACE_CULLING_ENABLED;
}

void rdCacheTest_RestoreModelCamera(void)
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
    rdSetPerPixelLighting(rdCacheTest_bSavedPerPixelLighting);
}
