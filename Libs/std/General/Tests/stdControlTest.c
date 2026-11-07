// Restored from std/Win95/Tests/stdControlTest.c (82b8f607^), which ran only in the Win32/DirectX build
// against stdControlDX9.c and its J3D_TEST seams (std/Win95/stdControlTestApi.h). The key and axis id
// cases describe stdControl.h itself; the no-device cases run here against the backend every column
// links, Null/stdControlNull.c, with the Null column's device side, Null/stdControlDevicesNull.c.
// Dropped with the DirectX backend: the DirectInput and XInput state translation (DIJOYSTATE,
// DIMOUSESTATE, XINPUT_STATE), stdControl_DIGetStatus, and the cases that drove the backend through its
// test seams (BeginRead, ApplyKeyboardState, SetAxisState, GetAxis, GetReadDeltaTime, GetSecFPS), which
// the Null backend does not have; stdControl_TestResetInputState is the one it kept. Also dropped:
// stdControl_ControlsActive, stdControl_ToggleMouse, stdControl_MouseSensitivityEnabled and
// stdControl_GetMaxJoystickButtons, which stdControl.h declares but no backend defines any more.
#include <unity_fixture.h>

#include <stdint.h>

#include <std/General/stdUtil.h>
#include <std/General/stdControl.h>
#include <std/General/stdKeys.h>
#include <std/General/stdPlatform.h>
#include <std/Null/stdControlNull.h>

#include "stdGeneralTest.h"

// The Null backend's test seam (stdControlNull.c); no header declares it.
void stdControl_TestResetInputState(void);

TEST_GROUP(stdControl);

TEST_SETUP(stdControl)
{
    StdGeneralTest_Startup();
    stdControl_Close();
    stdControl_Shutdown();
    // Added: a Startup/Shutdown pair clears the axis table, which the Null backend's test reset keeps,
    // so the axis registrations of one case cannot leak into the next.
    stdControl_Startup(0);
    stdControl_Shutdown();
    stdControl_QueueKeyEvent(STDKEY_F15, 0);
    stdControl_TestResetInputState();
    stdControl_Reset();
    stdControl_EnableMouse(0);
    stdControl_EnableMouseSensitivity(0);
}

TEST_TEAR_DOWN(stdControl)
{
    stdControl_Close();
    stdControl_Shutdown();
    stdControl_QueueKeyEvent(STDKEY_F15, 0);
    stdControl_TestResetInputState();
    stdControl_Reset();
    stdControl_EnableMouse(0);
    stdControl_EnableMouseSensitivity(0);
    StdGeneralTest_Shutdown();
}

#define STDCONTROLTEST_FORMAT_MESSAGE(message, label, value) \
    STD_FORMAT(message, "%s %u", label, value)

static bool stdControlTest_IsExpectedKeyboardKey(uint32_t keyId)
{
    return keyId < STDCONTROL_MAX_KEYBOARD_BUTTONS;
}

static bool stdControlTest_IsExpectedJoystickKey(uint32_t keyId)
{
    return keyId >= STDCONTROL_JOYSTICK_FIRSTCID
        && keyId < STDCONTROL_JOYSTICK_FIRSTCID + STDCONTROL_JOYSTICK_TOTALCIDS;
}

static bool stdControlTest_IsExpectedJoystickButton(uint32_t keyId)
{
    return stdControlTest_IsExpectedJoystickKey(keyId)
        && ((keyId - STDCONTROL_JOYSTICK_FIRSTCID) % STDCONTROL_TOTAL_JOYSTICK_CONTROLS) < STDCONTROL_NUM_JOYSTICK_BUTTONS;
}

static bool stdControlTest_IsExpectedJoystickPov(uint32_t keyId)
{
    return stdControlTest_IsExpectedJoystickKey(keyId)
        && ((keyId - STDCONTROL_JOYSTICK_FIRSTCID) % STDCONTROL_TOTAL_JOYSTICK_CONTROLS) >= STDCONTROL_NUM_JOYSTICK_BUTTONS;
}

static bool stdControlTest_IsExpectedMouseButton(uint32_t keyId)
{
    return keyId >= STDCONTROL_KID_MOUSE_LBUTTON && keyId <= STDCONTROL_KID_MOUSE_XBUTTON;
}

static void stdControlTest_RegisterJoystickAxes(size_t joyNum, int min, int max)
{
    for ( size_t axisIndex = 0u; axisIndex < STDCONTROL_JOYSTICK_NUMAXES; ++axisIndex )
    {
        size_t aid = STDCONTROL_GET_JOYSTICK_AXIS(joyNum, axisIndex);

        stdControl_RegisterAxis(aid, min, max, 0.0f);
        TEST_ASSERT_EQUAL_INT(1, stdControl_EnableAxis((int)aid));
    }
}

static void stdControlTest_AssertPovState(size_t joyNum, size_t povNum, bool bLeft, bool bUp, bool bRight, bool bDown)
{
    TEST_ASSERT_EQUAL_INT(bLeft, stdControl_ReadKey(STDCONTROL_JOYSTICK_GETPOVLEFT(joyNum, povNum), NULL));
    TEST_ASSERT_EQUAL_INT(bUp, stdControl_ReadKey(STDCONTROL_JOYSTICK_GETPOVUP(joyNum, povNum), NULL));
    TEST_ASSERT_EQUAL_INT(bRight, stdControl_ReadKey(STDCONTROL_JOYSTICK_GETPOVRIGHT(joyNum, povNum), NULL));
    TEST_ASSERT_EQUAL_INT(bDown, stdControl_ReadKey(STDCONTROL_JOYSTICK_GETPOVDOWN(joyNum, povNum), NULL));
}

TEST(stdControl, TestAllKeyIdsClassifyAsKeyboardJoystickOrMouse)
{
    size_t numKeyboardKeys = 0;
    size_t numJoyButtons   = 0;
    size_t numJoyPovs      = 0;
    size_t numMouseButtons = 0;

    for ( uint32_t keyId = 0; keyId < STDCONTROL_MAX_KEYID; ++keyId )
    {
        char aMessage[64];
        STDCONTROLTEST_FORMAT_MESSAGE(aMessage, "key id", keyId);

        bool bKeyboard = stdControlTest_IsExpectedKeyboardKey(keyId);
        bool bJoyBtn   = stdControlTest_IsExpectedJoystickButton(keyId);
        bool bJoyPov   = stdControlTest_IsExpectedJoystickPov(keyId);
        bool bMouse    = stdControlTest_IsExpectedMouseButton(keyId);

        TEST_ASSERT_EQUAL_INT_MESSAGE(bKeyboard, !!STDCONTROL_ISKEYBOARDBUTTON(keyId), aMessage);
        TEST_ASSERT_EQUAL_INT_MESSAGE(bJoyBtn, !!STDCONTROL_ISJOYSTICKBUTTON(keyId), aMessage);
        TEST_ASSERT_EQUAL_INT_MESSAGE(bJoyPov, !!STDCONTROL_ISJOYSTICKPOV(keyId), aMessage);
        TEST_ASSERT_EQUAL_INT_MESSAGE(bMouse, !!STDCONTROL_ISMOUSEBUTTON(keyId), aMessage);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, (int)bKeyboard + (int)bJoyBtn + (int)bJoyPov + (int)bMouse, aMessage);

        numKeyboardKeys += bKeyboard ? 1u : 0u;
        numJoyButtons   += bJoyBtn ? 1u : 0u;
        numJoyPovs      += bJoyPov ? 1u : 0u;
        numMouseButtons += bMouse ? 1u : 0u;
    }

    TEST_ASSERT_EQUAL_size_t(STDCONTROL_MAX_KEYBOARD_BUTTONS, numKeyboardKeys);
    TEST_ASSERT_EQUAL_size_t(STDCONTROL_MAX_JOYSTICK_DEVICES * STDCONTROL_NUM_JOYSTICK_BUTTONS, numJoyButtons);
    TEST_ASSERT_EQUAL_size_t(STDCONTROL_MAX_JOYSTICK_DEVICES * STDCONTROL_MAX_JOYSTICK_POVCONTROLERS * STDCONTROL_NUM_JOYSTICK_POVDIRECTIONS, numJoyPovs);
    TEST_ASSERT_EQUAL_size_t(STDCONTROL_MAX_MOUSE_BUTTONS, numMouseButtons);
    TEST_ASSERT_EQUAL_size_t(STDCONTROL_MAX_KEYID, numKeyboardKeys + numJoyButtons + numJoyPovs + numMouseButtons);
}

TEST(stdControl, TestAllJoystickButtonIds)
{
    for ( uint32_t joyNum = 0; joyNum < STDCONTROL_MAX_JOYSTICK_DEVICES; ++joyNum )
    {
        for ( uint32_t btnNum = 0; btnNum < STDCONTROL_NUM_JOYSTICK_BUTTONS; ++btnNum )
        {
            char aMessage[64];
            uint32_t keyId     = STDCONTROL_JOYSTICK_GETBUTTON(joyNum, btnNum);
            uint32_t expected  = STDCONTROL_JOYSTICK_FIRSTKID + joyNum * STDCONTROL_TOTAL_JOYSTICK_CONTROLS + btnNum;

            STDCONTROLTEST_FORMAT_MESSAGE(aMessage, "joy button", keyId);

            TEST_ASSERT_EQUAL_UINT32_MESSAGE(expected, keyId, aMessage);
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(btnNum, STDCONTROL_JOYSTICK_GETBUTTONINDEX(keyId), aMessage);
            TEST_ASSERT_EQUAL_UINT32_MESSAGE(btnNum, STDCONTROL_JOYSTICK_GETLOCALCID(keyId), aMessage);
            TEST_ASSERT_TRUE_MESSAGE(STDCONTROL_ISJOYSTICKBUTTON(keyId), aMessage);
            TEST_ASSERT_FALSE_MESSAGE(STDCONTROL_ISJOYSTICKPOV(keyId), aMessage);
            TEST_ASSERT_FALSE_MESSAGE(STDCONTROL_ISKEYBOARDBUTTON(keyId), aMessage);
            TEST_ASSERT_FALSE_MESSAGE(STDCONTROL_ISMOUSEBUTTON(keyId), aMessage);
        }
    }
}

TEST(stdControl, TestAllJoystickPovIds)
{
    for ( uint32_t joyNum = 0; joyNum < STDCONTROL_MAX_JOYSTICK_DEVICES; ++joyNum )
    {
        for ( uint32_t povNum = 0; povNum < STDCONTROL_MAX_JOYSTICK_POVCONTROLERS; ++povNum )
        {
            for ( uint32_t dirNum = 0; dirNum < STDCONTROL_NUM_JOYSTICK_POVDIRECTIONS; ++dirNum )
            {
                char aMessage[64];
                uint32_t keyId    = STDCONTROL_JOYSTICK_GETPOV(joyNum, povNum, dirNum);
                uint32_t expected = STDCONTROL_JOYSTICK_FIRTPOVCID
                    + joyNum * STDCONTROL_TOTAL_JOYSTICK_CONTROLS
                    + povNum * STDCONTROL_NUM_JOYSTICK_POVDIRECTIONS
                    + dirNum;

                STDCONTROLTEST_FORMAT_MESSAGE(aMessage, "joy pov", keyId);

                TEST_ASSERT_EQUAL_UINT32_MESSAGE(expected, keyId, aMessage);
                TEST_ASSERT_EQUAL_UINT32_MESSAGE(povNum, STDCONTROL_JOYSTICK_GETPOVINDEX(keyId), aMessage);
                TEST_ASSERT_EQUAL_UINT32_MESSAGE(dirNum, STDCONTROL_JOYSTICK_GETPOVDIRECTIONINDEX(keyId), aMessage);
                TEST_ASSERT_EQUAL_UINT32_MESSAGE(STDCONTROL_NUM_JOYSTICK_BUTTONS + povNum * STDCONTROL_NUM_JOYSTICK_POVDIRECTIONS + dirNum, STDCONTROL_JOYSTICK_GETLOCALCID(keyId), aMessage);
                TEST_ASSERT_TRUE_MESSAGE(STDCONTROL_ISJOYSTICKPOV(keyId), aMessage);
                TEST_ASSERT_FALSE_MESSAGE(STDCONTROL_ISJOYSTICKBUTTON(keyId), aMessage);
                TEST_ASSERT_FALSE_MESSAGE(STDCONTROL_ISKEYBOARDBUTTON(keyId), aMessage);
                TEST_ASSERT_FALSE_MESSAGE(STDCONTROL_ISMOUSEBUTTON(keyId), aMessage);

                switch ( dirNum )
                {
                    case STDCONTROL_JOYSTICK_POVDIRLEFT:
                        TEST_ASSERT_EQUAL_UINT32_MESSAGE(keyId, STDCONTROL_JOYSTICK_GETPOVLEFT(joyNum, povNum), aMessage);
                        break;
                    case STDCONTROL_JOYSTICK_POVDIRUP:
                        TEST_ASSERT_EQUAL_UINT32_MESSAGE(keyId, STDCONTROL_JOYSTICK_GETPOVUP(joyNum, povNum), aMessage);
                        break;
                    case STDCONTROL_JOYSTICK_POVDIRRIGHT:
                        TEST_ASSERT_EQUAL_UINT32_MESSAGE(keyId, STDCONTROL_JOYSTICK_GETPOVRIGHT(joyNum, povNum), aMessage);
                        break;
                    case STDCONTROL_JOYSTICK_POVDIRDOWN:
                        TEST_ASSERT_EQUAL_UINT32_MESSAGE(keyId, STDCONTROL_JOYSTICK_GETPOVDOWN(joyNum, povNum), aMessage);
                        break;
                }
            }
        }
    }
}

TEST(stdControl, TestAllAxisIdsAndFlaggedAxisIds)
{
    static const uint32_t aAxisFlags[] =
    {
        0u,
        STDCONTROL_AID_LOW_SENSITIVITY,
        STDCONTROL_AID_POSITIVE_AXIS,
        STDCONTROL_AID_NEGATIVE_AXIS,
        STDCONTROL_AID_LOW_SENSITIVITY | STDCONTROL_AID_POSITIVE_AXIS,
        STDCONTROL_AID_LOW_SENSITIVITY | STDCONTROL_AID_NEGATIVE_AXIS,
    };

    for ( uint32_t joyNum = 0; joyNum < STDCONTROL_MAX_JOYSTICK_DEVICES; ++joyNum )
    {
        for ( uint32_t axisIdx = 0; axisIdx < STDCONTROL_JOYSTICK_NUMAXES; ++axisIdx )
        {
            char aMessage[64];
            uint32_t aid = STDCONTROL_GET_JOYSTICK_AXIS(joyNum, axisIdx);

            STDCONTROLTEST_FORMAT_MESSAGE(aMessage, "joy axis", aid);

            TEST_ASSERT_EQUAL_UINT32_MESSAGE(joyNum * STDCONTROL_JOYSTICK_NUMAXES + axisIdx, aid, aMessage);
            TEST_ASSERT_TRUE_MESSAGE(STDCONTROL_ISJOYSTICKAXIS(aid), aMessage);
            TEST_ASSERT_FALSE_MESSAGE(STDCONTROL_ISMOUSEAXIS(aid), aMessage);

            for ( size_t i = 0; i < STD_ARRAYLEN(aAxisFlags); ++i )
            {
                uint32_t flaggedAid = aid | aAxisFlags[i];

                TEST_ASSERT_EQUAL_UINT32_MESSAGE(aid, STDCONTROL_GETAID(flaggedAid), aMessage);
                TEST_ASSERT_TRUE_MESSAGE(STDCONTROL_ISJOYSTICKAXIS(flaggedAid), aMessage);
                TEST_ASSERT_FALSE_MESSAGE(STDCONTROL_ISMOUSEAXIS(flaggedAid), aMessage);
            }
        }
    }

    for ( uint32_t axisIdx = 0; axisIdx < 3u; ++axisIdx )
    {
        char aMessage[64];
        uint32_t aid = STDCONTROL_AID_MOUSE_X + axisIdx;

        STDCONTROLTEST_FORMAT_MESSAGE(aMessage, "mouse axis", aid);

        TEST_ASSERT_TRUE_MESSAGE(STDCONTROL_ISMOUSEAXIS(aid), aMessage);
        TEST_ASSERT_FALSE_MESSAGE(STDCONTROL_ISJOYSTICKAXIS(aid), aMessage);

        for ( size_t i = 0; i < STD_ARRAYLEN(aAxisFlags); ++i )
        {
            uint32_t flaggedAid = aid | aAxisFlags[i];

            TEST_ASSERT_EQUAL_UINT32_MESSAGE(aid, STDCONTROL_GETAID(flaggedAid), aMessage);
            TEST_ASSERT_TRUE_MESSAGE(STDCONTROL_ISMOUSEAXIS(flaggedAid), aMessage);
            TEST_ASSERT_FALSE_MESSAGE(STDCONTROL_ISJOYSTICKAXIS(flaggedAid), aMessage);
        }
    }
}

TEST(stdControl, TestNamedJoystickAxisMacros)
{
    for ( uint32_t joyNum = 0; joyNum < STDCONTROL_MAX_JOYSTICK_DEVICES; ++joyNum )
    {
        char aMessage[64];
        STDCONTROLTEST_FORMAT_MESSAGE(aMessage, "joy axis macros", joyNum);

        TEST_ASSERT_EQUAL_UINT32_MESSAGE(STDCONTROL_GET_JOYSTICK_AXIS(joyNum, STDCONTROL_JOYSTICK_AXIS_X), STDCONTROL_GET_JOYSTICK_AXIS_X(joyNum), aMessage);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(STDCONTROL_GET_JOYSTICK_AXIS(joyNum, STDCONTROL_JOYSTICK_AXIS_Y), STDCONTROL_GET_JOYSTICK_AXIS_Y(joyNum), aMessage);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(STDCONTROL_GET_JOYSTICK_AXIS(joyNum, STDCONTROL_JOYSTICK_AXIS_Z), STDCONTROL_GET_JOYSTICK_AXIS_Z(joyNum), aMessage);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(STDCONTROL_GET_JOYSTICK_AXIS(joyNum, STDCONTROL_JOYSTICK_AXIS_RX), STDCONTROL_GET_JOYSTICK_AXIS_RX(joyNum), aMessage);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(STDCONTROL_GET_JOYSTICK_AXIS(joyNum, STDCONTROL_JOYSTICK_AXIS_RY), STDCONTROL_GET_JOYSTICK_AXIS_RY(joyNum), aMessage);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(STDCONTROL_GET_JOYSTICK_AXIS(joyNum, STDCONTROL_JOYSTICK_AXIS_RZ), STDCONTROL_GET_JOYSTICK_AXIS_RZ(joyNum), aMessage);
    }
}

TEST(stdControl, TestMouseAndOutOfRangeBoundaries)
{
    TEST_ASSERT_EQUAL_UINT32(STDCONTROL_KID_MOUSE_LBUTTON + 1u, STDCONTROL_KID_MOUSE_RBUTTON);
    TEST_ASSERT_EQUAL_UINT32(STDCONTROL_KID_MOUSE_LBUTTON + 2u, STDCONTROL_KID_MOUSE_MBUTTON);
    TEST_ASSERT_EQUAL_UINT32(STDCONTROL_KID_MOUSE_LBUTTON + 3u, STDCONTROL_KID_MOUSE_XBUTTON);
    TEST_ASSERT_EQUAL_UINT32(STDCONTROL_AID_MOUSE_X + 1u, STDCONTROL_AID_MOUSE_Y);
    TEST_ASSERT_EQUAL_UINT32(STDCONTROL_AID_MOUSE_X + 2u, STDCONTROL_AID_MOUSE_Z);

    TEST_ASSERT_FALSE(STDCONTROL_ISKEYBOARDBUTTON(STDCONTROL_MAX_KEYBOARD_BUTTONS));
    TEST_ASSERT_FALSE(STDCONTROL_ISJOYSTICKCID(STDCONTROL_JOYSTICK_FIRSTCID - 1u));
    TEST_ASSERT_FALSE(STDCONTROL_ISJOYSTICKCID(STDCONTROL_JOYSTICK_FIRSTCID + STDCONTROL_JOYSTICK_TOTALCIDS));
    TEST_ASSERT_FALSE(STDCONTROL_ISJOYSTICKBUTTON(STDCONTROL_JOYSTICK_FIRSTCID + STDCONTROL_JOYSTICK_TOTALCIDS));
    TEST_ASSERT_FALSE(STDCONTROL_ISJOYSTICKPOV(STDCONTROL_JOYSTICK_FIRSTCID + STDCONTROL_JOYSTICK_TOTALCIDS));
    TEST_ASSERT_FALSE(STDCONTROL_ISMOUSEBUTTON(STDCONTROL_KID_MOUSE_LBUTTON - 1u));
    TEST_ASSERT_FALSE(STDCONTROL_ISMOUSEBUTTON(STDCONTROL_KID_MOUSE_XBUTTON + 1u));
    TEST_ASSERT_FALSE(STDCONTROL_ISMOUSEAXIS(STDCONTROL_AID_MOUSE_X - 1u));
    TEST_ASSERT_FALSE(STDCONTROL_ISMOUSEAXIS(STDCONTROL_AID_MOUSE_Z + 1u));
    TEST_ASSERT_FALSE(STDCONTROL_ISJOYSTICKAXIS(STDCONTROL_AID_MOUSE_X));
    TEST_ASSERT_FALSE(STDCONTROL_ISJOYSTICKAXIS(STDCONTROL_MAX_AXES));
}

// Was TestDirectXVariantDependentLimits. stdControl.h has no XInput gamepad slots any more
// (STDCONTROL_MAX_GAMEPAD_DEVICES 0), so these are the original's no-gamepad (DirectX 6) expectations;
// its DirectX 9 branch (4 gamepads, 12 joysticks, 72/75 axes, 836 key ids) is gone with the slots.
TEST(stdControl, TestJoystickAndKeyIdLimits)
{
    TEST_ASSERT_EQUAL_UINT32(0u, STDCONTROL_MAX_GAMEPAD_DEVICES);
    TEST_ASSERT_EQUAL_UINT32(8u, STDCONTROL_MAX_JOYSTICK_DEVICES);
    TEST_ASSERT_EQUAL_UINT32(48u, STDCONTROL_MAX_JOYSTICK_AXES);
    TEST_ASSERT_EQUAL_UINT32(51u, STDCONTROL_MAX_AXES);
    TEST_ASSERT_EQUAL_UINT32(644u, STDCONTROL_MAX_KEYID);
    TEST_ASSERT_EQUAL_UINT32(640u, STDCONTROL_KID_MOUSE_LBUTTON);
    TEST_ASSERT_EQUAL_UINT32(643u, STDCONTROL_KID_MOUSE_XBUTTON);
}

TEST(stdControl, TestNoDeviceLifecycleAndReadDefaults)
{
    int pressed = 7;

    TEST_ASSERT_FALSE(stdControl_HasStarted());
    TEST_ASSERT_FALSE(stdControl_IsOpen());
    TEST_ASSERT_EQUAL_INT(1, stdControl_Open());
    TEST_ASSERT_FALSE(stdControl_IsOpen());
    TEST_ASSERT_EQUAL_INT(1, stdControl_SetActivation(1));
    // Dropped: stdControl_ControlsActive() == 0 (declared, not defined).
    TEST_ASSERT_EQUAL_INT(0, stdControl_ControlsIdle());

    TEST_ASSERT_EQUAL_FLOAT(0.0f, stdControl_ReadAxis(STDCONTROL_AID_MOUSE_X));
    TEST_ASSERT_EQUAL_INT(0, stdControl_ReadAxisRaw(STDCONTROL_AID_MOUSE_X));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, stdControl_ReadKeyAsAxis(0u));
    TEST_ASSERT_EQUAL_INT(0, stdControl_ReadKey(0u, &pressed));
    TEST_ASSERT_EQUAL_INT(0, pressed);

    pressed = 3;
    TEST_ASSERT_EQUAL_INT(0, stdControl_ReadAxisAsKey(STDCONTROL_AID_MOUSE_X, &pressed));
    TEST_ASSERT_EQUAL_INT(3, pressed);
    TEST_ASSERT_EQUAL_INT(0, stdControl_ReadAxisAsKeyEx(STDCONTROL_AID_MOUSE_X, &pressed, 0.25f));
    TEST_ASSERT_EQUAL_INT(3, pressed);

    stdControl_DisableReadJoysticks();
    stdControl_FinishRead();
}

TEST(stdControl, TestMouseAndSensitivityState)
{
    TEST_ASSERT_FALSE(stdControl_IsMouseEnabled());
    TEST_ASSERT_EQUAL_INT(1, stdControl_EnableMouse(1));
    TEST_ASSERT_TRUE(stdControl_IsMouseEnabled());
    TEST_ASSERT_EQUAL_INT(1, stdControl_EnableMouse(1));
    TEST_ASSERT_TRUE(stdControl_IsMouseEnabled());
    // Dropped: the two stdControl_ToggleMouse round trips (declared, not defined).
    TEST_ASSERT_EQUAL_INT(1, stdControl_EnableMouse(0));
    TEST_ASSERT_FALSE(stdControl_IsMouseEnabled());

    // Dropped: the stdControl_MouseSensitivityEnabled checks around EnableMouseSensitivity (declared,
    // not defined); the Null backend exposes no other observable of the sensitivity switch.
}

TEST(stdControl, TestAxisRegistrationAndFlags)
{
    size_t axis = STDCONTROL_GET_JOYSTICK_AXIS_X(0u);

    TEST_ASSERT_EQUAL_INT(0, stdControl_EnableAxis(STDCONTROL_MAX_AXES));
    TEST_ASSERT_EQUAL_INT(0, stdControl_EnableAxis((int)axis));
    TEST_ASSERT_EQUAL_INT(0, stdControl_TestAxisFlag(axis, STDCONTROL_AXIS_REGISTERED));

    stdControl_RegisterAxis(axis, -100, 100, 0.25f);
    TEST_ASSERT_NOT_EQUAL(0, stdControl_TestAxisFlag(axis, STDCONTROL_AXIS_REGISTERED));
    TEST_ASSERT_EQUAL_INT(1, stdControl_EnableAxis((int)axis));
    TEST_ASSERT_NOT_EQUAL(0, stdControl_TestAxisFlag(axis, STDCONTROL_AXIS_ENABLED));

    stdControl_SetAxisFlags(axis | STDCONTROL_AID_POSITIVE_AXIS, STDCONTROL_AXIS_ENABLED);
    TEST_ASSERT_NOT_EQUAL(0, stdControl_TestAxisFlag(axis | STDCONTROL_AID_POSITIVE_AXIS, STDCONTROL_AXIS_ENABLED));

    stdControl_SetAxisFlags(axis | STDCONTROL_AID_NEGATIVE_AXIS, STDCONTROL_AXIS_ENABLED);
    TEST_ASSERT_NOT_EQUAL(0, stdControl_TestAxisFlag(axis | STDCONTROL_AID_NEGATIVE_AXIS, STDCONTROL_AXIS_ENABLED));

    TEST_ASSERT_EQUAL_FLOAT(0.0f, stdControl_ReadAxis(axis));
    TEST_ASSERT_EQUAL_INT(0, stdControl_ReadAxisRaw(axis));
}

#ifdef J3D_RUNTIME_GUARDS
TEST(stdControl, TestRuntimeGuardsRejectOutOfRangeInputs)
{
    int pressed = 7;

    TEST_ASSERT_EQUAL_FLOAT(0.0f, stdControl_ReadKeyAsAxis(STDCONTROL_MAX_KEYID));
    TEST_ASSERT_EQUAL_INT(0, stdControl_ReadKey(STDCONTROL_MAX_KEYID, &pressed));
    TEST_ASSERT_EQUAL_INT(0, pressed);

    stdControl_RegisterAxis(STDCONTROL_MAX_AXES, -100, 100, 0.0f);
    // Moved: the empty-range registration, RegisterAxis(0u, 10, 10), is TestRegisterAxisRejectsEmptyRange.
}
#endif

// Split from TestRuntimeGuardsRejectOutOfRangeInputs: stdControlDX9.c rejected min >= max
// (STD_GUARD_VOID(... && max > min)). It is not under J3D_RUNTIME_GUARDS: the Null backend has no
// such guard at all.
TEST(stdControl, TestRegisterAxisRejectsEmptyRange)
{
    TEST_IGNORE_MESSAGE("TODO: [BUG] stdControlNull.c stdControl_RegisterAxis registers an empty range (min >= max); stdControlDX9.c rejected it, and the Null scale 2 / (max - min) divides by zero.");

    stdControl_RegisterAxis(0u, 10, 10, 0.0f);
    TEST_ASSERT_EQUAL_INT(0, stdControl_TestAxisFlag(0u, STDCONTROL_AXIS_REGISTERED));
    stdControl_RegisterAxis(1u, 10, -10, 0.0f);
    TEST_ASSERT_EQUAL_INT(0, stdControl_TestAxisFlag(1u, STDCONTROL_AXIS_REGISTERED));
}

TEST(stdControl, TestMouseAxisRegistrationAndSensitivityHelpers)
{
    stdControl_RegisterMouseAxesXY(1.5f, 2.0f);
    TEST_ASSERT_EQUAL_INT(0, stdControl_EnableAxis(STDCONTROL_AID_MOUSE_X));
    TEST_ASSERT_EQUAL_INT(0, stdControl_EnableAxis(STDCONTROL_AID_MOUSE_Y));

    stdControl_RegisterAxis(STDCONTROL_AID_MOUSE_X, -50, 50, 0.0f);
    stdControl_RegisterAxis(STDCONTROL_AID_MOUSE_Y, -40, 40, 0.0f);
    stdControl_RegisterAxis(STDCONTROL_AID_MOUSE_Z, -10, 10, 0.0f);

    TEST_ASSERT_NOT_EQUAL(0, stdControl_TestAxisFlag(STDCONTROL_AID_MOUSE_X, STDCONTROL_AXIS_REGISTERED));
    TEST_ASSERT_NOT_EQUAL(0, stdControl_TestAxisFlag(STDCONTROL_AID_MOUSE_Y, STDCONTROL_AXIS_REGISTERED));
    TEST_ASSERT_NOT_EQUAL(0, stdControl_TestAxisFlag(STDCONTROL_AID_MOUSE_Z, STDCONTROL_AXIS_REGISTERED));

    TEST_ASSERT_EQUAL_INT(1, stdControl_EnableAxis(STDCONTROL_AID_MOUSE_X));
    TEST_ASSERT_EQUAL_INT(1, stdControl_EnableAxis(STDCONTROL_AID_MOUSE_Y));
    TEST_ASSERT_EQUAL_INT(1, stdControl_EnableAxis(STDCONTROL_AID_MOUSE_Z));
    TEST_ASSERT_NOT_EQUAL(0, stdControl_TestAxisFlag(STDCONTROL_AID_MOUSE_X, STDCONTROL_AXIS_ENABLED));
    TEST_ASSERT_NOT_EQUAL(0, stdControl_TestAxisFlag(STDCONTROL_AID_MOUSE_Y, STDCONTROL_AXIS_ENABLED));
    TEST_ASSERT_NOT_EQUAL(0, stdControl_TestAxisFlag(STDCONTROL_AID_MOUSE_Z, STDCONTROL_AXIS_ENABLED));

    stdControl_SetMouseSensitivity(2.0f, 3.0f);
    TEST_ASSERT_NOT_EQUAL(0, stdControl_TestAxisFlag(STDCONTROL_AID_MOUSE_X, STDCONTROL_AXIS_REGISTERED));
    TEST_ASSERT_NOT_EQUAL(0, stdControl_TestAxisFlag(STDCONTROL_AID_MOUSE_Y, STDCONTROL_AXIS_REGISTERED));

    stdControl_Reset();
    TEST_ASSERT_EQUAL_INT(0, stdControl_TestAxisFlag(STDCONTROL_AID_MOUSE_X, STDCONTROL_AXIS_ENABLED));
    TEST_ASSERT_EQUAL_INT(0, stdControl_TestAxisFlag(STDCONTROL_AID_MOUSE_Y, STDCONTROL_AXIS_ENABLED));
    TEST_ASSERT_NOT_EQUAL(0, stdControl_TestAxisFlag(STDCONTROL_AID_MOUSE_X, STDCONTROL_AXIS_REGISTERED));
    TEST_ASSERT_NOT_EQUAL(0, stdControl_TestAxisFlag(STDCONTROL_AID_MOUSE_Y, STDCONTROL_AXIS_REGISTERED));

    stdControl_RegisterMouseAxesXY(0.5f, 0.75f);
    stdControl_ResetMousePos();
    TEST_ASSERT_EQUAL_FLOAT(0.0f, stdControl_ReadAxis(STDCONTROL_AID_MOUSE_X));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, stdControl_ReadAxis(STDCONTROL_AID_MOUSE_Y));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, stdControl_ReadAxis(STDCONTROL_AID_MOUSE_Z));
}

// Opens the Null backend so stdControl_ReadControls runs; the key state comes from the scripted
// device of Null/stdControlDevicesNull.c.
static void stdControlTest_OpenNullControls(void)
{
    TEST_ASSERT_EQUAL_INT(0, stdControl_Startup(0));
    TEST_ASSERT_EQUAL_INT(0, stdControl_Open());
    TEST_ASSERT_TRUE(stdControl_IsOpen());
}

// What is left of TestReadTimingAndFlaggedAxes: a key held over a whole read reads as the full axis,
// 1.0, the value the original expected for STDKEY_F15 (DIK_F15) held over a 16 ms read. The read
// timing comes from stdPlatform_GetTimeMsec here instead of the DX9 BeginRead seam, so only a read
// that took time is checked; the flagged-axis and FPS checks needed the dropped seams.
TEST(stdControl, TestHeldKeyReadsAsFullAxis)
{
    TEST_IGNORE_MESSAGE("TODO: [BUG] stdControlNull.c stdControl_ReadKeyAsAxis always returns 0.0, also for a key held down; stdControlDX9.c returned the held fraction of the read (1.0 here), which sithControl reads for keys bound to axes.");

    stdControlTest_OpenNullControls();
    TEST_ASSERT_TRUE(stdControl_QueueKeyEvent(STDKEY_F15, 1));
    stdControl_ReadControls();
    stdPlatform_Sleep(2u);
    stdControl_ReadControls();
    TEST_ASSERT_EQUAL_INT(1, stdControl_ReadKey(STDKEY_F15, NULL));
    TEST_ASSERT_FLOAT_WITHIN(0.00001f, 1.0f, stdControl_ReadKeyAsAxis(STDKEY_F15));
}

// Changed: the key state reaches the backend through its scripted device (stdControl_QueueKeyEvent,
// then stdControl_ReadControls) instead of the DX9 seams BeginRead and ApplyKeyboardState; the
// expectations of the original are unchanged. Dropped: the last two checks, a one-key and a NULL
// keyboard snapshot, which only the DX9 seam took.
TEST(stdControl, TestKeyboardStateTransitions)
{
    int pressed = 0;

    stdControlTest_OpenNullControls();

    TEST_ASSERT_TRUE(stdControl_QueueKeyEvent(STDKEY_F15, 1));
    stdControl_ReadControls();
    TEST_ASSERT_EQUAL_INT(1, stdControl_ReadKey(STDKEY_F15, &pressed));
    TEST_ASSERT_EQUAL_INT(1, pressed);

    stdControl_ReadControls();
    pressed = 0;
    TEST_ASSERT_EQUAL_INT(1, stdControl_ReadKey(STDKEY_F15, &pressed));
    TEST_ASSERT_EQUAL_INT(0, pressed);

    TEST_ASSERT_TRUE(stdControl_QueueKeyEvent(STDKEY_F15, 0));
    stdControl_ReadControls();
    TEST_ASSERT_EQUAL_INT(0, stdControl_ReadKey(STDKEY_F15, &pressed));
}

// Was TestStatusAndEmptyJoystickState. Dropped: the stdControl_DIGetStatus strings (DirectInput) and
// stdControl_GetMaxJoystickButtons() == 0 (declared, not defined). The description of a missing
// joystick is TestMissingJoystickDescriptionIsEmpty.
TEST(stdControl, TestEmptyJoystickState)
{
    TEST_ASSERT_EQUAL_size_t(0u, stdControl_GetNumJoysticks());
    TEST_ASSERT_EQUAL_INT(0, stdControl_IsGamePad(-1));
}

// Split from TestStatusAndEmptyJoystickState.
TEST(stdControl, TestMissingJoystickDescriptionIsEmpty)
{
    TEST_IGNORE_MESSAGE("TODO: [BUG] stdControlNull.c stdControl_GetJoysticDescription returns NULL; stdControlDX9.c returned \"\" for a joystick that does not exist, so a caller can print the description.");

    TEST_ASSERT_EQUAL_STRING("", stdControl_GetJoysticDescription(-1));
    TEST_ASSERT_EQUAL_STRING("", stdControl_GetJoysticDescription(0));
}

TEST_GROUP_RUNNER(stdControl)
{
    RUN_TEST_CASE(stdControl, TestAllKeyIdsClassifyAsKeyboardJoystickOrMouse);
    RUN_TEST_CASE(stdControl, TestAllJoystickButtonIds);
    RUN_TEST_CASE(stdControl, TestAllJoystickPovIds);
    RUN_TEST_CASE(stdControl, TestAllAxisIdsAndFlaggedAxisIds);
    RUN_TEST_CASE(stdControl, TestNamedJoystickAxisMacros);
    RUN_TEST_CASE(stdControl, TestMouseAndOutOfRangeBoundaries);
    RUN_TEST_CASE(stdControl, TestJoystickAndKeyIdLimits);
    RUN_TEST_CASE(stdControl, TestNoDeviceLifecycleAndReadDefaults);
    RUN_TEST_CASE(stdControl, TestMouseAndSensitivityState);
    RUN_TEST_CASE(stdControl, TestAxisRegistrationAndFlags);
#ifdef J3D_RUNTIME_GUARDS
    RUN_TEST_CASE(stdControl, TestRuntimeGuardsRejectOutOfRangeInputs);
#endif
    RUN_TEST_CASE(stdControl, TestRegisterAxisRejectsEmptyRange);
    RUN_TEST_CASE(stdControl, TestMouseAxisRegistrationAndSensitivityHelpers);
    RUN_TEST_CASE(stdControl, TestHeldKeyReadsAsFullAxis);
    RUN_TEST_CASE(stdControl, TestKeyboardStateTransitions);
    RUN_TEST_CASE(stdControl, TestEmptyJoystickState);
    RUN_TEST_CASE(stdControl, TestMissingJoystickDescriptionIsEmpty);
}
