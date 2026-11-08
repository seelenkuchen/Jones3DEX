// Added: the windowed Babylon build's main menu and nested Settings screen (ADR-0026); see
// JonesUiMainMenu.h. The menu replaces the launcher. Its background is the load-screen map of the
// last played level when the config remembers a last save, else the first level's; the menu theme
// is the load screen's own theme call. The Null column and the Babylon headless run never get
// here: jonesConfig_ResolveStartMode keeps their startMode unchanged.
#include <Jones3D/Platform/Babylon/JonesUiMainMenu.h>

#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Main/JonesMain.h>
#include <Jones3D/Platform/Babylon/JonesUiContext.h>
#include <Jones3D/Platform/Babylon/JonesUiPainter.h>
#include <Jones3D/Platform/Babylon/JonesUiSaveLoad.h>
#include <Jones3D/Platform/Null/JonesPlatformNull.h>

#include <rdroid/Engine/rdMaterial.h>
#include <rdroid/Primitives/rdWallpaper.h>
#include <rdroid/Raster/rdCache.h>

#include <sith/Devices/sithSoundMixer.h>
#include <sith/World/sithWorld.h>

#include <sound/Sound.h>

#include <std/Babylon/stdBabylon.h>

#include <std/General/std.h>

#include <babylon/ui/Screen.h>
#include <babylon/ui/UiEvent.h>
#include <babylon/ui/Widget.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <variant>
#include <vector>

namespace
{
    using namespace babylon::ui;

    // Every action id is non-zero: 0 is babylon::ui::kNoAction, which reports no action at all.
    // The JONES_MAINMENU_* result values are mapped separately in MapMainMenuResult below.
    constexpr ActionId kNewGameAction  = 1;
    constexpr ActionId kLoadAction     = 2;
    constexpr ActionId kSettingsAction = 3;
    constexpr ActionId kQuitAction     = 4;

    constexpr ActionId kGameplayAction = 10;
    constexpr ActionId kControlsAction = 11;
    constexpr ActionId kDisplayAction  = 12;
    constexpr ActionId kSoundAction    = 13;
    constexpr ActionId kBackAction     = 5;

    // The modal loop reports the action id that closed the menu; the game's callers get the
    // JONES_MAINMENU_* value instead.
    int MapMainMenuResult(ResultCode code)
    {
        switch ( (ActionId)code )
        {
            case kNewGameAction:
                return JONES_MAINMENU_NEWGAME;

            case kLoadAction:
                return JONES_MAINMENU_LOADGAME;

            default:
                return JONES_MAINMENU_QUIT;
        }
    }

    // Resolves a JONES_STR_ key through the game's string table. When the table has no such key
    // (the repo holds no game assets), the readable English text is used instead.
    std::string MenuLabel(const char* pKey, const char* pFallback)
    {
        const std::string text = JonesUiContext_ResolveString(pKey);
        return text == pKey ? std::string(pFallback) : text;
    }

    Screen BuildMainMenuScreen(bool bCanLoad)
    {
        Screen menu;
        // No game key holds the game's full title or a plain "New Game".
        menu.title = "Indiana Jones and the Infernal Machine"; // TODO: [COMPAT]
        menu.widgets.emplace_back(Button{
            "New Game", kNewGameAction, true }); // TODO: [COMPAT]
        menu.widgets.emplace_back(Button{
            MenuLabel("JONES_STR_LOADGM", "Load Game"), kLoadAction, bCanLoad });
        menu.widgets.emplace_back(Button{
            "Settings", kSettingsAction, true }); // TODO: [COMPAT] JONES_STR_SETTINGS is a format
        menu.widgets.emplace_back(Button{
            MenuLabel("JONES_STR_QUIT", "Quit"), kQuitAction, true });
        menu.backResult   = kQuitAction;
        menu.initialFocus = 0;
        return menu;
    }

    Screen BuildSettingsScreen(void)
    {
        Screen screen;
        // JONES_STR_SETTINGS is "Settings for %s", a format string with no plain title.
        screen.title = "Settings"; // TODO: [COMPAT] no game key holds a plain "Settings"
        screen.widgets.emplace_back(Button{
            MenuLabel("JONES_STR_INV_DFCTLY", "Gameplay Options"), kGameplayAction, true });
        screen.widgets.emplace_back(Button{
            MenuLabel("JONES_STR_INV_CONFIG", "Control Options"), kControlsAction, true });
        screen.widgets.emplace_back(Button{
            MenuLabel("JONES_STR_INV_DSPLY", "Display Options"), kDisplayAction, true });
        screen.widgets.emplace_back(Button{
            MenuLabel("JONES_STR_INV_SND", "Sound Options"), kSoundAction, true });
        screen.widgets.emplace_back(Button{
            "Back", kBackAction, true }); // TODO: [COMPAT] JONES_STR_BACK is "BACK SPACE"
        screen.backResult   = kBackAction;
        screen.initialFocus = 0;
        return screen;
    }
}

void JonesUiMainMenu_SetFinishedWallpaper(rdWallpaper* pWallpaper)
{
    // JonesDisplay_UpdateLoadProgress advances the cels at each of its three completion stages.
    for ( size_t step = 0; step < 3; ++step )
    {
        rdWallpaper_AdvanceCels(pWallpaper);
    }
}

JonesUiMainMenuAction JonesUiMainMenu_Run(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    const JonesUiModalBackground& background, bool bCanLoad, const JonesUiMainMenuHooks& hooks,
    char* pOutLoadPath)
{
    const Screen menu = BuildMainMenuScreen(bCanLoad);

    // Runs the nested Settings screen and calls the matching hook for every settings button. Back
    // and Escape close it with kBackAction; the action's return value is ignored deliberately, so
    // a settings screen that stays a stub for now still behaves like a working button.
    JonesUiModalEventHandler settingsHandler = [&hooks](Ui& uiRef, ScreenId screenId,
        const UiEvent& event) -> bool
    {
        const ActionPressed* pAction = std::get_if<ActionPressed>(&event);
        if ( !pAction || pAction->screen != screenId )
        {
            return false;
        }

        switch ( pAction->action )
        {
            case kGameplayAction:
                if ( hooks.gameplay )
                {
                    hooks.gameplay();
                }
                return true;

            case kControlsAction:
                if ( hooks.controls )
                {
                    hooks.controls();
                }
                return true;

            case kDisplayAction:
                if ( hooks.display )
                {
                    hooks.display();
                }
                return true;

            case kSoundAction:
                if ( hooks.sound )
                {
                    hooks.sound();
                }
                return true;

            default:
                return false; // Back closes the screen with kBackAction
        }
    };

    JonesUiModalEventHandler menuHandler = [&](Ui& uiRef, ScreenId screenId,
        const UiEvent& event) -> bool
    {
        const ActionPressed* pAction = std::get_if<ActionPressed>(&event);
        if ( !pAction || pAction->screen != screenId )
        {
            return false;
        }

        if ( pAction->action == kSettingsAction )
        {
            static_cast<void>(JonesUiModal_RunHandled(uiRef, pPainter, BuildSettingsScreen(),
                [](ResultCode code) { return (int)code; }, settingsHandler, background));
            return true;
        }

        if ( pAction->action == kLoadAction )
        {
            if ( !hooks.loadGame )
            {
                static_cast<void>(uiRef.pop(kLoadAction));
                return true;
            }

            char aPath[JONESCONFIG_GAMESAVE_FILEPATHSIZE] = { 0 };
            if ( pOutLoadPath && hooks.loadGame(aPath) )
            {
                std::snprintf(pOutLoadPath, JONESCONFIG_GAMESAVE_FILEPATHSIZE, "%s", aPath);
                static_cast<void>(uiRef.pop(kLoadAction));
            }
            return true;
        }

        if ( pAction->action == kNewGameAction )
        {
            if ( hooks.newGame )
            {
                hooks.newGame();
            }
            static_cast<void>(uiRef.pop(kNewGameAction));
            return true;
        }

        return false; // Quit uses the loop's default pop with its action id
    };

    const int result = JonesUiModal_RunHandled(ui, pPainter, menu, MapMainMenuResult,
        menuHandler, background);

    if ( result == JONES_MAINMENU_NEWGAME )
    {
        return JonesUiMainMenuAction::NewGame;
    }

    if ( result == JONES_MAINMENU_LOADGAME )
    {
        return JonesUiMainMenuAction::LoadGame;
    }

    return JonesUiMainMenuAction::Quit;
}

#ifdef J3D_TEST
void JonesUiMainMenu_AppendScreensForTest(std::vector<babylon::ui::Screen>& screens)
{
    screens.push_back(BuildMainMenuScreen(true));
    screens.push_back(BuildSettingsScreen());
}
#endif

extern "C" int J3DAPI jonesConfig_ResolveStartMode(int startMode, int bHasStartMode)
{
    StdBabylonContext* pContext = stdBabylon_GetContext();
    if ( !pContext || !pContext->pWindow )
    {
        // Headless (and the Null column, which does not link this file): keep the value.
        return startMode;
    }

    // A --load path loads that save directly instead of opening the menu.
    const char* pLoadPath = JonesPlatformNull_GetLoadPath();
    if ( pLoadPath && pLoadPath[0] )
    {
        return JONES_STARTMODE_LOADGAME;
    }

    // The windowed build has no launcher and no developer dialog any more.
    if ( !bHasStartMode || startMode == JONES_STARTMODE_DEVELOPERDIALOG )
    {
        return JONES_STARTMODE_MAINMENU;
    }

    return startMode;
}

extern "C" int J3DAPI jonesConfig_DefersStartMode(int startMode)
{
    StdBabylonContext* pContext = stdBabylon_GetContext();
    if ( !pContext || !pContext->pWindow )
    {
        // Headless (and the Null column, which does not link this file): keep the trace.
        return 0;
    }

    // The Win32 dialogs could run before the game display existed; the in-game screens cannot.
    return startMode == JONES_STARTMODE_LOADGAME
        || startMode == JONES_STARTMODE_SOUNDSETTINGS
        || startMode == JONES_STARTMODE_DISPLAYSETTINGS;
}

extern "C" int J3DAPI jonesConfig_ShowMainMenu(tSysWindow hWnd, JonesState* pConfig,
    char* pOutLoadPath)
{
    (void)hWnd;

    if ( !pConfig || !pOutLoadPath )
    {
        return JONES_MAINMENU_QUIT;
    }

    pOutLoadPath[0] = '\0';

    StdBabylonContext* pContext = stdBabylon_GetContext();
    if ( !pContext || !pContext->pWindow || !pContext->pFileSystem )
    {
        // Headless: the Null stub's answer, without blocking.
        return JONES_MAINMENU_QUIT;
    }

    JonesUiContext* pUiContext = JonesUiContext_Acquire();
    if ( !pUiContext )
    {
        return JONES_MAINMENU_QUIT;
    }

    const JonesLevelInfo& level = *JonesMain_GetMainMenuBackgroundLevel();
    JonesUiPainter& painter     = pUiContext->painter;

    // Load the level's load-screen wallpaper the way JonesDisplay_OpenLoadScreen does, and draw
    // all six tiles behind the panel every frame. The wallpaper stays alive until the menu closes.
    rdWallpaper* pWallpaper = rdWallpaper_New(level.pMatFilename);
    JonesUiModalBackground background;
    if ( pWallpaper )
    {
        // Draw the same finished image the load screen leaves up before it closes.
        JonesUiMainMenu_SetFinishedWallpaper(pWallpaper);

        background.draw = [pWallpaper]()
        {
            rdWallpaper_Draw(pWallpaper);
            rdCache_Flush();
            rdCache_FlushAlpha();
        };
    }

    // The load screen's own theme call: the primary or secondary static theme of the level.
    tSoundChannelHandle hMenuMusic = 0;
    tSoundHandle hTheme = Sound_GetSoundHandle(
        SITHWORLD_STATICINDEX(level.bPrimaryMusicTheme ? 0 : 1));
    if ( hTheme )
    {
        sithSoundMixer_StopAll();
        hMenuMusic = sithSoundMixer_PlaySound(hTheme, 1.0f, 0.0f, SOUNDPLAY_LOOP);
    }

    std::vector<JonesUiSaveEntry> entries;
    const bool bCanLoad = JonesUiSave_CollectEntries(*pContext->pFileSystem,
        sithGetSaveGamesDir(), entries) && !entries.empty();

    JonesUiMainMenuHooks hooks;
    hooks.newGame = []() {};
    hooks.loadGame = [](char* pPath) -> bool
    {
        // The load dialog answers 1 on success and 2 on cancel; it writes only on success.
        return jonesConfig_GetLoadGameFilePath(NULL, pPath) == 1;
    };
    hooks.gameplay = []()
    {
        static_cast<void>(jonesConfig_ShowGamePlayOptions(NULL));
    };
    hooks.controls = []()
    {
        static_cast<void>(jonesConfig_ShowControlOptions(NULL));
    };
    hooks.display = [pConfig]()
    {
        static_cast<void>(jonesConfig_ShowDisplaySettingsDialog(NULL,
            JonesMain_GetDisplayEnvironment(), &pConfig->displaySettings));
    };
    hooks.sound = [pConfig]()
    {
        static_cast<void>(jonesConfig_ShowSoundSettingsDialog(NULL,
            &pConfig->soundSettings));
    };

    const JonesUiMainMenuAction action = JonesUiMainMenu_Run(*pUiContext->pUi, &painter,
        background, bCanLoad, hooks, pOutLoadPath);

    if ( hMenuMusic
        && (Sound_GetChannelFlags(hMenuMusic) & SOUND_CHANNEL_PLAYING) != 0 )
    {
        sithSoundMixer_StopSound(hMenuMusic);
    }

    if ( pWallpaper )
    {
        rdWallpaper_Free(pWallpaper);
    }

    JonesUiContext_Release(pUiContext);
    return (int)action;
}
