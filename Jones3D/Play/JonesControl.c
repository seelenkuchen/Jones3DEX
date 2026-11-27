#include "JonesControl.h"

#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Display/JonesConsole.h>
#include <Jones3D/Main/JonesMain.h>
#include <Jones3D/Main/JonesScreenshot.h>

#include <sith/Devices/sithControl.h>
#include <sith/Gameplay/sithPlayerControls.h>
#include <sith/World/sithWeapon.h>

#include <std/General/std.h>
#include <std/General/stdConfig.h>
#include <std/General/stdControl.h>
#include <std/General/stdUtil.h>

static bool JonesControl_bStartup = false; // Added: Init to false
static unsigned int JonesControl_joystickChangeCount = 0; // Added: last hot-plug change acted on

void JonesControl_ResetGlobals(void)
{
    JonesControl_bStartup = false; // Added: a second run must register its callbacks again
    JonesControl_joystickChangeCount = 0; // Added
    JonesScreenshot_ResetGlobals(); // Added
}

int JonesControl_Startup(void)
{
    // Added: the mouse camera is on by default in the Enhanced profile; the Retail profile keeps
    // the original off default. An existing Jones.cfg value always wins, and a missing key is
    // written with its default so the file carries it.
    const bool bHasMouse = stdConfig_Contains(JONESCONTROL_CFG_CONTROLS_MOUSE);
    int bMouseControl = bHasMouse
        ? stdConfig_GetBool(JONESCONTROL_CFG_CONTROLS_MOUSE, false)
        : J3D_QOL_VALUE(true, false);
#ifdef J3D_QOL_IMPROVEMENTS
    if ( !bHasMouse )
    {
        (void)stdConfig_SetBool(JONESCONTROL_CFG_CONTROLS_MOUSE, bMouseControl);
    }
#endif
    // Added: rumble is on unless the player turned it off; a missing key is written with its
    // default so the next save carries it (presentation only, ADR-0028).
    bool bRumble = stdConfig_GetBool(JONESCONTROL_CFG_ENGINE_INPUT_RUMBLE, true);
    if ( !stdConfig_Contains(JONESCONTROL_CFG_ENGINE_INPUT_RUMBLE) )
    {
        (void)stdConfig_SetBool(JONESCONTROL_CFG_ENGINE_INPUT_RUMBLE, bRumble);
    }
    stdControl_EnableRumble(bRumble);

#ifdef J3D_QOL_IMPROVEMENTS
    // Added: the styles are per device: the keyboard and mouse default to Classic and the gamepad
    // to Modern. The old single key is only read, for migration: "classic" sets both Classic,
    // "modern" or a missing key gives the defaults. The new keys win when they are present. A new
    // install writes both; an existing config that only has the old key is not rewritten until the
    // player changes a style in Options (which writes that key).
    bool bKeyboardModern = false;
    bool bGamepadModern  = true;
    const bool bHasOldStyle = stdConfig_Contains(SITHCONTROL_CFG_ENGINE_INPUT_CONTROLSTYLE);

    if ( bHasOldStyle )
    {
        char aOldStyle[16] = { 0 };
        stdConfig_GetString(SITHCONTROL_CFG_ENGINE_INPUT_CONTROLSTYLE, aOldStyle,
            sizeof(aOldStyle), "modern");
        if ( streq(aOldStyle, "classic") )
        {
            bKeyboardModern = false;
            bGamepadModern  = false;
        }
        else if ( !streq(aOldStyle, "modern") )
        {
            SITHLOG_STATUS("Unknown control style \"%s\"; using the per-device defaults.\n",
                aOldStyle);
        }
    }

    const bool bHasKeyboardStyle = stdConfig_Contains(
        SITHCONTROL_CFG_ENGINE_INPUT_KEYBOARDCONTROLSTYLE);
    if ( bHasKeyboardStyle )
    {
        char aStyle[16] = { 0 };
        stdConfig_GetString(SITHCONTROL_CFG_ENGINE_INPUT_KEYBOARDCONTROLSTYLE, aStyle,
            sizeof(aStyle), "classic");
        if ( streq(aStyle, "modern") )
        {
            bKeyboardModern = true;
        }
        else
        {
            bKeyboardModern = false;
            if ( !streq(aStyle, "classic") )
            {
                SITHLOG_STATUS("Unknown keyboard control style \"%s\"; using classic.\n",
                    aStyle);
            }
        }
    }

    const bool bHasGamepadStyle = stdConfig_Contains(
        SITHCONTROL_CFG_ENGINE_INPUT_GAMEPADCONTROLSTYLE);
    if ( bHasGamepadStyle )
    {
        char aStyle[16] = { 0 };
        stdConfig_GetString(SITHCONTROL_CFG_ENGINE_INPUT_GAMEPADCONTROLSTYLE, aStyle,
            sizeof(aStyle), "modern");
        if ( streq(aStyle, "classic") )
        {
            bGamepadModern = false;
        }
        else
        {
            bGamepadModern = true;
            if ( !streq(aStyle, "modern") )
            {
                SITHLOG_STATUS("Unknown gamepad control style \"%s\"; using modern.\n",
                    aStyle);
            }
        }
    }

    sithControl_SetModernKeyboardStyle(bKeyboardModern);
    sithControl_SetModernGamepadStyle(bGamepadModern);
    sithControl_SetModernMovementEnabled(true);

    // Added: write a missing new key only on a new install or after Options already wrote one.
    const bool bWriteNewKeys = !bHasOldStyle || bHasKeyboardStyle || bHasGamepadStyle;
    if ( !bHasKeyboardStyle && bWriteNewKeys )
    {
        (void)stdConfig_SetString(SITHCONTROL_CFG_ENGINE_INPUT_KEYBOARDCONTROLSTYLE,
            bKeyboardModern ? "modern" : "classic");
    }

    if ( !bHasGamepadStyle && bWriteNewKeys )
    {
        (void)stdConfig_SetString(SITHCONTROL_CFG_ENGINE_INPUT_GAMEPADCONTROLSTYLE,
            bGamepadModern ? "modern" : "classic");
    }
#else
    // Retail always keeps the original tank controls and neither reads nor writes the keys.
    sithControl_SetModernControlStyle(false);
#endif

    JonesDisplaySettings* pSettings = JonesMain_GetDisplaySettings();

    if ( JonesControl_bStartup )
    {
        return 1;
    }

    if ( sithControl_Startup(0) )
    {
        return 1;
    }

    // Note, instead of manually initializing the sithControl_DefaultInit could be called 
    // and unbind mouse controller afterwards
    sithControl_Reset();
    sithControl_g_controlOptions = 0x04;
    sithControl_RegisterKeyboardBindings();
    sithControl_RegisterJoystickBindings();
    // Note, sithControl_RegisterMouseBindings() is not called probably because jonesconfig would unbind it anyway

    if ( !pSettings->bWindowMode || bMouseControl )
    {
        stdControl_EnableMouse(1);
    }

    sithControl_RegisterControlCallback(sithPlayerControls_Process);
    sithControl_RegisterControlCallback(sithWeapon_ProcessWeaponControls);
    sithControl_RegisterControlCallback(JonesControl_ProcessControls);

    for ( size_t functionId = 0; functionId < SITHCONTROL_MAXFUNCTIONS; ++functionId )
    {
        sithControl_RegisterAxisFunction((SithControlFunction)functionId, SITHCONTROLFUNCTION_KEY);
    }

    // Altered: a connected pad is always on; controls.controller is ignored on read and dropped
    // from the config so the next save removes it.
    if ( stdControl_GetNumJoysticks() )
    {
        JonesControl_EnableJoystickAxes();
        sithControl_UnbindJoystickAxes();
    }

    stdConfig_Remove(JONESCONTROL_CFG_CONTROLS_CONTROLLER);

    if ( (sithMain_g_sith_mode.debugModeFlags & SITHDEBUG_INEDITOR) == 0 && (!pSettings->bWindowMode || bMouseControl) )
    {
        sithControl_UnbindMouseAxes();
    }

    // Fixed: load the control schemes before binding them, so a first run binds the default scheme
    if ( !jonesConfig_Startup() )
    {
        STDLOG_WARNING("Unable to load the control schemes; keeping the built-in bindings.\n");
    }

    // Added: apply the remembered active control scheme after the default bindings are
    // registered, so the scheme replaces them. This is a no-op in the Null column.
    jonesConfig_BindActiveControlScheme();

    // Added: ignore joystick count changes the startup already accounted for.
    JonesControl_joystickChangeCount = stdControl_GetJoystickChangeCount();

    JonesControl_bStartup = true;
    return 0;
}

void JonesControl_Shutdown(void)
{
    sithControl_Shutdown();
    JonesControl_bStartup = false;
}

// Added: rebuilds the pad bindings when the connected joystick count changes. The per-frame control
// callback calls this at a safe point, never the platform event handler mid-frame.
static void JonesControl_UpdateJoystickBindings(void)
{
    unsigned int changeCount = stdControl_GetJoystickChangeCount();
    if ( changeCount == JonesControl_joystickChangeCount )
    {
        return;
    }

    JonesControl_joystickChangeCount = changeCount;

    if ( stdControl_GetNumJoysticks() )
    {
        JonesControl_EnableJoystickAxes();
    }

    sithControl_UnbindJoystickAxes();
    jonesConfig_BindActiveControlScheme();
}

int J3DAPI JonesControl_ProcessControls(SithThing* pPlayer, float secDeltaTime)
{
    (void)secDeltaTime;

    // Added: gamepads added or removed after startup take effect at the next control update.
    JonesControl_UpdateJoystickBindings();

    // Fixed: the removed Win32 window procedure called JonesMain_OnScreenshotKey on VK_F12; poll
    //        the bound control before the game's other key handling, on the press edge only.
    JonesScreenshot_ProcessKey();

    if ( (pPlayer->thingInfo.actorInfo.flags & SITH_AF_CONTROLSDISABLED) != 0 || JonesConsole_g_bVisible )
    {
        return 0;
    }

    int numPressed = 0;
    sithControl_GetKey(SITHCONTROL_TALK, &numPressed); // F10 pressed
    if ( numPressed )
    {
        JonesConsole_ShowConsole();
    }

    return 0;
}

void JonesControl_EnableJoystickAxes(void)
{
    int joystickCount = stdControl_GetNumJoysticks();
    for ( size_t i = 0; i < joystickCount; ++i )
    {
        stdControl_EnableAxis(STDCONTROL_GET_JOYSTICK_AXIS_X(i)); // 6 * i + 0
        stdControl_EnableAxis(STDCONTROL_GET_JOYSTICK_AXIS_Y(i)); // 6 * i + 1
        stdControl_EnableAxis(STDCONTROL_GET_JOYSTICK_AXIS_Z(i)); // 6 * i + 2
        stdControl_EnableAxis(STDCONTROL_GET_JOYSTICK_AXIS_RZ(i));  // 6 * i + 5
    }
}
