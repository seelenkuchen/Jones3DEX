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
#include <Jones3D/Platform/Babylon/JonesUiButtonLayoutInternal.h>

using namespace JonesUiButtonLayoutInternal;

namespace
{
#ifdef J3D_TEST
    unsigned g_layoutFrames = 0;
    unsigned g_layoutRuns = 0;
    JonesUiButtonLayoutDevice g_lastDevice = JonesUiButtonLayoutDevice::Controller;
#endif

    constexpr int kJonesDialogOk     = 1;
    constexpr int kJonesDialogCancel = 2;

    constexpr const char* kBackText           = "Back";                // TODO: [COMPAT]
    constexpr const char* kBindText           = "Bind";                // TODO: [COMPAT]

    constexpr const char* kLeftStickModernText =
        "Moved with the left stick in the Modern style.";  // TODO: [COMPAT]
    constexpr const char* kRightStickModernText =
        "Moved with the right stick in the Modern style."; // TODO: [COMPAT]

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

    // Added: the prompt bar of the screen. Reset shows the pad's North/Y button or the keyboard's R,
    // so it matches the shortcuts the screen handles instead of the Change arrows.
    std::vector<babylon::ui::Prompt> BuildPrompts(const JonesUiPromptIcons& icons)
    {
        std::vector<babylon::ui::Prompt> prompts;
        prompts.push_back(babylon::ui::Prompt{ icons.back, kBackText });
        prompts.push_back(babylon::ui::Prompt{ icons.accept, kBindText });
        prompts.push_back(babylon::ui::Prompt{ icons.reset, kResetText });
        return prompts;
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

        // Added: a built-in brought back to its own defaults keeps no user copy. Removing a stale
        // one is what lets a later change to the built-in data reach this player.
        if ( index < JonesControlSchemes_GetNumDefaultSchemes()
            && JonesUiControls_SchemeMatchesDefault(scheme, index) )
        {
            static_cast<void>(JonesControlSchemes_DeleteScheme(scheme.aName));
            static_cast<void>(JonesControlSchemes_SetActiveScheme(&scheme));
            config.selectedShemeIdx = (int)index;
            return true;
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

        const JonesUiPromptAssets emptyAssets;
        const JonesUiPromptIcons& icons = JonesUiPrompts_IconsFor(
            pContext ? pContext->prompts : emptyAssets, lastDevice);
        ui.setPrompts(BuildPrompts(icons));
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

        // Added: the reset restores the device from the scheme's own built-in data when the scheme
        // is a built-in, and from the style default for a custom one.
        const auto resetDevice = [&]()
        {
            const std::size_t defaultIndex =
                modelRef.schemeIndex < JonesControlSchemes_GetNumDefaultSchemes()
                ? modelRef.schemeIndex : JonesUiControls_DefaultSchemeIndex();
            if ( ConfirmReset(uiRef, pPainter) )
            {
                JonesUiButtonLayout_ResetDevice(scheme, device, defaultIndex);
                modelRef.bChanged = true;
                JonesUiButtonLayout_RefreshRows(modelRef);
            }
        };

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
                    resetDevice();
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

            // Added: a click on the visible Reset row runs the same reset as Y/R.
            if ( ResetRowRect(uiRef.canvasSize()).contains(pointer) )
            {
                resetDevice();
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
                    resetDevice();
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
std::vector<babylon::ui::Prompt> JonesUiButtonLayout_BuildPromptsForTest(
    const JonesUiPromptIcons& icons)
{
    return BuildPrompts(icons);
}

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
