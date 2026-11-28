#ifndef RDROID_RDPOLYSMOOTH_H
#define RDROID_RDPOLYSMOOTH_H
#include <j3dcore/j3d.h>

#include <rdroid/types.h>
#include <rdroid/Raster/rdCache.h>

#include <std/types.h>

J3D_EXTERN_C_START

// Added: Smooth Gouraud light inside world polygons. rdCache's old zig-zag triangulation
// interpolates the baked corner light along each triangle's diagonal, so a faint light/dark
// diagonal shows on a polygon (a Mach band). This module tessellates a convex planar polygon
// into rings whose extra vertices carry a smooth interpolation of the corner light.
//
// The boundary edges are never split, so neighbouring faces keep matching edges and no
// T-junctions appear. Ring r (1..rings-1) point i sits at C + (r/rings) * (V_i - C), C the
// average of the corners; ring `rings` is the corners themselves. The extra vertices follow the
// corners in this order: ring 1, ring 2, ..., ring rings-1, then the centre. The centre fan and
// the bands between rings keep the polygon's winding.
//
// The ring count comes from the polygon's view-space size, not from its screen size: the view
// transform is rigid, so the tessellation does not pop when the camera moves.
#define RDPOLYSMOOTH_RINGSPACING 0.1f // Longest edge per ring, in view-space units
#define RDPOLYSMOOTH_MAXRINGS    8u

// The most extra vertices one face can get: (rings - 1) * n + 1, n <= RDCACHE_MAXFACEVERTICES.
#define RDPOLYSMOOTH_MAXEXTRAVERTS ((RDPOLYSMOOTH_MAXRINGS - 1) * RDCACHE_MAXFACEVERTICES + 1)

// Added: the camera values rdPolySmooth_BuildVertices projects with, the formulas of
// rdClip_FaceToPlaneEx. focalLength is the camera's focal length times its aspect ratio.
typedef struct srdPolySmoothProjection
{
    float focalLength;
    float centerX;
    float centerY;
    float invNearPlane;
    float invFarPlane;
} rdPolySmoothProjection;

// Returns the number of rings to tessellate a convex planar polygon with, 0 to keep the old
// zig-zag triangulation. aViewVerts are the corners in view space. Returns 0 for fewer than 4
// corners, more than RDCACHE_MAXFACEVERTICES, a degenerate or non-convex
// polygon, or a longest edge below RDPOLYSMOOTH_RINGSPACING. Otherwise
// ceil(longest edge / RDPOLYSMOOTH_RINGSPACING), clamped to RDPOLYSMOOTH_MAXRINGS.
size_t rdPolySmooth_GetRingCount(const rdVector3* aViewVerts, size_t numVerts);

// Number of vertices the tessellation adds after the corners: (rings - 1) * numVerts + 1,
// 0 when rings is 0 or the arguments are out of range.
size_t rdPolySmooth_GetExtraVertexCount(size_t numVerts, size_t rings);

// Number of triangle indices the tessellation writes: 3 * numVerts * (2 * rings - 1),
// 0 when rings is 0 or the arguments are out of range.
size_t rdPolySmooth_GetIndexCount(size_t numVerts, size_t rings);

// Mean value coordinates (Floater 2003) of pPoint with respect to the polygon aVerts, computed
// in 3D. On success aOutWeights holds numVerts weights that sum to 1 and reproduce a linear
// function over the polygon exactly; they are smooth inside a convex polygon. When pPoint
// coincides with a corner the weights are 1 at that corner. Returns false, writing nothing,
// for NULL arguments, numVerts below 3 or above RDCACHE_MAXFACEVERTICES, or a degenerate sum.
bool rdPolySmooth_MeanValueWeights(const rdVector3* aVerts, size_t numVerts, const rdVector3* pPoint, float* aOutWeights);

// Builds the extra vertices of a face into aOutVerts, aOutValues, aOutViewVerts and
// aOutViewNormals, (rings - 1) * pFace->numVertices + 1 of each, in the order described above.
// pFace must carry corner screen vertices, view positions and view normals. aCornerValues holds
// one already-final value per corner (the corner's clamped color, not its raw intensity): the
// mean value weights blend those into aOutValues, so bright corners stay bright instead of being
// washed out by a later clamp. aOutVerts gets sx, sy, sz, rhw, tu and tv, with color and specular
// copied from corner 0 (the caller replaces the color). View normals are renormalized (zero stays
// zero). The caller must leave room for RDPOLYSMOOTH_MAXEXTRAVERTS entries.
void rdPolySmooth_BuildVertices(const rdCacheProcEntry* pFace, size_t rings, const rdPolySmoothProjection* pProj,
    const rdVector4* aCornerValues, StdVertexTL* aOutVerts, rdVector4* aOutValues,
    rdVector3* aOutViewVerts, rdVector3* aOutViewNormals);

// Writes the triangle indices of the tessellation, 3 * numVerts * (2 * rings - 1) of them.
// Corner k is baseIndex + k; the extra vertices follow in the order of rdPolySmooth_BuildVertices.
// Rings below 1 write nothing.
void rdPolySmooth_BuildIndices(size_t numVerts, size_t rings, uint16_t baseIndex, uint16_t* aOutIndices);

J3D_EXTERN_C_END
#endif // RDROID_RDPOLYSMOOTH_H
