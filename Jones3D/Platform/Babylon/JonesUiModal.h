// Added: the Babylon column's modal screen loop (ADR-0026). The recovered jonesConfig_* wrappers
// are blocking: each runs a frame loop around one babylon::ui screen until it closes and returns
// the original dialog's result. This loop is that frame loop. The game simulation does not run
// while it does; it is entered from inside the caller, exactly where the Win32 modal dialog blocked.
//
// Each iteration pumps the column's one event pump, feeds the raw platform events to Ui::handleEvent,
// advances Ui with the elapsed platform time and drains the Ui events. The screen itself is drawn by
// the present overlay at the window's pixel size (ADR-0035), after the game frame (or a game-renderer
// background) is presented. A button action closes the screen with the action id as its result, and
// back closes it with Screen::backResult; the result is then mapped to the wrapper's own return
// value. A quit or game-window close also closes the top screen with its back result, but leaves the
// request flagged so the game quits as it would after the dialog.
//
// Nested screens work because the caller passes the same Ui: an inner loop pushes and pops only its
// own screen and the outer one stays below it, unchanged. The background belongs to the ui session
// held by JonesUiContext: the first loop in the session adopts the opener's background, and every
// later loop reuses it, so a screen never captures the screen before it.
#ifndef JONES3D_PLATFORM_BABYLON_JONESUIMODAL_H
#define JONES3D_PLATFORM_BABYLON_JONESUIMODAL_H
#ifndef __cplusplus
#error "JonesUiModal.h is C++ only: it names babylon::ui types."
#endif

#include <babylon/ui/Painter.h>
#include <babylon/ui/Screen.h>
#include <babylon/ui/Ui.h>
#include <babylon/ui/UiEvent.h>

#include <functional>

// What is drawn behind a screen. image is a registered RenderPainter image, cover-scaled to the
// canvas (it keeps its aspect and fills the whole canvas, cropping the overhang); the caller keeps
// it alive while the context is held and releases it. draw, when set, runs once each frame in the
// game renderer before the present, for a background such as the main menu's six-tile wallpaper; it
// takes precedence over image. width and height are the image's pixel size. With neither, the
// overlay dims the live game frame. overlay, when set, runs in the overlay after the background and
// its dim layer, before the screen, so a menu can add a shade.
struct JonesUiModalBackground
{
    babylon::ui::ImageId image = 0;
    float width = 0.0f;
    float height = 0.0f;
    std::function<void()> draw;
    std::function<void()> overlay;
};

// Maps the result a screen closed with to the wrapper's original return value.
using JonesUiModalResultMapper = std::function<int(babylon::ui::ResultCode)>;

// Consumes one Ui event before the loop applies its default handling. Return true when the event
// was handled; return false to let the loop pop a button action or report the close as usual. A
// handler may push and run a nested screen through the same Ui, or pop the top screen itself.
using JonesUiModalEventHandler = std::function<bool(babylon::ui::Ui& ui,
    babylon::ui::ScreenId screen, const babylon::ui::UiEvent& event)>;

// Pushes a copy of screen on top of ui's screen stack and runs until that screen closes. pPainter is
// kept for source compatibility and may be the painter ui draws through or nullptr; the menu is
// drawn by the JonesUiContext present overlay, so the loop itself draws no UI. When background
// names an image, the overlay cover-scales it across the canvas and does not dim it; when draw is
// set, the loop draws it through the game renderer before the present. A loop outside a context
// only pumps input (the unit tests). Returns mapResult(result); a screen Ui::push rejects returns
// mapResult(screen.backResult) without entering the loop. Requires the std Babylon adapter context
// (the event pump and the platform clock), like every other Babylon-column loop.
int JonesUiModal_Run(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    const babylon::ui::Screen& screen, const JonesUiModalResultMapper& mapResult,
    const JonesUiModalBackground& background = JonesUiModalBackground{});

// JonesUiModal_Run with an event handler for screens that need more than one step, such as an
// overwrite prompt opened after a list row is accepted. handler is called for every Ui event
// before the loop's own action and close handling; nullptr runs exactly like JonesUiModal_Run.
// resizeHandler, when set, is called after a window resize has been applied to the Ui. The fonts
// now re-bake inside the painter's begin(), so the game screens pass none.
using JonesUiModalResizeHandler = std::function<void(int width, int height)>;

int JonesUiModal_RunHandled(babylon::ui::Ui& ui, babylon::ui::Painter* pPainter,
    const babylon::ui::Screen& screen, const JonesUiModalResultMapper& mapResult,
    const JonesUiModalEventHandler& handler,
    const JonesUiModalBackground& background = JonesUiModalBackground{},
    const JonesUiModalResizeHandler& resizeHandler = nullptr);

#endif // JONES3D_PLATFORM_BABYLON_JONESUIMODAL_H
