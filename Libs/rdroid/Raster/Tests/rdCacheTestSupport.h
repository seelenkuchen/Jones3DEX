#ifndef RDROID_RDCACHETESTSUPPORT_H
#define RDROID_RDCACHETESTSUPPORT_H
#include <j3dcore/j3d.h>

#include <rdroid/types.h>
#include <std/types.h>

#include <stddef.h>
#include <stdint.h>

// RCAP record ids (stdRenderCaptureIds.h): DrawRenderList, DrawRenderListLit.
#define RDCACHETEST_ID_DRAW    14u
#define RDCACHETEST_ID_DRAWLIT 35u

// What every face here draws with: nearest filtering keeps the QOL filter choice out of the state.
#define RDCACHETEST_RDFLAGS (STD3D_RS_SUBPIXEL_CORRECTION | STD3D_RS_UNKNOWN_2 | STD3D_RS_UNKNOWN_1)

// One recorded draw: its record id, texture handle, render state, vertices (StdVertexTL records
// leave the position and normal zero) and indices.
typedef struct sRdCacheTestDraw
{
    uint16_t id;
    uint32_t texHandle;
    int32_t rdflags;
    size_t numVerts;
    StdVertexLit aVerts[32];
    size_t numIndices;
    uint16_t aIndices[128];
} RdCacheTestDraw;

extern RdCacheTestDraw rdCacheTest_aDraws[8];
extern size_t rdCacheTest_numDraws;

// Gouraud intensities over the extra light (0.25, 0, 0.5): the colors 0xFF7F7FBF, 0xFFFFFFFF and
// 0xFF3F007F (0.5, 0.5, 0.75; 1, 1, 1.25 clamped to 1; 0.25, 0, 0.5, each * 255 truncated).
extern const rdVector4 rdCacheTest_extraLight;
extern const rdVector4 rdCacheTest_aIntensities[3];
extern const uint32_t rdCacheTest_aColors[3];

// Installs the test host services (print, malloc and the in-memory render capture file).
void rdCacheTest_InitHost(void);

// Clears the render capture and starts a fresh rdCache frame.
void rdCacheTest_ResetRender(void);

// Reads the draw records of the capture in memory into rdCacheTest_aDraws.
void rdCacheTest_ParseDraws(void);

// The raw capture for the batch parser: its bytes, size and whether the write overflowed.
const uint8_t* rdCacheTest_GetCaptureFile(size_t* pSize, bool* pOverflow);
uint32_t rdCacheTest_U32(const uint8_t* p);
float rdCacheTest_F32(const uint8_t* p);

// The faces the tests add: screen vertex i of face f.
StdVertexTL rdCacheTest_ScreenVertex(size_t face, size_t i);

// The camera the smooth and model tests project with, and its restore.
void rdCacheTest_SetupModelCamera(void);
void rdCacheTest_RestoreModelCamera(void);

#endif // RDROID_RDCACHETESTSUPPORT_H
