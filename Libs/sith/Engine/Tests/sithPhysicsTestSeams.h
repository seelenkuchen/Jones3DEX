#ifndef SITH_PHYSICS_TEST_SEAMS_H
#define SITH_PHYSICS_TEST_SEAMS_H

#include <rdroid/Math/rdVector.h>

// Include the real vector helper before renaming its call sites. Observing the
// integration step catches an unexpected step or the old unsigned negative-step loop.
void J3DAPI sithPhysicsTest_MultAcc3(rdVector3* pAcc, const rdVector3* pVector, float scale);
#define rdVector_MultAcc3 sithPhysicsTest_MultAcc3

// Model the obsolete setting; JSON persistence is covered by stdConfig's tests.
float J3DAPI sithPhysicsTest_ConfigGetFloat(const char* pKey, float defaultValue);
bool sithPhysicsTest_ConfigContains(const char* pKey);
bool J3DAPI sithPhysicsTest_ConfigSetFloat(const char* pKey, float value);
#define stdConfig_GetFloat sithPhysicsTest_ConfigGetFloat
#define stdConfig_Contains sithPhysicsTest_ConfigContains
#define stdConfig_SetFloat sithPhysicsTest_ConfigSetFloat

#endif
