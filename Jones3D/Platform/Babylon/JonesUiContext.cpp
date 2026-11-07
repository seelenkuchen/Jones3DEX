// Added: the Babylon column's shared babylon::ui context; see JonesUiContext.h.
#include <Jones3D/Platform/Babylon/JonesUiContext.h>

#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Main/jonesString.h>
#include <Jones3D/Platform/Babylon/JonesUiSkin.h>

#include <babylon/ui/Skin.h>

#include <std/Babylon/stdBabylon.h>

#include <std/General/std.h>
#include <std/General/stdDisplay.h>

#include <cstring>

namespace
{
    // The default HUD menu font, the same file JonesHud_Startup loads. A screen may open before
    // the HUD exists, so the context loads its own instance.
    constexpr const char* kJonesUiFontFile = "mat\\jonesComic Sans MS14.gcf";
    constexpr float kJonesUiFontSize = 16.0f;

    JonesUiContext* g_pContext = nullptr;
}

JonesUiContext::~JonesUiContext()
{
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

    pState->pUi = std::make_unique<babylon::ui::Ui>(pState->painter, *pContext->pCvars);
    if ( !pState->pUi->setSkin(JonesUiSkin_Create(pState->font, &pState->painter)) )
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
    return g_pContext;
}

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
            return std::string(pResolved);
        }
    }

    return std::string(pValue);
}
