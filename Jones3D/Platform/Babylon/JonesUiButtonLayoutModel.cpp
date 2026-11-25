// Added: the model, binding, focus and scrolling helpers of the Button Layout screen.
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
    constexpr const char* kQuickWhipText      = "Quick whip";          // TODO: [COMPAT]

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

namespace JonesUiButtonLayoutInternal
{
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

        // Added: in the Modern gamepad style the step-left row carries quick whip, not a sidestep
        // (ADR-0038). Its gamepad binding is LB, so it is named for the action and is not shown as
        // a stick row; the keyboard screen and the Classic style keep "Step Left".
        const bool bQuickWhipRow = device == JonesUiButtonLayoutDevice::Controller
            && action == (std::size_t)JONESCONTROL_ACTION_STEPLEFT
            && sithControl_IsModernGamepadStyle();
        if ( bQuickWhipRow )
        {
            row.name = kQuickWhipText;
        }

        if ( JonesUiButtonLayout_GetDeviceBinding(scheme, (JonesControlActions)action, device,
                row.controlId) )
        {
            row.binding = ControlName(row.controlId);
        }

        if ( device == JonesUiButtonLayoutDevice::Controller && !bQuickWhipRow )
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
