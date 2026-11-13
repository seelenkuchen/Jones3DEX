// Added: Unity Fixture suite for the Babylon column's device side of stdControl. It compiles
// stdControlNull.c and stdControlDevicesBabylon.cpp directly and drives them through a fresh
// StdBabylonContext whose platform is a babylon::platform::NullPlatform, so a queued key is the
// only thing that can ever change a key state and every value is exact and deterministic.
#include <unity_fixture.h>

#include <cstddef>
#include <cstdio>
#include <span>

#include <babylon/cvars/Registry.h>
#include <babylon/platform/Event.h>
#include <babylon/platform/Keys.h>
#include <babylon/platform/NullPlatform.h>
#include <babylon/time/NullClock.h>

#include <j3dcore/Tests/j3dTest.h>

#include <std/Babylon/stdBabylon.h>
#include <std/Babylon/stdBabylonEvents.h>
#include <std/Babylon/stdControlDevicesBabylon.h>
#include <std/General/std.h>
#include <std/Null/stdControlNull.h>
#include <std/General/stdControl.h>
#include <std/General/stdKeys.h>

TEST_GROUP(stdControlBabylon);

// The stdBabylonTests executable does not link General/std.c; stdControlNull.c's STD_ASSERTREL
// reads the std_g_pHS host-services global, so this suite owns it and points it at the J3DTest
// services, the way stdGeneralTest.c does for the std suites.
extern "C" tHostServices* std_g_pHS = nullptr;

namespace
{
    babylon::time::NullClock*      pRealClock    = nullptr;
    babylon::time::NullClock*      pVirtualClock = nullptr;
    babylon::platform::NullPlatform* pPlatform   = nullptr;
    // The context contract wants a registry; the device read uses no cvar, so this one stays empty.
    babylon::cvars::Registry     cvars;
    StdBabylonContext              context;

    // Installs the context. With bScriptPlatform false, pScriptPlatform stays null: the platform
    // is still there to read, but nothing can queue events into it.
    void InstallContext(bool bScriptPlatform = true)
    {
        pRealClock    = new babylon::time::NullClock();
        pVirtualClock = new babylon::time::NullClock();
        pPlatform     = new babylon::platform::NullPlatform();

        context.pClock          = pRealClock;
        context.pVirtualClock   = pVirtualClock;
        context.pPlatform       = pPlatform;
        context.pScriptPlatform = bScriptPlatform ? pPlatform : nullptr;

        // The device read is the headless one: no game window, so the pump records no window events.
        context.pWindow = nullptr;
        context.pCvars  = &cvars;

        // The control adapter never touches files, so this suite leaves pFileSystem null.
        context.pFileSystem = nullptr;
        stdBabylon_SetContext(&context);
    }

    void RemoveContext()
    {
        stdBabylon_SetContext(nullptr);
        delete pPlatform;
        delete pVirtualClock;
        delete pRealClock;
        pPlatform     = nullptr;
        pVirtualClock = nullptr;
        pRealClock    = nullptr;
    }

    // Reads one key the way the game does: down state as the return value, "went down this read"
    // in the out parameter.
    int ReadKeyState(int keyId, int* pbPressed)
    {
        if ( pbPressed )
        {
            *pbPressed = 0;
        }

        return stdControl_ReadKey((size_t)keyId, pbPressed);
    }
}

TEST_SETUP(stdControlBabylon)
{
    J3DTest_Startup();
    std_g_pHS = J3DTest_GetHostServices();

    InstallContext();

    // The held-key state is file-static in the adapter and never resets on its own.
    stdControlDevicesBabylon_ResetForTest();

    stdControl_Shutdown();
    stdControl_Reset();
    TEST_ASSERT_EQUAL_INT(0, stdControl_Startup(0));
    TEST_ASSERT_EQUAL_INT(0, stdControl_Open());
}

TEST_TEAR_DOWN(stdControlBabylon)
{
    stdControl_Close();
    stdControl_Shutdown();
    stdControl_Reset();
    stdControl_EnableRumble(true);
    stdControlDevicesBabylon_ResetForTest();

    RemoveContext();
    std_g_pHS = nullptr;
    J3DTest_Shutdown();
}

TEST(stdControlBabylon, EveryKeyMappingEntryRoundTrips)
{
    const std::span<const StdBabylonKeyMapping> mappings = stdControlDevicesBabylon_KeyMappings();
    TEST_ASSERT_TRUE(mappings.size() > 0u);

    for ( const StdBabylonKeyMapping& rMapping : mappings )
    {
        char aMessage[64];
        snprintf(aMessage, sizeof(aMessage), "engine key id %d", rMapping.engineKeyId);

        // id -> key -> id and key -> id -> key over the one table: a doubled id or a doubled key
        // would break the round trip on the second entry.
        TEST_ASSERT_TRUE_MESSAGE(stdControlDevicesBabylon_KeyFromEngineId(rMapping.engineKeyId) == rMapping.key,
            aMessage);
        TEST_ASSERT_EQUAL_INT_MESSAGE(rMapping.engineKeyId,
            stdControlDevicesBabylon_EngineIdFromKey(rMapping.key), aMessage);
    }
}

TEST(stdControlBabylon, KeyMappingSpotChecksAndUnmappedKeys)
{
    TEST_ASSERT_TRUE(stdControlDevicesBabylon_KeyFromEngineId(STDKEY_UP) == babylon::platform::Key::Up);
    TEST_ASSERT_EQUAL_INT(STDKEY_UP, stdControlDevicesBabylon_EngineIdFromKey(babylon::platform::Key::Up));
    TEST_ASSERT_TRUE(stdControlDevicesBabylon_KeyFromEngineId(STDKEY_LMENU) == babylon::platform::Key::LeftAlt);
    TEST_ASSERT_EQUAL_INT(STDKEY_LMENU, stdControlDevicesBabylon_EngineIdFromKey(babylon::platform::Key::LeftAlt));
    TEST_ASSERT_TRUE(stdControlDevicesBabylon_KeyFromEngineId(STDKEY_A) == babylon::platform::Key::A);
    TEST_ASSERT_EQUAL_INT(STDKEY_A, stdControlDevicesBabylon_EngineIdFromKey(babylon::platform::Key::A));

    // The keys one side lacks answer as unmapped: the engine's F13 and keypad comma have no
    // Babylon key, Babylon's PrintScreen has no engine id, and a mouse button is not a key.
    TEST_ASSERT_TRUE(stdControlDevicesBabylon_KeyFromEngineId(STDKEY_F13) == babylon::platform::Key::Unknown);
    TEST_ASSERT_TRUE(stdControlDevicesBabylon_KeyFromEngineId(STDKEY_NUMPADCOMMA) == babylon::platform::Key::Unknown);
    TEST_ASSERT_EQUAL_INT(-1, stdControlDevicesBabylon_EngineIdFromKey(babylon::platform::Key::PrintScreen));
    TEST_ASSERT_TRUE(stdControlDevicesBabylon_KeyFromEngineId((int)STDCONTROL_KID_MOUSE_LBUTTON)
        == babylon::platform::Key::Unknown);
}

TEST(stdControlBabylon, QueuedKeyPressReadsAsOneDownEdge)
{
    TEST_ASSERT_TRUE(stdControl_QueueKeyEvent(STDKEY_UP, 1));

    // Queueing alone changes nothing: the state is read from the queue, not written by it.
    TEST_ASSERT_EQUAL_INT(0, ReadKeyState(STDKEY_UP, NULL));

    stdControl_ReadControls();
    int pressed = 0;
    TEST_ASSERT_EQUAL_INT(1, ReadKeyState(STDKEY_UP, &pressed));
    TEST_ASSERT_EQUAL_INT(1, pressed);

    // The next read, with no event in between: still down, but nothing went down now.
    stdControl_ReadControls();
    TEST_ASSERT_EQUAL_INT(1, ReadKeyState(STDKEY_UP, &pressed));
    TEST_ASSERT_EQUAL_INT(0, pressed);

    TEST_ASSERT_TRUE(stdControl_QueueKeyEvent(STDKEY_UP, 0));
    stdControl_ReadControls();
    TEST_ASSERT_EQUAL_INT(0, ReadKeyState(STDKEY_UP, &pressed));
    TEST_ASSERT_EQUAL_INT(0, pressed);
}

TEST(stdControlBabylon, PressAndReleaseBeforeOneReadIsNotDown)
{
    // A snapshot like the polled DirectInput device: what happens between two reads is not down
    // at either of them.
    TEST_ASSERT_TRUE(stdControl_QueueKeyEvent(STDKEY_SPACE, 1));
    TEST_ASSERT_TRUE(stdControl_QueueKeyEvent(STDKEY_SPACE, 0));

    stdControl_ReadControls();
    int pressed = 0;
    TEST_ASSERT_EQUAL_INT(0, ReadKeyState(STDKEY_SPACE, &pressed));
    TEST_ASSERT_EQUAL_INT(0, pressed);
}

TEST(stdControlBabylon, MouseButtonIsOnlyReadWhileTheMouseIsEnabled)
{
    const int buttonId = (int)STDCONTROL_KID_MOUSE_LBUTTON;

    TEST_ASSERT_TRUE(stdControl_QueueKeyEvent(buttonId, 1));

    // stdControl_EnableMouse(0) is the state Startup left behind: the buttons are not read.
    stdControl_ReadControls();
    TEST_ASSERT_EQUAL_INT(0, ReadKeyState(buttonId, NULL));

    stdControl_EnableMouse(1);
    stdControl_ReadControls();
    int pressed = 0;
    TEST_ASSERT_EQUAL_INT(1, ReadKeyState(buttonId, &pressed));
    TEST_ASSERT_EQUAL_INT(1, pressed);

    TEST_ASSERT_TRUE(stdControl_QueueKeyEvent(buttonId, 0));
    stdControl_ReadControls();
    TEST_ASSERT_EQUAL_INT(0, ReadKeyState(buttonId, &pressed));
    TEST_ASSERT_EQUAL_INT(0, pressed);
}

TEST(stdControlBabylon, IdsTheColumnCannotDeliverReturnFalse)
{
    // An engine key id with no Babylon key, a joystick id, and the ids outside the key-state array.
    TEST_ASSERT_FALSE(stdControl_QueueKeyEvent(STDKEY_F13, 1));
    TEST_ASSERT_FALSE(stdControl_QueueKeyEvent((int)STDCONTROL_JOYSTICK_FIRSTKID, 1));
    TEST_ASSERT_FALSE(stdControl_QueueKeyEvent(-1, 1));
    TEST_ASSERT_FALSE(stdControl_QueueKeyEvent((int)STDCONTROL_MAX_KEYID, 1));

    // One past the engine's four mouse buttons: Babylon's X2 has no engine id.
    TEST_ASSERT_FALSE(stdControl_QueueKeyEvent((int)(STDCONTROL_KID_MOUSE_LBUTTON + STDCONTROL_MAX_MOUSE_BUTTONS), 1));

    // Without a script platform the column delivers nothing, not even a key it can name.
    context.pScriptPlatform = nullptr;
    TEST_ASSERT_FALSE(stdControl_QueueKeyEvent(STDKEY_UP, 1));
    TEST_ASSERT_FALSE(stdControl_QueueKeyEvent((int)STDCONTROL_KID_MOUSE_LBUTTON, 1));

    // The read still works: it drains the platform, which nothing has queued into.
    stdControl_ReadControls();
    TEST_ASSERT_EQUAL_INT(0, ReadKeyState(STDKEY_UP, NULL));
}

TEST(stdControlBabylon, MouseMotionAndWheelReadAsAccumulatedAxes)
{
    const int mouseX = (int)STDCONTROL_AID_MOUSE_X;
    const int mouseY = (int)STDCONTROL_AID_MOUSE_Y;
    const int mouseZ = (int)STDCONTROL_AID_MOUSE_Z;

    // The startup registration the Babylon column adds, like stdControlDX9.c's InitMouse.
    TEST_ASSERT_EQUAL_INT(1, stdControl_EnableAxis(mouseX));
    TEST_ASSERT_EQUAL_INT(1, stdControl_EnableAxis(mouseY));
    TEST_ASSERT_EQUAL_INT(1, stdControl_EnableAxis(mouseZ));
    TEST_ASSERT_EQUAL_INT(1, stdControl_EnableMouse(1));

    // Motion and wheel queued between two reads are summed, in pixels and WHEEL_DELTA units.
    pPlatform->pushEvent(babylon::platform::MouseMoved{ 0, 0.0f, 0.0f, 5.0f, -3.0f });
    pPlatform->pushEvent(babylon::platform::MouseWheelMoved{ 0, 0.0f, 1.0f });
    stdControl_ReadControls();

    TEST_ASSERT_EQUAL_FLOAT(5.0f / 250.0f, stdControl_ReadAxis(mouseX));
    TEST_ASSERT_EQUAL_FLOAT(-3.0f / 200.0f, stdControl_ReadAxis(mouseY));
    // One notch is 120 DirectInput units, clamped to the registered -20..20 range.
    TEST_ASSERT_EQUAL_INT(20, stdControl_ReadAxisRaw(mouseZ));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, stdControl_ReadAxis(mouseZ));

    // The next read has no motion since the previous one.
    stdControl_ReadControls();
    TEST_ASSERT_EQUAL_FLOAT(0.0f, stdControl_ReadAxis(mouseX));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, stdControl_ReadAxis(mouseY));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, stdControl_ReadAxis(mouseZ));
}

TEST(stdControlBabylon, ConnectedGamepadBecomesJoystickZero)
{
    const babylon::platform::GamepadId gamepad = 1;
    pPlatform->pushEvent(babylon::platform::GamepadConnected{ gamepad });

    // Enumerating pumps the queue and registers the slots, like DirectInput's InitJoysticks did.
    TEST_ASSERT_EQUAL_size_t(1, stdControl_GetNumJoysticks());
    TEST_ASSERT_EQUAL_INT(1, stdControl_IsGamePad(0));
    TEST_ASSERT_EQUAL_STRING("Gamepad1", stdControl_GetJoysticDescription(0));

    const int aAxes[] = {
        (int)STDCONTROL_GET_JOYSTICK_AXIS_X(0),
        (int)STDCONTROL_GET_JOYSTICK_AXIS_Y(0),
        (int)STDCONTROL_GET_JOYSTICK_AXIS_Z(0),
        (int)STDCONTROL_GET_JOYSTICK_AXIS_RX(0),
        (int)STDCONTROL_GET_JOYSTICK_AXIS_RY(0),
        (int)STDCONTROL_GET_JOYSTICK_AXIS_RZ(0),
    };
    for ( const int axis : aAxes )
    {
        TEST_ASSERT_EQUAL_INT(1, stdControl_EnableAxis(axis));
    }

    pPlatform->pushEvent(babylon::platform::GamepadAxisMoved{
        gamepad, babylon::platform::GamepadAxis::LeftX, 1.0f });
    pPlatform->pushEvent(babylon::platform::GamepadAxisMoved{
        gamepad, babylon::platform::GamepadAxis::LeftY, 1.0f });
    pPlatform->pushEvent(babylon::platform::GamepadAxisMoved{
        gamepad, babylon::platform::GamepadAxis::LeftTrigger, 1.0f });
    pPlatform->pushEvent(babylon::platform::GamepadAxisMoved{
        gamepad, babylon::platform::GamepadAxis::RightX, -1.0f });
    pPlatform->pushEvent(babylon::platform::GamepadAxisMoved{
        gamepad, babylon::platform::GamepadAxis::RightY, -1.0f });
    pPlatform->pushEvent(babylon::platform::GamepadAxisMoved{
        gamepad, babylon::platform::GamepadAxis::RightTrigger, 1.0f });
    pPlatform->pushEvent(babylon::platform::GamepadButtonChanged{
        gamepad, babylon::platform::GamepadButton::South, true });
    pPlatform->pushEvent(babylon::platform::GamepadButtonChanged{
        gamepad, babylon::platform::GamepadButton::DpadUp, true });

    stdControl_ReadControls();

    // Full-scale values pass both the dead zone and the axis normalization untouched.
    TEST_ASSERT_EQUAL_FLOAT(1.0f, stdControl_ReadAxis((size_t)aAxes[0]));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, stdControl_ReadAxis((size_t)aAxes[1]));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, stdControl_ReadAxis((size_t)aAxes[2]));
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, stdControl_ReadAxis((size_t)aAxes[3]));
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, stdControl_ReadAxis((size_t)aAxes[4]));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, stdControl_ReadAxis((size_t)aAxes[5]));

    TEST_ASSERT_EQUAL_INT(1, stdControl_ReadKey(
        STDCONTROL_JOYSTICK_GETBUTTON(0, 0), NULL));
    TEST_ASSERT_EQUAL_INT(0, stdControl_ReadKey(
        STDCONTROL_JOYSTICK_GETBUTTON(0, 1), NULL));
    TEST_ASSERT_EQUAL_INT(1, stdControl_ReadKey(
        STDCONTROL_JOYSTICK_GETPOVUP(0, 0), NULL));
    TEST_ASSERT_EQUAL_INT(0, stdControl_ReadKey(
        STDCONTROL_JOYSTICK_GETPOVLEFT(0, 0), NULL));

    // Disconnecting releases the slot: axes centre and buttons come up.
    pPlatform->pushEvent(babylon::platform::GamepadDisconnected{ gamepad });
    stdControl_ReadControls();
    TEST_ASSERT_EQUAL_size_t(0, stdControl_GetNumJoysticks());
    TEST_ASSERT_EQUAL_INT(0, stdControl_IsGamePad(0));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, stdControl_ReadAxis((size_t)aAxes[0]));
    TEST_ASSERT_EQUAL_INT(0, stdControl_ReadKey(
        STDCONTROL_JOYSTICK_GETBUTTON(0, 0), NULL));
    TEST_ASSERT_EQUAL_INT(0, stdControl_ReadKey(
        STDCONTROL_JOYSTICK_GETPOVUP(0, 0), NULL));
}

TEST(stdControlBabylon, GamepadSlotsFollowConnectionOrder)
{
    const babylon::platform::GamepadId first = 1;
    const babylon::platform::GamepadId second = 2;
    pPlatform->pushEvent(babylon::platform::GamepadConnected{ first });
    pPlatform->pushEvent(babylon::platform::GamepadConnected{ second });
    TEST_ASSERT_EQUAL_size_t(2, stdControl_GetNumJoysticks());

    TEST_ASSERT_EQUAL_INT(1, stdControl_EnableAxis(
        (int)STDCONTROL_GET_JOYSTICK_AXIS_X(0)));
    TEST_ASSERT_EQUAL_INT(1, stdControl_EnableAxis(
        (int)STDCONTROL_GET_JOYSTICK_AXIS_X(1)));

    pPlatform->pushEvent(babylon::platform::GamepadAxisMoved{
        first, babylon::platform::GamepadAxis::LeftX, 1.0f });
    pPlatform->pushEvent(babylon::platform::GamepadAxisMoved{
        second, babylon::platform::GamepadAxis::LeftX, -1.0f });
    stdControl_ReadControls();
    TEST_ASSERT_EQUAL_FLOAT(1.0f, stdControl_ReadAxis(
        STDCONTROL_GET_JOYSTICK_AXIS_X(0)));
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, stdControl_ReadAxis(
        STDCONTROL_GET_JOYSTICK_AXIS_X(1)));

    // Removing the first pad moves the second into slot 0 and clears the old slot.
    pPlatform->pushEvent(babylon::platform::GamepadDisconnected{ first });
    stdControl_ReadControls();
    TEST_ASSERT_EQUAL_size_t(1, stdControl_GetNumJoysticks());
    TEST_ASSERT_EQUAL_FLOAT(-1.0f, stdControl_ReadAxis(
        STDCONTROL_GET_JOYSTICK_AXIS_X(0)));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, stdControl_ReadAxis(
        STDCONTROL_GET_JOYSTICK_AXIS_X(1)));
}

TEST(stdControlBabylon, RumbleReachesEveryConnectedGamepad)
{
    // Two pads, in connection order.
    pPlatform->pushEvent(babylon::platform::GamepadConnected{ 11 });
    pPlatform->pushEvent(babylon::platform::GamepadConnected{ 12 });
    stdBabylonEvents_Pump();

    stdControl_EnableRumble(true);
    stdControl_Rumble(0.5f, 0.25f, 120);

    // The Null platform records only the last call, so check it is one of the pads and that the
    // values arrived unchanged.
    const std::optional<babylon::platform::NullPlatform::RumbleCall> last =
        pPlatform->lastRumble();
    TEST_ASSERT_TRUE(last.has_value());
    TEST_ASSERT_TRUE(last->gamepad == 11 || last->gamepad == 12);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, last->lowFrequency);
    TEST_ASSERT_EQUAL_FLOAT(0.25f, last->highFrequency);
    TEST_ASSERT_EQUAL_UINT32(120u, last->durationMs);

    // Disabled: the adapter forwards nothing, so the previous record stays as it was.
    stdControl_EnableRumble(false);
    stdControl_Rumble(0.9f, 0.9f, 400);
    const std::optional<babylon::platform::NullPlatform::RumbleCall> after =
        pPlatform->lastRumble();
    TEST_ASSERT_TRUE(after.has_value());
    TEST_ASSERT_EQUAL_FLOAT(0.5f, after->lowFrequency);
    TEST_ASSERT_EQUAL_FLOAT(0.25f, after->highFrequency);
    TEST_ASSERT_EQUAL_UINT32(120u, after->durationMs);
}

TEST_GROUP_RUNNER(stdControlBabylon)
{
    RUN_TEST_CASE(stdControlBabylon, EveryKeyMappingEntryRoundTrips);
    RUN_TEST_CASE(stdControlBabylon, KeyMappingSpotChecksAndUnmappedKeys);
    RUN_TEST_CASE(stdControlBabylon, QueuedKeyPressReadsAsOneDownEdge);
    RUN_TEST_CASE(stdControlBabylon, PressAndReleaseBeforeOneReadIsNotDown);
    RUN_TEST_CASE(stdControlBabylon, MouseButtonIsOnlyReadWhileTheMouseIsEnabled);
    RUN_TEST_CASE(stdControlBabylon, IdsTheColumnCannotDeliverReturnFalse);
    RUN_TEST_CASE(stdControlBabylon, MouseMotionAndWheelReadAsAccumulatedAxes);
    RUN_TEST_CASE(stdControlBabylon, ConnectedGamepadBecomesJoystickZero);
    RUN_TEST_CASE(stdControlBabylon, GamepadSlotsFollowConnectionOrder);
    RUN_TEST_CASE(stdControlBabylon, RumbleReachesEveryConnectedGamepad);
}
