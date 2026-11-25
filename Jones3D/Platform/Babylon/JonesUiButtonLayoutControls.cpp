// Added: the control naming and prompt icon helpers of the Button Layout screen.
#include <Jones3D/Platform/Babylon/JonesUiButtonLayout.h>

#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Platform/Babylon/JonesControlSchemes.h>
#include <Jones3D/Platform/Babylon/JonesUiAssets.h>
#include <Jones3D/Platform/Babylon/JonesUiContext.h>
#include <Jones3D/Platform/Babylon/JonesUiModal.h>
#include <Jones3D/Platform/Babylon/JonesUiPrompts.h>

#include <std/Babylon/stdBabylon.h>
#include <std/Babylon/stdBabylonEvents.h>
#include <std/General/stdControl.h>
#include <std/General/stdKeys.h>

#include <sith/Devices/sithControl.h>

#include <babylon/files/FileSystem.h>
#include <babylon/platform/Keys.h>
#include <babylon/input/InputState.h>
#include <babylon/ui/Binding.h>
#include <babylon/ui/Screen.h>
#include <babylon/ui/UiEvent.h>
#include <babylon/ui/Widget.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <Jones3D/Platform/Babylon/JonesUiButtonLayoutInternal.h>

using namespace JonesUiButtonLayoutInternal;

namespace
{
    bool IsMouseControl(std::size_t controlId)
    {
        return controlId >= STDCONTROL_KID_MOUSE_LBUTTON
            && controlId <= STDCONTROL_KID_MOUSE_XBUTTON;
    }

    bool IsKeyboardControl(std::size_t controlId)
    {
        return controlId < STDCONTROL_MAX_KEYBOARD_BUTTONS;
    }

    bool IsControllerControl(std::size_t controlId)
    {
        if ( controlId >= STDCONTROL_JOYSTICK_FIRSTKID
            && controlId <= STDCONTROL_JOYSTICK_LASTPOVCID )
        {
            return true;
        }

        return controlId == JONESCONFIG_JOYAXISKID_UP || controlId == JONESCONFIG_JOYAXISKID_RIGHT
            || controlId == JONESCONFIG_JOYAXISKID_DOWN || controlId == JONESCONFIG_JOYAXISKID_LEFT
            || controlId == JONESCONFIG_JOYAXISKID_TWISTL
            || controlId == JONESCONFIG_JOYAXISKID_TWISTR
            || controlId == JONESCONFIG_JOYAXISKID_AIR || controlId == JONESCONFIG_JOYAXISKID_GROUND
            || controlId == JONESCONTROLSCHEMES_JOYAXIS_RX_NEG
            || controlId == JONESCONTROLSCHEMES_JOYAXIS_RX_POS
            || controlId == JONESCONTROLSCHEMES_JOYAXIS_RY_NEG
            || controlId == JONESCONTROLSCHEMES_JOYAXIS_RY_POS;
    }
}

namespace JonesUiButtonLayoutInternal
{
    std::string ActionName(JonesControlActions action)
    {
        const char* pName = JonesControlSchemes_GetActionName(action);
        return pName ? std::string(pName) : std::string();
    }

    std::string CapActionName(JonesControlActions action)
    {
        const char* pName = JonesControlSchemes_GetCapActionName(action);
        return pName ? std::string(pName) : std::string();
    }

    std::string ControlName(std::size_t controlId)
    {
        char aName[JONESCONFIG_CONTROLSTRING_MAXLEN] = { 0 };
        jonesConfig_ControlToString(controlId, aName);
        return std::string(aName);
    }

    std::size_t ControllerControlId(babylon::platform::GamepadButton button)
    {
        using babylon::platform::GamepadButton;
        switch ( button )
        {
            case GamepadButton::DpadLeft:    return STDCONTROL_JOYSTICK_GETPOVLEFT(0, 0);
            case GamepadButton::DpadRight:   return STDCONTROL_JOYSTICK_GETPOVRIGHT(0, 0);
            case GamepadButton::DpadUp:      return STDCONTROL_JOYSTICK_GETPOVUP(0, 0);
            case GamepadButton::DpadDown:    return STDCONTROL_JOYSTICK_GETPOVDOWN(0, 0);
            case GamepadButton::South:       return STDCONTROL_JOYSTICK_GETBUTTON(0, 0);
            case GamepadButton::East:        return STDCONTROL_JOYSTICK_GETBUTTON(0, 1);
            case GamepadButton::West:        return STDCONTROL_JOYSTICK_GETBUTTON(0, 2);
            case GamepadButton::North:       return STDCONTROL_JOYSTICK_GETBUTTON(0, 3);
            case GamepadButton::LeftShoulder: return STDCONTROL_JOYSTICK_GETBUTTON(0, 4);
            case GamepadButton::RightShoulder: return STDCONTROL_JOYSTICK_GETBUTTON(0, 5);
            case GamepadButton::Back:        return STDCONTROL_JOYSTICK_GETBUTTON(0, 6);
            case GamepadButton::Start:       return STDCONTROL_JOYSTICK_GETBUTTON(0, 7);
            case GamepadButton::LeftStick:   return STDCONTROL_JOYSTICK_GETBUTTON(0, 8);
            case GamepadButton::RightStick:  return STDCONTROL_JOYSTICK_GETBUTTON(0, 9);
            case GamepadButton::Guide:       return STDCONTROL_JOYSTICK_GETBUTTON(0, 10);
            default:                         return kJonesUiButtonLayoutNoControl;
        }
    }

    std::size_t AxisControlId(babylon::platform::GamepadAxis axis, int direction)
    {
        using babylon::platform::GamepadAxis;
        switch ( axis )
        {
            case GamepadAxis::LeftX:
                return direction < 0 ? JONESCONFIG_JOYAXISKID_LEFT
                                     : JONESCONFIG_JOYAXISKID_RIGHT;
            case GamepadAxis::LeftY:
                return direction < 0 ? JONESCONFIG_JOYAXISKID_UP
                                     : JONESCONFIG_JOYAXISKID_DOWN;
            case GamepadAxis::RightX:
                return direction < 0 ? JONESCONTROLSCHEMES_JOYAXIS_RX_NEG
                                     : JONESCONTROLSCHEMES_JOYAXIS_RX_POS;
            case GamepadAxis::RightY:
                return direction < 0 ? JONESCONTROLSCHEMES_JOYAXIS_RY_NEG
                                     : JONESCONTROLSCHEMES_JOYAXIS_RY_POS;
            case GamepadAxis::LeftTrigger:
                return JONESCONFIG_JOYAXISKID_AIR;
            case GamepadAxis::RightTrigger:
                return JONESCONFIG_JOYAXISKID_GROUND;
            default:
                return kJonesUiButtonLayoutNoControl;
        }
    }

    std::string ControllerIconPath(std::size_t controlId, const char* pSet)
    {
        using babylon::platform::GamepadButton;

        std::string prefix;
        const bool bSwitch = std::strcmp(pSet, "switch") == 0;
        const bool bSteamDeck = std::strcmp(pSet, "steamdeck") == 0;
        const bool bPlayStation = std::strcmp(pSet, "ps5") == 0;
        if ( bSwitch )
        {
            prefix = "prompts/switch/Switch_";
        }
        else if ( bSteamDeck )
        {
            prefix = "prompts/steamdeck/SteamDeck_";
        }
        else if ( bPlayStation )
        {
            prefix = "prompts/ps5/PS5_";
        }
        else
        {
            prefix = "prompts/xbox/XboxSeriesX_";
        }

        const std::size_t south = ControllerControlId(GamepadButton::South);
        const std::size_t east = ControllerControlId(GamepadButton::East);
        const std::size_t west = ControllerControlId(GamepadButton::West);
        const std::size_t north = ControllerControlId(GamepadButton::North);
        const std::size_t lb = ControllerControlId(GamepadButton::LeftShoulder);
        const std::size_t rb = ControllerControlId(GamepadButton::RightShoulder);
        const std::size_t back = ControllerControlId(GamepadButton::Back);
        const std::size_t start = ControllerControlId(GamepadButton::Start);
        const std::size_t ls = ControllerControlId(GamepadButton::LeftStick);
        const std::size_t rs = ControllerControlId(GamepadButton::RightStick);
        const std::size_t guide = ControllerControlId(GamepadButton::Guide);
        const std::size_t dpadUp = ControllerControlId(GamepadButton::DpadUp);
        const std::size_t dpadDown = ControllerControlId(GamepadButton::DpadDown);
        const std::size_t dpadLeft = ControllerControlId(GamepadButton::DpadLeft);
        const std::size_t dpadRight = ControllerControlId(GamepadButton::DpadRight);

        if ( controlId == south ) return prefix + (bPlayStation ? "Cross.png" : bSwitch ? "B.png" : "A.png");
        if ( controlId == east ) return prefix + (bPlayStation ? "Circle.png" : bSwitch ? "A.png" : "B.png");
        if ( controlId == west ) return prefix + (bPlayStation ? "Square.png" : bSwitch ? "Y.png" : "X.png");
        if ( controlId == north ) return prefix + (bPlayStation ? "Triangle.png" : bSwitch ? "X.png" : "Y.png");
        if ( controlId == lb )
            return prefix + (bPlayStation ? "L1.png" : std::strcmp(pSet, "xbox") == 0 ? "LB.png" : "L1.png");
        if ( controlId == rb )
            return prefix + (bPlayStation ? "R1.png" : std::strcmp(pSet, "xbox") == 0 ? "RB.png" : "R1.png");
        if ( controlId == back )
            return prefix + (bPlayStation ? "Share.png" : std::strcmp(pSet, "xbox") == 0 ? "View.png" : "Minus.png");
        if ( controlId == start )
            return prefix + (bPlayStation ? "Options.png" : std::strcmp(pSet, "xbox") == 0 ? "Menu.png" : "Plus.png");
        if ( controlId == ls ) return prefix + "Left_Stick_Click.png";
        if ( controlId == rs ) return prefix + "Right_Stick_Click.png";
        if ( controlId == guide ) return prefix + (bPlayStation ? "Touch_Pad.png" : std::strcmp(pSet, "xbox") == 0 ? "Share.png" : "Home.png");
        if ( controlId == dpadUp ) return prefix + "Dpad_Up.png";
        if ( controlId == dpadDown ) return prefix + "Dpad_Down.png";
        if ( controlId == dpadLeft ) return prefix + "Dpad_Left.png";
        if ( controlId == dpadRight ) return prefix + "Dpad_Right.png";
        if ( controlId == JONESCONFIG_JOYAXISKID_LEFT || controlId == JONESCONFIG_JOYAXISKID_RIGHT
            || controlId == JONESCONFIG_JOYAXISKID_UP || controlId == JONESCONFIG_JOYAXISKID_DOWN )
        {
            return prefix + "Left_Stick.png";
        }
        if ( controlId == JONESCONTROLSCHEMES_JOYAXIS_RX_NEG
            || controlId == JONESCONTROLSCHEMES_JOYAXIS_RX_POS
            || controlId == JONESCONTROLSCHEMES_JOYAXIS_RY_NEG
            || controlId == JONESCONTROLSCHEMES_JOYAXIS_RY_POS )
        {
            return prefix + "Right_Stick.png";
        }
        if ( controlId == JONESCONFIG_JOYAXISKID_AIR )
            return prefix + (bPlayStation ? "L2.png" : std::strcmp(pSet, "xbox") == 0 ? "LT.png" : "L2.png");
        if ( controlId == JONESCONFIG_JOYAXISKID_GROUND )
            return prefix + (bPlayStation ? "R2.png" : std::strcmp(pSet, "xbox") == 0 ? "RT.png" : "R2.png");
        return std::string();
    }

    std::string KeyboardIconPath(std::size_t controlId)
    {
        if ( controlId >= STDKEY_1 && controlId <= STDKEY_0 )
        {
            const char aDigits[] = "1234567890";
            return std::string(1, aDigits[controlId - STDKEY_1]) + "_Key_Dark.png";
        }
        {
            // The engine letters are scan codes, so map them by their known values.
            static const char* const aLetters[] = {
                "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P",
                "A", "S", "D", "F", "G", "H", "J", "K", "L",
                "Z", "X", "C", "V", "B", "N", "M",
            };
            static const std::size_t aIds[] = {
                STDKEY_Q, STDKEY_W, STDKEY_E, STDKEY_R, STDKEY_T, STDKEY_Y, STDKEY_U,
                STDKEY_I, STDKEY_O, STDKEY_P, STDKEY_A, STDKEY_S, STDKEY_D, STDKEY_F,
                STDKEY_G, STDKEY_H, STDKEY_J, STDKEY_K, STDKEY_L, STDKEY_Z, STDKEY_X,
                STDKEY_C, STDKEY_V, STDKEY_B, STDKEY_N, STDKEY_M,
            };
            for ( std::size_t cur = 0; cur < std::size(aIds); ++cur )
            {
                if ( aIds[cur] == controlId )
                {
                    return std::string(aLetters[cur]) + "_Key_Dark.png";
                }
            }
        }
        if ( controlId >= STDKEY_F1 && controlId <= STDKEY_F12 )
        {
            static const char* const aF[] = {
                "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
            };
            return std::string(aF[controlId - STDKEY_F1]) + "_Key_Dark.png";
        }
        switch ( controlId )
        {
            case STDKEY_ESCAPE:    return "Esc_Key_Dark.png";
            case STDKEY_MINUS:     return "Minus_Key_Dark.png";
            case STDKEY_EQUALS:    return "Plus_Key_Dark.png";
            case STDKEY_BACK:      return "Backspace_Alt_Key_Dark.png";
            case STDKEY_TAB:       return "Tab_Key_Dark.png";
            case STDKEY_LBRACKET:  return "Bracket_Left_Key_Dark.png";
            case STDKEY_RBRACKET:  return "Bracket_Right_Key_Dark.png";
            case STDKEY_RETURN:    return "Enter_Key_Dark.png";
            case STDKEY_LCONTROL:
            case STDKEY_RCONTROL:  return "Ctrl_Key_Dark.png";
            case STDKEY_SEMICOLON: return "Semicolon_Key_Dark.png";
            case STDKEY_APOSTROPHE: return "Quote_Key_Dark.png";
            case STDKEY_GRAVE:     return "Tilda_Key_Dark.png";
            case STDKEY_LSHIFT:
            case STDKEY_RSHIFT:    return "Shift_Key_Dark.png";
            case STDKEY_SLASH:     return "Slash_Key_Dark.png";
            case STDKEY_MULTIPLY:  return "Asterisk_Key_Dark.png";
            case STDKEY_LMENU:
            case STDKEY_RMENU:     return "Alt_Key_Dark.png";
            case STDKEY_SPACE:     return "Space_Key_Dark.png";
            case STDKEY_CAPITAL:   return "Caps_Lock_Key_Dark.png";
            case STDKEY_NUMLOCK:   return "Num_Lock_Key_Dark.png";
            case STDKEY_SUBTRACT:  return "Minus_Key_Dark.png";
            case STDKEY_ADD:       return "Plus_Tall_Key_Dark.png";
            case STDKEY_NUMPAD0:   return "0_Key_Dark.png";
            case STDKEY_NUMPAD1:   return "1_Key_Dark.png";
            case STDKEY_NUMPAD2:   return "2_Key_Dark.png";
            case STDKEY_NUMPAD3:   return "3_Key_Dark.png";
            case STDKEY_NUMPAD4:   return "4_Key_Dark.png";
            case STDKEY_NUMPAD5:   return "5_Key_Dark.png";
            case STDKEY_NUMPAD6:   return "6_Key_Dark.png";
            case STDKEY_NUMPAD7:   return "7_Key_Dark.png";
            case STDKEY_NUMPAD8:   return "8_Key_Dark.png";
            case STDKEY_NUMPAD9:   return "9_Key_Dark.png";
            case STDKEY_NUMPADENTER: return "Enter_Tall_Key_Dark.png";
            case STDKEY_DIVIDE:    return "Slash_Key_Dark.png";
            case STDKEY_HOME:      return "Home_Key_Dark.png";
            case STDKEY_UP:        return "Arrow_Up_Key_Dark.png";
            case STDKEY_PRIOR:     return "Page_Up_Key_Dark.png";
            case STDKEY_LEFT:      return "Arrow_Left_Key_Dark.png";
            case STDKEY_RIGHT:     return "Arrow_Right_Key_Dark.png";
            case STDKEY_END:       return "End_Key_Dark.png";
            case STDKEY_DOWN:      return "Arrow_Down_Key_Dark.png";
            case STDKEY_NEXT:      return "Page_Down_Key_Dark.png";
            case STDKEY_INSERT:    return "Insert_Key_Dark.png";
            case STDKEY_DELETE:    return "Del_Key_Dark.png";
            case STDKEY_F11:       return "F11_Key_Dark.png";
            case STDKEY_F12:       return "F12_Key_Dark.png";
            case STDKEY_LWIN:
            case STDKEY_RWIN:      return "Win_Key_Dark.png";
            case STDCONTROL_KID_MOUSE_LBUTTON: return "Mouse_Left_Key_Dark.png";
            case STDCONTROL_KID_MOUSE_RBUTTON: return "Mouse_Right_Key_Dark.png";
            case STDCONTROL_KID_MOUSE_MBUTTON: return "Mouse_Middle_Key_Dark.png";
            default:               return std::string();
        }
    }

    const char* PromptSet(const babylon::input::LastDevice& device)
    {
        switch ( device.gamepadType )
        {
            case babylon::platform::GamepadType::Nintendo: return "switch";
            case babylon::platform::GamepadType::SteamDeck: return "steamdeck";
            default: return "xbox";
        }
    }
}

std::string JonesUiButtonLayout_GetPromptIconPath(std::size_t controlId,
    JonesUiButtonLayoutDevice device, const char* pControllerSet)
{
    if ( device == JonesUiButtonLayoutDevice::KeyboardMouse )
    {
        const std::string file = KeyboardIconPath(controlId);
        return file.empty() ? std::string() : std::string("prompts/keyboard/") + file;
    }

    return ControllerIconPath(controlId, pControllerSet && *pControllerSet ? pControllerSet : "xbox");
}

bool JonesUiButtonLayout_IsDeviceControl(std::size_t controlId,
    JonesUiButtonLayoutDevice device)
{
    if ( device == JonesUiButtonLayoutDevice::KeyboardMouse )
    {
        return IsKeyboardControl(controlId) || IsMouseControl(controlId);
    }
    return IsControllerControl(controlId);
}
