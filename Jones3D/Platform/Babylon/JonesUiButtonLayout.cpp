// Added: the "Button Layout" binding screens; see JonesUiButtonLayout.h. The screen draws one
// framed row per action in a scrollable left column and a pad diagram with action callouts on the
// right (ADR-0036). All focus, capture and file behavior lives in the model and the runner, so a
// test drives it with scripted events and a NullPainter.
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

namespace
{
#ifdef J3D_TEST
    unsigned g_layoutFrames = 0;
    unsigned g_layoutRuns = 0;
    JonesUiButtonLayoutDevice g_lastDevice = JonesUiButtonLayoutDevice::Controller;
#endif

    constexpr int kJonesDialogOk     = 1;
    constexpr int kJonesDialogCancel = 2;

    constexpr float kCanvasHeight = 480.0f;
    constexpr float kMargin       = 24.0f;
    constexpr float kHeaderRuleY  = 76.0f;
    constexpr float kListTop      = 94.0f;
    constexpr float kListGap      = 6.0f;
    constexpr float kListRowHeight = 34.0f;
    constexpr float kIconSize      = 24.0f;
    constexpr float kScrollWidth   = 4.0f;
    constexpr float kListWidthFraction = 0.40f;

    constexpr const char* kCategoryController = "CONTROLLER";          // TODO: [COMPAT]
    constexpr const char* kCategoryKeyboard   = "KEYBOARD & MOUSE";    // TODO: [COMPAT]
    constexpr const char* kTitleText          = "BUTTON LAYOUT";       // TODO: [COMPAT]
    constexpr const char* kBackText           = "Back";                // TODO: [COMPAT]
    constexpr const char* kBindText           = "Bind";                // TODO: [COMPAT]
    constexpr const char* kResetText          = "Reset Binds";         // TODO: [COMPAT]
    constexpr const char* kPressPadText       = "Press a button...";   // TODO: [COMPAT]
    constexpr const char* kPressKeyText       = "Press a key...";      // TODO: [COMPAT]
    constexpr const char* kUnboundText        = "Not bound";           // TODO: [COMPAT]
    constexpr const char* kLeftStickModernText =
        "Moved with the left stick in the Modern style.";  // TODO: [COMPAT]
    constexpr const char* kRightStickModernText =
        "Moved with the right stick in the Modern style."; // TODO: [COMPAT]

    constexpr babylon::ui::Color kRowFill      = { 0.03f, 0.03f, 0.05f, 0.72f };
    constexpr babylon::ui::Color kRowBorder    = { 0.80f, 0.76f, 0.66f, 0.35f };
    constexpr babylon::ui::Color kFocusBorder  = { 1.00f, 0.88f, 0.60f, 0.95f };
    constexpr babylon::ui::Color kAccent       = { 1.00f, 0.82f, 0.45f, 1.00f };
    constexpr babylon::ui::Color kWarmGrey     = { 0.78f, 0.74f, 0.66f, 0.85f };
    constexpr babylon::ui::Color kRule         = { 0.80f, 0.76f, 0.66f, 0.40f };
    constexpr babylon::ui::Color kGlowOuter    = { 1.00f, 0.72f, 0.32f, 0.08f };
    constexpr babylon::ui::Color kGlowInner    = { 1.00f, 0.80f, 0.45f, 0.16f };
    constexpr babylon::ui::Color kCallout      = { 0.86f, 0.82f, 0.74f, 0.90f };
    constexpr babylon::ui::Color kScrollTrack  = { 1.00f, 1.00f, 1.00f, 0.10f };
    constexpr babylon::ui::Color kScrollThumb  = { 1.00f, 0.88f, 0.60f, 0.65f };

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

    std::string UniqueCopyName(const JonesControlsConfig& config, const char* pBase)
    {
        char aName[128] = { 0 };
        std::snprintf(aName, sizeof(aName), "%s custom", pBase ? pBase : "Scheme");
        for ( int suffix = 2; suffix < 1000; ++suffix )
        {
            bool bUsed = false;
            for ( std::size_t cur = 0; cur < config.numSchemes; ++cur )
            {
                if ( std::strcmp(aName, config.aSchemes[cur].aName) == 0 )
                {
                    bUsed = true;
                    break;
                }
            }
            if ( !bUsed )
            {
                return std::string(aName);
            }
            std::snprintf(aName, sizeof(aName), "%s custom %d", pBase ? pBase : "Scheme", suffix);
        }
        return std::string("Custom");
    }

    // Turns a built-in default into a custom copy in config, the way the New dialog creates one.
    // Returns true and updates model's index when it did.
    bool EnsureCustomScheme(JonesUiButtonLayoutModel& model, std::size_t& schemeIndex)
    {
        JonesControlsConfig& config = *model.pConfig;
        const std::size_t numDefaults = JonesControlSchemes_GetNumDefaultSchemes();
        if ( schemeIndex >= numDefaults )
        {
            return true;
        }

        JonesControlsScheme copy = config.aSchemes[schemeIndex];
        const std::string name = UniqueCopyName(config, config.aSchemes[schemeIndex].aName);
        std::snprintf(copy.aName, sizeof(copy.aName), "%s", name.c_str());
        copy.bModified = 1;

        std::size_t inserted = 0;
        if ( !JonesUiControls_InsertScheme(config, copy, inserted) )
        {
            return false;
        }

        model.schemeIndex = inserted;
        schemeIndex = inserted;
        config.selectedShemeIdx = (int)inserted;
        return true;
    }

    struct CalloutAnchor
    {
        std::size_t controlId;
        std::vector<std::size_t> controls;
        float x;
        float y;
    };

    const std::vector<CalloutAnchor>& Anchors()
    {
        using babylon::platform::GamepadButton;
        static const std::vector<CalloutAnchor> anchors = {
            { ControllerControlId(GamepadButton::DpadLeft),
                { ControllerControlId(GamepadButton::DpadLeft),
                    ControllerControlId(GamepadButton::DpadRight),
                    ControllerControlId(GamepadButton::DpadUp),
                    ControllerControlId(GamepadButton::DpadDown) },
                0.24f, 0.56f },
            { ControllerControlId(GamepadButton::LeftShoulder),
                { ControllerControlId(GamepadButton::LeftShoulder) }, 0.20f, 0.20f },
            { AxisControlId(babylon::platform::GamepadAxis::LeftTrigger, 1),
                { AxisControlId(babylon::platform::GamepadAxis::LeftTrigger, 1) },
                0.18f, 0.08f },
            { ControllerControlId(GamepadButton::West),
                { ControllerControlId(GamepadButton::West) }, 0.38f, 0.24f },
            { ControllerControlId(GamepadButton::North),
                { ControllerControlId(GamepadButton::North) }, 0.50f, 0.14f },
            { ControllerControlId(GamepadButton::South),
                { ControllerControlId(GamepadButton::South) }, 0.62f, 0.24f },
            { ControllerControlId(GamepadButton::East),
                { ControllerControlId(GamepadButton::East) }, 0.74f, 0.24f },
            { ControllerControlId(GamepadButton::RightShoulder),
                { ControllerControlId(GamepadButton::RightShoulder) }, 0.80f, 0.20f },
            { AxisControlId(babylon::platform::GamepadAxis::RightTrigger, 1),
                { AxisControlId(babylon::platform::GamepadAxis::RightTrigger, 1) },
                0.82f, 0.08f },
            { ControllerControlId(GamepadButton::Back),
                { ControllerControlId(GamepadButton::Back) }, 0.38f, 0.34f },
            { ControllerControlId(GamepadButton::Start),
                { ControllerControlId(GamepadButton::Start) }, 0.62f, 0.34f },
            { ControllerControlId(GamepadButton::LeftStick),
                { ControllerControlId(GamepadButton::LeftStick) }, 0.38f, 0.64f },
            { ControllerControlId(GamepadButton::RightStick),
                { ControllerControlId(GamepadButton::RightStick) }, 0.62f, 0.64f },
            { JONESCONFIG_JOYAXISKID_UP,
                { JONESCONFIG_JOYAXISKID_UP, JONESCONFIG_JOYAXISKID_DOWN,
                    JONESCONFIG_JOYAXISKID_LEFT, JONESCONFIG_JOYAXISKID_RIGHT },
                0.38f, 0.58f },
            { JONESCONTROLSCHEMES_JOYAXIS_RX_POS,
                { JONESCONTROLSCHEMES_JOYAXIS_RX_NEG, JONESCONTROLSCHEMES_JOYAXIS_RX_POS,
                    JONESCONTROLSCHEMES_JOYAXIS_RY_NEG, JONESCONTROLSCHEMES_JOYAXIS_RY_POS },
                0.62f, 0.58f },
        };
        return anchors;
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

    JonesUiButtonLayoutAssets LoadAssets(babylon::uirender::RenderPainter& painter,
        babylon::files::FileSystem& files, const JonesControlsScheme& scheme,
        JonesUiButtonLayoutDevice device, const char* pSet)
    {
        JonesUiButtonLayoutAssets assets;
        if ( device == JonesUiButtonLayoutDevice::Controller )
        {
            assets.diagram = JonesUiAssets_LoadPng(painter, files,
                "prompts/xbox/XboxSeriesX_Diagram_Simple.png");
        }
        else
        {
            assets.mouse = JonesUiAssets_LoadPng(painter, files,
                "prompts/keyboard/Mouse_Simple_Key_Dark.png");
        }

        const auto load = [&](std::size_t controlId)
        {
            if ( controlId == kJonesUiButtonLayoutNoControl || assets.icons.count(controlId) )
            {
                return;
            }
            const std::string path = device == JonesUiButtonLayoutDevice::Controller
                ? ControllerIconPath(controlId, pSet) : KeyboardIconPath(controlId);
            if ( !path.empty() )
            {
                assets.icons[controlId] = JonesUiAssets_LoadPng(painter, files, path.c_str());
            }
        };

        for ( std::size_t action = 0; action < JONESCONTROL_ACTION_NUMACTIONS; ++action )
        {
            std::size_t controlId = 0;
            if ( JonesUiButtonLayout_GetDeviceBinding(scheme, (JonesControlActions)action,
                    device, controlId) )
            {
                load(controlId);
            }

            if ( device == JonesUiButtonLayoutDevice::Controller )
            {
                const JonesUiStickOwnership ownership =
                    JonesUiControls_GetModernStickOwnership((JonesControlActions)action);
                if ( ownership == JonesUiStickOwnership::Left )
                {
                    load(JONESCONFIG_JOYAXISKID_UP);
                }
                else if ( ownership == JonesUiStickOwnership::Right )
                {
                    load(JONESCONTROLSCHEMES_JOYAXIS_RX_POS);
                }
            }
        }

        return assets;
    }

    void DrawBorder(babylon::ui::Painter& painter, const babylon::ui::Rect& rect,
        const babylon::ui::Color& color, float width)
    {
        painter.fillRect(babylon::ui::Rect{ rect.x, rect.y, rect.width, width }, color);
        painter.fillRect(babylon::ui::Rect{ rect.x, rect.y + rect.height - width,
            rect.width, width }, color);
        painter.fillRect(babylon::ui::Rect{ rect.x, rect.y, width, rect.height }, color);
        painter.fillRect(babylon::ui::Rect{ rect.x + rect.width - width, rect.y,
            width, rect.height }, color);
    }

    void DrawPadlock(babylon::ui::Painter& painter, float x, float y,
        const babylon::ui::Color& color)
    {
        // A 10 x 12 padlock: a filled body with a thin three-piece shackle.
        painter.fillRect(babylon::ui::Rect{ x + 2.0f, y, 1.0f, 5.0f }, color);
        painter.fillRect(babylon::ui::Rect{ x + 8.0f, y, 1.0f, 5.0f }, color);
        painter.fillRect(babylon::ui::Rect{ x + 2.0f, y, 7.0f, 1.0f }, color);
        painter.fillRect(babylon::ui::Rect{ x, y + 5.0f, 10.0f, 7.0f }, color);
    }

    std::string Ellipsize(babylon::ui::Painter& painter, babylon::ui::FontId font,
        const std::string& text, float width)
    {
        if ( painter.measureText(font, text).width <= width )
        {
            return text;
        }

        const std::string ellipsis = "...";
        if ( painter.measureText(font, ellipsis).width > width )
        {
            return std::string();
        }

        std::string cut = text;
        while ( !cut.empty() )
        {
            cut.pop_back();
            if ( painter.measureText(font, cut + ellipsis).width <= width )
            {
                break;
            }
        }
        return cut + ellipsis;
    }

    // Greedily wraps text into at most maxLines lines. When words remain after the last line,
    // they are appended to it and the line is ellipsized, so a label never runs past its column.
    std::vector<std::string> WrapLines(babylon::ui::Painter& painter, babylon::ui::FontId font,
        const std::string& text, float width, std::size_t maxLines)
    {
        std::vector<std::string> lines;
        std::size_t pos = 0;
        while ( pos < text.size() && lines.size() < maxLines )
        {
            while ( pos < text.size() && text[pos] == ' ' )
            {
                ++pos;
            }
            if ( pos >= text.size() )
            {
                break;
            }

            const std::size_t end = text.find(' ', pos);
            const std::string word = text.substr(pos, end == std::string::npos
                ? std::string::npos : end - pos);
            std::string candidate = lines.empty() || !lines.back().empty()
                ? (lines.empty() ? word : lines.back() + " " + word) : word;

            if ( lines.empty() || painter.measureText(font, candidate).width <= width )
            {
                if ( lines.empty() )
                {
                    lines.push_back(word);
                }
                else
                {
                    lines.back() = candidate;
                }
            }
            else
            {
                lines.push_back(word);
            }
            pos = end == std::string::npos ? text.size() : end;
        }

        while ( pos < text.size() )
        {
            while ( pos < text.size() && text[pos] == ' ' )
            {
                ++pos;
            }
            if ( pos >= text.size() )
            {
                break;
            }
            const std::size_t end = text.find(' ', pos);
            const std::string word = text.substr(pos, end == std::string::npos
                ? std::string::npos : end - pos);
            lines.back() += " " + word;
            pos = end == std::string::npos ? text.size() : end;
        }

        for ( std::string& line : lines )
        {
            line = Ellipsize(painter, font, line, width);
        }
        return lines;
    }

    float ListWidth(const babylon::ui::Size& canvas)
    {
        return std::max(220.0f, canvas.width * kListWidthFraction);
    }

    babylon::ui::Rect RowRect(const babylon::ui::Size& canvas, std::size_t visualRow)
    {
        return babylon::ui::Rect{ kMargin,
            kListTop + (float)visualRow * (kListRowHeight + kListGap),
            ListWidth(canvas), kListRowHeight };
    }

    void DrawScrollbar(const JonesUiButtonLayoutModel& model, babylon::ui::Painter& painter,
        const babylon::ui::Size& canvas)
    {
        if ( model.rows.size() <= kJonesUiButtonLayoutVisibleRows )
        {
            return;
        }

        const float listHeight = kJonesUiButtonLayoutVisibleRows * kListRowHeight
            + (kJonesUiButtonLayoutVisibleRows - 1) * kListGap;
        const float x = kMargin + ListWidth(canvas) + 10.0f;
        painter.fillRect(babylon::ui::Rect{ x, kListTop, kScrollWidth, listHeight },
            kScrollTrack);

        const float thumbHeight = std::max(24.0f, listHeight
            * (float)kJonesUiButtonLayoutVisibleRows / (float)model.rows.size());
        const std::size_t maxScroll = model.rows.size() - kJonesUiButtonLayoutVisibleRows;
        const float thumbY = maxScroll == 0 ? kListTop
            : kListTop + (listHeight - thumbHeight) * (float)model.scroll / (float)maxScroll;
        painter.fillRect(babylon::ui::Rect{ x, thumbY, kScrollWidth, thumbHeight },
            kScrollThumb);
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

bool JonesUiButtonLayout_GetDeviceBinding(const JonesControlsScheme& scheme,
    JonesControlActions action, JonesUiButtonLayoutDevice device, std::size_t& controlId)
{
    const std::size_t count = JonesControlSchemes_GetNumBindings(&scheme, action);
    for ( std::size_t index = 0; index < count; ++index )
    {
        std::size_t bound = 0;
        if ( JonesControlSchemes_GetBinding(&scheme, action, index, &bound)
            && JonesUiButtonLayout_IsDeviceControl(bound, device) )
        {
            controlId = bound;
            return true;
        }
    }
    return false;
}

std::vector<JonesUiButtonLayoutRow> JonesUiButtonLayout_BuildRows(
    const JonesControlsScheme& scheme, JonesUiButtonLayoutDevice device)
{
    std::vector<JonesUiButtonLayoutRow> rows;
    rows.reserve(JONESCONTROL_ACTION_NUMACTIONS);

    for ( std::size_t action = 0; action < JONESCONTROL_ACTION_NUMACTIONS; ++action )
    {
        JonesUiButtonLayoutRow row;
        row.action = (JonesControlActions)action;
        row.name = ActionName((JonesControlActions)action);
        if ( JonesUiButtonLayout_GetDeviceBinding(scheme, (JonesControlActions)action, device,
                row.controlId) )
        {
            row.binding = ControlName(row.controlId);
        }

        if ( device == JonesUiButtonLayoutDevice::Controller )
        {
            row.stickOwnership = JonesUiControls_GetModernStickOwnership(
                (JonesControlActions)action);
            if ( row.stickOwnership != JonesUiStickOwnership::None )
            {
                row.controlId = row.stickOwnership == JonesUiStickOwnership::Left
                    ? JONESCONFIG_JOYAXISKID_UP : JONESCONTROLSCHEMES_JOYAXIS_RX_POS;
                row.binding.clear();
            }
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

void JonesUiButtonLayout_RefreshRows(JonesUiButtonLayoutModel& model)
{
    if ( !model.pConfig || model.schemeIndex >= model.pConfig->numSchemes )
    {
        model.rows.clear();
        model.focus = 0;
        model.scroll = 0;
        return;
    }

    model.rows = JonesUiButtonLayout_BuildRows(model.pConfig->aSchemes[model.schemeIndex],
        model.device);
    if ( model.rows.empty() )
    {
        model.focus = 0;
        model.scroll = 0;
        return;
    }

    model.focus = std::min(model.focus, model.rows.size() - 1);
    const std::size_t maxScroll = model.rows.size() > kJonesUiButtonLayoutVisibleRows
        ? model.rows.size() - kJonesUiButtonLayoutVisibleRows : 0;
    model.scroll = std::min(model.scroll, maxScroll);
    if ( model.focus < model.scroll )
    {
        model.scroll = model.focus;
    }
    else if ( model.focus >= model.scroll + kJonesUiButtonLayoutVisibleRows )
    {
        model.scroll = model.focus - kJonesUiButtonLayoutVisibleRows + 1;
    }
}

void JonesUiButtonLayout_MoveFocus(JonesUiButtonLayoutModel& model, long delta)
{
    if ( model.rows.empty() )
    {
        return;
    }

    if ( delta < 0 )
    {
        model.focus = model.focus > (std::size_t)(-delta) ? model.focus + delta : 0;
    }
    else
    {
        model.focus = std::min(model.rows.size() - 1, model.focus + (std::size_t)delta);
    }

    if ( model.focus < model.scroll )
    {
        model.scroll = model.focus;
    }
    else if ( model.focus >= model.scroll + kJonesUiButtonLayoutVisibleRows )
    {
        model.scroll = model.focus - kJonesUiButtonLayoutVisibleRows + 1;
    }
}

void JonesUiButtonLayout_Scroll(JonesUiButtonLayoutModel& model, long delta)
{
    if ( model.rows.size() <= kJonesUiButtonLayoutVisibleRows )
    {
        model.scroll = 0;
        return;
    }

    const std::size_t maxScroll = model.rows.size() - kJonesUiButtonLayoutVisibleRows;
    if ( delta < 0 )
    {
        model.scroll = model.scroll > (std::size_t)(-delta) ? model.scroll + delta : 0;
    }
    else
    {
        model.scroll = std::min(maxScroll, model.scroll + (std::size_t)delta);
    }
}

namespace
{
    std::string CalloutTextFor(const JonesControlsScheme& scheme, std::size_t controlId,
        JonesUiButtonLayoutDevice device)
    {
        std::string text;
        for ( std::size_t action = 0; action < JONESCONTROL_ACTION_NUMACTIONS; ++action )
        {
            const std::size_t count = JonesControlSchemes_GetNumBindings(&scheme,
                (JonesControlActions)action);
            for ( std::size_t index = 0; index < count; ++index )
            {
                std::size_t bound = 0;
                if ( JonesControlSchemes_GetBinding(&scheme, (JonesControlActions)action, index,
                        &bound) && bound == controlId
                    && JonesUiButtonLayout_IsDeviceControl(bound, device) )
                {
                    if ( !text.empty() )
                    {
                        text += " | ";
                    }
                    text += CapActionName((JonesControlActions)action);
                    break;
                }
            }
        }
        return text;
    }
}

std::string JonesUiButtonLayout_CalloutText(const JonesControlsScheme& scheme,
    std::size_t controlId)
{
    return CalloutTextFor(scheme, controlId, JonesUiButtonLayoutDevice::Controller);
}

bool JonesUiButtonLayout_SetDeviceBinding(JonesControlsScheme& scheme,
    JonesControlActions action, JonesUiButtonLayoutDevice device, std::size_t controlId)
{
    const std::size_t count = JonesControlSchemes_GetNumBindings(&scheme, action);
    for ( std::size_t index = 0; index < count; ++index )
    {
        std::size_t bound = 0;
        if ( JonesControlSchemes_GetBinding(&scheme, action, index, &bound)
            && JonesUiButtonLayout_IsDeviceControl(bound, device) )
        {
            return JonesControlSchemes_SetBinding(&scheme, action, index, controlId) != 0;
        }
    }
    return JonesControlSchemes_AddBinding(&scheme, action, controlId) != 0;
}

void JonesUiButtonLayout_ResetDevice(JonesControlsScheme& scheme,
    JonesUiButtonLayoutDevice device, std::size_t defaultIndex)
{
    JonesControlsScheme defaults;
    std::memset(&defaults, 0, sizeof(defaults));
    static_cast<void>(JonesControlSchemes_SetDefaultScheme(&defaults, defaultIndex));

    for ( std::size_t action = 0; action < JONESCONTROL_ACTION_NUMACTIONS; ++action )
    {
        for ( std::size_t index = JonesControlSchemes_GetNumBindings(&scheme,
                (JonesControlActions)action); index > 0; --index )
        {
            std::size_t bound = 0;
            if ( JonesControlSchemes_GetBinding(&scheme, (JonesControlActions)action,
                    index - 1, &bound)
                && JonesUiButtonLayout_IsDeviceControl(bound, device) )
            {
                static_cast<void>(JonesControlSchemes_RemoveBinding(&scheme,
                    (JonesControlActions)action, index - 1));
            }
        }

        const std::size_t count = JonesControlSchemes_GetNumBindings(&defaults,
            (JonesControlActions)action);
        for ( std::size_t index = 0; index < count; ++index )
        {
            std::size_t bound = 0;
            if ( JonesControlSchemes_GetBinding(&defaults, (JonesControlActions)action, index,
                    &bound) && JonesUiButtonLayout_IsDeviceControl(bound, device) )
            {
                static_cast<void>(JonesControlSchemes_AddBinding(&scheme,
                    (JonesControlActions)action, bound));
            }
        }
    }
    scheme.bModified = 1;
}

void JonesUiButtonLayout_Draw(const JonesUiButtonLayoutModel& model,
    babylon::ui::Painter& painter, const babylon::ui::Size& canvas,
    const babylon::ui::Skin& skin, const JonesUiButtonLayoutAssets& assets)
{
    using babylon::ui::Rect;
    using babylon::ui::TextAlign;

    const babylon::ui::FontId titleFont = skin.titleFont;
    const babylon::ui::FontId rowFont = skin.textFont;

    const char* pCategory = model.device == JonesUiButtonLayoutDevice::Controller
        ? kCategoryController : kCategoryKeyboard;
    painter.drawText(rowFont, pCategory, Rect{ kMargin, 16.0f, canvas.width - 2.0f * kMargin, 14.0f },
        TextAlign::Left, kWarmGrey);
    painter.drawText(titleFont, kTitleText,
        Rect{ kMargin, 32.0f, canvas.width - 2.0f * kMargin, 34.0f }, TextAlign::Left,
        skin.title);
    painter.fillRect(Rect{ kMargin, kHeaderRuleY, canvas.width - 2.0f * kMargin, 1.0f }, kRule);

    const float listWidth = ListWidth(canvas);
    const float listHeight = kJonesUiButtonLayoutVisibleRows * kListRowHeight
        + (kJonesUiButtonLayoutVisibleRows - 1) * kListGap;

    painter.pushClip(Rect{ kMargin - 16.0f, kListTop - 4.0f, listWidth + 36.0f,
        listHeight + 8.0f });
    for ( std::size_t visual = 0; visual < kJonesUiButtonLayoutVisibleRows; ++visual )
    {
        const std::size_t index = model.scroll + visual;
        if ( index >= model.rows.size() )
        {
            break;
        }

        const JonesUiButtonLayoutRow& row = model.rows[index];
        const Rect rect = RowRect(canvas, visual);
        const bool bFocused = index == model.focus;

        if ( bFocused )
        {
            painter.fillRect(Rect{ rect.x - 8.0f, rect.y - 5.0f, rect.width + 16.0f,
                rect.height + 10.0f }, kGlowOuter);
            painter.fillRect(Rect{ rect.x - 4.0f, rect.y - 2.0f, rect.width + 8.0f,
                rect.height + 4.0f }, kGlowInner);
        }

        painter.fillRect(rect, kRowFill);
        DrawBorder(painter, rect, bFocused ? kFocusBorder : kRowBorder, 1.0f);

        if ( bFocused && !skin.focusMarker.empty() )
        {
            painter.drawText(rowFont, skin.focusMarker,
                Rect{ kMargin - 18.0f, rect.y, 14.0f, rect.height }, TextAlign::Left,
                kAccent);
        }

        std::string label = row.name;
        if ( bFocused && model.capturing )
        {
            label = model.device == JonesUiButtonLayoutDevice::Controller
                ? kPressPadText : kPressKeyText;
        }
        const float iconSpace = kIconSize + 16.0f;
        const float lockSpace = row.stickOwnership != JonesUiStickOwnership::None ? 18.0f : 0.0f;
        babylon::ui::Color labelColor = bFocused ? skin.textFocused : skin.text;
        if ( row.stickOwnership != JonesUiStickOwnership::None )
        {
            labelColor.a *= 0.7f;
        }
        painter.drawText(rowFont, label,
            Rect{ rect.x + 12.0f, rect.y,
                rect.width - 24.0f - iconSpace - lockSpace, rect.height },
            TextAlign::Left, labelColor);

        if ( row.stickOwnership != JonesUiStickOwnership::None )
        {
            const auto icon = assets.icons.find(row.controlId);
            const float iconX = rect.x + rect.width - 12.0f - kIconSize;
            const Rect iconRect{ iconX, rect.y + (rect.height - kIconSize) * 0.5f,
                kIconSize, kIconSize };
            if ( icon != assets.icons.end() && icon->second != 0 )
            {
                painter.drawImage(icon->second, iconRect);
                painter.fillRect(iconRect, babylon::ui::Color{ 0.0f, 0.0f, 0.0f, 0.30f });
            }

            babylon::ui::Color lockColor = kWarmGrey;
            lockColor.a *= 0.80f;
            DrawPadlock(painter, iconX - 18.0f, rect.y + (rect.height - 12.0f) * 0.5f,
                lockColor);
        }
        else if ( row.controlId != kJonesUiButtonLayoutNoControl )
        {
            const auto icon = assets.icons.find(row.controlId);
            if ( icon != assets.icons.end() && icon->second != 0 )
            {
                painter.drawImage(icon->second,
                    Rect{ rect.x + rect.width - 12.0f - kIconSize,
                        rect.y + (rect.height - kIconSize) * 0.5f, kIconSize, kIconSize });
            }
            else
            {
                const float textWidth = painter.measureText(rowFont, row.binding).width;
                const float capWidth = std::max(30.0f, textWidth + 12.0f);
                const Rect cap{ rect.x + rect.width - 12.0f - capWidth,
                    rect.y + (rect.height - 24.0f) * 0.5f, capWidth, 24.0f };
                if ( model.device == JonesUiButtonLayoutDevice::KeyboardMouse )
                {
                    painter.fillRect(cap, kRowFill);
                    DrawBorder(painter, cap, bFocused ? kFocusBorder : kRowBorder, 1.0f);
                }
                painter.drawText(rowFont, row.binding, cap, TextAlign::Center,
                    bFocused ? skin.textFocused : skin.text);
            }
        }
        else
        {
            painter.drawText(rowFont, kUnboundText,
                Rect{ rect.x + rect.width - 12.0f - 100.0f, rect.y, 100.0f, rect.height },
                TextAlign::Right, bFocused ? skin.textFocused : skin.textDisabled);
        }
    }
    painter.popClip();

    DrawScrollbar(model, painter, canvas);

    // The right side: the pad or mouse drawing and one callout per physical input, in columns
    // entirely outside the image. Labels are sorted by their anchor's y and spread evenly, wrapped
    // to two lines and ellipsized so nothing overlaps or runs past the canvas.
    const float rightX = kMargin + listWidth + 34.0f;
    const float rightWidth = std::max(0.0f, canvas.width - rightX - kMargin);
    const float labelWidth = std::min(180.0f, std::max(82.0f, rightWidth * 0.22f));
    const float labelGap = 14.0f;

    std::size_t focusedControl = kJonesUiButtonLayoutNoControl;
    if ( model.focus < model.rows.size() )
    {
        focusedControl = model.rows[model.focus].controlId;
    }

    struct Callout
    {
        std::size_t controlId = 0;
        std::vector<std::size_t> controls;
        std::string text;
        std::vector<std::string> lines;
        float anchorX = 0.0f;
        float anchorY = 0.0f;
        bool bFocused = false;
    };

    std::vector<Callout> leftCallouts;
    std::vector<Callout> rightCallouts;

    const JonesControlsScheme* pScheme = nullptr;
    if ( model.pConfig && model.schemeIndex < model.pConfig->numSchemes )
    {
        pScheme = &model.pConfig->aSchemes[model.schemeIndex];
    }

    // The actions with a binding of this device in the group, in action order. Looking at every
    // binding (not only the row's first one) keeps an action visible on each physical input it
    // uses, such as a key and a mouse button.
    const auto groupActions = [&](const std::vector<std::size_t>& controls,
        std::vector<JonesControlActions>& actions, std::vector<std::string>& names)
    {
        if ( !pScheme )
        {
            return;
        }

        for ( std::size_t action = 0; action < JONESCONTROL_ACTION_NUMACTIONS; ++action )
        {
            const std::size_t count = JonesControlSchemes_GetNumBindings(pScheme,
                (JonesControlActions)action);
            for ( std::size_t index = 0; index < count; ++index )
            {
                std::size_t bound = 0;
                if ( JonesControlSchemes_GetBinding(pScheme, (JonesControlActions)action, index,
                        &bound)
                    && JonesUiButtonLayout_IsDeviceControl(bound, model.device)
                    && std::find(controls.begin(), controls.end(), bound) != controls.end() )
                {
                    actions.push_back((JonesControlActions)action);
                    names.push_back(action < model.rows.size()
                        ? model.rows[action].name : CapActionName((JonesControlActions)action));
                    break;
                }
            }
        }
    };

    const auto collect = [&](std::size_t controlId, const std::vector<std::size_t>& controls,
        float normalizedX, float normalizedY, const babylon::ui::Rect& image,
        float textWidth, const char* pOverrideText = nullptr)
    {
        std::vector<JonesControlActions> actions;
        std::vector<std::string> names;
        groupActions(controls, actions, names);
        if ( names.empty() && !pOverrideText )
        {
            return;
        }

        std::string text;
        const bool bStickMove = controls.size() == 4
            && std::find(controls.begin(), controls.end(), JONESCONFIG_JOYAXISKID_UP)
                != controls.end()
            && actions.size() == 4
            && std::find(actions.begin(), actions.end(), JONESCONTROL_ACTION_WALKFORWARD)
                != actions.end()
            && std::find(actions.begin(), actions.end(), JONESCONTROL_ACTION_WALKBACK)
                != actions.end()
            && std::find(actions.begin(), actions.end(), JONESCONTROL_ACTION_TURNLEFT)
                != actions.end()
            && std::find(actions.begin(), actions.end(), JONESCONTROL_ACTION_TURNRIGHT)
                != actions.end();
        if ( pOverrideText )
        {
            text = pOverrideText;
        }
        else if ( bStickMove )
        {
            text = "Move";
        }
        else
        {
            for ( std::size_t cur = 0; cur < names.size(); ++cur )
            {
                if ( cur > 0 )
                {
                    text += " | ";
                }
                text += names[cur];
            }
        }

        Callout callout;
        callout.controlId = controlId;
        callout.controls = controls;
        callout.text = text;
        callout.lines = WrapLines(painter, rowFont, text, textWidth, 2);
        callout.anchorX = image.x + image.width * normalizedX;
        callout.anchorY = image.y + image.height * normalizedY;
        callout.bFocused = focusedControl != kJonesUiButtonLayoutNoControl
            && std::find(controls.begin(), controls.end(), focusedControl) != controls.end();
        (normalizedX < 0.5f ? leftCallouts : rightCallouts).push_back(std::move(callout));
    };

    babylon::ui::Rect image;
    if ( model.device == JonesUiButtonLayoutDevice::Controller )
    {
        const float size = std::min(330.0f, std::max(80.0f, rightWidth
            - 2.0f * (labelWidth + labelGap) - 8.0f));
        image = babylon::ui::Rect{ rightX + (rightWidth - size) * 0.5f, 108.0f, size, size };
        if ( assets.diagram != 0 )
        {
            painter.drawImage(assets.diagram, image);
        }

        for ( const CalloutAnchor& anchor : Anchors() )
        {
            const float columnWidth = anchor.x < 0.5f
                ? std::max(1.0f, image.x - labelGap - rightX)
                : std::max(1.0f, rightX + rightWidth - (image.x + image.width + labelGap));
            const bool bHasIcon = assets.icons.find(anchor.controlId) != assets.icons.end()
                && assets.icons.at(anchor.controlId) != 0;
            const char* pOverrideText = nullptr;
            if ( sithControl_IsModernGamepadStyle() )
            {
                if ( std::find(anchor.controls.begin(), anchor.controls.end(),
                        JONESCONFIG_JOYAXISKID_UP) != anchor.controls.end() )
                {
                    pOverrideText = "Move";
                }
                else if ( std::find(anchor.controls.begin(), anchor.controls.end(),
                        JONESCONTROLSCHEMES_JOYAXIS_RX_POS) != anchor.controls.end() )
                {
                    pOverrideText = "Camera";
                }
            }
            collect(anchor.controlId, anchor.controls, anchor.x, anchor.y, image,
                columnWidth - (bHasIcon ? 20.0f : 0.0f), pOverrideText);
        }
    }
    else
    {
        const float mouseSize = std::min(260.0f, std::max(80.0f, rightWidth
            - 2.0f * (labelWidth + labelGap) - 8.0f));
        image = babylon::ui::Rect{ rightX + (rightWidth - mouseSize) * 0.5f, 108.0f,
            mouseSize, mouseSize };
        if ( assets.mouse != 0 )
        {
            painter.drawImage(assets.mouse, image);
        }

        const std::size_t aMouse[] = {
            STDCONTROL_KID_MOUSE_LBUTTON, STDCONTROL_KID_MOUSE_RBUTTON,
            STDCONTROL_KID_MOUSE_MBUTTON,
        };
        const float aX[] = { 0.34f, 0.66f, 0.50f };
        const float aY[] = { 0.32f, 0.32f, 0.22f };
        for ( std::size_t cur = 0; cur < std::size(aMouse); ++cur )
        {
            const float columnWidth = aX[cur] < 0.5f
                ? std::max(1.0f, image.x - labelGap - rightX)
                : std::max(1.0f, rightX + rightWidth - (image.x + image.width + labelGap));
            const bool bHasIcon = assets.icons.find(aMouse[cur]) != assets.icons.end()
                && assets.icons.at(aMouse[cur]) != 0;
            collect(aMouse[cur], { aMouse[cur] }, aX[cur], aY[cur], image,
                columnWidth - (bHasIcon ? 20.0f : 0.0f));
        }
    }

    const float lineHeight = 18.0f;
    const auto drawCallouts = [&](std::vector<Callout>& callouts, bool bLeft)
    {
        std::sort(callouts.begin(), callouts.end(),
            [](const Callout& a, const Callout& b) { return a.anchorY < b.anchorY; });

        const float columnX = bLeft ? rightX : image.x + image.width + labelGap;
        const float columnWidth = bLeft
            ? std::max(1.0f, image.x - labelGap - rightX)
            : std::max(1.0f, rightX + rightWidth - (image.x + image.width + labelGap));
        const float columnTop = image.y;
        const float columnHeight = model.device == JonesUiButtonLayoutDevice::Controller
            ? std::max(image.height, 420.0f - columnTop) : image.height;

        for ( std::size_t index = 0; index < callouts.size(); ++index )
        {
            Callout& callout = callouts[index];
            const float slot = columnHeight / (float)callouts.size();
            const float totalHeight = lineHeight * (float)callout.lines.size();
            const float y = columnTop + slot * ((float)index + 0.5f) - totalHeight * 0.5f;
            const babylon::ui::Color color = callout.bFocused ? kAccent : kCallout;

            const auto icon = assets.icons.find(callout.controlId);
            const bool bHasIcon = icon != assets.icons.end() && icon->second != 0;
            if ( bHasIcon )
            {
                painter.drawImage(icon->second,
                    babylon::ui::Rect{ columnX, y + (totalHeight - 16.0f) * 0.5f, 16.0f, 16.0f });
            }

            const float textX = columnX + (bHasIcon ? 20.0f : 0.0f);
            const float textWidth = std::max(1.0f, columnWidth - (bHasIcon ? 20.0f : 0.0f));
            for ( std::size_t line = 0; line < callout.lines.size(); ++line )
            {
                painter.drawText(rowFont, callout.lines[line],
                    babylon::ui::Rect{ textX, y + lineHeight * (float)line, textWidth,
                        lineHeight },
                    bLeft ? babylon::ui::TextAlign::Right : babylon::ui::TextAlign::Left,
                    color);
            }

            const float lineY = y + totalHeight * 0.5f;
            const float innerX = bLeft ? image.x - labelGap : image.x + image.width + labelGap;
            painter.fillRect(babylon::ui::Rect{ std::min(innerX, callout.anchorX), lineY,
                std::fabs(callout.anchorX - innerX), 1.0f }, color);
            if ( std::fabs(callout.anchorY - lineY) > 0.5f )
            {
                painter.fillRect(babylon::ui::Rect{ callout.anchorX,
                    std::min(lineY, callout.anchorY), 1.0f,
                    std::fabs(callout.anchorY - lineY) }, color);
            }
        }
    };

    drawCallouts(leftCallouts, true);
    drawCallouts(rightCallouts, false);

    if ( model.device == JonesUiButtonLayoutDevice::KeyboardMouse )
    {
        // The focused key in a clear band below the mouse and its callouts.
        const float largeIcon = 56.0f;
        const float textWidth = std::max(1.0f, rightWidth - largeIcon - 80.0f);
        std::string text = kUnboundText;
        std::size_t controlId = kJonesUiButtonLayoutNoControl;
        if ( model.focus < model.rows.size() )
        {
            const JonesUiButtonLayoutRow& row = model.rows[model.focus];
            controlId = row.controlId;
            if ( controlId != kJonesUiButtonLayoutNoControl )
            {
                text = row.binding + " - " + row.name;
            }
        }

        const float measured = painter.measureText(rowFont, text).width;
        const float panelWidth = largeIcon + 16.0f + std::min(measured, textWidth);
        const float x = rightX + (rightWidth - panelWidth) * 0.5f;
        const float y = image.y + image.height + 40.0f;

        const auto icon = assets.icons.find(controlId);
        if ( icon != assets.icons.end() && icon->second != 0 )
        {
            painter.drawImage(icon->second,
                babylon::ui::Rect{ x, y, largeIcon, largeIcon });
        }
        else if ( controlId != kJonesUiButtonLayoutNoControl )
        {
            const Rect cap{ x, y, largeIcon, largeIcon };
            painter.fillRect(cap, kRowFill);
            DrawBorder(painter, cap, kRowBorder, 1.0f);
            painter.drawText(rowFont, model.rows[model.focus].binding, cap,
                babylon::ui::TextAlign::Center, skin.textFocused);
        }
        painter.drawText(rowFont, Ellipsize(painter, rowFont, text, textWidth),
            babylon::ui::Rect{ x + largeIcon + 16.0f, y, textWidth, largeIcon },
            babylon::ui::TextAlign::Left, skin.textFocused);
    }

}

namespace
{
    bool ConfirmReset(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter)
    {
        babylon::ui::Screen screen;
        screen.title = kResetText;
        screen.widgets.emplace_back(babylon::ui::Label{
            "Restore this device's default bindings?", std::string() }); // TODO: [COMPAT]
        screen.widgets.emplace_back(babylon::ui::Button{
            "Yes", kJonesDialogOk, true }); // TODO: [COMPAT]
        screen.widgets.emplace_back(babylon::ui::Button{
            "No", kJonesDialogCancel, true }); // TODO: [COMPAT]
        screen.backResult = kJonesDialogCancel;
        screen.initialFocus = 1;
        return JonesUiModal_Run(ui, pPainter, screen,
            [](babylon::ui::ResultCode code) { return (int)code; }) == kJonesDialogOk;
    }

    bool MissingRequired(const JonesControlsScheme& scheme)
    {
        return !JonesUiControls_GetMissingActions(scheme).empty();
    }

    void ShowMissing(const JonesControlsScheme& scheme)
    {
        std::string text = "The following actions have no keys assigned:"; // TODO: [COMPAT]
        text += "\n";
        text += JonesUiControls_GetMissingActions(scheme);
        JonesUiControls_ShowControlMessage(text);
    }

    bool ApplyOnBack(JonesUiButtonLayoutModel& model)
    {
        JonesControlsConfig& config = *model.pConfig;
        std::size_t index = model.schemeIndex;
        JonesControlsScheme& scheme = config.aSchemes[index];

        // Nothing changed: Back closes without a required-binding check or a write, so an
        // untouched built-in default never nags or turns into a custom copy.
        if ( !model.bChanged )
        {
            return true;
        }

        if ( MissingRequired(scheme) )
        {
            ShowMissing(scheme);
            return false;
        }

        if ( !EnsureCustomScheme(model, index) )
        {
            JonesUiControls_ShowControlMessage(
                "Not enough memory for the control scheme."); // TODO: [COMPAT]
            return false;
        }

        JonesControlsScheme& applied = config.aSchemes[model.schemeIndex];
        applied.bModified = 1;
        static_cast<void>(jonesConfig_ControlOptions_WriteScheme(&applied));
        static_cast<void>(JonesControlSchemes_SetActiveScheme(&applied));
        config.selectedShemeIdx = (int)model.schemeIndex;
        return true;
    }
}

int JonesUiButtonLayout_RunScreen(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    JonesUiButtonLayoutModel& model)
{
    if ( !model.pConfig || model.schemeIndex >= model.pConfig->numSchemes )
    {
        return kJonesDialogCancel;
    }

    JonesUiButtonLayout_RefreshRows(model);

#ifdef J3D_TEST
    ++g_layoutRuns;
    g_lastDevice = model.device;
#endif

    JonesUiButtonLayoutAssets assets;
    JonesUiContext* pContext = JonesUiContext_GetActive();
    const babylon::input::LastDevice lastDevice = stdBabylonEvents_InputState().lastDevice();
    const char* pSet = PromptSet(lastDevice);
    if ( pContext )
    {
        if ( StdBabylonContext* pBabylon = stdBabylon_GetContext();
            pBabylon && pBabylon->pFileSystem )
        {
            assets = LoadAssets(pContext->painter, *pBabylon->pFileSystem,
                model.pConfig->aSchemes[model.schemeIndex], model.device, pSet);
        }
    }

    int aPadAxisDirection[static_cast<std::size_t>(babylon::platform::GamepadAxis::RightTrigger) + 1] = { 0 };

    babylon::ui::Screen screen;
    screen.title = kTitleText;
    screen.backResult = kJonesDialogOk;
    screen.widgets.emplace_back(babylon::ui::Label{});

    const JonesUiButtonLayoutDevice device = model.device;
    JonesUiButtonLayoutModel* pModel = &model;

    screen.draw = [pModel, pContext, &lastDevice, &assets, &ui](
        babylon::ui::Ui& uiRef, babylon::ui::Painter& painter, const babylon::ui::Size& canvas)
    {
        (void)uiRef;
        JonesUiButtonLayout_Draw(*pModel, painter, canvas, ui.skin(), assets);

        std::vector<babylon::ui::Prompt> prompts;
        const JonesUiPromptAssets emptyAssets;
        const JonesUiPromptIcons& icons = JonesUiPrompts_IconsFor(
            pContext ? pContext->prompts : emptyAssets, lastDevice);
        prompts.push_back(babylon::ui::Prompt{ icons.back, kBackText });
        prompts.push_back(babylon::ui::Prompt{ icons.accept, kBindText });
        prompts.push_back(babylon::ui::Prompt{ icons.changeLeft, kResetText });
        ui.setPrompts(std::move(prompts));
    };

    screen.event = [pModel, device, pPainter, &aPadAxisDirection](
        babylon::ui::Ui& uiRef, const babylon::platform::Event& event,
        const babylon::ui::Point& pointer, bool pointerValid) -> bool
    {
        JonesUiButtonLayoutModel& modelRef = *pModel;
#ifdef J3D_TEST
        ++g_layoutFrames;
#endif
        if ( !modelRef.pConfig || modelRef.schemeIndex >= modelRef.pConfig->numSchemes )
        {
            return false;
        }
        JonesControlsScheme& scheme = modelRef.pConfig->aSchemes[modelRef.schemeIndex];

        const auto cancelCapture = [&]()
        {
            modelRef.capturing = false;
        };

        const auto blockStickBinding = [&]() -> bool
        {
            if ( modelRef.focus >= modelRef.rows.size() )
            {
                return false;
            }

            const JonesUiStickOwnership ownership =
                modelRef.rows[modelRef.focus].stickOwnership;
            if ( ownership == JonesUiStickOwnership::None )
            {
                return false;
            }

            JonesUiControls_ShowControlMessage(ownership == JonesUiStickOwnership::Left
                ? kLeftStickModernText : kRightStickModernText);
            return true;
        };

        const auto finishCapture = [&](const babylon::ui::Binding& binding) -> bool
        {
            if ( blockStickBinding() )
            {
                modelRef.capturing = false;
                return true;
            }

            std::size_t controlId = 0;
            if ( !JonesUiControls_MapBinding(binding, controlId)
                || !JonesUiButtonLayout_IsDeviceControl(controlId, device) )
            {
                return true;
            }

            modelRef.capturing = false;

            std::size_t owner = 0;
            std::size_t offset = 0;
            bool bOwned = false;
            for ( std::size_t action = 0; action < JONESCONTROL_ACTION_NUMACTIONS && !bOwned;
                ++action )
            {
                const std::size_t count = JonesControlSchemes_GetNumBindings(&scheme,
                    (JonesControlActions)action);
                for ( std::size_t index = 0; index < count; ++index )
                {
                    std::size_t bound = 0;
                    if ( JonesControlSchemes_GetBinding(&scheme, (JonesControlActions)action,
                            index, &bound) && bound == controlId )
                    {
                        owner = action;
                        offset = index;
                        bOwned = true;
                        break;
                    }
                }
            }

            const JonesControlActions action = modelRef.rows[modelRef.focus].action;
            if ( bOwned && owner == (std::size_t)action )
            {
                return true;
            }

            std::size_t existing = 0;
            if ( JONESCONTROL_ACTION_HASMAXBINDS(scheme.aActions[action])
                && !JonesUiButtonLayout_GetDeviceBinding(scheme, action, device, existing) )
            {
                std::string text = "The action ";
                text += JonesControlSchemes_GetCapActionName(action);
                text += " already has the maximum number of keys."; // TODO: [COMPAT]
                JonesUiControls_ShowControlMessage(text);
                return true;
            }

            if ( bOwned )
            {
                if ( !JonesUiControls_AskReassign(uiRef, pPainter, (JonesControlActions)owner,
                        action, controlId) )
                {
                    return true;
                }
                static_cast<void>(JonesControlSchemes_RemoveBinding(&scheme,
                    (JonesControlActions)owner, offset));
            }

            static_cast<void>(JonesUiButtonLayout_SetDeviceBinding(scheme, action, device,
                controlId));
            modelRef.bChanged = true;
            JonesUiButtonLayout_RefreshRows(modelRef);
            return true;
        };

        if ( std::holds_alternative<babylon::platform::QuitRequested>(event)
            || std::holds_alternative<babylon::platform::WindowCloseRequested>(event) )
        {
            return false;
        }

        if ( const babylon::platform::KeyChanged* pKey =
                std::get_if<babylon::platform::KeyChanged>(&event) )
        {
            if ( !pKey->down )
            {
                return true;
            }

            if ( pKey->key == babylon::platform::Key::Escape )
            {
                if ( modelRef.capturing )
                {
                    cancelCapture();
                }
                else if ( ApplyOnBack(modelRef) )
                {
                    static_cast<void>(uiRef.pop(kJonesDialogOk));
                }
                return true;
            }

            if ( modelRef.capturing )
            {
                if ( device != JonesUiButtonLayoutDevice::KeyboardMouse )
                {
                    return true;
                }
                if ( pKey->key == babylon::platform::Key::Unknown )
                {
                    return true;
                }
                const babylon::ui::Binding binding{ babylon::ui::BindingKind::Key, pKey->key };
                return finishCapture(binding);
            }

            switch ( pKey->key )
            {
                case babylon::platform::Key::Up:
                    JonesUiButtonLayout_MoveFocus(modelRef, -1);
                    return true;
                case babylon::platform::Key::Down:
                    JonesUiButtonLayout_MoveFocus(modelRef, 1);
                    return true;
                case babylon::platform::Key::PageUp:
                    JonesUiButtonLayout_Scroll(modelRef, -(long)kJonesUiButtonLayoutVisibleRows);
                    return true;
                case babylon::platform::Key::PageDown:
                    JonesUiButtonLayout_Scroll(modelRef, (long)kJonesUiButtonLayoutVisibleRows);
                    return true;
                case babylon::platform::Key::Enter:
                case babylon::platform::Key::Space:
                    if ( !blockStickBinding() )
                    {
                        modelRef.capturing = true;
                    }
                    return true;
                case babylon::platform::Key::R:
                    if ( ConfirmReset(uiRef, pPainter) )
                    {
                        JonesUiButtonLayout_ResetDevice(scheme, device,
                            JonesUiControls_DefaultSchemeIndex());
                        modelRef.bChanged = true;
                        JonesUiButtonLayout_RefreshRows(modelRef);
                    }
                    return true;
                default:
                    return true;
            }
        }

        if ( const babylon::platform::MouseMoved* pMoved =
                std::get_if<babylon::platform::MouseMoved>(&event) )
        {
            if ( !pointerValid || modelRef.capturing )
            {
                return true;
            }
            for ( std::size_t visual = 0; visual < kJonesUiButtonLayoutVisibleRows; ++visual )
            {
                const std::size_t index = modelRef.scroll + visual;
                if ( index >= modelRef.rows.size() )
                {
                    break;
                }
                if ( RowRect(uiRef.canvasSize(), visual).contains(pointer) )
                {
                    modelRef.focus = index;
                    return true;
                }
            }
            return true;
        }

        if ( const babylon::platform::MouseButtonChanged* pButton =
                std::get_if<babylon::platform::MouseButtonChanged>(&event) )
        {
            if ( pButton->button != babylon::platform::MouseButton::Left || !pButton->down )
            {
                return true;
            }

            if ( modelRef.capturing && device == JonesUiButtonLayoutDevice::KeyboardMouse )
            {
                const babylon::ui::Binding binding{ babylon::ui::BindingKind::MouseButton,
                    babylon::platform::Key::Unknown, pButton->button };
                return finishCapture(binding);
            }

            if ( !pointerValid )
            {
                return true;
            }
            for ( std::size_t visual = 0; visual < kJonesUiButtonLayoutVisibleRows; ++visual )
            {
                const std::size_t index = modelRef.scroll + visual;
                if ( index >= modelRef.rows.size() )
                {
                    break;
                }
                if ( RowRect(uiRef.canvasSize(), visual).contains(pointer) )
                {
                    modelRef.focus = index;
                    if ( !blockStickBinding() )
                    {
                        modelRef.capturing = true;
                    }
                    return true;
                }
            }
            return true;
        }

        if ( const babylon::platform::MouseWheelMoved* pWheel =
                std::get_if<babylon::platform::MouseWheelMoved>(&event) )
        {
            if ( pWheel->dy > 0.0f )
            {
                JonesUiButtonLayout_Scroll(modelRef, -1);
            }
            else if ( pWheel->dy < 0.0f )
            {
                JonesUiButtonLayout_Scroll(modelRef, 1);
            }
            return true;
        }

        if ( const babylon::platform::GamepadAxisMoved* pAxis =
                std::get_if<babylon::platform::GamepadAxisMoved>(&event) )
        {
            const std::size_t axisIndex = (std::size_t)pAxis->axis;
            if ( axisIndex >= std::size(aPadAxisDirection) )
            {
                return true;
            }

            const bool bTrigger = pAxis->axis == babylon::platform::GamepadAxis::LeftTrigger
                || pAxis->axis == babylon::platform::GamepadAxis::RightTrigger;
            const int direction = pAxis->value >= 0.5f ? 1
                : (!bTrigger && pAxis->value <= -0.5f ? -1 : 0);
            const int current = aPadAxisDirection[axisIndex];
            aPadAxisDirection[axisIndex] = direction;

            if ( modelRef.capturing && device == JonesUiButtonLayoutDevice::Controller )
            {
                if ( direction != 0 && current == 0 )
                {
                    return finishCapture(babylon::ui::Binding{
                        babylon::ui::BindingKind::GamepadAxis,
                        babylon::platform::Key::Unknown, babylon::platform::MouseButton::Left,
                        babylon::platform::GamepadButton::South, pAxis->axis,
                        (float)direction });
                }
                return true;
            }

            if ( pAxis->axis == babylon::platform::GamepadAxis::LeftY
                && direction != 0 && current == 0 )
            {
                JonesUiButtonLayout_MoveFocus(modelRef, direction);
            }
            return true;
        }

        if ( const babylon::platform::GamepadButtonChanged* pButton =
                std::get_if<babylon::platform::GamepadButtonChanged>(&event) )
        {
            if ( !pButton->down )
            {
                return true;
            }

            if ( modelRef.capturing )
            {
                if ( pButton->button == babylon::platform::GamepadButton::East
                    || pButton->button == babylon::platform::GamepadButton::Back )
                {
                    cancelCapture();
                    return true;
                }
                if ( device != JonesUiButtonLayoutDevice::Controller )
                {
                    return true;
                }
                const babylon::ui::Binding binding{ babylon::ui::BindingKind::GamepadButton,
                    babylon::platform::Key::Unknown, babylon::platform::MouseButton::Left,
                    pButton->button };
                return finishCapture(binding);
            }

            using babylon::platform::GamepadButton;
            switch ( pButton->button )
            {
                case GamepadButton::DpadUp:
                    JonesUiButtonLayout_MoveFocus(modelRef, -1);
                    return true;
                case GamepadButton::DpadDown:
                    JonesUiButtonLayout_MoveFocus(modelRef, 1);
                    return true;
                case GamepadButton::LeftShoulder:
                    JonesUiButtonLayout_Scroll(modelRef, -(long)kJonesUiButtonLayoutVisibleRows);
                    return true;
                case GamepadButton::RightShoulder:
                    JonesUiButtonLayout_Scroll(modelRef, (long)kJonesUiButtonLayoutVisibleRows);
                    return true;
                case GamepadButton::South:
                    if ( !blockStickBinding() )
                    {
                        modelRef.capturing = true;
                    }
                    return true;
                case GamepadButton::North:
                    if ( ConfirmReset(uiRef, pPainter) )
                    {
                        JonesUiButtonLayout_ResetDevice(scheme, device,
                            JonesUiControls_DefaultSchemeIndex());
                        modelRef.bChanged = true;
                        JonesUiButtonLayout_RefreshRows(modelRef);
                    }
                    return true;
                case GamepadButton::East:
                case GamepadButton::Back:
                    if ( ApplyOnBack(modelRef) )
                    {
                        static_cast<void>(uiRef.pop(kJonesDialogOk));
                    }
                    return true;
                default:
                    return true;
            }
        }

        return false;
    };

    const int result = JonesUiModal_Run(ui, pPainter, screen,
        [](babylon::ui::ResultCode code) { return (int)code; });
    return result;
}

#ifdef J3D_TEST
unsigned JonesUiButtonLayout_GetLayoutFramesForTest(void)
{
    return g_layoutFrames;
}

unsigned JonesUiButtonLayout_GetLayoutRunsForTest(void)
{
    return g_layoutRuns;
}

JonesUiButtonLayoutDevice JonesUiButtonLayout_GetLastDeviceForTest(void)
{
    return g_lastDevice;
}

void JonesUiButtonLayout_ResetLayoutFramesForTest(void)
{
    g_layoutFrames = 0;
    g_layoutRuns = 0;
    g_lastDevice = JonesUiButtonLayoutDevice::Controller;
}
#endif
