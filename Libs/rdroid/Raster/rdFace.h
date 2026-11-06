#ifndef RDROID_RDFACE_H
#define RDROID_RDFACE_H
#include <j3dcore/j3d.h>
#include <rdroid/types.h>
#include <std/types.h>

J3D_EXTERN_C_START

rdFace* J3DAPI rdFace_New(void); // Added
int J3DAPI rdFace_NewEntry(rdFace* pFace);

void J3DAPI rdFace_Free(rdFace* pFace); // Added
void J3DAPI rdFace_FreeEntry(rdFace* pFace);

void rdFace_ResetGlobals(void);

// Added: option A L3 (ADR-0023). Smooth normals for faces that store only a face normal (level
// surfaces), computed once, at load. A corner is one vertex of one face; the corners of face f are
// aCornerNormals[aFirstCorner[f]] to aCornerNormals[aFirstCorner[f] + apFaces[f]->numVertices - 1],
// in the face's vertex order, laid out face after face (aFirstCorner[0] is 0), so aCornerNormals needs
// the sum of every face's numVertices. The normal of a corner of face f at vertex v is the normalized
// sum of the unit normals of f and of every other partner face that has v as a vertex and whose normal
// is less than maxAngle degrees from f's (cos of the angle greater than cos(maxAngle)); a corner with no
// such partner gets f's own unit normal, so hard edges stay hard and flat faces keep their normal. The
// test is between f and each partner, not transitive. A face is never a partner when
// abExcluded[f] is true (abExcluded may be NULL: no face excluded), when its geometry mode is
// RD_GEOMETRY_NONE or when its normal is zero; an excluded face still gets its corners. A vertex index
// outside [0, numVertices) joins nothing: that corner gets the face normal. The normals are in the
// faces' space; a zero face normal gives zero corners.
// Returns false and writes nothing when an argument is NULL (apFaces, an entry of it, aFirstCorner
// or aCornerNormals, unless numFaces is 0) or the temporary vertex-to-face table can't be allocated
// (STDMALLOC, freed before returning). Nothing else is read or changed.
bool rdFace_CalcSmoothNormals(const rdFace* const* apFaces, const bool* abExcluded, size_t numFaces, size_t numVertices, float maxAngle, size_t* aFirstCorner, rdVector3* aCornerNormals);

J3D_EXTERN_C_END
#endif // RDROID_RDFACE_H
