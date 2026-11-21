// Added: the Babylon column's shared babylon::ui context; see JonesUiContext.h.
#include <Jones3D/Platform/Babylon/JonesUiContext.h>

#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Main/jonesString.h>
#include <Jones3D/Platform/Babylon/JonesUiAssets.h>
#include <Jones3D/Platform/Babylon/JonesUiSkin.h>

#include <babylon/files/FileSystem.h>
#include <babylon/platform/Window.h>
#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/ui/Skin.h>
#include <babylon/uirender/RenderPainter.h>

#include <std/Babylon/stdBabylon.h>
#include <std/Babylon/stdRenderBabylon.h>

#include <std/General/std.h>
#include <std/General/stdDisplay.h>

#include <cstring>
#include <string>

namespace
{
    constexpr const char* kJonesUiTitleFontFile = "fonts/Cinzel[wght].ttf";
    constexpr float kJonesUiTitleFontSize = 22.0f;
    constexpr const char* kJonesUiRowFontFile = "fonts/Jost[wght].ttf";
    constexpr float kJonesUiRowFontSize = 16.0f;
    constexpr const char* kJonesUiPromptFontFile = "fonts/Jost[wght].ttf";
    constexpr float kJonesUiPromptFontSize = 14.0f;

    // The canvas height the ui module uses.
    constexpr float kJonesUiCanvasHeight = 480.0f;

    // Loads one icon file; a missing or broken file returns 0, which draws the label alone.
    babylon::ui::ImageId LoadPromptIcon(babylon::uirender::RenderPainter& painter,
        babylon::files::FileSystem& files, const char* pRelativePath)
    {
        return JonesUiAssets_LoadPng(painter, files, pRelativePath);
    }

    // Loads the icons of one device set. A pad set has one D-pad icon for both Change
    // directions, so changeRight stays 0.
    JonesUiPromptIcons LoadPromptIcons(babylon::uirender::RenderPainter& painter,
        babylon::files::FileSystem& files, const char* pSet,
        const char* pAccept, const char* pBack, const char* pTabsLeft,
        const char* pTabsRight, const char* pChange)
    {
        JonesUiPromptIcons icons;
        const std::string prefix = std::string("prompts/") + pSet + "/";
        icons.accept = LoadPromptIcon(painter, files, (prefix + pAccept).c_str());
        icons.back = LoadPromptIcon(painter, files, (prefix + pBack).c_str());
        icons.tabsLeft = LoadPromptIcon(painter, files, (prefix + pTabsLeft).c_str());
        icons.tabsRight = LoadPromptIcon(painter, files, (prefix + pTabsRight).c_str());
        icons.changeLeft = LoadPromptIcon(painter, files, (prefix + pChange).c_str());
        return icons;
    }

    // The keyboard set has a separate key for each Change direction; the pad sets use one D-pad
    // icon for both.
    JonesUiPromptIcons LoadKeyboardPromptIcons(babylon::uirender::RenderPainter& painter,
        babylon::files::FileSystem& files)
    {
        JonesUiPromptIcons icons;
        icons.accept = LoadPromptIcon(painter, files, "prompts/keyboard/Enter_Key_Dark.png");
        icons.back = LoadPromptIcon(painter, files, "prompts/keyboard/Esc_Key_Dark.png");
        icons.tabsLeft = LoadPromptIcon(painter, files, "prompts/keyboard/Q_Key_Dark.png");
        icons.tabsRight = LoadPromptIcon(painter, files, "prompts/keyboard/E_Key_Dark.png");
        icons.changeLeft = LoadPromptIcon(painter, files,
            "prompts/keyboard/Arrow_Left_Key_Dark.png");
        icons.changeRight = LoadPromptIcon(painter, files,
            "prompts/keyboard/Arrow_Right_Key_Dark.png");
        return icons;
    }

    // Cover-scales an image to the canvas: it keeps its aspect and fills the canvas, cropping the
    // overhang. A zero source size stretches to the whole canvas.
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

    // The present overlay: one pass over the presented game frame, at the target's pixel size.
    bool DrawOverlay(void* pUser, babylon::render::CommandList& list,
        const StdRenderBabylonOverlayTarget& target)
    {
        JonesUiContext* pContext = static_cast<JonesUiContext*>(pUser);
        if ( !pContext || !pContext->pUi || pContext->pUi->screenCount() == 0 )
        {
            return false;
        }
        if ( target.width == 0 || target.height == 0 )
        {
            return false;
        }
        if ( !pContext->pUi->setViewport((int)target.width, (int)target.height) )
        {
            return false;
        }

        babylon::uirender::FrameTarget frameTarget;
        frameTarget.texture = target.texture;
        frameTarget.format  = target.format;
        frameTarget.load    = babylon::render::LoadOp::Load;
        if ( !pContext->painter.begin(list, target.width, target.height, frameTarget) )
        {
            return false;
        }

        const babylon::ui::Size canvas = pContext->pUi->canvasSize();
        const babylon::ui::Rect full{ 0.0f, 0.0f, canvas.width, canvas.height };
        const JonesUiModalBackground& background = pContext->modalBackground;

        if ( background.image != 0 )
        {
            pContext->painter.drawImage(background.image, CoverRect(canvas, background));
        }
        else if ( !background.draw )
        {
            pContext->painter.fillRect(full, pContext->pUi->skin().dim);
        }

        if ( background.overlay )
        {
            background.overlay();
        }

        pContext->pUi->draw();
        return pContext->painter.end();
    }

    JonesUiContext* g_pContext = nullptr;

    // Removes the Win32 dialog mnemonics the game's string table stores: a lone '&' marks the
    // access key and is dropped, while "&&" stands for a literal '&'.
    std::string StripMnemonics(const char* pText)
    {
        std::string text;
        if ( !pText )
        {
            return text;
        }

        text.reserve(std::strlen(pText));
        for ( const char* pCur = pText; *pCur; ++pCur )
        {
            if ( *pCur != '&' )
            {
                text.push_back(*pCur);
            }
            else if ( pCur[1] == '&' )
            {
                text.push_back('&');
                ++pCur;
            }
        }

        return text;
    }
}

JonesUiContext::JonesUiContext(babylon::render::Device& device)
    : painter(device)
{
}

JonesUiContext::~JonesUiContext()
{
    // The overlay borrows this context; clear it before the painter and Ui go.
    stdRenderBabylon_SetPresentOverlay(nullptr, nullptr);
    pUi.reset();
}

JonesUiContext* JonesUiContext_Acquire()
{
    if ( g_pContext )
    {
        ++g_pContext->refCount;
        STDLOG_DEBUG("JonesUiContext_Acquire: context=%p refCount=%d (shared)\n",
            (void*)g_pContext, g_pContext->refCount);
        return g_pContext;
    }

    if ( !stdDisplay_IsOpen() )
    {
        return nullptr;
    }

    StdBabylonContext* pContext = stdBabylon_GetContext();
    if ( !pContext || !pContext->pCvars || !pContext->pWindow || !pContext->pRenderDevice
        || !pContext->pFileSystem )
    {
        return nullptr;
    }

    const babylon::platform::Size windowSize = pContext->pWindow->size();
    if ( windowSize.width <= 0 || windowSize.height <= 0 )
    {
        return nullptr;
    }

    std::unique_ptr<JonesUiContext> pState(new JonesUiContext(*pContext->pRenderDevice));
    static_cast<void>(pState->painter.setPixelScale(
        (float)windowSize.height / kJonesUiCanvasHeight));

    // The packaged Cinzel and Jost are required; the missing-TTF rdFont fallback is gone
    // (ADR-0035). If one file is missing, the other font keeps the screen readable.
    babylon::ui::FontId titleFont = JonesUiAssets_LoadFont(pState->painter,
        *pContext->pFileSystem, kJonesUiTitleFontFile, kJonesUiTitleFontSize);
    babylon::ui::FontId rowFont = JonesUiAssets_LoadFont(pState->painter,
        *pContext->pFileSystem, kJonesUiRowFontFile, kJonesUiRowFontSize);
    if ( titleFont == 0 )
    {
        titleFont = JonesUiAssets_LoadFont(pState->painter, *pContext->pFileSystem,
            kJonesUiRowFontFile, kJonesUiTitleFontSize);
    }
    if ( rowFont == 0 )
    {
        rowFont = JonesUiAssets_LoadFont(pState->painter, *pContext->pFileSystem,
            kJonesUiTitleFontFile, kJonesUiRowFontSize);
    }
    if ( titleFont == 0 || rowFont == 0 )
    {
        STDLOG_WARNING("JonesUiContext: the menu fonts are unavailable.\n");
        return nullptr;
    }

    pState->titleFont = titleFont;
    pState->rowFont   = rowFont;
    pState->promptFont = JonesUiAssets_LoadFont(pState->painter, *pContext->pFileSystem,
        kJonesUiPromptFontFile, kJonesUiPromptFontSize);
    if ( pState->promptFont == 0 )
    {
        pState->promptFont = pState->rowFont;
    }

    pState->prompts.keyboard = LoadKeyboardPromptIcons(pState->painter,
        *pContext->pFileSystem);
    pState->prompts.xbox = LoadPromptIcons(pState->painter, *pContext->pFileSystem,
        "xbox", "XboxSeriesX_A.png", "XboxSeriesX_B.png", "XboxSeriesX_LB.png",
        "XboxSeriesX_RB.png", "XboxSeriesX_Dpad.png");
    pState->prompts.playStation = LoadPromptIcons(pState->painter, *pContext->pFileSystem,
        "ps5", "PS5_Cross.png", "PS5_Circle.png", "PS5_L1.png", "PS5_R1.png",
        "PS5_Dpad.png");
    pState->prompts.nintendo = LoadPromptIcons(pState->painter, *pContext->pFileSystem,
        "switch", "Switch_B.png", "Switch_A.png", "Switch_LB.png", "Switch_RB.png",
        "Switch_Dpad.png");
    pState->prompts.steamDeck = LoadPromptIcons(pState->painter, *pContext->pFileSystem,
        "steamdeck", "SteamDeck_A.png", "SteamDeck_B.png", "SteamDeck_L1.png",
        "SteamDeck_R1.png", "SteamDeck_Dpad.png");

    pState->pUi = std::make_unique<babylon::ui::Ui>(pState->painter, *pContext->pCvars);
    if ( !pState->pUi->setSkin(JonesUiSkin_Create(pState->titleFont, pState->rowFont,
            &pState->painter, pState->promptFont)) )
    {
        return nullptr;
    }

    if ( !pState->pUi->setViewport(windowSize.width, windowSize.height) )
    {
        return nullptr;
    }

    pState->refCount = 1;
    g_pContext = pState.release();
    stdRenderBabylon_SetPresentOverlay(&DrawOverlay, g_pContext);
    STDLOG_DEBUG("JonesUiContext_Acquire: context=%p refCount=1 (created)\n",
        (void*)g_pContext);
    return g_pContext;
}

JonesUiContext* JonesUiContext_GetActive(void)
{
    return g_pContext;
}

#ifdef J3D_TEST
void JonesUiContext_SetActiveForTest(JonesUiContext* pContext)
{
    g_pContext = pContext;
}

bool JonesUiContext_DrawOverlayForTest(JonesUiContext& context,
    babylon::render::CommandList& list, std::uint32_t width, std::uint32_t height,
    babylon::render::TextureHandle target)
{
    const StdRenderBabylonOverlayTarget overlayTarget{ target,
        babylon::render::TextureFormat::Bgra8, width, height };
    return DrawOverlay(&context, list, overlayTarget);
}
#endif

void JonesUiContext_Release(JonesUiContext* pContext)
{
    if ( !pContext )
    {
        return;
    }

    if ( pContext->refCount > 0 )
    {
        --pContext->refCount;
    }

    STDLOG_DEBUG("JonesUiContext_Release: context=%p refCount=%d\n",
        (void*)pContext, pContext->refCount);

    if ( pContext->refCount == 0 )
    {
        if ( g_pContext == pContext )
        {
            g_pContext = nullptr;
        }

        delete pContext;
    }
}

extern "C" void JonesUiContext_Shutdown(void)
{
    delete g_pContext;
    g_pContext = nullptr;
}

// Added: the Babylon column's jonesConfig_ReleaseDisplayResources: free the shared ui context,
// which owns the painter and its textures, before the display shuts down.
extern "C" void J3DAPI jonesConfig_ReleaseDisplayResources(void)
{
    JonesUiContext_Shutdown();
}

std::string JonesUiContext_ResolveString(const char* pValue)
{
    if ( !pValue )
    {
        return std::string();
    }

    if ( std::strncmp(pValue, "JONES_STR_", 10) == 0 )
    {
        const char* pResolved = jonesString_GetString(pValue);
        if ( pResolved && pResolved[0] )
        {
            return StripMnemonics(pResolved);
        }
    }

    return StripMnemonics(pValue);
}
