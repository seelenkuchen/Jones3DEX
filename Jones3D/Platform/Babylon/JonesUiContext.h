// Added: the Babylon column's one babylon::ui context (ADR-0026). Every screen wrapper shares
// the same painter, fonts and Ui, so a screen opened from another screen stacks on top of it and
// the same look is used everywhere. The painter records the menus through babylon::uirender on the
// window's present overlay, at native pixel size (ADR-0035). The outermost screen creates the
// context on Acquire and the last Release frees it; JonesUiContext_Shutdown frees it at the latest
// before std3D_Shutdown.
#ifndef JONES3D_PLATFORM_BABYLON_JONESUICONTEXT_H
#define JONES3D_PLATFORM_BABYLON_JONESUICONTEXT_H
#ifndef __cplusplus
#error "JonesUiContext.h is C++ only: it names babylon::ui and render types."
#endif

#include <Jones3D/Platform/Babylon/JonesUiModal.h>
#include <Jones3D/Platform/Babylon/JonesUiPrompts.h>

#include <babylon/render/Handle.h>
#include <babylon/ui/Ui.h>
#include <babylon/uirender/RenderPainter.h>

#include <cstdint>
#include <memory>
#include <string>

namespace babylon::render
{
    class CommandList;
    class Device;
}

// One reference-counted context for the column. painter draws every screen and owns its textures;
// titleFont, rowFont and promptFont are the three registered font ids, and pUi is the single Ui
// all screens share.
struct JonesUiContext
{
    explicit JonesUiContext(babylon::render::Device& device);

    babylon::uirender::RenderPainter painter;
    babylon::ui::FontId titleFont = 0;
    babylon::ui::FontId rowFont = 0;
    babylon::ui::FontId promptFont = 0;

    // Added: the five prompt icon sets, loaded once with the context. A missing file leaves its
    // id 0, and the bar draws that label alone.
    JonesUiPromptAssets prompts;

    std::unique_ptr<babylon::ui::Ui> pUi;
    int refCount = 0;

    // Added: the background shared by every modal screen opened while this context is held. The
    // first modal loop adopts the opener's background; later loops reuse it, so a screen opened
    // after another closes does not capture the closed screen.
    bool bModalBackgroundStarted = false;
    JonesUiModalBackground modalBackground;

    ~JonesUiContext();

    JonesUiContext(const JonesUiContext&) = delete;
    JonesUiContext& operator=(const JonesUiContext&) = delete;
};

// Returns the shared context, creating it on the first call, and increments its reference count.
// Returns nullptr when there is no open display, no window, render device, file system or cvar
// registry, when the fonts cannot be loaded, or when the Ui cannot start. A missing TTF is not
// replaced by the HUD's rdFont: the menus require the packaged Cinzel and Jost assets.
JonesUiContext* JonesUiContext_Acquire();

// Decrements the reference count and frees the context with the last reference. A null pointer is
// a no-op.
void JonesUiContext_Release(JonesUiContext* pContext);

// Frees the context regardless of its reference count. Call before std3D_Shutdown so the painter
// releases its textures while the device is still open. Safe when no context exists.
extern "C" void JonesUiContext_Shutdown(void);

// The context currently held by Acquire, or nullptr. The modal loop uses it to share one
// background across every screen opened while the context is held.
JonesUiContext* JonesUiContext_GetActive(void);

#ifdef J3D_TEST
// Test surface: installs a hand-built context as the active one. Tests must clear it with nullptr
// before the context is destroyed.
void JonesUiContext_SetActiveForTest(JonesUiContext* pContext);

// Test surface: records the context's present overlay pass into list at the given target, so a
// test can read back the background, dim and screen layering.
bool JonesUiContext_DrawOverlayForTest(JonesUiContext& context,
    babylon::render::CommandList& list, std::uint32_t width, std::uint32_t height,
    babylon::render::TextureHandle target);
#endif

// Resolves a JONES_STR_* key through the game's string table, like the Win32 dialogs did with
// jonesString_GetString. Any other text is used as given. The returned string owns its bytes
// because jonesString_GetString hands back one shared buffer.
std::string JonesUiContext_ResolveString(const char* pValue);

#endif // JONES3D_PLATFORM_BABYLON_JONESUICONTEXT_H
