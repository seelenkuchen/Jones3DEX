#ifndef RDROID_TESTS_RDMODEL3TESTHOST_H
#define RDROID_TESTS_RDMODEL3TESTHOST_H

#include <rdroid/Main/rdroid.h>
#include <rdroid/Engine/rdCamera.h>

extern tHostServices* rdModel3Test_pHS;
extern rdCamera* rdModel3Test_pCamera;
extern rdMatrix34 rdModel3Test_cameraMatrix;
extern rdMatrix34 rdModel3Test_identity;
extern rdRoidFlags rdModel3Test_renderOptions;
extern rdLightMode rdModel3Test_lightingMode;
extern int rdModel3Test_numDrawnFaces;
extern int rdModel3Test_numDrawnAlphaFaces;

// Altered: this fork's rdroid globals are real variables, not RTI address slots, so redirect them
// by name. rdroid_g_identMatrix34 and rdModel3_g_numDrawn*Faces are defined by translation units
// this target compiles itself and need no redirect.
#undef rdroid_g_pHS_ADDR
#define rdroid_g_pHS_ADDR ((uintptr_t)&rdModel3Test_pHS)
#define rdroid_g_pHS rdModel3Test_pHS
#define rdCamera_g_pCurCamera rdModel3Test_pCamera
#define rdCamera_g_camMatrix rdModel3Test_cameraMatrix
#define rdroid_g_curRenderOptions rdModel3Test_renderOptions
#define rdroid_g_curLightingMode rdModel3Test_lightingMode

// Keep the actual model/face submission code while replacing the device-facing
// queue and clipping endpoints with deferred CPU fixtures.
#define rdCache_GetProcEntry rdModel3Test_GetProcEntry
#define rdCache_GetAlphaProcEntry rdModel3Test_GetAlphaProcEntry
#define rdCache_AddProcFace rdModel3Test_AddProcFace
#define rdCache_AddAlphaProcFace rdModel3Test_AddAlphaProcFace
#define rdClip_FaceToPlane rdModel3Test_ClipFaceToPlane

#endif
