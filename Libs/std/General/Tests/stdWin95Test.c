// Restored from std/Win95/Tests/stdWin95Test.c (82b8f607^), which ran only in the Win32/DirectX build
// against std/Win95/stdWin95.c. The cases describe the stdWin95.h contract, so they run here against the
// accessors every column links, Null/stdWin95Null.c. The Windows types HWND, HINSTANCE and GUID became
// the shared tSysWindow, tSysInstance and tSysGuid; the GUID values are the same 16 bytes in the
// little-endian layout of a Windows GUID.
#include <unity_fixture.h>

#include <stdint.h>
#include <string.h>

#include <j3dcore/Tests/j3dTest.h>
#include <std/General/stdUtil.h>
#include <std/General/stdWin95.h>

#include "stdGeneralTest.h"

TEST_GROUP(stdWin95);

TEST_SETUP(stdWin95)
{
    StdGeneralTest_Startup();
    stdWin95_SetWindow(NULL);
    stdWin95_SetInstance(NULL);

    tSysGuid guid = { { 0 } };
    stdWin95_SetGuid(&guid);
}

TEST_TEAR_DOWN(stdWin95)
{
    stdWin95_SetWindow(NULL);
    stdWin95_SetInstance(NULL);
    StdGeneralTest_Shutdown();
}

TEST(stdWin95, TestWindowAndInstanceAccessors)
{
    tSysWindow hwnd        = (tSysWindow)(uintptr_t)0x1234u;
    tSysInstance hInstance = (tSysInstance)(uintptr_t)0x5678u;

    stdWin95_SetWindow(hwnd);
    stdWin95_SetInstance(hInstance);

    TEST_ASSERT_EQUAL_PTR(hwnd, stdWin95_GetWindow());
    TEST_ASSERT_EQUAL_PTR(hInstance, stdWin95_GetInstance());
}

TEST(stdWin95, TestGuidAccessorCopiesInput)
{
    // { 0x12345678, 0x9ABC, 0xDEF0, { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 } }
    tSysGuid guid =
    {
        {
            0x78u, 0x56u, 0x34u, 0x12u, 0xBCu, 0x9Au, 0xF0u, 0xDEu,
            0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u, 0x77u, 0x88u
        }
    };
    tSysGuid changedGuid = guid;

    stdWin95_SetGuid(&guid);
    STD_ZEROMEM(changedGuid.aData, 4u); // Data1 = 0

    TEST_ASSERT_NOT_NULL(stdWin95_GetGuid());
    TEST_ASSERT_EQUAL_MEMORY(&guid, stdWin95_GetGuid(), sizeof(guid));
    TEST_ASSERT_NOT_EQUAL(0, memcmp(changedGuid.aData, stdWin95_GetGuid()->aData, 4u));
}

TEST(stdWin95, TestGuidAccessorKeepsStableStorageAndLatestValue)
{
    // { 0x11111111, 0x2222, 0x3333, { 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB } }
    tSysGuid firstGuid =
    {
        {
            0x11u, 0x11u, 0x11u, 0x11u, 0x22u, 0x22u, 0x33u, 0x33u,
            0x44u, 0x55u, 0x66u, 0x77u, 0x88u, 0x99u, 0xAAu, 0xBBu
        }
    };
    // { 0x01020304, 0x0506, 0x0708, { 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10 } }
    tSysGuid secondGuid =
    {
        {
            0x04u, 0x03u, 0x02u, 0x01u, 0x06u, 0x05u, 0x08u, 0x07u,
            0x09u, 0x0Au, 0x0Bu, 0x0Cu, 0x0Du, 0x0Eu, 0x0Fu, 0x10u
        }
    };

    stdWin95_SetGuid(&firstGuid);
    const tSysGuid* pGuid = stdWin95_GetGuid();

    TEST_ASSERT_EQUAL_MEMORY(&firstGuid, pGuid, sizeof(firstGuid));

    stdWin95_SetGuid(&secondGuid);

    TEST_ASSERT_EQUAL_PTR(pGuid, stdWin95_GetGuid());
    TEST_ASSERT_EQUAL_MEMORY(&secondGuid, stdWin95_GetGuid(), sizeof(secondGuid));
}

TEST_GROUP_RUNNER(stdWin95)
{
    RUN_TEST_CASE(stdWin95, TestWindowAndInstanceAccessors);
    RUN_TEST_CASE(stdWin95, TestGuidAccessorCopiesInput);
    RUN_TEST_CASE(stdWin95, TestGuidAccessorKeepsStableStorageAndLatestValue);
}
