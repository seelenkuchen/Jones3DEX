#include "rdFace.h"
#include <rdroid/Main/rdroid.h>
#include <rdroid/Math/rdVector.h>
#include <std/General/stdMath.h>
#include <std/General/stdMemory.h>

#include <math.h>
#include <stdint.h>
#include <string.h>

void rdFace_ResetGlobals(void)
{}

rdFace* J3DAPI rdFace_New(void)
{
    rdFace* pFace;

    pFace = (rdFace*)STDMALLOC(sizeof(rdFace));
    if ( !pFace )

    {
        RDLOG_ERROR("Error allocating memory for face.\n");
        return NULL;
    }

    rdFace_NewEntry(pFace);
    RDLOG_DEBUG("New face created.\n");
    return pFace;
}

int J3DAPI rdFace_NewEntry(rdFace* pFace)
{
    pFace->num   = 0;
    pFace->flags = 0;

    pFace->numVertices  = 0;
    pFace->aVertices    = NULL;
    pFace->aTexVertices = NULL;

    pFace->pMaterial = NULL;
    pFace->matCelNum = -1;

    pFace->normal = rdroid_g_zeroVector3;

    pFace->texVertOffset.x = 0.0f;
    pFace->texVertOffset.y = 0.0f;

    pFace->extraLight.red   = 0.0f;
    pFace->extraLight.green = 0.0f;
    pFace->extraLight.blue  = 0.0f;
    pFace->extraLight.alpha = 0.0f;
    return 1;
}

void J3DAPI rdFace_Free(rdFace* pFace)
{
    if ( !pFace )
    {
        RDLOG_ERROR("Warning: Attempt to free NULL face ptr.\n");
        return;
    }

    rdFace_FreeEntry(pFace);
    stdMemory_Free(pFace);
}

void J3DAPI rdFace_FreeEntry(rdFace* pFace)
{
    if ( pFace->aVertices )
    {
        stdMemory_Free(pFace->aVertices);
    }

    if ( pFace->aTexVertices )
    {
        stdMemory_Free(pFace->aTexVertices);
    }
}

static bool rdFace_IsSmoothPartner(const rdFace* pFace, const bool* abExcluded, size_t faceNum)
{
    // Added
    if ( abExcluded && abExcluded[faceNum] )
    {
        return false;
    }

    return pFace->geometryMode != RD_GEOMETRY_NONE
        && (pFace->normal.x != 0.0f || pFace->normal.y != 0.0f || pFace->normal.z != 0.0f);
}

bool rdFace_CalcSmoothNormals(const rdFace* const* apFaces, const bool* abExcluded, size_t numFaces, size_t numVertices, float maxAngle, size_t* aFirstCorner, rdVector3* aCornerNormals)
{
    // Added
    if ( numFaces == 0 )
    {
        return true;
    }

    if ( !apFaces || !aFirstCorner || !aCornerNormals )
    {
        return false;
    }

    // The vertex-to-face table: the partner faces of vertex v are aVertFaces[aVertStart[v]] to
    // aVertFaces[aVertStart[v + 1] - 1], in face order (a face listing v twice is there twice, next
    // to itself).
    size_t numRefs = 0;
    for ( size_t faceNum = 0; faceNum < numFaces; ++faceNum )
    {
        const rdFace* pFace = apFaces[faceNum];
        if ( !pFace )
        {
            return false;
        }

        if ( !rdFace_IsSmoothPartner(pFace, abExcluded, faceNum) )
        {
            continue;
        }

        for ( size_t i = 0; i < pFace->numVertices; ++i )
        {
            int vertIdx = pFace->aVertices[i];
            if ( vertIdx >= 0 && (size_t)vertIdx < numVertices )
            {
                ++numRefs;
            }
        }
    }

    const size_t maxEntries = SIZE_MAX / sizeof(size_t);
    if ( numRefs >= maxEntries - 3u || numVertices >= (maxEntries - numRefs - 3u) / 2u )
    {
        return false;
    }

    size_t* aTable = (size_t*)STDMALLOC(sizeof(size_t) * (2u * (numVertices + 1u) + numRefs + 1u));
    if ( !aTable )
    {
        return false;
    }

    size_t* aVertStart  = aTable;
    size_t* aVertCursor = aTable + numVertices + 1u;
    size_t* aVertFaces  = aVertCursor + numVertices + 1u;
    memset(aVertStart, 0, sizeof(size_t) * (numVertices + 1u));

    for ( size_t faceNum = 0; faceNum < numFaces; ++faceNum )
    {
        const rdFace* pFace = apFaces[faceNum];
        if ( rdFace_IsSmoothPartner(pFace, abExcluded, faceNum) )
        {
            for ( size_t i = 0; i < pFace->numVertices; ++i )
            {
                int vertIdx = pFace->aVertices[i];
                if ( vertIdx >= 0 && (size_t)vertIdx < numVertices )
                {
                    ++aVertStart[vertIdx + 1];
                }
            }
        }
    }

    for ( size_t v = 0; v < numVertices; ++v )
    {
        aVertStart[v + 1u] += aVertStart[v];
        aVertCursor[v]      = aVertStart[v];
    }

    for ( size_t faceNum = 0; faceNum < numFaces; ++faceNum )
    {
        const rdFace* pFace = apFaces[faceNum];
        if ( rdFace_IsSmoothPartner(pFace, abExcluded, faceNum) )
        {
            for ( size_t i = 0; i < pFace->numVertices; ++i )
            {
                int vertIdx = pFace->aVertices[i];
                if ( vertIdx >= 0 && (size_t)vertIdx < numVertices )
                {
                    aVertFaces[aVertCursor[vertIdx]++] = faceNum;
                }
            }
        }
    }

    const float cosMaxAngle = cosf((float)STDMATH_RADIANS(maxAngle));

    size_t curCorner = 0;
    for ( size_t faceNum = 0; faceNum < numFaces; ++faceNum )
    {
        const rdFace* pFace = apFaces[faceNum];
        aFirstCorner[faceNum] = curCorner;

        rdVector3 faceNormal;
        rdVector_Normalize3(&faceNormal, &pFace->normal);
        bool bHasNormal = faceNormal.x != 0.0f || faceNormal.y != 0.0f || faceNormal.z != 0.0f;

        for ( size_t i = 0; i < pFace->numVertices; ++i )
        {
            rdVector3* pOut = &aCornerNormals[curCorner++];
            *pOut           = faceNormal;

            int vertIdx = pFace->aVertices[i];
            if ( !bHasNormal || vertIdx < 0 || (size_t)vertIdx >= numVertices )
            {
                continue;
            }

            rdVector3 sum      = faceNormal;
            bool bSmooth       = false;
            size_t prevFaceNum = SIZE_MAX;
            for ( size_t k = aVertStart[vertIdx]; k < aVertStart[vertIdx + 1]; ++k )
            {
                size_t partnerNum = aVertFaces[k];
                if ( partnerNum == prevFaceNum || partnerNum == faceNum )
                {
                    continue;
                }

                prevFaceNum = partnerNum;

                rdVector3 partnerNormal;
                rdVector_Normalize3(&partnerNormal, &apFaces[partnerNum]->normal);
                if ( rdVector_Dot3(&faceNormal, &partnerNormal) > cosMaxAngle )
                {
                    rdVector_Add3Acc(&sum, &partnerNormal);
                    bSmooth = true;
                }
            }

            if ( bSmooth && (sum.x != 0.0f || sum.y != 0.0f || sum.z != 0.0f) )
            {
                rdVector_Normalize3(pOut, &sum);
            }
        }
    }

    STDFREE(aTable);
    return true;
}
