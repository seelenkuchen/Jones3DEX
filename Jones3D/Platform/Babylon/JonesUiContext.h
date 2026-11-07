// Added: the Babylon column's one babylon::ui context (ADR-0026). Every screen wrapper shares
// the same painter, menu font and Ui, so a screen opened from another screen stacks on top of it
// and the same look is used everywhere. The outermost screen creates the context on Acquire and
// the last Release frees it; JonesUiContext_Shutdown frees it at the latest before std3D_Shutdown.
#ifndef JONES3D_PLATFORM_BABYLON_JONESUICONTEXT_H
#define JONES3D_PLATFORM_BABYLON_JONESUICONTEXT_H
#ifndef __cplusplus
#error "JonesUiContext.h is C++ only: it names babylon::ui types."
#endif

#include <Jones3D/Platform/Babylon/JonesUiModal.h>
#include <Jones3D/Platform/Babylon/JonesUiPainter.h>

#include <babylon/ui/Painter.h>
#include <babylon/ui/Ui.h>

#include <rdroid/Primitives/rdFont.h>

#include <memory>
#include <string>

// One reference-counted context for the column. painter draws every screen and is owned here;
// pFont is the loaded menu font, font its painter id, and pUi the single Ui all screens share.
struct JonesUiContext
{
    JonesUiPainter painter;
    rdFont* pFont = nullptr;
    babylon::ui::FontId font = 0;
    std::unique_ptr<babylon::ui::Ui> pUi;
    int refCount = 0;

    // Added: the background shared by every modal screen opened while this context is held. The
    // first modal loop captures the frozen frame or adopts the opener's background; later loops
    // reuse it, so a screen opened after another closes does not capture the closed screen.
    bool bModalBackgroundStarted = false;
    JonesUiModalBackground modalBackground;
    babylon::ui::ImageId modalFrozenImage = 0;
    JonesUiPainter* pModalFramePainter = nullptr;

    JonesUiContext() = default;
    ~JonesUiContext();

    JonesUiContext(const JonesUiContext&) = delete;
    JonesUiContext& operator=(const JonesUiContext&) = delete;
};

// Returns the shared context, creating it on the first call, and increments its reference count.
// Returns nullptr when there is no open display, no Babylon context or no cvar registry, or when
// the menu font cannot be loaded, exactly when the message screens of M7.10 could not start.
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
// Test surface: installs a hand-built context as the active one, without loading the menu font.
// Tests must clear it with nullptr before the context is destroyed.
void JonesUiContext_SetActiveForTest(JonesUiContext* pContext);
#endif

// Resolves a JONES_STR_* key through the game's string table, like the Win32 dialogs did with
// jonesString_GetString. Any other text is used as given. The returned string owns its bytes
// because jonesString_GetString hands back one shared buffer.
std::string JonesUiContext_ResolveString(const char* pValue);

#endif // JONES3D_PLATFORM_BABYLON_JONESUICONTEXT_H
