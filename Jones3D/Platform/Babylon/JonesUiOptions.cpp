// Added: the Babylon column's one tabbed Options screen (ADR-0028); see JonesUiOptions.h.
//
// The screen is split into four tabs. Each tab reuses the standalone screen's builder, reader and
// apply function, so a setting keeps its cvars, config keys and save behavior. A tab switch reads
// the leaving tab's row values into its model and closes that screen with a private result; the
// outer loop builds the next tab from the retained model. Q/E, LB/RB and a click on the tab strip
// all report TabChanged from babylon::ui.
#include <Jones3D/Platform/Babylon/JonesUiOptions.h>

#include <Jones3D/Display/JonesDisplay.h>
#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Main/JonesMain.h>
#include <Jones3D/Platform/Babylon/JonesBabylon.h>
#include <Jones3D/Platform/Babylon/JonesUiBrightnessCalibration.h>
#include <Jones3D/Platform/Babylon/JonesUiContext.h>
#include <Jones3D/Platform/Babylon/JonesUiControls.h>
#include <Jones3D/Platform/Babylon/JonesUiDisplaySettings.h>
#include <Jones3D/Platform/Babylon/JonesUiGameplaySettings.h>
#include <Jones3D/Platform/Babylon/JonesUiSkin.h>
#include <Jones3D/Platform/Babylon/JonesUiSoundSettings.h>

#include <sound/Sound.h>

#include <std/Babylon/stdBabylon.h>
#include <std/General/stdConfig.h>

#include <babylon/ui/Screen.h>
#include <babylon/ui/Skin.h>
#include <babylon/ui/Types.h>
#include <babylon/ui/Ui.h>
#include <babylon/ui/UiEvent.h>
#include <babylon/ui/Widget.h>
#include <babylon/uirender/RenderPainter.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace
{
    constexpr int kJonesOptionsOk     = 1;
    constexpr int kJonesOptionsCancel = 2;
    constexpr babylon::ui::ActionId kJonesOptionsRunSetup = 3;

    // A tab switch closes the current tab's screen with one of these results; the outer loop
    // builds that tab next. They stay clear of the dialog results 1 and 2.
    constexpr babylon::ui::ResultCode kJonesOptionsTabResultBase = 100;
    constexpr std::size_t kJonesOptionsNumTabs = 5;

    constexpr std::size_t kJonesOptionsGameplayTab = 0;
    constexpr std::size_t kJonesOptionsControlsTab = 1;
    constexpr std::size_t kJonesOptionsDisplayTab  = 2;
    constexpr std::size_t kJonesOptionsSoundTab    = 3;
    constexpr std::size_t kJonesOptionsGameDataTab = 4;

    // Added: the tab titles and the screen title. The original game has no key for plain tab
    // names, so the English text is used until one exists.
    constexpr const char* kJonesOptionsTitleText =
        "Options"; // TODO: [COMPAT] JONES_STR_SETTINGS is a format string
    constexpr const char* kJonesOptionsGameplayText = "Gameplay"; // TODO: [COMPAT]
    constexpr const char* kJonesOptionsControlsText = "Controls"; // TODO: [COMPAT]
    constexpr const char* kJonesOptionsDisplayText  = "Display";  // TODO: [COMPAT]
    constexpr const char* kJonesOptionsSoundText    = "Sound";    // TODO: [COMPAT]
    constexpr const char* kJonesOptionsGameDataText = "Game data"; // TODO: [COMPAT]

    const char* const kJonesOptionsTabNames[kJonesOptionsNumTabs] = {
        kJonesOptionsGameplayText,
        kJonesOptionsControlsText,
        kJonesOptionsDisplayText,
        kJonesOptionsSoundText,
        kJonesOptionsGameDataText,
    };

    void SetTabs(babylon::ui::Screen& screen, std::size_t active)
    {
        screen.title = kJonesOptionsTitleText;
        screen.tabs.assign(kJonesOptionsTabNames, kJonesOptionsTabNames + kJonesOptionsNumTabs);
        screen.activeTab = std::min(active, kJonesOptionsNumTabs - 1);
    }

    // Added: the row that accepts the whole Options screen. It stays for mouse users, who cannot
    // click the prompt bar's Back.
    constexpr const char* kJonesOptionsApplyText = "Apply"; // TODO: [COMPAT]

    // Altered: leaving a tab keeps its changes, as Back does in a modern game (ADR-0028). The
    // standalone builder's accept row becomes Apply and its Cancel row goes; Back closes the tab
    // with the OK result, so the caller applies the same way it does for the button.
    void KeepChangesOnBack(babylon::ui::Screen& screen, std::size_t ok, std::size_t back)
    {
        if ( ok < screen.widgets.size() )
        {
            if ( babylon::ui::Button* pButton =
                    std::get_if<babylon::ui::Button>(&screen.widgets[ok]) )
            {
                pButton->text = kJonesOptionsApplyText;
            }
        }

        if ( back < screen.widgets.size() )
        {
            screen.widgets.erase(screen.widgets.begin() + (std::ptrdiff_t)back);
        }

        screen.backResult = kJonesOptionsOk;
    }

    int Identity(babylon::ui::ResultCode code)
    {
        return (int)code;
    }

    // The width the tabbed screen gives a row's text, matching Ui's Top-panel layout: the help
    // column and the focus marker are removed, not only the canvas margin and panel padding.
    float OptionsTextWidth(const babylon::ui::Ui& ui, const babylon::ui::Painter* pPainter)
    {
        const babylon::ui::Skin& skin = ui.skin();
        const float canvasWidth = ui.canvasSize().width;
        const float available = std::max(0.0f, canvasWidth - 2.0f * skin.canvasMargin);
        const float panelWidth = skin.fixedPanelWidth > 0.0f
            ? std::min(canvasWidth * skin.fixedPanelWidth, available) : available;
        const float innerWidth = std::max(0.0f, panelWidth - 2.0f * skin.panelPadding);

        float rowsWidth = innerWidth;
        const float helpRatio = std::min(skin.helpColumnWidth, 0.9f);
        if ( helpRatio > 0.0f )
        {
            const float helpWidth = std::max(0.0f, innerWidth * helpRatio - 4.0f);
            rowsWidth = std::max(0.0f, innerWidth - helpWidth - 8.0f);
        }

        float markerInset = 0.0f;
        if ( pPainter && !skin.focusMarker.empty() )
        {
            markerInset = pPainter->measureText(skin.textFont, skin.focusMarker).width
                + skin.markerSpacing;
        }

        const float width = rowsWidth - markerInset;
        return width > 1.0f ? width : 1.0f;
    }

    std::size_t Utf8CodePointLength(unsigned char lead)
    {
        if ( (lead >> 5) == 0x6 )
        {
            return 2;
        }
        if ( (lead >> 4) == 0xE )
        {
            return 3;
        }
        if ( (lead >> 3) == 0x1E )
        {
            return 4;
        }
        return 1;
    }

    // Elides the middle of a long path, keeping its start and its last component. The pieces are
    // measured so the result fits maxWidth; without a painter or a width the text is unchanged.
    std::string ElideMiddle(babylon::ui::Painter* pPainter, babylon::ui::FontId font,
        const std::string& text, float maxWidth)
    {
        if ( !pPainter || text.empty() || maxWidth <= 0.0f
            || pPainter->measureText(font, text).width <= maxWidth )
        {
            return text;
        }

        const std::string ellipsis = "...";
        const float ellipsisWidth = pPainter->measureText(font, ellipsis).width;
        const std::size_t separator = text.find_last_of("/\\");
        const std::string suffix = separator == std::string::npos
            ? text : text.substr(separator + 1);

        if ( !suffix.empty()
            && pPainter->measureText(font, ellipsis + suffix).width <= maxWidth )
        {
            const float prefixBudget = maxWidth - ellipsisWidth
                - pPainter->measureText(font, suffix).width;
            std::string prefix;
            std::size_t cur = 0;
            while ( cur < text.size()
                && (separator == std::string::npos || cur < separator) )
            {
                const std::size_t length = Utf8CodePointLength(
                    static_cast<unsigned char>(text[cur]));
                const std::size_t next = std::min(text.size(), cur + length);
                const std::string candidate = text.substr(0, next);
                if ( pPainter->measureText(font, candidate).width > prefixBudget )
                {
                    break;
                }

                prefix = candidate;
                cur = next;
            }

            return prefix + ellipsis + suffix;
        }

        std::vector<std::size_t> starts;
        for ( std::size_t cur = 0; cur < text.size(); )
        {
            starts.push_back(cur);
            cur += Utf8CodePointLength(static_cast<unsigned char>(text[cur]));
        }

        for ( std::size_t index = 1; index <= starts.size(); ++index )
        {
            const std::string candidate = ellipsis + text.substr(starts[index - 1]);
            if ( pPainter->measureText(font, candidate).width <= maxWidth )
            {
                return candidate;
            }
        }

        return ellipsis;
    }

    babylon::ui::Screen BuildGameplayTab(bool bEnhancedProfile,
        const JonesUiGameplayValues& values, JonesUiGameplayLayout& layout)
    {
        babylon::ui::Screen screen = JonesUiGameplay_BuildScreen(bEnhancedProfile, values,
            layout);
        SetTabs(screen, kJonesOptionsGameplayTab);
        KeepChangesOnBack(screen, layout.ok, layout.back);
        layout.back = babylon::ui::kNoWidget;

        screen.help.assign(screen.widgets.size(), std::string());
        screen.help[layout.showText] = "Show spoken dialogue as text on screen.";
        screen.help[layout.showHints] = "Show a hint when the game has one for your progress.";
        screen.help[layout.rotateMap] = "Rotate the overhead map to follow the player's facing.";
        screen.help[layout.difficulty] = "Choose how hard combat and puzzles are.";
        screen.help[layout.defaultRun] = "Start running by default when the player moves.";
        screen.help[layout.reticle] = "Draw an aim reticle for the equipped weapon.";
        screen.help[layout.blood] = "Show blood splatter effects when enemies are hit.";
        screen.help[layout.interactionPrompts] =
            "Show a small Use or Pick up hint when the player can interact.";
        screen.help[layout.quickWhip] =
            "Swing the whip at a target without drawing it first, then switch back.";
        screen.help[layout.profile] =
            "The game profile is fixed by this build and cannot be changed here.";
        screen.help[layout.fireFlash] = "Show the flash at the weapon muzzle when firing.";
        screen.help[layout.externalMaterials] =
            "Load material data from external CND files.";
        screen.help[layout.externalKeyframes] =
            "Load animation keyframes from external CND files.";
        screen.help[layout.ok] = "Apply every change on all tabs and close Options.";
        return screen;
    }

    babylon::ui::Screen BuildControlsTab(const JonesUiControlsModel& model,
        JonesUiControlsLayout& layout, bool bEnhancedProfile)
    {
        babylon::ui::Screen screen = JonesUiControls_BuildOptionsTabScreen(model, layout);
        layout.ok = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Button{
            kJonesOptionsApplyText, kJonesOptionsOk, true });
        layout.back = babylon::ui::kNoWidget;

        screen.backResult = kJonesOptionsOk;
        SetTabs(screen, kJonesOptionsControlsTab);

        // Added: the per-device styles are Enhanced options. The row stays visible in Retail but
        // cannot be changed, like the Profile and enhancement rows.
        if ( !bEnhancedProfile )
        {
            if ( layout.keyboardStyle < screen.widgets.size() )
            {
                if ( babylon::ui::Choice* pChoice =
                        std::get_if<babylon::ui::Choice>(&screen.widgets[layout.keyboardStyle]) )
                {
                    pChoice->enabled = false;
                }
            }
            if ( layout.gamepadStyle < screen.widgets.size() )
            {
                if ( babylon::ui::Choice* pChoice =
                        std::get_if<babylon::ui::Choice>(&screen.widgets[layout.gamepadStyle]) )
                {
                    pChoice->enabled = false;
                }
            }
        }

        screen.help.assign(screen.widgets.size(), std::string());
        screen.help[layout.scheme] =
            "Choose the control scheme used for the keyboard and gamepad bindings.";
        screen.help[layout.newScheme] = "Create a new custom control scheme.";
        screen.help[layout.deleteScheme] = "Delete the selected custom control scheme.";
        screen.help[layout.controller] = "Change the gamepad button bindings.";
        screen.help[layout.keyboard] = "Change the keyboard and mouse bindings.";
        screen.help[layout.mouse] = "Turn the camera with the mouse.";
        if ( layout.keyboardStyle < screen.help.size() )
        {
            screen.help[layout.keyboardStyle] =
                "How Indy moves with the keyboard: Classic keeps the camera following Indy; "
                "Modern lets you turn the camera with the mouse.";
        }
        if ( layout.gamepadStyle < screen.help.size() )
        {
            screen.help[layout.gamepadStyle] =
                "How Indy moves with a gamepad: Classic keeps the camera following Indy; "
                "Modern gives you a free camera on the right stick.";
        }
        screen.help[layout.rumble] = "Turn gamepad vibration on or off.";
        screen.help[layout.ok] = "Apply every change on all tabs and close Options.";
        return screen;
    }

    babylon::ui::Screen BuildDisplayTab(const JonesUiDisplayModel& model,
        const JonesUiDisplayValues& values, JonesUiDisplayLayout& layout)
    {
        // ADR-0028: the Display tab has no Performance row. The applied value is still read and
        // written unchanged, so existing configs and savegames keep their level.
        babylon::ui::Screen screen = JonesUiDisplay_BuildScreen(model, values, layout, false);
        SetTabs(screen, kJonesOptionsDisplayTab);
        KeepChangesOnBack(screen, layout.ok, layout.back);
        layout.back = babylon::ui::kNoWidget;

        screen.help.assign(screen.widgets.size(), std::string());
        if ( layout.bitDepth )
        {
            screen.help[*layout.bitDepth] = "Choose the color depth of the display mode.";
        }
        if ( layout.resolution )
        {
            screen.help[*layout.resolution] = "Choose the screen resolution.";
        }
        screen.help[layout.windowed] = "Run the game in a window instead of fullscreen.";
        screen.help[layout.filter] = "Choose how textures are filtered when they are scaled.";
        screen.help[layout.fog] = "Enable distance fog in the world.";
        screen.help[layout.fogDensity] = "Set how thick the distance fog is.";
        screen.help[layout.hiPoly] =
            "Use the high-polygon models; the game must restart to apply the change.";
        screen.help[layout.litFaces] =
            "Light world faces when the display reopens.";
        screen.help[layout.gameplayFOV] =
            "Set how wide the view is. It changes only the picture, not the game.";
        screen.help[layout.renderScale] =
            "Render the 3D world above (sharper) or below its resolution; menus stay native "
            "and the display size is unchanged. Applies when the display reopens.";
        screen.help[layout.antiAliasing] =
            "Smooth the 3D scene's edges with SMAA; the HUD, menus and prompts are unchanged. "
            "Applies when the display reopens.";
        screen.help[layout.bloom] =
            "Add a soft glow around bright parts of the 3D scene; the HUD, menus and prompts are "
            "unchanged. Applies when the display reopens.";
        screen.help[layout.dofCutscenes] =
            "Soften the picture away from the focus in cutscenes; the HUD, menus and prompts are "
            "unchanged. Applies when the display reopens.";
        screen.help[layout.dofGameplay] =
            "Soften only the far background during play, leaving Indy and the foreground sharp; "
            "the HUD, menus and prompts are unchanged. Applies when the display reopens.";
        screen.help[layout.sharpening] =
            "Restore detail softened by a low render scale or by anti-aliasing; the HUD, menus "
            "and prompts are unchanged. Applies when the display reopens.";
        screen.help[layout.toneMapping] =
            "Choose how bright scene values are mapped into the display: Clamp, Neutral or "
            "Filmic. Applies when the display reopens.";
        screen.help[layout.exposure] =
            "Scale brightness before tone mapping; higher values brighten, lower ones darken. "
            "Applies when the display reopens.";
        screen.help[layout.colorGrading] =
            "Choose a subtle colour look for the 3D scene after tone mapping; Neutral leaves the "
            "picture unchanged. Applies when the display reopens.";
        screen.help[layout.anisotropic] =
            "Sharpen world and model textures seen at a slant. Higher levels cost more and apply "
            "when the display reopens.";
        screen.help[layout.crispPixels] =
            "Use nearest filtering for world and model textures, for the original sharp, blocky "
            "look. Applies when the display reopens.";
        screen.help[layout.brightness] =
            "Calibrate the scene brightness on a dark test pattern; the value applies at once.";
        screen.help[layout.ok] = "Apply every change on all tabs and close Options.";
        return screen;
    }

    babylon::ui::Screen BuildSoundTab(const JonesUiSoundModel& model,
        const JonesUiSoundValues& values, JonesUiSoundLayout& layout)
    {
        babylon::ui::Screen screen = JonesUiSound_BuildScreen(model, values, layout);
        SetTabs(screen, kJonesOptionsSoundTab);
        KeepChangesOnBack(screen, layout.ok, layout.back);
        layout.back = babylon::ui::kNoWidget;

        screen.help.assign(screen.widgets.size(), std::string());
        screen.help[layout.volume] = "Set the master sound volume.";
        screen.help[layout.reverse] = "Swap the left and right audio channels.";
        screen.help[layout.outputDevice] = "Choose the audio output device.";
        screen.help[layout.ok] = "Apply every change on all tabs and close Options.";
        return screen;
    }

    babylon::ui::Screen BuildGameDataTab(const JonesUiOptionsData& data,
        babylon::ui::Painter* pPainter, babylon::ui::FontId font, float rowTextWidth)
    {
        babylon::ui::Screen screen;

        const char* pRelease = data.pRelease && data.pRelease[0] ? data.pRelease : "Unknown";
        const char* pFolder = data.pInstallPath && data.pInstallPath[0]
            ? data.pInstallPath : "Unknown";

        // A Label draws its value right-aligned in the same row as the label, so the path may only
        // use the space left after "Folder" and the column gap.
        float folderWidth = rowTextWidth;
        if ( pPainter )
        {
            const float labelWidth = pPainter->measureText(font, "Folder").width;
            folderWidth = std::max(1.0f, rowTextWidth - labelWidth - 8.0f);
        }

        screen.widgets.emplace_back(babylon::ui::Label{ "Release", pRelease });
        screen.widgets.emplace_back(babylon::ui::Label{ "Folder",
            ElideMiddle(pPainter, font, pFolder, folderWidth) });

        const std::size_t runSetup = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Button{ "Run setup again...",
            kJonesOptionsRunSetup, !data.bLevelRunning });

        const std::size_t ok = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Button{ kJonesOptionsApplyText,
            kJonesOptionsOk, true });

        screen.backResult = kJonesOptionsOk;
        SetTabs(screen, kJonesOptionsGameDataTab);

        screen.help.assign(screen.widgets.size(), std::string());
        screen.help[runSetup] = data.bLevelRunning
            ? "Available from the main menu. Your settings and saves are kept."
            : "Choose a new copy of the game. Your settings and saves are kept.";
        screen.help[ok] = "Apply every change on all tabs and close Options.";
        return screen;
    }

    bool ConfirmRunSetup(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
        const JonesUiModalBackground& background)
    {
        babylon::ui::Screen screen;
        screen.title = "Run setup again?";
        screen.widgets.emplace_back(babylon::ui::Label{
            "Jones3D EX closes the game and opens the setup.", std::string() });
        screen.widgets.emplace_back(babylon::ui::Label{
            "Your settings and saves are kept.", std::string() });
        screen.widgets.emplace_back(babylon::ui::Button{ "Open setup", 1, true });
        screen.widgets.emplace_back(babylon::ui::Button{ "Cancel", 2, true });
        screen.backResult = 2;
        screen.initialFocus = 0;
        return JonesUiModal_Run(ui, pPainter, screen, Identity, background) == 1;
    }
}

int JonesUiOptions_RunOptionsScreen(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    const JonesUiOptionsData& data, const JonesUiModalBackground& background)
{
    JonesUiGameplayValues gameplayValues = JonesUiGameplay_LoadValues(data.bEnhancedProfile);
    JonesUiGameplayLayout gameplayLayout;

    // Without a loaded scheme list the other tabs still work; the Controls tab shows its empty
    // list and a scheme is not applied.
    JonesControlsConfig controlsFallback{};
    controlsFallback.selectedShemeIdx = -1;
    JonesControlsConfig* pControls = data.pControlsConfig ? data.pControlsConfig
        : &controlsFallback;
    JonesUiControlsModel controlsModel;
    if ( !JonesUiControls_InitModel(controlsModel, *pControls) )
    {
        return kJonesOptionsCancel;
    }
    JonesUiControlsLayout controlsLayout;

    // The standalone references keep their own fallbacks when a tab's data is missing, so the
    // runner also works from a unit test without a display or sound driver.
    JonesDisplaySettings displayFallback{};
    JonesDisplaySettings* pDisplay = data.pDisplaySettings ? data.pDisplaySettings
        : &displayFallback;
    JonesUiDisplayValues displayValues;
    const JonesUiDisplayModel displayModel = JonesUiDisplay_LoadModel(data.pDisplayEnv,
        pDisplay, displayValues);
    JonesUiDisplayLayout displayLayout;

    JonesSoundSettings soundFallback{};
    JonesSoundSettings* pSound = data.pSoundSettings;
    if ( !pSound )
    {
        soundFallback.maxSoundVolume = Sound_GetMaxVolume();
        soundFallback.b3DHWSupport   = Sound_Get3DHWState();
        soundFallback.bReverseSound  = stdConfig_GetBool(JONESCONFIG_CFG_SOUND_REVERSE, false)
            ? 1 : 0;
        pSound = &soundFallback;
    }
    JonesUiSoundValues soundValues;
    const JonesUiSoundModel soundModel = JonesUiSound_LoadModel(pSound, soundValues);
    JonesUiSoundLayout soundLayout;

    std::size_t active = kJonesOptionsGameplayTab;
    int result = kJonesOptionsCancel;

    // Reads the active tab's row values while its screen is open. Called on every value change
    // and before a tab or the whole screen closes, so Back applies the same values OK does.
    const auto readActiveTab = [&](babylon::ui::Ui& uiRef, babylon::ui::ScreenId screenId)
    {
        switch ( active )
        {
            case kJonesOptionsGameplayTab:
                JonesUiGameplay_ReadValues(uiRef, screenId, gameplayLayout, gameplayValues);
                break;

            case kJonesOptionsControlsTab:
                JonesUiControls_ReadValues(uiRef, screenId, controlsLayout, controlsModel);
                break;

            case kJonesOptionsDisplayTab:
                JonesUiDisplay_ReadValues(uiRef, screenId, displayLayout, displayValues);
                break;

            case kJonesOptionsSoundTab:
                JonesUiSound_ReadValues(uiRef, screenId, soundLayout, soundValues);
                break;

            default:
                break;
        }
    };

    const JonesUiModalEventHandler handler = [&](babylon::ui::Ui& uiRef,
        babylon::ui::ScreenId screenId, const babylon::ui::UiEvent& event) -> bool
    {
        // Added: keep the active tab's model current, so Back (which closes the screen without an
        // action event) still applies the values the player just changed.
        if ( const babylon::ui::ValueChanged* pChanged =
                std::get_if<babylon::ui::ValueChanged>(&event) )
        {
            if ( pChanged->screen == screenId )
            {
                readActiveTab(uiRef, screenId);
            }
            return false;
        }

        const babylon::ui::TabChanged* pTab = std::get_if<babylon::ui::TabChanged>(&event);
        if ( pTab && pTab->screen == screenId )
        {
            // Keep the leaving tab's row values in its model; they are rebuilt when the player
            // returns, and applied only when the whole Options screen is accepted.
            readActiveTab(uiRef, screenId);

            const std::size_t next = std::min(pTab->tab, kJonesOptionsNumTabs - 1);
            static_cast<void>(uiRef.pop(
                (babylon::ui::ResultCode)(kJonesOptionsTabResultBase + next)));
            return true;
        }

        if ( const babylon::ui::ActionPressed* pAction =
                std::get_if<babylon::ui::ActionPressed>(&event) )
        {
            if ( pAction->screen != screenId )
            {
                return false;
            }

            if ( pAction->action == kJonesOptionsRunSetup )
            {
                if ( !data.bLevelRunning && ConfirmRunSetup(uiRef, pPainter, background) )
                {
                    JonesBabylon_RequestSetup();
                }
                return true;
            }

            // Added: the Display tab's "Brightness..." entry opens the calibration on top of
            // Options. It writes its own key, so the tab's apply path does not touch it.
            if ( active == kJonesOptionsDisplayTab
                && pAction->action == kJonesDisplayActionBrightness )
            {
                static_cast<void>(JonesUiBrightness_Run(uiRef, pPainter));
                return true;
            }

            // Added: the Controls tab's own rows (New, Delete, Controller, Keyboard & Mouse,
            // Reset) open their nested screens through the shared handler. Without this the modal
            // loop treats the action id as the screen's close result and Options closes at once.
            if ( active == kJonesOptionsControlsTab
                && JonesUiControls_HandleAction(uiRef, screenId, pAction->action, pPainter,
                    controlsModel, controlsLayout) )
            {
                return true;
            }

            if ( pAction->action != kJonesOptionsOk )
            {
                return false;
            }

            // Read the accepting tab before the modal loop pops it. The other tabs were read
            // when the player left them.
            readActiveTab(uiRef, screenId);

            // The default action handling pops the screen with the action id (1).
            return false;
        }

        return false;
    };

    for ( ;; )
    {
        babylon::ui::Screen screen;
        switch ( active )
        {
            case kJonesOptionsGameplayTab:
                screen = BuildGameplayTab(data.bEnhancedProfile, gameplayValues,
                    gameplayLayout);
                break;

            case kJonesOptionsControlsTab:
                screen = BuildControlsTab(controlsModel, controlsLayout, data.bEnhancedProfile);
                break;

            case kJonesOptionsDisplayTab:
                screen = BuildDisplayTab(displayModel, displayValues, displayLayout);
                break;

            case kJonesOptionsSoundTab:
                screen = BuildSoundTab(soundModel, soundValues, soundLayout);
                break;

            default:
                screen = BuildGameDataTab(data, pPainter,
                    pPainter ? ui.skin().textFont : 0, OptionsTextWidth(ui, pPainter));
                break;
        }

        result = JonesUiModal_RunHandled(ui, pPainter, screen, Identity, handler, background);
        if ( result >= (int)kJonesOptionsTabResultBase
            && result < (int)(kJonesOptionsTabResultBase + kJonesOptionsNumTabs) )
        {
            active = (std::size_t)(result - (int)kJonesOptionsTabResultBase);
            continue;
        }
        break;
    }

    if ( result != kJonesOptionsOk )
    {
        JonesUiControls_FreeModel(controlsModel);
        return kJonesOptionsCancel;
    }

    JonesUiGameplay_Apply(gameplayValues, data.bEnhancedProfile);
    if ( !data.bEnhancedProfile )
    {
        // Added: the per-device styles are Enhanced options; Retail keeps both Classic.
        controlsModel.bKeyboardModern = false;
        controlsModel.bGamepadModern  = false;
    }
    if ( data.pControlsConfig )
    {
        static_cast<void>(JonesUiControls_Apply(controlsModel, *data.pControlsConfig));
    }
    if ( data.pDisplaySettings )
    {
        static_cast<void>(JonesUiDisplay_Apply(displayModel, displayValues));
    }
    if ( data.pSoundSettings )
    {
        static_cast<void>(JonesUiSound_Apply(soundModel, soundValues));
    }

    JonesUiControls_FreeModel(controlsModel);
    return kJonesOptionsOk;
}

#ifdef J3D_TEST
babylon::ui::Screen JonesUiOptions_BuildTabScreenForTest(std::size_t tab,
    const JonesUiOptionsData& data, JonesUiControlsLayout* pControlsLayout)
{
    switch ( tab )
    {
        case kJonesOptionsGameplayTab:
        {
            const JonesUiGameplayValues values = JonesUiGameplay_LoadValues(
                data.bEnhancedProfile);
            JonesUiGameplayLayout layout;
            return BuildGameplayTab(data.bEnhancedProfile, values, layout);
        }

        case kJonesOptionsControlsTab:
        {
            JonesUiControlsModel model;
            if ( data.pControlsConfig )
            {
                static_cast<void>(JonesUiControls_InitModel(model, *data.pControlsConfig));
            }
            JonesUiControlsLayout local;
            JonesUiControlsLayout& layout = pControlsLayout ? *pControlsLayout : local;
            babylon::ui::Screen screen = BuildControlsTab(model, layout, data.bEnhancedProfile);
            JonesUiControls_FreeModel(model);
            return screen;
        }

        case kJonesOptionsDisplayTab:
        {
            JonesDisplaySettings fallback{};
            JonesDisplaySettings* pSettings = data.pDisplaySettings ? data.pDisplaySettings
                : &fallback;
            JonesUiDisplayValues values;
            const JonesUiDisplayModel model = JonesUiDisplay_LoadModel(data.pDisplayEnv,
                pSettings, values);
            JonesUiDisplayLayout layout;
            return BuildDisplayTab(model, values, layout);
        }

        case kJonesOptionsSoundTab:
        {
            JonesSoundSettings fallback{};
            fallback.maxSoundVolume = 1.0f;
            JonesSoundSettings* pData = data.pSoundSettings ? data.pSoundSettings : &fallback;
            JonesUiSoundValues values;
            const JonesUiSoundModel model = JonesUiSound_LoadModel(pData, values);
            JonesUiSoundLayout layout;
            return BuildSoundTab(model, values, layout);
        }

        default:
            return BuildGameDataTab(data, nullptr, 0, 0.0f);
    }
}
#endif

namespace
{
    // Wraps the menu background so the Options screen draws it darkened (ADR-0028). The overlay
    // draws the image and the dim at native resolution; draw stays in the game renderer.
    JonesUiModalBackground DarkenBackground(const JonesUiModalBackground& background,
        babylon::ui::Painter& painter, babylon::ui::Ui& ui)
    {
        if ( background.image == 0 && !background.draw )
        {
            return JonesUiModalBackground{};
        }

        const babylon::ui::Color dim = ui.skin().dim;
        const std::function<void()> previousOverlay = background.overlay;
        JonesUiModalBackground dark = background;
        dark.overlay = [&painter, &ui, dim, previousOverlay]()
        {
            if ( previousOverlay )
            {
                previousOverlay();
            }

            const babylon::ui::Size canvas = ui.canvasSize();
            painter.fillRect(babylon::ui::Rect{ 0.0f, 0.0f, canvas.width, 480.0f }, dim);
        };
        return dark;
    }
}

int JonesUiOptions_Show(const JonesUiModalBackground* pBackground, bool bLevelRunning)
{
    StdBabylonContext* pContext = stdBabylon_GetContext();
    if ( !pContext || !pContext->pWindow || !pContext->pFileSystem )
    {
        return kJonesOptionsCancel;
    }

    JonesUiContext* pUiContext = JonesUiContext_Acquire();
    if ( !pUiContext )
    {
        return kJonesOptionsCancel;
    }

    // Cinzel for the tabs and Jost for the rows and help, from the context's one font set.
    babylon::uirender::RenderPainter& painter = pUiContext->painter;
    const babylon::ui::FontId tabFont = pUiContext->titleFont;
    const babylon::ui::FontId rowFont = pUiContext->rowFont;
    const babylon::ui::Skin previousSkin = pUiContext->pUi->skin();
    const babylon::ui::Skin optionsSkin = JonesUiSkin_CreateOptions(tabFont, rowFont,
        rowFont, &painter);
    static_cast<void>(pUiContext->pUi->setSkin(optionsSkin));

    // Gather the settings the tabs edit. The control scheme list is loaded here because the
    // standalone Controls wrapper is no longer on the menu path.
    JonesControlsConfig controlsConfig;
    memset(&controlsConfig, 0, sizeof(controlsConfig));
    controlsConfig.selectedShemeIdx = -1;
    const bool bControlsLoaded = JonesControlSchemes_LoadAllSchemes(&controlsConfig);

    JonesDisplaySettings* pDisplaySettings = JonesMain_GetDisplaySettings();
    JonesSoundSettings soundSettings;
    soundSettings.maxSoundVolume = Sound_GetMaxVolume();
    soundSettings.b3DHWSupport   = Sound_Get3DHWState();
    soundSettings.bReverseSound  = stdConfig_GetBool(JONESCONFIG_CFG_SOUND_REVERSE, false)
        ? 1 : 0;

    // The player's profile is fixed by the build, as the standalone Gameplay screen shows.
    std::string profile = J3D_QOL_VALUE("enhanced", "retail");
    if ( const std::optional<std::string> value = pContext->pCvars->getString("game.profile") )
    {
        profile = *value;
    }

    JonesUiOptionsData data;
    data.bEnhancedProfile = profile != "retail";
    data.pDisplayEnv      = JonesMain_GetDisplayEnvironment();
    data.pDisplaySettings = pDisplaySettings;
    data.pSoundSettings   = &soundSettings;
    data.pControlsConfig  = bControlsLoaded ? &controlsConfig : nullptr;
    data.pRelease         = JonesMain_GetDataRelease();
    data.pInstallPath     = JonesMain_GetInstallPath();
    data.bLevelRunning    = bLevelRunning;

    // A nested Options screen keeps the session's background; temporarily replace it with the
    // darkened version and put it back for the menu after the loop.
    JonesUiContext* pActive = JonesUiContext_GetActive();
    const bool bSessionStarted = pActive != nullptr && pActive->bModalBackgroundStarted;
    const JonesUiModalBackground savedBackground = bSessionStarted
        ? pActive->modalBackground : JonesUiModalBackground{};

    JonesUiModalBackground dark;
    if ( pBackground )
    {
        dark = DarkenBackground(*pBackground, painter, *pUiContext->pUi);
    }
    else if ( bSessionStarted )
    {
        dark = DarkenBackground(pActive->modalBackground, painter, *pUiContext->pUi);
    }
    if ( bSessionStarted )
    {
        pActive->modalBackground = dark;
    }

    const int result = JonesUiOptions_RunOptionsScreen(*pUiContext->pUi, &painter, data, dark);

    if ( bSessionStarted )
    {
        pActive->modalBackground = savedBackground;
    }

    if ( bControlsLoaded )
    {
        jonesConfig_FreeControlConfigEntry(&controlsConfig);
    }

    if ( result == kJonesOptionsOk )
    {
        // The same post-OK work the standalone wrappers did.
        if ( pDisplaySettings )
        {
            JonesMain_RefreshDisplayDevice();
        }
        Sound_SetMaxVolume(soundSettings.maxSoundVolume);
        Sound_Set3DHWState(soundSettings.b3DHWSupport);
        Sound_SetReverseSound(soundSettings.bReverseSound);
    }

    static_cast<void>(pUiContext->pUi->setSkin(previousSkin));
    JonesUiContext_Release(pUiContext);
    return result;
}
