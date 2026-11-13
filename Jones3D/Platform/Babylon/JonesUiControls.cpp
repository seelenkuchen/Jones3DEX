// Added: the Babylon column's Controls screen; see JonesUiControls.h.
//
// Original behavior, from the dialogs in
// (git show 82b8f607^:Jones3D/Platform/Win32/jonesConfig.c):
// - jonesConfig_ShowControlOptions (line 4787) loads the built-in schemes and every .kfg in
//   the key-sets directory, works on a copy, and returns 1 on OK and 2 on cancel or when the load
//   fails. On OK it writes controls.configFile, binds the selected scheme and applies controls.mouse
//   (line 4803-4829).
// - The main dialog's OK (line 5091) removes the schemes queued for deletion, writes every modified
//   custom scheme past the built-in defaults (the default schemes are never written), stores controls.mouse and
//   binds the active scheme. The gamepad is always on, so a connected pad's bindings are always
//   applied and the joystick axes stay enabled.
// - The New dialog (line 5713-5808) limits the name to 60 characters and rejects an empty name
//   (JONES_STR_NO_SCHEME), the characters /:\"?<>*|+ (JONES_STR_INVALIDFILE) and a duplicate name
//   (JONES_STR_DUP_SCHEME). A new scheme starts as a copy of default scheme 0 and is inserted among
//   the custom schemes in name order (line 5559-5678).
// - The main dialog deletes a custom scheme at once (line 5404-5472); the file is only queued and
//   removed on OK, so Cancel keeps it. A built-in scheme cannot be deleted.
// - The Edit dialog (line 5810-6371) lists the actions, disables Add/Remove/OK for a built-in
//   scheme, refuses OK while a non-weapon action has no binding (JONES_STR_NO_MAPNG), and sets
//   bModified on OK. Its Add path (Assign Key, line 6372-6440) rejects a control already bound to
//   the same action (JONES_STR_SAMEMAP), asks before moving one bound to another action
//   (Reassign Key, line 6806-6954), and refuses a ninth binding (JONES_STR_EXCEED).
//
// The one list here holds both keyboard and controller controls, so the original's separate key and
// controller lists and its "key only"/"button only" checks do not apply. The rest of the contract,
// the files and the config keys are unchanged.
#include <Jones3D/Platform/Babylon/JonesUiControls.h>

#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Play/JonesControl.h>
#include <Jones3D/Platform/Babylon/JonesControlSchemes.h>
#include <Jones3D/Platform/Babylon/JonesUiContext.h>
#include <Jones3D/Platform/Babylon/JonesUiModal.h>

#include <sith/Devices/sithControl.h>

#include <std/Babylon/stdBabylon.h>
#include <std/Babylon/stdControlDevicesBabylon.h>
#include <std/General/std.h>
#include <std/General/stdConfig.h>
#include <std/General/stdControl.h>
#include <std/General/stdMemory.h>

#include <babylon/ui/Binding.h>
#include <babylon/ui/Screen.h>
#include <babylon/ui/Types.h>
#include <babylon/ui/Ui.h>
#include <babylon/ui/UiEvent.h>
#include <babylon/ui/Widget.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace
{
    constexpr int kJonesDialogOk     = 1;
    constexpr int kJonesDialogCancel = 2;

    // The main screen's own controls; OK and Cancel keep the dialog result values so the modal loop
    // can close the screen with them unchanged.
    constexpr babylon::ui::ActionId kJonesControlsActionNew    = 10;
    constexpr babylon::ui::ActionId kJonesControlsActionDelete = 11;
    constexpr babylon::ui::ActionId kJonesControlsActionEdit   = 12;
    constexpr babylon::ui::ActionId kJonesControlsActionReset  = 13;

    constexpr babylon::ui::ActionId kJonesEditActionAccept = 20;
    constexpr babylon::ui::ActionId kJonesAssignActionRemove = 30;

    // Widget indices of the main screen.
    constexpr std::size_t kJonesMainChoice     = 0;
    constexpr std::size_t kJonesMainNew        = 1;
    constexpr std::size_t kJonesMainDelete     = 2;
    constexpr std::size_t kJonesMainMouse      = 3;
    constexpr std::size_t kJonesMainEdit       = 4;
    constexpr std::size_t kJonesMainReset      = 5;

    // Widget indices of the Edit screen and the assignment screen.
    constexpr std::size_t kJonesEditList = 0;
    constexpr std::size_t kJonesEditOk   = 1;
    constexpr std::size_t kJonesEditBack = 2;

    constexpr std::size_t kJonesAssignCapture = 0;
    constexpr std::size_t kJonesAssignRemove  = 1;
    constexpr std::size_t kJonesAssignBack    = 2;

    // Added: the original dialog captions came from the dialog resources, which hold no JONES_STR
    // key. The English text is used until one exists.
    constexpr const char* kJonesControlsSchemeText     = "Scheme";            // TODO: [COMPAT]
    constexpr const char* kJonesControlsNewText        = "New";               // TODO: [COMPAT]
    constexpr const char* kJonesControlsDeleteText     = "Delete";            // TODO: [COMPAT]
    constexpr const char* kJonesControlsMouseText      = "Mouse";             // TODO: [COMPAT]
    constexpr const char* kJonesControlsResetText      = "Reset to defaults"; // TODO: [COMPAT]
    constexpr const char* kJonesControlsNameText       = "Name";              // TODO: [COMPAT]
    constexpr const char* kJonesControlsAddText        = "Add binding";       // TODO: [COMPAT]
    constexpr const char* kJonesControlsRemoveText     = "Remove";            // TODO: [COMPAT]
    constexpr const char* kJonesControlsOkText         = "OK";
    constexpr const char* kJonesControlsFallbackTitle  = "Control Options";   // TODO: [COMPAT]

    // Added: English fallbacks for the original messages, used only when no game string table is
    // loaded (a unit test or the startup before the strings open).
    constexpr const char* kJonesControlsNoSchemeText =
        "Please enter a name."; // TODO: [COMPAT]
    constexpr const char* kJonesControlsInvalidFileText =
        "The name \"%s\" is not a valid file name."; // TODO: [COMPAT]
    constexpr const char* kJonesControlsDuplicateText =
        "A scheme with that name already exists."; // TODO: [COMPAT]
    constexpr const char* kJonesControlsNoMappingText =
        "The following actions have no keys assigned:"; // TODO: [COMPAT]
    constexpr const char* kJonesControlsExceedText =
        "The action %s already has the maximum number of keys."; // TODO: [COMPAT]
    constexpr const char* kJonesControlsSameMapText =
        "is already assigned to"; // TODO: [COMPAT]
    constexpr const char* kJonesControlsNewMapText =
        "%s is already assigned."; // TODO: [COMPAT]
    constexpr const char* kJonesControlsFromText = "From: %s"; // TODO: [COMPAT]
    constexpr const char* kJonesControlsToText   = "To: %s";   // TODO: [COMPAT]
    constexpr const char* kJonesControlsNoMemText =
        "Not enough memory for the control scheme."; // TODO: [COMPAT]

    int Identity(babylon::ui::ResultCode code)
    {
        return (int)code;
    }

    // Resolves a JONES_STR_ key through the game's string table. When the table has no such key
    // (the repo holds no game assets), the readable English text is used instead.
    std::string ResolveLabel(const char* pKey, const char* pFallback)
    {
        const std::string text = JonesUiContext_ResolveString(pKey);
        return text == pKey ? std::string(pFallback) : text;
    }

    // Formats a resolved format string with one string argument. A string without a conversion
    // keeps its text, so an unresolved format does not drop or garble the argument.
    std::string FormatLabel(const std::string& format, const std::string& argument)
    {
        if ( format.find('%') == std::string::npos )
        {
            return format;
        }

        char aText[1024] = { 0 };
        std::snprintf(aText, sizeof(aText), format.c_str(), argument.c_str());
        return std::string(aText);
    }

    void ShowMessage(const std::string& text)
    {
        static_cast<void>(jonesConfig_ShowMessageDialog(NULL, "JONES_STR_CTRL_OPTS", text.c_str(), 145));
    }

    std::string ControlName(std::size_t controlId)
    {
        char aName[JONESCONFIG_CONTROLSTRING_MAXLEN] = { 0 };
        jonesConfig_ControlToString(controlId, aName);
        return std::string(aName);
    }

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

    // Added: the keyboard-and-gamepad scheme is the default for new installs.
    std::size_t JonesUiControls_DefaultSchemeIndex(void)
    {
        return 3u;
    }

    bool FindBinding(const JonesControlsScheme& scheme, std::size_t controlId,
        std::size_t& action, std::size_t& offset)
    {
        for ( std::size_t cur = 0; cur < JONESCONTROL_ACTION_NUMACTIONS; ++cur )
        {
            const std::size_t count = JonesControlSchemes_GetNumBindings(&scheme,
                (JonesControlActions)cur);
            for ( std::size_t index = 0; index < count; ++index )
            {
                std::size_t bound = 0;
                if ( JonesControlSchemes_GetBinding(&scheme, (JonesControlActions)cur, index, &bound)
                    && bound == controlId )
                {
                    action = cur;
                    offset = index;
                    return true;
                }
            }
        }

        return false;
    }

    std::vector<babylon::ui::ListItem> BuildActionItems(const JonesControlsScheme& scheme)
    {
        std::vector<babylon::ui::ListItem> items;
        items.reserve(JONESCONTROL_ACTION_NUMACTIONS);

        for ( std::size_t action = 0; action < JONESCONTROL_ACTION_NUMACTIONS; ++action )
        {
            std::string text = ActionName((JonesControlActions)action);

            const std::size_t count = JonesControlSchemes_GetNumBindings(&scheme,
                (JonesControlActions)action);
            for ( std::size_t index = 0; index < count; ++index )
            {
                std::size_t controlId = 0;
                if ( !JonesControlSchemes_GetBinding(&scheme, (JonesControlActions)action, index,
                        &controlId) )
                {
                    continue;
                }

                text += index == 0 ? "  " : ", ";
                text += ControlName(controlId);
            }

            items.push_back(babylon::ui::ListItem{ text, 0, (std::int64_t)action + 1 });
        }

        return items;
    }

    std::vector<babylon::ui::ListItem> BuildBindingItems(const JonesControlsScheme& scheme,
        JonesControlActions action)
    {
        std::vector<babylon::ui::ListItem> items;

        const std::size_t count = JonesControlSchemes_GetNumBindings(&scheme, action);
        for ( std::size_t index = 0; index < count; ++index )
        {
            std::size_t controlId = 0;
            if ( !JonesControlSchemes_GetBinding(&scheme, action, index, &controlId) )
            {
                continue;
            }

            items.push_back(babylon::ui::ListItem{ ControlName(controlId), 0,
                (std::int64_t)index + 1 });
        }

        return items;
    }

    babylon::ui::Choice BuildSchemeChoice(const JonesControlsConfig& config, std::size_t selected)
    {
        std::vector<std::string> options;
        options.reserve(config.numSchemes);
        for ( std::size_t cur = 0; cur < config.numSchemes; ++cur )
        {
            options.emplace_back(config.aSchemes[cur].aName);
        }

        if ( options.empty() )
        {
            // Ui::push rejects a Choice without options; a loaded list always has the defaults.
            options.emplace_back(std::string());
        }

        return babylon::ui::Choice{ kJonesControlsSchemeText, std::string(), std::move(options),
            true, std::min(selected, options.size() - 1) };
    }

    bool IsCustomScheme(const JonesControlsConfig& config, std::size_t selected)
    {
        return selected >= JonesControlSchemes_GetNumDefaultSchemes() && selected < config.numSchemes;
    }

    babylon::ui::Button EditButton(const JonesControlsConfig& config, std::size_t selected)
    {
        const bool bCustom = IsCustomScheme(config, selected);
        return babylon::ui::Button{
            bCustom ? ResolveLabel("JONES_STR_EDIT_SCHEME", "Edit")
                    : ResolveLabel("JONES_STR_VIEW_SCHEME", "View"),
            kJonesControlsActionEdit, selected < config.numSchemes };
    }

    babylon::ui::Screen BuildMainScreen(const JonesControlsConfig& config, std::size_t selected,
        bool bMouse)
    {
        babylon::ui::Screen screen;
        screen.title = ResolveLabel("JONES_STR_CTRL_OPTS", kJonesControlsFallbackTitle);

        const bool bCustom = IsCustomScheme(config, selected);

        screen.widgets.emplace_back(BuildSchemeChoice(config, selected));
        screen.widgets.emplace_back(babylon::ui::Button{
            kJonesControlsNewText, kJonesControlsActionNew, true });
        screen.widgets.emplace_back(babylon::ui::Button{
            kJonesControlsDeleteText, kJonesControlsActionDelete, bCustom });
        screen.widgets.emplace_back(babylon::ui::Toggle{
            kJonesControlsMouseText, std::string(), true, bMouse });
        screen.widgets.emplace_back(EditButton(config, selected));
        screen.widgets.emplace_back(babylon::ui::Button{
            kJonesControlsResetText, kJonesControlsActionReset, bCustom });
        screen.widgets.emplace_back(babylon::ui::Button{
            kJonesControlsOkText, kJonesDialogOk, true });
        screen.widgets.emplace_back(babylon::ui::Button{
            ResolveLabel("JONES_STR_CANCEL", "Cancel"), kJonesDialogCancel, true });

        screen.backResult   = kJonesDialogCancel;
        screen.initialFocus = kJonesMainChoice;
        return screen;
    }

    // Rewrites the main screen's scheme-dependent widgets after a scheme was added or removed. The
    // Choice is replaced through a Label first because setWidget keeps an unbound value of the same
    // kind; this makes it start from the new selection.
    void UpdateMainWidgets(babylon::ui::Ui& ui, babylon::ui::ScreenId screenId,
        const JonesControlsConfig& config, std::size_t selected)
    {
        const bool bCustom = IsCustomScheme(config, selected);

        static_cast<void>(ui.setWidget(screenId, kJonesMainChoice, babylon::ui::Label{}));
        static_cast<void>(ui.setWidget(screenId, kJonesMainChoice,
            BuildSchemeChoice(config, selected)));
        static_cast<void>(ui.setWidget(screenId, kJonesMainDelete, babylon::ui::Button{
            kJonesControlsDeleteText, kJonesControlsActionDelete, bCustom }));
        static_cast<void>(ui.setWidget(screenId, kJonesMainEdit, EditButton(config, selected)));
        static_cast<void>(ui.setWidget(screenId, kJonesMainReset, babylon::ui::Button{
            kJonesControlsResetText, kJonesControlsActionReset, bCustom }));
    }

    bool ReadToggle(const babylon::ui::Ui& ui, babylon::ui::ScreenId screenId,
        std::size_t index, bool& value)
    {
        const std::optional<babylon::ui::WidgetValue> pValue = ui.widgetValue(screenId, index);
        if ( !pValue || !std::holds_alternative<bool>(*pValue) )
        {
            return false;
        }

        value = std::get<bool>(*pValue);
        return true;
    }

    // The selection the main screen opens with: the caller's index, else the scheme named by
    // controls.configFile, else the keyboard-and-gamepad default (original line 4952-4983).
    std::size_t InitialSelection(const JonesControlsConfig& config)
    {
        if ( config.selectedShemeIdx >= 0
            && (std::size_t)config.selectedShemeIdx < config.numSchemes )
        {
            return (std::size_t)config.selectedShemeIdx;
        }

        char aConfigured[128] = { 0 };
        stdConfig_GetString(JONESCONFIG_CFG_CONTROLS_CONFIGFILE, aConfigured,
            STD_ARRAYLEN(aConfigured), "");

        const char* pWanted = aConfigured;
        if ( !pWanted[0] )
        {
            pWanted = JonesControlSchemes_GetDefaultSchemeName(JonesUiControls_DefaultSchemeIndex());
        }

        if ( pWanted )
        {
            for ( std::size_t cur = 0; cur < config.numSchemes; ++cur )
            {
                if ( std::strcmp(config.aSchemes[cur].aName, pWanted) == 0 )
                {
                    return cur;
                }
            }
        }

        return 0;
    }

    // Inserts a new custom scheme in name order among the custom schemes (original line 5598-5677).
    bool InsertScheme(JonesControlsConfig& config, const JonesControlsScheme& scheme,
        std::size_t& selected)
    {
        const std::size_t numDefaults = JonesControlSchemes_GetNumDefaultSchemes();
        std::size_t index = numDefaults;
        while ( index < config.numSchemes
            && std::strcmp(scheme.aName, config.aSchemes[index].aName) >= 0 )
        {
            ++index;
        }

        // The working copy is cloned with exactly numSchemes slots
        // (jonesConfig_CopyControlConfig), so it always needs room for one more; the original
        // grew in tens only because its loaded list was allocated that way.
        JonesControlsScheme* pNew = (JonesControlsScheme*)STDREALLOC(config.aSchemes,
            sizeof(JonesControlsScheme) * (config.numSchemes + 1));
        if ( !pNew )
        {
            return false;
        }

        config.aSchemes = pNew;
        memset(&config.aSchemes[config.numSchemes], 0, sizeof(JonesControlsScheme));

        for ( std::size_t cur = config.numSchemes; cur > index; --cur )
        {
            config.aSchemes[cur] = config.aSchemes[cur - 1];
        }

        config.aSchemes[index] = scheme;
        ++config.numSchemes;
        selected = index;
        return true;
    }

    // Removes a custom scheme from the working list. Its file is queued for deletion on OK unless it
    // was only created in this screen (bModified), as the original did (line 5426-5432).
    bool DeleteSchemeAt(JonesControlsConfig& config, std::size_t index,
        std::vector<std::string>& files)
    {
        if ( index < JonesControlSchemes_GetNumDefaultSchemes() || index >= config.numSchemes )
        {
            return false;
        }

        if ( config.aSchemes[index].bModified != 1 )
        {
            files.emplace_back(config.aSchemes[index].aName);
        }

        for ( std::size_t cur = index; cur + 1 < config.numSchemes; ++cur )
        {
            config.aSchemes[cur] = config.aSchemes[cur + 1];
        }

        memset(&config.aSchemes[config.numSchemes - 1], 0, sizeof(JonesControlsScheme));
        --config.numSchemes;
        return true;
    }

    std::string EditTitle(const JonesControlsScheme& scheme)
    {
        const std::string text = ResolveLabel("JONES_STR_SETTINGS", "Settings");
        if ( text.find('%') != std::string::npos )
        {
            return FormatLabel(text, scheme.aName);
        }

        return text + " - " + scheme.aName;
    }

    babylon::ui::Screen BuildEditScreen(const JonesControlsScheme& scheme, bool bEditable)
    {
        babylon::ui::Screen screen;
        screen.title = EditTitle(scheme);

        screen.widgets.emplace_back(babylon::ui::List{
            ResolveLabel("JONES_STR_FCTN", "Function"), BuildActionItems(scheme), 0,
            kJonesEditActionAccept, true });
        screen.widgets.emplace_back(babylon::ui::Button{
            kJonesControlsOkText, kJonesDialogOk, bEditable });
        screen.widgets.emplace_back(babylon::ui::Button{
            ResolveLabel("JONES_STR_CANCEL", "Cancel"), kJonesDialogCancel, true });

        screen.backResult   = kJonesDialogCancel;
        screen.initialFocus = kJonesEditList;
        return screen;
    }

    void RefreshEditList(babylon::ui::Ui& ui, babylon::ui::ScreenId screenId,
        const JonesControlsScheme& scheme)
    {
        static_cast<void>(ui.setWidget(screenId, kJonesEditList, babylon::ui::List{
            ResolveLabel("JONES_STR_FCTN", "Function"), BuildActionItems(scheme), 0,
            kJonesEditActionAccept, true }));
    }

    babylon::ui::Screen BuildAssignScreen(const JonesControlsScheme& scheme,
        JonesControlActions action, bool bEditable)
    {
        babylon::ui::Screen screen;
        screen.title = CapActionName(action);
        if ( screen.title.empty() )
        {
            screen.title = ActionName(action);
        }

        const std::vector<babylon::ui::ListItem> bindings = BuildBindingItems(scheme, action);

        screen.widgets.emplace_back(babylon::ui::BindingCapture{
            kJonesControlsAddText, bEditable });
        screen.widgets.emplace_back(babylon::ui::List{
            kJonesControlsRemoveText, bindings, 0, kJonesAssignActionRemove,
            bEditable && !bindings.empty() });
        screen.widgets.emplace_back(babylon::ui::Button{
            "Back", kJonesDialogCancel, true }); // TODO: [COMPAT] JONES_STR_BACK is "BACK SPACE"

        screen.backResult   = kJonesDialogCancel;
        screen.initialFocus = bEditable ? kJonesAssignCapture
            : (bindings.empty() ? kJonesAssignBack : kJonesAssignRemove);
        return screen;
    }

    void RefreshAssignList(babylon::ui::Ui& ui, babylon::ui::ScreenId screenId,
        const JonesControlsScheme& scheme, JonesControlActions action, bool bEditable)
    {
        const std::vector<babylon::ui::ListItem> bindings = BuildBindingItems(scheme, action);
        static_cast<void>(ui.setWidget(screenId, kJonesAssignRemove, babylon::ui::List{
            kJonesControlsRemoveText, bindings, 0, kJonesAssignActionRemove,
            bEditable && !bindings.empty() }));
    }

    void ShowExceed(JonesControlActions action)
    {
        ShowMessage(FormatLabel(ResolveLabel("JONES_STR_EXCEED", kJonesControlsExceedText),
            CapActionName(action)));
    }

    // The original Reassign Key prompt (dialog 120): the control, the action it is bound to and the
    // action it would move to. Yes returns true.
    babylon::ui::Screen BuildReassignScreen(JonesControlActions owner,
        JonesControlActions target, std::size_t controlId)
    {
        babylon::ui::Screen screen;
        screen.title = ResolveLabel("JONES_STR_CTRL_OPTS", kJonesControlsFallbackTitle);

        const std::string quoted = "\"" + ControlName(controlId) + "\"";
        screen.widgets.emplace_back(babylon::ui::Label{
            FormatLabel(ResolveLabel("JONES_STR_NEWMAP", kJonesControlsNewMapText), quoted),
            std::string() });
        screen.widgets.emplace_back(babylon::ui::Label{
            FormatLabel(ResolveLabel("JONES_STR_FROM", kJonesControlsFromText),
                CapActionName(owner)),
            std::string() });
        screen.widgets.emplace_back(babylon::ui::Label{
            FormatLabel(ResolveLabel("JONES_STR_TO", kJonesControlsToText),
                CapActionName(target)),
            std::string() });
        screen.widgets.emplace_back(babylon::ui::Button{
            ResolveLabel("JONES_STR_YES", "Yes"), kJonesDialogOk, true });
        screen.widgets.emplace_back(babylon::ui::Button{
            ResolveLabel("JONES_STR_NO", "No"), kJonesDialogCancel, true });

        screen.backResult   = kJonesDialogCancel;
        screen.initialFocus = 3;
        return screen;
    }

    bool AskReassign(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
        JonesControlActions owner, JonesControlActions target, std::size_t controlId)
    {
        return JonesUiModal_Run(ui, pPainter, BuildReassignScreen(owner, target, controlId),
            Identity) == kJonesDialogOk;
    }

    // The assignment screen opened by accepting an action row: the capture row adds a binding, the
    // list removes one of the current bindings, and Back returns to the Edit list.
    int RunAssignScreen(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
        JonesControlsScheme& scheme, JonesControlActions action, bool bEditable)
    {
        const babylon::ui::Screen screen = BuildAssignScreen(scheme, action, bEditable);

        const JonesUiModalEventHandler handler = [&](babylon::ui::Ui& uiRef,
            babylon::ui::ScreenId screenId, const babylon::ui::UiEvent& event) -> bool
        {
            if ( const babylon::ui::BindingCaptured* pCaptured =
                    std::get_if<babylon::ui::BindingCaptured>(&event) )
            {
                if ( pCaptured->screen != screenId || pCaptured->widget != kJonesAssignCapture )
                {
                    return false;
                }

                if ( !bEditable )
                {
                    return true;
                }

                std::size_t controlId = 0;
                if ( !JonesUiControls_MapBinding(pCaptured->binding, controlId) )
                {
                    return true;
                }

                std::size_t owner = 0;
                std::size_t offset = 0;
                if ( FindBinding(scheme, controlId, owner, offset) )
                {
                    if ( owner == (std::size_t)action )
                    {
                        // The original closed the assign dialog with the same-map message (line
                        // 6391-6423).
                        std::string text = "\"" + ControlName(controlId) + "\" ";
                        text += ResolveLabel("JONES_STR_SAMEMAP", kJonesControlsSameMapText);
                        text += " " + CapActionName(action) + ".";
                        ShowMessage(text);
                        static_cast<void>(uiRef.pop(kJonesDialogCancel));
                        return true;
                    }

                    if ( JONESCONTROL_ACTION_HASMAXBINDS(scheme.aActions[action]) )
                    {
                        ShowExceed(action);
                        return true;
                    }

                    if ( !AskReassign(uiRef, pPainter, (JonesControlActions)owner, action,
                            controlId) )
                    {
                        // No leaves the capture screen open so another control can be tried.
                        return true;
                    }

                    static_cast<void>(JonesControlSchemes_RemoveBinding(&scheme,
                        (JonesControlActions)owner, offset));
                }
                else if ( JONESCONTROL_ACTION_HASMAXBINDS(scheme.aActions[action]) )
                {
                    ShowExceed(action);
                    return true;
                }

                static_cast<void>(JonesControlSchemes_AddBinding(&scheme, action, controlId));
                RefreshAssignList(uiRef, screenId, scheme, action, bEditable);
                return true;
            }

            const babylon::ui::ActionPressed* pAction =
                std::get_if<babylon::ui::ActionPressed>(&event);
            if ( !pAction || pAction->screen != screenId
                || pAction->action != kJonesAssignActionRemove )
            {
                return false;
            }

            if ( bEditable && pAction->item > 0 )
            {
                static_cast<void>(JonesControlSchemes_RemoveBinding(&scheme, action,
                    (std::size_t)(pAction->item - 1)));
                RefreshAssignList(uiRef, screenId, scheme, action, bEditable);
            }

            return true;
        };

        return JonesUiModal_RunHandled(ui, pPainter, screen, Identity, handler);
    }

    // The original Edit OK check: every action outside the weapon range must keep a binding
    // (line 6064-6089).
    std::string MissingActions(const JonesControlsScheme& scheme)
    {
        std::string missing;
        for ( std::size_t cur = 0; cur < JONESCONTROL_ACTION_NUMACTIONS; ++cur )
        {
            const bool bWeapon = cur >= JONESCONTROL_ACTION_FISTS && cur <= JONESCONTROL_ACTION_BAZOOKA;
            if ( !bWeapon && JonesControlSchemes_GetNumBindings(&scheme,
                    (JonesControlActions)cur) == 0 )
            {
                missing += CapActionName((JonesControlActions)cur);
                missing += "\n";
            }
        }

        return missing;
    }

    // The Edit screen. scheme is a working copy the caller takes back only after OK.
    int RunEditScreen(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
        JonesControlsScheme& scheme, std::size_t schemeIndex)
    {
        const bool bEditable = schemeIndex >= JonesControlSchemes_GetNumDefaultSchemes();
        const babylon::ui::Screen screen = BuildEditScreen(scheme, bEditable);

        const JonesUiModalEventHandler handler = [&](babylon::ui::Ui& uiRef,
            babylon::ui::ScreenId screenId, const babylon::ui::UiEvent& event) -> bool
        {
            const babylon::ui::ActionPressed* pAction =
                std::get_if<babylon::ui::ActionPressed>(&event);
            if ( !pAction || pAction->screen != screenId )
            {
                return false;
            }

            if ( pAction->action == kJonesEditActionAccept )
            {
                const std::size_t row = pAction->item > 0 ? (std::size_t)(pAction->item - 1)
                    : kJonesEditList;
                if ( row < JONESCONTROL_ACTION_NUMACTIONS )
                {
                    static_cast<void>(RunAssignScreen(uiRef, pPainter, scheme,
                        (JonesControlActions)row, bEditable));
                    RefreshEditList(uiRef, screenId, scheme);
                }

                return true;
            }

            if ( pAction->action != kJonesDialogOk )
            {
                return false;
            }

            if ( !bEditable )
            {
                return true;
            }

            const std::string missing = MissingActions(scheme);
            if ( !missing.empty() )
            {
                std::string text = ResolveLabel("JONES_STR_NO_MAPNG", kJonesControlsNoMappingText);
                text += "\n";
                text += missing;
                ShowMessage(text);
                return true;
            }

            scheme.bModified = 1;
            static_cast<void>(uiRef.pop(kJonesDialogOk));
            return true;
        };

        return JonesUiModal_RunHandled(ui, pPainter, screen, Identity, handler);
    }

    // Added: clears a TextEntry through a Label because setWidget keeps an unbound value of the
    // same kind (the original cleared the field after a bad or duplicate name).
    void ClearNameEntry(babylon::ui::Ui& ui, babylon::ui::ScreenId screenId)
    {
        static_cast<void>(ui.setWidget(screenId, 0, babylon::ui::Label{}));
        static_cast<void>(ui.setWidget(screenId, 0, babylon::ui::TextEntry{
            kJonesControlsNameText, std::string(), std::string(), true }));
    }

    babylon::ui::Screen BuildNameScreen(void)
    {
        babylon::ui::Screen screen;
        screen.title = ResolveLabel("JONES_STR_CTRL_OPTS", kJonesControlsFallbackTitle);
        screen.widgets.emplace_back(babylon::ui::TextEntry{
            kJonesControlsNameText, std::string(), std::string(), true });
        screen.widgets.emplace_back(babylon::ui::Button{
            kJonesControlsOkText, kJonesDialogOk, true });
        screen.widgets.emplace_back(babylon::ui::Button{
            ResolveLabel("JONES_STR_CANCEL", "Cancel"), kJonesDialogCancel, true });
        screen.backResult   = kJonesDialogCancel;
        screen.initialFocus = 0;
        return screen;
    }

    // The original New Scheme dialog (line 5713-5808): a single-line name field, checked in place so
    // the dialog stays open on an error.
    bool RunNameScreen(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
        const JonesControlsConfig& config, std::string& name)
    {
        const babylon::ui::Screen screen = BuildNameScreen();

        bool bAccepted = false;

        const JonesUiModalEventHandler handler = [&](babylon::ui::Ui& uiRef,
            babylon::ui::ScreenId screenId, const babylon::ui::UiEvent& event) -> bool
        {
            std::string text;
            if ( const babylon::ui::TextSubmitted* pText =
                    std::get_if<babylon::ui::TextSubmitted>(&event) )
            {
                if ( pText->screen != screenId || pText->widget != 0 )
                {
                    return false;
                }

                text = pText->text;
            }
            else
            {
                const babylon::ui::ActionPressed* pAction =
                    std::get_if<babylon::ui::ActionPressed>(&event);
                if ( !pAction || pAction->screen != screenId
                    || pAction->action != kJonesDialogOk )
                {
                    return false;
                }

                const std::optional<babylon::ui::WidgetValue> pValue =
                    uiRef.widgetValue(screenId, 0);
                if ( pValue && std::holds_alternative<std::string>(*pValue) )
                {
                    text = std::get<std::string>(*pValue);
                }
            }

            // The original limit was Edit_LimitText(1052, 60).
            if ( text.size() > 60 )
            {
                text.resize(60);
            }

            switch ( JonesUiControls_ValidateSchemeName(text.c_str(), config) )
            {
                case JonesUiControlsNameStatus::Ok:
                    name = text;
                    bAccepted = true;
                    static_cast<void>(uiRef.pop(kJonesDialogOk));
                    return true;

                case JonesUiControlsNameStatus::Empty:
                    ShowMessage(ResolveLabel("JONES_STR_NO_SCHEME", kJonesControlsNoSchemeText));
                    return true;

                case JonesUiControlsNameStatus::Invalid:
                    ShowMessage(FormatLabel(
                        ResolveLabel("JONES_STR_INVALIDFILE", kJonesControlsInvalidFileText), text));
                    ClearNameEntry(uiRef, screenId);
                    return true;

                case JonesUiControlsNameStatus::Duplicate:
                    ShowMessage(ResolveLabel("JONES_STR_DUP_SCHEME", kJonesControlsDuplicateText));
                    ClearNameEntry(uiRef, screenId);
                    return true;
            }

            return true;
        };

        static_cast<void>(JonesUiModal_RunHandled(ui, pPainter, screen, Identity, handler));
        return bAccepted;
    }
}

JonesUiControlsNameStatus JonesUiControls_ValidateSchemeName(const char* pName,
    const JonesControlsConfig& config)
{
    if ( !pName || !pName[0] )
    {
        return JonesUiControlsNameStatus::Empty;
    }

    // The original strcspn() set, exactly (jonesConfig.c line 5761).
    if ( std::strcspn(pName, "/:\\\"?<>*|+") != std::strlen(pName) )
    {
        return JonesUiControlsNameStatus::Invalid;
    }

    for ( std::size_t cur = 0; cur < config.numSchemes; ++cur )
    {
        if ( std::strcmp(pName, config.aSchemes[cur].aName) == 0 )
        {
            return JonesUiControlsNameStatus::Duplicate;
        }
    }

    return JonesUiControlsNameStatus::Ok;
}

bool JonesUiControls_MapBinding(const babylon::ui::Binding& binding, std::size_t& pControlId)
{
    using babylon::ui::BindingKind;
    using babylon::ui::Binding;

    std::size_t controlId = (std::size_t)-1;

    switch ( binding.kind )
    {
        case BindingKind::Key:
        {
            const int engineKeyId = stdControlDevicesBabylon_EngineIdFromKey(binding.key);
            if ( engineKeyId < 0 )
            {
                return false;
            }

            controlId = (std::size_t)engineKeyId;
            break;
        }

        case BindingKind::MouseButton:
        {
            // The engine ids use the adapter's mouse order: left, right, middle, X1.
            switch ( binding.mouseButton )
            {
                case babylon::platform::MouseButton::Left:
                    controlId = STDCONTROL_KID_MOUSE_LBUTTON;
                    break;
                case babylon::platform::MouseButton::Right:
                    controlId = STDCONTROL_KID_MOUSE_RBUTTON;
                    break;
                case babylon::platform::MouseButton::Middle:
                    controlId = STDCONTROL_KID_MOUSE_MBUTTON;
                    break;
                case babylon::platform::MouseButton::X1:
                    controlId = STDCONTROL_KID_MOUSE_XBUTTON;
                    break;
                default:
                    return false; // X2 has no engine id.
            }
            break;
        }

        case BindingKind::GamepadButton:
        {
            // The d-pad drives POV 0; the rest use the adapter's button indices.
            switch ( binding.button )
            {
                case babylon::platform::GamepadButton::DpadLeft:
                    controlId = STDCONTROL_JOYSTICK_GETPOVLEFT(0, 0);
                    break;
                case babylon::platform::GamepadButton::DpadRight:
                    controlId = STDCONTROL_JOYSTICK_GETPOVRIGHT(0, 0);
                    break;
                case babylon::platform::GamepadButton::DpadUp:
                    controlId = STDCONTROL_JOYSTICK_GETPOVUP(0, 0);
                    break;
                case babylon::platform::GamepadButton::DpadDown:
                    controlId = STDCONTROL_JOYSTICK_GETPOVDOWN(0, 0);
                    break;

                case babylon::platform::GamepadButton::South:
                    controlId = STDCONTROL_JOYSTICK_GETBUTTON(0, 0);
                    break;
                case babylon::platform::GamepadButton::East:
                    controlId = STDCONTROL_JOYSTICK_GETBUTTON(0, 1);
                    break;
                case babylon::platform::GamepadButton::West:
                    controlId = STDCONTROL_JOYSTICK_GETBUTTON(0, 2);
                    break;
                case babylon::platform::GamepadButton::North:
                    controlId = STDCONTROL_JOYSTICK_GETBUTTON(0, 3);
                    break;
                case babylon::platform::GamepadButton::LeftShoulder:
                    controlId = STDCONTROL_JOYSTICK_GETBUTTON(0, 4);
                    break;
                case babylon::platform::GamepadButton::RightShoulder:
                    controlId = STDCONTROL_JOYSTICK_GETBUTTON(0, 5);
                    break;
                case babylon::platform::GamepadButton::Back:
                    controlId = STDCONTROL_JOYSTICK_GETBUTTON(0, 6);
                    break;
                case babylon::platform::GamepadButton::Start:
                    controlId = STDCONTROL_JOYSTICK_GETBUTTON(0, 7);
                    break;
                case babylon::platform::GamepadButton::LeftStick:
                    controlId = STDCONTROL_JOYSTICK_GETBUTTON(0, 8);
                    break;
                case babylon::platform::GamepadButton::RightStick:
                    controlId = STDCONTROL_JOYSTICK_GETBUTTON(0, 9);
                    break;
                case babylon::platform::GamepadButton::Guide:
                    controlId = STDCONTROL_JOYSTICK_GETBUTTON(0, 10);
                    break;
                default:
                    return false;
            }
            break;
        }

        case BindingKind::GamepadAxis:
        {
            if ( binding.axisDirection == 0.0f )
            {
                return false;
            }

            std::size_t axis = 0;
            switch ( binding.axis )
            {
                case babylon::platform::GamepadAxis::LeftX:        axis = STDCONTROL_JOYSTICK_AXIS_X;  break;
                case babylon::platform::GamepadAxis::LeftY:        axis = STDCONTROL_JOYSTICK_AXIS_Y;  break;
                case babylon::platform::GamepadAxis::LeftTrigger:  axis = STDCONTROL_JOYSTICK_AXIS_Z;  break;
                case babylon::platform::GamepadAxis::RightX:       axis = STDCONTROL_JOYSTICK_AXIS_RX; break;
                case babylon::platform::GamepadAxis::RightY:       axis = STDCONTROL_JOYSTICK_AXIS_RY; break;
                case babylon::platform::GamepadAxis::RightTrigger: axis = STDCONTROL_JOYSTICK_AXIS_RZ; break;
                default:
                    return false;
            }

            const std::size_t engineAxis = STDCONTROL_GET_JOYSTICK_AXIS(0, axis)
                | (binding.axisDirection > 0.0f ? STDCONTROL_AID_POSITIVE_AXIS
                                                : STDCONTROL_AID_NEGATIVE_AXIS);
            controlId = JonesControlSchemes_MapAxisControl(engineAxis);
            if ( controlId == (std::size_t)-1 )
            {
                return false;
            }
            break;
        }

        default:
            return false;
    }

    // A control the scheme format cannot name would be written as an empty quoted string.
    char aName[JONESCONFIG_CONTROLSTRING_MAXLEN] = { 0 };
    jonesConfig_ControlToString(controlId, aName);
    if ( !aName[0] )
    {
        return false;
    }

    pControlId = controlId;
    return true;
}

int JonesUiControls_RunEditScreen(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    JonesControlsScheme& scheme, std::size_t schemeIndex)
{
    return RunEditScreen(ui, pPainter, scheme, schemeIndex);
}

int JonesUiControls_RunControlOptionsScreen(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    JonesControlsConfig& config)
{
    JonesControlsConfig working;
    memset(&working, 0, sizeof(working));
    working.selectedShemeIdx = -1;

    if ( !jonesConfig_CopyControlConfig(&config, &working) )
    {
        jonesConfig_FreeControlConfigEntry(&working);
        return kJonesDialogCancel;
    }

    // The original main init cleared every loaded scheme's modified flag (line 4992).
    for ( std::size_t cur = 0; cur < working.numSchemes; ++cur )
    {
        working.aSchemes[cur].bModified = 0;
    }

    std::size_t selected = InitialSelection(working);
    working.selectedShemeIdx = (int)selected;

    bool bMouse = stdConfig_GetBool(JONESCONTROL_CFG_CONTROLS_MOUSE, false);

    const babylon::ui::Screen screen = BuildMainScreen(working, selected, bMouse);
    std::vector<std::string> deleteFiles;
    bool bApply = false;

    const JonesUiModalEventHandler handler = [&](babylon::ui::Ui& uiRef,
        babylon::ui::ScreenId screenId, const babylon::ui::UiEvent& event) -> bool
    {
        const babylon::ui::ActionPressed* pAction =
            std::get_if<babylon::ui::ActionPressed>(&event);
        if ( !pAction || pAction->screen != screenId )
        {
            return false;
        }

        switch ( pAction->action )
        {
            case kJonesControlsActionNew:
            {
                std::string name;
                if ( !RunNameScreen(uiRef, pPainter, working, name) )
                {
                    return true;
                }

                JonesControlsScheme scheme;
                memset(&scheme, 0, sizeof(scheme));
                if ( !JonesControlSchemes_CreateScheme(name.c_str(), &scheme) )
                {
                    ShowMessage(ResolveLabel("JONES_STR_NO_MEM_SCHEME", kJonesControlsNoMemText));
                    return true;
                }

                // A new scheme is custom, so its index (one past the list) enables editing.
                if ( RunEditScreen(uiRef, pPainter, scheme, working.numSchemes) != kJonesDialogOk )
                {
                    return true;
                }

                std::size_t inserted = 0;
                if ( InsertScheme(working, scheme, inserted) )
                {
                    selected = inserted;
                    working.selectedShemeIdx = (int)selected;
                    UpdateMainWidgets(uiRef, screenId, working, selected);
                }
                else
                {
                    ShowMessage(ResolveLabel("JONES_STR_NO_MEM_SCHEME", kJonesControlsNoMemText));
                }
                return true;
            }

            case kJonesControlsActionDelete:
            {
                if ( DeleteSchemeAt(working, selected, deleteFiles) )
                {
                    if ( selected >= working.numSchemes )
                    {
                        selected = working.numSchemes ? working.numSchemes - 1 : 0;
                    }
                    working.selectedShemeIdx = (int)selected;
                    UpdateMainWidgets(uiRef, screenId, working, selected);
                }
                return true;
            }

            case kJonesControlsActionEdit:
            {
                if ( selected >= working.numSchemes )
                {
                    return true;
                }

                JonesControlsScheme scheme = working.aSchemes[selected];
                if ( RunEditScreen(uiRef, pPainter, scheme, selected) == kJonesDialogOk )
                {
                    scheme.bModified = 1;
                    working.aSchemes[selected] = scheme;
                }
                return true;
            }

            case kJonesControlsActionReset:
            {
                if ( IsCustomScheme(working, selected) )
                {
                    JonesControlSchemes_ResetToDefault(&working.aSchemes[selected],
                        JonesUiControls_DefaultSchemeIndex());
                    working.aSchemes[selected].bModified = 1;
                }
                return true;
            }

            case kJonesDialogOk:
            {
                // Read the unbound toggles while the screen is still open; the modal loop then pops
                // it with this action's value.
                static_cast<void>(ReadToggle(uiRef, screenId, kJonesMainMouse, bMouse));
                bApply = true;
                return false;
            }

            default:
                return false;
        }
    };

    const int result = JonesUiModal_RunHandled(ui, pPainter, screen, Identity, handler);
    if ( result != kJonesDialogOk || !bApply )
    {
        jonesConfig_FreeControlConfigEntry(&working);
        return kJonesDialogCancel;
    }

    // The main dialog's OK (line 5091-5161).
    for ( const std::string& name : deleteFiles )
    {
        static_cast<void>(JonesControlSchemes_DeleteScheme(name.c_str()));
    }

    const std::size_t numDefaults = JonesControlSchemes_GetNumDefaultSchemes();
    for ( std::size_t cur = numDefaults; cur < working.numSchemes; ++cur )
    {
        if ( working.aSchemes[cur].bModified == 1
            && !jonesConfig_ControlOptions_WriteScheme(&working.aSchemes[cur]) )
        {
            ShowMessage(ResolveLabel("JONES_STR_NO_MEM_SCHEME", kJonesControlsNoMemText));
        }
    }

    if ( selected >= working.numSchemes )
    {
        selected = 0;
    }

    static_cast<void>(stdConfig_SetBool(JONESCONTROL_CFG_CONTROLS_MOUSE, bMouse));

    JonesControl_EnableJoystickAxes();

    static_cast<void>(JonesControlSchemes_SetActiveScheme(&working.aSchemes[selected]));

    // Adopt the applied list; the caller's old arrays are released and the working copy moved.
    jonesConfig_FreeControlConfigEntry(&config);
    config = working;
    memset(&working, 0, sizeof(working));
    return kJonesDialogOk;
}

extern "C" int J3DAPI jonesConfig_ShowControlOptions(tSysWindow hWnd)
{
    (void)hWnd;

    StdBabylonContext* pContext = stdBabylon_GetContext();
    if ( !pContext || !pContext->pWindow || !pContext->pFileSystem )
    {
        // Headless: the Null stub answers cancel without blocking.
        return kJonesDialogCancel;
    }

    JonesControlsConfig config;
    memset(&config, 0, sizeof(config));
    config.selectedShemeIdx = -1;
    if ( !JonesControlSchemes_LoadAllSchemes(&config) )
    {
        return kJonesDialogCancel;
    }

    JonesUiContext* pUiContext = JonesUiContext_Acquire();
    if ( !pUiContext )
    {
        jonesConfig_FreeControlConfigEntry(&config);
        return kJonesDialogCancel;
    }

    const int result = JonesUiControls_RunControlOptionsScreen(*pUiContext->pUi,
        &pUiContext->painter, config);
    JonesUiContext_Release(pUiContext);
    jonesConfig_FreeControlConfigEntry(&config);
    return result;
}

#ifdef J3D_TEST
void JonesUiControls_AppendScreensForTest(std::vector<babylon::ui::Screen>& screens)
{
    JonesControlsConfig config{};
    JonesControlsScheme scheme{};
    const JonesControlActions action = (JonesControlActions)0;

    screens.push_back(BuildMainScreen(config, 0, false));
    screens.push_back(BuildEditScreen(scheme, true));
    screens.push_back(BuildAssignScreen(scheme, action, true));
    screens.push_back(BuildNameScreen());
    screens.push_back(BuildReassignScreen(action, action, 0));
}
#endif
