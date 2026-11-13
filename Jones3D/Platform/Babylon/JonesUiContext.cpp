// Added: the Babylon column's shared babylon::ui context; see JonesUiContext.h.
#include <Jones3D/Platform/Babylon/JonesUiContext.h>

#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Main/jonesString.h>
#include <Jones3D/Platform/Babylon/JonesUiSkin.h>

#include <babylon/files/FileSystem.h>
#include <babylon/ui/Skin.h>

#include <std/Babylon/stdBabylon.h>

#include <std/General/std.h>
#include <std/General/stdDisplay.h>

#include <cstring>
#include <string>

namespace
{
    // The default HUD menu font, the same file JonesHud_Startup loads. A screen may open before
    // the HUD exists, so the context loads its own instance.
    constexpr const char* kJonesUiFontFile = "mat\\jonesComic Sans MS14.gcf";
    constexpr float kJonesUiFontSize = 16.0f;

    // Added: the default skin's Cinzel title and Jost rows (ADR-0028), at the same sizes as the
    // end screens and the Options screen.
    constexpr const char* kJonesUiTitleFontFile = "fonts/Cinzel[wght].ttf";
    constexpr float kJonesUiTitleFontSize = 22.0f;
    constexpr const char* kJonesUiRowFontFile = "fonts/Jost[wght].ttf";
    constexpr float kJonesUiRowFontSize = 16.0f;

    // Added: the prompt bar's Jost and the five Xelu icon sets (ADR-0028, ADR-0031).
    constexpr const char* kJonesUiPromptFontFile = "fonts/Jost[wght].ttf";
    constexpr float kJonesUiPromptFontSize = 14.0f;

    // Loads one icon file; a missing or broken file returns 0, which draws the label alone.
    babylon::ui::ImageId LoadPromptIcon(JonesUiPainter& painter,
        babylon::files::FileSystem& files, const char* pRelativePath)
    {
        return painter.registerPngImageFile(files, pRelativePath);
    }

    // Loads the icons of one device set. A pad set has one D-pad icon for both Change
    // directions, so changeRight stays 0. The sets are loaded once with the context, so the
    // prompt bar never touches the file system while a screen runs.
    JonesUiPromptIcons LoadPromptIcons(JonesUiPainter& painter,
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
    JonesUiPromptIcons LoadKeyboardPromptIcons(JonesUiPainter& painter,
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

JonesUiContext::~JonesUiContext()
{
    // Release the frozen frame the modal loop captured for this ui session before the painter goes.
    if ( modalFrozenImage != 0 && pModalFramePainter )
    {
        pModalFramePainter->unregisterImage(modalFrozenImage);
        modalFrozenImage = 0;
    }

    // The Ui does not use the painter when it is destroyed; the painter's destructor frees
    // images, not fonts, so the font can go first.
    pUi.reset();
    if ( pFont )
    {
        rdFont_Free(pFont);
        pFont = nullptr;
    }
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
    if ( !pContext || !pContext->pCvars )
    {
        return nullptr;
    }

    std::unique_ptr<JonesUiContext> pState(new JonesUiContext());

    pState->pFont = rdFont_Load(kJonesUiFontFile);
    if ( !pState->pFont )
    {
        STDLOG_WARNING("JonesUiContext: could not load the menu font '%s'.\n", kJonesUiFontFile);
        return nullptr;
    }

    pState->font = pState->painter.registerFont(pState->pFont, kJonesUiFontSize);
    if ( pState->font == 0 )
    {
        return nullptr;
    }

    // Added: the default skin's fonts; a missing file keeps the shared rdFont, so the default skin
    // still starts and only looks like the old HUD skin.
    babylon::ui::FontId titleFont = pState->font;
    babylon::ui::FontId rowFont   = pState->font;

    // Added: the prompt bar's font and icons. Without a file system the labels still draw in the
    // menu font, so a missing asset never blocks a screen.
    if ( pContext->pFileSystem )
    {
        titleFont = pState->painter.registerTtfFontFile(*pContext->pFileSystem,
            kJonesUiTitleFontFile, kJonesUiTitleFontSize);
        if ( titleFont == 0 )
        {
            STDLOG_WARNING("JonesUiContext: '%s' unavailable, the default skin uses the rdFont.\n",
                kJonesUiTitleFontFile);
            titleFont = pState->font;
        }

        rowFont = pState->painter.registerTtfFontFile(*pContext->pFileSystem,
            kJonesUiRowFontFile, kJonesUiRowFontSize);
        if ( rowFont == 0 )
        {
            STDLOG_WARNING("JonesUiContext: '%s' unavailable, the default skin uses the rdFont.\n",
                kJonesUiRowFontFile);
            rowFont = pState->font;
        }

        pState->promptFont = pState->painter.registerTtfFontFile(*pContext->pFileSystem,
            kJonesUiPromptFontFile, kJonesUiPromptFontSize);
        if ( pState->promptFont == 0 )
        {
            STDLOG_WARNING("JonesUiContext: '%s' unavailable, prompts use the menu font.\n",
                kJonesUiPromptFontFile);
        }

        pState->prompts.keyboard = LoadKeyboardPromptIcons(pState->painter,
            *pContext->pFileSystem);
        pState->prompts.xbox = LoadPromptIcons(pState->painter, *pContext->pFileSystem,
            "xbox", "XboxSeriesX_A.png", "XboxSeriesX_B.png", "XboxSeriesX_LB.png",
            "XboxSeriesX_RB.png", "XboxSeriesX_Dpad.png");
        pState->prompts.playStation = LoadPromptIcons(pState->painter,
            *pContext->pFileSystem, "ps5", "PS5_Cross.png", "PS5_Circle.png",
            "PS5_L1.png", "PS5_R1.png", "PS5_Dpad.png");
        pState->prompts.nintendo = LoadPromptIcons(pState->painter, *pContext->pFileSystem,
            "switch", "Switch_B.png", "Switch_A.png", "Switch_LB.png", "Switch_RB.png",
            "Switch_Dpad.png");
        pState->prompts.steamDeck = LoadPromptIcons(pState->painter, *pContext->pFileSystem,
            "steamdeck", "SteamDeck_A.png", "SteamDeck_B.png", "SteamDeck_L1.png",
            "SteamDeck_R1.png", "SteamDeck_Dpad.png");
    }

    pState->pUi = std::make_unique<babylon::ui::Ui>(pState->painter, *pContext->pCvars);
    if ( !pState->pUi->setSkin(JonesUiSkin_Create(titleFont, rowFont, &pState->painter,
            pState->promptFont)) )
    {
        return nullptr;
    }

    uint32_t width  = 0;
    uint32_t height = 0;
    stdDisplay_GetBackBufferSize(&width, &height);
    if ( !width || !height || !pState->pUi->setViewport((int)width, (int)height) )
    {
        return nullptr;
    }

    pState->refCount = 1;
    g_pContext = pState.release();
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
