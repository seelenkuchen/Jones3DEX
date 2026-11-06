#ifndef RDROID_RDCACHE_H
#define RDROID_RDCACHE_H
#include <j3dcore/j3d.h>
#include <rdroid/types.h>
#include <std/types.h>
#include <std/General/std3D.h>    

J3D_EXTERN_C_START

// Changed: For compatibility reasons was changed to use macros from std3D.h
#define RDCACHE_MAXFACEVERTICES STD3D_MAXFACEVERTICES // Max vertices per face
                                                      // Note, some clipping functions, such as rdClip_FaceToPlane can handle at max 144 vertices. 
                                                      // So be conservative when raising this number

#define RDCACHE_MAXVERTICES STD3D_MAXVERTICES // Changed: Was 2048, and for compatibility reasons was changed to use macros from std3D.h

    void rdCache_Startup(void);
void rdCache_AdvanceFrame(void);
size_t rdCache_GetFrameNum(void);

rdCacheProcEntry* rdCache_GetProcEntry(void);
rdCacheProcEntry* rdCache_GetAlphaProcEntry(void);

// Added: option A L1 (ADR-0023). rdCache_GetProcEntry and rdCache_GetAlphaProcEntry for a face whose
// vertices carry their view-space position and normal: the entry's aViewVertices and aViewNormals
// point at room for RDCACHE_MAXFACEVERTICES vectors each, next to aVertices and aVertIntensities,
// valid until the face is added and drawn. The caller fills them for every vertex it adds (a zero
// normal where it knows none; rdClip_FaceToPlaneLit fills both); the face is then drawn with
// std3D_DrawRenderListLit, in the same color as the classic way. NULL when the vertex buffer is
// full, as the classic functions.
rdCacheProcEntry* rdCache_GetLitProcEntry(void);
rdCacheProcEntry* rdCache_GetLitAlphaProcEntry(void);

void  rdCache_Flush(void);
void  rdCache_FlushAlpha(void);

void J3DAPI rdCache_AddProcFace(size_t numVerts);
void J3DAPI rdCache_AddAlphaProcFace(size_t numVertices);

void J3DAPI rdCache_SendFaceListToHardware(size_t numPolys, rdCacheProcEntry* aPolys, rdCacheSortFunc pfSort);
void J3DAPI rdCache_SendWireframeFaceListToHardware(size_t numPolys, rdCacheProcEntry* aPolys);

void J3DAPI rdCache_AddToTextureCache(tSystemTexture* pTexture, StdColorFormatType format);

void rdCache_ResetGlobals(void);

J3D_EXTERN_C_END
#endif // RDROID_RDCACHE_H
