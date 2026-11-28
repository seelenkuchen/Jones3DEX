// Added: test-only seam for the sithRender static shadow caster upload (M11.3). The test build forces
// this file in before sithRender.c and renames std3D_SetStaticShadowCasters to the recorder below, so the
// production upload helper is exercised as it is without inventing a recording seam in std3D.
#ifndef SITH_ENGINE_TESTS_SITHRENDERTESTSEAMS_H
#define SITH_ENGINE_TESTS_SITHRENDERTESTSEAMS_H

#include <stddef.h>

void sithRenderTest_SetStaticShadowCasters(const float* aPositions, size_t numVertices);

#endif
