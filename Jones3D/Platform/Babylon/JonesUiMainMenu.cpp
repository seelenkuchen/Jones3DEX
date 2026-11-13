// Added: the windowed Babylon build's main menu (ADR-0026, ADR-0028); see JonesUiMainMenu.h.
// The menu replaces the launcher. Its background is the load-screen map of the
// last played level when the config remembers a last save, else the first level's; the menu theme
// is the load screen's own theme call. The Null column and the Babylon headless run never get
// here: jonesConfig_ResolveStartMode keeps their startMode unchanged.
#include <Jones3D/Platform/Babylon/JonesUiMainMenu.h>

#include <Jones3D/Platform/Babylon/JonesUiOptions.h>

#include <Jones3D/Display/JonesHud.h>
#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Main/JonesMain.h>
#include <Jones3D/Platform/Babylon/JonesUiContext.h>
#include <Jones3D/Platform/Babylon/JonesUiPainter.h>
#include <Jones3D/Platform/Babylon/JonesUiSaveLoad.h>
#include <Jones3D/Platform/Babylon/JonesUiSkin.h>
#include <Jones3D/Platform/Null/JonesPlatformNull.h>

#include <rdroid/Engine/rdMaterial.h>
#include <rdroid/Primitives/rdWallpaper.h>
#include <rdroid/Raster/rdCache.h>

#include <sith/Devices/sithSoundMixer.h>
#include <sith/World/sithWorld.h>

#include <sound/Sound.h>

#include <std/Babylon/stdBabylon.h>

#include <std/General/std.h>
#include <std/General/stdDisplay.h>

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
    constexpr ActionId kContinueAction = 6;


    // The modal loop reports the action id that closed the menu; the game's callers get the
    // JONES_MAINMENU_* value instead.
    int MapMainMenuResult(ResultCode code)
    {
        switch ( (ActionId)code )
        {
            case kNewGameAction:
                return JONES_MAINMENU_NEWGAME;

            case kContinueAction:
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

    // Uppercases the ASCII and Latin-1 letters the game's string table stores one byte each.
    std::string Uppercase(std::string text)
    {
        for ( char& chr : text )
        {
            const unsigned char value = (unsigned char)chr;
            if ( (value >= 'a' && value <= 'z')
                || (value >= 0xE0 && value <= 0xFE && value != 0xF7) )
            {
                chr = (char)(value - 0x20);
            }
        }
        return text;
    }

    Screen BuildMainMenuScreen(bool bCanLoad)
    {
        Screen menu;
        menu.title.clear();
        if ( bCanLoad )
        {
            menu.widgets.emplace_back(Button{
                Uppercase(MenuLabel("JONES_STR_CONTINUE", "Continue")),
                kContinueAction, true }); // TODO: [COMPAT] no game key holds a plain "Continue"
        }
        menu.widgets.emplace_back(Button{
            Uppercase(MenuLabel("JONES_STR_NEWGAME", "New Game")),
            kNewGameAction, true }); // TODO: [COMPAT] no game key holds a plain "New Game"
        menu.widgets.emplace_back(Button{
            Uppercase(MenuLabel("JONES_STR_LOADGM", "Load Game")), kLoadAction, bCanLoad });
        menu.widgets.emplace_back(Button{
            Uppercase(MenuLabel("JONES_STR_OPTIONS", "Options")),
            kSettingsAction, true }); // TODO: [COMPAT] JONES_STR_SETTINGS is a format
        menu.widgets.emplace_back(Button{
            "EXIT GAME", kQuitAction, true }); // TODO: [COMPAT] no game key holds exactly "Exit Game"
        menu.backResult   = kQuitAction;
        menu.initialFocus = 0;
        return menu;
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
    char* pOutLoadPath, const babylon::ui::Skin* pMenuSkin)
{
    const Screen menu = BuildMainMenuScreen(bCanLoad);

    // The context default to restore when the menu closes, and the skin its nested Load screen
    // runs on; the nested Options screen manages its own.
    const Skin settingsSkin = ui.skin();
    if ( pMenuSkin )
    {
        static_cast<void>(ui.setSkin(*pMenuSkin));
    }

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
            // Added: one tabbed Options screen replaces the Settings hub (ADR-0028). It reuses
            // this menu's background and restores the menu skin when it closes.
            static_cast<void>(JonesUiOptions_Show(&background));
            return true;
        }

        if ( pAction->action == kContinueAction || pAction->action == kLoadAction )
        {
            const ActionId action = pAction->action;
            const std::function<bool(char*)>& load = action == kContinueAction
                ? hooks.continueGame : hooks.loadGame;
            if ( !load )
            {
                static_cast<void>(uiRef.pop(action));
                return true;
            }

            // Added: a screen opened from the menu (the Load list and its warnings) uses the
            // context's default skin and the darkened menu background, like Options. The menu's
            // own skin and background come back when it closes.
            const bool bMenuSkin = pMenuSkin != nullptr;
            if ( bMenuSkin )
            {
                static_cast<void>(uiRef.setSkin(settingsSkin));
            }

            JonesUiContext* pActive = JonesUiContext_GetActive();
            const bool bDarken = pActive != nullptr && pActive->bModalBackgroundStarted;
            JonesUiModalBackground savedBackground;
            if ( bDarken )
            {
                savedBackground = pActive->modalBackground;
                const babylon::ui::Color dim = settingsSkin.dim;
                const std::function<void()> overlay = savedBackground.overlay;
                pActive->modalBackground.overlay = [pPainter, dim, overlay, &uiRef]()
                {
                    if ( overlay )
                    {
                        overlay();
                    }
                    if ( pPainter )
                    {
                        const babylon::ui::Size canvas = uiRef.canvasSize();
                        pPainter->fillRect(babylon::ui::Rect{ 0.0f, 0.0f, canvas.width,
                            canvas.height }, dim);
                    }
                };
            }

            char aPath[JONESCONFIG_GAMESAVE_FILEPATHSIZE] = { 0 };
            const bool bLoaded = pOutLoadPath && load(aPath);

            if ( bDarken )
            {
                pActive->modalBackground = savedBackground;
            }
            if ( bMenuSkin )
            {
                static_cast<void>(uiRef.setSkin(*pMenuSkin));
            }

            if ( bLoaded )
            {
                std::snprintf(pOutLoadPath, JONESCONFIG_GAMESAVE_FILEPATHSIZE, "%s", aPath);
                static_cast<void>(uiRef.pop(action));
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

    JonesUiModalResizeHandler resizeHandler;
    if ( hooks.resized )
    {
        resizeHandler = hooks.resized;
    }

    const int result = JonesUiModal_RunHandled(ui, pPainter, menu, MapMainMenuResult,
        menuHandler, background, resizeHandler);

    if ( pMenuSkin )
    {
        static_cast<void>(ui.setSkin(settingsSkin));
    }

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
babylon::ui::Screen JonesUiMainMenu_BuildScreenForTest(bool bCanLoad)
{
    return BuildMainMenuScreen(bCanLoad);
}

void JonesUiMainMenu_AppendScreensForTest(std::vector<babylon::ui::Screen>& screens)
{
    screens.push_back(BuildMainMenuScreen(true));
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

    // The HUD is opened after the main menu, so its menu sounds must be loaded here.
    JonesHud_InitializeMenuSounds();

    const JonesLevelInfo& level = *JonesMain_GetMainMenuBackgroundLevel();
    JonesUiPainter& painter     = pUiContext->painter;
    babylon::ui::Ui* pUi        = pUiContext->pUi.get();

    // The main menu uses Cinzel at 22 canvas units. registerTtfFontFile() bakes it at the
    // window's current pixel scale; the resize hook re-registers it when that scale changes.
    constexpr const char* kMainMenuFontFile = "fonts/Cinzel[wght].ttf";
    constexpr float kMainMenuFontSize       = 22.0f;

    babylon::ui::FontId cinzel = painter.registerTtfFontFile(*pContext->pFileSystem,
        kMainMenuFontFile, kMainMenuFontSize);
    if ( cinzel == 0 )
    {
        // Missing or unusable Cinzel: keep the shared rdFont look and say so once.
        STDLOG_WARNING("Main menu: '%s' unavailable, using the rdFont skin.\n",
            kMainMenuFontFile);
    }

    babylon::ui::FontId menuFont = cinzel != 0 ? cinzel : pUiContext->font;
    babylon::ui::Skin menuSkin   = JonesUiSkin_CreateMainMenu(menuFont, &painter, pUiContext->promptFont);

    float menuFontScale = 1.0f;
    {
        uint32_t width  = 0;
        uint32_t height = 0;
        stdDisplay_GetBackBufferSize(&width, &height);
        if ( height )
        {
            menuFontScale = (float)height / 480.0f;
        }
    }

    // Load the level's load-screen wallpaper the way JonesDisplay_OpenLoadScreen does, and draw
    // all six tiles behind the panel every frame. The wallpaper stays alive until the menu closes.
    rdWallpaper* pWallpaper = rdWallpaper_New(level.pMatFilename);
    JonesUiModalBackground background;
    if ( pWallpaper )
    {
        // Draw the same finished image the load screen leaves up before it closes.
        JonesUiMainMenu_SetFinishedWallpaper(pWallpaper);

        background.draw = [pWallpaper, &painter, pUi]()
        {
            rdWallpaper_Draw(pWallpaper);
            rdCache_Flush();
            rdCache_FlushAlpha();

            // With no panel box, a soft quadratic shade in the lower-left corner keeps the text
            // readable.
            JonesUiSkin_DrawBottomLeftShade(painter, pUi->canvasSize());
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
    hooks.continueGame = [&entries](char* pPath) -> bool
    {
        // The loader collects the save folder newest first; Continue takes the first entry and
        // returns the same engine path the Load screen would.
        if ( entries.empty() )
        {
            return false;
        }

        std::snprintf(pPath, JONESCONFIG_GAMESAVE_FILEPATHSIZE, "%s",
            entries.front().path.c_str());
        return true;
    };
    hooks.resized = [&](int width, int height)
    {
        (void)width;
        (void)height;
        float scale = 1.0f;
        if ( height > 0 )
        {
            scale = (float)height / 480.0f;
        }
        if ( scale == menuFontScale )
        {
            return;
        }
        menuFontScale = scale;

        const babylon::ui::FontId newFont = painter.registerTtfFontFile(
            *pContext->pFileSystem, kMainMenuFontFile, kMainMenuFontSize);
        if ( newFont != 0 )
        {
            menuFont = newFont;
            menuSkin = JonesUiSkin_CreateMainMenu(menuFont, &painter, pUiContext->promptFont);
            static_cast<void>(pUi->setSkin(menuSkin));
        }
    };
    const JonesUiMainMenuAction action = JonesUiMainMenu_Run(*pUiContext->pUi, &painter,
        background, bCanLoad, hooks, pOutLoadPath, &menuSkin);

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
