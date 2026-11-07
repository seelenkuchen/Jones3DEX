// Altered: this suite ran only in the Win32/DirectX build, against std/Win95/stdPlatformWin32.c. It now
// covers the shared std/General/stdPlatform.c and the platform layer every column links,
// Null/stdPlatformNull.c with the column's time file. The cases of the Windows layer alone (the stack
// frame string probe CheckStackFrameStringArg, the QueryPerformanceCounter timer, the unhandled-exception
// filter and its minidump) went with that file (ADR-0011).
#include <unity_fixture.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <std/General/std.h>
#include <std/General/stdFileUtil.h>
#include <std/General/stdMemory.h>
#include <std/General/stdPlatform.h>
#include <std/General/stdUtil.h>

#include "stdGeneralTest.h"

// The banner stdPlatform_PrintStackTrace prints before any frame.
#define STDPLATFORMTEST_STACKTRACE_BANNER "\n==================== STACK TRACE ====================\n"

static char stdPlatformTest_aStackTrace[2048];

// Altered: formats its arguments; the Null layer passes the frame number and symbol as arguments
// ("  [%d] %s\n"), where the Windows layer handed in finished lines.
static void J3DAPI stdPlatformTest_CaptureStackTrace(const char* pFormat, ...)
{
    if ( pFormat )
    {
        char aLine[512];
        va_list args;
        va_start(args, pFormat);
        vsnprintf(aLine, sizeof(aLine), pFormat, args);
        va_end(args);
        stdUtil_StringCat(stdPlatformTest_aStackTrace, sizeof(stdPlatformTest_aStackTrace), aLine);
    }
}

TEST_GROUP(stdPlatform);

TEST_SETUP(stdPlatform)
{
    StdGeneralTest_Startup();
    stdPlatformTest_aStackTrace[0] = '\0';
}

TEST_TEAR_DOWN(stdPlatform)
{
    stdFileUtil_RmDir("stdPlatformTest_dir");
    StdGeneralTest_Shutdown();
}

TEST(stdPlatform, TestInitServicesPopulatesExpectedCallbacksAndClearZerosThem)
{
    tHostServices services;
    tHostServices zeroServices;

    STD_ZEROMEM(&services, sizeof(services));
    STD_ZEROMEM(&zeroServices, sizeof(zeroServices));
    // Altered: the result is the platform's COM start-up result; SUCCEEDED(hr) || hr == S_FALSE was the
    // Windows spelling of "not negative". The Null layer has no COM and returns S_OK, 0.
    int result = stdPlatform_InitServices(&services);
    TEST_ASSERT_TRUE(result >= 0);
    TEST_ASSERT_EQUAL_FLOAT(1000.0f, services.unknown1);
    TEST_ASSERT_EQUAL_PTR(stdPlatform_Printf, services.pMessagePrint);
    TEST_ASSERT_EQUAL_PTR(stdPlatform_Printf, services.pStatusPrint);
    TEST_ASSERT_EQUAL_PTR(stdPlatform_Printf, services.pWarningPrint);
    TEST_ASSERT_EQUAL_PTR(stdPlatform_Printf, services.pErrorPrint);
    TEST_ASSERT_EQUAL_PTR(stdPlatform_Printf, services.pDebugPrint);
    TEST_ASSERT_EQUAL_PTR(stdPlatform_Assert, services.pAssert);
    TEST_ASSERT_EQUAL_PTR(stdMemory_BlockMalloc, services.pMalloc);
    TEST_ASSERT_EQUAL_PTR(stdMemory_BlockFree, services.pFree);
    TEST_ASSERT_EQUAL_PTR(stdMemory_BlockRealloc, services.pRealloc);
    TEST_ASSERT_EQUAL_PTR(stdPlatform_GetTimeMsec, services.pGetTimeMsec);
    TEST_ASSERT_EQUAL_PTR(stdFileOpen, services.pFileOpen);
    TEST_ASSERT_EQUAL_PTR(stdFileClose, services.pFileClose);
    TEST_ASSERT_EQUAL_PTR(stdFileRead, services.pFileRead);
    TEST_ASSERT_EQUAL_PTR(stdFileGets, services.pFileGets);
    TEST_ASSERT_EQUAL_PTR(stdFileWrite, services.pFileWrite);
    TEST_ASSERT_EQUAL_PTR(stdFileEof, services.pFileEOF);
    TEST_ASSERT_EQUAL_PTR(stdFileTell, services.pFileTell);
    TEST_ASSERT_EQUAL_PTR(stdFileSeek, services.pFileSeek);
    TEST_ASSERT_EQUAL_PTR(stdFileSize, services.pFileSize);
    TEST_ASSERT_EQUAL_PTR(stdFilePrintf, services.pFilePrintf);
    TEST_ASSERT_EQUAL_PTR(stdFileGetws, services.pFileGetws);
    TEST_ASSERT_EQUAL_PTR(stdPlatform_AllocHandle, services.pAllocHandle);
    TEST_ASSERT_EQUAL_PTR(stdPlatform_FreeHandle, services.pFreeHandle);
    TEST_ASSERT_EQUAL_PTR(stdPlatform_ReallocHandle, services.pReallocHandle);
    TEST_ASSERT_EQUAL_PTR(stdPlatform_LockHandle, services.pLockHandle);
    TEST_ASSERT_EQUAL_PTR(stdPlatform_UnlockHandle, services.pUnlockHandle);

    stdPlatform_ClearServices(&services);
    TEST_ASSERT_EQUAL_MEMORY(&zeroServices, &services, sizeof(services));
}

TEST(stdPlatform, TestHandleAllocationReallocationFreeAndLock)
{
    uint8_t* pData = (uint8_t*)stdPlatform_AllocHandle(8u);

    TEST_ASSERT_NOT_NULL(pData);
    pData[0] = 0x12u;
    pData[7] = 0x34u;

    pData = (uint8_t*)stdPlatform_ReallocHandle(pData, 16u);
    TEST_ASSERT_NOT_NULL(pData);
    TEST_ASSERT_EQUAL_UINT8(0x12u, pData[0]);
    TEST_ASSERT_EQUAL_UINT8(0x34u, pData[7]);
    stdPlatform_FreeHandle(pData);

    TEST_ASSERT_EQUAL_INT(123, stdPlatform_LockHandle(123));
    stdPlatform_UnlockHandle();
}

TEST(stdPlatform, TestTimerPrintfAndDirExists)
{
    tStdTime first = stdPlatform_GetTimeMsec();
    tStdTime second = stdPlatform_GetTimeMsec();

    TEST_ASSERT_TRUE(second >= first);
    TEST_ASSERT_EQUAL_INT(1, stdPlatform_Printf("%s %d", "platform", 7));
    TEST_ASSERT_EQUAL_STRING("platform 7", std_g_genBuffer);

    TEST_ASSERT_TRUE(stdFileUtil_MkDir("stdPlatformTest_dir"));
    TEST_ASSERT_TRUE(stdPlatform_DirExists("stdPlatformTest_dir"));
    TEST_ASSERT_FALSE(stdPlatform_DirExists("stdPlatformTest_missing_dir"));
}

// Altered: was TestTimeMsecUsesActiveTimerBranch; the QueryPerformanceCounter branch it also checked
// went with the Windows layer. Sleep is the engine's stdPlatform_Sleep instead of the Win32 one.
TEST(stdPlatform, TestTimeMsecDoesNotGoBackAcrossSleep)
{
    tStdTime first = stdPlatform_GetTimeMsec();
    stdPlatform_Sleep(1u);
    TEST_ASSERT_TRUE(stdPlatform_GetTimeMsec() >= first);
}

// Altered: was TestInstallSignalHandlerAndZeroFrameStackTrace. stdPlatform_InstallSignalHandler has no
// definition any more (only the Windows layer had one), so only the zero-frame trace is left. The Null
// layer prints the banner and no closing line; the Windows layer closed with a line of '='.
TEST(stdPlatform, TestZeroFrameStackTracePrintsOnlyTheBanner)
{
    stdPlatform_PrintStackTrace(stdPlatformTest_CaptureStackTrace, 0u);

#ifdef _WIN32
    // The Null layer on Windows has no unwinder and says so after the banner.
    TEST_ASSERT_EQUAL_STRING(
        STDPLATFORMTEST_STACKTRACE_BANNER
        "  no stack trace on this platform\n",
        stdPlatformTest_aStackTrace
    );
#else
    TEST_ASSERT_EQUAL_STRING(STDPLATFORMTEST_STACKTRACE_BANNER, stdPlatformTest_aStackTrace);
#endif
}

TEST(stdPlatform, TestOneFrameStackTraceProducesFrameOutput)
{
    stdPlatform_PrintStackTrace(stdPlatformTest_CaptureStackTrace, 1u);

    // Altered: the Windows layer numbered its frames "#00"; the Null layer prints glibc's frames as
    // "  [0] <symbol>" and, on Windows, no frame at all.
    TEST_ASSERT_EQUAL_MEMORY(STDPLATFORMTEST_STACKTRACE_BANNER, stdPlatformTest_aStackTrace, sizeof(STDPLATFORMTEST_STACKTRACE_BANNER) - 1u);
#ifdef _WIN32
    TEST_ASSERT_EQUAL_STRING(
        STDPLATFORMTEST_STACKTRACE_BANNER
        "  no stack trace on this platform\n",
        stdPlatformTest_aStackTrace
    );
#else
    const char* pFrame = &stdPlatformTest_aStackTrace[sizeof(STDPLATFORMTEST_STACKTRACE_BANNER) - 1u];
    TEST_ASSERT_EQUAL_MEMORY("  [0] ", pFrame, 6u);
    TEST_ASSERT_NULL(strstr(pFrame, "  [1] "));
#endif
}

TEST_GROUP_RUNNER(stdPlatform)
{
    RUN_TEST_CASE(stdPlatform, TestInitServicesPopulatesExpectedCallbacksAndClearZerosThem);
    RUN_TEST_CASE(stdPlatform, TestHandleAllocationReallocationFreeAndLock);
    RUN_TEST_CASE(stdPlatform, TestTimerPrintfAndDirExists);
    RUN_TEST_CASE(stdPlatform, TestTimeMsecDoesNotGoBackAcrossSleep);
    RUN_TEST_CASE(stdPlatform, TestZeroFrameStackTracePrintsOnlyTheBanner);
    RUN_TEST_CASE(stdPlatform, TestOneFrameStackTraceProducesFrameOutput);
}
