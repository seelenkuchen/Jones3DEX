// Added: the Babylon column's device side of std/Null/stdControlNull.h. The engine's keyboard,
// mouse buttons, mouse axes and gamepads come from the Babylon platform event queue through the one
// shared babylon::input::InputState of stdBabylonEvents.cpp, read once per stdControl_ReadControls
// as a snapshot of every key's state, the way stdControlDX9.c polls DirectInput; the headless
// runner's scripted keys go into that same queue.
//
// Gamepad mapping (a connected gamepad is one joystick, in InputState::gamepads() order):
//
//     stdControl axis   gamepad axis       state range          notes
//     X                 LeftX              -32768..32767        left stick deadzone
//     Y                 LeftY              -32768..32767        down positive
//     Z                 LeftTrigger        -255..255            0..255, 30 threshold
//     RX                RightX             -32768..32767        right stick deadzone
//     RY                RightY             -32768..32767        down positive
//     RZ                RightTrigger      -255..255            0..255, 30 threshold
//
//     button index      gamepad button     button index         gamepad button
//     0                 South              6                    Back
//     1                 East               7                    Start
//     2                 West               8                    LeftStick
//     3                 North              9                    RightStick
//     4                 LeftShoulder       10                   Guide
//     5                 RightShoulder
//
//     POV 0             d-pad: left, up, right, down
//
// The ranges and signs are those stdControlDX9.c's DIJOYSTATE path produced; the trigger bytes
// and the POV are those its XInput path produced, with the two triggers on separate axes instead
// of one combined Z. Adapter code: no Fixed/Added markers inside.
#include <babylon/input/InputState.h>
#include <babylon/platform/Event.h>
#include <babylon/platform/Keys.h>
#include <babylon/platform/NullPlatform.h>
#include <babylon/platform/Platform.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>

#include <std/Babylon/stdBabylonEvents.h>
#include <std/Babylon/stdControlDevicesBabylon.h>
#include <std/Null/stdControlNull.h>
#include <std/General/stdControl.h>
#include <std/General/stdKeys.h>

#include <std/General/stdUtil.h>

#include "stdBabylon.h"

namespace
{
    // Every key the engine and babylon::platform::Key both name, in engine key id order. Keys only
    // one of them has stay out: the engine has no PrintScreen, ScrollLock or Pause and no keypad
    // comma or keypad equals, and Babylon has no F13 to F15. Mouse buttons stay out too, they have
    // their own engine ids and their own table below.
    constexpr StdBabylonKeyMapping aKeyMappings[] =
    {
        { STDKEY_ESCAPE,     babylon::platform::Key::Escape          },
        { STDKEY_1,          babylon::platform::Key::Num1            },
        { STDKEY_2,          babylon::platform::Key::Num2            },
        { STDKEY_3,          babylon::platform::Key::Num3            },
        { STDKEY_4,          babylon::platform::Key::Num4            },
        { STDKEY_5,          babylon::platform::Key::Num5            },
        { STDKEY_6,          babylon::platform::Key::Num6            },
        { STDKEY_7,          babylon::platform::Key::Num7            },
        { STDKEY_8,          babylon::platform::Key::Num8            },
        { STDKEY_9,          babylon::platform::Key::Num9            },
        { STDKEY_0,          babylon::platform::Key::Num0            },
        { STDKEY_MINUS,      babylon::platform::Key::Minus           },
        { STDKEY_EQUALS,     babylon::platform::Key::Equals          },
        { STDKEY_BACK,       babylon::platform::Key::Backspace       },
        { STDKEY_TAB,        babylon::platform::Key::Tab             },
        { STDKEY_Q,          babylon::platform::Key::Q               },
        { STDKEY_W,          babylon::platform::Key::W               },
        { STDKEY_E,          babylon::platform::Key::E               },
        { STDKEY_R,          babylon::platform::Key::R               },
        { STDKEY_T,          babylon::platform::Key::T               },
        { STDKEY_Y,          babylon::platform::Key::Y               },
        { STDKEY_U,          babylon::platform::Key::U               },
        { STDKEY_I,          babylon::platform::Key::I               },
        { STDKEY_O,          babylon::platform::Key::O               },
        { STDKEY_P,          babylon::platform::Key::P               },
        { STDKEY_LBRACKET,   babylon::platform::Key::LeftBracket     },
        { STDKEY_RBRACKET,   babylon::platform::Key::RightBracket    },
        { STDKEY_RETURN,     babylon::platform::Key::Enter           },
        { STDKEY_LCONTROL,   babylon::platform::Key::LeftCtrl        },
        { STDKEY_A,          babylon::platform::Key::A               },
        { STDKEY_S,          babylon::platform::Key::S               },
        { STDKEY_D,          babylon::platform::Key::D               },
        { STDKEY_F,          babylon::platform::Key::F               },
        { STDKEY_G,          babylon::platform::Key::G               },
        { STDKEY_H,          babylon::platform::Key::H               },
        { STDKEY_J,          babylon::platform::Key::J               },
        { STDKEY_K,          babylon::platform::Key::K               },
        { STDKEY_L,          babylon::platform::Key::L               },
        { STDKEY_SEMICOLON,  babylon::platform::Key::Semicolon       },
        { STDKEY_APOSTROPHE, babylon::platform::Key::Apostrophe      },
        { STDKEY_GRAVE,      babylon::platform::Key::Grave           },
        { STDKEY_LSHIFT,     babylon::platform::Key::LeftShift       },
        { STDKEY_BACKSLASH,  babylon::platform::Key::Backslash       },
        { STDKEY_Z,          babylon::platform::Key::Z               },
        { STDKEY_X,          babylon::platform::Key::X               },
        { STDKEY_C,          babylon::platform::Key::C               },
        { STDKEY_V,          babylon::platform::Key::V               },
        { STDKEY_B,          babylon::platform::Key::B               },
        { STDKEY_N,          babylon::platform::Key::N               },
        { STDKEY_M,          babylon::platform::Key::M               },
        { STDKEY_COMMA,      babylon::platform::Key::Comma           },
        { STDKEY_PERIOD,     babylon::platform::Key::Period          },
        { STDKEY_SLASH,      babylon::platform::Key::Slash           },
        { STDKEY_RSHIFT,     babylon::platform::Key::RightShift      },
        { STDKEY_MULTIPLY,   babylon::platform::Key::KeypadMultiply  },
        { STDKEY_LMENU,      babylon::platform::Key::LeftAlt         },
        { STDKEY_SPACE,      babylon::platform::Key::Space           },
        { STDKEY_CAPITAL,    babylon::platform::Key::CapsLock        },
        { STDKEY_F1,         babylon::platform::Key::F1              },
        { STDKEY_F2,         babylon::platform::Key::F2              },
        { STDKEY_F3,         babylon::platform::Key::F3              },
        { STDKEY_F4,         babylon::platform::Key::F4              },
        { STDKEY_F5,         babylon::platform::Key::F5              },
        { STDKEY_F6,         babylon::platform::Key::F6              },
        { STDKEY_F7,         babylon::platform::Key::F7              },
        { STDKEY_F8,         babylon::platform::Key::F8              },
        { STDKEY_F9,         babylon::platform::Key::F9              },
        { STDKEY_F10,        babylon::platform::Key::F10             },
        { STDKEY_NUMLOCK,    babylon::platform::Key::NumLock         },
        { STDKEY_NUMPAD7,    babylon::platform::Key::Keypad7         },
        { STDKEY_NUMPAD8,    babylon::platform::Key::Keypad8         },
        { STDKEY_NUMPAD9,    babylon::platform::Key::Keypad9         },
        { STDKEY_SUBTRACT,   babylon::platform::Key::KeypadMinus     },
        { STDKEY_NUMPAD4,    babylon::platform::Key::Keypad4         },
        { STDKEY_NUMPAD5,    babylon::platform::Key::Keypad5         },
        { STDKEY_NUMPAD6,    babylon::platform::Key::Keypad6         },
        { STDKEY_ADD,        babylon::platform::Key::KeypadPlus      },
        { STDKEY_NUMPAD1,    babylon::platform::Key::Keypad1         },
        { STDKEY_NUMPAD2,    babylon::platform::Key::Keypad2         },
        { STDKEY_NUMPAD3,    babylon::platform::Key::Keypad3         },
        { STDKEY_NUMPAD0,    babylon::platform::Key::Keypad0         },
        { STDKEY_DECIMAL,    babylon::platform::Key::KeypadPeriod    },
        { STDKEY_F11,        babylon::platform::Key::F11             },
        { STDKEY_F12,        babylon::platform::Key::F12             },
        { STDKEY_NUMPADENTER, babylon::platform::Key::KeypadEnter    },
        { STDKEY_RCONTROL,   babylon::platform::Key::RightCtrl       },
        { STDKEY_DIVIDE,     babylon::platform::Key::KeypadDivide    },
        { STDKEY_RMENU,      babylon::platform::Key::RightAlt        },
        { STDKEY_HOME,       babylon::platform::Key::Home            },
        { STDKEY_UP,         babylon::platform::Key::Up              },
        { STDKEY_PRIOR,      babylon::platform::Key::PageUp          },
        { STDKEY_LEFT,       babylon::platform::Key::Left            },
        { STDKEY_RIGHT,      babylon::platform::Key::Right           },
        { STDKEY_END,        babylon::platform::Key::End             },
        { STDKEY_DOWN,       babylon::platform::Key::Down            },
        { STDKEY_NEXT,       babylon::platform::Key::PageDown        },
        { STDKEY_INSERT,     babylon::platform::Key::Insert          },
        { STDKEY_DELETE,     babylon::platform::Key::Delete          },
        { STDKEY_LWIN,       babylon::platform::Key::LeftMeta        },
        { STDKEY_RWIN,       babylon::platform::Key::RightMeta       },
    };

    // The Babylon mouse button of each engine mouse-button id, in the order of the engine ids
    // STDCONTROL_KID_MOUSE_LBUTTON, _RBUTTON, _MBUTTON and _XBUTTON. Babylon's X2 has no engine id.
    constexpr babylon::platform::MouseButton aMouseButtons[] =
    {
        babylon::platform::MouseButton::Left,
        babylon::platform::MouseButton::Right,
        babylon::platform::MouseButton::Middle,
        babylon::platform::MouseButton::X1,
    };

    // True when no engine key id and no Babylon key appears twice in aKeyMappings, so every entry
    // pairs one engine id with one key and both directions of the lookup are unambiguous. Keys
    // either side lacks stay out of the table, so they cannot collide.
    constexpr bool stdControlDevicesBabylon_MappingsAreOneToOne()
    {
        for ( size_t first = 0; first < STD_ARRAYLEN(aKeyMappings); ++first )
        {
            for ( size_t second = first + 1; second < STD_ARRAYLEN(aKeyMappings); ++second )
            {
                if ( aKeyMappings[first].engineKeyId == aKeyMappings[second].engineKeyId
                    || aKeyMappings[first].key == aKeyMappings[second].key )
                {
                    return false;
                }
            }
        }

        return true;
    }

    static_assert(stdControlDevicesBabylon_MappingsAreOneToOne(),
        "the key mapping must give every engine key id one Babylon key and every Babylon key one engine key id");
    static_assert(STD_ARRAYLEN(aMouseButtons) == STDCONTROL_MAX_MOUSE_BUTTONS,
        "the mouse button mapping must give every engine mouse button one Babylon mouse button");

    // The Babylon mouse button of an engine key id, or nullopt when the id is not one of the four
    // engine mouse buttons.
    std::optional<babylon::platform::MouseButton> stdControlDevicesBabylon_MouseButtonFromEngineId(int engineKeyId)
    {
        if ( engineKeyId < 0 || !STDCONTROL_ISMOUSEBUTTON((size_t)engineKeyId) )
        {
            return std::nullopt;
        }

        return aMouseButtons[(size_t)engineKeyId - STDCONTROL_KID_MOUSE_LBUTTON];
    }

    // The number of joystick slots the previous read applied, so a disconnected gamepad's buttons
    // and POV directions can be released even though InputState already forgot the pad.
    size_t stdControlDevicesBabylon_lastJoystickCount = 0;

    // The DirectInput backend's XInput path: these are XINPUT_GAMEPAD_LEFT/RIGHT_THUMB_DEADZONE and
    // XINPUT_GAMEPAD_TRIGGER_THRESHOLD over their full ranges.
    constexpr float kLeftStickDeadzone  = 7849.0f / 32767.0f;
    constexpr float kRightStickDeadzone = 8689.0f / 32767.0f;
    constexpr float kTriggerDeadzone    = 30.0f / 255.0f;

    constexpr int kStickMin = -32768;
    constexpr int kStickMax = 32767;
    constexpr int kTriggerMin = -255;
    constexpr int kTriggerMax = 255;

    // The ranges stdControlDX9.c's InitMouse registered.
    constexpr int kMouseXRange = 250;
    constexpr int kMouseYRange = 200;
    constexpr int kMouseZRange = 20;

    // stdControlDX9.c's ApplyXInputDeadzone, on the -1..1 value InputState keeps: the dead zone is
    // removed and the rest stretched back over the full range.
    float StdControlDevicesBabylon_ApplyDeadzone(float value, float deadzone)
    {
        const float magnitude = std::fabs(value);
        if ( magnitude < deadzone )
        {
            return 0.0f;
        }
        const float sign = value < 0.0f ? -1.0f : 1.0f;
        return sign * (magnitude - deadzone) / (1.0f - deadzone);
    }

    int StdControlDevicesBabylon_StickState(float value, float deadzone)
    {
        value = std::clamp(value, -1.0f, 1.0f);
        value = StdControlDevicesBabylon_ApplyDeadzone(value, deadzone);
        return static_cast<int>(std::lround(value * 32767.0f));
    }

    // The trigger byte the XInput path stored, with its 30-of-255 threshold.
    int StdControlDevicesBabylon_TriggerState(float value)
    {
        const long raw = std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f);
        return raw < 30 ? 0 : static_cast<int>(raw);
    }

    // Registers one joystick slot's six axes if they are not registered yet. stdControl_Reset()
    // clears only the enabled flags, and stdControl_Startup() zeroes the axes, so an unregistered
    // slot is registered again after a restart.
    void StdControlDevicesBabylon_RegisterJoystickAxes(size_t joyNum)
    {
        if ( stdControl_TestAxisFlag(STDCONTROL_GET_JOYSTICK_AXIS_X(joyNum),
                STDCONTROL_AXIS_REGISTERED) != 0 )
        {
            return;
        }

        stdControl_RegisterAxis(STDCONTROL_GET_JOYSTICK_AXIS_X(joyNum), kStickMin, kStickMax,
            kLeftStickDeadzone);
        stdControl_RegisterAxis(STDCONTROL_GET_JOYSTICK_AXIS_Y(joyNum), kStickMin, kStickMax,
            kLeftStickDeadzone);
        stdControl_RegisterAxis(STDCONTROL_GET_JOYSTICK_AXIS_Z(joyNum), kTriggerMin, kTriggerMax,
            kTriggerDeadzone);
        stdControl_RegisterAxis(STDCONTROL_GET_JOYSTICK_AXIS_RX(joyNum), kStickMin, kStickMax,
            kRightStickDeadzone);
        stdControl_RegisterAxis(STDCONTROL_GET_JOYSTICK_AXIS_RY(joyNum), kStickMin, kStickMax,
            kRightStickDeadzone);
        stdControl_RegisterAxis(STDCONTROL_GET_JOYSTICK_AXIS_RZ(joyNum), kTriggerMin, kTriggerMax,
            kTriggerDeadzone);

        for ( size_t axisIndex = 0; axisIndex < STDCONTROL_JOYSTICK_NUMAXES; ++axisIndex )
        {
            stdControl_SetAxisFlags(STDCONTROL_GET_JOYSTICK_AXIS(joyNum, axisIndex),
                STDCONTROL_AXIS_GAMEPAD);
        }
    }

    // Registers the mouse axes if they are not registered yet. stdControl_Startup() does it in the
    // Babylon column; this covers a reset that dropped only the registration.
    void StdControlDevicesBabylon_RegisterMouseAxes()
    {
        if ( stdControl_TestAxisFlag(STDCONTROL_AID_MOUSE_X, STDCONTROL_AXIS_REGISTERED) == 0 )
        {
            stdControl_RegisterAxis(STDCONTROL_AID_MOUSE_X, -kMouseXRange, kMouseXRange, 0.0f);
        }
        if ( stdControl_TestAxisFlag(STDCONTROL_AID_MOUSE_Y, STDCONTROL_AXIS_REGISTERED) == 0 )
        {
            stdControl_RegisterAxis(STDCONTROL_AID_MOUSE_Y, -kMouseYRange, kMouseYRange, 0.0f);
        }
        if ( stdControl_TestAxisFlag(STDCONTROL_AID_MOUSE_Z, STDCONTROL_AXIS_REGISTERED) == 0 )
        {
            stdControl_RegisterAxis(STDCONTROL_AID_MOUSE_Z, -kMouseZRange, kMouseZRange, 0.0f);
        }
    }

    // Pumps the platform queue and returns the number of connected gamepads that fit in joystick
    // slots, registering their axes when asked.
    size_t StdControlDevicesBabylon_SyncGamepads(bool bRegister)
    {
        stdBabylonEvents_Pump();
        babylon::input::InputState& rInputState = stdBabylonEvents_InputState();

        const size_t numGamepads = std::min(rInputState.gamepads().size(),
            static_cast<size_t>(STDCONTROL_MAX_JOYSTICK_DEVICES));
        if ( bRegister )
        {
            for ( size_t joyNum = 0; joyNum < numGamepads; ++joyNum )
            {
                StdControlDevicesBabylon_RegisterJoystickAxes(joyNum);
            }
        }
        return numGamepads;
    }

    // One row of the fixed gamepad button mapping: the Babylon button and the engine joystick
    // button index it becomes.
    struct StdBabylonGamepadButtonMapping
    {
        babylon::platform::GamepadButton button;
        size_t engineButtonIndex;
    };

    constexpr StdBabylonGamepadButtonMapping aGamepadButtons[] =
    {
        { babylon::platform::GamepadButton::South,          0  },
        { babylon::platform::GamepadButton::East,           1  },
        { babylon::platform::GamepadButton::West,           2  },
        { babylon::platform::GamepadButton::North,          3  },
        { babylon::platform::GamepadButton::LeftShoulder,   4  },
        { babylon::platform::GamepadButton::RightShoulder,  5  },
        { babylon::platform::GamepadButton::Back,           6  },
        { babylon::platform::GamepadButton::Start,          7  },
        { babylon::platform::GamepadButton::LeftStick,      8  },
        { babylon::platform::GamepadButton::RightStick,     9  },
        { babylon::platform::GamepadButton::Guide,          10 },
    };

    void StdControlDevicesBabylon_ApplyGamepad(size_t joyNum,
        babylon::platform::GamepadId gamepad, babylon::input::InputState& rInputState,
        uint32_t readTime)
    {
        using babylon::platform::GamepadAxis;
        using babylon::platform::GamepadButton;

        stdControl_SetAxisState(STDCONTROL_GET_JOYSTICK_AXIS_X(joyNum),
            StdControlDevicesBabylon_StickState(
                rInputState.gamepadAxis(gamepad, GamepadAxis::LeftX), kLeftStickDeadzone));
        stdControl_SetAxisState(STDCONTROL_GET_JOYSTICK_AXIS_Y(joyNum),
            StdControlDevicesBabylon_StickState(
                rInputState.gamepadAxis(gamepad, GamepadAxis::LeftY), kLeftStickDeadzone));
        stdControl_SetAxisState(STDCONTROL_GET_JOYSTICK_AXIS_Z(joyNum),
            StdControlDevicesBabylon_TriggerState(
                rInputState.gamepadAxis(gamepad, GamepadAxis::LeftTrigger)));
        stdControl_SetAxisState(STDCONTROL_GET_JOYSTICK_AXIS_RX(joyNum),
            StdControlDevicesBabylon_StickState(
                rInputState.gamepadAxis(gamepad, GamepadAxis::RightX), kRightStickDeadzone));
        stdControl_SetAxisState(STDCONTROL_GET_JOYSTICK_AXIS_RY(joyNum),
            StdControlDevicesBabylon_StickState(
                rInputState.gamepadAxis(gamepad, GamepadAxis::RightY), kRightStickDeadzone));
        stdControl_SetAxisState(STDCONTROL_GET_JOYSTICK_AXIS_RZ(joyNum),
            StdControlDevicesBabylon_TriggerState(
                rInputState.gamepadAxis(gamepad, GamepadAxis::RightTrigger)));

        for ( const StdBabylonGamepadButtonMapping& rMapping : aGamepadButtons )
        {
            stdControl_UpdateKeyState(
                STDCONTROL_JOYSTICK_GETBUTTON(joyNum, rMapping.engineButtonIndex),
                rInputState.gamepadButtonDown(gamepad, rMapping.button) ? 1 : 0, readTime);
        }

        stdControl_UpdateKeyState(STDCONTROL_JOYSTICK_GETPOVLEFT(joyNum, 0),
            rInputState.gamepadButtonDown(gamepad, GamepadButton::DpadLeft) ? 1 : 0, readTime);
        stdControl_UpdateKeyState(STDCONTROL_JOYSTICK_GETPOVUP(joyNum, 0),
            rInputState.gamepadButtonDown(gamepad, GamepadButton::DpadUp) ? 1 : 0, readTime);
        stdControl_UpdateKeyState(STDCONTROL_JOYSTICK_GETPOVRIGHT(joyNum, 0),
            rInputState.gamepadButtonDown(gamepad, GamepadButton::DpadRight) ? 1 : 0, readTime);
        stdControl_UpdateKeyState(STDCONTROL_JOYSTICK_GETPOVDOWN(joyNum, 0),
            rInputState.gamepadButtonDown(gamepad, GamepadButton::DpadDown) ? 1 : 0, readTime);
    }

    // Releases a slot no gamepad uses now, like stdControlDX9.c's ClearXInputState.
    void StdControlDevicesBabylon_ClearGamepad(size_t joyNum, uint32_t readTime)
    {
        for ( size_t axisIndex = 0; axisIndex < STDCONTROL_JOYSTICK_NUMAXES; ++axisIndex )
        {
            stdControl_SetAxisState(STDCONTROL_GET_JOYSTICK_AXIS(joyNum, axisIndex), 0);
        }

        for ( size_t buttonIndex = 0; buttonIndex < STDCONTROL_NUM_JOYSTICK_BUTTONS; ++buttonIndex )
        {
            stdControl_UpdateKeyState(STDCONTROL_JOYSTICK_GETBUTTON(joyNum, buttonIndex), 0, readTime);
        }

        for ( size_t direction = 0; direction < STDCONTROL_NUM_JOYSTICK_POVDIRECTIONS; ++direction )
        {
            stdControl_UpdateKeyState(STDCONTROL_JOYSTICK_GETPOV(joyNum, 0, direction), 0, readTime);
        }
    }
}

std::span<const StdBabylonKeyMapping> stdControlDevicesBabylon_KeyMappings()
{
    return std::span<const StdBabylonKeyMapping>(aKeyMappings);
}

babylon::platform::Key stdControlDevicesBabylon_KeyFromEngineId(int engineKeyId)
{
    for ( const StdBabylonKeyMapping& rMapping : aKeyMappings )
    {
        if ( rMapping.engineKeyId == engineKeyId )
        {
            return rMapping.key;
        }
    }

    return babylon::platform::Key::Unknown;
}

int stdControlDevicesBabylon_EngineIdFromKey(babylon::platform::Key key)
{
    for ( const StdBabylonKeyMapping& rMapping : aKeyMappings )
    {
        if ( rMapping.key == key )
        {
            return rMapping.engineKeyId;
        }
    }

    return -1;
}

#ifdef J3D_TEST
void stdControlDevicesBabylon_ResetForTest()
{
    stdBabylonEvents_ResetForTest();
    stdControlDevicesBabylon_lastJoystickCount = 0;
}
#endif

extern "C" {

size_t stdControl_GetNumJoysticks(void)
{
    // Pumping here means a pad connected before the game enumerates is visible, and so is one
    // plugged in later when the game asks again. The game enumerates only at startup
    // (JonesControl_Startup); a pad connected afterwards is read through stdControl, but the
    // gameplay bindings are not rebuilt until sithControl_RebindJoystick() runs.
    return StdControlDevicesBabylon_SyncGamepads(true);
}

const char* J3DAPI stdControl_GetJoysticDescription(int joyNum)
{
    const size_t numGamepads = StdControlDevicesBabylon_SyncGamepads(false);
    if ( joyNum < 0 || (size_t)joyNum >= numGamepads )
    {
        return "";
    }

    static char aDescription[32];
    snprintf(aDescription, sizeof(aDescription), "Gamepad%d", joyNum + 1);
    return aDescription;
}

int J3DAPI stdControl_IsGamePad(int joyNum)
{
    const size_t numGamepads = StdControlDevicesBabylon_SyncGamepads(false);
    return joyNum >= 0 && (size_t)joyNum < numGamepads ? 1 : 0;
}

void stdControl_ReadDevices(uint32_t readTime, bool bReadMouse)
{
    // The frame's events: everything the platform has queued, applied in the order it made them,
    // through the column's one pump. The pump is safe to call any number of times per frame, so
    // the window adapter may have drained the queue already; the held-key state then still says
    // what this read reports.
    stdBabylonEvents_Pump();

    babylon::input::InputState& rInputState = stdBabylonEvents_InputState();

    // The keyboard snapshot, stdControlDX9.c's stdControl_ApplyKeyboardState over the device state:
    // every mapped key is reported as it is now, so a key that went down and came up between two
    // reads never shows as down, exactly like the polled device.
    for ( const StdBabylonKeyMapping& rMapping : aKeyMappings )
    {
        stdControl_UpdateKeyState(rMapping.engineKeyId, rInputState.keyDown(rMapping.key) ? 1 : 0, readTime);
    }

    // The mouse button snapshot, stdControlDX9.c's stdControl_ApplyMouseButtonState, only for the
    // four buttons the engine has ids for.
    if ( bReadMouse )
    {
        for ( size_t i = 0; i < STD_ARRAYLEN(aMouseButtons); ++i )
        {
            const int keyId = (int)(STDCONTROL_KID_MOUSE_LBUTTON + i);
            stdControl_UpdateKeyState(keyId, rInputState.mouseButtonDown(aMouseButtons[i]) ? 1 : 0, readTime);
        }
    }

    // The mouse axes, stdControlDX9.c's stdControl_ApplyMouseAxisState in the sensitivity mode the
    // game turns on: the motion and wheel accumulated since the previous read, in DirectInput's
    // units (a wheel notch is WHEEL_DELTA, 120), clamped to the registered range. The platform
    // reports y positive down, like the DirectInput mouse.
    if ( bReadMouse )
    {
        StdControlDevicesBabylon_RegisterMouseAxes();

        const long mouseX = std::lround(rInputState.mouseDeltaX());
        const long mouseY = std::lround(rInputState.mouseDeltaY());
        const long mouseZ = std::lround(rInputState.wheelY() * 120.0f);
        stdControl_SetAxisState(STDCONTROL_AID_MOUSE_X,
            (int)std::clamp(mouseX, (long)-kMouseXRange, (long)kMouseXRange));
        stdControl_SetAxisState(STDCONTROL_AID_MOUSE_Y,
            (int)std::clamp(mouseY, (long)-kMouseYRange, (long)kMouseYRange));
        stdControl_SetAxisState(STDCONTROL_AID_MOUSE_Z,
            (int)std::clamp(mouseZ, (long)-kMouseZRange, (long)kMouseZRange));
    }

    // Every connected gamepad is one joystick slot, in InputState::gamepads() order. A slot that
    // just lost its pad is cleared, so its buttons release and its axes return to their centers.
    const size_t numGamepads = std::min(rInputState.gamepads().size(),
        static_cast<size_t>(STDCONTROL_MAX_JOYSTICK_DEVICES));
    for ( size_t joyNum = 0; joyNum < numGamepads; ++joyNum )
    {
        StdControlDevicesBabylon_RegisterJoystickAxes(joyNum);
        StdControlDevicesBabylon_ApplyGamepad(joyNum, rInputState.gamepads()[joyNum],
            rInputState, readTime);
    }
    for ( size_t joyNum = numGamepads; joyNum < stdControlDevicesBabylon_lastJoystickCount; ++joyNum )
    {
        StdControlDevicesBabylon_ClearGamepad(joyNum, readTime);
    }
    stdControlDevicesBabylon_lastJoystickCount = numGamepads;

    // The snapshot is taken: clear the per-frame flags, so the next pump records the next frame's
    // transitions, the way beginFrame before the apply of the frame's events did.
    rInputState.beginFrame();
}

bool stdControl_QueueKeyEvent(int keyId, int bPressed)
{
    StdBabylonContext& rContext = stdBabylon_RequireContext("stdControl_QueueKeyEvent");
    if ( !rContext.pScriptPlatform )
    {
        return false;
    }

    // Scripted events carry no window: 0 is the platform's "no window".
    const babylon::platform::Key key = stdControlDevicesBabylon_KeyFromEngineId(keyId);
    if ( key != babylon::platform::Key::Unknown )
    {
        rContext.pScriptPlatform->pushEvent(
            babylon::platform::KeyChanged{ 0, key, bPressed != 0, false });
        return true;
    }

    const std::optional<babylon::platform::MouseButton> button =
        stdControlDevicesBabylon_MouseButtonFromEngineId(keyId);
    if ( button )
    {
        rContext.pScriptPlatform->pushEvent(
            babylon::platform::MouseButtonChanged{ 0, *button, bPressed != 0 });
        return true;
    }

    return false;
}

} // extern "C"
