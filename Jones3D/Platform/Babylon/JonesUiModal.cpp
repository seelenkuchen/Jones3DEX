// Added: the Babylon column's modal screen loop; see JonesUiModal.h.
#include <Jones3D/Platform/Babylon/JonesUiModal.h>

#include <Jones3D/Display/JonesHud.h>
#include <Jones3D/Platform/Babylon/JonesUiContext.h>
#include <Jones3D/Platform/Babylon/JonesUiPrompts.h>
#include <Jones3D/Platform/Babylon/JonesUiSkin.h>
#include <Jones3D/Platform/Babylon/JonesWindowMode.h>

#include <babylon/platform/Event.h>
#include <babylon/platform/Window.h>
#include <babylon/ui/Skin.h>
#include <babylon/ui/UiEvent.h>

#include <std/Babylon/std3DBabylon.h>
#include <std/Babylon/stdBabylon.h>
#include <std/Babylon/stdBabylonEvents.h>
#include <std/Babylon/stdBabylonTour.h>

#include <std/General/std.h>
#include <std/General/std3D.h>
#include <std/General/stdDisplay.h>
#include <std/General/stdPlatform.h>

#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace
{
    // Added: the nesting depth of the modal loops, for the run log only.
    int g_modalDepth = 0;

    // True when the event is a quit request or a close of the game window (pWindow may be null:
    // then only a QuitRequested counts, as in stdBabylonEvents_Pump).
    bool IsQuitEvent(const babylon::platform::Event& event, const babylon::platform::Window* pWindow)
    {
        if ( std::holds_alternative<babylon::platform::QuitRequested>(event) )
        {
            return true;
        }

        const babylon::platform::WindowCloseRequested* pClose =
            std::get_if<babylon::platform::WindowCloseRequested>(&event);
        return pClose && pWindow && pClose->window == pWindow->id();
    }

    // A background the game renderer draws (the main menu wallpaper) goes into the game frame
    // before the present, so the overlay can put the screen over it.
    void DrawGameBackground(const JonesUiModalBackground& background)
    {
        if ( !background.draw )
        {
            return;
        }

        std3D_ClearZBuffer();
        if ( std3D_StartScene() == 0 )
        {
            background.draw();
            std3D_EndScene();
        }
    }
}

static int JonesUiModal_RunInternal(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    const babylon::ui::Screen& screen, const JonesUiModalResultMapper& mapResult,
    const JonesUiModalEventHandler& handler, const JonesUiModalBackground& background,
    const JonesUiModalResizeHandler& resizeHandler)
{
    // The screen is drawn by the context's present overlay; the painter argument is kept for the
    // public API and the direct-call unit tests.
    (void)pPainter;

    const babylon::ui::ScreenId screenId = ui.push(screen);
    if ( screenId == babylon::ui::kInvalidScreen )
    {
        return mapResult(screen.backResult);
    }

    ++g_modalDepth;

    // Added: name this screen in the scripted tour's present log until it closes. A screen opened
    // from a handler pushes and pops over this one.
    stdBabylonTour_PushScreenTitle(screen.title.c_str());

    StdBabylonContext& rContext = stdBabylon_RequireContext("JonesUiModal_Run");
    babylon::platform::Window* pWindow = rContext.pWindow;

    // A message can open from inside the game's frame (JonesHud_Process calls one). Closing the
    // caller's scene before the present keeps its pass from being dropped by the frame flush and
    // lets the loop begin scenes of its own; the caller gets a fresh scene before it continues.
    const bool bHadScene = std3D_IsInScene();
    if ( bHadScene )
    {
        std3D_EndScene();
    }

    // The background belongs to the ui session, not to one loop: the first modal loop while a
    // context is held adopts the opener's background, and every later loop reuses it.
    JonesUiContext* pUiContext = JonesUiContext_GetActive();
    const JonesUiModalBackground* pBackground = &background;
    if ( pUiContext )
    {
        if ( !pUiContext->bModalBackgroundStarted )
        {
            pUiContext->bModalBackgroundStarted = true;
            pUiContext->modalBackground         = background;
        }

        pBackground = &pUiContext->modalBackground;
    }

    const char* pBackgroundState = "none";
    if ( pBackground->draw )
    {
        pBackgroundState = "callback";
    }
    else if ( pBackground->image != 0 )
    {
        pBackgroundState = "image";
    }
    else
    {
        pBackgroundState = "game";
    }

    STDLOG_DEBUG("JonesUiModal_Run: context=%p depth=%d background=%s\n",
        (void*)pUiContext, g_modalDepth, pBackgroundState);

    int result = (int)screen.backResult;
    bool bClosed = false;
    uint32_t lastMsec = stdPlatform_GetTimeMsec();

    // Added: the prompt bar follows the last used device and the focused widget. The icon sets
    // belong to the shared context; a loop outside one draws the labels alone.
    const JonesUiPromptAssets emptyPromptAssets;
    const JonesUiPromptAssets& promptAssets = pUiContext ? pUiContext->prompts
                                                         : emptyPromptAssets;
    const bool bHasBack = screen.backResult != 0;

    while ( !bClosed )
    {
        const std::size_t numEvents = stdBabylonEvents_Pump();
        bool bQuit = stdBabylonEvents_PeekQuitRequest();

        for ( const babylon::platform::Event& event : stdBabylonEvents_LastEvents() )
        {
            if ( IsQuitEvent(event, pWindow) )
            {
                bQuit = true;
            }

            const babylon::platform::WindowResized* pResized =
                std::get_if<babylon::platform::WindowResized>(&event);
            if ( pResized && (!pWindow || pResized->window == pWindow->id()) )
            {
                static_cast<void>(ui.setViewport(pResized->size.width, pResized->size.height));
                if ( resizeHandler )
                {
                    resizeHandler(pResized->size.width, pResized->size.height);
                }
            }

            ui.handleEvent(event);
        }

        // Added: Alt+Enter switches windowed/fullscreen from a menu too.
        (void)JonesWindowMode_HandleHotkey();

        // A quit or close closes this screen with its back result but leaves the recorded request
        // for the game loop, which turns it into the WM_QUIT it expects after a dialog.
        if ( bQuit && ui.isOpen(screenId) )
        {
            ui.pop(screen.backResult);
        }

        const uint32_t nowMsec = stdPlatform_GetTimeMsec();
        const uint32_t elapsedMsec = nowMsec - lastMsec;
        lastMsec = nowMsec;
        ui.update((double)elapsedMsec / 1000.0);

        // Added: fill the bar after the frame's input so the Change entry follows a focus that
        // just moved, and before the events drain so a handler sees the same bar the player does.
        ui.setPrompts(JonesUiPrompts_Build(promptAssets,
            stdBabylonEvents_InputState().lastDevice(), bHasBack,
            ui.focusedWidgetChangesValue(), !screen.tabs.empty()));

        // Added: the frame's sound cues play after the drain. The accept and back cues of the
        // frame that closes this modal's screen are dropped: leaving a menu (Resume, Back, a
        // dialog's OK) is silent, like the original dialogs.
        std::vector<int> frameSoundCues;
        while ( const std::optional<babylon::ui::UiEvent> pEvent = ui.pollEvent() )
        {
            if ( handler && handler(ui, screenId, *pEvent) )
            {
                continue;
            }

            if ( const babylon::ui::ActionPressed* pAction =
                    std::get_if<babylon::ui::ActionPressed>(&*pEvent) )
            {
                if ( pAction->screen == screenId )
                {
                    // A dialog button's action id is its result code.
                    static_cast<void>(ui.pop((babylon::ui::ResultCode)pAction->action));
                }
            }
            else if ( const babylon::ui::ScreenClosed* pClosed =
                    std::get_if<babylon::ui::ScreenClosed>(&*pEvent) )
            {
                if ( pClosed->screen == screenId )
                {
                    result  = (int)pClosed->result;
                    bClosed = true;
                }
            }
            else if ( const babylon::ui::TextInputWanted* pTextInput =
                    std::get_if<babylon::ui::TextInputWanted>(&*pEvent) )
            {
                if ( pWindow )
                {
                    static_cast<void>(pWindow->setTextInput(pTextInput->on));
                }
            }
            else if ( const babylon::ui::SoundCue* pSound =
                    std::get_if<babylon::ui::SoundCue>(&*pEvent) )
            {
                // Added: play the HUD menu's own sound for the cue. The HUD owns the menu sounds
                // and loads them with its menu; before a world opens (the main menu at startup)
                // its handles are zero and the cue stays silent.
                frameSoundCues.push_back((int)pSound->cue);
            }
        }

        for ( const int cue : frameSoundCues )
        {
            if ( bClosed
                && (cue == (int)kJonesUiSoundCueAccept || cue == (int)kJonesUiSoundCueBack) )
            {
                continue;
            }

            JonesHud_PlayMenuSoundCue(cue);
        }

        if ( bClosed )
        {
            break;
        }

        if ( stdDisplay_IsOpen() )
        {
            DrawGameBackground(*pBackground);
            static_cast<void>(stdDisplay_Update());
        }
        else if ( numEvents == 0 )
        {
            // Nothing to do and nowhere to draw: yield like the blocking Win32 dialog did.
            stdPlatform_Sleep(1);
        }
    }

    // Give the caller back the scene it was drawing into, so its remaining draws and its own
    // std3D_EndScene work as they did around a native dialog.
    if ( bHadScene )
    {
        static_cast<void>(std3D_StartScene());
    }

    stdBabylonTour_PopScreenTitle();

    --g_modalDepth;

    return mapResult(result);
}

int JonesUiModal_Run(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    const babylon::ui::Screen& screen, const JonesUiModalResultMapper& mapResult,
    const JonesUiModalBackground& background)
{
    return JonesUiModal_RunInternal(ui, pPainter, screen, mapResult, JonesUiModalEventHandler(),
        background, nullptr);
}

int JonesUiModal_RunHandled(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    const babylon::ui::Screen& screen, const JonesUiModalResultMapper& mapResult,
    const JonesUiModalEventHandler& handler, const JonesUiModalBackground& background,
    const JonesUiModalResizeHandler& resizeHandler)
{
    return JonesUiModal_RunInternal(ui, pPainter, screen, mapResult, handler, background,
        resizeHandler);
}
