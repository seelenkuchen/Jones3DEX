// Added: the Babylon column's pause menu; see JonesUiPauseMenu.h.
//
// The entries are the HUD menu's system items, so each calls the same shared JonesHud_Show*
// function that the HUD's branch calls (ADR-0026). The opening code in JonesMain pauses the game
// around this screen, exactly as the HUD paused it around a system dialog.
#include <Jones3D/Platform/Babylon/JonesUiPauseMenu.h>

#include <Jones3D/Display/JonesHud.h>
#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Platform/Babylon/JonesUiContext.h>
#include <Jones3D/Platform/Babylon/JonesUiModal.h>
#include <Jones3D/Platform/Babylon/JonesUiSkin.h>

#include <sith/Dss/sithGamesave.h>
#include <sith/Dss/sithGamesaveSys.h>

#include <std/Babylon/stdBabylon.h>
#include <std/General/std.h>
#include <std/General/stdDisplay.h>

#include <babylon/ui/Skin.h>
#include <babylon/ui/UiEvent.h>
#include <babylon/ui/Widget.h>

#include <string>
#include <variant>

namespace
{
    constexpr babylon::ui::ActionId kJonesPauseResume     = 1;
    constexpr babylon::ui::ActionId kJonesPauseSave       = 2;
    constexpr babylon::ui::ActionId kJonesPauseLoad       = 3;
    constexpr babylon::ui::ActionId kJonesPauseSettings   = 4;
    constexpr babylon::ui::ActionId kJonesPauseStatistics = 5;
    constexpr babylon::ui::ActionId kJonesPauseExit       = 6;

    // Resume and Back both close; the wrapper always answers the closed value.
    constexpr babylon::ui::ResultCode kJonesPauseClosed = 1;

    constexpr babylon::ui::ActionId kJonesSettingsGamePlay = 11;
    constexpr babylon::ui::ActionId kJonesSettingsControls = 12;
    constexpr babylon::ui::ActionId kJonesSettingsDisplay  = 13;
    constexpr babylon::ui::ActionId kJonesSettingsSound    = 14;
    constexpr babylon::ui::ActionId kJonesSettingsBack     = 15;

    constexpr babylon::ui::ResultCode kJonesSettingsClosed = 15;

    constexpr int kJonesDialogCancel = 2;

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
}

babylon::ui::Screen JonesUiPauseMenu_BuildScreen(void)
{
    babylon::ui::Screen screen;
    // The main menu style has no title; the items are the whole screen.
    screen.title.clear();

    // TODO: [COMPAT] ADR-0028 names RESUME and STATISTICS; the string table only has "Return to Game" and
    // "Game Statistics".
    screen.widgets.emplace_back(babylon::ui::Button{
        "RESUME", kJonesPauseResume, true });
    screen.widgets.emplace_back(babylon::ui::Button{
        Uppercase(MenuLabel("JONES_STR_INV_SAVE", "Save Game")), kJonesPauseSave, true });
    screen.widgets.emplace_back(babylon::ui::Button{
        Uppercase(MenuLabel("JONES_STR_INV_LOAD", "Load Game")), kJonesPauseLoad, true });
    screen.widgets.emplace_back(babylon::ui::Button{
        Uppercase(MenuLabel("JONES_STR_OPTIONS", "Options")), kJonesPauseSettings, true });
    screen.widgets.emplace_back(babylon::ui::Button{
        "STATISTICS", kJonesPauseStatistics, true });
    screen.widgets.emplace_back(babylon::ui::Button{
        Uppercase(MenuLabel("JONES_STR_INV_EXIT", "Exit Game")), kJonesPauseExit, true });

    screen.backResult   = kJonesPauseClosed;
    screen.initialFocus = 0;
    return screen;
}

babylon::ui::Screen JonesUiPauseMenu_BuildSettingsScreen(void)
{
    babylon::ui::Screen screen;
    // JONES_STR_SETTINGS is "Settings for %s", a format string with no plain title.
    screen.title = "Settings"; // TODO: [COMPAT] no game key holds a plain "Settings"

    screen.widgets.emplace_back(babylon::ui::Button{
        JonesUiContext_ResolveString("JONES_STR_GMPLY_OPTS"), kJonesSettingsGamePlay, true });
    screen.widgets.emplace_back(babylon::ui::Button{
        JonesUiContext_ResolveString("JONES_STR_CTRL_OPTS"), kJonesSettingsControls, true });
    screen.widgets.emplace_back(babylon::ui::Button{
        JonesUiContext_ResolveString("JONES_STR_DPLY_OPTS"), kJonesSettingsDisplay, true });
    screen.widgets.emplace_back(babylon::ui::Button{
        JonesUiContext_ResolveString("JONES_STR_SND_OPTS"), kJonesSettingsSound, true });
    screen.widgets.emplace_back(babylon::ui::Button{
        JonesUiContext_ResolveString("JONES_STR_CANCEL"), kJonesSettingsBack, true });

    screen.backResult   = kJonesSettingsClosed;
    screen.initialFocus = 0;
    return screen;
}

int JonesUiPauseMenu_RunPauseMenu(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    const JonesUiModalBackground& background, const babylon::ui::Skin* pMenuSkin)
{
    const babylon::ui::Screen screen = JonesUiPauseMenu_BuildScreen();

    // The nested screens keep the look they had before the pause menu skin (the Settings screen,
    // save and load, Statistics, Exit), exactly as the main menu's nested Settings screen does.
    const babylon::ui::Skin previousSkin = ui.skin();
    if ( pMenuSkin )
    {
        static_cast<void>(ui.setSkin(*pMenuSkin));
    }

    const auto restoreMenuSkin = [&ui, pMenuSkin]()
    {
        if ( pMenuSkin )
        {
            static_cast<void>(ui.setSkin(*pMenuSkin));
        }
    };

    JonesUiModalEventHandler handler = [&](babylon::ui::Ui& uiRef,
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
            case kJonesPauseResume:
                static_cast<void>(uiRef.pop(kJonesPauseClosed));
                return true;

            case kJonesPauseSave:
                // The save screen nests on the pause menu; its answer leaves the menu as it was.
                static_cast<void>(uiRef.setSkin(previousSkin));
                static_cast<void>(JonesHud_ShowSaveGame());
                restoreMenuSkin();
                return true;

            case kJonesPauseLoad:
                static_cast<void>(uiRef.setSkin(previousSkin));
                if ( JonesHud_ShowLoadGame() == 1 )
                {
                    // Loading begins once the menu closes, so close it like the HUD did.
                    static_cast<void>(uiRef.pop(kJonesPauseClosed));
                }
                restoreMenuSkin();
                return true;

            case kJonesPauseSettings:
            {
                const babylon::ui::Screen settings = JonesUiPauseMenu_BuildSettingsScreen();
                JonesUiModalEventHandler settingsHandler = [](babylon::ui::Ui& settingsUi,
                    babylon::ui::ScreenId settingsId, const babylon::ui::UiEvent& settingsEvent) -> bool
                {
                    const babylon::ui::ActionPressed* pSettingsAction =
                        std::get_if<babylon::ui::ActionPressed>(&settingsEvent);
                    if ( !pSettingsAction || pSettingsAction->screen != settingsId )
                    {
                        return false;
                    }

                    switch ( pSettingsAction->action )
                    {
                        case kJonesSettingsGamePlay:
                            static_cast<void>(JonesHud_ShowGamePlaySettings());
                            return true;

                        case kJonesSettingsControls:
                            static_cast<void>(JonesHud_ShowControlSettings());
                            return true;

                        case kJonesSettingsDisplay:
                            static_cast<void>(JonesHud_ShowDisplaySettings());
                            return true;

                        case kJonesSettingsSound:
                            static_cast<void>(JonesHud_ShowSoundSettings());
                            return true;

                        case kJonesSettingsBack:
                            static_cast<void>(settingsUi.pop(kJonesSettingsClosed));
                            return true;

                        default:
                            return false;
                    }
                };

                static_cast<void>(uiRef.setSkin(previousSkin));
                static_cast<void>(JonesUiModal_RunHandled(uiRef, pPainter, settings,
                    [](babylon::ui::ResultCode code) { return (int)code; }, settingsHandler));
                restoreMenuSkin();
                return true;
            }

            case kJonesPauseStatistics:
                static_cast<void>(uiRef.setSkin(previousSkin));
                static_cast<void>(JonesHud_ShowStatistics());
                restoreMenuSkin();
                return true;

            case kJonesPauseExit:
                static_cast<void>(uiRef.setSkin(previousSkin));
                if ( JonesHud_ShowExitGame() != kJonesDialogCancel )
                {
                    // The game loop sees the activated exit once the menu is closed.
                    static_cast<void>(uiRef.pop(kJonesPauseClosed));
                }
                restoreMenuSkin();
                return true;

            default:
                return false;
        }
    };

    const int result = JonesUiModal_RunHandled(ui, pPainter, screen,
        [](babylon::ui::ResultCode code) { return (int)code; }, handler, background);

    if ( pMenuSkin )
    {
        static_cast<void>(ui.setSkin(previousSkin));
    }
    return result;
}

bool JonesUiPauseMenu_CaptureThumbnail(tVBuffer* pFrame)
{
    if ( !pFrame )
    {
        pFrame = &stdDisplay_g_backBuffer;
    }

    // Replace a thumbnail an earlier opening held; CreateThumbnail otherwise returns it unchanged.
    sithGamesaveSys_ReleaseThumbnail();
    sithGamesave_SetThumbnailImage(pFrame);
    return sithGamesave_CreateThumbnail() != NULL;
}

extern "C" int J3DAPI jonesConfig_ShowPauseMenu(tSysWindow hWnd)
{
    (void)hWnd;

    StdBabylonContext* pContext = stdBabylon_GetContext();
    if ( !pContext || !pContext->pWindow || !pContext->pFileSystem )
    {
        // Headless: the Null stub answers the closed value without blocking.
        return 2;
    }

    JonesUiContext* pUiContext = JonesUiContext_Acquire();
    if ( !pUiContext )
    {
        return 2;
    }

    // The game frame is still in the back buffer here; capture it before the modal loop draws the
    // dimmed frame and the menu over it, so a save made from the menu shows the game.
    static_cast<void>(JonesUiPauseMenu_CaptureThumbnail(NULL));

    // The pause menu uses the main menu's Cinzel list at 22 canvas units.
    constexpr const char* kPauseMenuFontFile = "fonts/Cinzel[wght].ttf";
    constexpr float kPauseMenuFontSize       = 22.0f;

    babylon::ui::FontId cinzel = pUiContext->painter.registerTtfFontFile(
        *pContext->pFileSystem, kPauseMenuFontFile, kPauseMenuFontSize);
    if ( cinzel == 0 )
    {
        STDLOG_WARNING("Pause menu: '%s' unavailable, using the rdFont skin.\n",
            kPauseMenuFontFile);
    }

    const babylon::ui::FontId menuFont = cinzel != 0 ? cinzel : pUiContext->font;
    const babylon::ui::Skin menuSkin =
        JonesUiSkin_CreateMainMenu(menuFont, &pUiContext->painter);

    // Keep today's dim from the modal loop and add the main menu's lower-left shade over it. When
    // the context already owns a background (an earlier screen in the same session), the modal
    // loop reuses that one, so the overlay is set there too.
    JonesUiModalBackground background;
    background.overlay = [pUiContext]()
    {
        JonesUiSkin_DrawBottomLeftShade(pUiContext->painter,
            pUiContext->pUi->canvasSize());
    };
    if ( pUiContext->bModalBackgroundStarted )
    {
        pUiContext->modalBackground.overlay = background.overlay;
    }

    static_cast<void>(JonesUiPauseMenu_RunPauseMenu(*pUiContext->pUi, &pUiContext->painter,
        background, &menuSkin));

    // A pending save keeps the thumbnail until sithGamesave_Process writes it; every other close
    // drops it, so a later quicksave captures its own frame instead of this stale one.
    const char* pPendingSave = NULL;
    if ( sithGamesave_GetState(&pPendingSave) != SITHGAMESAVE_SAVE )
    {
        sithGamesaveSys_ReleaseThumbnail();
    }

    JonesUiContext_Release(pUiContext);
    return 2;
}
