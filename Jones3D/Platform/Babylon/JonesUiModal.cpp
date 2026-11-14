// Added: the Babylon column's modal screen loop; see JonesUiModal.h.
#include <Jones3D/Platform/Babylon/JonesUiModal.h>

#include <Jones3D/Display/JonesHud.h>
#include <Jones3D/Platform/Babylon/JonesUiContext.h>
#include <Jones3D/Platform/Babylon/JonesUiPainter.h>
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
#include <std/General/stdColor.h>
#include <std/General/stdDisplay.h>
#include <std/General/stdPlatform.h>

#include <cstdint>
#include <cstring>
#include <optional>
#include <variant>
#include <vector>

namespace
{
    // A copy of the frame the game last drew, registered with the painter as an image. It is
    // captured once when a modal loop starts; the game does not draw while the loop runs, so it
    // stays the frozen frame behind the screen.
    struct FrozenFrame
    {
        babylon::ui::ImageId image = 0;

        void Capture(JonesUiPainter& painter)
        {
            void* pLocked      = nullptr;
            uint32_t width     = 0;
            uint32_t height    = 0;
            int32_t pitch      = 0;
            if ( stdDisplay_LockBackBufferReadOnly(&pLocked, &width, &height, &pitch) != 0
                || !pLocked || !width || !height )
            {
                return;
            }

            const tRasterInfo& back = stdDisplay_g_backBuffer.rasterInfo;
            const uint32_t bytesPerPixel = back.colorInfo.bpp / 8u;
            const size_t rowBytes        = (size_t)width * bytesPerPixel;
            int64_t rowPitch             = pitch;
            if ( rowPitch < 0 )
            {
                rowPitch = -rowPitch;
            }

            if ( !bytesPerPixel || rowPitch < (int64_t)rowBytes )
            {
                stdDisplay_UnlockBackBuffer();
                return;
            }

            // A software VBuffer keeps the pixels in the back buffer's own format; the color
            // conversion below turns them into the B, G, R blocks registerRgbImage() takes.
            tRasterInfo raster;
            memset(&raster, 0, sizeof(raster));
            raster.width     = width;
            raster.height    = height;
            raster.rowSize   = rowBytes;
            raster.rowWidth  = width;
            raster.size      = rowBytes * height;
            raster.colorInfo = back.colorInfo;

            tVBuffer* pCopy = stdDisplay_VBufferNew(&raster, /*bUseVSurface=*/0, /*bUseVideoMemory=*/0);
            if ( !pCopy )
            {
                stdDisplay_UnlockBackBuffer();
                return;
            }

            for ( uint32_t y = 0; y < height; ++y )
            {
                memcpy(&pCopy->pPixels[pCopy->rasterInfo.rowSize * y],
                    (const uint8_t*)pLocked + rowPitch * y, rowBytes);
            }

            stdDisplay_UnlockBackBuffer();

            // The converter frees pCopy when it returns a new buffer.
            tVBuffer* pRgb = stdDisplay_VBufferConvertColorFormat(&stdColor_cfRGB888, pCopy,
                /*bColorKey=*/0, NULL);
            if ( !pRgb )
            {
                stdDisplay_VBufferFree(pCopy);
                return;
            }

            image = painter.registerRgbImage(pRgb->pPixels, width, height);
            stdDisplay_VBufferFree(pRgb);
        }

        // Cover-scales an image to the canvas: it keeps its aspect and fills the canvas, cropping
        // the overhang. A zero source size stretches to the whole canvas.
        static babylon::ui::Rect CoverRect(const babylon::ui::Size& canvas,
            const JonesUiModalBackground& background)
        {
            babylon::ui::Rect rect;
            if ( !(background.width > 0.0f) || !(background.height > 0.0f) )
            {
                rect.width  = canvas.width;
                rect.height = canvas.height;
                return rect;
            }

            // Cover: the larger of the two scale factors, so neither canvas axis is left empty;
            // the overhang is centered and cropped by the painter's clip.
            const float scaleX = canvas.width / background.width;
            const float scaleY = canvas.height / background.height;
            const float scale  = scaleX > scaleY ? scaleX : scaleY;
            rect.width  = background.width * scale;
            rect.height = background.height * scale;
            rect.x = (canvas.width - rect.width) * 0.5f;
            rect.y = (canvas.height - rect.height) * 0.5f;
            return rect;
        }

        void Draw(babylon::ui::Ui& ui, JonesUiPainter& painter,
            const JonesUiModalBackground& background) const
        {
            const babylon::ui::Size canvas = ui.canvasSize();
            babylon::ui::Rect full;
            full.width  = canvas.width;
            full.height = canvas.height;

            // Without a captured frame or a background of its own, clear so the previous modal
            // frame does not show through.
            if ( image == 0 && background.image == 0 && !background.draw )
            {
                stdDisplay_BackBufferFill(0, NULL);
            }

            // Start the modal frame with a fresh depth buffer, as the load screen and the game
            // frame do. Without this, a panel drawn by the previous screen keeps its nearer depth
            // and the background redrawn at full screen fails the depth test where the panel was.
            std3D_ClearZBuffer();

            // The scene begin returns 0 on success, like the painter test's other frame calls.
            if ( std3D_StartScene() != 0 )
            {
                return;
            }

            if ( background.draw )
            {
                // The game's own background (the main menu's wallpaper) is drawn at full brightness
                // before the screen; the panel is opaque enough to keep the text readable.
                background.draw();
            }
            else if ( background.image != 0 )
            {
                // The menu background is drawn at full brightness; the panel is opaque enough to
                // keep the text readable.
                painter.drawImage(background.image, CoverRect(canvas, background));
            }
            else
            {
                if ( image != 0 )
                {
                    painter.drawImage(image, full);
                }

                painter.fillRect(full, ui.skin().dim);
            }

            if ( background.overlay )
            {
                background.overlay();
            }

            ui.draw();

            std3D_EndScene();
            stdDisplay_Update();
        }

        void Release(JonesUiPainter& painter)
        {
            if ( image != 0 )
            {
                painter.unregisterImage(image);
                image = 0;
            }
        }
    };

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
}

static int JonesUiModal_RunInternal(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    const babylon::ui::Screen& screen, const JonesUiModalResultMapper& mapResult,
    const JonesUiModalEventHandler& handler, const JonesUiModalBackground& background,
    const JonesUiModalResizeHandler& resizeHandler)
{
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
    // caller's scene before the capture keeps its pass from being dropped by the frame flush and
    // lets the loop begin scenes of its own; the caller gets a fresh scene before it continues.
    const bool bHadScene = std3D_IsInScene();
    if ( bHadScene )
    {
        std3D_EndScene();
    }

    // Only the game's own painter can hold a frozen frame image; a Null painter has no display
    // path behind it. The background belongs to the ui session, not to one loop: the first modal
    // loop while a context is held captures the frozen frame or adopts the opener's background,
    // and every later loop reuses it. A direct call without a context, such as a test, captures
    // its own frame.
    JonesUiPainter* pFramePainter = dynamic_cast<JonesUiPainter*>(pPainter);
    JonesUiContext* pUiContext    = JonesUiContext_GetActive();
    FrozenFrame frame;
    const JonesUiModalBackground* pBackground = &background;
    JonesUiPainter* pDrawPainter = pFramePainter;
    bool bCapturedNewFrame = false;
    if ( pUiContext )
    {
        if ( !pUiContext->bModalBackgroundStarted )
        {
            pUiContext->bModalBackgroundStarted = true;
            pUiContext->modalBackground         = background;
            pUiContext->pModalFramePainter      = pFramePainter;
            if ( pFramePainter && stdDisplay_IsOpen() && background.image == 0 && !background.draw )
            {
                frame.Capture(*pFramePainter);
                pUiContext->modalFrozenImage = frame.image;
                bCapturedNewFrame = true;
            }
        }

        frame.image  = pUiContext->modalFrozenImage;
        pBackground  = &pUiContext->modalBackground;
        pDrawPainter = pUiContext->pModalFramePainter;
    }
    else if ( pFramePainter && stdDisplay_IsOpen() && background.image == 0 && !background.draw )
    {
        frame.Capture(*pFramePainter);
        bCapturedNewFrame = true;
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
    else if ( frame.image != 0 )
    {
        pBackgroundState = "frozen";
    }

    STDLOG_DEBUG("JonesUiModal_Run: context=%p depth=%d background=%s capture=%d\n",
        (void*)pUiContext, g_modalDepth, pBackgroundState, bCapturedNewFrame ? 1 : 0);

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

        if ( pDrawPainter && stdDisplay_IsOpen() )
        {
            frame.Draw(ui, *pDrawPainter, *pBackground);
        }
        else if ( numEvents == 0 )
        {
            // Nothing to do and nowhere to draw: yield like the blocking Win32 dialog did.
            stdPlatform_Sleep(1);
        }
    }

    // A session's frozen frame lives until the context is released; a direct call frees its own.
    if ( !pUiContext && pFramePainter )
    {
        frame.Release(*pFramePainter);
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
