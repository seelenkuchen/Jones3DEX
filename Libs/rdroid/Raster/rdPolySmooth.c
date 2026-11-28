#include "rdPolySmooth.h"

#include <rdroid/Math/rdVector.h>

#include <math.h>
#include <string.h>

// Added: Smooth Gouraud light inside world polygons. See rdPolySmooth.h for the tessellation and
// why its extra vertices carry a smooth interpolation of the corner light.

#define RDPOLYSMOOTH_EPSILON 1e-6f
#define RDPOLYSMOOTH_MINDEPTH 1e-5f
#define RDPOLYSMOOTH_STRAIGHTTURN 1e-3f // sine of the largest backward turn still taken as straight

// 1/y for the projection. A ring point of a face that crosses the camera plane can sit at y = 0;
// it is moved off it so the projection stays finite. Corners behind the camera (y < 0) project
// the same way rdClip_FaceToPlaneEx projects them, and the GPU clips the face.
static float rdPolySmooth_InvDepth(float y)
{
    if ( fabsf(y) < RDPOLYSMOOTH_MINDEPTH )
    {
        y = y < 0.0f ? -RDPOLYSMOOTH_MINDEPTH : RDPOLYSMOOTH_MINDEPTH;
    }

    return 1.0f / y;
}

static float rdPolySmooth_ProjectX(const rdVector3* pViewVert, const rdPolySmoothProjection* pProj)
{
    float invY  = rdPolySmooth_InvDepth(pViewVert->y);
    float scale = pProj->focalLength * invY;
    return pViewVert->x * scale + pProj->centerX;
}

static float rdPolySmooth_ProjectY(const rdVector3* pViewVert, const rdPolySmoothProjection* pProj)
{
    float invY  = rdPolySmooth_InvDepth(pViewVert->y);
    float scale = pProj->focalLength * invY;
    return pProj->centerY - pViewVert->z * scale;
}

static float rdPolySmooth_ProjectZ(const rdVector3* pViewVert, const rdPolySmoothProjection* pProj)
{
    float invY = rdPolySmooth_InvDepth(pViewVert->y);
    return (invY - pProj->invNearPlane) * pProj->invFarPlane;
}

static float rdPolySmooth_ProjectRHW(const rdVector3* pViewVert)
{
    return rdPolySmooth_InvDepth(pViewVert->y) / 32.0f;
}

size_t rdPolySmooth_GetRingCount(const rdVector3* aViewVerts, size_t numVerts)
{
    if ( !aViewVerts || numVerts < 4u || numVerts > RDCACHE_MAXFACEVERTICES )
    {
        return 0;
    }

    // The polygon normal by Newell's method, so collinear first corners do not matter.
    rdVector3 normal = { 0.0f, 0.0f, 0.0f };
    for ( size_t i = 0; i < numVerts; ++i )
    {
        const rdVector3* pCur  = &aViewVerts[i];
        const rdVector3* pNext = &aViewVerts[(i + 1u) % numVerts];
        normal.x += (pCur->y - pNext->y) * (pCur->z + pNext->z);
        normal.y += (pCur->z - pNext->z) * (pCur->x + pNext->x);
        normal.z += (pCur->x - pNext->x) * (pCur->y + pNext->y);
    }

    float normalLen = rdVector_Len3(&normal);
    if ( normalLen <= RDPOLYSMOOTH_EPSILON )
    {
        return 0;
    }

    float longestEdge = 0.0f;
    for ( size_t i = 0; i < numVerts; ++i )
    {
        size_t j = (i + 1u) % numVerts;
        rdVector3 edge;
        rdVector_Sub3(&edge, &aViewVerts[j], &aViewVerts[i]);
        float len = rdVector_Len3(&edge);
        if ( len > longestEdge )
        {
            longestEdge = len;
        }
    }

    // Every consecutive pair of edges must turn the same way as the polygon normal. Corners on a
    // straight edge (a turn of at most RDPOLYSMOOTH_STRAIGHTTURN of the edge lengths, either way)
    // are allowed: world faces often carry them.
    for ( size_t i = 0; i < numVerts; ++i )
    {
        size_t j = (i + 1u) % numVerts;
        size_t k = (i + 2u) % numVerts;
        rdVector3 edgeA;
        rdVector3 edgeB;
        rdVector3 turn;
        rdVector_Sub3(&edgeA, &aViewVerts[j], &aViewVerts[i]);
        rdVector_Sub3(&edgeB, &aViewVerts[k], &aViewVerts[j]);
        rdVector_Cross3(&turn, &edgeA, &edgeB);
        if ( rdVector_Dot3(&turn, &normal) < -RDPOLYSMOOTH_STRAIGHTTURN * normalLen * rdVector_Len3(&edgeA) * rdVector_Len3(&edgeB) )
        {
            return 0;
        }
    }

    if ( longestEdge < RDPOLYSMOOTH_RINGSPACING )
    {
        return 0;
    }

    float ringCount = ceilf(longestEdge / RDPOLYSMOOTH_RINGSPACING);
    if ( ringCount < 1.0f )
    {
        ringCount = 1.0f;
    }
    if ( ringCount > (float)RDPOLYSMOOTH_MAXRINGS )
    {
        ringCount = (float)RDPOLYSMOOTH_MAXRINGS;
    }

    return (size_t)ringCount;
}

size_t rdPolySmooth_GetExtraVertexCount(size_t numVerts, size_t rings)
{
    if ( rings == 0 || numVerts == 0 || numVerts > RDCACHE_MAXFACEVERTICES || rings > RDPOLYSMOOTH_MAXRINGS )
    {
        return 0;
    }

    return (rings - 1u) * numVerts + 1u;
}

size_t rdPolySmooth_GetIndexCount(size_t numVerts, size_t rings)
{
    if ( rings == 0 || numVerts == 0 || numVerts > RDCACHE_MAXFACEVERTICES || rings > RDPOLYSMOOTH_MAXRINGS )
    {
        return 0;
    }

    return 3u * numVerts * (2u * rings - 1u);
}

bool rdPolySmooth_MeanValueWeights(const rdVector3* aVerts, size_t numVerts, const rdVector3* pPoint, float* aOutWeights)
{
    if ( !aVerts || !pPoint || !aOutWeights || numVerts < 3u || numVerts > RDCACHE_MAXFACEVERTICES )
    {
        return false;
    }

    rdVector3 aDirs[RDCACHE_MAXFACEVERTICES];
    float aRadius[RDCACHE_MAXFACEVERTICES];
    float aTanHalf[RDCACHE_MAXFACEVERTICES];
    float aWeights[RDCACHE_MAXFACEVERTICES];

    for ( size_t i = 0; i < numVerts; ++i )
    {
        rdVector_Sub3(&aDirs[i], &aVerts[i], pPoint);
        aRadius[i] = rdVector_Len3(&aDirs[i]);
        if ( aRadius[i] < RDPOLYSMOOTH_EPSILON )
        {
            memset(aOutWeights, 0, numVerts * sizeof(*aOutWeights));
            aOutWeights[i] = 1.0f;
            return true;
        }
    }

    // tan(a_i / 2) for the angle a_i between d_i and d_(i+1), in the polygon's 3D plane.
    for ( size_t i = 0; i < numVerts; ++i )
    {
        size_t j = (i + 1u) % numVerts;
        rdVector3 cross;
        rdVector_Cross3(&cross, &aDirs[i], &aDirs[j]);
        float denom = aRadius[i] * aRadius[j] + rdVector_Dot3(&aDirs[i], &aDirs[j]);
        if ( denom > RDPOLYSMOOTH_EPSILON )
        {
            aTanHalf[i] = rdVector_Len3(&cross) / denom;
        }
        else
        {
            aTanHalf[i] = 0.0f;
        }
    }

    float total = 0.0f;
    for ( size_t i = 0; i < numVerts; ++i )
    {
        size_t prev = (i + numVerts - 1u) % numVerts;
        aWeights[i] = (aTanHalf[prev] + aTanHalf[i]) / aRadius[i];
        total += aWeights[i];
    }

    if ( !(total > RDPOLYSMOOTH_EPSILON) || !isfinite(total) )
    {
        return false;
    }

    for ( size_t i = 0; i < numVerts; ++i )
    {
        aOutWeights[i] = aWeights[i] / total;
    }

    return true;
}

// One extra vertex at P = C + t * (V_cornerIndex - C), with the affine weights
// beta_k = (1 - t) / n + t * [k == cornerIndex]. The centre uses t = 0.
static void rdPolySmooth_MakeExtraVertex(const rdCacheProcEntry* pFace, size_t numVerts, float t, size_t cornerIndex,
    const rdPolySmoothProjection* pProj, const rdVector4* aCornerValues, StdVertexTL* pOutVert, rdVector4* pOutValue,
    rdVector3* pOutViewVert, rdVector3* pOutViewNormal)
{
    float aBeta[RDCACHE_MAXFACEVERTICES];
    for ( size_t k = 0; k < numVerts; ++k )
    {
        aBeta[k] = (1.0f - t) / (float)numVerts;
        if ( k == cornerIndex )
        {
            aBeta[k] += t;
        }
    }

    rdVector3 viewVert = { 0.0f, 0.0f, 0.0f };
    rdVector3 viewNormal = { 0.0f, 0.0f, 0.0f };
    float tu = 0.0f;
    float tv = 0.0f;
    for ( size_t k = 0; k < numVerts; ++k )
    {
        rdVector_MultAcc3(&viewVert, &pFace->aViewVertices[k], aBeta[k]);
        rdVector_MultAcc3(&viewNormal, &pFace->aViewNormals[k], aBeta[k]);
        tu += aBeta[k] * pFace->aVertices[k].tu;
        tv += aBeta[k] * pFace->aVertices[k].tv;
    }

    float normalLen = rdVector_Len3(&viewNormal);
    if ( normalLen > 0.0f )
    {
        rdVector_InvScale3Acc(&viewNormal, normalLen);
    }

    float aWeights[RDCACHE_MAXFACEVERTICES];
    if ( !rdPolySmooth_MeanValueWeights(pFace->aViewVertices, numVerts, &viewVert, aWeights) )
    {
        memcpy(aWeights, aBeta, numVerts * sizeof(*aWeights));
    }

    rdVector4 value = { 0.0f, 0.0f, 0.0f, 0.0f };
    for ( size_t k = 0; k < numVerts; ++k )
    {
        rdVector_MultAcc4(&value, &aCornerValues[k], aWeights[k]);
    }

    pOutVert->sx       = rdPolySmooth_ProjectX(&viewVert, pProj);
    pOutVert->sy       = rdPolySmooth_ProjectY(&viewVert, pProj);
    pOutVert->sz       = rdPolySmooth_ProjectZ(&viewVert, pProj);
    pOutVert->rhw      = rdPolySmooth_ProjectRHW(&viewVert);
    pOutVert->color    = pFace->aVertices[0].color;
    pOutVert->specular = pFace->aVertices[0].specular;
    pOutVert->tu       = tu;
    pOutVert->tv       = tv;

    *pOutValue      = value;
    *pOutViewVert   = viewVert;
    *pOutViewNormal = viewNormal;
}

void rdPolySmooth_BuildVertices(const rdCacheProcEntry* pFace, size_t rings, const rdPolySmoothProjection* pProj,
    const rdVector4* aCornerValues, StdVertexTL* aOutVerts, rdVector4* aOutValues,
    rdVector3* aOutViewVerts, rdVector3* aOutViewNormals)
{
    if ( !pFace || !pProj || !aCornerValues || !aOutVerts || !aOutValues || !aOutViewVerts || !aOutViewNormals
        || rings == 0 || rings > RDPOLYSMOOTH_MAXRINGS
        || !pFace->aVertices || !pFace->aViewVertices || !pFace->aViewNormals )
    {
        return;
    }

    size_t numVerts = pFace->numVertices;
    if ( numVerts < 4u || numVerts > RDCACHE_MAXFACEVERTICES )
    {
        return;
    }

    size_t outIdx = 0;
    for ( size_t r = 1; r < rings; ++r )
    {
        float t = (float)r / (float)rings;
        for ( size_t i = 0; i < numVerts; ++i, ++outIdx )
        {
            rdPolySmooth_MakeExtraVertex(pFace, numVerts, t, i, pProj, aCornerValues,
                &aOutVerts[outIdx], &aOutValues[outIdx], &aOutViewVerts[outIdx], &aOutViewNormals[outIdx]);
        }
    }

    // The centre is the last extra vertex (also the only one when rings == 1).
    rdPolySmooth_MakeExtraVertex(pFace, numVerts, 0.0f, 0, pProj, aCornerValues,
        &aOutVerts[outIdx], &aOutValues[outIdx], &aOutViewVerts[outIdx], &aOutViewNormals[outIdx]);
}

void rdPolySmooth_BuildIndices(size_t numVerts, size_t rings, uint16_t baseIndex, uint16_t* aOutIndices)
{
    if ( !aOutIndices || rings == 0 || rings > RDPOLYSMOOTH_MAXRINGS
        || numVerts < 4u || numVerts > RDCACHE_MAXFACEVERTICES )
    {
        return;
    }

    uint16_t centre = (uint16_t)(baseIndex + rings * numVerts);

    // Centre fan to ring 1, or to the corners themselves when rings == 1.
    for ( size_t i = 0; i < numVerts; ++i )
    {
        size_t i1 = (i + 1u) % numVerts;
        uint16_t ringIndex = rings == 1u ? 0u : (uint16_t)(numVerts + i);
        uint16_t ringIndex1 = rings == 1u ? 0u : (uint16_t)(numVerts + i1);
        *aOutIndices++ = centre;
        *aOutIndices++ = (uint16_t)(baseIndex + (rings == 1u ? i : ringIndex));
        *aOutIndices++ = (uint16_t)(baseIndex + (rings == 1u ? i1 : ringIndex1));
    }

    // Bands between ring r and ring r+1; the last outer ring is the corners.
    for ( size_t r = 1; r < rings; ++r )
    {
        size_t inStart  = (r - 1u) * numVerts;
        size_t outStart = (r + 1u == rings) ? 0u : r * numVerts;
        for ( size_t i = 0; i < numVerts; ++i )
        {
            size_t i1 = (i + 1u) % numVerts;
            uint16_t in0  = (uint16_t)(baseIndex + numVerts + inStart + i);
            uint16_t in1  = (uint16_t)(baseIndex + numVerts + inStart + i1);
            uint16_t out0 = (uint16_t)(baseIndex + (r + 1u == rings ? outStart + i : numVerts + outStart + i));
            uint16_t out1 = (uint16_t)(baseIndex + (r + 1u == rings ? outStart + i1 : numVerts + outStart + i1));

            *aOutIndices++ = in0;
            *aOutIndices++ = out0;
            *aOutIndices++ = out1;
            *aOutIndices++ = in0;
            *aOutIndices++ = out1;
            *aOutIndices++ = in1;
        }
    }
}
