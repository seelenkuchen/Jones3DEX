// Restored from std/Win95/Tests/stdCommTest.c (82b8f607^), which ran only in the DirectX 9 build against
// the std/Win95/stdComm.c stubs. The return values describe the stdComm.h contract and run here against
// the backend every column links, Null/stdCommNull.c. What came from the DX9 stubs alone is gone: their
// fatal "<function> not implemented!" asserts (stdCommNull.c drops them by design: an inactive session is
// the expected state there), the session state RejoinSession switched on, the stdComm_aGames table and
// stdComm_GetGame (declared in stdComm.h, defined by no backend any more).
#include <unity_fixture.h>

#include <string.h>

#include <j3dcore/Tests/j3dTest.h>
#include <std/General/std.h>
#include <std/General/stdUtil.h>
#include <std/General/stdComm.h>

#include "stdGeneralTest.h"

TEST_GROUP(stdComm);

TEST_SETUP(stdComm)
{
    StdGeneralTest_Startup();
    stdComm_CloseGame();
    J3DTest_ResetAssertCapture();
}

TEST_TEAR_DOWN(stdComm)
{
    stdComm_CloseGame();
    J3DTest_ResetAssertCapture();
    StdGeneralTest_Shutdown();
}

TEST(stdComm, TestInactiveStateAndEmptyPlayerTable)
{
    TEST_ASSERT_EQUAL_UINT32(24u, STDCOMM_MAX_PLAYERS);
    TEST_ASSERT_EQUAL_UINT32(32u, STDCOMM_MAX_GAMES);
    // Dropped: sizeof(StdCommPlayerInfo) == 44 and sizeof(StdCommGame) == 356. They hold only where
    // wchar_t has 2 bytes; types.h no longer asserts them (J3D_ASSERT_SIZE32_WCHAR is empty) and neither
    // struct is read or written as bytes.
    TEST_ASSERT_FALSE(stdComm_IsGameActive());
    TEST_ASSERT_FALSE(stdComm_IsGameHost());
    TEST_ASSERT_EQUAL_size_t(0u, stdComm_GetNumPlayers());
    TEST_ASSERT_EQUAL_INT(1, stdComm_VerifyPlayer((DPID)1u));
}

// Was TestStubbedOperationsAssertAndReturnLegacyValues; the assert checks are dropped (see the top).
TEST(stdComm, TestStubbedOperationsReturnLegacyValues)
{
    DPID sender = 0;
    uint8_t data[8];
    size_t length = sizeof(data);

    TEST_ASSERT_EQUAL_INT(0, stdComm_Send(1u, 2u, data, sizeof(data), 0u));
    TEST_ASSERT_EQUAL_INT(-1, stdComm_Receive(&sender, data, &length));
    TEST_ASSERT_EQUAL_INT(0, stdComm_CreatePlayer(L"Player"));
}

// Was TestStubbedNullArgumentsAssertBeforeLegacyReturn; the assert checks are dropped (see the top), and so
// is GetSessionSettings(NULL) == 0: without an open session the Null backend answers 1 ("no settings").
TEST(stdComm, TestStubbedNullArgumentsReturnLegacyValues)
{
    TEST_ASSERT_EQUAL_INT(-1, stdComm_Receive(NULL, NULL, NULL));
    TEST_ASSERT_EQUAL_INT(0, stdComm_CreateGame(NULL));
}

// The assert checks are dropped (see the top), and so are two answers of the DX9 stubs the Null backend
// legitimately does not share: RejoinSession left IsGameActive TRUE, and GetSessionSettings returned 0.
TEST(stdComm, TestStubbedGameAndSessionOperations)
{
    StdCommGame game;
    DPID playerId = 0;

    STD_ZEROMEM(&game, sizeof(game));

    TEST_ASSERT_EQUAL_INT(0, stdComm_CreateGame(&game));
    TEST_ASSERT_EQUAL_INT(0, stdComm_SetGameParams(&game));
    TEST_ASSERT_EQUAL_INT(0, stdComm_JoinGame(0u, L""));
    TEST_ASSERT_EQUAL_INT(0, stdComm_RejoinSession(&playerId, L"Player"));
    TEST_ASSERT_FALSE(stdComm_IsGameHost());
}

// The assert checks of DestroyPlayer, its only observable on the DX9 stubs, are dropped (see the top).
TEST(stdComm, TestStubbedPlayerHelpers)
{
    TEST_ASSERT_EQUAL_INT(-1, stdComm_UpdatePlayers(0u));
    TEST_ASSERT_EQUAL_UINT32(0u, stdComm_GetPlayerID(0u));
}

// Was TestCloseGameClearsStateAndReportsStub; the assert check is dropped (see the top).
TEST(stdComm, TestCloseGameClearsState)
{
    stdComm_CloseGame();

    TEST_ASSERT_FALSE(stdComm_IsGameActive());
    TEST_ASSERT_FALSE(stdComm_IsGameHost());
}

TEST_GROUP_RUNNER(stdComm)
{
    RUN_TEST_CASE(stdComm, TestInactiveStateAndEmptyPlayerTable);
    RUN_TEST_CASE(stdComm, TestStubbedOperationsReturnLegacyValues);
    RUN_TEST_CASE(stdComm, TestStubbedNullArgumentsReturnLegacyValues);
    RUN_TEST_CASE(stdComm, TestStubbedGameAndSessionOperations);
    RUN_TEST_CASE(stdComm, TestStubbedPlayerHelpers);
    RUN_TEST_CASE(stdComm, TestCloseGameClearsState);
}
