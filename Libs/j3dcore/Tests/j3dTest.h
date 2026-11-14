#ifndef J3DCORE_TESTS_J3DTEST_H
#define J3DCORE_TESTS_J3DTEST_H

#include <stddef.h>
#include <stdint.h>

#include <j3dcore/j3d.h>

// Added: AddressSanitizer aborts an allocation above its own maximum instead of letting the
// system allocator return NULL. Tests that intentionally request such a block can skip only
// that request when this is set.
#if defined(__SANITIZE_ADDRESS__)
#  define J3DTEST_ADDRESS_SANITIZER 1
#elif defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    define J3DTEST_ADDRESS_SANITIZER 1
#  endif
#endif

typedef struct sHostServices tHostServices;

J3D_EXTERN_C_START

void J3DTest_MapRtiMemory(uintptr_t address, size_t size);

void J3DTest_Startup(void);
void J3DTest_BindHostServicesGlobal(uintptr_t hostServicesAddress);
void J3DTest_Shutdown(void);
tHostServices* J3DTest_GetHostServices(void);

void J3DTest_ResetAssertCapture(void);
int J3DTest_GetAssertCount(void);
const char* J3DTest_GetLastAssertText(void);
const char* J3DTest_GetLastAssertFile(void);
int J3DTest_GetLastAssertLine(void);
void J3DTest_AssertLastAssert(const char* pExpectedText, const char* pExpectedFileFragment);

J3D_EXTERN_C_END

#endif // J3DCORE_TESTS_J3DTEST_H
