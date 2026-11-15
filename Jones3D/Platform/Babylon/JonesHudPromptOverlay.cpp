// Added: the interaction prompt's native-resolution present overlay; see JonesHudPromptOverlay.h.
// The HUD decides once per frame what a press would do; this module only maps that to the active
// scheme's device binding and draws the Xelu icon and verb at the window's pixel scale (ADR-0035).
#include <Jones3D/Platform/Babylon/JonesHudPromptOverlay.h>

#include <Jones3D/Display/JonesHudPrompt.h>
#include <Jones3D/Platform/Babylon/JonesControlSchemes.h>
#include <Jones3D/Platform/Babylon/JonesUiAssets.h>
#include <Jones3D/Platform/Babylon/JonesUiButtonLayout.h>

#include <std/Babylon/stdBabylon.h>
#include <std/Babylon/stdBabylonEvents.h>
#include <std/Babylon/stdRenderBabylon.h>

#include <babylon/input/InputState.h>
#include <babylon/platform/Keys.h>
#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/uirender/RenderPainter.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace
{
    constexpr float kJonesHudPromptCanvasHeight = 480.0f;
    constexpr float kJonesHudPromptFontHeight   = 14.0f;
    constexpr float kJonesHudPromptIconSize     = 20.0f;
    constexpr float kJonesHudPromptGap          = 6.0f;
    constexpr float kJonesHudPromptPadding      = 8.0f;
    constexpr float kJonesHudPromptLineHeight   = 20.0f;
    constexpr float kJonesHudPromptBottomMargin = 20.0f;

    const babylon::ui::Color kJonesHudPromptBackdrop{ 0.0f, 0.0f, 0.0f, 0.45f };
    const babylon::ui::Color kJonesHudPromptText{ 0.95f, 0.92f, 0.85f, 1.0f };

    const char* JonesHudPrompt_Verb(JonesHudInteractionPrompt prompt)
    {
        switch ( prompt )
        {
            case JONESHUD_INTERACTIONPROMPT_PICKUP: return "Pick up";
            case JONESHUD_INTERACTIONPROMPT_USE:    return "Use";
            default:                                return nullptr;
        }
    }

    const char* JonesHudPrompt_ControllerSet(const babylon::input::LastDevice& device)
    {
        switch ( device.gamepadType )
        {
            case babylon::platform::GamepadType::PlayStation: return "ps5";
            case babylon::platform::GamepadType::Nintendo:    return "switch";
            case babylon::platform::GamepadType::SteamDeck:   return "steamdeck";
            default:                                          return "xbox";
        }
    }

    struct JonesHudPromptResources
    {
        babylon::render::Device* pDevice = nullptr;
        std::unique_ptr<babylon::uirender::RenderPainter> painter;
        babylon::ui::FontId font = 0;
        std::unordered_map<std::string, babylon::ui::ImageId> icons;

        void Reset()
        {
            icons.clear();
            font = 0;
            painter.reset();
            pDevice = nullptr;
        }
    };

    JonesHudPromptResources g_resources;

    // Prepares the painter and font for the target height. Recreates everything when the render
    // device changes, so a display restart never leaves a painter on a dead device.
    bool JonesHudPrompt_EnsureResources(StdBabylonContext& context, std::uint32_t height)
    {
        if ( !context.pRenderDevice || !context.pFileSystem || height == 0 )
        {
            return false;
        }

        if ( g_resources.pDevice != context.pRenderDevice )
        {
            g_resources.Reset();
            g_resources.pDevice = context.pRenderDevice;
            g_resources.painter = std::make_unique<babylon::uirender::RenderPainter>(
                *context.pRenderDevice);
        }

        if ( !g_resources.painter )
        {
            return false;
        }

        const float pixelScale = (float)height / kJonesHudPromptCanvasHeight;
        if ( !g_resources.painter->setPixelScale(pixelScale) )
        {
            return false;
        }

        if ( g_resources.font == 0 )
        {
            g_resources.font = JonesUiAssets_LoadFont(*g_resources.painter,
                *context.pFileSystem, "fonts/Jost[wght].ttf", kJonesHudPromptFontHeight);
        }

        return g_resources.font != 0;
    }

    babylon::ui::ImageId JonesHudPrompt_LoadIcon(StdBabylonContext& context,
        const std::string& path)
    {
        if ( path.empty() || !g_resources.painter || !context.pFileSystem )
        {
            return 0;
        }

        const auto found = g_resources.icons.find(path);
        if ( found != g_resources.icons.end() )
        {
            return found->second;
        }

        const babylon::ui::ImageId icon = JonesUiAssets_LoadPng(*g_resources.painter,
            *context.pFileSystem, path);
        g_resources.icons.emplace(path, icon);
        return icon;
    }

    babylon::ui::ImageId JonesHudPrompt_BindingIcon(StdBabylonContext& context)
    {
        const JonesControlsScheme* pScheme = JonesControlSchemes_GetActiveScheme();
        if ( !pScheme )
        {
            return 0;
        }

        const babylon::input::LastDevice device = stdBabylonEvents_InputState().lastDevice();
        const JonesUiButtonLayoutDevice uiDevice =
            device.device == babylon::input::DeviceClass::KeyboardMouse
            ? JonesUiButtonLayoutDevice::KeyboardMouse
            : JonesUiButtonLayoutDevice::Controller;

        std::size_t controlId = 0;
        if ( !JonesUiButtonLayout_GetDeviceBinding(*pScheme, JONESCONTROL_ACTION_ACTIVATE,
                uiDevice, controlId) )
        {
            return 0;
        }

        const char* pSet = device.device == babylon::input::DeviceClass::KeyboardMouse
            ? nullptr
            : JonesHudPrompt_ControllerSet(device);
        return JonesHudPrompt_LoadIcon(context,
            JonesUiButtonLayout_GetPromptIconPath(controlId, uiDevice, pSet));
    }

    bool JonesHudPrompt_Draw(void* pUser, babylon::render::CommandList& list,
        const StdRenderBabylonOverlayTarget& target)
    {
        (void)pUser;

        const JonesHudInteractionPrompt prompt = JonesHudPrompt_GetInteraction();
        const char* pVerb = JonesHudPrompt_Verb(prompt);
        if ( !pVerb || target.width == 0 || target.height == 0 )
        {
            return false;
        }

        // A menu owns the present overlay while it is open; the prompt stays under it and hidden.
        if ( stdRenderBabylon_HasPresentOverlay() )
        {
            return false;
        }

        StdBabylonContext* pContext = stdBabylon_GetContext();
        if ( !pContext || !JonesHudPrompt_EnsureResources(*pContext, target.height) )
        {
            return false;
        }

        babylon::uirender::RenderPainter& painter = *g_resources.painter;
        const babylon::ui::ImageId icon = JonesHudPrompt_BindingIcon(*pContext);

        const float canvasHeight = kJonesHudPromptCanvasHeight;
        const float canvasWidth  = canvasHeight * (float)target.width / (float)target.height;
        const float textWidth    = painter.measureText(g_resources.font, pVerb).width;
        const float iconWidth    = icon != 0 ? kJonesHudPromptIconSize + kJonesHudPromptGap : 0.0f;
        const float contentWidth = iconWidth + textWidth;
        const float width        = contentWidth + kJonesHudPromptPadding * 2.0f;
        const float x            = (canvasWidth - width) * 0.5f;
        const float y            = canvasHeight - kJonesHudPromptBottomMargin - kJonesHudPromptLineHeight;

        babylon::uirender::FrameTarget frameTarget;
        frameTarget.texture = target.texture;
        frameTarget.format  = target.format;
        frameTarget.load    = babylon::render::LoadOp::Load;
        if ( !painter.begin(list, target.width, target.height, frameTarget) )
        {
            return false;
        }

        painter.fillRect(babylon::ui::Rect{ x, y, width, kJonesHudPromptLineHeight },
            kJonesHudPromptBackdrop);

        float contentX = x + kJonesHudPromptPadding;
        if ( icon != 0 )
        {
            painter.drawImage(icon, babylon::ui::Rect{ contentX,
                y + (kJonesHudPromptLineHeight - kJonesHudPromptIconSize) * 0.5f,
                kJonesHudPromptIconSize, kJonesHudPromptIconSize });
            contentX += iconWidth;
        }

        painter.drawText(g_resources.font, pVerb,
            babylon::ui::Rect{ contentX, y, textWidth, kJonesHudPromptLineHeight },
            babylon::ui::TextAlign::Left, kJonesHudPromptText);

        return painter.end();
    }
}

void JonesHudPromptOverlay_EnsureInstalled(void)
{
    if ( !stdRenderBabylon_IsOpen() || stdRenderBabylon_HasHudOverlay() )
    {
        return;
    }

    stdRenderBabylon_SetHudOverlay(&JonesHudPrompt_Draw, nullptr);
}

void JonesHudPromptOverlay_Shutdown(void)
{
    stdRenderBabylon_SetHudOverlay(nullptr, nullptr);
    g_resources.Reset();
}
