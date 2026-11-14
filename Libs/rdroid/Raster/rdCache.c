#include "rdCache.h"

#include <rdroid/Main/rdroid.h>

#include <std/General/stdEffect.h>
#include <std/General/stdMath.h>
#include <std/General/stdUtil.h>
#include <std/General/std3D.h>

#include <float.h>
#include <math.h>
#include <stdlib.h>

#define RDCACHE_VERTBUFFERSIZE (RDCACHE_MAXVERTICES * RDCACHE_MAXFACEVERTICES)
static_assert(RDCACHE_VERTBUFFERSIZE == 2097152, "RDCACHE_VERTBUFFERSIZE == 2097152");

static size_t rdCache_numProcFaces              = 0;
static rdCacheProcEntry rdCache_aProcFaces[128] = { 0 };

static size_t rdCache_numUsedVertices = 0;
static StdVertexTL rdCache_aVertices[RDCACHE_VERTBUFFERSIZE]      = { 0 };
static rdVector4 rdCache_aVertIntensities[RDCACHE_VERTBUFFERSIZE] = { 0 }; // Fixed: Changed to match size of rdCache_aVertices, was 8192
static rdVector3 rdCache_aViewVertices[RDCACHE_VERTBUFFERSIZE]     = { 0 }; // Added: the lit path's view-space positions and normals
static rdVector3 rdCache_aViewNormals[RDCACHE_VERTBUFFERSIZE]      = { 0 };

static size_t rdCache_numAlphaProcFaces                                    = 0;
// Altered: QOL builds hold 4096 alpha faces, the original 2048; a larger list means fewer early flushes, so alpha face batches (and their sort order) differ from the original.
static rdCacheProcEntry rdCache_aAlphaProcFaces[J3D_QOL_VALUE(4096, 2048)] = { 0 };

static size_t rdCache_numUsedAlphaVertices = 0;
static StdVertexTL rdCache_aAlphaVertices[RDCACHE_VERTBUFFERSIZE]      = { 0 };
static rdVector4 rdCache_aAlphaVertIntensities[RDCACHE_VERTBUFFERSIZE] = { 0 };
static rdVector3 rdCache_aAlphaViewVertices[RDCACHE_VERTBUFFERSIZE]    = { 0 }; // Added: as rdCache_aViewVertices
static rdVector3 rdCache_aAlphaViewNormals[RDCACHE_VERTBUFFERSIZE]     = { 0 };

static size_t rdCache_totalVerts             = 0;
static StdVertexTL* rdCache_pCurInVert      = NULL;
static StdVertexTL* rdCache_pCurCacheVertex = NULL;

static StdVertexTL rdCache_aHWVertices[RDCACHE_VERTBUFFERSIZE]     = { 0 };
static StdVertexLit rdCache_aHWLitVertices[RDCACHE_VERTBUFFERSIZE]  = { 0 }; // Added: rdCache_aHWVertices of a lit draw
static uint16_t rdCache_aVertIndices[RDCACHE_VERTBUFFERSIZE * 3]   = { 0 };

static size_t rdCache_drawnFaces = 0;
static size_t rdCache_frameNum   = 0;

int J3DAPI rdCache_ProcFaceDistanceCompare(const rdCacheProcEntry* pEntry1, const rdCacheProcEntry* pEntry2);
int J3DAPI rdCache_ProcFaceCompare(const rdCacheProcEntry* pEntry1, const rdCacheProcEntry* pEntry2);

void rdCache_ResetGlobals(void)
{}

void rdCache_Startup(void)
{
    STD_ZEROMEM(rdCache_aHWVertices, sizeof(rdCache_aHWVertices)); // Fixed: num bytes to the bytes size of rdCache_aHWVertices. Was STD_ARRAYLEN(rdCache_aHWVertices)
    rdCache_frameNum = 0;
}

void rdCache_AdvanceFrame(void)
{
    rdCache_drawnFaces = 0; // Could be part rdCache_ClearFrameCounters();
    ++rdCache_frameNum;

    // Below code could be part of function rdCache_Reset
    rdCache_numProcFaces         = 0;
    rdCache_numUsedVertices      = 0;
    rdCache_numAlphaProcFaces    = 0;
    rdCache_numUsedAlphaVertices = 0;
}

size_t rdCache_GetFrameNum(void)
{
    return rdCache_frameNum;
}

rdCacheProcEntry* rdCache_GetProcEntry(void)
{
    size_t procNum;
    rdCacheProcEntry* pProcEntry;

    procNum = rdCache_numProcFaces;
    if ( rdCache_numProcFaces >= STD_ARRAYLEN(rdCache_aProcFaces) )
    {
        rdCache_Flush();
        procNum = rdCache_numProcFaces;
    }

     // Added: Check if the vertex buffer is large enough to hold all face vertices.
    //        Note, taken from grimengine / OpenJKDF2
    if ( RDCACHE_VERTBUFFERSIZE - rdCache_numUsedVertices < RDCACHE_MAXFACEVERTICES )
    {
        return NULL;
    }

    // TODO: add bound check for cached verts
    pProcEntry                   = &rdCache_aProcFaces[procNum];
    pProcEntry->aVertices        = &rdCache_aVertices[rdCache_numUsedVertices];
    pProcEntry->aVertIntensities = &rdCache_aVertIntensities[rdCache_numUsedVertices];
    pProcEntry->aViewVertices    = NULL; // Added: drawn the classic way
    pProcEntry->aViewNormals     = NULL;
    return pProcEntry;
}

rdCacheProcEntry* rdCache_GetLitProcEntry(void)
{
    // Added
    rdCacheProcEntry* pProcEntry = rdCache_GetProcEntry();
    if ( pProcEntry )
    {
        pProcEntry->aViewVertices = &rdCache_aViewVertices[rdCache_numUsedVertices];
        pProcEntry->aViewNormals  = &rdCache_aViewNormals[rdCache_numUsedVertices];
    }

    return pProcEntry;
}

rdCacheProcEntry* rdCache_GetAlphaProcEntry(void)
{
    size_t entryNum;
    rdCacheProcEntry* pProcEntry;

    entryNum = rdCache_numAlphaProcFaces;
    if ( rdCache_numAlphaProcFaces >= STD_ARRAYLEN(rdCache_aAlphaProcFaces) )
    {
        rdCache_Flush();
        rdCache_FlushAlpha();
        entryNum = rdCache_numAlphaProcFaces;
    }

    // Added: Check if the vertex buffer is large enough to hold all face vertices.
    //        Note, taken from grimengine / OpenJKDF2
    if ( RDCACHE_VERTBUFFERSIZE - rdCache_numUsedAlphaVertices < RDCACHE_MAXFACEVERTICES )
    {
        return NULL;
    }

    // TODO: add bound check for verts
    pProcEntry                   = &rdCache_aAlphaProcFaces[entryNum];
    pProcEntry->aVertices        = &rdCache_aAlphaVertices[rdCache_numUsedAlphaVertices];
    pProcEntry->aVertIntensities = &rdCache_aAlphaVertIntensities[rdCache_numUsedAlphaVertices];
    pProcEntry->aViewVertices    = NULL; // Added: drawn the classic way
    pProcEntry->aViewNormals     = NULL;
    return pProcEntry;
}

rdCacheProcEntry* rdCache_GetLitAlphaProcEntry(void)
{
    // Added
    rdCacheProcEntry* pProcEntry = rdCache_GetAlphaProcEntry();
    if ( pProcEntry )
    {
        pProcEntry->aViewVertices = &rdCache_aAlphaViewVertices[rdCache_numUsedAlphaVertices];
        pProcEntry->aViewNormals  = &rdCache_aAlphaViewNormals[rdCache_numUsedAlphaVertices];
    }

    return pProcEntry;
}

void rdCache_Flush(void)
{
    if ( rdCache_numProcFaces )
    {
        switch ( rdroid_g_curGeometryMode )
        {
            case RD_GEOMETRY_NONE:
                break;

            case RD_GEOMETRY_VERTEX:
            case RD_GEOMETRY_WIREFRAME:
                rdCache_SendWireframeFaceListToHardware(rdCache_numProcFaces, rdCache_aProcFaces);
                break;

            default:
                rdCache_SendFaceListToHardware(rdCache_numProcFaces, rdCache_aProcFaces, rdCache_ProcFaceCompare);
                break;
        }

        rdCache_drawnFaces      += rdCache_numProcFaces;
        rdCache_numProcFaces    = 0;
        rdCache_numUsedVertices = 0;
    }
}

void rdCache_FlushAlpha(void)
{
    if ( rdCache_numAlphaProcFaces )
    {
        switch ( rdroid_g_curGeometryMode )
        {
            case RD_GEOMETRY_NONE:
                break;

            case RD_GEOMETRY_VERTEX:
            case RD_GEOMETRY_WIREFRAME:
                rdCache_SendWireframeFaceListToHardware(rdCache_numAlphaProcFaces, rdCache_aAlphaProcFaces);
                break;

            default:
                rdCache_SendFaceListToHardware(rdCache_numAlphaProcFaces, rdCache_aAlphaProcFaces, rdCache_ProcFaceDistanceCompare);
                break;
        }

        rdCache_drawnFaces          += rdCache_numAlphaProcFaces;
        rdCache_numAlphaProcFaces    = 0;
        rdCache_numUsedAlphaVertices = 0;
    }
}

void J3DAPI rdCache_AddProcFace(size_t numVerts)
{
    rdCache_aProcFaces[rdCache_numProcFaces].numVertices = numVerts;
    rdCache_numUsedVertices += numVerts;
    ++rdCache_numProcFaces;
}

void J3DAPI rdCache_AddAlphaProcFace(size_t numVertices)
{
    rdCacheProcEntry* pEntry = &rdCache_aAlphaProcFaces[rdCache_numAlphaProcFaces];
    pEntry->numVertices = numVertices;

    float sz = FLT_MAX; // 3.4028235e38f;
    for ( size_t i = 0; i < numVertices; ++i )
    {
        if ( pEntry->aVertices[i].sz < sz )
        {
            sz = pEntry->aVertices[i].sz;
        }
    }

    pEntry->distance = sz;
    rdCache_numUsedAlphaVertices += numVertices;
    ++rdCache_numAlphaProcFaces;
}

void J3DAPI rdCache_SendFaceListToHardware(size_t numPolys, rdCacheProcEntry* pCurPoly, rdCacheSortFunc pfSort)
{
    size_t polyNum   = 0;
    int curMatCelNum = -1;
    // Fixed: Bound WORD-indexed batches, including devices limited to 65,535 unique vertices.
    const size_t maxBatchVertices = J3DMIN((size_t)UINT16_MAX, std3D_g_maxVertices);
    const tStdFadeFactor* pFadeFactor = stdEffect_GetFadeFactor();

    if ( pfSort == rdCache_ProcFaceDistanceCompare )
    {
        qsort(pCurPoly, numPolys, sizeof(rdCacheProcEntry), (int (*)(const void*, const void*))pfSort);
    }

LABEL_4:
    if ( polyNum < numPolys )
    {
        rdFaceFlags fflags = pCurPoly->flags;
        Std3DRenderState rdflags = STD3D_RS_SUBPIXEL_CORRECTION | STD3D_RS_UNKNOWN_2 | STD3D_RS_UNKNOWN_1;

        if ( (fflags & RD_FF_TEX_CLAMP_X) != 0 )
        {
            rdflags |= STD3D_RS_TEX_CPAMP_U;
        }

        if ( (fflags & RD_FF_TEX_CLAMP_Y) != 0 )
        {
            rdflags |= STD3D_RS_TEX_CPAMP_V;
        }

        if ( (fflags & RD_FF_TEX_FILTER_NEAREST) == 0 )
        {
            rdflags |= J3D_QOL_VALUE(STD3D_RS_TEXFILTER_ANISOTROPIC, STD3D_RS_TEXFILTER_BILINEAR); // Altered: Use STD3D_RS_TEXFILTER_ANISOTROPIC.
        }

        if ( (fflags & RD_FF_ZWRITE_DISABLED) != 0 )
        {
            rdflags |=  STD3D_RS_ZWRITE_DISABLED;
        }

        if ( (fflags & RD_FF_FOG_ENABLED) != 0 )
        {
            rdflags |= STD3D_RS_FOG_ENABLED;
        }

        // Added: a draw holds only lit faces (std3D_DrawRenderListLit) or only classic ones
        bool bLit = pCurPoly->aViewNormals != NULL;

        rdMaterial* pCurMat = NULL;
        if ( rdroid_g_curGeometryMode != RD_GEOMETRY_SOLID )
        {
            pCurMat = pCurPoly->pMaterial;
        }

        tSysTexture* pCachedTexture = NULL;
        if ( pCurMat )
        {
            // format = pCurMat->formatType;
            if ( pCurMat->formatType == STDCOLOR_FORMAT_RGBA_1BITALPHA )
            {
                rdflags |= STD3D_RS_ALPHAREF_SET;
            }

            tSystemTexture* pTex = NULL;
            curMatCelNum = pCurPoly->matCelNum;
            if ( curMatCelNum == -1 )
            {
                int texCelNum = STDMATH_CLAMP((int)pCurPoly->pMaterial->curCelNum, 0, (int)pCurPoly->pMaterial->numCels - 1);
                pTex = &pCurPoly->pMaterial->aTextures[texCelNum];
                rdCache_AddToTextureCache(pTex, pCurMat->formatType);
            }
            else
            {
                int texCelNum = STDMATH_CLAMP(curMatCelNum, 0, (int)pCurPoly->pMaterial->numCels - 1);
                pTex = &pCurPoly->pMaterial->aTextures[texCelNum];
                rdCache_AddToTextureCache(pTex, pCurMat->formatType);
            }

            pCachedTexture = pTex->pCachedTexture;
        }

        size_t totalIndices = 0;
        rdCache_totalVerts  = 0;
        while ( 1 )
        {
            uint16_t curVertIdx = rdCache_totalVerts;
            rdLightMode lightingMode = rdroid_g_curLightingMode;
            if ( pCurPoly->lightingMode < rdroid_g_curLightingMode )
            {
                lightingMode = pCurPoly->lightingMode;
            }

            rdCache_pCurInVert = pCurPoly->aVertices;
            rdCache_pCurCacheVertex = &rdCache_aHWVertices[rdCache_totalVerts];
            for ( size_t i = 0; i < pCurPoly->numVertices; ++i )
            {
                STD_COPYMEM(rdCache_pCurCacheVertex, rdCache_pCurInVert++, sizeof(StdVertexTL));

                float blue  = 1.0f;
                float green = 1.0f;
                float red   = 1.0f;
                float alpha = pCurPoly->aVertIntensities->alpha;
                if ( lightingMode > RD_LIGHTING_NONE && lightingMode <= RD_LIGHTING_GOURAUD )
                {
                    red   = pCurPoly->extraLight.red;
                    green = pCurPoly->extraLight.green;
                    blue  = pCurPoly->extraLight.blue;
                    alpha = pCurPoly->aVertIntensities->alpha;
                }

                if ( lightingMode == RD_LIGHTING_GOURAUD )
                {
                    red   += pCurPoly->aVertIntensities[i].red;
                    green += pCurPoly->aVertIntensities[i].green;
                    blue  += pCurPoly->aVertIntensities[i].blue;
                    if ( pCurPoly->extraLight.alpha >= 1.0f )
                    {
                        alpha = pCurPoly->aVertIntensities[i].alpha;
                    }
                    else
                    {
                        alpha = pCurPoly->aVertIntensities[i].alpha + pCurPoly->extraLight.alpha;
                    }
                }

                if ( pFadeFactor->bEnabled )
                {
                    red   = pFadeFactor->factor * red;
                    green = pFadeFactor->factor * green;
                    blue  = pFadeFactor->factor * blue;
                }

                red   = STDMATH_CLAMP(red, 0.0f, 1.0f);
                green = STDMATH_CLAMP(green, 0.0f, 1.0f);
                blue  = STDMATH_CLAMP(blue, 0.0f, 1.0f);
                alpha = STDMATH_CLAMP(alpha, 0.0f, 1.0f);

                if ( (fflags & RD_FF_TEX_TRANSLUCENT) != 0 )
                {
                    rdCache_pCurCacheVertex->color = STD_COLOR_FROM_FLOATS(red, green, blue, alpha); // (int32_t)(blue * 255.0f) | ((unsigned int)(int32_t)(green * 255.0f) << 8) | ((unsigned int)(int32_t)(red * 255.0f) << 16) | ((unsigned int)(int32_t)(alpha * 255.0f) << 24);
                }
                else
                {
                    rdCache_pCurCacheVertex->color = STD_COLOR_FROM_FLOATS_RGB(red, green, blue); //(int32_t)(blue * 255.0f) | ((unsigned int)(int32_t)(green * 255.0f) << 8) | ((unsigned int)(int32_t)(red * 255.0f) << 16) | 0xFF000000;
                }

                ++rdCache_pCurCacheVertex;
                ++rdCache_totalVerts;
            }

            // Added: a lit face's vertices are the ones above with their view-space position and normal
            if ( bLit )
            {
                for ( size_t i = 0; i < pCurPoly->numVertices; ++i )
                {
                    StdVertexLit* pLitVert = &rdCache_aHWLitVertices[curVertIdx + i];
                    pLitVert->tl = rdCache_aHWVertices[curVertIdx + i];
                    pLitVert->vx = pCurPoly->aViewVertices[i].x;
                    pLitVert->vy = pCurPoly->aViewVertices[i].y;
                    pLitVert->vz = pCurPoly->aViewVertices[i].z;
                    pLitVert->nx = pCurPoly->aViewNormals[i].x;
                    pLitVert->ny = pCurPoly->aViewNormals[i].y;
                    pLitVert->nz = pCurPoly->aViewNormals[i].z;
                }
            }

            // Triangulation
            if ( pCurPoly->numVertices <= 3u )
            {
                rdCache_aVertIndices[totalIndices++] = curVertIdx;
                rdCache_aVertIndices[totalIndices++] = curVertIdx + 1;
                rdCache_aVertIndices[totalIndices++] = curVertIdx + 2;
            }
            else
            {
                uint16_t triCount     = pCurPoly->numVertices - 2;
                uint16_t triPoint1Num = 0;
                uint16_t triPoint2Num = 1;
                uint16_t triPoint3Num = pCurPoly->numVertices - 1;

                for ( size_t triNum = 0; triNum < triCount; ++triNum )
                {
                    rdCache_aVertIndices[totalIndices++] = triPoint1Num + curVertIdx;
                    rdCache_aVertIndices[totalIndices++] = triPoint2Num + curVertIdx;
                    rdCache_aVertIndices[totalIndices++] = triPoint3Num + curVertIdx;
                    if ( (triNum & 1) != 0 ) // if odd
                    {
                        triPoint1Num = triPoint3Num;
                        triPoint3Num--;// = triPoint3Num - 1;
                    }
                    else
                    {
                        triPoint1Num = triPoint2Num++;
                    }
                }
            }

            ++pCurPoly;
            ++polyNum;

            // Fixed: Check polyNum first; after the last poly, pCurPoly points past the end of the list
            if ( polyNum >= numPolys
                || pCurPoly->numVertices + rdCache_totalVerts > maxBatchVertices
                || 3 * (pCurPoly->numVertices - 2) + totalIndices > STD_ARRAYLEN(rdCache_aVertIndices)
                || 3 * (pCurPoly->numVertices - 2) + totalIndices >= std3D_g_maxVertices // i.e. totalIndices + num required triangle indices for next poly >= std3D_g_maxVertices
                || pCurMat != pCurPoly->pMaterial
                || curMatCelNum != pCurPoly->matCelNum
                || fflags != pCurPoly->flags
                || bLit != (pCurPoly->aViewNormals != NULL) ) // Added
            {
                RD_ASSERTREL(rdCache_totalVerts < RDCACHE_VERTBUFFERSIZE);
                if ( bLit ) // Added
                {
                    std3D_DrawRenderListLit(pCachedTexture, rdflags, rdCache_aHWLitVertices, rdCache_totalVerts, rdCache_aVertIndices, totalIndices);
                }
                else
                {
                    std3D_DrawRenderList(pCachedTexture, rdflags, rdCache_aHWVertices, rdCache_totalVerts, rdCache_aVertIndices, totalIndices);
                }
                goto LABEL_4;
            }
        }
    }
}

void J3DAPI rdCache_SendWireframeFaceListToHardware(size_t numPolys, rdCacheProcEntry* pCurPoly)
{
    std3D_SetWireframeRenderState();

    for ( size_t i = 0; i < numPolys; ++i )
    {
        for ( size_t j = 0; j < pCurPoly->numVertices; ++j )
        {
            pCurPoly->aVertices[j].color = STD_COLOR_ARGB(255, 255, 255, 255);// aka white color
        }

        if ( rdroid_g_curGeometryMode == RD_GEOMETRY_VERTEX )
        {
            std3D_DrawPointList(pCurPoly->aVertices, pCurPoly->numVertices); // Fixed: Use correct vertex buffer for drawing. Was using rdCache_aHWVertices.
        }
        else if ( rdroid_g_curGeometryMode == RD_GEOMETRY_WIREFRAME )
        {
            std3D_DrawLineStrip(pCurPoly->aVertices, pCurPoly->numVertices);

            StdVertexTL aVerts[2]; // Draw end vert twice to complete the line
            STD_COPYMEM(aVerts, &pCurPoly->aVertices[pCurPoly->numVertices - 1], sizeof(StdVertexTL));
            STD_COPYMEM(&aVerts[1], pCurPoly->aVertices, sizeof(StdVertexTL));
            std3D_DrawLineStrip(aVerts, 2u);
        }

        ++pCurPoly;
    }
}

void J3DAPI rdCache_AddToTextureCache(tSystemTexture* pTexture, StdColorFormatType format)
{
    if ( pTexture->pCachedTexture )
    {
        std3D_UpdateFrameCount(pTexture);
    }
    else
    {
        std3D_AddToTextureCache(pTexture, format);
    }
}

int J3DAPI rdCache_ProcFaceDistanceCompare(const rdCacheProcEntry* pEntry1, const rdCacheProcEntry* pEntry2)
{
    // Fixed: Compare equal depths equally and sort NaN keys last.
    if ( isnan(pEntry1->distance) )
    {
        return isnan(pEntry2->distance) ? 0 : 1;
    }
    if ( isnan(pEntry2->distance) )
    {
        return -1;
    }
    return (pEntry1->distance < pEntry2->distance) - (pEntry1->distance > pEntry2->distance);
}

int J3DAPI rdCache_ProcFaceCompare(const rdCacheProcEntry* pEntry1, const rdCacheProcEntry* pEntry2)
{
    rdMaterial* pMat1 = pEntry1->pMaterial;
    rdMaterial* pMat2 = pEntry2->pMaterial;
    if ( pMat1 == pMat2 )
    {
        return pEntry2->matCelNum - pEntry1->matCelNum;
    }

    return pMat2 - pMat1;
}