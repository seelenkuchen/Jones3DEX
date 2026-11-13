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
#include <Jones3D/Platform/Babylon/JonesUiContext.h>
#include <Jones3D/Platform/Babylon/JonesUiControls.h>
#include <Jones3D/Platform/Babylon/JonesUiDisplaySettings.h>
#include <Jones3D/Platform/Babylon/JonesUiGameplaySettings.h>
#include <Jones3D/Platform/Babylon/JonesUiPainter.h>
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

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace
{
    constexpr int kJonesOptionsOk     = 1;
    constexpr int kJonesOptionsCancel = 2;

    // A tab switch closes the current tab's screen with one of these results; the outer loop
    // builds that tab next. They stay clear of the dialog results 1 and 2.
    constexpr babylon::ui::ResultCode kJonesOptionsTabResultBase = 100;
    constexpr std::size_t kJonesOptionsNumTabs = 4;

    constexpr std::size_t kJonesOptionsGameplayTab = 0;
    constexpr std::size_t kJonesOptionsControlsTab = 1;
    constexpr std::size_t kJonesOptionsDisplayTab  = 2;
    constexpr std::size_t kJonesOptionsSoundTab    = 3;

    // Added: the tab titles and the screen title. The original game has no key for plain tab
    // names, so the English text is used until one exists.
    constexpr const char* kJonesOptionsTitleText =
        "Options"; // TODO: [COMPAT] JONES_STR_SETTINGS is a format string
    constexpr const char* kJonesOptionsGameplayText = "Gameplay"; // TODO: [COMPAT]
    constexpr const char* kJonesOptionsControlsText = "Controls"; // TODO: [COMPAT]
    constexpr const char* kJonesOptionsDisplayText  = "Display";  // TODO: [COMPAT]
    constexpr const char* kJonesOptionsSoundText    = "Sound";    // TODO: [COMPAT]

    const char* const kJonesOptionsTabNames[kJonesOptionsNumTabs] = {
        kJonesOptionsGameplayText,
        kJonesOptionsControlsText,
        kJonesOptionsDisplayText,
        kJonesOptionsSoundText,
    };

    void SetTabs(babylon::ui::Screen& screen, std::size_t active)
    {
        screen.title = kJonesOptionsTitleText;
        screen.tabs.assign(kJonesOptionsTabNames, kJonesOptionsTabNames + kJonesOptionsNumTabs);
        screen.activeTab = std::min(active, kJonesOptionsNumTabs - 1);
    }

    int Identity(babylon::ui::ResultCode code)
    {
        return (int)code;
    }

    std::string Label(const char* pKey, const char* pFallback)
    {
        const std::string text = JonesUiContext_ResolveString(pKey);
        return text == pKey ? std::string(pFallback) : text;
    }

    babylon::ui::Screen BuildGameplayTab(bool bEnhancedProfile,
        const JonesUiGameplayValues& values, JonesUiGameplayLayout& layout)
    {
        babylon::ui::Screen screen = JonesUiGameplay_BuildScreen(bEnhancedProfile, values,
            layout);
        SetTabs(screen, kJonesOptionsGameplayTab);

        screen.help.assign(screen.widgets.size(), std::string());
        screen.help[layout.showText] = "Show spoken dialogue as text on screen.";
        screen.help[layout.showHints] = "Show a hint when the game has one for your progress.";
        screen.help[layout.rotateMap] = "Rotate the overhead map to follow the player's facing.";
        screen.help[layout.difficulty] = "Choose how hard combat and puzzles are.";
        screen.help[layout.defaultRun] = "Start running by default when the player moves.";
        screen.help[layout.reticle] = "Draw an aim reticle for the equipped weapon.";
        screen.help[layout.blood] = "Show blood splatter effects when enemies are hit.";
        screen.help[layout.profile] =
            "The game profile is fixed by this build and cannot be changed here.";
        screen.help[layout.fireFlash] = "Show the flash at the weapon muzzle when firing.";
        screen.help[layout.externalMaterials] =
            "Load material data from external CND files.";
        screen.help[layout.externalKeyframes] =
            "Load animation keyframes from external CND files.";
        screen.help[layout.ok] = "Apply every change on all tabs and close Options.";
        screen.help[layout.back] = "Close Options without applying changes.";
        return screen;
    }

    babylon::ui::Screen BuildControlsTab(const JonesUiControlsModel& model,
        JonesUiControlsLayout& layout)
    {
        babylon::ui::Screen screen = JonesUiControls_BuildOptionsTabScreen(model, layout);
        layout.ok = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Button{
            Label("OK", "OK"), kJonesOptionsOk, true });

        layout.back = screen.widgets.size();
        screen.widgets.emplace_back(babylon::ui::Button{
            JonesUiContext_ResolveString("JONES_STR_CANCEL"), kJonesOptionsCancel, true });

        screen.backResult = kJonesOptionsCancel;
        SetTabs(screen, kJonesOptionsControlsTab);

        screen.help.assign(screen.widgets.size(), std::string());
        screen.help[layout.scheme] =
            "Choose the control scheme used for the keyboard and gamepad bindings.";
        screen.help[layout.newScheme] = "Create a new custom control scheme.";
        screen.help[layout.deleteScheme] = "Delete the selected custom control scheme.";
        screen.help[layout.list] =
            "Select an action to open the scheme editor and change its bindings.";
        screen.help[layout.rumble] = "Turn gamepad vibration on or off.";
        screen.help[layout.edit] = "Edit the bindings of the selected scheme.";
        screen.help[layout.reset] = "Reset the selected custom scheme to its default bindings.";
        screen.help[layout.ok] = "Apply every change on all tabs and close Options.";
        screen.help[layout.back] = "Close Options without applying changes.";
        return screen;
    }

    babylon::ui::Screen BuildDisplayTab(const JonesUiDisplayModel& model,
        const JonesUiDisplayValues& values, JonesUiDisplayLayout& layout)
    {
        // ADR-0028: the Display tab has no Performance row. The applied value is still read and
        // written unchanged, so existing configs and savegames keep their level.
        babylon::ui::Screen screen = JonesUiDisplay_BuildScreen(model, values, layout, false);
        SetTabs(screen, kJonesOptionsDisplayTab);

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
        screen.help[layout.ok] = "Apply every change on all tabs and close Options.";
        screen.help[layout.back] = "Close Options without applying changes.";
        return screen;
    }

    babylon::ui::Screen BuildSoundTab(const JonesUiSoundModel& model,
        const JonesUiSoundValues& values, JonesUiSoundLayout& layout)
    {
        babylon::ui::Screen screen = JonesUiSound_BuildScreen(model, values, layout);
        SetTabs(screen, kJonesOptionsSoundTab);

        screen.help.assign(screen.widgets.size(), std::string());
        screen.help[layout.volume] = "Set the master sound volume.";
        screen.help[layout.reverse] = "Swap the left and right audio channels.";
        screen.help[layout.outputDevice] = "Choose the audio output device.";
        screen.help[layout.ok] = "Apply every change on all tabs and close Options.";
        screen.help[layout.back] = "Close Options without applying changes.";
        return screen;
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

    const JonesUiModalEventHandler handler = [&](babylon::ui::Ui& uiRef,
        babylon::ui::ScreenId screenId, const babylon::ui::UiEvent& event) -> bool
    {
        const babylon::ui::TabChanged* pTab = std::get_if<babylon::ui::TabChanged>(&event);
        if ( pTab && pTab->screen == screenId )
        {
            // Keep the leaving tab's row values in its model; they are rebuilt when the player
            // returns, and applied only when the whole Options screen is accepted.
            switch ( active )
            {
                case kJonesOptionsGameplayTab:
                    JonesUiGameplay_ReadValues(uiRef, screenId, gameplayLayout,
                        gameplayValues);
                    break;

                case kJonesOptionsControlsTab:
                    JonesUiControls_ReadValues(uiRef, screenId, controlsLayout, controlsModel);
                    break;

                case kJonesOptionsDisplayTab:
                    JonesUiDisplay_ReadValues(uiRef, screenId, displayLayout, displayValues);
                    break;

                default:
                    JonesUiSound_ReadValues(uiRef, screenId, soundLayout, soundValues);
                    break;
            }

            const std::size_t next = std::min(pTab->tab, kJonesOptionsNumTabs - 1);
            static_cast<void>(uiRef.pop(
                (babylon::ui::ResultCode)(kJonesOptionsTabResultBase + next)));
            return true;
        }

        if ( const babylon::ui::ActionPressed* pAction =
                std::get_if<babylon::ui::ActionPressed>(&event) )
        {
            if ( pAction->screen != screenId || pAction->action != kJonesOptionsOk )
            {
                return false;
            }

            // Read the accepting tab before the modal loop pops it. The other tabs were read
            // when the player left them.
            switch ( active )
            {
                case kJonesOptionsGameplayTab:
                    JonesUiGameplay_ReadValues(uiRef, screenId, gameplayLayout,
                        gameplayValues);
                    break;

                case kJonesOptionsControlsTab:
                    JonesUiControls_ReadValues(uiRef, screenId, controlsLayout, controlsModel);
                    break;

                case kJonesOptionsDisplayTab:
                    JonesUiDisplay_ReadValues(uiRef, screenId, displayLayout, displayValues);
                    break;

                default:
                    JonesUiSound_ReadValues(uiRef, screenId, soundLayout, soundValues);
                    break;
            }

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
                screen = BuildControlsTab(controlsModel, controlsLayout);
                break;

            case kJonesOptionsDisplayTab:
                screen = BuildDisplayTab(displayModel, displayValues, displayLayout);
                break;

            default:
                screen = BuildSoundTab(soundModel, soundValues, soundLayout);
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
    const JonesUiOptionsData& data)
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
            JonesUiControlsLayout layout;
            babylon::ui::Screen screen = BuildControlsTab(model, layout);
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

        default:
        {
            JonesSoundSettings fallback{};
            fallback.maxSoundVolume = 1.0f;
            JonesSoundSettings* pData = data.pSoundSettings ? data.pSoundSettings : &fallback;
            JonesUiSoundValues values;
            const JonesUiSoundModel model = JonesUiSound_LoadModel(pData, values);
            JonesUiSoundLayout layout;
            return BuildSoundTab(model, values, layout);
        }
    }
}
#endif

namespace
{
    // Cover-scales the menu image to the canvas, like the modal loop, so a darkened background
    // keeps the image's aspect.
    babylon::ui::Rect CoverRect(const babylon::ui::Size& canvas,
        const JonesUiModalBackground& background)
    {
        babylon::ui::Rect rect;
        if ( !(background.width > 0.0f) || !(background.height > 0.0f) )
        {
            rect.width  = canvas.width;
            rect.height = canvas.height;
            return rect;
        }

        const float scaleX = canvas.width / background.width;
        const float scaleY = canvas.height / background.height;
        const float scale  = scaleX > scaleY ? scaleX : scaleY;
        rect.width  = background.width * scale;
        rect.height = background.height * scale;
        rect.x = (canvas.width - rect.width) * 0.5f;
        rect.y = (canvas.height - rect.height) * 0.5f;
        return rect;
    }

    // Wraps the menu background so the Options screen draws it darkened (ADR-0028).
    JonesUiModalBackground DarkenBackground(const JonesUiModalBackground& background,
        JonesUiPainter& painter, babylon::ui::Ui& ui)
    {
        JonesUiModalBackground dark;
        if ( background.image == 0 && !background.draw )
        {
            return dark;
        }

        const babylon::ui::Color dim = ui.skin().dim;
        dark = background;
        dark.image = 0;
        dark.draw = [background, &painter, &ui, dim]()
        {
            if ( background.draw )
            {
                background.draw();
            }
            else if ( background.image != 0 )
            {
                painter.drawImage(background.image,
                    CoverRect(ui.canvasSize(), background));
            }

            const babylon::ui::Size canvas = ui.canvasSize();
            painter.fillRect(babylon::ui::Rect{ 0.0f, 0.0f, canvas.width, 480.0f }, dim);
        };
        return dark;
    }
}

int JonesUiOptions_Show(const JonesUiModalBackground* pBackground)
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

    // Cinzel for the tabs and Jost for the rows and help, at 16 canvas units like the main menu's
    // Cinzel. A missing font falls back to the shared rdFont.
    constexpr const char* kOptionsTabFontFile = "fonts/Cinzel[wght].ttf";
    constexpr const char* kOptionsRowFontFile = "fonts/Jost[wght].ttf";
    constexpr float kOptionsTabFontSize = 22.0f;
    constexpr float kOptionsRowFontSize = 16.0f;

    JonesUiPainter& painter = pUiContext->painter;
    const babylon::ui::FontId cinzel = painter.registerTtfFontFile(*pContext->pFileSystem,
        kOptionsTabFontFile, kOptionsTabFontSize);
    const babylon::ui::FontId jost = painter.registerTtfFontFile(*pContext->pFileSystem,
        kOptionsRowFontFile, kOptionsRowFontSize);
    if ( cinzel == 0 || jost == 0 )
    {
        STDLOG_WARNING("Options: a UI font is unavailable, using the rdFont skin.\n");
    }

    const babylon::ui::FontId tabFont = cinzel != 0 ? cinzel : pUiContext->font;
    const babylon::ui::FontId rowFont = jost != 0 ? jost : pUiContext->font;
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
