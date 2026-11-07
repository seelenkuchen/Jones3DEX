// Added: the Babylon column's modal screen loop; see JonesUiModal.h.
#include <Jones3D/Platform/Babylon/JonesUiModal.h>

#include <Jones3D/Platform/Babylon/JonesUiPainter.h>

#include <babylon/platform/Event.h>
#include <babylon/platform/Window.h>
#include <babylon/ui/Skin.h>
#include <babylon/ui/UiEvent.h>

#include <std/Babylon/std3DBabylon.h>
#include <std/Babylon/stdBabylon.h>
#include <std/Babylon/stdBabylonEvents.h>

#include <std/General/std.h>
#include <std/General/std3D.h>
#include <std/General/stdColor.h>
#include <std/General/stdDisplay.h>
#include <std/General/stdPlatform.h>

#include <cstdint>
#include <cstring>
#include <optional>
#include <variant>

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

            // Without a captured frame or background image, clear so the previous modal frame does
            // not show through.
            if ( image == 0 && background.image == 0 )
            {
                stdDisplay_BackBufferFill(0, NULL);
            }

            // The scene begin returns 0 on success, like the painter test's other frame calls.
            if ( std3D_StartScene() != 0 )
            {
                return;
            }

            if ( background.image != 0 )
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
    const JonesUiModalEventHandler& handler, const JonesUiModalBackground& background)
{
    const babylon::ui::ScreenId screenId = ui.push(screen);
    if ( screenId == babylon::ui::kInvalidScreen )
    {
        return mapResult(screen.backResult);
    }

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
    // path behind it.
    JonesUiPainter* pFramePainter = dynamic_cast<JonesUiPainter*>(pPainter);
    FrozenFrame frame;
    if ( pFramePainter && stdDisplay_IsOpen() && background.image == 0 )
    {
        frame.Capture(*pFramePainter);
    }

    int result = (int)screen.backResult;
    bool bClosed = false;
    uint32_t lastMsec = stdPlatform_GetTimeMsec();

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
            }

            ui.handleEvent(event);
        }

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
        }

        if ( bClosed )
        {
            break;
        }

        if ( pFramePainter && stdDisplay_IsOpen() )
        {
            frame.Draw(ui, *pFramePainter, background);
        }
        else if ( numEvents == 0 )
        {
            // Nothing to do and nowhere to draw: yield like the blocking Win32 dialog did.
            stdPlatform_Sleep(1);
        }
    }

    if ( pFramePainter )
    {
        frame.Release(*pFramePainter);
    }

    // Give the caller back the scene it was drawing into, so its remaining draws and its own
    // std3D_EndScene work as they did around a native dialog.
    if ( bHadScene )
    {
        static_cast<void>(std3D_StartScene());
    }

    return mapResult(result);
}

int JonesUiModal_Run(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    const babylon::ui::Screen& screen, const JonesUiModalResultMapper& mapResult,
    const JonesUiModalBackground& background)
{
    return JonesUiModal_RunInternal(ui, pPainter, screen, mapResult, JonesUiModalEventHandler(),
        background);
}

int JonesUiModal_RunHandled(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    const babylon::ui::Screen& screen, const JonesUiModalResultMapper& mapResult,
    const JonesUiModalEventHandler& handler, const JonesUiModalBackground& background)
{
    return JonesUiModal_RunInternal(ui, pPainter, screen, mapResult, handler, background);
}
